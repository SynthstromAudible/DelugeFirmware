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

#pragma once

#include "gui/menu_item/selection.h"
#include "model/settings/runtime_feature_settings.h"

namespace deluge::gui::menu_item::runtime_feature {
class Settings;
class DevSysexSetting final : public Selection<2> {
public:
	explicit DevSysexSetting(RuntimeFeatureSettingType ty);

	void readCurrentValue() override;
	void writeCurrentValue() override;
	static_vector<std::string, 2> getOptions() override;
	[[nodiscard]] std::string_view getName() const override;
	[[nodiscard]] std::string_view getTitle() const override;

private:
	friend class Settings;
	uint32_t currentSettingIndex;
	int32_t onValue;
};
} // namespace deluge::gui::menu_item::runtime_feature
