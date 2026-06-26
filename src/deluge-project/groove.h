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

/// Groove / swing settings of a song. A component of deluge::project::Song.
/// (Grouped struct for now; methods/value-semantics polish comes later, inside the library.)
struct Groove {
	int8_t swing_amount{0};    ///< -49..49; 0 = no swing
	uint8_t swing_interval{0}; ///< sync level the swing is applied at (set from defaults by the controller)

	/// Whether any swing is applied.
	[[nodiscard]] bool hasSwing() const { return swing_amount != 0; }
};

} // namespace deluge::project
