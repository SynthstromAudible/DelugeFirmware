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

#include "setting.h"
#include "model/settings/runtime_feature_settings.h"
#include "gui/ui/sound_editor.h"
#include <algorithm>
namespace deluge::gui::menu_item::runtime_feature {

Setting::Setting(RuntimeFeatureSettingType ty) : currentSettingIndex(static_cast<uint32_t>(ty)) {
}

void Setting::readCurrentValue() {
	for (uint32_t idx = 0; idx < RUNTIME_FEATURE_SETTING_MAX_OPTIONS; ++idx) {
		if (runtimeFeatureSettings.settings[currentSettingIndex].options[idx].value
		    == runtimeFeatureSettings.settings[currentSettingIndex].value) {
			this->value_ = idx;
			return;
		}
	}

	this->value_ = 0;
}

void Setting::writeCurrentValue() {
	runtimeFeatureSettings.settings[currentSettingIndex].value =
	    runtimeFeatureSettings.settings[currentSettingIndex].options[this->value_].value;
}

Sized<char const**> Setting::getOptions() {
	static char const* options[RUNTIME_FEATURE_SETTING_MAX_OPTIONS];
	std::fill(options, &options[RUNTIME_FEATURE_SETTING_MAX_OPTIONS], nullptr);

	uint32_t optionCount = 0;
	for (auto& option : runtimeFeatureSettings.settings[currentSettingIndex].options) {
		if (option.displayName != nullptr) {
			options[optionCount++] = option.displayName;
		}
		else {
			options[optionCount] = nullptr;
			break;
		}
	}
	return {options, getNumOptions()};
}

size_t Setting::getNumOptions() const {
	for (uint32_t idx = 0; idx < RUNTIME_FEATURE_SETTING_MAX_OPTIONS; ++idx) {
		if (runtimeFeatureSettings.settings[currentSettingIndex].options[idx].displayName == nullptr) {
			return idx;
		}
	}

	return 0;
}

char const* Setting::getName() {
	return runtimeFeatureSettings.settings[currentSettingIndex].displayName;
}

char const* Setting::getTitle() {
	return runtimeFeatureSettings.settings[currentSettingIndex].displayName;
}

} // namespace deluge::gui::menu_item::runtime_feature
