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

#include "gui/ui/load/load_midi_device_definition_ui.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/root_ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "io/debug/log.h"
#include "model/action/action_logger.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

using namespace deluge;

#define MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER "MIDI_DEVICES/DEFINITION"

LoadMidiDeviceDefinitionUI loadMidiDeviceDefinitionUI{};

bool LoadMidiDeviceDefinitionUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool LoadMidiDeviceDefinitionUI::opened() {
	if (!getRootUI()->toClipMinder() || getCurrentOutputType() != OutputType::MIDI_OUT) {
		return false;
	}

	Error error = beginSlotSession(); // Requires currentDir to be set. (Not anymore?)
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	actionLogger.deleteAllLogs();

	error = setupForLoadingMidiDeviceDefinition(); // Sets currentDir.
	if (error != Error::NONE) {
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads, in call to setupForLoadingMidiDeviceDefinition().
		display->displayError(error);
		return false;
	}

	focusRegained();

	return true;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-usage="
// If OLED, then you should make sure renderUIsForOLED() gets called after this.
Error LoadMidiDeviceDefinitionUI::setupForLoadingMidiDeviceDefinition() {
	// reset
	fileIconPt2 = nullptr;
	fileIconPt2Width = 0;

	if (display->haveOLED()) {
		title = "Load midi device";
		fileIcon = deluge::hid::display::OLED::midiIcon;
		fileIconPt2 = deluge::hid::display::OLED::midiIconPt2;
		fileIconPt2Width = 1;
	}

	enteredText.clear();

	String searchFilename;

	MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();

	// is empty we just start with nothing. currentSlot etc remain set to "zero" from before
	if (midiInstrument->deviceDefinitionFileName.isEmpty()) {
		Error error = currentDir.set(MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER);
		if (error != Error::NONE) {
			return error;
		}
	}
	else {
		char const* fullPath = midiInstrument->deviceDefinitionFileName.get();

		// locate last occurence of "/" in string
		char* filename = strrchr((char*)fullPath, '/');

		// lengths of full path
		auto fullPathLength = strlen(fullPath);

		// directory
		char dir[sizeof(char) * fullPathLength + 1];

		memset(dir, 0, sizeof(char) * fullPathLength + 1);
		strncpy(dir, fullPath, fullPathLength - strlen(filename));

		currentDir.set(dir);
		searchFilename.set(++filename);
	}

	if (!searchFilename.isEmpty()) {
		Error error = searchFilename.concatenate(".XML");
		if (error != Error::NONE) {
			return error;
		}
	}

	Error error = arrivedInNewFolder(0, searchFilename.get(), MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER);
	if (error != Error::NONE) {
		return error;
	}

	currentLabelLoadError = (fileIndexSelected >= 0) ? Error::NONE : Error::UNSPECIFIED;

	drawKeys();

	if (display->have7SEG()) {
		displayText(false);
	}

	return Error::NONE;
}
#pragma GCC diagnostic pop

void LoadMidiDeviceDefinitionUI::folderContentsReady(int32_t entryDirection) {
}

void LoadMidiDeviceDefinitionUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	// If it's a directory...
	if (currentFileItem->isFolder) {

		Error error = goIntoFolder(currentFileItem->filename.get());

		if (error != Error::NONE) {
			display->displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}

	else {

		currentLabelLoadError = performLoad();
		if (currentLabelLoadError != Error::NONE) {
			display->displayError(currentLabelLoadError);
			return;
		}

		display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MIDI_DEVICE_LOADED));

		close();
	}
}

ActionResult LoadMidiDeviceDefinitionUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Load button
	if (b == LOAD) {
		return mainButtonAction(on);
	}

	else {
		if (on && b == BACK) {
			// don't allow navigation backwards if we're in the default folder
			if (!strcmp(currentDir.get(), MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER)) {
				close();
				return ActionResult::DEALT_WITH;
			}
		}

		return LoadUI::buttonAction(b, on, inCardRoutine);
	}
}

ActionResult LoadMidiDeviceDefinitionUI::padAction(int32_t x, int32_t y, int32_t on) {
	if (x < kDisplayWidth) {
		return LoadUI::padAction(x, y, on);
	}
	else {
		LoadUI::exitAction();
		return ActionResult::DEALT_WITH;
	}
}

Error LoadMidiDeviceDefinitionUI::performLoad(bool doClone) {

	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem == nullptr) {
		// Make it say "NONE" on numeric Deluge, for
		// consistency with old times.
		return display->haveOLED() ? Error::FILE_NOT_FOUND : Error::NO_FURTHER_FILES_THIS_DIRECTION;
	}

	if (currentFileItem->isFolder) {
		return Error::NONE;
	}

	String fileName;
	fileName.set(currentDir.get());
	fileName.concatenate("/");
	fileName.concatenate(enteredText.get());
	fileName.concatenate(".XML");

	Error error = StorageManager::loadMidiDeviceDefinitionFile((MIDIInstrument*)getCurrentOutput(),
	                                                           &currentFileItem->filePointer, &fileName);

	if (error != Error::NONE) {
		return error;
	}

	return Error::NONE;
}
