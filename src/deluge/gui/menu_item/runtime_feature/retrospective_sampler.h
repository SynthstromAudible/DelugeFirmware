/*
 * Copyright © 2024-2025 Owlet Records
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
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */

#pragma once

#include "gui/menu_item/runtime_feature/setting.h"
#include "gui/menu_item/submenu.h"
#include "model/settings/runtime_feature_settings.h"

namespace deluge::gui::menu_item::runtime_feature {

/// Custom toggle for the Retrospective Sampler that allocates/deallocates the buffer on toggle.
class RetrospectiveSamplerToggle final : public SettingToggle {
public:
	RetrospectiveSamplerToggle() : SettingToggle(RuntimeFeatureSettingType::RetrospectiveSampler) {}

	void writeCurrentValue() override;
};

/// Custom setting for buffer-affecting settings (Duration, BitDepth, Channels) that calls reinit.
class RetrospectiveSamplerBufferSetting final : public Setting {
public:
	RetrospectiveSamplerBufferSetting(RuntimeFeatureSettingType type) : Setting(type) {}

	void writeCurrentValue() override;
};

/// Submenu containing all retrospective sampler settings.
class RetrospectiveSamplerMenu final : public Submenu {
public:
	RetrospectiveSamplerMenu();

	[[nodiscard]] std::string_view getTitle() const override;
};

/// Global instance of the retrospective sampler submenu
extern RetrospectiveSamplerMenu menuRetrospectiveSamplerSubmenu;

} // namespace deluge::gui::menu_item::runtime_feature
