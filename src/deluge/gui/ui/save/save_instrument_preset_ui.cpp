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

#include "gui/ui/save/save_instrument_preset_ui.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/overwrite_file.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"
#include <string.h>

using namespace deluge;

SaveInstrumentPresetUI saveInstrumentPresetUI{};

SaveInstrumentPresetUI::SaveInstrumentPresetUI() {
}

bool SaveInstrumentPresetUI::opened() {

	Instrument* currentInstrument = getCurrentInstrument();
	// Must set this before calling SaveUI::opened(), which uses this to work out folder name
	outputTypeToLoad = currentInstrument->type;

	bool success = SaveUI::opened();
	if (!success) { // In this case, an error will have already displayed.
doReturnFalse:
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads.
		return false;
	}

	enteredText.set(&currentInstrument->name);
	enteredTextEditPos = enteredText.getLength();
	currentFolderIsEmpty = false;

	char const* defaultDir = getInstrumentFolder(outputTypeToLoad);

	currentDir.set(&currentInstrument->dirPath);
	if (currentDir.isEmpty()) { // Would this even be able to happen?
tryDefaultDir:
		currentDir.set(defaultDir);
	}

	if (display->haveOLED()) {
		fileIcon = (outputTypeToLoad == OutputType::SYNTH) ? deluge::hid::display::OLED::synthIcon
		                                                   : deluge::hid::display::OLED::kitIcon;
		title = (outputTypeToLoad == OutputType::SYNTH) ? "Save synth" : "Save kit";

		switch (outputTypeToLoad) {
		case OutputType::SYNTH:
			title = "Save synth";
			break;
		case OutputType::KIT:
			title = "Save kit";
			break;
		case OutputType::MIDI_OUT:
			title = "Save midi";
		}
	}
	// not used for midi
	filePrefix = (outputTypeToLoad == OutputType::SYNTH) ? "SYNT" : "KIT";

	Error error = arrivedInNewFolder(0, enteredText.get(), defaultDir);
	if (error != Error::NONE) {
gotError:
		display->displayError(error);
		goto doReturnFalse;
	}

	if (outputTypeToLoad == OutputType::SYNTH) {
		indicator_leds::blinkLed(IndicatorLED::SYNTH);
	}
	else {
		indicator_leds::blinkLed(IndicatorLED::KIT);
	}

	/*
	String filePath;
	error = getCurrentFilePath(&filePath);
	if (error != Error::NONE) goto gotError;
	currentFileExists = storageManager.fileExists(filePath.get());
*/

	focusRegained();
	return true;
}

bool SaveInstrumentPresetUI::performSave(StorageManager& bdsm, bool mayOverwrite) {
	if (display->have7SEG()) {
		display->displayLoadingAnimation();
	}
	Instrument* instrumentToSave = getCurrentInstrument();

	bool isDifferentSlot = !enteredText.equalsCaseIrrespective(&instrumentToSave->name);

	// If saving into a new, different slot than the Instrument previously had...
	if (isDifferentSlot) {

		// We can't save into this slot if another Instrument in this Song already uses it
		if (currentSong->getInstrumentFromPresetSlot(outputTypeToLoad, 0, 0, enteredText.get(), currentDir.get(),
		                                             false)) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_SAME_NAME));
			display->removeWorkingAnimation();
			return false;
		}

		// Alright, we know the new slot isn't used by an Instrument in the Song, but there may be an Instrument lurking
		// in memory with that slot, which we need to just delete
		currentSong->deleteHibernatingInstrumentWithSlot(outputTypeToLoad, enteredText.get());
	}

	String filePath;
	Error error = getCurrentFilePath(&filePath);
	if (error != Error::NONE) {
fail:
		display->displayError(error);
		return false;
	}

	error = bdsm.createXMLFile(filePath.get(), smSerializer, mayOverwrite, false);

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

	instrumentToSave->writeToFile(bdsm, getCurrentClip(), currentSong);

	char const* endString;
	switch (outputTypeToLoad) {
	case OutputType::SYNTH:
		endString = "\n</sound>\n";
		break;
	case OutputType::KIT:
		endString = "\n</kit>\n";
		break;
	case OutputType::MIDI_OUT:
		endString = "\n</midi>\n";
	}

	error = GetSerializer().closeFileAfterWriting(filePath.get(), "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n",
	                                              endString);
	display->removeWorkingAnimation();
	if (error != Error::NONE) {
		goto fail;
	}

	// Give the Instrument in memory its new slot
	instrumentToSave->name.set(&enteredText);
	instrumentToSave->dirPath.set(&currentDir);
	instrumentToSave->existsOnCard = true;

	// There's now no chance that we saved over a preset that's already in use in the song, because we didn't allow the
	// user to select such a slot

	display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PRESET_SAVED));
	close();
	return true;
}

/*
void SaveInstrumentPresetUI::selectEncoderAction(int8_t offset) {

    SaveUI::selectEncoderAction(offset);

    // Normal navigation through numeric or text-based names
    if (numberEditPos == -1) {

        Instrument* instrument = getCurrentInstrument();

        int32_t previouslySavedSlot = instrument->name.isEmpty() ? instrument->slot : -1;

        Error error = storageManager.decideNextSaveableSlot(offset,
                &currentSlot, &currentSubSlot, &enteredText, &currentFileIsFolder,
                previouslySavedSlot, &currentFileExists, numInstrumentSlots, getThingName(outputType), currentDir.get(),
outputType, getCurrentInstrument()); if (error != Error::NONE) { display->displayError(error); if (error !=
Error::FOLDER_DOESNT_EXIST) { close();
            }
            return;
        }

        enteredTextEditPos = std::min(enteredTextEditPos, enteredText.getLength());

        currentFilename.set(&enteredText); // Only used for folders.
    }

    // Editing specific digit of numeric names. Pretty much no one will really be using this anymore
    else {
        int32_t jumpSize;
        if (numberEditPos == 0) jumpSize = 1;
        else if (numberEditPos == 1) jumpSize = 10;
        else jumpSize = 100;

        currentSlot += jumpSize * offset;

        int16_t bestSlotFound;
        int8_t bestSubSlotFound;

        if (currentSlot >= kNumSongSlots) currentSlot = 0;
        else if (currentSlot < 0) currentSlot = kNumSongSlots - 1;

        // We want the "last" subslot, or -1 if there's none

        int8_t nothing;
        Instrument* nothingInstrument;
        bool nothing2;

        storageManager.findNextInstrumentPreset(-1, outputType,
                &bestSlotFound, &bestSubSlotFound, NULL, NULL, // No folders allowed.
                currentSlot + 1, -1, NULL, currentDir.get(),
                &nothing, Availability::ANY, &nothingInstrument, &nothing2);

        if (bestSlotFound == currentSlot) {
            // If the preset was already saved in this slot, offer a brand new subslot
            if (currentSlot == getCurrentInstrument()->slot) {
                currentSubSlot = bestSubSlotFound + 1;
                currentFileExists = false;

                if (currentSubSlot >= 26) goto doNormalThing; // Watch out for end of alphabet
            }

            else {
doNormalThing:
                currentSubSlot = bestSubSlotFound;
                currentFileExists = true;
            }
        }
        else {
            currentSubSlot = -1;
            currentFileExists = false;
        }
    }

    displayText(false);
}
*/
