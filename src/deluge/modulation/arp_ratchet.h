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

#include "plugin-api/types/sync.h"

#include <algorithm>
#include <cstdint>

/// @brief A ratchet's doublings (0 none, 1 two notes, 2 four, 3 eight) as a sync level allows them: the finer levels
/// leave no room to divide, so 1/128 allows two notes at most and 1/64 four. A draw of no ratchet stays none.
[[nodiscard]] constexpr uint32_t ratchet_doublings(deluge::plugin::SyncLevel level, uint32_t drawn) {
	if (level == deluge::plugin::SyncLevel::ONE_HUNDRED_TWENTY_EIGHTH) {
		return std::min(uint32_t{1}, drawn);
	}
	if (level == deluge::plugin::SyncLevel::SIXTY_FOURTH) {
		return std::min(uint32_t{2}, drawn);
	}
	return drawn;
}
