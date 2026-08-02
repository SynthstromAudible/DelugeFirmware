/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "gui/context_menu/reset_settings.h"
#include "deluge.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_follow.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"

namespace deluge::gui::context_menu {

ResetSettings resetSettings{};

char const* ResetSettings::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_ARE_YOU_SURE_QMARK);
}

std::span<char const*> ResetSettings::getOptions() {
	using enum l10n::String;

	if (display->haveOLED()) {
		static char const* options[] = {l10n::get(STRING_FOR_OK)};
		return {options, 1};
	}

	static char const* options[] = {l10n::get(STRING_FOR_SURE)};
	return {options, 1};
}

bool ResetSettings::acceptCurrentOption() {
	switch (action_) {
	case ResetSettingsAction::Flash:
		FlashStorage::factoryReset();
		break;
	case ResetSettingsAction::CommunityFeatures:
		runtimeFeatureSettings.factoryReset();
		break;
	case ResetSettingsAction::MidiFollow:
		midiFollow.factoryReset();
		break;
	case ResetSettingsAction::MidiDevices:
		MIDIDeviceManager::factoryReset();
		break;
	case ResetSettingsAction::All:
		Deluge::factoryReset();
		break;
	}

	return false;
}

} // namespace deluge::gui::context_menu
