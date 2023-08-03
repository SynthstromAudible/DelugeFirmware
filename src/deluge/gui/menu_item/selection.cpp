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

#include "selection.h"
#include "gui/ui/sound_editor.h"
#include "hid/display.h"

extern "C" {
#include "RZA1/oled/oled_low_level.h"
}

namespace menu_item {

char const* onOffOptions[] = {"Off", "On", NULL};

Selection::Selection(char const* newName) : Value(newName) {
	basicOptions = onOffOptions;
}

void Selection::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
	if (display.type == DisplayType::OLED) {
		soundEditor.menuCurrentScroll = 0;
	}
	else {
		drawValue();
	}
}

// Virtual - may be overriden.
char const** Selection::getOptions() {
	return basicOptions;
}

int32_t Selection::getNumOptions() {
	int32_t i = 0;
	while (basicOptions[i]) {
		i++;
	}
	return i;
}

void Selection::selectEncoderAction(int32_t offset) {
	soundEditor.currentValue += offset;
	int32_t numOptions = getNumOptions();

	if (display.type == DisplayType::OLED) {
		if (soundEditor.currentValue > numOptions - 1) {
			soundEditor.currentValue = numOptions - 1;
		}
		else if (soundEditor.currentValue < 0) {
			soundEditor.currentValue = 0;
		}
	}
	else {
		if (soundEditor.currentValue >= numOptions) {
			soundEditor.currentValue -= numOptions;
		}
		else if (soundEditor.currentValue < 0) {
			soundEditor.currentValue += numOptions;
		}
	}

	Value::selectEncoderAction(offset);
}

void Selection::drawValue() {
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		display.setText(getOptions()[soundEditor.currentValue]);
	}
}

void Selection::drawPixelsForOled() {
	// Move scroll
	if (soundEditor.menuCurrentScroll > soundEditor.currentValue) {
		soundEditor.menuCurrentScroll = soundEditor.currentValue;
	}
	else if (soundEditor.menuCurrentScroll < soundEditor.currentValue - kOLEDMenuNumOptionsVisible + 1) {
		soundEditor.menuCurrentScroll = soundEditor.currentValue - kOLEDMenuNumOptionsVisible + 1;
	}

	char const** options = &getOptions()[soundEditor.menuCurrentScroll];
	int32_t selectedOption = soundEditor.currentValue - soundEditor.menuCurrentScroll;

	drawItemsForOled(options, selectedOption);
}
} // namespace menu_item
