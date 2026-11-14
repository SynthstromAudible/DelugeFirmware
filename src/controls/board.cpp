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

#include "controls/board.h"

namespace deluge::controls {

std::optional<Logical> map_input(const Board& board, Physical input) {
	switch (input.kind) {
	case Physical::Kind::SECTION_BUTTON:
		if (input.index < board.sections) {
			return Logical{.kind = Logical::Kind::SECTION, .index = input.index};
		}
		break;
	case Physical::Kind::SLOT_ENCODER:
		if (input.index < board.slots) {
			return Logical{.kind = Logical::Kind::SLOT, .index = input.index};
		}
		break;
	case Physical::Kind::SLOT_PRESS:
		if (board.slot_press && input.index < board.slots) {
			return Logical{.kind = Logical::Kind::SLOT_PRESS, .index = input.index};
		}
		break;
	}
	return std::nullopt;
}

} // namespace deluge::controls
