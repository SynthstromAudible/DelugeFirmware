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

#include <MenuItemMPEDirectionSelector.h>
#include "soundeditor.h"
#include "MenuItemMPEZoneSelector.h"
#include "MIDIDevice.h"

MenuItemMPEDirectionSelector mpeDirectionSelectorMenu;

void MenuItemMPEDirectionSelector::beginSession(MenuItem* navigatedBackwardFrom) {
	if (!navigatedBackwardFrom) whichDirection = MIDI_DIRECTION_INPUT_TO_DELUGE;
	MenuItemSelection::beginSession(navigatedBackwardFrom);
}

char const** MenuItemMPEDirectionSelector::getOptions() {
	static char const* options[] = {"In", "Out", NULL};
	return options;
}

void MenuItemMPEDirectionSelector::readCurrentValue() {
	soundEditor.currentValue = whichDirection;
}

void MenuItemMPEDirectionSelector::writeCurrentValue() {
	whichDirection = soundEditor.currentValue;
}

MenuItem* MenuItemMPEDirectionSelector::selectButtonPress() {
#if HAVE_OLED
	mpeZoneSelectorMenu.basicTitle = whichDirection ? "MPE output" : "MPE input";
#endif
	return &mpeZoneSelectorMenu;
}
