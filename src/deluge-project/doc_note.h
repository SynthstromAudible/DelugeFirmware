/*
 * Copyright © 2024 Synthstrom Audible Limited
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

namespace deluge::project {

/// The DOM-owned save representation of one engine Note (M3 owner-2 Slice 2c). A flat mirror of the fields the
/// note serializer emits. `iterance` is the packed `Iterance::toInt()` form (reconstruct via `Iterance::fromInt`
/// on the engine side) so this header stays portable (no engine includes). Rebuilt from the engine note vector
/// at save; the engine `Note` stays authoritative at runtime.
struct DocNote {
	int32_t pos{0};
	int32_t length{0};
	uint8_t velocity{0};
	uint8_t probability{0};
	uint16_t iterance{0}; ///< Iterance::toInt() packed form
	uint8_t fill{0};
	uint8_t lift{0}; ///< noteOff velocity
};

} // namespace deluge::project
