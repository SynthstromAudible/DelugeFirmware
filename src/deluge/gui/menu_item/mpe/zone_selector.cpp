/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "zone_selector.h"
#include "zone_num_member_channels.h"

namespace deluge::gui::menu_item::mpe {

PLACE_SDRAM_BSS ZoneSelector zoneSelectorMenu{};

void ZoneSelector::beginSession(MenuItem* navigatedBackwardFrom) {
	if (!navigatedBackwardFrom) {
		whichZone = 0;
	}
	Selection::beginSession(navigatedBackwardFrom);
}

MenuItem* ZoneSelector::selectButtonPress() {
	return &zoneNumMemberChannelsMenu;
}
} // namespace deluge::gui::menu_item::mpe
