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
#include "dsp-next/float/osc/saw_bands.h"

#include <array>
#include <cmath>
#include <numbers>

namespace deluge::dsp::fp {

namespace {

constexpr std::size_t kBandStride = kSawBandSize + 1;

/// The set, filled once by build() on first use rather than at static initialisation, which a boot would pay for.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): written once, by build().
std::array<f32, kSawBands * kBandStride> storage;

bool build() {
	for (std::size_t band = 0; band < kSawBands; ++band) {
		const std::size_t top = std::size_t{1} << (kSawBands - 1 - band);
		for (std::size_t i = 0; i < kBandStride; ++i) {
			// The last sample is the first's cycle position again: i = kSawBandSize wraps to 0.
			const std::size_t at = i % kSawBandSize;
			double sum = 0.0;
			for (std::size_t h = 1; h <= top; ++h) {
				// (h · at) mod kSawBandSize keeps the sine's argument exact for the large harmonics.
				const double cycles = static_cast<double>((h * at) % kSawBandSize) / static_cast<double>(kSawBandSize);
				sum += std::sin(2.0 * std::numbers::pi * cycles) / static_cast<double>(h);
			}
			storage[(band * kBandStride) + i] = static_cast<f32>(-(2.0 / std::numbers::pi) * sum);
		}
	}
	return true;
}

} // namespace

const f32* saw_bands() {
	static const bool built = build();
	(void)built;
	return storage.data();
}

std::size_t saw_band_for(f32 inc) {
	std::size_t band = 0;
	// Each test multiplies by a power of two, so it is exact.
	while (band < kSawBands - 1 && static_cast<f32>(std::size_t{1} << (kSawBands - 1 - band)) * inc >= 0.5f) {
		++band;
	}
	return band;
}

} // namespace deluge::dsp::fp
