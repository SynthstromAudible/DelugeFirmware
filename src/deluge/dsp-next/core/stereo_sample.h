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
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace deluge::dsp {

/// @brief One interleaved stereo audio frame: a left/right pair of Q31 samples.
///
/// A trivial value type (an aggregate of two Q<31>). It deliberately carries only value semantics — a named
/// mono constructor and frame addition; everything that *operates on* frames (panning, VCA, mixing) is a free
/// function or a dedicated component, not a member, so the data frame stays decoupled from the DSP. (Successor to
/// the legacy app `::StereoSample`, which had accumulate/pan helpers bolted on; those become the free functions
/// below + plain `+=`.)
struct StereoSample {
	Q<31> l; ///< The left channel.
	Q<31> r; ///< The right channel.

	/// A frame with both channels set to @p mono.
	[[nodiscard]] static constexpr StereoSample from_mono(Q<31> mono) { return {.l = mono, .r = mono}; }

	/// @brief @p a + @p b in each channel: add_wrap per channel, a frame's mix.
	/// @param a,b The frames to add.
	/// @return Their sum.
	[[nodiscard]] friend constexpr StereoSample operator+(StereoSample a, StereoSample b) {
		return {.l = add_wrap(a.l, b.l), .r = add_wrap(a.r, b.r)};
	}
	/// @brief Adds @p rhs in each channel: add_wrap per channel.
	/// @param rhs The frame to add.
	/// @return This frame.
	constexpr StereoSample& operator+=(StereoSample rhs) {
		l = add_wrap(l, rhs.l);
		r = add_wrap(r, rhs.r);
		return *this;
	}
};

static_assert(std::is_standard_layout_v<StereoSample> && sizeof(StereoSample) == 2 * sizeof(std::int32_t)
                  && offsetof(StereoSample, l) == 0 && offsetof(StereoSample, r) == sizeof(std::int32_t),
              "a block of StereoSample frames is an interleaved array of q31, left first");

/// @brief A mono sample panned to a stereo frame: each channel is @p mono scaled by its amplitude.
/// @details Q31 × Q30 is Q29 (`smmul`), reformatted to Q31 by `<< 2`, wrapping. Accumulate with `+=`.
/// @param mono the sample.
/// @param amp_l,amp_r per-channel amplitudes: unity is 1.0.
[[nodiscard, gnu::always_inline]] inline StereoSample panned_mono(Q<31> mono, Q<30> amp_l, Q<30> amp_r) {
	return {.l = mul_as_wrap<Q<31>>(mono, amp_l), .r = mul_as_wrap<Q<31>>(mono, amp_r)};
}

/// @brief A stereo sample with each channel scaled by its amplitude, as @ref panned_mono. Accumulate with `+=`.
/// @param left,right the channels.
/// @param amp_l,amp_r per-channel amplitudes: unity is 1.0.
[[nodiscard, gnu::always_inline]] inline StereoSample panned_stereo(Q<31> left, Q<31> right, Q<30> amp_l, Q<30> amp_r) {
	return {.l = mul_as_wrap<Q<31>>(left, amp_l), .r = mul_as_wrap<Q<31>>(right, amp_r)};
}

} // namespace deluge::dsp
