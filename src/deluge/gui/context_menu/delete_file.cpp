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
#include "definitions_cxx.hpp"
#include "gui/context_menu/save_song_or_instrument.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/browser.h"
#include "hid/display/display.h"

extern "C" {
#include "fatfs/ff.h"
}

namespace deluge::gui::context_menu {

DeleteFile deleteFile{};

char const* DeleteFile::getTitle() {
	using enum l10n::String;
	if (getUIUpOneLevel() == &context_menu::saveSongOrInstrument) {
		return l10n::get(STRING_FOR_ARE_YOU_SURE_QMARK);
	}
	return l10n::get(STRING_FOR_DELETE_QMARK);
}

std::span<char const*> DeleteFile::getOptions() {
	using enum l10n::String;

	if (display->haveOLED()) {
		static char const* options[] = {l10n::get(STRING_FOR_OK)};
		return {options, 1};
	}
	else {
		if (getUIUpOneLevel() == &context_menu::saveSongOrInstrument) {
			static char const* options[] = {l10n::get(STRING_FOR_SURE)};
			return {options, 1};
		}

		static char const* options[] = {l10n::get(STRING_FOR_DELETE)};
		return {options, 1};
	}
}

bool DeleteFile::acceptCurrentOption() {
	using enum l10n::String;

	UI* ui = getUIUpOneLevel();
	if (ui == &context_menu::saveSongOrInstrument) {
		ui = getUIUpOneLevel(2);
	}

	auto* browser = static_cast<Browser*>(ui);

	FileItem* toDelete = browser->getCurrentFileItem();
	if (toDelete->existsOnCard) {
		String filePath;
		Error error = browser->getCurrentFilePath(&filePath);
		if (error != Error::NONE) {
			display->displayError(error);
			return false;
		}

		FRESULT result = f_unlink(filePath.get());

		// If didn't work
		if (result != FR_OK) {
			display->displayPopup(l10n::get(STRING_FOR_ERROR_DELETING_FILE));
			// But we'll still go back to the Browser
		}
		else {
			display->displayPopup(l10n::get(STRING_FOR_FILE_DELETED));
			browser->currentFileDeleted();
		}
	}
	else if (toDelete->instrumentAlreadyInSong) {
		display->displayPopup(l10n::get(STRING_FOR_ERROR_PRESET_IN_USE));
	}
	else if (toDelete->instrument) {
		// it has an instrument, it's not on the card, it's not in use, let's remove it
		browser->currentFileDeleted();
	}
	close();
	if (getCurrentUI() == &context_menu::saveSongOrInstrument) {
		context_menu::saveSongOrInstrument.close();
	}

	return true;
}
} // namespace deluge::gui::context_menu
