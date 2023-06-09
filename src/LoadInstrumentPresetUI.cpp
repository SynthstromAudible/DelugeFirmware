/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include <ArrangerView.h>
#include <AudioFileManager.h>
#include <InstrumentClip.h>
#include <InstrumentClipView.h>
#include <LoadInstrumentPresetUI.h>
#include <RootUI.h>
#include "functions.h"
#include "song.h"
#include "instrument.h"
#include "numericdriver.h"
#include "matrixdriver.h"
#include "uart.h"
#include "View.h"
#include "storagemanager.h"
#include "KeyboardScreen.h"
#include "ActionLogger.h"
#include "MIDIInstrument.h"
#include "ContextMenuLoadInstrumentPreset.h"
#include "PadLEDs.h"
#include "IndicatorLEDs.h"
#include "Encoders.h"
#include "Buttons.h"
#include "extern.h"
#include "uitimermanager.h"
#include "FileItem.h"
#include "oled.h"

LoadInstrumentPresetUI loadInstrumentPresetUI;

LoadInstrumentPresetUI::LoadInstrumentPresetUI() {
}

bool LoadInstrumentPresetUI::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	if (showingAuditionPads()) {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
		*cols = 0xFFFFFFFE;
#else
		*cols = 0b10;
#endif
	}
	else {
		*cols = 0xFFFFFFFF;
	}
	return true;
}

bool LoadInstrumentPresetUI::opened() {

	if (getRootUI() == &keyboardScreen) PadLEDs::skipGreyoutFade();

	initialInstrumentType = instrumentToReplace->type;
	initialName.set(&instrumentToReplace->name);
	initialDirPath.set(&instrumentToReplace->dirPath);

	switch (instrumentToReplace->type) {
	case INSTRUMENT_TYPE_MIDI_OUT:
		initialChannelSuffix = ((MIDIInstrument*)instrumentToReplace)->channelSuffix;
		// No break

	case INSTRUMENT_TYPE_CV:
		initialChannel = ((NonAudioInstrument*)instrumentToReplace)->channel;
		break;
	}

	changedInstrumentForClip = false;
	replacedWholeInstrument = false;

	if (instrumentClipToLoadFor) {
		instrumentClipToLoadFor
		    ->backupPresetSlot(); // Store this now cos we won't be storing it between each navigation we do
	}

	int error = beginSlotSession(); // Requires currentDir to be set. (Not anymore?)
	if (error) {
gotError:
		numericDriver.displayError(error);
		return false;
	}

	actionLogger.deleteAllLogs();

	error = setupForInstrumentType(); // Sets currentDir.
	if (error) {
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on the pads, in call to setupForInstrumentType().
		goto gotError;
	}

	focusRegained();
	return true;
}

// If HAVE_OLED, then you should make sure renderUIsForOLED() gets called after this.
int LoadInstrumentPresetUI::setupForInstrumentType() {
	IndicatorLEDs::setLedState(synthLedX, synthLedY, false);
	IndicatorLEDs::setLedState(kitLedX, kitLedY, false);
	IndicatorLEDs::setLedState(midiLedX, midiLedY, false);
	IndicatorLEDs::setLedState(cvLedX, cvLedY, false);

	if (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) {
		IndicatorLEDs::blinkLed(synthLedX, synthLedY);
	}
	else {
		IndicatorLEDs::blinkLed(kitLedX, kitLedY);
	}

#if HAVE_OLED
	fileIcon = (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) ? OLED::synthIcon : OLED::kitIcon;
	title = (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) ? "Load synth" : "Load kit";
#endif

	filePrefix = (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) ? "SYNT" : "KIT";

	enteredText.clear();

	char const* defaultDir = getInstrumentFolder(instrumentTypeToLoad);

	String searchFilename;

	// I don't have this calling arrivedInNewFolder(), because as you can see below, we want to either just display the existing preset, or
	// call confirmPresetOrNextUnlaunchedOne() to skip any which aren't "available".

	// If same Instrument type as we already had...
	if (instrumentToReplace->type == instrumentTypeToLoad) {

		// Then we can start by just looking at the existing Instrument, cos they're the same type...
		currentDir.set(&instrumentToReplace->dirPath);
		searchFilename.set(&instrumentToReplace->name);

		if (currentDir.isEmpty()) goto useDefaultFolder;
	}

	// Or if the Instruments are different types...
	else {

		// If we've got a Clip, we can see if it used to use another Instrument of this new type...
		if (instrumentClipToLoadFor) {
			String* backedUpName = &instrumentClipToLoadFor->backedUpInstrumentName[instrumentTypeToLoad];
			enteredText.set(backedUpName);
			searchFilename.set(backedUpName);
			currentDir.set(&instrumentClipToLoadFor->backedUpInstrumentDirPath[instrumentTypeToLoad]);
			if (currentDir.isEmpty()) goto useDefaultFolder;
		}
		// Otherwise we just start with nothing. currentSlot etc remain set to "zero" from before
		else {
useDefaultFolder:
			int error = currentDir.set(defaultDir);
			if (error) return error;
		}
	}

	if (!searchFilename.isEmpty()) {
		int error = searchFilename.concatenate(".XML");
		if (error) return error;
	}

	int error = arrivedInNewFolder(0, searchFilename.get(), defaultDir);
	if (error) return error;

	currentInstrumentLoadError = (fileIndexSelected >= 0) ? NO_ERROR : ERROR_UNSPECIFIED;

	// The redrawing of the sidebar only actually has to happen if we just changed to a different type *or* if we came in from (musical) keyboard view, I think
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	PadLEDs::clearAllPadsWithoutSending();
	drawKeys();
	PadLEDs::sendOutMainPadColours();
#endif

	if (showingAuditionPads()) {
		instrumentClipView.recalculateColours();
		renderingNeededRegardlessOfUI(0, 0xFFFFFFFF);
	}

#if !HAVE_OLED
	displayText(false);
#endif
	return NO_ERROR;
}

void LoadInstrumentPresetUI::folderContentsReady(int entryDirection) {
	currentFileChanged(0);
}

void LoadInstrumentPresetUI::currentFileChanged(int movementDirection) {
	//FileItem* currentFileItem = getCurrentFileItem();

	//if (currentFileItem->instrument != instrumentToReplace) {

	currentUIMode = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
	currentInstrumentLoadError = performLoad();
	currentUIMode = UI_MODE_NONE;
	//}
}

void LoadInstrumentPresetUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) return;

	// If it's a directory...
	if (currentFileItem->isFolder) {

		int error = goIntoFolder(currentFileItem->filename.get());

		if (error) {
			numericDriver.displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}

	else {

		if (currentInstrumentLoadError) {
			currentInstrumentLoadError = performLoad();
			if (currentInstrumentLoadError) {
				numericDriver.displayError(currentInstrumentLoadError);
				return;
			}
		}

		if (currentFileItem
		        ->instrument) { // When would this not have something? Well ok, maybe now that we have folders.
			convertToPrefixFormatIfPossible();
		}

		if (instrumentTypeToLoad == INSTRUMENT_TYPE_KIT && showingAuditionPads()) {
			// New NoteRows have probably been created, whose colours haven't been grabbed yet.
			instrumentClipView.recalculateColours();
		}

		close();
	}
}

int LoadInstrumentPresetUI::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	int newInstrumentType;

	// Load button
	if (x == loadButtonX && y == loadButtonY) {
		return mainButtonAction(on);
	}

	// Synth button
	else if (x == synthButtonX && y == synthButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_SYNTH;
doChangeInstrumentType:
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			convertToPrefixFormatIfPossible(); // Why did I put this here?
			changeInstrumentType(newInstrumentType);
		}
	}

	// Kit button
	else if (x == kitButtonX && y == kitButtonY) {
		if (instrumentClipToLoadFor && instrumentClipToLoadFor->onKeyboardScreen) {
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
			IndicatorLEDs::indicateAlertOnLed(keyboardLedX, keyboardLedX);
#endif
		}
		else {
			newInstrumentType = INSTRUMENT_TYPE_KIT;
			goto doChangeInstrumentType;
		}
	}

	// MIDI button
	else if (x == midiButtonX && y == midiButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_MIDI_OUT;
		goto doChangeInstrumentType;
	}

	// CV button
	else if (x == cvButtonX && y == cvButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_CV;
		goto doChangeInstrumentType;
	}

	else return LoadUI::buttonAction(x, y, on, inCardRoutine);

	return ACTION_RESULT_DEALT_WITH;
}

int LoadInstrumentPresetUI::timerCallback() {
	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {

		if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // The below needs to access the card.

		currentUIMode = UI_MODE_NONE;

		FileItem* currentFileItem = getCurrentFileItem();

		// Folders don't have a context menu
		if (!currentFileItem || currentFileItem->isFolder) return ACTION_RESULT_DEALT_WITH;

		// We want to open the context menu to choose to reload the original file for the currently selected preset in some way.
		// So first up, make sure there is a file, and that we've got its pointer
		String filePath;
		int error = getCurrentFilePath(&filePath);
		if (error) return error;

		bool fileExists = storageManager.fileExists(filePath.get(), &currentFileItem->filePointer);
		if (!fileExists) {
			numericDriver.displayError(ERROR_FILE_NOT_FOUND);
			return ACTION_RESULT_DEALT_WITH;
		}

		bool available = contextMenuLoadInstrumentPreset.setupAndCheckAvailability();

		if (available) {
			numericDriver.setNextTransitionDirection(1);
			convertToPrefixFormatIfPossible();
			openUI(&contextMenuLoadInstrumentPreset);
		}
		else {
			exitUIMode(UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS);
		}

		return ACTION_RESULT_DEALT_WITH;
	}
	else {
		return LoadUI::timerCallback();
	}
}

void LoadInstrumentPresetUI::changeInstrumentType(int newInstrumentType) {
	if (newInstrumentType == instrumentTypeToLoad) return;

	// If MIDI or CV, we have a different method for this, and the UI will be exited
	if (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT || newInstrumentType == INSTRUMENT_TYPE_CV) {

		Instrument* newInstrument;
		// In arranger...
		if (!instrumentClipToLoadFor) {
			newInstrument = currentSong->changeInstrumentType(instrumentToReplace, newInstrumentType);
		}

		// Or, in SessionView or a ClipMinder
		else {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);

			newInstrument = instrumentClipToLoadFor->changeInstrumentType(modelStack, newInstrumentType);
		}

		// If that succeeded, get out
		if (newInstrument) {

			// If going back to a view where the new selection won't immediately be displayed, gotta give some confirmation
			if (!getRootUI()->toClipMinder()) {
#if HAVE_OLED
				char const* message = (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT)
				                          ? "Instrument switched to MIDI channel"
				                          : "Instrument switched to CV channel";
#else
				char const* message = "DONE";
#endif
				numericDriver.displayPopup(message);
			}

			close();
		}
	}

	// Or, for normal synths and kits
	else {
		int oldInstrumentType = instrumentTypeToLoad;
		instrumentTypeToLoad = newInstrumentType;

		int error = setupForInstrumentType();
		if (error) {
			instrumentTypeToLoad = oldInstrumentType;
			return;
		}

#if HAVE_OLED
		renderUIsForOled();
#endif
		performLoad();
	}
}

void LoadInstrumentPresetUI::revertToInitialPreset() {

	// Can only do this if we've changed Instrument in one of the two ways, but not both.
	// TODO: that's very limiting, and I can't remember why I mandated this, or what would be so hard about allowing this.
	// Very often, the user might enter this interface for a Clip sharing its Output/Instrument with other Clips, so when
	// user starts navigating through presets, it'll first do a "change just for Clip", but then on the new preset, this will now
	// be the only Clip, so next time it'll do a "replace whole Instrument".
	if (changedInstrumentForClip == replacedWholeInstrument) return;

	int availabilityRequirement;
	bool oldInstrumentCanBeReplaced;
	if (instrumentClipToLoadFor) {
		oldInstrumentCanBeReplaced =
		    currentSong->canOldOutputBeReplaced(instrumentClipToLoadFor, &availabilityRequirement);
	}

	else {
		oldInstrumentCanBeReplaced = true;
		availabilityRequirement = AVAILABILITY_INSTRUMENT_UNUSED;
	}

	// If we're looking to replace the whole Instrument, but we're not allowed, that's obviously a no-go
	if (replacedWholeInstrument && !oldInstrumentCanBeReplaced) return;

	bool needToAddInstrumentToSong = false;

	Instrument* initialInstrument = NULL;

	// Search main, non-hibernating Instruments
	initialInstrument =
	    currentSong->getInstrumentFromPresetSlot(initialInstrumentType, initialChannel, initialChannelSuffix,
	                                             initialName.get(), initialDirPath.get(), false, true);

	// If we found it already as a non-hibernating one...
	if (initialInstrument) {

		// ... check that our availabilityRequirement allows this
		if (availabilityRequirement == AVAILABILITY_INSTRUMENT_UNUSED) {
			return;
		}

		else if (availabilityRequirement == AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION) {
			if (currentSong->doesOutputHaveActiveClipInSession(initialInstrument)) {
				return;
			}
		}
	}

	// Or if we did not find it as a non-hibernating one...
	else {

		needToAddInstrumentToSong = true;

		// MIDI / CV
		if (initialInstrumentType == INSTRUMENT_TYPE_MIDI_OUT || initialInstrumentType == INSTRUMENT_TYPE_CV) {

			// One MIDIInstrument may be hibernating...
			if (initialInstrumentType == INSTRUMENT_TYPE_MIDI_OUT) {
				initialInstrument = currentSong->grabHibernatingMIDIInstrument(initialChannel, initialChannelSuffix);
				if (initialInstrument) goto gotAnInstrument;
			}

			// Otherwise, create a new one
			initialInstrument =
			    storageManager.createNewNonAudioInstrument(initialInstrumentType, initialChannel, initialChannelSuffix);
			if (!initialInstrument) return;
		}

		// Synth / kit...
		else {

			// Search hibernating Instruments
			initialInstrument = currentSong->getInstrumentFromPresetSlot(initialInstrumentType, 0, 0, initialName.get(),
			                                                             initialDirPath.get(), true, false);

			// If found hibernating synth or kit...
			if (initialInstrument) {
				// Must remove it from hibernation list
				currentSong->removeInstrumentFromHibernationList(initialInstrument);
			}

			// Or if could not find hibernating synth or kit...
			else {

				// Set this stuff so that getCurrentFilePath() will return what we want. This is just ok because we're exiting anyway
				instrumentTypeToLoad = initialInstrumentType;
				enteredText.set(&initialName);
				currentDir.set(&initialDirPath);

				// Try getting from file
				String filePath;
				int error = getCurrentFilePath(&filePath);
				if (error) return;

				FilePointer tempFilePointer;

				bool success = storageManager.fileExists(filePath.get(), &tempFilePointer);
				if (!success) return;

				error = storageManager.loadInstrumentFromFile(currentSong, instrumentClipToLoadFor,
				                                              initialInstrumentType, false, &initialInstrument,
				                                              &tempFilePointer, &initialName, &initialDirPath);
				if (error) return;
			}

			initialInstrument->loadAllAudioFiles(true);
		}
	}

gotAnInstrument:

	// If swapping whole Instrument...
	if (replacedWholeInstrument) {

		// We know the Instrument hasn't been added to the Song, and this call will do it
		currentSong->replaceInstrument(instrumentToReplace, initialInstrument);

		replacedWholeInstrument = true;
	}

	// Otherwise, just changeInstrument() for this one Clip.
	else {

		// If that Instrument wasn't already in use in the Song, copy default velocity over
		initialInstrument->defaultVelocity = instrumentToReplace->defaultVelocity;

		// If we're here, we know the Clip is not playing in the arranger (and doesn't even have an instance in there)

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);

		int error = instrumentClipToLoadFor->changeInstrument(modelStack, initialInstrument, NULL,
		                                                      INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED);
		// TODO: deal with errors!

		if (needToAddInstrumentToSong) {
			currentSong->addOutput(initialInstrument);
		}

		changedInstrumentForClip = true;
	}
}

bool LoadInstrumentPresetUI::isInstrumentInList(Instrument* searchInstrument, Output* list) {
	while (list) {
		if (list == searchInstrument) return true;
		list = list->next;
	}
	return false;
}

// Returns whether it was in fact an unused one that it was able to return
bool LoadInstrumentPresetUI::findUnusedSlotVariation(String* oldName, String* newName) {

	char const* oldNameChars = oldName->get();
	int oldNameLength = strlen(oldNameChars);

#if !HAVE_OLED
	int subSlot = -1;
	// For numbered slots
	if (oldNameLength == 3) {
doSlotNumber:
		char buffer[5];
		buffer[0] = oldNameChars[0];
		buffer[1] = oldNameChars[1];
		buffer[2] = oldNameChars[2];
		buffer[3] = 0;
		buffer[4] = 0;
		int slotNumber = stringToUIntOrError(buffer);
		if (slotNumber < 0) goto nonNumeric;

		while (true) {
			// Try next subSlot up
			subSlot++;

			// If reached end of alphabet/subslots, try next number up.
			if (subSlot >= 26) goto tryWholeNewSlotNumbers;

			buffer[3] = 'A' + subSlot;

			int i = fileItems.search(buffer);
			if (i >= fileItems.getNumElements()) break;

			FileItem* fileItem = (FileItem*)fileItems.getElementAddress(i);
			char const* fileItemNameChars = fileItem->filename.get();
			if (!memcasecmp(buffer, fileItemNameChars, 4)) {
				if (fileItemNameChars[4] == 0) continue;
				if (fileItemNameChars[4] == '.' && fileItem->filenameIncludesExtension) continue;
			}
			break;
		}

		if (false) {
tryWholeNewSlotNumbers:
			while (true) {
				slotNumber++;
				if (slotNumber >= numSongSlots) {
					newName->set(oldName);
					return false;
				}
				intToString(slotNumber, buffer, 3);

				int i = fileItems.search(buffer);
				if (i >= fileItems.getNumElements()) break;

				FileItem* fileItem = (FileItem*)fileItems.getElementAddress(i);
				char const* fileItemNameChars = fileItem->filename.get();
				if (!memcasecmp(buffer, fileItemNameChars, 4)) {
					if (fileItemNameChars[4] == 0) continue;
					if (fileItemNameChars[4] == '.' && fileItem->filenameIncludesExtension) continue;
				}
				break;
			}
		}

		newName->set(buffer);
	}
	else if (oldNameLength == 4) {
		char subSlotChar = oldNameChars[3];
		if (subSlotChar >= 'a' && subSlotChar <= 'z') subSlot = subSlotChar - 'a';
		else if (subSlotChar >= 'A' && subSlotChar <= 'Z') subSlot = subSlotChar - 'A';
		else goto nonNumeric;

		goto doSlotNumber;
	}

	// Or, for named slots
	else
#endif
	{
nonNumeric:
		int oldNumber = 1;
		newName->set(oldName);

		int numberStartPos;

		char const* underscoreAddress = strrchr(oldNameChars, ' ');
		if (underscoreAddress) {
lookAtSuffixNumber:
			int underscorePos = (uint32_t)underscoreAddress - (uint32_t)oldNameChars;
			numberStartPos = underscorePos + 1;
			int oldNumberLength = oldNameLength - numberStartPos;
			if (oldNumberLength > 0) {
				int numberHere = stringToUIntOrError(&oldNameChars[numberStartPos]);
				if (numberHere >= 0) { // If it actually was a number, as opposed to other chars
					oldNumber = numberHere;
					newName->shorten(numberStartPos);
					goto addNumber;
				}
			}
		}
		else {
			underscoreAddress = strrchr(oldNameChars, '_');
			if (underscoreAddress) goto lookAtSuffixNumber;
		}

		numberStartPos = oldNameLength + 1;
		newName->concatenate(" ");

addNumber:
		for (;; oldNumber++) {
			newName->shorten(numberStartPos);
			newName->concatenateInt(oldNumber + 1);
			char const* newNameChars = newName->get();

			int i = fileItems.search(newNameChars);
			if (i >= fileItems.getNumElements()) break;

			FileItem* fileItem = (FileItem*)fileItems.getElementAddress(i);
			char const* fileItemNameChars = fileItem->filename.get();
			int newNameLength = strlen(newNameChars);
			if (!memcasecmp(newNameChars, fileItemNameChars, newNameLength)) {
				if (fileItemNameChars[newNameLength] == 0) continue;
				if (fileItemNameChars[newNameLength] == '.' && fileItem->filenameIncludesExtension) continue;
			}

			break;
		}
	}

	return true;
}

// I thiiink you're supposed to check currentFileExists before calling this?
int LoadInstrumentPresetUI::performLoad(bool doClone) {

	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) return ERROR_UNSPECIFIED;

	if (currentFileItem->isFolder) return NO_ERROR;
	if (currentFileItem->instrument == instrumentToReplace && !doClone)
		return NO_ERROR; // Happens if navigate over a folder's name (Instrument stays the same),
		    // then back onto that neighbouring Instrument - you'd incorrectly get a "USED" error without this line.

	// Work out availabilityRequirement. This can't change as presets are navigated through... I don't think?
	int availabilityRequirement;
	bool oldInstrumentCanBeReplaced;
	if (instrumentClipToLoadFor) {
		oldInstrumentCanBeReplaced =
		    currentSong->canOldOutputBeReplaced(instrumentClipToLoadFor, &availabilityRequirement);
	}

	else {
		oldInstrumentCanBeReplaced = true;
		availabilityRequirement = AVAILABILITY_INSTRUMENT_UNUSED;
	}

	bool shouldReplaceWholeInstrument;
	bool needToAddInstrumentToSong;
	bool loadedFromFile = false;

	Instrument* newInstrument = currentFileItem->instrument;

	bool newInstrumentWasHibernating = false;

	// If we found an already existing Instrument object...
	if (!doClone && newInstrument) {

		newInstrumentWasHibernating = isInstrumentInList(newInstrument, currentSong->firstHibernatingInstrument);

		if (availabilityRequirement == AVAILABILITY_INSTRUMENT_UNUSED) {
			if (!newInstrumentWasHibernating) {
giveUsedError:
				return ERROR_PRESET_IN_USE;
			}
		}

		else if (availabilityRequirement == AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION) {
			if (!newInstrumentWasHibernating && currentSong->doesOutputHaveActiveClipInSession(newInstrument)) {
				goto giveUsedError;
			}
		}

		// Ok, we can have it!
		shouldReplaceWholeInstrument = (oldInstrumentCanBeReplaced && newInstrumentWasHibernating);
		needToAddInstrumentToSong = newInstrumentWasHibernating;
	}

	// Or, if we need to load from file - perhaps forcibly because the user manually chose to clone...
	else {

		String clonedName;

		if (doClone) {
			bool success = findUnusedSlotVariation(&enteredText, &clonedName);
			if (!success) return ERROR_UNSPECIFIED;
		}

		int error = storageManager.loadInstrumentFromFile(currentSong, instrumentClipToLoadFor, instrumentTypeToLoad,
		                                                  false, &newInstrument, &currentFileItem->filePointer,
		                                                  &enteredText, &currentDir);

		if (error) return error;

		shouldReplaceWholeInstrument = oldInstrumentCanBeReplaced;
		needToAddInstrumentToSong = true;
		loadedFromFile = true;

		if (doClone) {
			newInstrument->name.set(&clonedName);
			newInstrument->editedByUser = true;
		}
	}
#if HAVE_OLED
	OLED::displayWorkingAnimation("Loading");
#else
	numericDriver.displayLoadingAnimation(false, true);
#endif
	int error = newInstrument->loadAllAudioFiles(true);

#if HAVE_OLED
	OLED::removeWorkingAnimation();
#else
	numericDriver.removeTopLayer();
#endif

	// If error, most likely because user interrupted sample loading process...
	if (error) {
		// Probably need to do some cleaning up of the new Instrument
		if (loadedFromFile) currentSong->deleteOutput(newInstrument);

		return error;
	}

	if (newInstrumentWasHibernating) {
		currentSong->removeInstrumentFromHibernationList(newInstrument);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);

	// If swapping whole Instrument...
	if (shouldReplaceWholeInstrument) {

		// We know the Instrument hasn't been added to the Song, and this call will do it
		currentSong->replaceInstrument(instrumentToReplace, newInstrument);

		replacedWholeInstrument = true;
	}

	// Otherwise, just changeInstrument() for this one Clip
	else {

		// If that Instrument wasn't already in use in the Song, copy default velocity over
		newInstrument->defaultVelocity = instrumentToReplace->defaultVelocity;

		// If we're here, we know the Clip is not playing in the arranger (and doesn't even have an instance in there)

		int error = instrumentClipToLoadFor->changeInstrument(
		    modelStack, newInstrument, NULL, INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED, NULL, true);
		// TODO: deal with errors!

		if (needToAddInstrumentToSong) {
			currentSong->addOutput(newInstrument);
		}

		changedInstrumentForClip = true;
	}

	// Check if old Instrument has been deleted, in which case need to update the appropriate FileItem.
	if (!isInstrumentInList(instrumentToReplace, currentSong->firstOutput)
	    && !isInstrumentInList(instrumentToReplace, currentSong->firstHibernatingInstrument)) {
		for (int f = fileItems.getNumElements() - 1; f >= 0; f--) {
			FileItem* fileItem = (FileItem*)fileItems.getElementAddress(f);
			if (fileItem->instrument == instrumentToReplace) {
				fileItem->instrument = NULL;
				break;
			}
		}
	}

	currentFileItem->instrument = newInstrument;
	currentInstrument = newInstrument;

	if (instrumentClipToLoadFor) {
		view.instrumentChanged(modelStack,
		                       newInstrument); // modelStack's TimelineCounter is set to instrumentClipToLoadFor, FYI

		if (showingAuditionPads()) {
			renderingNeededRegardlessOfUI(0, 0xFFFFFFFF);
		}
	}
	else {
		currentSong->instrumentSwapped(newInstrument);
		view.setActiveModControllableTimelineCounter(newInstrument->activeClip);
	}

	instrumentToReplace = newInstrument;
#if HAVE_OLED
	OLED::removeWorkingAnimation();
#endif

	return NO_ERROR;
}

// Previously called "exitAndResetInstrumentToInitial()". Does just that.
void LoadInstrumentPresetUI::exitAction() {
	revertToInitialPreset();
	LoadUI::exitAction();
}

int LoadInstrumentPresetUI::padAction(int x, int y, int on) {

	// Audition pad
	if (x == displayWidth + 1) {
		if (!showingAuditionPads()) goto potentiallyExit;
		if (currentInstrumentLoadError) {
			if (on) numericDriver.displayError(currentInstrumentLoadError);
		}
		else {
			return instrumentClipView.padAction(x, y, on);
		}
	}

	// Mute pad
	else if (x == displayWidth) {
potentiallyExit:
		if (on && !currentUIMode) {
			if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			exitAction();
		}
	}

	else {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
		goto potentiallyExit;
#else
		return LoadUI::padAction(x, y, on);
#endif
	}

	return ACTION_RESULT_DEALT_WITH;
}

int LoadInstrumentPresetUI::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (showingAuditionPads()) {
		if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(xEncButtonX, xEncButtonY))
			return ACTION_RESULT_DEALT_WITH;

		int result = instrumentClipView.verticalEncoderAction(offset, inCardRoutine);

		if (result == ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE) return result;

		if (getRootUI() == &keyboardScreen) {
			uiNeedsRendering(this, 0, 0xFFFFFFFF);
		}

		return result;
	}

	return ACTION_RESULT_DEALT_WITH;
}

bool LoadInstrumentPresetUI::renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                           uint8_t occupancyMask[][displayWidth + sideBarWidth]) {
	if (getRootUI() != &keyboardScreen) return false;
	return instrumentClipView.renderSidebar(whichRows, image, occupancyMask);
}

bool LoadInstrumentPresetUI::showingAuditionPads() {
	return getRootUI()->toClipMinder();
}

void LoadInstrumentPresetUI::instrumentEdited(Instrument* instrument) {
	if (instrument == currentInstrument && !currentInstrumentLoadError && enteredText.isEmpty()) {
		enteredText.set(&instrument->name);
		// TODO: update the FileItem too?
		displayText(false);
	}
}
