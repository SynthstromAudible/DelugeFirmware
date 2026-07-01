/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "gui/ui/save/save_macro_preset_ui.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/overwrite_file.h"
#include "gui/l10n/l10n.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "io/midi/midi_macro.h"
#include "storage/storage_manager.h"
#include <string.h>

using namespace deluge;

SaveMacroPresetUI saveMacroPresetUI{};

SaveMacroPresetUI::SaveMacroPresetUI() {
	filePrefix = "MACRO";
}

bool SaveMacroPresetUI::opened() {
	Error error = createFoldersRecursiveIfNotExists(MIDIMacro::kPresetsFolder);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	bool success = SaveUI::opened();
	if (!success) { // In this case, an error will have already displayed.
		renderingNeededRegardlessOfUI();
		return false;
	}

	enteredText.clear();
	enteredTextEditPos = 0;
	currentFolderIsEmpty = false;

	currentDir.set(MIDIMacro::kPresetsFolder);

	if (display->haveOLED()) {
		title = "Save preset";
	}

	error = arrivedInNewFolder(0, enteredText.get(), MIDIMacro::kPresetsFolder);
	if (error != Error::NONE) {
		display->displayError(error);
		renderingNeededRegardlessOfUI();
		return false;
	}

	focusRegained();

	return true;
}

bool SaveMacroPresetUI::performSave(bool mayOverwrite) {
	if (display->have7SEG()) {
		display->displayLoadingAnimation();
	}

	String filePath;
	Error error = getCurrentFilePath(&filePath);
	if (error != Error::NONE) {
fail:
		display->displayError(error);
		return false;
	}

	error = StorageManager::createXMLFile(filePath.get(), smSerializer, mayOverwrite, false);

	if (error == Error::FILE_ALREADY_EXISTS) {
		gui::context_menu::overwriteFile.currentSaveUI = this;

		bool available = gui::context_menu::overwriteFile.setupAndCheckAvailability();
		if (available) { // Will always be true.
			display->setNextTransitionDirection(1);
			openUI(&gui::context_menu::overwriteFile);
			return true;
		}
		error = Error::UNSPECIFIED;
		goto fail;
	}

	if (error != Error::NONE) {
		goto fail;
	}

	if (display->haveOLED()) {
		deluge::hid::display::OLED::displayWorkingAnimation("Saving");
	}

	MIDIMacro::writeMacroPreset(GetSerializer(), MIDIMacro::presetMacroIndex);

	GetSerializer().closeFileAfterWriting();

	display->removeWorkingAnimation();

	display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MACRO_PRESET_SAVED));
	close();
	return true;
}

ActionResult SaveMacroPresetUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Load button
	if (b == LOAD) {
		return mainButtonAction(on);
	}

	if (on && b == BACK) {
		// don't allow navigation backwards if we're in the default folder
		if (!strcmp(currentDir.get(), MIDIMacro::kPresetsFolder)) {
			close();
			return ActionResult::DEALT_WITH;
		}
	}

	return SaveUI::buttonAction(b, on, inCardRoutine);
}
