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
#include "gui/menu_item/runtime_feature/setting.h"
#include "model/settings/runtime_feature_settings.h"
#include "gui/ui/sound_editor.h"
#include "util/container/static_vector.hpp"
#include <algorithm>
#include <iterator>
#include <ranges>

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

static_vector<string, RUNTIME_FEATURE_SETTING_MAX_OPTIONS> Setting::getOptions() {
	static static_vector<string, capacity()> options;
	if (options.empty()) {
		for (auto& option : runtimeFeatureSettings.settings[currentSettingIndex].options) {
			options.push_back(option.displayName);
		}
	}
	return options;
}

const string& Setting::getName() const {
	return runtimeFeatureSettings.settings[currentSettingIndex].displayName;
}

const string& Setting::getTitle() const {
	return runtimeFeatureSettings.settings[currentSettingIndex].displayName;
}

} // namespace deluge::gui::menu_item::runtime_feature
