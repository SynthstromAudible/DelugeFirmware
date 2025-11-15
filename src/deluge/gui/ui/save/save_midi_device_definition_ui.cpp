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

#include "gui/ui/save/save_midi_device_definition_ui.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/overwrite_file.h"
#include "gui/l10n/l10n.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"
#include <string.h>

using namespace deluge;

#define MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER "MIDI_DEVICES/DEFINITION"

PLACE_SDRAM_BSS SaveMidiDeviceDefinitionUI saveMidiDeviceDefinitionUI{};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-usage="
bool SaveMidiDeviceDefinitionUI::opened() {
	if (!getRootUI()->toClipMinder() || getCurrentOutputType() != OutputType::MIDI_OUT) {
		return false;
	}

	bool success = SaveUI::opened();
	if (!success) { // In this case, an error will have already displayed.
doReturnFalse:
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads.
		return false;
	}

	MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();

	enteredText.set(&midiInstrument->deviceDefinitionFileName);
	enteredTextEditPos = enteredText.getLength();
	currentFolderIsEmpty = false;

	// is empty we just start with nothing. currentSlot etc remain set to "zero" from before
	if (enteredText.isEmpty()) {
		currentDir.set(MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER);
	}
	else {
		char const* fullPath = enteredText.get();

		// locate last occurence of "/" in string
		char* filename = strrchr((char*)fullPath, '/');

		// lengths of full path
		auto fullPathLength = strlen(fullPath);

		// directory
		char dir[sizeof(char) * fullPathLength + 1];

		memset(dir, 0, sizeof(char) * fullPathLength + 1);
		strncpy(dir, fullPath, fullPathLength - strlen(filename));

		currentDir.set(dir);
	}

	title = "Save midi device";
	fileIcon = deluge::hid::display::OLED::midiIcon;
	fileIconPt2 = deluge::hid::display::OLED::midiIconPt2;
	fileIconPt2Width = 1;

	Error error = arrivedInNewFolder(0, enteredText.get(), MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER);
	if (error != Error::NONE) {
gotError:
		display->displayError(error);
		goto doReturnFalse;
	}

	focusRegained();

	return true;
}
#pragma GCC diagnostic pop

bool SaveMidiDeviceDefinitionUI::performSave(bool mayOverwrite) {
	if (display->have7SEG()) {
		display->displayLoadingAnimation();
	}

	MIDIInstrument* midiInstrumentToSave = (MIDIInstrument*)getCurrentInstrument();

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
		else {
			error = Error::UNSPECIFIED;
			goto fail;
		}
	}

	else if (error != Error::NONE) {
		goto fail;
	}

	if (display->haveOLED()) {
		deluge::hid::display::OLED::displayWorkingAnimation("Saving");
	}

	Serializer& writer = GetSerializer();

	midiInstrumentToSave->writeDeviceDefinitionFile(writer, false);

	writer.closeFileAfterWriting();

	display->removeWorkingAnimation();
	if (error != Error::NONE) {
		goto fail;
	}

	// Link the instrument with the definition file saved
	midiInstrumentToSave->deviceDefinitionFileName.set(filePath.get());

	display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MIDI_DEVICE_SAVED));
	close();
	return true;
}
