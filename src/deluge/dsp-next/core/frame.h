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

#include "dsp-next/core/sample.h"
#include "dsp-next/core/stereo_sample.h"

#include <cstddef>
#include <type_traits>

/// @file
/// @brief Frame: one sample from each of N independent streams, interleaved.
namespace deluge::dsp {

/// @brief N samples, one per lane: a block of frames is N independent streams interleaved, lane 0 first in each frame.
/// @details A trivially copyable aggregate whose layout is N @p T's in lane order, so `Frame<q31, 2>` has the layout of
/// @ref StereoSample, left in lane 0.
/// @tparam T The sample type: q31 for the fixed-point core.
/// @tparam N The number of lanes. At least 1.
template <class T, std::size_t N>
requires(N >= 1)
struct Frame {
	// A built-in array, not std::array: the layout is the N samples alone, as an interleaved block requires.
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	T lane[N]; ///< The samples, lane 0 first.

	/// @brief Lane @p i's sample. @p i is less than N.
	[[nodiscard]] [[gnu::always_inline]] constexpr T& operator[](std::size_t i) { return lane[i]; }
	/// @brief Lane @p i's sample. @p i is less than N.
	[[nodiscard]] [[gnu::always_inline]] constexpr const T& operator[](std::size_t i) const { return lane[i]; }
};

static_assert(sizeof(Frame<q31, 2>) == sizeof(StereoSample) && alignof(Frame<q31, 2>) == alignof(StereoSample)
                  && std::is_trivially_copyable_v<Frame<q31, 2>>,
              "a two-lane frame of q31 has a stereo frame's layout, so the two convert by bit cast");

/// @brief A stereo frame as two lanes: left in lane 0, right in lane 1, Q31 unchanged.
[[nodiscard]] [[gnu::always_inline]] constexpr Frame<q31, 2> to_frame(StereoSample s) {
	return __builtin_bit_cast(Frame<q31, 2>, s);
}

/// @brief Two lanes as a stereo frame: lane 0 to left, lane 1 to right, Q31 unchanged.
[[nodiscard]] [[gnu::always_inline]] constexpr StereoSample to_stereo(Frame<q31, 2> f) {
	return __builtin_bit_cast(StereoSample, f);
}

} // namespace deluge::dsp
