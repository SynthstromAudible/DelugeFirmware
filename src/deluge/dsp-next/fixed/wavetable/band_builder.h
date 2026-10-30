/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "dsp-next/fixed/fft/complex.h"
#include "dsp-next/fixed/fft/real_fft.h"

#include <bit>
#include <cstdint>
#include <span>
#include <type_traits>

namespace deluge::dsp::fixed {

/// Bits of headroom a source sample loses on its way into the transforms: a cycle buffer value is the sample's Q31
/// value shifted right by this.
inline constexpr int32_t kCycleHeadroomBits = 12;

/// @brief A wavetable's band layout: band 0's cycle and, one octave apart down to 8 samples, every other band's. The
///        one place it is computed: WaveTable::setup and the band builder's spec both read it.
struct BandGeometry {
	int32_t band0_magnitude; ///< log2 of band 0's cycle: the source cycle's when it is a power of two, else the next
	                         ///< power of two above it (so the spectrum fits)
	bool source_is_power_of_two;

	/// @param source_cycle_size samples per source cycle, ≥ kWavetableMinCycleSize, already clamped to the source
	/// length
	[[nodiscard]] static constexpr BandGeometry of(int32_t source_cycle_size) {
		const auto size = static_cast<uint32_t>(source_cycle_size);
		const bool pot = std::has_single_bit(size);
		return {static_cast<int32_t>(std::bit_width(size)) - 1 + (pot ? 0 : 1), pot};
	}
	/// @brief One band per octave down to 8 samples (band0_magnitude - 2), or 1 when band 0's transform is missing.
	/// @param band0_has_transform whether band 0's RealFft (at band0_magnitude) is available
	[[nodiscard]] constexpr int32_t num_bands(bool band0_has_transform) const {
		return band0_has_transform ? band0_magnitude - 2 : 1;
	}
	/// @brief log2 of band @p band's cycle size.
	[[nodiscard]] constexpr int32_t cycle_magnitude(int32_t band) const { return band0_magnitude - band; }
	/// @brief Band @p band's cycle size, without its duplicates.
	[[nodiscard]] constexpr int32_t cycle_size(int32_t band) const { return int32_t{1} << cycle_magnitude(band); }
	/// @brief The highest phase increment band @p band plays: a full-scale step over its cycle, with 1.25× headroom.
	[[nodiscard]] constexpr uint32_t max_phase_increment(int32_t band) const {
		return static_cast<uint32_t>(static_cast<uint32_t>(0xFFFFFFFF >> cycle_magnitude(band)) * 1.25);
	}
};

static_assert(BandGeometry::of(8).band0_magnitude == 3 && BandGeometry::of(8).source_is_power_of_two
              && BandGeometry::of(8).num_bands(true) == 1);
static_assert(BandGeometry::of(16).band0_magnitude == 4 && BandGeometry::of(16).num_bands(true) == 2
              && BandGeometry::of(16).cycle_size(1) == 8);
static_assert(BandGeometry::of(600).band0_magnitude == 10 && !BandGeometry::of(600).source_is_power_of_two
              && BandGeometry::of(600).num_bands(true) == 8 && BandGeometry::of(600).cycle_size(7) == 8);
static_assert(BandGeometry::of(8192).band0_magnitude == 13 && BandGeometry::of(8192).num_bands(true) == 11
              && BandGeometry::of(8192).num_bands(false) == 1);

/// @brief Where one band's cycles go.
struct BandTarget {
	int16_t* samples;        ///< cycle 0's first sample; cycle c starts at c × (2^cycle_magnitude + duplicates)
	int32_t cycle_magnitude; ///< log2 of the band's cycle size
};

/// @brief Builds a wavetable's bands from its source cycles, one cycle at a time: band 0 straight from the cycle when
///        the cycle is a power of two, every other band (and band 0 otherwise) from the cycle's spectrum through an
///        inverse transform, saturated to 16 bits, each cycle followed by its duplicates. Writes into storage it is
///        lent; allocates nothing; the caller yields between calls.
class BandBuilder {
public:
	struct Settings {
		int32_t source_cycle_size; ///< samples per cycle in the source
		int32_t band0_magnitude;   ///< log2 of band 0's cycle: the source's, or the next power of two above it
		int32_t num_cycles;
	};

	/// @param cycle    at least max(source size, band 0 size) values: the source cycle in, each inverse transform out
	/// @param spectrum 2^(band0_magnitude-1)+1 bins
	BandBuilder(Settings settings, std::span<int32_t> cycle, std::span<fft::Complex> spectrum);

	/// @brief Where the reader puts each source cycle: source_cycle_size values, each Q31 >> kCycleHeadroomBits.
	[[nodiscard]] std::span<int32_t> cycle_input() const { return cycle_.first(settings_.source_cycle_size); }
	/// @brief Band 0 of a power-of-two source: cycle @p cycle_index's samples, each the cycle value >> (16 −
	///        kCycleHeadroomBits), and its duplicates.
	void copy_band0(BandTarget band0, int32_t cycle_index);
	/// @brief The cycle's spectrum: @p forward of it, or the DFT when @p forward is null (a source that is not a power
	///        of two, whose bins above its own Nyquist are then cleared to band 0's).
	void analyse(const fft::RealFft* forward);
	/// @brief One band's cycle @p cycle_index from the spectrum: the band's Nyquist bin folded into its real part when
	///        the band is smaller than band 0, @p inverse, saturated to 16 bits after shifting down by
	///        16 − kCycleHeadroomBits + band0_magnitude, then its duplicates.
	void render_band(BandTarget band, int32_t cycle_index, const fft::RealFft& inverse);
	/// @brief One past the last cycle a band's range covers once @p cycle_index is built: that cycle and the next,
	/// which a
	///        morph from it reads, within the table. The range always starts at 0.
	[[nodiscard]] int32_t to_cycle_after(int32_t cycle_index) const;

private:
	Settings settings_;
	std::span<int32_t> cycle_;
	std::span<fft::Complex> spectrum_;
};
static_assert(!std::is_polymorphic_v<BandBuilder>);

} // namespace deluge::dsp::fixed
