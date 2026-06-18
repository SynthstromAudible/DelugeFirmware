/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief One band of a loaded wavetable: the anti-aliased samples for a pitch range, plus the
/// cycle geometry the renderer needs.
///
/// These fields keep their legacy names while the wavetable render loop still lives in storage and
/// reads them by name; they become snake_case when the renderer relocates into dsp-next. Storage's
/// `WaveTableBand` derives from this (adding its load/steal lifecycle), so this is the prefix the
/// renderer sees through @ref WaveTableData.
struct WaveTableBand {
	uint32_t maxPhaseIncrement; ///< highest phase increment (pitch) this band covers; also the ascending sort key.
	int32_t fromCycleNumber;    ///< first wavetable cycle this band holds data for.
	int32_t toCycleNumber;      ///< one past the last wavetable cycle this band holds data for.
	uint16_t cycleSizeNoDuplicates;
	uint8_t cycleSizeMagnitude; ///< log2 of the cycle size; a within-cycle sample index is `phase >> (32 - this)`.
	int16_t* dataAccessAddress; ///< the band's cycle samples (mutable: storage writes them at load).
};

/// @brief A read-only, storage-independent view of a loaded wavetable for the renderer.
///
/// It exposes the render scalars plus indexed band access — pick the band for a pitch with
/// @ref select_band, walk bands for the cross-cycle morph with @ref band — without dsp-next seeing
/// how WaveTable stores them. Bands are addressed by base pointer + stride, so the view spans the
/// current (storage-owned) band array as-is; once WaveTable's bands migrate to a `fast_vector` of
/// @ref WaveTableBand the stride simply becomes `sizeof(WaveTableBand)` and this view is unchanged.
/// (That migration is a deferred, separately-tested task — the stealable-memory lifecycle it
/// touches isn't exercised by the offline golden.)
class WaveTableData {
public:
	/// A null/empty view (`num_bands() == 0`) — for the context slots of non-wavetable sources, which build the
	/// shared @ref deluge::synth::SourceRenderContext but never read the view.
	WaveTableData() = default;

	WaveTableData(int32_t num_cycles, int32_t num_cycle_transitions_magnitude, int32_t wave_index_multiplier,
	              const void* bands_base, std::size_t band_stride, int32_t num_bands)
	    : num_cycles_{num_cycles}, num_cycle_transitions_magnitude_{num_cycle_transitions_magnitude},
	      wave_index_multiplier_{wave_index_multiplier}, bands_base_{bands_base}, band_stride_{band_stride},
	      num_bands_{num_bands} {}

	/// Number of wavetable cycles. > 1 means a morphing (multi-cycle) wavetable; 1 is single-cycle.
	[[nodiscard]] int32_t num_cycles() const { return num_cycles_; }
	/// log2 of the number of cycle transitions, rounded up — the wave-index → cycle-pair scaling.
	[[nodiscard]] int32_t num_cycle_transitions_magnitude() const { return num_cycle_transitions_magnitude_; }
	/// Scales an incoming wave index into the band's cycle space.
	[[nodiscard]] int32_t wave_index_multiplier() const { return wave_index_multiplier_; }
	[[nodiscard]] int32_t num_bands() const { return num_bands_; }

	/// The band at @p index (0 ≤ index < @ref num_bands), in ascending pitch order.
	[[nodiscard]] const WaveTableBand& band(int32_t index) const {
		return *reinterpret_cast<const WaveTableBand*>(static_cast<const std::byte*>(bands_base_)
		                                               + static_cast<std::size_t>(index) * band_stride_);
	}

	/// The band whose pitch range covers @p phase_increment: the first band (ascending
	/// maxPhaseIncrement) at or above it, clamped to the last. Mirrors the legacy
	/// `bands.search(phaseIncrement, GREATER_OR_EQUAL)` + clamp, so it picks the same band.
	[[nodiscard]] int32_t select_band(uint32_t phase_increment) const {
		int32_t index = 0;
		while (index < num_bands_ && band(index).maxPhaseIncrement < phase_increment) {
			++index;
		}
		if (index >= num_bands_) {
			index = num_bands_ - 1;
		}
		return index;
	}

private:
	int32_t num_cycles_ = 0;
	int32_t num_cycle_transitions_magnitude_ = 0;
	int32_t wave_index_multiplier_ = 0;
	const void* bands_base_ = nullptr;
	std::size_t band_stride_ = 0;
	int32_t num_bands_ = 0;
};

} // namespace deluge::dsp::fixed
