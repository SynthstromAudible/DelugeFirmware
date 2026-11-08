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

#include "board_config.h"

#include <array>
#include <numbers>

namespace deluge::dsp::fp {

namespace detail {
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): written once, by build_saw_bands().
// The 90 KB set lives in SDRAM in every image: on-chip it did not fit the bench image's link. It is all-zero until
// build_saw_bands() fills it at run time, so placing it in the zeroed .sdram_bss section is legal.
PLACE_SDRAM_BSS std::array<f32, kSawBands*(kSawBandSize + 1)> saw_band_storage;
bool saw_bands_built = false;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace detail

namespace {

constexpr std::size_t kBandStride = kSawBandSize + 1;
constexpr std::size_t kQuarter = kSawBandSize / 4;

/// cos and sin of 2π/kSawBandSize, as the nearest doubles.
constexpr double kStepCos = 0.9999952938095762;
constexpr double kStepSin = 0.003067956762965976;

/// sin(2π k / kSawBandSize) for k = 0 … kSawBandSize / 4, from basic double arithmetic only.
/// Rotating (cos, sin) by the step angle k times accumulates a rounding error of about k · 1e-16, so every entry is
/// within 1e-13 of the true sine; the top entry is set to its exact value, 1.
struct QuarterSine {
	std::array<double, kQuarter + 1> value{};

	QuarterSine() {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		double c = 1.0;
		double s = 0.0;
		for (std::size_t k = 1; k < kQuarter; ++k) {
			const double next_c = (c * kStepCos) - (s * kStepSin);
			const double next_s = (s * kStepCos) + (c * kStepSin);
			c = next_c;
			s = next_s;
			value[k] = s;
		}
		value[kQuarter] = 1.0;
	}

	/// sin(2π k / kSawBandSize) for any k below kSawBandSize, by the sine's symmetries.
	[[nodiscard]] double at(std::size_t k) const {
		if (k >= kSawBandSize / 2) {
			return -at(k - (kSawBandSize / 2));
		}
		return value[k <= kQuarter ? k : (kSawBandSize / 2) - k];
	}
};

} // namespace

void build_saw_bands() {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	if (detail::saw_bands_built) {
		return;
	}
	static const QuarterSine sine;
	const double scale = -2.0 / std::numbers::pi;
	for (std::size_t i = 0; i < kBandStride; ++i) {
		// Band 10 holds harmonic 1; band b adds the harmonics above 2^(9 − b) up to 2^(10 − b).
		double sum = 0.0;
		std::size_t h = 1;
		for (std::size_t band = kSawBands; band-- > 0;) {
			const std::size_t top = std::size_t{1} << (kSawBands - 1 - band);
			for (; h <= top; ++h) {
				sum += sine.at((h * i) % kSawBandSize) / static_cast<double>(h);
			}
			detail::saw_band_storage[(band * kBandStride) + i] = static_cast<f32>(scale * sum);
		}
	}
	detail::saw_bands_built = true;
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
