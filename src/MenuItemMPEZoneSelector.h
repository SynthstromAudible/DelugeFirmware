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

#ifndef MENUITEMMPEZONESELECTOR_H_
#define MENUITEMMPEZONESELECTOR_H_

#include "MenuItemSelection.h"

class MenuItemMPEZoneSelector final : public MenuItemSelection {
public:
	MenuItemMPEZoneSelector(char const* newName = NULL) : MenuItemSelection(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);
	char const** getOptions();
	void readCurrentValue();
	void writeCurrentValue();
	MenuItem* selectButtonPress();
	uint8_t whichZone;

#if HAVE_OLED
	char const* getTitle(char* buffer);
#endif
};

extern MenuItemMPEZoneSelector mpeZoneSelectorMenu;

#endif /* MENUITEMMPEZONESELECTOR_H_ */
