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

#include "gui/ui/load/load_instrument_preset_ui.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/context_menu/load_instrument_preset.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/root_ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

using namespace deluge;
namespace encoders = deluge::hid::encoders;
using encoders::EncoderName;

LoadInstrumentPresetUI loadInstrumentPresetUI{};

bool LoadInstrumentPresetUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	if (showingAuditionPads() && !qwertyAlwaysVisible) {
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
	if (instrumentToReplace) {
		initialOutputType = instrumentToReplace->type;
		initialName.set(&instrumentToReplace->name);
		initialDirPath.set(&instrumentToReplace->dirPath);
	}

	if (loadingSynthToKitRow) {
		initialOutputType = outputTypeToLoad = OutputType::SYNTH;
		if (soundDrumToReplace) {
			initialName.set(&soundDrumToReplace->name);
		}
		else {
			initialName.set("");
		}

		initialDirPath.set("SYNTHS");
	}

	switch (instrumentToReplace->type) {
	case OutputType::MIDI_OUT:
		initialChannelSuffix = ((MIDIInstrument*)instrumentToReplace)->channelSuffix;
		// No break

	case OutputType::CV:
		initialChannel = ((NonAudioInstrument*)instrumentToReplace)->getChannel();
		break;
	}

	changedInstrumentForClip = false;
	replacedWholeInstrument = false;

	if (instrumentClipToLoadFor) {
		instrumentClipToLoadFor
		    ->backupPresetSlot(); // Store this now cos we won't be storing it between each navigation we do
	}

	Error error = beginSlotSession(); // Requires currentDir to be set. (Not anymore?)
	if (error != Error::NONE) {
gotError:
		display->displayError(error);
		return false;
	}

	actionLogger.deleteAllLogs();

	error = setupForOutputType(); // Sets currentDir.
	if (error != Error::NONE) {
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads, in call to setupForOutputType().
		goto gotError;
	}

	focusRegained();
	return true;
}

// If OLED, then you should make sure renderUIsForOLED() gets called after this.
Error LoadInstrumentPresetUI::setupForOutputType() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);

	if (loadingSynthToKitRow) {
		indicator_leds::blinkLed(IndicatorLED::SYNTH);
		indicator_leds::blinkLed(IndicatorLED::KIT);
	}
	else if (outputTypeToLoad == OutputType::SYNTH) {
		indicator_leds::blinkLed(IndicatorLED::SYNTH);
	}
	else if (outputTypeToLoad == OutputType::MIDI_OUT) {
		indicator_leds::blinkLed(IndicatorLED::MIDI);
	}
	else {
		indicator_leds::blinkLed(IndicatorLED::KIT);
	}

	// reset
	fileIconPt2 = nullptr;
	fileIconPt2Width = 0;

	if (display->haveOLED()) {
		if (loadingSynthToKitRow) {
			title = "Synth to row";
			fileIcon = deluge::hid::display::OLED::synthIcon;
		}
		else {
			switch (outputTypeToLoad) {
			case OutputType::SYNTH:
				title = "Load synth";
				fileIcon = deluge::hid::display::OLED::synthIcon;
				break;
			case OutputType::KIT:
				title = "Load kit";
				fileIcon = deluge::hid::display::OLED::kitIcon;
				break;
			case OutputType::MIDI_OUT:
				title = "Load midi preset";
				fileIcon = deluge::hid::display::OLED::midiIcon;
				fileIconPt2 = deluge::hid::display::OLED::midiIconPt2;
				fileIconPt2Width = 1;
			}
		}
	}

	// not used for midi
	filePrefix = (outputTypeToLoad == OutputType::SYNTH) ? "SYNT" : "KIT";

	enteredText.clear();

	char const* defaultDir = getInstrumentFolder(outputTypeToLoad);

	String searchFilename;

	// I don't have this calling arrivedInNewFolder(), because as you can see below, we want to either just display the
	// existing preset, or call confirmPresetOrNextUnlaunchedOne() to skip any which aren't "available".

	// If same Instrument type as we already had...
	if (instrumentToReplace && instrumentToReplace->type == outputTypeToLoad) {

		// Then we can start by just looking at the existing Instrument, cos they're the same type...
		currentDir.set(&instrumentToReplace->dirPath);
		searchFilename.set(&instrumentToReplace->name);

		if (currentDir.isEmpty()) {
			goto useDefaultFolder;
		}
	}

	// Or if the Instruments are different types...
	else {
		if (loadingSynthToKitRow && soundDrumToReplace) {

			if (&soundDrumToReplace->name) {
				String* name = &soundDrumToReplace->name;
				enteredText.set(name);
				searchFilename.set(name);
			}

			if (&soundDrumToReplace->path) {
				currentDir.set(&soundDrumToReplace->path);
				if (currentDir.isEmpty()) {
					goto useDefaultFolder;
				}
			}

			else {
				goto useDefaultFolder;
			}
		}
		// If we've got a Clip, we can see if it used to use another Instrument of this new type...
		else if (instrumentClipToLoadFor && outputTypeToLoad != OutputType::MIDI_OUT) {
			const size_t outputTypeToLoadAsIdx = static_cast<size_t>(outputTypeToLoad);
			String* backedUpName = &instrumentClipToLoadFor->backedUpInstrumentName[outputTypeToLoadAsIdx];
			enteredText.set(backedUpName);
			searchFilename.set(backedUpName);
			currentDir.set(&instrumentClipToLoadFor->backedUpInstrumentDirPath[outputTypeToLoadAsIdx]);
			if (currentDir.isEmpty()) {
				goto useDefaultFolder;
			}
		}

		// Otherwise we just start with nothing. currentSlot etc remain set to "zero" from before
		else {
useDefaultFolder:
			Error error = currentDir.set(defaultDir);
			if (error != Error::NONE) {
				return error;
			}
		}
	}

	if (!searchFilename.isEmpty()) {
		Error error = searchFilename.concatenate(".XML");
		if (error != Error::NONE) {
			return error;
		}
	}

	Error error = arrivedInNewFolder(0, searchFilename.get(), defaultDir);
	if (error != Error::NONE) {
		return error;
	}

	currentInstrumentLoadError = (fileIndexSelected >= 0) ? Error::NONE : Error::UNSPECIFIED;

	// The redrawing of the sidebar only actually has to happen if we just changed to a different type *or* if we came
	// in from (musical) keyboard view, I think

	drawKeys();
	favouritesManager.setCategory(defaultDir);
	favouritesChanged();

	if (showingAuditionPads()) {
		instrumentClipView.recalculateColours();
		renderingNeededRegardlessOfUI(0, 0xFFFFFFFF);
	}

	if (display->have7SEG()) {
		displayText(false);
	}
	return Error::NONE;
}

void LoadInstrumentPresetUI::folderContentsReady(int32_t entryDirection) {
	currentFileChanged(0);
}

void LoadInstrumentPresetUI::currentFileChanged(int32_t movementDirection) {
	// FileItem* currentFileItem = getCurrentFileItem();

	// if (currentFileItem->instrument != instrumentToReplace) {

	currentUIMode = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
	if (loadingSynthToKitRow) {
		currentInstrumentLoadError = performLoadSynthToKit();
	}
	else {
		currentInstrumentLoadError = performLoad();
	}
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

		Error error = goIntoFolder(currentFileItem->filename.get());

		if (error != Error::NONE) {
			display->displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}

	else {

		if (currentInstrumentLoadError != Error::NONE) {
			if (loadingSynthToKitRow) {
				currentInstrumentLoadError = performLoadSynthToKit();
			}
			else {
				currentInstrumentLoadError = performLoad();
			}
			if (currentInstrumentLoadError != Error::NONE) {
				display->displayError(currentInstrumentLoadError);
				return;
			}
		}

		if (currentFileItem
		        ->instrument) { // When would this not have something? Well ok, maybe now that we have folders.
			convertToPrefixFormatIfPossible();
		}

		if (outputTypeToLoad == OutputType::KIT && showingAuditionPads()) {
			// New NoteRows have probably been created, whose colours haven't been grabbed yet.
			instrumentClipView.recalculateColours();
		}

		close();
	}
}

ActionResult LoadInstrumentPresetUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	OutputType newOutputType;

	// Load button
	if (b == LOAD) {
		return mainButtonAction(on);
	}

	// Synth button
	else if (b == SYNTH) {
		newOutputType = OutputType::SYNTH;
doChangeOutputType:
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			convertToPrefixFormatIfPossible(); // Why did I put this here?
			changeOutputType(newOutputType);
		}
	}

	// Kit button
	else if (b == KIT) {
		newOutputType = OutputType::KIT;
		goto doChangeOutputType;
	}

	// MIDI button
	else if (b == MIDI) {
		newOutputType = OutputType::MIDI_OUT;
		goto doChangeOutputType;
	}

	// CV button
	else if (b == CV) {
		newOutputType = OutputType::CV;
		goto doChangeOutputType;
	}

	else {
		return LoadUI::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult LoadInstrumentPresetUI::timerCallback() {
	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // The below needs to access the card.
		}

		currentUIMode = UI_MODE_NONE;

		FileItem* currentFileItem = getCurrentFileItem();

		// Folders don't have a context menu
		if (!currentFileItem || currentFileItem->isFolder) {
			return ActionResult::DEALT_WITH;
		}

		// We want to open the context menu to choose to reload the original file for the currently selected preset in
		// some way. So first up, make sure there is a file, and that we've got its pointer
		String filePath;
		Error error = getCurrentFilePath(&filePath);
		if (error != Error::NONE) {
			display->displayError(error);
			return ActionResult::DEALT_WITH;
		}

		bool fileExists = StorageManager::fileExists(filePath.get(), &currentFileItem->filePointer);
		if (!fileExists) {
			display->displayError(Error::FILE_NOT_FOUND);
			return ActionResult::DEALT_WITH;
		}

		bool available = gui::context_menu::loadInstrumentPreset.setupAndCheckAvailability();

		if (available) {
			display->setNextTransitionDirection(1);
			convertToPrefixFormatIfPossible();
			openUI(&gui::context_menu::loadInstrumentPreset);
		}
		else {
			exitUIMode(UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS);
		}

		return ActionResult::DEALT_WITH;
	}
	else {
		return LoadUI::timerCallback();
	}
}

void LoadInstrumentPresetUI::changeOutputType(OutputType newOutputType) {
	if (newOutputType == outputTypeToLoad) {
		return;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();

	// don't allow clip type change if clip is not empty
	// only impose this restriction if switching to/from kit clip
	if (((outputTypeToLoad == OutputType::KIT) || (newOutputType == OutputType::KIT))
	    && (!clip->isEmpty() || !clip->output->isEmpty())) {
		return;
	}

	// If CV, we have a different method for this, and the UI will be exited
	if (newOutputType == OutputType::CV) {

		Instrument* newInstrument;
		// In arranger...
		if (!instrumentClipToLoadFor) {
			newInstrument = currentSong->changeOutputType(instrumentToReplace, newOutputType);
		}

		// Or, in SessionView or a ClipMinder
		else {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);

			newInstrument = instrumentClipToLoadFor->changeOutputType(modelStack, newOutputType);
		}

		// If that succeeded, get out
		if (newInstrument) {

			// If going back to a view where the new selection won't immediately be displayed, gotta give some
			// confirmation
			if (!getRootUI()->toClipMinder()) {
				char const* message;
				if (display->haveOLED()) {
					message = "Instrument switched to CV channel";
				}
				else {
					message = "DONE";
				}
				display->displayPopup(message);
			}

			close();
		}
	}

	// Or, for normal synths, kits and midi
	else {
		OutputType oldOutputType = outputTypeToLoad;
		outputTypeToLoad = newOutputType;

		Error error = setupForOutputType();
		if (error != Error::NONE) {
			outputTypeToLoad = oldOutputType;
			return;
		}

		if (display->haveOLED()) {
			renderUIsForOled();
		}
		performLoad();
	}
}

void LoadInstrumentPresetUI::revertToInitialPreset() {

	// Can only do this if we've changed Instrument in one of the two ways, but not both.
	// TODO: that's very limiting, and I can't remember why I mandated this, or what would be so hard about allowing
	// this. Very often, the user might enter this interface for a Clip sharing its Output/Instrument with other Clips,
	// so when user starts navigating through presets, it'll first do a "change just for Clip", but then on the new
	// preset, this will now be the only Clip, so next time it'll do a "replace whole Instrument".
	if (changedInstrumentForClip == replacedWholeInstrument) {
		return;
	}

	Availability availabilityRequirement;
	bool oldInstrumentShouldBeReplaced;
	if (instrumentClipToLoadFor) {
		oldInstrumentShouldBeReplaced =
		    currentSong->shouldOldOutputBeReplaced(instrumentClipToLoadFor, &availabilityRequirement);
	}

	else {
		oldInstrumentShouldBeReplaced = true;
		availabilityRequirement = Availability::INSTRUMENT_UNUSED;
	}

	// If we're looking to replace the whole Instrument, but we're not allowed, that's obviously a no-go
	if (replacedWholeInstrument && !oldInstrumentShouldBeReplaced) {
		return;
	}

	bool needToAddInstrumentToSong = false;

	Instrument* initialInstrument = NULL;

	// Search main, non-hibernating Instruments
	initialInstrument = currentSong->getInstrumentFromPresetSlot(
	    initialOutputType, initialChannel, initialChannelSuffix, initialName.get(), initialDirPath.get(), false, true);

	// If we found it already as a non-hibernating one...
	if (initialInstrument) {

		// ... check that our availabilityRequirement allows this
		if (availabilityRequirement == Availability::INSTRUMENT_UNUSED) {
			return;
		}

		else if (availabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
			if (currentSong->doesOutputHaveActiveClipInSession(initialInstrument)) {
				return;
			}
		}
	}

	// Or if we did not find it as a non-hibernating one...
	else {

		needToAddInstrumentToSong = true;

		// MIDI / CV
		if (initialOutputType == OutputType::MIDI_OUT || initialOutputType == OutputType::CV) {

			// One MIDIInstrument may be hibernating...
			if (initialOutputType == OutputType::MIDI_OUT) {
				initialInstrument = currentSong->grabHibernatingMIDIInstrument(initialChannel, initialChannelSuffix);
				if (initialInstrument) {
					goto gotAnInstrument;
				}
			}

			// Otherwise, create a new one
			initialInstrument =
			    StorageManager::createNewNonAudioInstrument(initialOutputType, initialChannel, initialChannelSuffix);
			if (!initialInstrument) {
				return;
			}
		}

		// Synth / kit...
		else {

			// Search hibernating Instruments
			initialInstrument = currentSong->getInstrumentFromPresetSlot(initialOutputType, 0, 0, initialName.get(),
			                                                             initialDirPath.get(), true, false);

			// If found hibernating synth or kit...
			if (initialInstrument) {
				// Must remove it from hibernation list
				currentSong->removeInstrumentFromHibernationList(initialInstrument);
			}

			// Or if could not find hibernating synth or kit...
			else {

				// Set this stuff so that getCurrentFilePath() will return what we want. This is just ok because we're
				// exiting anyway
				outputTypeToLoad = initialOutputType;
				enteredText.set(&initialName);
				currentDir.set(&initialDirPath);

				// Try getting from file
				String filePath;
				Error error = getCurrentFilePath(&filePath);
				if (error != Error::NONE) {
					return;
				}

				FilePointer tempFilePointer;

				bool success = StorageManager::fileExists(filePath.get(), &tempFilePointer);
				if (!success) {
					return;
				}

				error = StorageManager::loadInstrumentFromFile(currentSong, instrumentClipToLoadFor, initialOutputType,
				                                               false, &initialInstrument, &tempFilePointer,
				                                               &initialName, &initialDirPath);
				if (error != Error::NONE) {
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

		Error error = instrumentClipToLoadFor->changeInstrument(modelStack, initialInstrument, NULL,
		                                                        InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED);
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
	int32_t oldNameLength = strlen(oldNameChars);

	if (display->have7SEG()) {
		int32_t subSlot = -1;
		// For numbered slots
		if (oldNameLength == 3) {
doSlotNumber:
			char buffer[5];
			buffer[0] = oldNameChars[0];
			buffer[1] = oldNameChars[1];
			buffer[2] = oldNameChars[2];
			buffer[3] = 0;
			buffer[4] = 0;
			int32_t slotNumber = stringToUIntOrError(buffer);
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

				int32_t i = fileItems.search(buffer);
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
					if (slotNumber >= kNumSongSlots) {
						newName->set(oldName);
						return false;
					}
					intToString(slotNumber, buffer, 3);

					int32_t i = fileItems.search(buffer);
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
		else {
			goto nonNumeric;
		}
	}

	{
nonNumeric:
		int32_t oldNumber = 1;
		newName->set(oldName);

		int32_t numberStartPos;

		char const* underscoreAddress = strrchr(oldNameChars, ' ');
		if (underscoreAddress) {
lookAtSuffixNumber:
			int32_t underscorePos = (uint32_t)underscoreAddress - (uint32_t)oldNameChars;
			numberStartPos = underscorePos + 1;
			int32_t oldNumberLength = oldNameLength - numberStartPos;
			if (oldNumberLength > 0) {
				int32_t numberHere = stringToUIntOrError(&oldNameChars[numberStartPos]);
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

			int32_t i = fileItems.search(newNameChars);
			if (i >= fileItems.getNumElements()) {
				break;
			}

			FileItem* fileItem = (FileItem*)fileItems.getElementAddress(i);
			char const* fileItemNameChars = fileItem->filename.get();
			int32_t newNameLength = strlen(newNameChars);
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
Error LoadInstrumentPresetUI::performLoad(bool doClone) {

	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem == nullptr) {
		// Make it say "NONE" on numeric Deluge, for
		// consistency with old times.
		return display->haveOLED() ? Error::FILE_NOT_FOUND : Error::NO_FURTHER_FILES_THIS_DIRECTION;
	}

	if (currentFileItem->isFolder) {
		return Error::NONE;
	}
	if (currentFileItem->instrument == instrumentToReplace && !doClone) {
		return Error::NONE; // Happens if navigate over a folder's name (Instrument stays the same),
	}

	// then back onto that neighbouring Instrument - you'd incorrectly get a "USED" error without this line.

	// Work out availabilityRequirement. This can't change as presets are navigated through... I don't think?
	Availability availabilityRequirement;
	bool oldInstrumentShouldBeReplaced;
	if (instrumentClipToLoadFor) {
		oldInstrumentShouldBeReplaced =
		    currentSong->shouldOldOutputBeReplaced(instrumentClipToLoadFor, &availabilityRequirement);
	}

	else {
		oldInstrumentShouldBeReplaced = true;
		availabilityRequirement = Availability::INSTRUMENT_UNUSED;
	}

	bool shouldReplaceWholeInstrument;
	bool needToAddInstrumentToSong;
	bool loadedFromFile = false;

	Instrument* newInstrument = currentFileItem->instrument;

	bool newInstrumentWasHibernating = false;

	// If we found an already existing Instrument object...
	if (!doClone && newInstrument) {

		newInstrumentWasHibernating = isInstrumentInList(newInstrument, currentSong->firstHibernatingInstrument);

		if (availabilityRequirement == Availability::INSTRUMENT_UNUSED) {
			if (!newInstrumentWasHibernating) {
giveUsedError:
				return Error::PRESET_IN_USE;
			}
		}

		else if (availabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
			if (!newInstrumentWasHibernating && currentSong->doesOutputHaveActiveClipInSession(newInstrument)) {
				goto giveUsedError;
			}
		}

		// Ok, we can have it!
		// this can only happen when changing a clip that is the only instance of its instrument to another instrument
		// that has an inactive clip already
		shouldReplaceWholeInstrument = (oldInstrumentShouldBeReplaced && newInstrumentWasHibernating);
		needToAddInstrumentToSong = newInstrumentWasHibernating;
	}

	// Or, if we need to load from file - perhaps forcibly because the user manually chose to clone...
	else {

		String clonedName;

		if (doClone) {
			bool success = findUnusedSlotVariation(&enteredText, &clonedName);
			if (!success) {
				return Error::UNSPECIFIED;
			}
		}
		Error error;
		// check if the file pointer matches the current file item
		// Browser::checkFP();

		// synth or kit
		error = StorageManager::loadInstrumentFromFile(currentSong, instrumentClipToLoadFor, outputTypeToLoad, false,
		                                               &newInstrument, &currentFileItem->filePointer, &enteredText,
		                                               &currentDir);

		if (error != Error::NONE) {
			return error;
		}

		shouldReplaceWholeInstrument = oldInstrumentShouldBeReplaced;
		needToAddInstrumentToSong = true;
		loadedFromFile = true;

		if (doClone) {
			newInstrument->name.set(&clonedName);
			newInstrument->editedByUser = true;
		}
	}
	display->displayLoadingAnimationText("Loading", false, true);
	Error error = newInstrument->loadAllAudioFiles(true);

	display->removeLoadingAnimation();

	// If error, most likely because user interrupted sample loading process...
	if (error != Error::NONE) {
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

		Error error = instrumentClipToLoadFor->changeInstrument(
		    modelStack, newInstrument, NULL, InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, NULL, true);
		// TODO: deal with errors!

		if (needToAddInstrumentToSong) {
			currentSong->addOutput(newInstrument);
		}

		changedInstrumentForClip = true;
	}

	// Check if old Instrument has been deleted, in which case need to update the appropriate FileItem.
	if (!isInstrumentInList(instrumentToReplace, currentSong->firstOutput)
	    && !isInstrumentInList(instrumentToReplace, currentSong->firstHibernatingInstrument)) {
		for (int32_t f = fileItems.getNumElements() - 1; f >= 0; f--) {
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
		view.setActiveModControllableTimelineCounter(newInstrument->getActiveClip());
	}

	instrumentToReplace = newInstrument;
	display->removeWorkingAnimation();

	// for the instrument we just loaded, let's check if there's any midi labels we should load
	if (newInstrument->type == OutputType::MIDI_OUT) {
		MIDIInstrument* midiInstrument = (MIDIInstrument*)newInstrument;
		if (midiInstrument->loadDeviceDefinitionFile) {
			FilePointer tempfp;
			bool fileExists = StorageManager::fileExists(midiInstrument->deviceDefinitionFileName.get(), &tempfp);
			if (fileExists) {
				StorageManager::loadMidiDeviceDefinitionFile(midiInstrument, &tempfp,
				                                             &midiInstrument->deviceDefinitionFileName, false);
			}
		}
	}

	return Error::NONE;
}

Error LoadInstrumentPresetUI::performLoadSynthToKit() {
	FileItem* currentFileItem = getCurrentFileItem();
	Kit* kitToLoadFor = static_cast<Kit*>(instrumentToReplace);
	if (!currentFileItem) {
		// Make it say "NONE" on numeric Deluge, for consistency with old times.
		return display->haveOLED() ? Error::FILE_NOT_FOUND : Error::NO_FURTHER_FILES_THIS_DIRECTION;
	}

	if (currentFileItem->isFolder) {
		return Error::NONE;
	}
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);
	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);
	// make sure the drum isn't currently in use
	noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);
	kitToLoadFor->drumsWithRenderingActive.deleteAtKey((int32_t)(Drum*)soundDrumToReplace);
	kitToLoadFor->removeDrum(soundDrumToReplace);

	// swaps out the drum pointed to by soundDrumToReplace
	Error error = StorageManager::loadSynthToDrum(currentSong, instrumentClipToLoadFor, false, &soundDrumToReplace,
	                                              &currentFileItem->filePointer, &enteredText, &currentDir);
	if (error != Error::NONE) {
		return error;
	}
	// kitToLoadFor->addDrum(soundDrumToReplace);
	display->displayLoadingAnimationText("Loading", false, true);
	soundDrumToReplace->loadAllSamples(true);

	// soundDrumToReplace->name.set(getCurrentFilenameWithoutExtension());
	getCurrentFilenameWithoutExtension(&soundDrumToReplace->name);
	soundDrumToReplace->path.set(&currentDir);
	ParamManager* paramManager =
	    currentSong->getBackedUpParamManagerPreferablyWithClip(soundDrumToReplace, instrumentClipToLoadFor);
	if (paramManager) {
		kitToLoadFor->addDrum(soundDrumToReplace);
		// don't back up the param manager since we can't use the backup anyway
		noteRow->setDrum(soundDrumToReplace, kitToLoadFor, modelStackWithNoteRow, instrumentClipToLoadFor, paramManager,
		                 false);

		kitToLoadFor->selectedDrum = soundDrumToReplace;
		kitToLoadFor->beenEdited();
	}
	else {
		error = Error::FILE_CORRUPTED;
	}

	display->removeLoadingAnimation();
	return error;
}
// Previously called "exitAndResetInstrumentToInitial()". Does just that.
void LoadInstrumentPresetUI::exitAction() {
	revertToInitialPreset();
	LoadUI::exitAction();
}

ActionResult LoadInstrumentPresetUI::padAction(int32_t x, int32_t y, int32_t on) {

	// Audition pad
	if (x == kDisplayWidth + 1) {
		if (!showingAuditionPads()) {
			goto potentiallyExit;
		}
		if (currentInstrumentLoadError != Error::NONE) {
			if (on) {
				display->displayError(currentInstrumentLoadError);
			}
		}
		else {
			return instrumentClipView.padAction(x, y, on);
		}
	}

	// Mute pad
	else if (x == kDisplayWidth) {
potentiallyExit:
		if (on && !currentUIMode) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitAction();
		}
	}

	else {
		return LoadUI::padAction(x, y, on);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult LoadInstrumentPresetUI::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed()) {
		LoadUI::verticalEncoderAction(offset, false);
	}
	if (showingAuditionPads()) {
		if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
			return ActionResult::DEALT_WITH;
		}

		ActionResult result = instrumentClipView.verticalEncoderAction(offset, inCardRoutine);

		if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
			return result;
		}

		if (getRootUI() == &keyboardScreen) {
			uiNeedsRendering(this, 0, 0xFFFFFFFF);
		}

		return result;
	}

	return ActionResult::DEALT_WITH;
}

bool LoadInstrumentPresetUI::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (getRootUI() != &keyboardScreen) {
		return false;
	}
	return instrumentClipView.renderSidebar(whichRows, image, occupancyMask);
}

bool LoadInstrumentPresetUI::showingAuditionPads() {
	return getRootUI()->toClipMinder();
}

void LoadInstrumentPresetUI::instrumentEdited(Instrument* instrument) {
	if (instrument == currentInstrument && currentInstrumentLoadError == Error::NONE && enteredText.isEmpty()) {
		enteredText.set(&instrument->name);
		// TODO: update the FileItem too?
		displayText(false);
	}
}

// Caller must set currentDir before calling this.
// Caller must call emptyFileItems() at some point after calling this function.
// song may be supplied as NULL, in which case it won't be searched for Instruments; sometimes this will get called when
// the currentSong is not set up.
ReturnOfConfirmPresetOrNextUnlaunchedOne
LoadInstrumentPresetUI::findAnUnlaunchedPresetIncludingWithinSubfolders(Song* song, OutputType outputType,
                                                                        Availability availabilityRequirement) {

	AudioEngine::logAction("findAnUnlaunchedPresetIncludingWithinSubfolders");
	allowedFileExtensions = allowedFileExtensionsXML;

	ReturnOfConfirmPresetOrNextUnlaunchedOne toReturn;

	int32_t initialDirLength = currentDir.getLength();

	int32_t folderIndex = -1;
	bool doingSubfolders = false;
	String searchNameLocalCopy;

goAgain:

	toReturn.error = readFileItemsFromFolderAndMemory(song, outputType, getThingName(outputType),
	                                                  searchNameLocalCopy.get(), NULL, true);

	if (toReturn.error != Error::NONE) {
emptyFileItemsAndReturn:
		emptyFileItems();
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
			toReturn.error = Error::NO_FURTHER_FILES_THIS_DIRECTION;
			return toReturn;
		}
	}

	// Store rightmost display name before filtering, for later.
	String lastFileItemDisplayNameBeforeFiltering;
	FileItem* rightmostFileItemBeforeFiltering = (FileItem*)fileItems.getElementAddress(fileItems.getNumElements() - 1);
	toReturn.error = lastFileItemDisplayNameBeforeFiltering.set(rightmostFileItemBeforeFiltering->displayName);
	if (toReturn.error != Error::NONE) {
		return toReturn;
	}

	deleteFolderAndDuplicateItems(availabilityRequirement);

	// If we're still looking for preset / XML files, and not subfolders yet...
	if (!doingSubfolders) {

		// Look through our list of FileItems, for a preset.
		for (int32_t i = 0; i < fileItems.getNumElements(); i++) {
			toReturn.fileItem = (FileItem*)fileItems.getElementAddress(i);
			if (!toReturn.fileItem->isFolder) {
				return toReturn; // We found a preset / file.
			}
		}

		// Ok, we found none. Should we do some more reading of the folder contents, to get more files, or are there no
		// more?
		if (numFileItemsDeletedAtEnd) {
			searchNameLocalCopy.set(&lastFileItemDisplayNameBeforeFiltering); // Can't fail.
			goto goAgain;
		}

		// Ok, we've looked at every file, and none were presets we could use. So now we want to look in subfolders. Do
		// we still have the "start" of our folder's contents in memory?
		if (numFileItemsDeletedAtStart) {
			goto startDoingFolders;
		}

		doingSubfolders = true;
	}

	// Ok, do folders now.
	int32_t i;
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
		if (toReturn.error != Error::NONE) {
			goto emptyFileItemsAndReturn;
		}
		toReturn.error = currentDir.concatenate(&toReturn.fileItem->filename);
		if (toReturn.error != Error::NONE) {
			goto emptyFileItemsAndReturn;
		}

		// Call self
		toReturn = findAnUnlaunchedPresetIncludingWithinSubfolders(song, outputType, availabilityRequirement);
		if (toReturn.error == Error::NO_FURTHER_FILES_THIS_DIRECTION) {
			if (anyMoreForLater) {
				currentDir.shorten(initialDirLength);
				goto goAgain;
			}
			else {
				return toReturn;
			}
		}
		else if (toReturn.error != Error::NONE) {
			goto emptyFileItemsAndReturn;
		}

		// If still here, the recursive call found something, so return.
		return toReturn;
	}
}

// Caller must call emptyFileItems() at some point after calling this function.
// And, set currentDir, before this is called.
ReturnOfConfirmPresetOrNextUnlaunchedOne
LoadInstrumentPresetUI::confirmPresetOrNextUnlaunchedOne(OutputType outputType, String* searchName,
                                                         Availability availabilityRequirement) {
	ReturnOfConfirmPresetOrNextUnlaunchedOne toReturn;

	String searchNameLocalCopy;
	searchNameLocalCopy.set(searchName); // Can't fail.
	bool shouldJustGrabLeftmost = false;

doReadFiles:
	toReturn.error = readFileItemsFromFolderAndMemory(currentSong, outputType, getThingName(outputType),
	                                                  searchNameLocalCopy.get(), NULL, false, availabilityRequirement);

	AudioEngine::logAction("confirmPresetOrNextUnlaunchedOne");

	if (toReturn.error == Error::FOLDER_DOESNT_EXIST) {
justGetAnyPreset: // This does *not* favour the currentDir, so you should exhaust all avenues before calling this.
		toReturn.error = currentDir.set(getInstrumentFolder(outputType));
		if (toReturn.error != Error::NONE) {
			return toReturn;
		}
		toReturn = findAnUnlaunchedPresetIncludingWithinSubfolders(currentSong, outputType, availabilityRequirement);
		return toReturn;
	}
	else if (toReturn.error != Error::NONE) {
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
	if (toReturn.error != Error::NONE) {
		return toReturn;
	}

	deleteFolderAndDuplicateItems(availabilityRequirement);

	// If we've shot off the end of the list, that means our searched-for preset didn't exist or wasn't available, and
	// any subsequent ones which at first made it onto the (possibly truncated) list also weren't available.
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
	return toReturn;
}

/// Caller must call emptyFileItems() at some point after calling this function - unless an error is returned
/// Caller must remove OLED working animation after calling this too.
PresetNavigationResult LoadInstrumentPresetUI::doPresetNavigation(int32_t offset, Instrument* oldInstrument,
                                                                  Availability availabilityRequirement, bool doBlink) {

	AudioEngine::logAction("doPresetNavigation");

	currentDir.set(&oldInstrument->dirPath);
	OutputType outputType = oldInstrument->type;

	PresetNavigationResult toReturn;

	String oldNameString; // We only might use this later for temporary storage
	String newName;

	oldNameString.set(&oldInstrument->name);
	toReturn.error = oldNameString.concatenate(".XML");
	if (toReturn.error != Error::NONE) {
		return toReturn;
	}

readAgain:
	int32_t newCatalogSearchDirection = (offset >= 0) ? CATALOG_SEARCH_RIGHT : CATALOG_SEARCH_LEFT;
readAgainWithSameOffset:
	toReturn.error =
	    readFileItemsForFolder(getThingName(outputType), false, allowedFileExtensionsXML, oldNameString.get(),
	                           FILE_ITEMS_MAX_NUM_ELEMENTS_FOR_NAVIGATION, newCatalogSearchDirection);

	if (toReturn.error != Error::NONE) {
		return toReturn;
	}

	AudioEngine::logAction("doPresetNavigation2");

	toReturn.error = currentSong->addInstrumentsToFileItems(outputType);
	if (toReturn.error != Error::NONE) {
emptyFileItemsAndReturn:
		emptyFileItems();
		return toReturn;
	}
	AudioEngine::logAction("doPresetNavigation3");

	sortFileItems();
	AudioEngine::logAction("doPresetNavigation4");

	deleteFolderAndDuplicateItems(Availability::INSTRUMENT_AVAILABLE_IN_SESSION);
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
			toReturn.error = Error::NO_ERROR_BUT_GET_OUT;
			goto emptyFileItemsAndReturn;
		}
	}
	else if (fileItems.getNumElements() == 1
	         && ((FileItem*)fileItems.getElementAddress(0))->instrument == oldInstrument) {
		goto reachedEnd;
	}

	int32_t i = (offset >= 0) ? 0 : (fileItems.getNumElements() - 1);
	/*
	if (i >= fileItems.getNumElements()) { // If not found *and* we'd be past the end of the list...
	    if (offset >= 0) i = 0;
	    else i = fileItems.getNumElements() - 1;
	    goto doneMoving;
	}
	else {
	    int32_t oldNameLength = strlen(oldNameChars);
	    FileItem* searchResultItem = (FileItem*)fileItems.getElementAddress(i);
	    if (memcasecmp(oldNameChars, searchResultItem->displayName, oldNameLength)) {
notFound:	if (offset < 0) {
	            i--;
	            if (i < 0) i += fileItems.getNumElements();
	        }
	        goto doneMoving;
	    }
	    if (searchResultItem->filenameIncludesExtension) {
	        if (strrchr(searchResultItem->displayName, '.') != &searchResultItem->displayName[oldNameLength]) goto
notFound;
	    }
	    else {
	        if (searchResultItem->displayName[oldNameLength] != 0) goto notFound;
	    }
	}
*/
	int32_t wrapped = 0;
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
			wrapped += 1;
			if (numFileItemsDeletedAtEnd) {
searchFromOneEnd:
				oldNameString.clear();
				D_PRINTLN("reloading and wrap");
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
			wrapped += 1;
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

	bool isAlreadyInSong = toReturn.fileItem->instrument && toReturn.fileItem->instrumentAlreadyInSong;
	// wrapped is here to prevent an infinite loop
	if (availabilityRequirement == Availability::INSTRUMENT_UNUSED && isAlreadyInSong && wrapped < 2) {
		goto moveAgain;
	}

	toReturn.loadedFromFile = false;
	bool isHibernating = toReturn.fileItem->instrument && !toReturn.fileItem->instrumentAlreadyInSong;

	if (toReturn.fileItem->instrument) {
		view.displayOutputName(toReturn.fileItem->instrument, doBlink);
	}
	else {
		toReturn.error = toReturn.fileItem->getDisplayNameWithoutExtension(&newName);
		if (toReturn.error != Error::NONE) {
			goto emptyFileItemsAndReturn;
		}
		toReturn.error = oldNameString.set(toReturn.fileItem->displayName);
		if (toReturn.error != Error::NONE) {
			goto emptyFileItemsAndReturn;
		}
		view.drawOutputNameFromDetails(outputType, 0, 0, newName.get(), newName.isEmpty(), false, doBlink);
	}

	if (display->haveOLED()) {
		deluge::hid::display::OLED::sendMainImage(); // Sorta cheating - bypassing the UI layered renderer.
	}

	if (encoders::getEncoder(EncoderName::SELECT).detentPos) {
		D_PRINTLN("go again 1 --------------------------");

doPendingPresetNavigation:
		offset = encoders::getEncoder(EncoderName::SELECT).getLimitedDetentPosAndReset();

		if (toReturn.loadedFromFile) {
			currentSong->deleteOutput(toReturn.fileItem->instrument);
			toReturn.fileItem->instrument = NULL;
		}
		goto moveAgain;
	}

	// Unlike in ClipMinder, there's no need to check whether we came back to the same Instrument, cos we've specified
	// that we were looking for "unused" ones only
	// TODO: This isn't true, it's an argument so that must have changed at some point. This logic will create a clone
	// if anything other than unused is passed in
	if (!toReturn.fileItem->instrument) {
		toReturn.error =
		    StorageManager::loadInstrumentFromFile(currentSong, NULL, outputType, false, &toReturn.fileItem->instrument,
		                                           &toReturn.fileItem->filePointer, &newName, &Browser::currentDir);
		if (toReturn.error != Error::NONE) {
			goto emptyFileItemsAndReturn;
		}

		toReturn.loadedFromFile = true;

		if (encoders::getEncoder(EncoderName::SELECT).detentPos) {
			D_PRINTLN("go again 2 --------------------------");
			goto doPendingPresetNavigation;
		}
	}

	// view.displayOutputName(toReturn.fileItem->instrument);

	display->displayLoadingAnimationText("Loading", false, true);
	int32_t oldUIMode = currentUIMode;
	currentUIMode = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
	toReturn.fileItem->instrument->loadAllAudioFiles(true);
	currentUIMode = oldUIMode;

	// If user wants to move on...
	if (encoders::getEncoder(EncoderName::SELECT).detentPos) {
		D_PRINTLN("go again 3 --------------------------");
		goto doPendingPresetNavigation;
	}

	if (isHibernating) {
		currentSong->removeInstrumentFromHibernationList(toReturn.fileItem->instrument);
	}

	return toReturn;
}
