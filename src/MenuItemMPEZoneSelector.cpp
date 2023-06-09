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

#include <MenuItemMPEZoneSelector.h>
#include "MenuItemMPEZoneNumMemberChannels.h"
#include "soundeditor.h"
#include "MenuItemMPEDirectionSelector.h"

MenuItemMPEZoneSelector mpeZoneSelectorMenu;

void MenuItemMPEZoneSelector::beginSession(MenuItem* navigatedBackwardFrom) {
	if (!navigatedBackwardFrom) whichZone = 0;
	MenuItemSelection::beginSession(navigatedBackwardFrom);
}

char const** MenuItemMPEZoneSelector::getOptions() {
	static char const* options[] =
#if HAVE_OLED
	    {"Lower zone", "Upper zone", NULL};
#else
	    {"Lowe", "Uppe", NULL};
#endif
	return options;
}

void MenuItemMPEZoneSelector::readCurrentValue() {
	soundEditor.currentValue = whichZone;
}

void MenuItemMPEZoneSelector::writeCurrentValue() {
	whichZone = soundEditor.currentValue;
}

MenuItem* MenuItemMPEZoneSelector::selectButtonPress() {
	return &mpeZoneNumMemberChannelsMenu;
}
