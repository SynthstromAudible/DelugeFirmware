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

#include "dsp-next/core/sample.h"        // q31
#include "dsp-next/core/stereo_sample.h" // StereoSample
#include <argon.hpp>
#include <array>
#include <span>

/// @file
/// @brief The SIMD form of stereo frames, kept apart from types.h so that code naming only the block types needs
/// no Argon.
namespace deluge::dsp {

/// @brief A vector's worth of stereo frames, one q31 vector per channel.
/// @details The SIMD stereo form of a @ref StereoSample component: its `render` takes and returns a
/// StereoVector, and the mixins fill a block of interleaved frames from it, de-interleaving on load and
/// re-interleaving on store. Lane `i` of @ref l and of @ref r belong to the same frame.
struct StereoVector {
	Argon<q31> l; ///< The left channel of each frame (q31).
	Argon<q31> r; ///< The right channel of each frame (q31).
};

namespace detail {

// The casts below view frames as their q31 samples: the layout is asserted in stereo_sample.h, and the app's
// interleaved buffers are int32 storage to begin with, so both views name the same int32 objects.

/// @brief The interleaved q31 samples of a block of stereo frames (left, right, left, ...).
/// @param frames The block.
/// @return A pointer to its first sample.
[[nodiscard, gnu::always_inline]] inline const q31* interleaved(std::span<const StereoSample> frames) {
	return reinterpret_cast<const q31*>(frames.data());
}

/// @brief The interleaved q31 samples of a writable block of stereo frames (left, right, left, ...).
/// @param frames The block.
/// @return A pointer to its first sample.
[[nodiscard, gnu::always_inline]] inline q31* interleaved(std::span<StereoSample> frames) {
	return reinterpret_cast<q31*>(frames.data());
}

/// @brief The channels of an interleaved stereo load (`step.Load` in `for_each_interleaved<q31, 2>`) as a
/// StereoVector.
/// @details A bit cast rather than element access or a structured binding: std::array's accessors and `std::get`
/// are calls under `-fno-inline-functions`, and this runs once per step.
/// @param channels The left and right vectors, in that order.
/// @return The same two vectors as a StereoVector.
[[nodiscard, gnu::always_inline]] inline StereoVector to_stereo(const std::array<Argon<q31>, 2>& channels) {
	static_assert(sizeof(StereoVector) == sizeof(channels));
	return __builtin_bit_cast(StereoVector, channels);
}

} // namespace detail

} // namespace deluge::dsp
