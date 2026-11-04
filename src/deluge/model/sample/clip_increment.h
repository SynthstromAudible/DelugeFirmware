/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include <cstdint>
#include <limits>

/// @file
/// @brief An audio clip's phase increments: Q24 of the engine's sample rate (2^24 plays a sample at the engine's rate),
/// held under 2^31, the most an increment holds.

/// @brief @p increment scaled by the Q24 @p ratio: the 64-bit product's Q24, held under 2^31, the product itself held
/// where a ratio past 2^32 would carry it past 64 bits.
[[nodiscard]] constexpr int32_t scaled_increment(uint32_t increment, uint64_t ratio) {
	if ((ratio >> 32) != 0 && increment != 0 && ratio > std::numeric_limits<uint64_t>::max() / increment) {
		return std::numeric_limits<int32_t>::max();
	}
	const uint64_t scaled = (uint64_t{increment} * ratio) >> 24;
	return scaled > std::numeric_limits<int32_t>::max() ? std::numeric_limits<int32_t>::max()
	                                                    : static_cast<int32_t>(scaled);
}
