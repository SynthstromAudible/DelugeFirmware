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

#include "util/fixedpoint.h" // q31_t, multiply_32x32_rshift32

namespace deluge::dsp {

/// @brief One interleaved stereo audio frame: a left/right pair of q31 samples.
///
/// A trivial value type (an aggregate of two @ref q31_t). It deliberately carries only value semantics — a named
/// mono constructor and frame addition; everything that *operates on* frames (panning, VCA, mixing) is a free
/// function or a dedicated component, not a member, so the data frame stays decoupled from the DSP. (Successor to
/// the legacy app `::StereoSample`, which had accumulate/pan helpers bolted on; those become the free functions
/// below + plain `+=`.)
struct StereoSample {
	q31_t l;
	q31_t r;

	/// A frame with both channels set to @p mono.
	[[nodiscard]] static constexpr StereoSample fromMono(q31_t mono) { return {.l = mono, .r = mono}; }

	[[nodiscard]] friend constexpr StereoSample operator+(StereoSample a, StereoSample b) {
		return {.l = a.l + b.l, .r = a.r + b.r};
	}
	constexpr StereoSample& operator+=(StereoSample rhs) {
		l += rhs.l;
		r += rhs.r;
		return *this;
	}
};

/// @brief A mono sample panned to a stereo frame: each channel is @p mono scaled by its amplitude.
/// @param ampL,ampR per-channel amplitudes (~Q2.29 — the `<< 2` restores unity headroom). Accumulate with `+=`.
[[nodiscard, gnu::always_inline]] inline StereoSample pannedMono(q31_t mono, int32_t ampL, int32_t ampR) {
	return {.l = multiply_32x32_rshift32(mono, ampL) << 2, .r = multiply_32x32_rshift32(mono, ampR) << 2};
}

/// @brief A stereo sample with each channel scaled by its amplitude (~Q2.29, see @ref pannedMono). Accumulate with
/// `+=`.
[[nodiscard, gnu::always_inline]] inline StereoSample pannedStereo(q31_t left, q31_t right, int32_t ampL,
                                                                   int32_t ampR) {
	return {.l = multiply_32x32_rshift32(left, ampL) << 2, .r = multiply_32x32_rshift32(right, ampR) << 2};
}

} // namespace deluge::dsp
