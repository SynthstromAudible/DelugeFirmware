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

#include "dsp-next/core/types.h"
#include "util/fixedpoint.h"

#include <cstdint>

/// @file
/// @brief duck_gain: a sidechain's duck as the squared level its owner's volume takes.
namespace deluge::dsp::fixed {

/// @brief The output scale of @ref duck_gain: one law at two owners' volume scales.
enum class DuckScale : uint8_t {
	CLIP,          ///< A clip chain's post-reverb volume: 134217728 (2^27) unducked.
	REVERB_RETURN, ///< The reverb's return volume: 2^29 unducked.
};

/// @brief The level the duck offsets from before squaring, Q31: a quarter, 2^29.
inline constexpr q31 kDuckGainNeutral = 1 << 29;
/// @brief The shift on the first factor of the square, at either scale.
inline constexpr int kDuckGainFirstShift = 15;

/// @brief The duck scaled by its depth and offset to the neutral level, then squared at @p S's scale.
/// @details p = (duck × depth) ≫ 32 + 2^29; CLIP gives (p ≫ 15)(p ≫ 16), REVERB_RETURN (p ≫ 15)(p ≫ 14). Each factor
/// is truncated before the multiply, as both owners have always squared it.
///
/// **Wrapping.** The square is taken in unsigned arithmetic and reinterpreted, as the target's `mul` gives it: a duck
/// above zero or a negative depth lifts p toward 2^30, where REVERB_RETURN's square reaches 2^31 and wraps.
/// @tparam S The owner's scale.
/// @param duck The duck, Q31: 0 is none, negative ducks.
/// @param depth The depth, Q29: the sidechain volume as a patch cable's amount.
/// @return The squared level at @p S's scale.
template <DuckScale S>
[[nodiscard]] [[gnu::always_inline]] inline q31 duck_gain(q31 duck, q31 depth) {
	constexpr int kSecondShift = S == DuckScale::CLIP ? 16 : 14;
	const q31 p = multiply_32x32_rshift32(duck, depth) + kDuckGainNeutral;
	return static_cast<q31>(static_cast<uint32_t>(p >> kDuckGainFirstShift) * static_cast<uint32_t>(p >> kSecondShift));
}

} // namespace deluge::dsp::fixed
