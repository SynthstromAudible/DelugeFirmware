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

#include "devSysexSetting.h"
#include "gui/menu_item/runtime_feature/setting.h"
#include "gui/ui/sound_editor.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/container/static_vector.hpp"
#include "util/functions.h"
#include <algorithm>
#include <fmt/core.h>
#include <iterator>
#include <ranges>

namespace deluge::gui::menu_item::runtime_feature {

DevSysexSetting::DevSysexSetting(RuntimeFeatureSettingType ty) : currentSettingIndex(static_cast<uint32_t>(ty)) {
}

void DevSysexSetting::readCurrentValue() {
	int32_t rawValue = runtimeFeatureSettings.settings[currentSettingIndex].value;
	setValue(rawValue != 0);
	if (rawValue != 0) {
		onValue = rawValue;
	}
	else {
		do {
			onValue = getNoise() & 0x7FFFFFFF;
		} while (onValue == 0);
	}
}

void DevSysexSetting::writeCurrentValue() {
	if (this->getValue()) {
		runtimeFeatureSettings.settings[currentSettingIndex].value = onValue;
	}
	else {
		runtimeFeatureSettings.settings[currentSettingIndex].value = 0;
	}
}

static_vector<std::string, 2> DevSysexSetting::getOptions() {
	static_vector<std::string, capacity()> options;
	options.push_back("Off");
	options.push_back(fmt::vformat("on ({:8x})", fmt::make_format_args(onValue)));
	return options;
}

std::string_view DevSysexSetting::getName() const {
	return runtimeFeatureSettings.settings[currentSettingIndex].displayName;
}

std::string_view DevSysexSetting::getTitle() const {
	return runtimeFeatureSettings.settings[currentSettingIndex].displayName;
}

} // namespace deluge::gui::menu_item::runtime_feature
