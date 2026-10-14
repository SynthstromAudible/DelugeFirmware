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

#include <algorithm>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief The amplitudes of the two channels at a pan position, Q30 (1073741823 is unity).
struct PanAmplitudes {
	int32_t left;
	int32_t right;
};

/// @brief The amplitudes at @p pan: the channel the pan moves away from falls linearly to silence; the other stays
/// at unity.
///
/// @param pan Pan position, Q31: 0 is the centre; +2^30 and beyond is fully right, -2^30 and beyond fully left.
/// @return The left and right channel amplitudes, Q30.
[[nodiscard]] constexpr PanAmplitudes pan_amplitudes(int32_t pan) {
	constexpr int32_t kUnity = 1073741823;
	constexpr int32_t kHalfRange = 1073741824;
	if (pan == 0) {
		return {.left = kUnity, .right = kUnity};
	}
	const int32_t offset = std::clamp(pan, -kHalfRange, kHalfRange);
	return {.left = pan <= 0 ? kUnity : kHalfRange - offset, .right = pan >= 0 ? kUnity : kHalfRange + offset};
}

} // namespace deluge::dsp::fixed
