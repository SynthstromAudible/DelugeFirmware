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
#include "hid/display/numeric_driver.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/context_menu/delete_file.h"
#include "storage/file_item.h"

namespace deluge::gui::context_menu {
SaveSongOrInstrument saveSongOrInstrument{};

char const* SaveSongOrInstrument::getTitle() {
	static char const* title = "Options";
	return title;
}

Sized<char const**> SaveSongOrInstrument::getOptions() {
	static char const* options[] = {"Collect media", "Create folder", "Delete"};
	return {options, 3};
}

bool SaveSongOrInstrument::acceptCurrentOption() {
	switch (currentOption) {
	case 0: // Collect media
		saveSongUI.collectingSamples = true;
		return saveSongUI.performSave();

	case 1: { // Create folder
		Browser* browser = (Browser*)getUIUpOneLevel();
		int error = browser->createFolder();

		if (error) {
			numericDriver.displayError(error);
			return false;
		}
		close();
		return true;
	}
	case 2: { // Delete file
		bool available = context_menu::deleteFile.setupAndCheckAvailability();

		if (available) { // It always will be - but we gotta check.
			numericDriver.setNextTransitionDirection(1);
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

int SaveSongOrInstrument::padAction(int x, int y, int on) {
	return getUIUpOneLevel()->padAction(x, y, on);
}
} // namespace deluge::gui::context_menu
