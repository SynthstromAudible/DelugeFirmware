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

#include "dsp-next/core/fixed_point_neon.h" // QVec
#include "dsp-next/core/stereo_sample.h"    // StereoSample
#include <argon.hpp>
#include <array>
#include <cstdint>
#include <span>

/// @file
/// @brief The SIMD form of stereo frames, kept apart from types.h so that code naming only the block types needs
/// no Argon.
namespace deluge::dsp {

/// @brief A vector's worth of stereo frames, one Q31 vector per channel.
/// @details The SIMD stereo form of a @ref StereoSample component: its `render` takes and returns a
/// StereoVector, and the mixins fill a block of interleaved frames from it, de-interleaving on load and
/// re-interleaving on store. Lane `i` of @ref l and of @ref r belong to the same frame.
struct StereoVector {
	QVec<31> l; ///< The left channel of each frame.
	QVec<31> r; ///< The right channel of each frame.
};

namespace detail {

// The casts below view frames as their raw words, which Argon's interleaved loads and stores take: the layout is
// asserted in stereo_sample.h, and the app's interleaved buffers are int32 storage to begin with, so both views name
// the same int32 objects.

/// @brief The interleaved raw words of a block of stereo frames (left, right, left, ...).
/// @param frames The block.
/// @return A pointer to its first sample.
[[nodiscard, gnu::always_inline]] inline const std::int32_t* interleaved(std::span<const StereoSample> frames) {
	return reinterpret_cast<const std::int32_t*>(frames.data());
}

/// @brief The interleaved raw words of a writable block of stereo frames (left, right, left, ...).
/// @param frames The block.
/// @return A pointer to its first sample.
[[nodiscard, gnu::always_inline]] inline std::int32_t* interleaved(std::span<StereoSample> frames) {
	return reinterpret_cast<std::int32_t*>(frames.data());
}

/// @brief The channels of an interleaved stereo load (`step.Load` in `for_each_interleaved<std::int32_t, 2>`) as a
/// StereoVector.
/// @details A bit cast rather than element access or a structured binding, so that the step makes no accessor call
/// whatever the inliner decides; this runs once per step.
/// @param channels The left and right vectors, in that order.
/// @return The same two vectors as a StereoVector.
[[nodiscard, gnu::always_inline]] inline StereoVector to_stereo(const std::array<Argon<std::int32_t>, 2>& channels) {
	static_assert(sizeof(StereoVector) == sizeof(channels));
	return __builtin_bit_cast(StereoVector, channels);
}

} // namespace detail

} // namespace deluge::dsp
