/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "MenuItemSelection.h"
#include "soundeditor.h"
#include "numericdriver.h"

extern "C" {
#if HAVE_OLED
#include <oled_low_level.h>
#endif
}

char const* onOffOptions[] = {"Off", "On", NULL};

MenuItemSelection::MenuItemSelection(char const* newName) : MenuItemValue(newName) {
	basicOptions = onOffOptions;
}

void MenuItemSelection::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItemValue::beginSession(navigatedBackwardFrom);
#if HAVE_OLED
	soundEditor.menuCurrentScroll = 0;
#else
	drawValue();
#endif
}

// Virtual - may be overriden.
char const** MenuItemSelection::getOptions() {
	return basicOptions;
}

int MenuItemSelection::getNumOptions() {
	int i = 0;
	while (basicOptions[i]) {
		i++;
	}
	return i;
}

void MenuItemSelection::selectEncoderAction(int offset) {
	soundEditor.currentValue += offset;
	int numOptions = getNumOptions();

#if HAVE_OLED
	if (soundEditor.currentValue > numOptions - 1) soundEditor.currentValue = numOptions - 1;
	else if (soundEditor.currentValue < 0) soundEditor.currentValue = 0;
#else
	if (soundEditor.currentValue >= numOptions) soundEditor.currentValue -= numOptions;
	else if (soundEditor.currentValue < 0) soundEditor.currentValue += numOptions;
#endif

	MenuItemValue::selectEncoderAction(offset);
}

void MenuItemSelection::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	numericDriver.setText(getOptions()[soundEditor.currentValue]);
#endif
}

#if HAVE_OLED
void MenuItemSelection::drawPixelsForOled() {
	// Move scroll
	if (soundEditor.menuCurrentScroll > soundEditor.currentValue)
		soundEditor.menuCurrentScroll = soundEditor.currentValue;
	else if (soundEditor.menuCurrentScroll < soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1)
		soundEditor.menuCurrentScroll = soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1;

	char const** options = &getOptions()[soundEditor.menuCurrentScroll];
	int selectedOption = soundEditor.currentValue - soundEditor.menuCurrentScroll;

	drawItemsForOled(options, selectedOption);
}
#endif
