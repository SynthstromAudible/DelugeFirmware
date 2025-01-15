/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/save_song_or_instrument.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/delete_file.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/save/save_song_ui.h"
#include "hid/display/display.h"
#include "storage/file_item.h"

namespace deluge::gui::context_menu {
SaveSongOrInstrument saveSongOrInstrument{};

char const* SaveSongOrInstrument::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_OPTIONS);
}

std::span<char const*> SaveSongOrInstrument::getOptions() {
	using enum l10n::String;
	static char const* options[] = {
	    l10n::get(STRING_FOR_COLLECT_MEDIA), //<
	    l10n::get(STRING_FOR_CREATE_FOLDER), //<
	    l10n::get(STRING_FOR_DELETE)         //<
	};
	return {options, 3};
}

bool SaveSongOrInstrument::acceptCurrentOption() {
	switch (currentOption) {
	case 0: // Collect media
		saveSongUI.collectingSamples = true;
		return saveSongUI.performSave();

	case 1: { // Create folder
		Browser* browser = (Browser*)getUIUpOneLevel();
		Error error = browser->createFolder();

		if (error != Error::NONE) {
			display->displayError(error);
			return false;
		}
		close();
		return true;
	}
	case 2: { // Delete file
		bool available = context_menu::deleteFile.setupAndCheckAvailability();

		if (available) { // It always will be - but we gotta check.
			display->setNextTransitionDirection(1);
			openUI(&context_menu::deleteFile); // Might fail
		}
		return available;
	}
	default:
		__builtin_unreachable();
		return false;
	}
}

bool SaveSongOrInstrument::isCurrentOptionAvailable() {

	FileItem* currentFileItem = Browser::getCurrentFileItem();

	switch (currentOption) {
	case 0: // Collect media
		return (isUIOpen(&saveSongUI)) && (!currentFileItem || !currentFileItem->isFolder);

	case 1: // Create folder
		return (!QwertyUI::enteredText.isEmpty() && !currentFileItem);

	case 2: // Delete file
		return (currentFileItem && !currentFileItem->isFolder);

	default:
		__builtin_unreachable();
		return false;
	}
}

ActionResult SaveSongOrInstrument::padAction(int32_t x, int32_t y, int32_t on) {
	return getUIUpOneLevel()->padAction(x, y, on);
}
} // namespace deluge::gui::context_menu
