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
#include <algorithm>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief The amplitudes of the two channels at a pan position (1073741823 is unity).
struct PanAmplitudes {
	Q<30> left;
	Q<30> right;
};

/// @brief The amplitudes at @p pan: the channel the pan moves away from falls linearly to silence; the other stays
/// at unity.
///
/// @param pan Pan position: 0 is the centre; +2^30 (a half) and beyond is fully right, -2^30 and beyond fully left.
/// @return The left and right channel amplitudes.
[[nodiscard]] constexpr PanAmplitudes pan_amplitudes(Q<31> pan) {
	constexpr int32_t kUnity = 1073741823;
	constexpr int32_t kHalfRange = 1073741824;
	if (pan == Q<31>{}) {
		return {.left = Q<30>::from_raw(kUnity), .right = Q<30>::from_raw(kUnity)};
	}
	// Raw: the Q31 offset from the centre is the Q30 amplitude's distance from one, bit for bit
	const int32_t offset = std::clamp(pan.raw(), -kHalfRange, kHalfRange);
	return {.left = Q<30>::from_raw(pan.raw() <= 0 ? kUnity : kHalfRange - offset),
	        .right = Q<30>::from_raw(pan.raw() >= 0 ? kUnity : kHalfRange + offset)};
}

} // namespace deluge::dsp::fixed
