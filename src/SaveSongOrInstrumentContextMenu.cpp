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

#include <SaveSongOrInstrumentContextMenu.h>
#include "numericdriver.h"
#include "SaveSongUI.h"
#include "ContextMenuDeleteFile.h"
#include "FileItem.h"

SaveSongOrInstrumentContextMenu saveSongOrInstrumentContextMenu;

SaveSongOrInstrumentContextMenu::SaveSongOrInstrumentContextMenu() {
#if HAVE_OLED
	title = "Options";
#endif
}

char const** SaveSongOrInstrumentContextMenu::getOptions() {
	static char const* options[] = {"Collect media", "Create folder", "Delete"};
	return options;
}

bool SaveSongOrInstrumentContextMenu::acceptCurrentOption() {
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
		bool available = contextMenuDeleteFile.setupAndCheckAvailability();

		if (available) { // It always will be - but we gotta check.
			numericDriver.setNextTransitionDirection(1);
			openUI(&contextMenuDeleteFile); // Might fail
		}
		return available;
	}
	default:
		__builtin_unreachable();
		return false;
	}
}

bool SaveSongOrInstrumentContextMenu::isCurrentOptionAvailable() {

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

int SaveSongOrInstrumentContextMenu::padAction(int x, int y, int on) {
	return getUIUpOneLevel()->padAction(x, y, on);
}
