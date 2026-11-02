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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/q31_ops.h" // q31_t, multiply_32x32_rshift32
#include <cstddef>
#include <type_traits>

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
	[[nodiscard]] static constexpr StereoSample from_mono(q31_t mono) { return {.l = mono, .r = mono}; }

	/// @brief @p a + @p b in each channel, wrapping modulo 2^32 (an `add` per channel, with defined overflow).
	[[nodiscard]] friend constexpr StereoSample operator+(StereoSample a, StereoSample b) {
		return {.l = detail::wrap_add(a.l, b.l), .r = detail::wrap_add(a.r, b.r)};
	}
	/// @brief Adds @p rhs in each channel, wrapping modulo 2^32.
	constexpr StereoSample& operator+=(StereoSample rhs) {
		l = detail::wrap_add(l, rhs.l);
		r = detail::wrap_add(r, rhs.r);
		return *this;
	}
};

static_assert(std::is_standard_layout_v<StereoSample> && sizeof(StereoSample) == 2 * sizeof(q31_t)
                  && offsetof(StereoSample, l) == 0 && offsetof(StereoSample, r) == sizeof(q31_t),
              "a block of StereoSample frames is an interleaved array of q31, left first");

/// @brief A mono sample panned to a stereo frame: each channel is @p mono scaled by its amplitude.
/// @details Q31 × Q30 is Q29 (`smmul`), reformatted to Q31 by `<< 2`, wrapping. Accumulate with `+=`. The multiply
/// is q31_ops.h's inline asm, not fixed_point.h's `mul`: in the voice's per-sample loops the typed form folds the
/// shift into the accumulate but reallocates their registers, so it moves with the voice kernel, benched.
/// @param mono the sample, Q31.
/// @param amp_l,amp_r per-channel amplitudes, Q30: unity is 2^30.
[[nodiscard, gnu::always_inline]] inline StereoSample panned_mono(q31_t mono, Q<30> amp_l, Q<30> amp_r) {
	return {.l = multiply_32x32_rshift32(mono, amp_l.raw()) << 2, .r = multiply_32x32_rshift32(mono, amp_r.raw()) << 2};
}

/// @brief A stereo sample with each channel scaled by its amplitude, as @ref panned_mono. Accumulate with `+=`.
/// @param left,right the channels, Q31.
/// @param amp_l,amp_r per-channel amplitudes, Q30: unity is 2^30.
[[nodiscard, gnu::always_inline]] inline StereoSample panned_stereo(q31_t left, q31_t right, Q<30> amp_l, Q<30> amp_r) {
	return {.l = multiply_32x32_rshift32(left, amp_l.raw()) << 2,
	        .r = multiply_32x32_rshift32(right, amp_r.raw()) << 2};
}

} // namespace deluge::dsp
