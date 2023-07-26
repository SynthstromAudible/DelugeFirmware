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

#include "gui/context_menu/delete_file.h"
#include "gui/ui/browser/browser.h"
#include "hid/display/numeric_driver.h"
#include "io/debug/print.h"
#include "hid/matrix/matrix_driver.h"
#include "gui/context_menu/save_song_or_instrument.h"

extern "C" {
#include "fatfs/ff.h"
}

namespace deluge::gui::context_menu {

DeleteFile deleteFile{};

char const* DeleteFile::getTitle() {
	static char* title;
	if (getUIUpOneLevel() == &context_menu::saveSongOrInstrument) {
		title = "Are you sure?";
	}
	else {
		title = "Delete?";
	}
	return title;
}

Sized<char const**> DeleteFile::getOptions() {
#if HAVE_OLED
	static char const* options[] = {"OK"};
	return {options, 1};
#else
	static char const* options[] = {"DELETE"};
	static char const* optionsSure[] = {"SURE"};

	if (getUIUpOneLevel() == &context_menu::saveSongOrInstrument) {
		return {optionsSure, 1};
	}
	else {
		return {options, 1};
	}
#endif
}

bool DeleteFile::acceptCurrentOption() {

	UI* ui = getUIUpOneLevel();
	if (ui == &context_menu::saveSongOrInstrument) {
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
	if (getCurrentUI() == &context_menu::saveSongOrInstrument) {
		context_menu::saveSongOrInstrument.close();
	}

	return true;
}
} // namespace deluge::gui::context_menu
