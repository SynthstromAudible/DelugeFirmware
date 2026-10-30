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

#include "dsp-next/fixed/wavetable/band_builder.h"
#include "dsp-next/fixed/fft/real_dft.h"
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include "util/fast_pythag.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace deluge::dsp::fixed {

namespace {

/// @brief Where cycle @p cycle_index of a band whose cycles are @p cycle_size samples starts.
[[nodiscard, gnu::always_inline]] inline int16_t* cycle_start(int16_t* samples, int32_t cycle_size,
                                                              int32_t cycle_index) {
	return samples + ((cycle_size + kWavetableDuplicateSamplesAtCycleEnd) * cycle_index);
}

/// @brief Repeats a cycle's first samples after it, so the interpolation window can read past its end.
[[gnu::always_inline]] inline void write_duplicates(int16_t* cycle, int32_t cycle_size) {
	for (int32_t i = 0; i < kWavetableDuplicateSamplesAtCycleEnd; i++) {
		cycle[cycle_size + i] = cycle[i];
	}
}

/// @brief Each cycle value, already 2^-kCycleHeadroomBits of its Q31 sample, as the band's 16-bit sample.
[[gnu::always_inline]] inline void narrow_band0(int16_t* out, std::span<const int32_t> cycle) {
	for (std::size_t i = 0; i < cycle.size(); i++) {
		out[i] = static_cast<int16_t>(cycle[i] >> (16 - kCycleHeadroomBits));
	}
}

/// @brief Each inverse transform value, shifted down by @p shift, saturated to 16 bits.
[[gnu::always_inline]] inline void narrow_rendered(int16_t* out, std::span<const int32_t> cycle, int32_t shift) {
	for (std::size_t i = 0; i < cycle.size(); i++) {
		out[i] = static_cast<int16_t>(signed_saturate<32 - 16>(cycle[i] >> shift));
	}
}

} // namespace

BandBuilder::BandBuilder(Settings settings, std::span<int32_t> cycle, std::span<fft::Complex> spectrum)
    : settings_{settings}, cycle_{cycle}, spectrum_{spectrum} {
	assert(cycle.size()
	       >= static_cast<std::size_t>(std::max(settings.source_cycle_size, 1 << settings.band0_magnitude)));
	assert(spectrum.size() >= (std::size_t{1} << (settings.band0_magnitude - 1)) + 1);
}

void BandBuilder::copy_band0(BandTarget band0, int32_t cycle_index) {
	const int32_t size = settings_.source_cycle_size;
	int16_t* out = cycle_start(band0.samples, size, cycle_index);
	narrow_band0(out, cycle_.first(size));
	write_duplicates(out, size);
}

void BandBuilder::analyse(const fft::RealFft* forward) {
	const int32_t band0_size = 1 << settings_.band0_magnitude;
	if (forward != nullptr) {
		forward->forward(cycle_.first(band0_size), spectrum_.first((band0_size >> 1) + 1));
		return;
	}
	const int32_t source_size = settings_.source_cycle_size;
	fft::real_dft(cycle_.first(source_size), spectrum_);
	// Band 0's transform runs over its own length, past the source's Nyquist: those bins are silent.
	for (int32_t i = (source_size >> 1) + 1; i <= (band0_size >> 1); i++) {
		spectrum_[i] = {0, 0};
	}
}

void BandBuilder::render_band(BandTarget band, int32_t cycle_index, const fft::RealFft& inverse) {
	const int32_t size = 1 << band.cycle_magnitude;
	if (band.cycle_magnitude < settings_.band0_magnitude) {
		// A real cycle of this length has no imaginary part at its Nyquist bin: fold the bin's magnitude into its real
		// part, keeping the real part's sign.
		fft::Complex& nyquist = spectrum_[size >> 1];
		int32_t magnitude = fastPythag(nyquist.r, nyquist.i);
		if (nyquist.r < 0) {
			magnitude = -magnitude;
		}
		nyquist = {magnitude, 0};
	}

	inverse.inverse(spectrum_.first((size >> 1) + 1), cycle_.first(size));

	int16_t* out = cycle_start(band.samples, size, cycle_index);
	narrow_rendered(out, cycle_.first(size), 16 - kCycleHeadroomBits + settings_.band0_magnitude);
	write_duplicates(out, size);
}

int32_t BandBuilder::to_cycle_after(int32_t cycle_index) const {
	return std::min(cycle_index + 2, settings_.num_cycles);
}

} // namespace deluge::dsp::fixed
