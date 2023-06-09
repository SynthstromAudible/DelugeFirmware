/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include <ContextMenuDeleteFile.h>
#include "Browser.h"
#include "numericdriver.h"
#include "uart.h"
#include "matrixdriver.h"
#include "SaveSongOrInstrumentContextMenu.h"

extern "C" {
#include "fatfs/ff.h"
}

ContextMenuDeleteFile contextMenuDeleteFile;

ContextMenuDeleteFile::ContextMenuDeleteFile() {
}

char const** ContextMenuDeleteFile::getOptions() {
#if HAVE_OLED
	if (getUIUpOneLevel() == &saveSongOrInstrumentContextMenu) title = "Are you sure?";
	else title = "Delete?";

	static char const* options[] = {"OK"};
	return options;
#else
	static char const* options[] = {"DELETE"};
	static char const* optionsSure[] = {"SURE"};

	if (getUIUpOneLevel() == &saveSongOrInstrumentContextMenu) return optionsSure;
	else return options;
#endif
}

bool ContextMenuDeleteFile::acceptCurrentOption() {

	UI* ui = getUIUpOneLevel();
	if (ui == &saveSongOrInstrumentContextMenu) {
		ui = getUIUpOneLevel(2);
	}

	Browser* browser = (Browser*)ui;

	String filePath;
	int error = browser->getCurrentFilePath(&filePath);
	if (error) {
		numericDriver.displayError(error);
		return false;
	}

	FRESULT result = f_unlink(filePath.get());

	// If didn't work
	if (result != FR_OK) {
		numericDriver.displayPopup(HAVE_OLED ? "Error deleting file" : "ERROR");
		// But we'll still go back to the Browser
	}
	else {
		numericDriver.displayPopup(HAVE_OLED ? "File deleted" : "DONE");
		browser->currentFileDeleted();
	}

	close();
	if (getCurrentUI() == &saveSongOrInstrumentContextMenu) {
		saveSongOrInstrumentContextMenu.close();
	}

	return true;
}
