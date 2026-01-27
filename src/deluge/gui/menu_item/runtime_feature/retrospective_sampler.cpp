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

#include "retrospective_sampler.h"
#include "gui/l10n/l10n.h"
#include "processing/retrospective/retrospective_buffer.h"

namespace deluge::gui::menu_item::runtime_feature {

// Menu items for the submenu
static RetrospectiveSamplerToggle menuRetroEnabled;
static Setting menuRetroSource(RuntimeFeatureSettingType::RetrospectiveSamplerSource);
static RetrospectiveSamplerBufferSetting menuRetroDuration(RuntimeFeatureSettingType::RetrospectiveSamplerDuration);
static RetrospectiveSamplerBufferSetting menuRetroBitDepth(RuntimeFeatureSettingType::RetrospectiveSamplerBitDepth);
static RetrospectiveSamplerBufferSetting menuRetroChannels(RuntimeFeatureSettingType::RetrospectiveSamplerChannels);
static SettingToggle menuRetroMonitor(RuntimeFeatureSettingType::RetrospectiveSamplerMonitor);
static SettingToggle menuRetroNormalize(RuntimeFeatureSettingType::RetrospectiveSamplerNormalize);

void RetrospectiveSamplerToggle::writeCurrentValue() {
	Setting::writeCurrentValue();

	if (runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSampler)) {
		// Allocate buffer when enabled
		retrospectiveBuffer.init();
	}
	else {
		// Free buffer when disabled
		retrospectiveBuffer.deinit();
	}
}

void RetrospectiveSamplerBufferSetting::writeCurrentValue() {
	Setting::writeCurrentValue();

	// Reinit buffer if enabled to apply new settings (clears buffer content)
	if (runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSampler)) {
		retrospectiveBuffer.reinit();
	}
}

RetrospectiveSamplerMenu::RetrospectiveSamplerMenu()
    : Submenu(l10n::String::STRING_FOR_COMMUNITY_FEATURE_RETRO_SAMPLER,
              {&menuRetroEnabled, &menuRetroSource, &menuRetroDuration, &menuRetroBitDepth, &menuRetroChannels,
               &menuRetroMonitor, &menuRetroNormalize}) {
}

std::string_view RetrospectiveSamplerMenu::getTitle() const {
	return l10n::getView(l10n::String::STRING_FOR_COMMUNITY_FEATURE_RETRO_SAMPLER);
}

// Global instance
RetrospectiveSamplerMenu menuRetrospectiveSamplerSubmenu;

} // namespace deluge::gui::menu_item::runtime_feature
