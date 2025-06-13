/*
 * Copyright (c) 2024 Sean Ditny
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

#include "gui/context_menu/clip_settings/clip_settings.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/clip_settings/launch_style.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/rename/rename_clip_ui.h"
#include "gui/ui/root_ui.h"
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include <cstddef>

namespace deluge::gui::context_menu::clip_settings {

ClipSettingsMenu clipSettings{};

char const* ClipSettingsMenu::getTitle() {
	static char const* title = "Clip Settings";
	return title;
}

std::span<char const*> ClipSettingsMenu::getOptions() {
	using enum l10n::String;
	if (clip->type == ClipType::AUDIO) {
		static const char* optionsls[] = {
		    l10n::get(STRING_FOR_CLIP_MODE),
		    l10n::get(STRING_FOR_CLIP_NAME),
		};
		return {optionsls, 2};
	}
	else {
		static const char* optionsls[] = {
		    l10n::get(STRING_FOR_CONVERT_TO_AUDIO),
		    l10n::get(STRING_FOR_CLIP_MODE),
		    l10n::get(STRING_FOR_CLIP_NAME),
		};
		return {optionsls, 3};
	}
}

bool ClipSettingsMenu::setupAndCheckAvailability() {
	this->currentOption = scrollPos = 0; // start at the top
	return true;
}

void ClipSettingsMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
}

bool ClipSettingsMenu::acceptCurrentOption() {
	if (clip->type == ClipType::INSTRUMENT && this->currentOption == 0) {
		sessionView.replaceInstrumentClipWithAudioClip(clip);
		return false; // exit UI
	}
	else {
		int32_t option = this->currentOption;
		if (clip->type == ClipType::INSTRUMENT) {
			option--; // rebase option selection to 0
		}
		if (option == 0) {
			launchStyle.clip = clip;
			launchStyle.setupAndCheckAvailability();
			openUI(&launchStyle);
		}
		else {
			currentUIMode = UI_MODE_NONE;
			renameClipUI.clip = clip;
			openUI(&renameClipUI);
		}
		return true;
	}
}

ActionResult ClipSettingsMenu::padAction(int32_t x, int32_t y, int32_t on) {
	if (on) {
		return ContextMenu::padAction(x, y, on);
	}
	else {                                      // this would happen if you release pad after entering the menu
		return sessionView.padAction(x, y, on); // let the grid handle this
	}
}

} // namespace deluge::gui::context_menu::clip_settings
