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

#include <MenuItemRuntimeFeatureSetting.h>
#include "RuntimeFeatureSettings.h"
#include "soundeditor.h"

MenuItemRuntimeFeatureSetting runtimeFeatureSettingMenuItem;

MenuItemRuntimeFeatureSetting::MenuItemRuntimeFeatureSetting(char const* newName) : MenuItemSelection(newName) {
}

void MenuItemRuntimeFeatureSetting::readCurrentValue() {
	for (uint32_t idx = 0; idx < RUNTIME_FEATURE_SETTING_MAX_OPTIONS; ++idx) {
		if (runtimeFeatureSettings.settings[currentSettingIndex].options[idx].value
		    == runtimeFeatureSettings.settings[currentSettingIndex].value) {
			soundEditor.currentValue = idx;
			return;
		}
	}

	soundEditor.currentValue = 0;
}

void MenuItemRuntimeFeatureSetting::writeCurrentValue() {
	runtimeFeatureSettings.settings[currentSettingIndex].value =
	    runtimeFeatureSettings.settings[currentSettingIndex].options[soundEditor.currentValue].value;
}

char const** MenuItemRuntimeFeatureSetting::getOptions() {
	static char const* options[RUNTIME_FEATURE_SETTING_MAX_OPTIONS] = {0};
	uint32_t optionCount = 0;
	for (uint32_t idx = 0; idx < RUNTIME_FEATURE_SETTING_MAX_OPTIONS; ++idx) {
		if (runtimeFeatureSettings.settings[currentSettingIndex].options[idx].displayName != NULL) {
			options[optionCount++] = runtimeFeatureSettings.settings[currentSettingIndex].options[idx].displayName;
		}
		else {
			options[optionCount] = NULL;
			break;
		}
	}
	return (char const**)&options;
}

int MenuItemRuntimeFeatureSetting::getNumOptions() {
	for (uint32_t idx = 0; idx < RUNTIME_FEATURE_SETTING_MAX_OPTIONS; ++idx) {
		if (runtimeFeatureSettings.settings[currentSettingIndex].options[idx].displayName == NULL) {
			return idx;
		}
	}

	return 0;
}
