/*
 * Copyright © 2020-2026 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp" // kInterpolationMaxNumSamples
#include <argon.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {

/// Number of 8-lane int16 vectors that hold one interpolation window (kInterpolationMaxNumSamples taps).
inline constexpr std::size_t kSincWindowVectors = kInterpolationMaxNumSamples / Argon<int16_t>::lanes;

/// One interpolation window — the @ref kInterpolationMaxNumSamples samples to weight, as int16 vectors.
using SincWindow = std::array<Argon<int16_t>, kSincWindowVectors>;

/// @brief Apply a windowed-sinc interpolation kernel to a sample window (argon; the shared core of
/// the wavetable render and the sample-playback Interpolator).
///
/// @param kernel_row the first of two adjacent kernel rows — each @ref kInterpolationMaxNumSamples
/// taps, the next row one window later in memory. The kernel table is discretised into a fixed
/// number of sub-sample rows; this picks the lower one.
/// @param row_blend the Q15 fraction (`[0, 0x7FFF]`) to interpolate from @p kernel_row toward the
/// next row — i.e. the sub-sample position between the two table rows (`a + (b - a) · row_blend`),
/// so the kernel itself is positioned to sub-sample accuracy.
/// @param samples the window to weight.
/// @returns the interpolated sample (the dot product of the blended kernel with @p samples).
///
/// This is the argon form of the legacy hand-written NEON: `MultiplyAddFixedQMax` is
/// `vaddq(a, vqdmulhq_n(b - a, row_blend))`, `MultiplyAddLong` is `vmlal`, `ReduceAdd` the final
/// horizontal add — bit-identical (a sinc_interpolation_spec pins it against a raw-NEON reference).
[[gnu::always_inline]] inline int32_t windowed_sinc_apply(const int16_t* kernel_row, int16_t row_blend,
                                                          const SincWindow& samples) {
	SincWindow kernel;
	for (std::size_t i = 0; i < kSincWindowVectors; ++i) {
		Argon<int16_t> row = Argon<int16_t>::Load(kernel_row + i * Argon<int16_t>::lanes);
		Argon<int16_t> next_row =
		    Argon<int16_t>::Load(kernel_row + kInterpolationMaxNumSamples + i * Argon<int16_t>::lanes);
		kernel[i] = row.MultiplyAddFixedQMax(next_row - row, row_blend);
	}

	Argon<int32_t> accumulated = 0;
	for (std::size_t i = 0; i < kSincWindowVectors; ++i) {
		accumulated = accumulated.MultiplyAddLong(kernel[i].GetLow(), samples[i].GetLow())
		                  .MultiplyAddLong(kernel[i].GetHigh(), samples[i].GetHigh());
	}
	return accumulated.ReduceAdd();
}

} // namespace deluge::dsp::fixed
