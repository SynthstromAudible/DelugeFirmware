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
#include "model/action/action_logger.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/macros/macro_preset.h"
#include "modulation/macros/macros.h"
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
	Error error = createFoldersRecursiveIfNotExists(Macros::kPresetsFolder);
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

	Error error = currentDir.set(Macros::kPresetsFolder);
	if (error != Error::NONE) {
		return error;
	}

	error = arrivedInNewFolder(0, nullptr, Macros::kPresetsFolder);
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
		// The loaded target destinations are kept even if one is already used by another macro; flag
		// the first such collision instead of the plain "Preset loaded".
		int32_t owner = -1;
		uint8_t conflictDestination = 0;
		if (MelodicInstrument* instrument = Macros::macroClipInstrument(getCurrentClip())) {
			Macros::Macro* macros = instrument->macros;
			for (int32_t f = 0; f < Macros::kNumTargetSlots && owner < 0; f++) {
				uint8_t destination = macros[Macros::presetMacroIndex].targets[f].destination;
				owner = Macros::findTargetDestinationOwner(macros, destination, Macros::presetMacroIndex, f);
				if (owner >= 0) {
					conflictDestination = destination;
				}
			}
		}
		if (owner >= 0) {
			Macros::showDestinationConflictPopup(conflictDestination, owner);
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
		if (!strcmp(currentDir.get(), Macros::kPresetsFolder)) {
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

	// Presets load into the current clip's instrument macros (per-track, MIDI or synth).
	Clip* clip = getCurrentClip();
	MelodicInstrument* instrument = Macros::macroClipInstrument(clip);
	if (!instrument) {
		return Error::NONE;
	}
	Error error =
	    Macros::loadMacroPreset(&currentFileItem->filePointer, clip, instrument->macros, Macros::presetMacroIndex);
	if (error == Error::NONE) {
		instrument->editedByUser = true;
	}
	return error;
}
