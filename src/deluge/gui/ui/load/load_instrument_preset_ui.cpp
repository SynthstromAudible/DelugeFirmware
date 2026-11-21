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
#include "deluge.h"
#include "extern.h"
#include "gui/context_menu/load_instrument_preset.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/root_ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
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
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/try.h"

using namespace deluge;
namespace encoders = deluge::hid::encoders;

namespace {
constexpr int32_t kPresetPreviewDelayMs = 150;
}

PLACE_SDRAM_BSS LoadInstrumentPresetUI loadInstrumentPresetUI{};

bool LoadInstrumentPresetUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	// grey out the mute pads, not the audition pads or main pads
	if (showingAuditionPads()) {
		*cols = 0b10;
	}
	// grey out everything
	else {
		*cols = 0xFFFFFFFF;
	}
	return true;
}

bool LoadInstrumentPresetUI::opened() {
	cancelPresetNavigation();
	cancelPendingPreview();
	clearPresetNavigationCursor();

	// The QWERTY keyboard is always shown in this UI (qwertyAlwaysVisible stays true), so the favourites row should
	// be too. The static qwertyVisible flag can be left false by another browser (e.g. song load with the keyboard
	// toggled off); without resetting it here the favourites row would stay hidden until something else set it. #4674
	qwertyVisible = true;

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
			initialName.set(soundDrumToReplace->drumName);
		}
		else {
			initialName.set("");
		}

		initialDirPath.set("SYNTHS");
	}

	if (instrumentToReplace != nullptr) {
		switch (instrumentToReplace->type) {
		case OutputType::MIDI_OUT:
			initialChannelSuffix =
			    static_cast<int8_t>(static_cast<MIDIInstrument*>(instrumentToReplace)->channelSuffix);
			// intentional fallthrough to share code with CV case

		case OutputType::CV:
			initialChannel = static_cast<int16_t>(static_cast<NonAudioInstrument*>(instrumentToReplace)->getChannel());
			break;

		// explicit fallthrough cases
		case OutputType::AUDIO:
		case OutputType::SYNTH:
		case OutputType::KIT:
		case OutputType::NONE:;
		}
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
				break;
			// explicit fallthrough cases
			case OutputType::AUDIO:
			case OutputType::CV:
			case OutputType::NONE:;
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

			if (!soundDrumToReplace->drumName.empty()) {
				enteredText.set(soundDrumToReplace->drumName);
				searchFilename.set(soundDrumToReplace->drumName);
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

void LoadInstrumentPresetUI::currentFileChanged(int32_t /*movementDirection*/) {
	cancelPendingPreview();
	currentInstrumentLoadError = Error::NONE;
	FileItem* item = getCurrentFileItem();
	if ((item != nullptr) && !item->isFolder) {
		preview_pending_ = true;
		startPresetLoadingIndicator();
		uiTimerManager.setTimer(TimerName::INSTRUMENT_PREVIEW, kPresetPreviewDelayMs);
	}
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

		if (preview_pending_ || currentInstrumentLoadError != Error::NONE) {
			loadPendingPreview();
			if (currentInstrumentLoadError != Error::NONE) {
				display->displayError(currentInstrumentLoadError);
				return;
			}
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
			// An unsaved (in-memory only) synth preset cannot be loaded / cloned because
			// the XML is read from disk to create the instrument. If the preset hasn't
			// been saved yet, there is no file on the SD card to read from.
			display->displayError(Error::FILE_NOT_SAVED);
			return ActionResult::DEALT_WITH;
		}

		bool available = gui::context_menu::loadInstrumentPreset.setupAndCheckAvailability();

		if (available) {
			display->setNextTransitionDirection(1);
			stopPresetLoadingIndicator();
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

	// Switching to/from a kit requires the target clip and output to be empty.
	// Arranger browsing replaces an output without a target clip; the current clip may belong to another track.
	if (((outputTypeToLoad == OutputType::KIT) || (newOutputType == OutputType::KIT))
	    && ((instrumentClipToLoadFor && !instrumentClipToLoadFor->isEmpty()) || !instrumentToReplace->isEmpty())) {
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
		loadPendingPreview();
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

	Instrument* initialInstrument = nullptr;

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

		Error error = instrumentClipToLoadFor->changeInstrument(modelStack, initialInstrument, nullptr,
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
			int32_t underscorePos = (uintptr_t)underscoreAddress - (uintptr_t)oldNameChars;
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
	LoadingIndicatorGuard indicator_guard{*this};

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

	startPresetLoadingIndicator();

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
	startPresetLoadingIndicator();
	Error error = newInstrument->loadAllAudioFiles(true);

	stopPresetLoadingIndicator();

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
		    modelStack, newInstrument, nullptr, InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, nullptr, true);
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
				fileItem->instrument = nullptr;
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
	stopPresetLoadingIndicator();

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
	LoadingIndicatorGuard indicator_guard{*this};
	FileItem* currentFileItem = getCurrentFileItem();
	Kit* kitToLoadFor = static_cast<Kit*>(instrumentToReplace);
	if (!currentFileItem) {
		// Make it say "NONE" on numeric Deluge, for consistency with old times.
		return display->haveOLED() ? Error::FILE_NOT_FOUND : Error::NO_FURTHER_FILES_THIS_DIRECTION;
	}

	if (currentFileItem->isFolder) {
		return Error::NONE;
	}

	// An unsaved (in-memory only) synth preset cannot be loaded into a kit row because
	// loadSynthToDrum() reads XML from disk to create a SoundDrum. If the preset hasn't
	// been saved yet, there is no file on the SD card to read from.
	if (!currentFileItem->maybeExistsOnCard) {
		return Error::FILE_NOT_SAVED;
	}

	startPresetLoadingIndicator();
	SoundDrum* oldDrum = soundDrumToReplace;
	SoundDrum* newDrum = nullptr;
	Error error = StorageManager::loadSynthToDrum(currentSong, instrumentClipToLoadFor, false, &newDrum,
	                                              &currentFileItem->filePointer, &enteredText, &currentDir);
	if (error != Error::NONE) {
		return error;
	}

	// The old drum stays in the kit, and alive, until every NoteRow has been moved off it - loading samples can run
	// the audio routine, and NoteRows in any Clip of this Kit may still be played or rendered in the meantime.
	startPresetLoadingIndicator();
	newDrum->loadAllSamples(true);

	newDrum->drumName = enteredText.get();
	newDrum->path.set(&currentDir);
	// Steal the ParamManager that loading backed up, so its (now empty) backup entry is removed from the Song. Take it
	// out of the backups first - setDrum() backs up the old ParamManager, which can move the backups around
	ParamManagerForTimeline paramManager;
	if (!currentSong->getBackedUpParamManagerPreferablyWithClip(newDrum, instrumentClipToLoadFor, &paramManager)) {
		currentSong->deleteBackedUpParamManagersForModControllable(newDrum);
		void* toDealloc = dynamic_cast<void*>(newDrum);
		newDrum->~SoundDrum();
		delugeDealloc(toDealloc);
		stopPresetLoadingIndicator();
		return Error::FILE_CORRUPTED;
	}

	kitToLoadFor->addDrum(newDrum);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);
	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);
	noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);
	// Back up the old ParamManager so it's freed along with the old drum below, rather than leaked
	noteRow->setDrum(newDrum, kitToLoadFor, modelStackWithNoteRow, instrumentClipToLoadFor, &paramManager, true);

	// The same Drum is shared by NoteRows in every Clip of this Kit - move all of them onto the new drum, otherwise
	// they'd be left pointing at the old one once it's deleted. They clone their ParamManager from the row above.
	for (InstrumentClip* clip : InstrumentClips::everywhere(currentSong)) {
		if (oldDrum == nullptr || clip->output != kitToLoadFor) {
			continue;
		}
		for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = clip->noteRows.getElement(i);
			if (thisNoteRow->drum != oldDrum) {
				continue;
			}
			char otherModelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithNoteRow* otherModelStackWithNoteRow =
			    setupModelStackWithTimelineCounter(otherModelStackMemory, currentSong, clip)
			        ->addNoteRow(clip->getNoteRowId(thisNoteRow, i), thisNoteRow);
			thisNoteRow->stopCurrentlyPlayingNote(otherModelStackWithNoteRow);
			thisNoteRow->setDrum(newDrum, kitToLoadFor, otherModelStackWithNoteRow, instrumentClipToLoadFor, nullptr,
			                     true);
		}
	}

	// Nothing references the old drum now, so it can go
	if (oldDrum != nullptr) {
		oldDrum->wontBeRenderedForAWhile();
		kitToLoadFor->drumsWithRenderingActive.deleteAtKey((int32_t)(Drum*)oldDrum);
		kitToLoadFor->removeDrum(oldDrum);
		currentSong->deleteBackedUpParamManagersForModControllable(oldDrum);
		void* toDealloc = dynamic_cast<void*>(oldDrum);
		oldDrum->~SoundDrum();
		delugeDealloc(toDealloc);
	}

	soundDrumToReplace = newDrum;
	kitToLoadFor->selectedDrum = newDrum;
	kitToLoadFor->beenEdited();
	// The view may still have the old drum cached as its active ModControllable, and gold knobs turned while this
	// browser is open go straight to it
	view.setActiveModControllableTimelineCounter(instrumentClipToLoadFor);

	stopPresetLoadingIndicator();
	return Error::NONE;
}
// Previously called "exitAndResetInstrumentToInitial()". Does just that.
void LoadInstrumentPresetUI::exitAction() {
	cancelPendingPreview();
	revertToInitialPreset();
	LoadUI::exitAction();
}

ActionResult LoadInstrumentPresetUI::padAction(int32_t x, int32_t y, int32_t on) {

	// Audition pad
	if (x == kDisplayWidth + 1) {
		// The release must not overtake the press whose synchronous load is yielding.
		if (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (!showingAuditionPads()) {
			goto potentiallyExit;
		}
		if ((on != 0) && preview_pending_) {
			if (currentUIMode != UI_MODE_NONE) {
				return ActionResult::DEALT_WITH;
			}
			if (sdRoutineLock || StorageManager::isDeserializerOpen() || encoders::selectNavigationPending()) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			loadPendingPreview();
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
std::expected<FileItem*, Error>
LoadInstrumentPresetUI::findAnUnlaunchedPresetIncludingWithinSubfolders(Song* song, OutputType outputType,
                                                                        Availability availabilityRequirement) {

	AudioEngine::logAction("findAnUnlaunchedPresetIncludingWithinSubfolders");
	allowedFileExtensions = allowedFileExtensionsXML;

	int32_t initialDirLength = currentDir.getLength();

	int32_t folderIndex = -1;
	bool doingSubfolders = false;
	String searchNameLocalCopy;

goAgain:

	Error error = readFileItemsFromFolderAndMemory(song, outputType, getThingName(outputType),
	                                               searchNameLocalCopy.get(), nullptr, true);
	if (error != Error::NONE) {
		emptyFileItems();
		return std::unexpected{error};
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
			return std::unexpected{Error::NO_FURTHER_FILES_THIS_DIRECTION};
		}
	}

	// Store rightmost display name before filtering, for later.
	String lastFileItemDisplayNameBeforeFiltering;
	auto* rightmostFileItemBeforeFiltering = (FileItem*)fileItems.getElementAddress(fileItems.getNumElements() - 1);
	error = lastFileItemDisplayNameBeforeFiltering.set(rightmostFileItemBeforeFiltering->displayName);
	if (error != Error::NONE) {
		return std::unexpected{error};
	}

	deleteFolderAndDuplicateItems(availabilityRequirement);

	// If we're still looking for preset / XML files, and not subfolders yet...
	if (!doingSubfolders) {

		// Look through our list of FileItems, for a preset.
		for (int32_t i = 0; i < fileItems.getNumElements(); i++) {
			auto* fileItem = (FileItem*)fileItems.getElementAddress(i);
			if (!fileItem->isFolder) {
				return fileItem; // We found a preset / file.
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
	FileItem* fileItem;
	for (i = 0; i < fileItems.getNumElements(); i++) {
		fileItem = (FileItem*)fileItems.getElementAddress(i);
		if (fileItem->isFolder) {
			goto doThisFolder;
		}
	}
	if (numFileItemsDeletedAtEnd) {
		searchNameLocalCopy.set(&lastFileItemDisplayNameBeforeFiltering);
		goto goAgain;
	}
	else {
		return std::unexpected{Error::NO_FURTHER_FILES_THIS_DIRECTION};
	}

	if (false) {
doThisFolder:
		bool anyMoreForLater = numFileItemsDeletedAtEnd || (i < (fileItems.getNumElements() - 1));
		searchNameLocalCopy.set(fileItem->displayName);

		Error error = currentDir.concatenate("/");
		if (error != Error::NONE) {
			emptyFileItems();
			return std::unexpected{error};
		}
		error = currentDir.concatenate(&fileItem->filename);
		if (error != Error::NONE) {
			emptyFileItems();
			return std::unexpected{error};
		}

		// Call self
		return D_TRY_CATCH(findAnUnlaunchedPresetIncludingWithinSubfolders(song, outputType, availabilityRequirement),
		                   error, {
			                   if (error == Error::NO_FURTHER_FILES_THIS_DIRECTION) {
				                   if (anyMoreForLater) {
					                   currentDir.shorten(initialDirLength);
					                   goto goAgain;
				                   }
				                   return result;
			                   }
			                   emptyFileItems();
			                   return result;
		                   });
	}
}

// Caller must call emptyFileItems() at some point after calling this function.
// And, set currentDir, before this is called.
std::expected<FileItem*, Error>
LoadInstrumentPresetUI::confirmPresetOrNextUnlaunchedOne(OutputType outputType, String* searchName,
                                                         Availability availabilityRequirement) {
	String searchNameLocalCopy;
	searchNameLocalCopy.set(searchName); // Can't fail.
	bool shouldJustGrabLeftmost = false;

	// This does *not* favour the currentDir, so you should exhaust all avenues before calling this.
	auto justGetAnyPreset = [&]() -> std::expected<FileItem*, Error> {
		Error error = currentDir.set(getInstrumentFolder(outputType));
		if (error != Error::NONE) {
			return std::unexpected{error};
		}
		return findAnUnlaunchedPresetIncludingWithinSubfolders(currentSong, outputType, availabilityRequirement);
	};

doReadFiles:
	Error error = readFileItemsFromFolderAndMemory(currentSong, outputType, getThingName(outputType),
	                                               searchNameLocalCopy.get(), nullptr, false, availabilityRequirement);

	AudioEngine::logAction("confirmPresetOrNextUnlaunchedOne");

	if (error == Error::FOLDER_DOESNT_EXIST) {
		justGetAnyPreset();
	}
	else if (error != Error::NONE) {
		return std::unexpected{error};
	}

	sortFileItems();
	if (!fileItems.getNumElements()) {
		if (shouldJustGrabLeftmost) {
			return justGetAnyPreset();
		}

		if (numFileItemsDeletedAtStart) {
needToGrabLeftmostButHaveToReadFirst:
			searchNameLocalCopy.clear();
			shouldJustGrabLeftmost = true;
			goto doReadFiles;
		}
		else {
			return justGetAnyPreset();
		}
	}

	// Store rightmost display name before filtering, for later.
	String lastFileItemDisplayNameBeforeFiltering;
	auto* rightmostFileItemBeforeFiltering = (FileItem*)fileItems.getElementAddress(fileItems.getNumElements() - 1);
	error = lastFileItemDisplayNameBeforeFiltering.set(rightmostFileItemBeforeFiltering->displayName);
	if (error != Error::NONE) {
		return std::unexpected{error};
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
				return justGetAnyPreset();
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
					return justGetAnyPreset();
				}
				// Otherwise, everything's fine and we can just take the first element.
			}
		}
	}
	return (FileItem*)fileItems.getElementAddress(0);
}

namespace {
FileItem* advancePresetNavigationPage(deluge::gui::PresetNavigationSteps& steps, Availability requirement,
                                      Instrument* origin, bool& advancedSinceWrap) {
	const int32_t direction = steps.direction();
	const int32_t count = Browser::fileItems.getNumElements();
	for (int32_t index = direction > 0 ? 0 : count - 1; index >= 0 && index < count; index += direction) {
		auto* candidate = static_cast<FileItem*>(Browser::fileItems.getElementAddress(index));
		const bool available = deluge::gui::presetInstrumentAvailable(requirement, candidate->instrumentAlreadyInSong,
		                                                              false, candidate->instrument, origin);
		advancedSinceWrap |= available;
		if (steps.visit(available)) {
			return candidate;
		}
	}
	return nullptr;
}

Error readPresetNavigationPage(Browser& browser, OutputType outputType, char const* searchName, int32_t direction) {
	Error error = browser.readFileItemsForFolder(getThingName(outputType), false, allowedFileExtensionsXML, searchName,
	                                             FILE_ITEMS_MAX_NUM_ELEMENTS_FOR_NAVIGATION, direction);
	if (error == Error::NONE) {
		error = currentSong->addInstrumentsToFileItems(outputType);
	}
	if (error == Error::NONE) {
		browser.sortFileItems();
	}
	else {
		Browser::emptyFileItems();
	}
	return error;
}

std::expected<FileItem*, Error> findPresetNavigationItem(Browser& browser, int32_t offset, Instrument* origin,
                                                         Availability requirement, String& searchName) {
	deluge::gui::PresetNavigationSteps steps(offset);
	const int32_t direction = steps.direction();
	bool wrapped = false;
	bool advanced_since_wrap = false;
	bool include_boundary = false;
	// Count available candidates across pages and wraps, retaining all detents.
	while (true) {
		int32_t search_direction = direction > 0 ? CATALOG_SEARCH_RIGHT : CATALOG_SEARCH_LEFT;
		if (include_boundary) {
			search_direction = direction > 0 ? CATALOG_SEARCH_RIGHT_INCLUSIVE : CATALOG_SEARCH_LEFT_INCLUSIVE;
		}
		Error error = readPresetNavigationPage(browser, origin->type, searchName.get(), search_direction);
		if (error != Error::NONE) {
			return std::unexpected(error);
		}
		const int32_t page_count = Browser::fileItems.getNumElements();
		if (page_count == 0) {
			if (searchName.isEmpty() || (wrapped && !advanced_since_wrap)) {
				Browser::emptyFileItems();
				return std::unexpected(Error::NO_ERROR_BUT_GET_OUT);
			}
			wrapped = true;
			advanced_since_wrap = false;
			searchName.clear();
			include_boundary = false;
			continue;
		}
		// Cleanup discards a culled edge's incomplete group. Revisit that key
		// inclusively; advance exclusively only past fully processed entries.
		include_boundary =
		    direction > 0 ? !Browser::lastFileItemRemaining.isEmpty() : !Browser::firstFileItemRemaining.isEmpty();
		auto* last = static_cast<FileItem*>(Browser::fileItems.getElementAddress(direction > 0 ? page_count - 1 : 0));
		error = searchName.set(last->displayName);
		if (error != Error::NONE) {
			Browser::emptyFileItems();
			return std::unexpected(error);
		}
		Browser::deleteFolderAndDuplicateItems(Availability::INSTRUMENT_AVAILABLE_IN_SESSION, origin);
		if (auto* candidate = advancePresetNavigationPage(steps, requirement, origin, advanced_since_wrap)) {
			return candidate;
		}
	}
}
} // namespace

/// Caller must call emptyFileItems() at some point after calling this function - unless an error is returned
/// Caller must remove OLED working animation after calling this too.
PresetNavigationResult LoadInstrumentPresetUI::doPresetNavigation(int32_t offset, Instrument* oldInstrument,
                                                                  Availability availabilityRequirement, bool doBlink,
                                                                  Clip* displayClip, bool fromArranger,
                                                                  bool replacingWholeOutput) {

	// Resumption enters through the original caller, which has rebuilt its model
	// stack and recalculated instrument availability. No caller stack survives the wait.
	if (preset_navigation_.resuming) {
		auto result = loadNavigatedPreset(&preset_navigation_.item, oldInstrument, displayClip);
		preset_navigation_.loadSucceeded = result.error == Error::NONE;
		preset_navigation_.loadError = result.error;
		return result;
	}
	preset_navigation_.loadError = Error::NONE;
	preset_navigation_.editGesture.reset();
	if (preset_navigation_.task >= 0 && !presetNavigationContextMatches()) {
		clearPresetNavigationCursor();
	}
	cancelPresetNavigation();
	LoadingIndicatorGuard indicator_guard{*this};
	AudioEngine::logAction("doPresetNavigation");
	// This is encoder navigation, not a QWERTY prediction. A keyboard abort
	// left over from a previous browser visit must not abort every new load.
	predictionInterrupted = false;

	currentDir.set(&oldInstrument->dirPath);
	OutputType output_type = oldInstrument->type;

	PresetNavigationResult to_return;

	String search_name;
	String new_name;

	// Move from the last displayed candidate while keeping the loaded sound
	// intact. Own the filename: the catalog is freed before returning.
	const bool resume_from_candidate =
	    preset_navigation_cursor_.instrument == oldInstrument && preset_navigation_cursor_.song == currentSong
	    && preset_navigation_cursor_.clip == getCurrentClip() && preset_navigation_cursor_.ui == getCurrentUI()
	    && preset_navigation_cursor_.targetClip == displayClip
	    && (strcmp(preset_navigation_cursor_.sourceName.get(), oldInstrument->name.get()) == 0)
	    && (strcmp(preset_navigation_cursor_.dir.get(), oldInstrument->dirPath.get()) == 0);
	if (resume_from_candidate) {
		search_name.set(&preset_navigation_cursor_.filename);
	}
	else {
		search_name.set(&oldInstrument->name);
		to_return.error = search_name.concatenate(".XML");
	}
	clearPresetNavigationCursor();
	if (to_return.error != Error::NONE) {
		return to_return;
	}

	auto candidate = findPresetNavigationItem(*this, offset, oldInstrument, availabilityRequirement, search_name);
	if (!candidate) {
		to_return.error = candidate.error();
		return to_return;
	}
	to_return.fileItem = *candidate;
	stopPresetLoadingIndicator();

	to_return.loadedFromFile = false;

	if (to_return.fileItem->instrument != nullptr) {
		view.displayOutputName(to_return.fileItem->instrument, doBlink, displayClip);
		// Reversing away from a failed preset can select the still-loaded sound.
		// Nothing needs replacing (or detaching from the clip) in that case.
		if (to_return.fileItem->instrument == oldInstrument) {
			clearPresetNavigationCursor();
			to_return.error = Error::NO_ERROR_BUT_GET_OUT;
			emptyFileItems();
			return to_return;
		}
	}
	else {
		to_return.error = to_return.fileItem->getFilenameWithoutExtension(&new_name);
		if (to_return.error != Error::NONE) {
			emptyFileItems();
			return to_return;
		}
		to_return.error = search_name.set(to_return.fileItem->displayName);
		if (to_return.error != Error::NONE) {
			emptyFileItems();
			return to_return;
		}
		view.drawOutputNameFromDetails(output_type, 0, 0, new_name.get(), new_name.isEmpty(), false, doBlink,
		                               displayClip);
	}

	if (!to_return.fileItem->instrumentAlreadyInSong) {
		startPresetLoadingIndicator();
	}
	if (display->haveOLED()) {
		deluge::hid::display::OLED::sendMainImage(); // Sorta cheating - bypassing the UI layered renderer.
	}

	// Leave queued turns for the encoder task. The owned cursor below lets the
	// next turn continue from this selection, and resumption yields to that input.
	if (to_return.fileItem->instrumentAlreadyInSong) {
		return loadNavigatedPreset(to_return.fileItem, oldInstrument, displayClip);
	}

	// Own the candidate across the delay: Browser's catalog is shared with other
	// UIs and must not stay live while normal input handling continues.
	auto& pending = preset_navigation_;
	pending.item.filename.set(&to_return.fileItem->filename);
	to_return.error = pending.displayName.set(to_return.fileItem->displayName);
	if (to_return.error != Error::NONE) {
		stopPresetLoadingIndicator();
		emptyFileItems();
		return to_return;
	}
	pending.item.displayName = pending.displayName.get();
	pending.item.filePointer = to_return.fileItem->filePointer;
	pending.item.filenameIncludesExtension = to_return.fileItem->filenameIncludesExtension;
	pending.item.isFolder = false;
	pending.item.instrument = to_return.fileItem->instrument;
	pending.item.instrumentAlreadyInSong = false;
	if (pending.item.instrument != nullptr) {
		pending.candidateName.set(&pending.item.instrument->name);
		pending.candidateDir.set(&pending.item.instrument->dirPath);
	}
	pending.song = currentSong;
	pending.ui = getCurrentUI();
	pending.uiMode = currentUIMode;
	pending.currentClip = getCurrentClip();
	pending.selectedClip =
	    currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW ? sessionView.getClipForLayout() : nullptr;
	pending.displayClip = displayClip;
	pending.instrument = oldInstrument;
	pending.doBlink = doBlink;
	// Performance View can delegate navigation to Arranger View while remaining the current UI.
	pending.fromArranger = fromArranger;
	pending.replacingWholeOutput = replacingWholeOutput;
	pending.padReleased = false;
	pending.deadline = getSystemTime() + (kPresetPreviewDelayMs / 1000.0);
	// The next encoder turn starts from this candidate, even though the original
	// instrument remains loaded. The same cursor also lets us pass corrupt presets.
	to_return.error = rememberPresetNavigationCandidate(oldInstrument, to_return.fileItem, displayClip);
	emptyFileItems();
	to_return.fileItem = nullptr;
	if (to_return.error != Error::NONE) {
		return to_return;
	}
	to_return.error = schedulePresetNavigation();
	if (to_return.error != Error::NONE) {
		return to_return;
	}
	indicator_guard.keepActive = true;
	to_return.pending = true;
	return to_return;
}

PresetNavigationResult LoadInstrumentPresetUI::loadNavigatedPreset(FileItem* item, Instrument* oldInstrument,
                                                                   Clip* displayClip) {
	LoadingIndicatorGuard indicator_guard{*this};
	PresetNavigationResult to_return;
	to_return.fileItem = item;
	bool is_hibernating = (to_return.fileItem->instrument != nullptr) && !to_return.fileItem->instrumentAlreadyInSong;
	String new_name;

	if (to_return.fileItem->instrument == nullptr) {
		to_return.error = to_return.fileItem->getFilenameWithoutExtension(&new_name);
		if (to_return.error == Error::NONE) {
			uint32_t old_ui_mode = currentUIMode;
			currentUIMode = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
			to_return.error = StorageManager::loadInstrumentFromFile(
			    currentSong, nullptr, oldInstrument->type, false, &to_return.fileItem->instrument,
			    &to_return.fileItem->filePointer, &new_name, &oldInstrument->dirPath);
			currentUIMode = old_ui_mode;
		}
		if (to_return.error != Error::NONE) {
			rememberPresetNavigationCandidate(oldInstrument, item, displayClip);
			if (to_return.error == Error::ABORTED_BY_USER && encoders::selectNavigationPending()) {
				to_return.error = Error::NO_ERROR_BUT_GET_OUT;
			}
			emptyFileItems();
			return to_return;
		}
		to_return.loadedFromFile = true;
	}

	startPresetLoadingIndicator();
	uint32_t old_ui_mode = currentUIMode;
	currentUIMode = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
	to_return.fileItem->instrument->loadAllAudioFiles(true);
	currentUIMode = old_ui_mode;

	if (encoders::selectNavigationPending()) {
		rememberPresetNavigationCandidate(oldInstrument, item, displayClip);
		if (to_return.loadedFromFile) {
			currentSong->deleteOutput(to_return.fileItem->instrument);
			to_return.fileItem->instrument = nullptr;
		}
		to_return.error = Error::NO_ERROR_BUT_GET_OUT;
		emptyFileItems();
		return to_return;
	}

	if (is_hibernating) {
		currentSong->removeInstrumentFromHibernationList(to_return.fileItem->instrument);
	}
	clearPresetNavigationCursor();
	return to_return;
}

void LoadInstrumentPresetUI::displayText(bool blinkImmediately) {
	const bool restore_loading_indicator = !display->haveOLED() && loading_indicator_active_;
	if (restore_loading_indicator) {
		stopPresetLoadingIndicator();
	}
	Browser::displayText(blinkImmediately);
	if (restore_loading_indicator) {
		// Numeric text redraws replace the display layers, including the loading
		// animation. Keep its state in sync and reinstall it above the new text.
		startPresetLoadingIndicator();
	}
}

void LoadInstrumentPresetUI::startPresetLoadingIndicator() {
	if (!loading_indicator_active_) {
		display->displayLoadingAnimationText("Loading", false, true);
		loading_indicator_active_ = true;
		if (display->haveOLED()) {
			// Show the card immediately; start its motion when the selection
			// settling delay expires rather than using the usual 350 ms lead-in.
			uiTimerManager.setTimer(TimerName::LOADING_ANIMATION, kPresetPreviewDelayMs);
		}
	}
}

void LoadInstrumentPresetUI::stopPresetLoadingIndicator() {
	if (loading_indicator_active_) {
		display->removeLoadingAnimation();
		loading_indicator_active_ = false;
	}
}

void LoadInstrumentPresetUI::cancelPendingPreview() {
	stopPresetLoadingIndicator();
	preview_pending_ = false;
	uiTimerManager.unsetTimer(TimerName::INSTRUMENT_PREVIEW);
}

bool LoadInstrumentPresetUI::hasPendingPresetLoad() const {
	return (getCurrentUI() == this
	        && (preview_pending_ || currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED))
	       || preset_navigation_.task >= 0 || preset_navigation_.resuming;
}

void LoadInstrumentPresetUI::selectEncoderUsedForGlobalControl() {
	// Modifier state may change after an abort was requested. A global control
	// will not consume the navigation cursor, so restore the actual preset.
	if (getCurrentUI() != this && preset_navigation_cursor_.instrument != nullptr) {
		cancelPresetNavigation();
		clearPresetNavigationCursor();
		redrawLivePresetName();
		uiNeedsRendering(getCurrentUI());
	}
}

bool LoadInstrumentPresetUI::prepareForParameterEdit() {
	if (hasPendingPresetLoad()
	    && (sdRoutineLock || StorageManager::isDeserializerOpen() || encoders::selectNavigationPending())) {
		return false;
	}
	// Resolve browser previews before View obtains the parameter/model stack.
	if (getCurrentUI() == this && preview_pending_) {
		if (currentUIMode != UI_MODE_NONE || sdRoutineLock) {
			return false;
		}
		loadPendingPreview();
	}
	if (getCurrentUI() == this) {
		if (currentInstrumentLoadError != Error::NONE) {
			display->displayError(currentInstrumentLoadError);
			return false;
		}
	}
	if (preset_navigation_.task < 0) {
		return !presetNavigationContextMatches() || !preset_navigation_.editGesture.rejects(getSystemTime());
	}
	if (presetNavigationDecision() == deluge::gui::PresetNavigationDecision::CANCEL) {
		cancelPresetNavigation();
		clearPresetNavigationCursor();
		redrawLivePresetName();
		return true; // The new gesture edits the now visibly restored live model.
	}
	// Auditioning still prevents replacement. Cancel the selection and restore
	// the loaded preset's name before editing it instead.
	if (presetNavigationDecision() == deluge::gui::PresetNavigationDecision::WAIT) {
		const bool valid_target = presetNavigationContextMatches();
		cancelPresetNavigation();
		clearPresetNavigationCursor();
		if (valid_target) {
			view.displayOutputName(preset_navigation_.instrument, preset_navigation_.doBlink,
			                       preset_navigation_.displayClip);
		}
		return true;
	}
	// The callback normally runs as a one-shot scheduler task. Remove that task
	// before invoking it synchronously, so it cannot run again after the edit.
	removeTask(preset_navigation_.task);
	resumePresetNavigation();
	if (preset_navigation_.task < 0 && !preset_navigation_.loadSucceeded) {
		preset_navigation_.editGesture.reject(getSystemTime());
	}
	return preset_navigation_.task < 0 && preset_navigation_.loadSucceeded;
}

void LoadInstrumentPresetUI::focusRegained() {
	// The base focus redraw replaces numeric display layers directly.
	stopPresetLoadingIndicator();
	LoadUI::focusRegained();
	if (preview_pending_) {
		startPresetLoadingIndicator();
		uiTimerManager.setTimer(TimerName::INSTRUMENT_PREVIEW, kPresetPreviewDelayMs);
	}
}

void LoadInstrumentPresetUI::previewTimerCallback() {
	if (!preview_pending_ || getCurrentUI() != this) {
		stopPresetLoadingIndicator();
		return;
	}
	// File parsing services UI timers too. Never start a nested load, or disturb
	// a long press/context menu. Give queued navigation a chance to run first.
	if (sdRoutineLock || StorageManager::isDeserializerOpen() || currentUIMode != UI_MODE_NONE
	    || encoders::selectNavigationPending()) {
		if (currentUIMode != UI_MODE_NONE) {
			stopPresetLoadingIndicator();
		}
		uiTimerManager.setTimer(TimerName::INSTRUMENT_PREVIEW, kPresetPreviewDelayMs);
		return;
	}
	loadPendingPreview();
}

void LoadInstrumentPresetUI::loadPendingPreview() {
	deluge::gui::performPresetLoad(
	    preview_pending_, currentUIMode, predictionInterrupted, currentInstrumentLoadError,
	    [] { uiTimerManager.unsetTimer(TimerName::INSTRUMENT_PREVIEW); },
	    [this] { return loadingSynthToKitRow ? performLoadSynthToKit() : performLoad(); });
}

void LoadInstrumentPresetUI::clearPresetNavigationCursor() {
	preset_navigation_cursor_.instrument = nullptr;
	preset_navigation_cursor_.filename.clear();
	preset_navigation_cursor_.sourceName.clear();
	preset_navigation_cursor_.dir.clear();
}

Error LoadInstrumentPresetUI::rememberPresetNavigationCandidate(Instrument* origin, FileItem* candidate,
                                                                Clip* targetClip) {
	clearPresetNavigationCursor();
	const Error error = preset_navigation_cursor_.filename.set(candidate->displayName);
	if (error != Error::NONE) {
		clearPresetNavigationCursor();
		return error;
	}
	preset_navigation_cursor_.sourceName.set(&origin->name);
	preset_navigation_cursor_.dir.set(&origin->dirPath);
	preset_navigation_cursor_.instrument = origin;
	preset_navigation_cursor_.song = currentSong;
	preset_navigation_cursor_.clip = getCurrentClip();
	preset_navigation_cursor_.ui = getCurrentUI();
	preset_navigation_cursor_.targetClip = targetClip;
	return Error::NONE;
}

void LoadInstrumentPresetUI::cancelPresetNavigation() {
	if (preset_navigation_.task >= 0) {
		removeTask(preset_navigation_.task);
		preset_navigation_.task = -1;
		stopPresetLoadingIndicator();
	}
}

Error LoadInstrumentPresetUI::schedulePresetNavigation() {
	preset_navigation_.task = addConditionalTask(resumePresetNavigation, 20, presetNavigationReady, "preset navigation",
	                                             RESOURCE_SD | RESOURCE_SD_ROUTINE);
	if (preset_navigation_.task < 0) {
		stopPresetLoadingIndicator();
		clearPresetNavigationCursor();
		return Error::INSUFFICIENT_RAM;
	}
	return Error::NONE;
}

deluge::gui::PresetNavigationDecision LoadInstrumentPresetUI::presetNavigationDecision() const {
	return preset_navigation_.decision(currentSong, getCurrentUI(), currentUIMode,
	                                   (currentSong != nullptr) ? getCurrentClip() : nullptr,
	                                   playbackHandler.recording == RecordingMode::ARRANGEMENT);
}

void LoadInstrumentPresetUI::presetNavigationPadReleased() {
	// Capture a genuine release before the caller clears its selection and UI mode.
	if (preset_navigation_.task >= 0) {
		preset_navigation_.markPadReleased(presetNavigationContextMatches());
		stopPresetLoadingIndicator();
	}
}

void LoadInstrumentPresetUI::presetNavigationPadReleaseFinished() {
	if (preset_navigation_.task >= 0 && preset_navigation_.padReleased) {
		startPresetLoadingIndicator();
	}
}

void LoadInstrumentPresetUI::redrawLivePresetName() {
	if (!display->have7SEG() || currentSong == nullptr) {
		return;
	}
	if (getCurrentUI() == &sessionView && currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		if (Clip* clip = sessionView.getClipForLayout(); clip != nullptr) {
			view.displayOutputName(clip->output, false, clip);
		}
	}
	else if (getCurrentUI() == &sessionView || getCurrentUI() == &arrangerView) {
		sessionView.redrawNumericDisplay();
	}
	else if (getCurrentUI() == &performanceView) {
		performanceView.redrawNumericDisplay();
	}
	else if (getCurrentUI() == preset_navigation_.ui && getCurrentClip() != nullptr) {
		// Use the live clip/output, never the saved target of a cancelled task.
		view.displayOutputName(getCurrentClip()->output, false, getCurrentClip());
	}
}

bool LoadInstrumentPresetUI::presetNavigationContextMatches() const {
	const auto& pending = preset_navigation_;
	if (presetNavigationDecision() == deluge::gui::PresetNavigationDecision::CANCEL) {
		return false;
	}
	// Hibernating candidates can be deleted or reused while the task waits.
	if (pending.item.instrument != nullptr) {
		if (!isInstrumentInList(pending.item.instrument, currentSong->firstHibernatingInstrument)
		    || strcmp(pending.item.instrument->name.get(), pending.candidateName.get()) != 0
		    || strcmp(pending.item.instrument->dirPath.get(), pending.candidateDir.get()) != 0) {
			return false;
		}
	}
	// Compare against live objects before dereferencing saved pointers.
	bool instrument_exists = false;
	for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
		if (output == pending.instrument) {
			instrument_exists = true;
			break;
		}
	}
	if (!instrument_exists || (strcmp(pending.instrument->name.get(), preset_navigation_cursor_.sourceName.get()) != 0)
	    || (strcmp(pending.instrument->dirPath.get(), preset_navigation_cursor_.dir.get()) != 0)) {
		return false;
	}
	auto row = arrangerView.yPressedEffective;
	bool arranger_target_matches =
	    row >= 0 && row < kDisplayHeight && arrangerView.outputsOnScreen[row] == pending.instrument;
	Clip* selected_clip =
	    !pending.padReleased && (pending.selectedClip != nullptr) ? sessionView.getClipForLayout() : nullptr;
	if (!pending.matchesSelection(selected_clip, arranger_target_matches)) {
		return false;
	}
	Clip* target = (pending.displayClip != nullptr) ? pending.displayClip : pending.selectedClip;
	if (target != nullptr) {
		for (Clip* clip : AllClips::everywhere(currentSong)) {
			if (clip == target) {
				return clip->output == pending.instrument;
			}
		}
		return false;
	}
	return true;
}

bool LoadInstrumentPresetUI::presetNavigationReady() {
	// Keep the scheduler's condition cheap. Validate the saved context and check
	// queued input when the task actually runs, after its resources are available.
	return !StorageManager::isDeserializerOpen() && !encoders::selectNavigationPending()
	       && getSystemTime() >= loadInstrumentPresetUI.preset_navigation_.deadline
	       && loadInstrumentPresetUI.presetNavigationDecision() != deluge::gui::PresetNavigationDecision::WAIT;
}

void LoadInstrumentPresetUI::resumePresetNavigation() {
	auto& ui = loadInstrumentPresetUI;
	auto& pending = ui.preset_navigation_;
	pending.task = -1; // The scheduler removes this one-shot task after it returns.
	pending.loadSucceeded = false;
	if (!ui.presetNavigationContextMatches()) {
		ui.stopPresetLoadingIndicator();
		ui.clearPresetNavigationCursor();
		ui.redrawLivePresetName();
		uiNeedsRendering(getCurrentUI());
		renderUIsForOled();
		return;
	}

	// The scheduler may have latched readiness before an audition pad went down.
	// Requeue after validating the target, rather than losing the candidate or
	// replacing the instrument while notes from it are still being auditioned.
	if (ui.presetNavigationDecision() == deluge::gui::PresetNavigationDecision::WAIT
	    || encoders::selectNavigationPending()) {
		Error error = ui.schedulePresetNavigation();
		if (error != Error::NONE) {
			display->displayError(error);
		}
		return;
	}

	pending.resuming = true;
	// Preserve whole-output replacement even when another UI delegates the navigation.
	if (pending.destination() == deluge::gui::PresetNavigationDestination::CLIP) {
		alignas(ModelStackWithTimelineCounter) std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
		auto* model_stack =
		    setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, pending.displayClip);
		view.navigateThroughPresetsForInstrumentClip(0, model_stack, pending.doBlink);
		pending.loadSucceeded = pending.loadSucceeded && pending.displayClip->output == pending.item.instrument;
		if (pending.loadSucceeded) {
			view.displayOutputName(pending.displayClip->output, pending.doBlink, pending.displayClip);
			if (getCurrentUI() == &keyboardScreen) {
				keyboardScreen.presetChanged();
			}
		}
	}
	else if (pending.destination() == deluge::gui::PresetNavigationDestination::ARRANGER) {
		arrangerView.navigateThroughPresets(0);
	}
	else {
		Output* output = currentSong->navigateThroughPresetsForInstrument(pending.instrument, 0, pending.displayClip);
		view.setActiveModControllableTimelineCounter(output->getActiveClip());
		uiNeedsRendering(getCurrentUI());
		renderUIsForOled();
	}
	pending.resuming = false;
	if (pending.padReleased) {
		view.setActiveModControllableTimelineCounter(currentSong);
		uiNeedsRendering(getCurrentUI());
		renderUIsForOled();
	}
}
