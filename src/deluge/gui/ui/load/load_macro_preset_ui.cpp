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

#include "gui/ui/load/load_macro_preset_ui.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "io/midi/midi_macro.h"
#include "model/action/action_logger.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include <string.h>

using namespace deluge;

LoadMacroPresetUI loadMacroPresetUI{};

bool LoadMacroPresetUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool LoadMacroPresetUI::opened() {
	Error error = createFoldersRecursiveIfNotExists(MIDIMacro::kPresetsFolder);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	error = beginSlotSession();
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	actionLogger.deleteAllLogs();

	error = setupForLoadingPreset(); // Sets currentDir and draws the QWERTY interface.
	if (error != Error::NONE) {
		renderingNeededRegardlessOfUI();
		display->displayError(error);
		return false;
	}

	focusRegained();

	return true;
}

Error LoadMacroPresetUI::setupForLoadingPreset() {
	if (display->haveOLED()) {
		title = "Load preset";
	}

	enteredText.clear();

	Error error = currentDir.set(MIDIMacro::kPresetsFolder);
	if (error != Error::NONE) {
		return error;
	}

	error = arrivedInNewFolder(0, nullptr, MIDIMacro::kPresetsFolder);
	if (error != Error::NONE) {
		return error;
	}

	drawKeys();

	if (display->have7SEG()) {
		displayText(false);
	}

	return Error::NONE;
}

void LoadMacroPresetUI::folderContentsReady(int32_t entryDirection) {
}

void LoadMacroPresetUI::enterKeyPress() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	// If it's a directory...
	if (currentFileItem->isFolder) {
		Error error = goIntoFolder(currentFileItem->filename.get());
		if (error != Error::NONE) {
			display->displayError(error);
			close();
			return;
		}
	}
	else {
		Error error = performLoad();
		if (error != Error::NONE) {
			display->displayError(error);
			return;
		}
		// The loaded follower CCs are kept even if a CC is already used by another macro; flag the first
		// such collision instead of the plain "Preset loaded".
		Output* output = getCurrentOutput();
		int32_t owner = -1;
		uint8_t conflictCC = 0;
		if (output && output->type == OutputType::MIDI_OUT) {
			MIDIMacro::Macro* macros = static_cast<MIDIInstrument*>(output)->macros;
			for (int32_t f = 0; f < MIDIMacro::kNumFollowerSlots && owner < 0; f++) {
				uint8_t cc = macros[MIDIMacro::presetMacroIndex].followers[f].cc;
				owner = MIDIMacro::findFollowerCCOwner(macros, cc, MIDIMacro::presetMacroIndex, f);
				if (owner >= 0) {
					conflictCC = cc;
				}
			}
		}
		if (owner >= 0) {
			MIDIMacro::showCCConflictPopup(conflictCC, owner);
		}
		else {
			display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MACRO_PRESET_LOADED));
		}
		close();
	}
}

ActionResult LoadMacroPresetUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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

	return LoadUI::buttonAction(b, on, inCardRoutine);
}

ActionResult LoadMacroPresetUI::padAction(int32_t x, int32_t y, int32_t on) {
	if (x < kDisplayWidth) {
		return LoadUI::padAction(x, y, on);
	}
	LoadUI::exitAction();
	return ActionResult::DEALT_WITH;
}

Error LoadMacroPresetUI::performLoad() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem == nullptr) {
		return display->haveOLED() ? Error::FILE_NOT_FOUND : Error::NO_FURTHER_FILES_THIS_DIRECTION;
	}

	if (currentFileItem->isFolder) {
		return Error::NONE;
	}

	// Presets load into the current MIDI clip's instrument macros (per-track).
	Output* output = getCurrentOutput();
	if (!output || output->type != OutputType::MIDI_OUT) {
		return Error::NONE;
	}
	auto* instrument = static_cast<MIDIInstrument*>(output);
	Error error =
	    MIDIMacro::loadMacroPreset(&currentFileItem->filePointer, instrument->macros, MIDIMacro::presetMacroIndex);
	if (error == Error::NONE) {
		instrument->editedByUser = true;
	}
	return error;
}
