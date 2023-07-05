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

#include "settings.h"
#include "setting.h"
#include "model/settings/runtime_feature_settings.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include <cstdio>

extern menu_item::runtime_feature::Setting runtimeFeatureSettingMenuItem;

namespace menu_item::runtime_feature {

void Settings::beginSession(MenuItem* navigatedBackwardFrom) {
	if (!navigatedBackwardFrom) {
		lastActiveValue = 0; // Reset last active value
	}

	soundEditor.currentValue = lastActiveValue; // Restore

#if HAVE_OLED
	soundEditor.menuCurrentScroll = soundEditor.currentValue;
#else
	drawValue();
#endif
}

void Settings::selectEncoderAction(int offset) {
	soundEditor.currentValue += offset;
	int numOptions = RuntimeFeatureSettingType::MaxElement;

#if HAVE_OLED
	if (soundEditor.currentValue > numOptions - 1) {
		soundEditor.currentValue = numOptions - 1;
	}
	else if (soundEditor.currentValue < 0) {
		soundEditor.currentValue = 0;
	}
#else
	if (soundEditor.currentValue >= numOptions)
		soundEditor.currentValue -= numOptions;
	else if (soundEditor.currentValue < 0)
		soundEditor.currentValue += numOptions;
#endif

	lastActiveValue = soundEditor.currentValue;

#if HAVE_OLED
	if (soundEditor.currentValue < soundEditor.menuCurrentScroll) {
		soundEditor.menuCurrentScroll = soundEditor.currentValue;
	}

	if (soundEditor.currentValue > (soundEditor.menuCurrentScroll + (OLED_MENU_NUM_OPTIONS_VISIBLE - 1))) {
		soundEditor.menuCurrentScroll = soundEditor.currentValue - (OLED_MENU_NUM_OPTIONS_VISIBLE - 1);
	}

#endif

	drawValue();
}

void Settings::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	numericDriver.setScrollingText(runtimeFeatureSettings.settings[soundEditor.currentValue].displayName);
#endif
}

MenuItem* Settings::selectButtonPress() {
#if HAVE_OLED
	runtimeFeatureSettingMenuItem.basicTitle = runtimeFeatureSettings.settings[soundEditor.currentValue]
	                                               .displayName; // A bit ugly, but saves us extending a class.
#endif
	runtimeFeatureSettingMenuItem.currentSettingIndex = soundEditor.currentValue;
	return &runtimeFeatureSettingMenuItem;
}

#if HAVE_OLED

void Settings::drawPixelsForOled() {
	char const* itemNames[OLED_MENU_NUM_OPTIONS_VISIBLE];

	int selectedRow = -1;

	int displayRow = soundEditor.menuCurrentScroll;
	int row = 0;
	while (row < OLED_MENU_NUM_OPTIONS_VISIBLE && displayRow < RuntimeFeatureSettingType::MaxElement) {
		itemNames[row] = runtimeFeatureSettings.settings[displayRow].displayName;
		if (displayRow == soundEditor.currentValue) {
			selectedRow = row;
		}
		row++;
		displayRow++;
	}

	while (row < OLED_MENU_NUM_OPTIONS_VISIBLE) {
		itemNames[row++] = NULL;
	}

	drawItemsForOled(itemNames, selectedRow);
}
#endif

} // namespace menu_item::runtime_feature
