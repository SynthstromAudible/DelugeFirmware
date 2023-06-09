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

#include <AudioEngine.h>
#include <InstrumentClip.h>
#include <InstrumentClipMinder.h>
#include <soundinstrument.h>
#include "saveinstrumentpresetui.h"
#include "storagemanager.h"
#include "definitions.h"
#include "functions.h"
#include "matrixdriver.h"
#include "song.h"
#include "lookuptables.h"
#include "numericdriver.h"
#include "kit.h"
#include "KeyboardScreen.h"
#include <string.h>
#include "View.h"
#include "ContextMenuOverwriteFile.h"
#include "IndicatorLEDs.h"
#include "Buttons.h"
#include "oled.h"

SaveInstrumentPresetUI saveInstrumentPresetUI;

SaveInstrumentPresetUI::SaveInstrumentPresetUI() {
}

bool SaveInstrumentPresetUI::opened() {

	Instrument* currentInstrument = (Instrument*)currentSong->currentClip->output;
	instrumentTypeToLoad =
	    currentInstrument
	        ->type; // Must set this before calling SaveUI::opened(), which uses this to work out folder name

	bool success = SaveUI::opened();
	if (!success) { // In this case, an error will have already displayed.
doReturnFalse:
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on the pads.
		return false;
	}

	enteredText.set(&currentInstrument->name);
	enteredTextEditPos = enteredText.getLength();
	currentFolderIsEmpty = false;

	char const* defaultDir = getInstrumentFolder(instrumentTypeToLoad);

	currentDir.set(&currentInstrument->dirPath);
	if (currentDir.isEmpty()) { // Would this even be able to happen?
tryDefaultDir:
		currentDir.set(defaultDir);
	}

#if HAVE_OLED
	fileIcon = (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) ? OLED::synthIcon : OLED::kitIcon;
	title = (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) ? "Save synth" : "Save kit";
#endif

	filePrefix = (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) ? "SYNT" : "KIT";

	int error = arrivedInNewFolder(0, enteredText.get(), defaultDir);
	if (error) {
gotError:
		numericDriver.displayError(error);
		goto doReturnFalse;
	}

	if (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) IndicatorLEDs::blinkLed(synthLedX, synthLedY);
	else IndicatorLEDs::blinkLed(kitLedX, kitLedY);

	/*
	String filePath;
	error = getCurrentFilePath(&filePath);
	if (error) goto gotError;
    currentFileExists = storageManager.fileExists(filePath.get());
*/

	focusRegained();
	return true;
}

bool SaveInstrumentPresetUI::performSave(bool mayOverwrite) {

#if !HAVE_OLED
	numericDriver.displayLoadingAnimation();
#endif

	Instrument* instrumentToSave = (Instrument*)currentSong->currentClip->output;

	bool isDifferentSlot = !enteredText.equalsCaseIrrespective(&instrumentToSave->name);

	// If saving into a new, different slot than the Instrument previously had...
	if (isDifferentSlot) {

		// We can't save into this slot if another Instrument in this Song already uses it
		if (currentSong->getInstrumentFromPresetSlot(instrumentTypeToLoad, 0, 0, enteredText.get(), currentDir.get(),
		                                             false)) {
			numericDriver.displayPopup(HAVE_OLED ? "Another instrument in the song has the same name / number"
			                                     : "CANT");
#if HAVE_OLED
			OLED::removeWorkingAnimation();
#endif
			return false;
		}

		// Alright, we know the new slot isn't used by an Instrument in the Song, but there may be an Instrument lurking in memory with that slot, which we need to just delete
		currentSong->deleteHibernatingInstrumentWithSlot(instrumentTypeToLoad, enteredText.get());
	}

	String filePath;
	int error = getCurrentFilePath(&filePath);
	if (error) {
fail:
		numericDriver.displayError(error);
		return false;
	}

	error = storageManager.createXMLFile(filePath.get(), mayOverwrite);

	if (error == ERROR_FILE_ALREADY_EXISTS) {
		contextMenuOverwriteFile.currentSaveUI = this;

		bool available = contextMenuOverwriteFile.setupAndCheckAvailability();

		if (available) { // Will always be true.
			numericDriver.setNextTransitionDirection(1);
			openUI(&contextMenuOverwriteFile);
			return true;
		}
		else {
			error = ERROR_UNSPECIFIED;
			goto fail;
		}
	}

	else if (error) goto fail;

#if HAVE_OLED
	OLED::displayWorkingAnimation("Saving");
#endif

	instrumentToSave->writeToFile(currentSong->currentClip, currentSong);

	char const* endString = (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) ? "\n</sound>\n" : "\n</kit>\n";

	error =
	    storageManager.closeFileAfterWriting(filePath.get(), "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", endString);
#if HAVE_OLED
	OLED::removeWorkingAnimation();
#endif
	if (error) goto fail;

	// Give the Instrument in memory its new slot
	instrumentToSave->name.set(&enteredText);
	instrumentToSave->dirPath.set(&currentDir);
	instrumentToSave->existsOnCard = true;

	// There's now no chance that we saved over a preset that's already in use in the song, because we didn't allow the user to select such a slot

#if HAVE_OLED
	OLED::consoleText("Preset saved");
#else
	numericDriver.displayPopup("DONE");
#endif
	close();
	return true;
}

/*
void SaveInstrumentPresetUI::selectEncoderAction(int8_t offset) {

	SaveUI::selectEncoderAction(offset);

	// Normal navigation through numeric or text-based names
	if (numberEditPos == -1) {

		Instrument* instrument = (Instrument*)currentSong->currentClip->output;

		int previouslySavedSlot = instrument->name.isEmpty() ? instrument->slot : -1;

		int error = storageManager.decideNextSaveableSlot(offset,
				&currentSlot, &currentSubSlot, &enteredText, &currentFileIsFolder,
				previouslySavedSlot, &currentFileExists, numInstrumentSlots, getThingName(instrumentType), currentDir.get(), instrumentType, (Instrument*)currentSong->currentClip->output);
		if (error) {
			numericDriver.displayError(error);
			if (error != ERROR_FOLDER_DOESNT_EXIST) {
				close();
			}
			return;
		}

		enteredTextEditPos = getMin(enteredTextEditPos, enteredText.getLength());

		currentFilename.set(&enteredText); // Only used for folders.
	}

	// Editing specific digit of numeric names. Pretty much no one will really be using this anymore
	else {
		int jumpSize;
		if (numberEditPos == 0) jumpSize = 1;
		else if (numberEditPos == 1) jumpSize = 10;
		else jumpSize = 100;

		currentSlot += jumpSize * offset;

		int16_t bestSlotFound;
		int8_t bestSubSlotFound;

		if (currentSlot >= numSongSlots) currentSlot = 0;
		else if (currentSlot < 0) currentSlot = numSongSlots - 1;

		// We want the "last" subslot, or -1 if there's none

		int8_t nothing;
		Instrument* nothingInstrument;
		bool nothing2;

		storageManager.findNextInstrumentPreset(-1, instrumentType,
				&bestSlotFound, &bestSubSlotFound, NULL, NULL, // No folders allowed.
				currentSlot + 1, -1, NULL, currentDir.get(),
				&nothing, AVAILABILITY_ANY, &nothingInstrument, &nothing2);

		if (bestSlotFound == currentSlot) {
			// If the preset was already saved in this slot, offer a brand new subslot
			if (currentSlot == ((Instrument*)currentSong->currentClip->output)->slot) {
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
