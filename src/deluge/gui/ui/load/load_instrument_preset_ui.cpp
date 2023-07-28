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

#include "gui/views/arranger_view.h"
#include "storage/audio/audio_file_manager.h"
#include "model/clip/instrument_clip.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/root_ui.h"
#include "util/functions.h"
#include "model/song/song.h"
#include "model/instrument/instrument.h"
#include "hid/display/numeric_driver.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/print.h"
#include "gui/views/view.h"
#include "storage/storage_manager.h"
#include "gui/ui/keyboard_screen.h"
#include "model/action/action_logger.h"
#include "model/instrument/midi_instrument.h"
#include "gui/context_menu/load_instrument_preset.h"
#include "hid/led/pad_leds.h"
#include "hid/led/indicator_leds.h"
#include "hid/encoders.h"
#include "hid/buttons.h"
#include "extern.h"
#include "gui/ui_timer_manager.h"
#include "storage/file_item.h"
#include "hid/display/oled.h"
#include "processing/engines/audio_engine.h"

using namespace deluge;

LoadInstrumentPresetUI loadInstrumentPresetUI{};

LoadInstrumentPresetUI::LoadInstrumentPresetUI() {
}

bool LoadInstrumentPresetUI::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	if (showingAuditionPads()) {
		*cols = 0b10;
	}
	else {
		*cols = 0xFFFFFFFF;
	}
	return true;
}

bool LoadInstrumentPresetUI::opened() {

	if (getRootUI() == &keyboardScreen) {
		PadLEDs::skipGreyoutFade();
	}

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
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);

	if (instrumentTypeToLoad == INSTRUMENT_TYPE_SYNTH) {
		indicator_leds::blinkLed(IndicatorLED::SYNTH);
	}
	else {
		indicator_leds::blinkLed(IndicatorLED::KIT);
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

		if (currentDir.isEmpty()) {
			goto useDefaultFolder;
		}
	}

	// Or if the Instruments are different types...
	else {

		// If we've got a Clip, we can see if it used to use another Instrument of this new type...
		if (instrumentClipToLoadFor) {
			String* backedUpName = &instrumentClipToLoadFor->backedUpInstrumentName[instrumentTypeToLoad];
			enteredText.set(backedUpName);
			searchFilename.set(backedUpName);
			currentDir.set(&instrumentClipToLoadFor->backedUpInstrumentDirPath[instrumentTypeToLoad]);
			if (currentDir.isEmpty()) {
				goto useDefaultFolder;
			}
		}
		// Otherwise we just start with nothing. currentSlot etc remain set to "zero" from before
		else {
useDefaultFolder:
			int error = currentDir.set(defaultDir);
			if (error) {
				return error;
			}
		}
	}

	if (!searchFilename.isEmpty()) {
		int error = searchFilename.concatenate(".XML");
		if (error) {
			return error;
		}
	}

	int error = arrivedInNewFolder(0, searchFilename.get(), defaultDir);
	if (error) {
		return error;
	}

	currentInstrumentLoadError = (fileIndexSelected >= 0) ? NO_ERROR : ERROR_UNSPECIFIED;

	// The redrawing of the sidebar only actually has to happen if we just changed to a different type *or* if we came in from (musical) keyboard view, I think
	PadLEDs::clearAllPadsWithoutSending();
	drawKeys();
	PadLEDs::sendOutMainPadColours();

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
	if (!currentFileItem) {
		return;
	}

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

int LoadInstrumentPresetUI::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	int newInstrumentType;

	// Load button
	if (b == LOAD) {
		return mainButtonAction(on);
	}

	// Synth button
	else if (b == SYNTH) {
		newInstrumentType = INSTRUMENT_TYPE_SYNTH;
doChangeInstrumentType:
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			convertToPrefixFormatIfPossible(); // Why did I put this here?
			changeInstrumentType(newInstrumentType);
		}
	}

	// Kit button
	else if (b == KIT) {
		if (instrumentClipToLoadFor && instrumentClipToLoadFor->onKeyboardScreen) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::KEYBOARD);
		}
		else {
			newInstrumentType = INSTRUMENT_TYPE_KIT;
			goto doChangeInstrumentType;
		}
	}

	// MIDI button
	else if (b == MIDI) {
		newInstrumentType = INSTRUMENT_TYPE_MIDI_OUT;
		goto doChangeInstrumentType;
	}

	// CV button
	else if (b == CV) {
		newInstrumentType = INSTRUMENT_TYPE_CV;
		goto doChangeInstrumentType;
	}

	else {
		return LoadUI::buttonAction(b, on, inCardRoutine);
	}

	return ACTION_RESULT_DEALT_WITH;
}

int LoadInstrumentPresetUI::timerCallback() {
	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {

		if (sdRoutineLock) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // The below needs to access the card.
		}

		currentUIMode = UI_MODE_NONE;

		FileItem* currentFileItem = getCurrentFileItem();

		// Folders don't have a context menu
		if (!currentFileItem || currentFileItem->isFolder) {
			return ACTION_RESULT_DEALT_WITH;
		}

		// We want to open the context menu to choose to reload the original file for the currently selected preset in some way.
		// So first up, make sure there is a file, and that we've got its pointer
		String filePath;
		int error = getCurrentFilePath(&filePath);
		if (error) {
			return error;
		}

		bool fileExists = storageManager.fileExists(filePath.get(), &currentFileItem->filePointer);
		if (!fileExists) {
			numericDriver.displayError(ERROR_FILE_NOT_FOUND);
			return ACTION_RESULT_DEALT_WITH;
		}

		bool available = gui::context_menu::loadInstrumentPreset.setupAndCheckAvailability();

		if (available) {
			numericDriver.setNextTransitionDirection(1);
			convertToPrefixFormatIfPossible();
			openUI(&gui::context_menu::loadInstrumentPreset);
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
	if (newInstrumentType == instrumentTypeToLoad) {
		return;
	}

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
	if (changedInstrumentForClip == replacedWholeInstrument) {
		return;
	}

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
	if (replacedWholeInstrument && !oldInstrumentCanBeReplaced) {
		return;
	}

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
				if (initialInstrument) {
					goto gotAnInstrument;
				}
			}

			// Otherwise, create a new one
			initialInstrument =
			    storageManager.createNewNonAudioInstrument(initialInstrumentType, initialChannel, initialChannelSuffix);
			if (!initialInstrument) {
				return;
			}
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
				if (error) {
					return;
				}

				FilePointer tempFilePointer;

				bool success = storageManager.fileExists(filePath.get(), &tempFilePointer);
				if (!success) {
					return;
				}

				error = storageManager.loadInstrumentFromFile(currentSong, instrumentClipToLoadFor,
				                                              initialInstrumentType, false, &initialInstrument,
				                                              &tempFilePointer, &initialName, &initialDirPath);
				if (error) {
					return;
				}
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
		if (list == searchInstrument) {
			return true;
		}
		list = list->next;
	}
	return false;
}

// Returns whether it was in fact an unused one that it was able to return
bool LoadInstrumentPresetUI::findUnusedSlotVariation(String* oldName, String* newName) {

	shouldInterpretNoteNames = false;

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
		if (slotNumber < 0) {
			goto nonNumeric;
		}

		while (true) {
			// Try next subSlot up
			subSlot++;

			// If reached end of alphabet/subslots, try next number up.
			if (subSlot >= 26) {
				goto tryWholeNewSlotNumbers;
			}

			buffer[3] = 'A' + subSlot;

			int i = fileItems.search(buffer);
			if (i >= fileItems.getNumElements()) {
				break;
			}

			FileItem* fileItem = (FileItem*)fileItems.getElementAddress(i);
			char const* fileItemNameChars = fileItem->filename.get();
			if (!memcasecmp(buffer, fileItemNameChars, 4)) {
				if (fileItemNameChars[4] == 0) {
					continue;
				}
				if (fileItemNameChars[4] == '.' && fileItem->filenameIncludesExtension) {
					continue;
				}
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
				if (i >= fileItems.getNumElements()) {
					break;
				}

				FileItem* fileItem = (FileItem*)fileItems.getElementAddress(i);
				char const* fileItemNameChars = fileItem->filename.get();
				if (!memcasecmp(buffer, fileItemNameChars, 4)) {
					if (fileItemNameChars[4] == 0) {
						continue;
					}
					if (fileItemNameChars[4] == '.' && fileItem->filenameIncludesExtension) {
						continue;
					}
				}
				break;
			}
		}

		newName->set(buffer);
	}
	else if (oldNameLength == 4) {
		char subSlotChar = oldNameChars[3];
		if (subSlotChar >= 'a' && subSlotChar <= 'z') {
			subSlot = subSlotChar - 'a';
		}
		else if (subSlotChar >= 'A' && subSlotChar <= 'Z') {
			subSlot = subSlotChar - 'A';
		}
		else {
			goto nonNumeric;
		}

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
			if (underscoreAddress) {
				goto lookAtSuffixNumber;
			}
		}

		numberStartPos = oldNameLength + 1;
		newName->concatenate(" ");

addNumber:
		for (;; oldNumber++) {
			newName->shorten(numberStartPos);
			newName->concatenateInt(oldNumber + 1);
			char const* newNameChars = newName->get();

			int i = fileItems.search(newNameChars);
			if (i >= fileItems.getNumElements()) {
				break;
			}

			FileItem* fileItem = (FileItem*)fileItems.getElementAddress(i);
			char const* fileItemNameChars = fileItem->filename.get();
			int newNameLength = strlen(newNameChars);
			if (!memcasecmp(newNameChars, fileItemNameChars, newNameLength)) {
				if (fileItemNameChars[newNameLength] == 0) {
					continue;
				}
				if (fileItemNameChars[newNameLength] == '.' && fileItem->filenameIncludesExtension) {
					continue;
				}
			}

			break;
		}
	}

	return true;
}

// I thiiink you're supposed to check currentFileExists before calling this?
int LoadInstrumentPresetUI::performLoad(bool doClone) {

	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return HAVE_OLED
		           ? ERROR_FILE_NOT_FOUND
		           : ERROR_NO_FURTHER_FILES_THIS_DIRECTION; // Make it say "NONE" on numeric Deluge, for consistency with old times.
	}

	if (currentFileItem->isFolder) {
		return NO_ERROR;
	}
	if (currentFileItem->instrument == instrumentToReplace && !doClone) {
		return NO_ERROR; // Happens if navigate over a folder's name (Instrument stays the same),
	}
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
			if (!success) {
				return ERROR_UNSPECIFIED;
			}
		}

		int error = storageManager.loadInstrumentFromFile(currentSong, instrumentClipToLoadFor, instrumentTypeToLoad,
		                                                  false, &newInstrument, &currentFileItem->filePointer,
		                                                  &enteredText, &currentDir);

		if (error) {
			return error;
		}

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
		if (loadedFromFile) {
			currentSong->deleteOutput(newInstrument);
		}

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
		if (!showingAuditionPads()) {
			goto potentiallyExit;
		}
		if (currentInstrumentLoadError) {
			if (on) {
				numericDriver.displayError(currentInstrumentLoadError);
			}
		}
		else {
			return instrumentClipView.padAction(x, y, on);
		}
	}

	// Mute pad
	else if (x == displayWidth) {
potentiallyExit:
		if (on && !currentUIMode) {
			if (sdRoutineLock) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitAction();
		}
	}

	else {
		return LoadUI::padAction(x, y, on);
	}

	return ACTION_RESULT_DEALT_WITH;
}

int LoadInstrumentPresetUI::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (showingAuditionPads()) {
		if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(hid::button::X_ENC)) {
			return ACTION_RESULT_DEALT_WITH;
		}

		int result = instrumentClipView.verticalEncoderAction(offset, inCardRoutine);

		if (result == ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE) {
			return result;
		}

		if (getRootUI() == &keyboardScreen) {
			uiNeedsRendering(this, 0, 0xFFFFFFFF);
		}

		return result;
	}

	return ACTION_RESULT_DEALT_WITH;
}

bool LoadInstrumentPresetUI::renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                           uint8_t occupancyMask[][displayWidth + sideBarWidth]) {
	if (getRootUI() != &keyboardScreen) {
		return false;
	}
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

// Caller must set currentDir before calling this.
// Caller must call emptyFileItems() at some point after calling this function.
// song may be supplied as NULL, in which case it won't be searched for Instruments; sometimes this will get called when the currentSong is not set up.
ReturnOfConfirmPresetOrNextUnlaunchedOne
LoadInstrumentPresetUI::findAnUnlaunchedPresetIncludingWithinSubfolders(Song* song, int instrumentType,
                                                                        int availabilityRequirement) {

	AudioEngine::logAction("findAnUnlaunchedPresetIncludingWithinSubfolders");
	allowedFileExtensions = allowedFileExtensionsXML;

	ReturnOfConfirmPresetOrNextUnlaunchedOne toReturn;

	int initialDirLength = currentDir.getLength();

	int folderIndex = -1;
	bool doingSubfolders = false;
	String searchNameLocalCopy;

goAgain:

	toReturn.error = readFileItemsFromFolderAndMemory(song, instrumentType, getThingName(instrumentType),
	                                                  searchNameLocalCopy.get(), NULL, true);

	if (toReturn.error) {
emptyFileItemsAndReturn:
		emptyFileItems();
doReturn:
		return toReturn;
	}

	sortFileItems();

	// If that folder-read gave us no files, that's gotta mean we got to the end of the folder.
	if (!fileItems.getNumElements()) {

		// If we weren't yet looking at subfolders, do that now, going back to the start of this folder's contents.
		if (!doingSubfolders) {
startDoingFolders:
			doingSubfolders = true;
			searchNameLocalCopy.clear();
			goto goAgain;
		}

		// Or if we already were looking at subfolders, we're all outta options now.
		else {
noFurtherFiles:
			toReturn.error = ERROR_NO_FURTHER_FILES_THIS_DIRECTION;
			goto doReturn;
		}
	}

	// Store rightmost display name before filtering, for later.
	String lastFileItemDisplayNameBeforeFiltering;
	FileItem* rightmostFileItemBeforeFiltering = (FileItem*)fileItems.getElementAddress(fileItems.getNumElements() - 1);
	toReturn.error = lastFileItemDisplayNameBeforeFiltering.set(rightmostFileItemBeforeFiltering->displayName);
	if (toReturn.error) {
		goto doReturn;
	}

	deleteFolderAndDuplicateItems(availabilityRequirement);

	// If we're still looking for preset / XML files, and not subfolders yet...
	if (!doingSubfolders) {

		// Look through our list of FileItems, for a preset.
		for (int i = 0; i < fileItems.getNumElements(); i++) {
			toReturn.fileItem = (FileItem*)fileItems.getElementAddress(i);
			if (!toReturn.fileItem->isFolder) {
				goto doReturn; // We found a preset / file.
			}
		}

		// Ok, we found none. Should we do some more reading of the folder contents, to get more files, or are there no more?
		if (numFileItemsDeletedAtEnd) {
			searchNameLocalCopy.set(&lastFileItemDisplayNameBeforeFiltering); // Can't fail.
			goto goAgain;
		}

		// Ok, we've looked at every file, and none were presets we could use. So now we want to look in subfolders. Do we still have the "start" of our folder's contents in memory?
		if (numFileItemsDeletedAtStart) {
			goto startDoingFolders;
		}

		doingSubfolders = true;
	}

	// Ok, do folders now.
	int i;
	for (i = 0; i < fileItems.getNumElements(); i++) {
		toReturn.fileItem = (FileItem*)fileItems.getElementAddress(i);
		if (toReturn.fileItem->isFolder) {
			goto doThisFolder;
		}
	}
	if (numFileItemsDeletedAtEnd) {
		searchNameLocalCopy.set(&lastFileItemDisplayNameBeforeFiltering);
		goto goAgain;
	}
	else {
		goto noFurtherFiles;
	}

	if (false) {
doThisFolder:
		bool anyMoreForLater = numFileItemsDeletedAtEnd || (i < (fileItems.getNumElements() - 1));
		searchNameLocalCopy.set(toReturn.fileItem->displayName);

		toReturn.error = currentDir.concatenate("/");
		if (toReturn.error) {
			goto emptyFileItemsAndReturn;
		}
		toReturn.error = currentDir.concatenate(&toReturn.fileItem->filename);
		if (toReturn.error) {
			goto emptyFileItemsAndReturn;
		}

		// Call self
		toReturn = findAnUnlaunchedPresetIncludingWithinSubfolders(song, instrumentType, availabilityRequirement);
		if (toReturn.error == ERROR_NO_FURTHER_FILES_THIS_DIRECTION) {
			if (anyMoreForLater) {
				currentDir.shorten(initialDirLength);
				goto goAgain;
			}
			else {
				goto doReturn;
			}
		}
		else if (toReturn.error) {
			goto emptyFileItemsAndReturn;
		}

		// If still here, the recursive call found something, so return.
		goto doReturn;
	}
}

// Caller must call emptyFileItems() at some point after calling this function.
// And, set currentDir, before this is called.
ReturnOfConfirmPresetOrNextUnlaunchedOne
LoadInstrumentPresetUI::confirmPresetOrNextUnlaunchedOne(int instrumentType, String* searchName,
                                                         int availabilityRequirement) {
	ReturnOfConfirmPresetOrNextUnlaunchedOne toReturn;

	String searchNameLocalCopy;
	searchNameLocalCopy.set(searchName); // Can't fail.
	bool shouldJustGrabLeftmost = false;

doReadFiles:
	toReturn.error = readFileItemsFromFolderAndMemory(currentSong, instrumentType, getThingName(instrumentType),
	                                                  searchNameLocalCopy.get(), NULL, false, availabilityRequirement);

	AudioEngine::logAction("confirmPresetOrNextUnlaunchedOne");

	if (toReturn.error == ERROR_FOLDER_DOESNT_EXIST) {
justGetAnyPreset: // This does *not* favour the currentDir, so you should exhaust all avenues before calling this.
		toReturn.error = currentDir.set(getInstrumentFolder(instrumentType));
		if (toReturn.error) {
			goto doReturn;
		}
		toReturn =
		    findAnUnlaunchedPresetIncludingWithinSubfolders(currentSong, instrumentType, availabilityRequirement);
		goto doReturn;
	}
	else if (toReturn.error) {
doReturn:
		return toReturn;
	}

	sortFileItems();
	if (!fileItems.getNumElements()) {
		if (shouldJustGrabLeftmost) {
			goto justGetAnyPreset;
		}

		if (numFileItemsDeletedAtStart) {
needToGrabLeftmostButHaveToReadFirst:
			searchNameLocalCopy.clear();
			shouldJustGrabLeftmost = true;
			goto doReadFiles;
		}
		else {
			goto justGetAnyPreset;
		}
	}

	// Store rightmost display name before filtering, for later.
	String lastFileItemDisplayNameBeforeFiltering;
	FileItem* rightmostFileItemBeforeFiltering = (FileItem*)fileItems.getElementAddress(fileItems.getNumElements() - 1);
	toReturn.error = lastFileItemDisplayNameBeforeFiltering.set(rightmostFileItemBeforeFiltering->displayName);
	if (toReturn.error) {
		goto doReturn;
	}

	deleteFolderAndDuplicateItems(availabilityRequirement);

	// If we've shot off the end of the list, that means our searched-for preset didn't exist or wasn't available, and any subsequent ones which at first made it onto the
	// (possibly truncated) list also weren't available.
	if (!fileItems.getNumElements()) {
		if (numFileItemsDeletedAtEnd) { // Probably couldn'g happen anymore...
			// We have to read more FileItems, further to the right.
			searchNameLocalCopy.set(&lastFileItemDisplayNameBeforeFiltering); // Can't fail.
			goto doReadFiles;
		}
		else {
			// If we've already been trying to grab just any preset within this folder, well that's failed.
			if (shouldJustGrabLeftmost) {
				goto justGetAnyPreset;
			}

			// Otherwise, let's do that now:
			// We might have to go back and read FileItems again from the start...
			else if (numFileItemsDeletedAtStart) {
				goto needToGrabLeftmostButHaveToReadFirst;
			}

			// Or, if we've actually managed to fit the whole folder contents into our fileItems...
			else {
				// Well, if there's still nothing in that, then we really need to give up.
				if (!fileItems.getNumElements()) {
					goto justGetAnyPreset;
				}
				// Otherwise, everything's fine and we can just take the first element.
			}
		}
	}
	toReturn.fileItem = (FileItem*)fileItems.getElementAddress(0);
	goto doReturn;
}

// Caller must call emptyFileItems() at some point after calling this function - unless an error is returned.
// Caller must remove OLED working animation after calling this too.
PresetNavigationResult LoadInstrumentPresetUI::doPresetNavigation(int offset, Instrument* oldInstrument,
                                                                  int availabilityRequirement, bool doBlink) {

	AudioEngine::logAction("doPresetNavigation");

	currentDir.set(&oldInstrument->dirPath);
	int instrumentType = oldInstrument->type;

	PresetNavigationResult toReturn;

	String oldNameString; // We only might use this later for temporary storage
	String newName;

	oldNameString.set(&oldInstrument->name);
	toReturn.error = oldNameString.concatenate(".XML");
	if (toReturn.error) {
doReturn:
		return toReturn;
	}

readAgain:
	int newCatalogSearchDirection = (offset >= 0) ? CATALOG_SEARCH_RIGHT : CATALOG_SEARCH_LEFT;
readAgainWithSameOffset:
	toReturn.error =
	    readFileItemsForFolder(getThingName(instrumentType), false, allowedFileExtensionsXML, oldNameString.get(),
	                           FILE_ITEMS_MAX_NUM_ELEMENTS_FOR_NAVIGATION, newCatalogSearchDirection);

	if (toReturn.error) {
		goto doReturn;
	}

	AudioEngine::logAction("doPresetNavigation2");

	toReturn.error = currentSong->addInstrumentsToFileItems(instrumentType);
	if (toReturn.error) {
emptyFileItemsAndReturn:
		emptyFileItems();
		goto doReturn;
	}
	AudioEngine::logAction("doPresetNavigation3");

	sortFileItems();
	AudioEngine::logAction("doPresetNavigation4");

	deleteFolderAndDuplicateItems(AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION);
	AudioEngine::logAction("doPresetNavigation5");

	// Now that we've deleted duplicates etc...
	if (!fileItems.getNumElements()) {
reachedEnd:
		// If we've reached one end, try going again from the far other end.
		if (!oldNameString.isEmpty()) {
			oldNameString.clear();
			goto readAgainWithSameOffset;
		}
		else {
noErrorButGetOut:
			toReturn.error = NO_ERROR_BUT_GET_OUT;
			goto emptyFileItemsAndReturn;
		}
	}
	else if (fileItems.getNumElements() == 1
	         && ((FileItem*)fileItems.getElementAddress(0))->instrument == oldInstrument) {
		goto reachedEnd;
	}

	int i = (offset >= 0) ? 0 : (fileItems.getNumElements() - 1);
	/*
	if (i >= fileItems.getNumElements()) { // If not found *and* we'd be past the end of the list...
		if (offset >= 0) i = 0;
		else i = fileItems.getNumElements() - 1;
		goto doneMoving;
	}
	else {
		int oldNameLength = strlen(oldNameChars);
		FileItem* searchResultItem = (FileItem*)fileItems.getElementAddress(i);
		if (memcasecmp(oldNameChars, searchResultItem->displayName, oldNameLength)) {
notFound:	if (offset < 0) {
				i--;
				if (i < 0) i += fileItems.getNumElements();
			}
			goto doneMoving;
		}
		if (searchResultItem->filenameIncludesExtension) {
			if (strrchr(searchResultItem->displayName, '.') != &searchResultItem->displayName[oldNameLength]) goto notFound;
		}
		else {
			if (searchResultItem->displayName[oldNameLength] != 0) goto notFound;
		}
	}
*/
	if (false) {
moveAgain:
		// Move along list
		i += offset;
	}

	// If moved left off the start of the list...
	if (i < 0) {
		if (numFileItemsDeletedAtStart) {
			goto readAgain;
		}
		else { // Wrap to end
			if (numFileItemsDeletedAtEnd) {
searchFromOneEnd:
				oldNameString.clear();
				Debug::println("reloading and wrap");
				goto readAgain;
			}
			else {
				i = fileItems.getNumElements() - 1;
			}
		}
	}

	// Or if moved right off the end of the list...
	else if (i >= fileItems.getNumElements()) {
		if (numFileItemsDeletedAtEnd) {
			goto readAgain;
		}
		else { // Wrap to start
			if (numFileItemsDeletedAtStart) {
				goto searchFromOneEnd;
			}
			else {
				i = 0;
			}
		}
	}

doneMoving:
	toReturn.fileItem = (FileItem*)fileItems.getElementAddress(i);

	toReturn.loadedFromFile = false;
	bool isHibernating = toReturn.fileItem->instrument && !toReturn.fileItem->instrumentAlreadyInSong;

	if (toReturn.fileItem->instrument) {
		view.displayOutputName(toReturn.fileItem->instrument, doBlink);
	}
	else {
		toReturn.error = toReturn.fileItem->getDisplayNameWithoutExtension(&newName);
		if (toReturn.error) {
			goto emptyFileItemsAndReturn;
		}
		toReturn.error = oldNameString.set(toReturn.fileItem->displayName);
		if (toReturn.error) {
			goto emptyFileItemsAndReturn;
		}
		view.drawOutputNameFromDetails(instrumentType, 0, 0, newName.get(), false, doBlink);
	}

#if HAVE_OLED
	OLED::sendMainImage(); // Sorta cheating - bypassing the UI layered renderer.
#endif

	if (Encoders::encoders[ENCODER_SELECT].detentPos) {
		Debug::println("go again 1 --------------------------");

doPendingPresetNavigation:
		offset = Encoders::encoders[ENCODER_SELECT].getLimitedDetentPosAndReset();

		if (toReturn.loadedFromFile) {
			currentSong->deleteOutput(toReturn.fileItem->instrument);
			toReturn.fileItem->instrument = NULL;
		}
		goto moveAgain;
	}

	// Unlike in ClipMinder, there's no need to check whether we came back to the same Instrument, cos we've specified that we were looking for "unused" ones only

	if (!toReturn.fileItem->instrument) {
		toReturn.error = storageManager.loadInstrumentFromFile(
		    currentSong, NULL, instrumentType, false, &toReturn.fileItem->instrument, &toReturn.fileItem->filePointer,
		    &newName, &Browser::currentDir);
		if (toReturn.error) {
			goto emptyFileItemsAndReturn;
		}

		toReturn.loadedFromFile = true;

		if (Encoders::encoders[ENCODER_SELECT].detentPos) {
			Debug::println("go again 2 --------------------------");
			goto doPendingPresetNavigation;
		}
	}

	//view.displayOutputName(toReturn.fileItem->instrument);

#if HAVE_OLED
	OLED::displayWorkingAnimation("Loading");
#else
	numericDriver.displayLoadingAnimation(false, true);
#endif
	int oldUIMode = currentUIMode;
	currentUIMode = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
	toReturn.fileItem->instrument->loadAllAudioFiles(true);
	currentUIMode = oldUIMode;

	// If user wants to move on...
	if (Encoders::encoders[ENCODER_SELECT].detentPos) {
		Debug::println("go again 3 --------------------------");
		goto doPendingPresetNavigation;
	}

	if (isHibernating) {
		currentSong->removeInstrumentFromHibernationList(toReturn.fileItem->instrument);
	}

	goto doReturn;
}
