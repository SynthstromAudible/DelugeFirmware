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
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/root_ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
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
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/string.h"
#include "util/try.h"

using namespace deluge;
namespace encoders = deluge::hid::encoders;

LoadInstrumentPresetUI loadInstrumentPresetUI{};

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

	// The QWERTY keyboard is always shown in this UI (qwertyAlwaysVisible stays true), so the favourites row should
	// be too. The static qwertyVisible flag can be left false by another browser (e.g. song load with the keyboard
	// toggled off); without resetting it here the favourites row would stay hidden until something else set it. #4674
	qwertyVisible = true;

	if (getRootUI() == &keyboardScreen) {
		PadLEDs::skipGreyoutFade();
	}
	if (instrumentToReplace) {
		initialOutputType = instrumentToReplace->type;
		initialName = instrumentToReplace->name;
		initialDirPath = instrumentToReplace->dirPath;
	}

	if (loadingSynthToKitRow) {
		initialOutputType = outputTypeToLoad = OutputType::SYNTH;
		if (soundDrumToReplace) {
			initialName = soundDrumToReplace->drumName;
		}
		else {
			initialName = "";
		}

		initialDirPath = "SYNTHS";
	}

	switch (instrumentToReplace->type) {
	case OutputType::MIDI_OUT:
		initialChannelSuffix = ((MIDIInstrument*)instrumentToReplace)->channelSuffix;
		// intentional fallthrough to share code with CV case

	case OutputType::CV:
		initialChannel = ((NonAudioInstrument*)instrumentToReplace)->getChannel();
		break;

	// explicit fallthrough cases
	case OutputType::AUDIO:
	case OutputType::SYNTH:
	case OutputType::KIT:
	case OutputType::NONE:;
	}

	changedInstrumentForClip = false;
	replacedWholeInstrument = false;

	if (instrumentClipToLoadFor) {
		instrumentClipToLoadFor
		    ->backupPresetSlot(); // Store this now cos we won't be storing it between each navigation we do
	}

	Error error = beginSlotSession(); // Requires currentDir to be set. (Not anymore?)
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	actionLogger.deleteAllLogs();

	std::string searchFilename = setupForOutputType(); // Sets currentDir.
	// The listing (and its post-listing tail, in onBrowserOpened()) happens async: dispatch it and
	// return optimistically. Failure goes through the base Browser::onListingFailed() (displayError
	// + close()) once the listing completes.
	beginListing({.action = ListingAction::Open,
	              .direction = 0,
	              .filenameToStartAt = searchFilename,
	              .defaultDir = getInstrumentFolder(outputTypeToLoad)});

	return true;
}

// Computes the LED/icon/title state and currentDir for outputTypeToLoad's category, and returns
// the filename to search for within it (empty if none). Does NOT perform the listing itself -
// callers combine this with getInstrumentFolder(outputTypeToLoad) (the category's default dir) to
// dispatch an async Open listing, either from opened() or from changeOutputType() (both go through
// beginListing(); see changeOutputType()'s comment for how its Open listing is told apart from
// opened()'s).
std::string LoadInstrumentPresetUI::setupForOutputType() {
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

	// not used for midi
	filePrefix = (outputTypeToLoad == OutputType::SYNTH) ? "SYNT" : "KIT";

	enteredText.clear();

	char const* defaultDir = getInstrumentFolder(outputTypeToLoad);

	std::string searchFilename;

	// I don't have this calling arrivedInNewFolder(), because as you can see below, we want to either just display the
	// existing preset, or call confirmPresetOrNextUnlaunchedOne() to skip any which aren't "available".

	// If same Instrument type as we already had...
	if (instrumentToReplace && instrumentToReplace->type == outputTypeToLoad) {

		// Then we can start by just looking at the existing Instrument, cos they're the same type...
		currentDir = instrumentToReplace->dirPath;
		searchFilename = instrumentToReplace->name;

		if (currentDir.empty()) {
			goto useDefaultFolder;
		}
	}

	// Or if the Instruments are different types...
	else {
		if (loadingSynthToKitRow && soundDrumToReplace) {

			if (!soundDrumToReplace->drumName.empty()) {
				enteredText = soundDrumToReplace->drumName;
				searchFilename = soundDrumToReplace->drumName;
			}

			if (&soundDrumToReplace->path) {
				currentDir = soundDrumToReplace->path;
				if (currentDir.empty()) {
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
			const std::string& backedUpName = instrumentClipToLoadFor->backedUpInstrumentName[outputTypeToLoadAsIdx];
			enteredText = backedUpName;
			searchFilename = backedUpName;
			currentDir = instrumentClipToLoadFor->backedUpInstrumentDirPath[outputTypeToLoadAsIdx];
			if (currentDir.empty()) {
				goto useDefaultFolder;
			}
		}

		// Otherwise we just start with nothing. currentSlot etc remain set to "zero" from before
		else {
useDefaultFolder:
			currentDir = defaultDir;
		}
	}

	if (!searchFilename.empty()) {
		searchFilename.append(".XML");
	}

	return searchFilename;
}

// Shared post-listing tail for both async Open listings this UI dispatches (onBrowserOpened(), run
// once beginListing()'s listing completes): the browser-open path from opened(), and the output-type
// switch path from changeOutputType() (see onBrowserOpened() below for how the two are told apart).
void LoadInstrumentPresetUI::finishArrivedInFolder(char const* defaultDir) {
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
}

void LoadInstrumentPresetUI::onBrowserOpened() {
	finishArrivedInFolder(getInstrumentFolder(outputTypeToLoad));

	if (changingOutputType_) {
		// This Open listing was dispatched by changeOutputType(), not opened() - run its post-listing
		// follow-up now that the listing has actually completed (see changeOutputType()'s comment).
		changingOutputType_ = false;
		renderUIsForOled();
		// The new type's folder is listed: load its first file, as a scroll onto it would.
		currentFileChanged(0);
	}
	else {
		// opened()'s post-listing tail: focusRegained() runs here, after the listing actually
		// completes, matching the Save* browsers' convention.
		focusRegained();
	}
}

void LoadInstrumentPresetUI::onListingFailed(Error error) {
	if (changingOutputType_) {
		changingOutputType_ = false;
		outputTypeToLoad = outputTypeBeforeChange_;
		if (!listingRestored_) {
			// No copy of what the change replaced, for want of memory: changeOutputType()'s original synchronous
			// path, which silently reverted outputTypeToLoad and left the browser open.
			return;
		}
		// The failed listing put back the previous type's folder, list and entered text. setupForOutputType() puts
		// back that type's title, icon, lights and file prefix, which the change replaced, but it also picks a folder
		// and entered text of its own: keep the restored ones. The base then shows the error and stays open.
		std::string dir = std::move(currentDir);
		std::string text = std::move(enteredText);
		(void)setupForOutputType();
		currentDir = std::move(dir);
		enteredText = std::move(text);
	}
	Browser::onListingFailed(error);
}

void LoadInstrumentPresetUI::folderContentsReady(int32_t entryDirection) {
	currentFileChanged(0);
}

deluge::gui::InstrumentLoadTarget LoadInstrumentPresetUI::snapshotTarget(int32_t movementDirection) {
	FileItem* currentFileItem = getCurrentFileItem();
	return {
	    .loadingSynthToKitRow = loadingSynthToKitRow,
	    .movementDirection = movementDirection,
	    .hasFile = currentFileItem != nullptr,
	    .isFolder = currentFileItem != nullptr && currentFileItem->isFolder,
	    .maybeExistsOnCard = currentFileItem == nullptr || currentFileItem->maybeExistsOnCard,
	    .existingInstrument = currentFileItem != nullptr ? currentFileItem->instrument : nullptr,
	    .path = currentFileItem != nullptr ? getCurrentFilePath() : std::string{},
	    .name = enteredText,
	    .dirPath = currentDir,
	};
}

bool LoadInstrumentPresetUI::scrollLoadInFlight() const {
	return deluge::gui::instrumentLoad.inFlight();
}

// Every non-reload encoder tick asks for the file the cursor lands on. The machine coalesces latest-wins, so a fast
// scroll cancels each load it leaves and only loads where the user settles.
void LoadInstrumentPresetUI::currentFileChanged(int32_t movementDirection) {
	deluge::gui::instrumentLoad.request({.target = snapshotTarget(movementDirection), .completion = &onScrollLoaded});
}

void LoadInstrumentPresetUI::onScrollLoaded(Error error) {
	loadInstrumentPresetUI.currentInstrumentLoadError = error;
	if (error != Error::NONE) {
		display->displayError(error);
	}
}

void LoadInstrumentPresetUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	// If it's a directory...
	if (currentFileItem->isFolder) {
		// goIntoFolder() now dispatches onto the storage owner; failure is handled by the base
		// Browser::onListingFailed() (displayError + close()) once the listing completes.
		goIntoFolder(currentFileItem->filename.c_str());
	}

	else {
		// The commit does its own authoritative load of the file, rather than trusting currentInstrumentLoadError: a
		// scroll-load of it may still be out, in which case the machine promotes that load to the commit.
		deluge::gui::instrumentLoad.request(
		    {.target = snapshotTarget(0), .commit = true, .completion = &onCommitLoaded});
	}
}

void LoadInstrumentPresetUI::onCommitLoaded(Error error) {
	loadInstrumentPresetUI.currentInstrumentLoadError = error;
	if (error != Error::NONE) {
		display->displayError(error);
		return;
	}

	if (loadInstrumentPresetUI.outputTypeToLoad == OutputType::KIT && loadInstrumentPresetUI.showingAuditionPads()) {
		// New NoteRows have probably been created, whose colours haven't been grabbed yet.
		instrumentClipView.recalculateColours();
	}

	loadInstrumentPresetUI.close();
}

void LoadInstrumentPresetUI::requestClone(void (*completion)(Error error)) {
	deluge::gui::instrumentLoad.request(
	    {.target = snapshotTarget(0), .clone = true, .commit = true, .completion = completion});
}

void LoadInstrumentPresetUI::selectEncoderAction(int8_t offset) {
	// Refused while a commit is in flight: InstrumentLoad closes the browser's gate until it ends.
	if (deluge::gui::instrumentLoad.commitInFlight()) {
		return;
	}
	LoadUI::selectEncoderAction(offset);
}

void LoadInstrumentPresetUI::close() {
	deluge::gui::instrumentLoad.cancel();
	LoadUI::close();
}

bool LoadInstrumentPresetUI::dispatchPresetAcquisition(const deluge::storage::PresetRequest& request) {
	return deluge::gui::instrumentLoad.request(
	    {.kind = deluge::gui::InstrumentLoadRequest::Kind::Acquire, .preset = request});
}

bool LoadInstrumentPresetUI::presetAcquisitionInFlight() const {
	return deluge::gui::instrumentLoad.acquisitionInFlight();
}

bool LoadInstrumentPresetUI::beginAcquisition(const deluge::storage::PresetRequest& request,
                                              deluge::storage::AcquireWalkSpec* walk) {
	using deluge::storage::AcquireWalk;
	using deluge::storage::PresetConsumer;
	if (request.oldInstrument != nullptr && !isRequestInstrumentStillLive(request.oldInstrument)) {
		return false;
	}
	switch (request.consumer) {
	case PresetConsumer::ArrangerChangeType:
	case PresetConsumer::SessionChangeType:
		// Song::changeOutputType()'s Synth/Kit arm: the first preset of the type nobody in the song uses.
		*walk = {.walk = AcquireWalk::Unlaunched,
		         .outputType = request.outputType,
		         .availability = Availability::INSTRUMENT_UNUSED,
		         .dirPath = getInstrumentFolder(request.outputType)};
		return true;
	case PresetConsumer::ViewChangeType:
	case PresetConsumer::ViewChangeTypeMinder:
	case PresetConsumer::SessionRowsChangeType:
	case PresetConsumer::KeyboardChangeType: {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, request.clip);
		return request.clip->beginChangeOutputType(modelStack, request.outputType, walk);
	}
	case PresetConsumer::ArrangerNavigate:
	case PresetConsumer::SessionNavigate:
		// Song::navigateThroughPresetsForInstrument()'s prologue.
		actionLogger.deleteAllLogs();
		currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E063", "H063");
		*walk = {.walk = AcquireWalk::Navigate,
		         .outputType = request.oldInstrument->type,
		         .availability = Availability::INSTRUMENT_UNUSED,
		         .dirPath = request.oldInstrument->dirPath,
		         .name = request.oldInstrument->name,
		         .offset = request.offset,
		         .oldInstrument = request.oldInstrument,
		         .blink = true};
		return true;
	case PresetConsumer::ViewNavigateClipPreset: {
		Availability availabilityRequirement;
		(void)currentSong->shouldOldOutputBeReplaced(request.clip, &availabilityRequirement);
		*walk = {.walk = AcquireWalk::Navigate,
		         .outputType = request.oldInstrument->type,
		         .availability = availabilityRequirement,
		         .dirPath = request.oldInstrument->dirPath,
		         .name = request.oldInstrument->name,
		         .offset = request.offset,
		         .oldInstrument = request.oldInstrument,
		         .blink = false};
		return true;
	}
	case PresetConsumer::BlankSongPreset:
		// The blank song's first preset: the first SYNTHS preset there is, with the song's own default synth left out
		// of the walk -- it is named "0", so a walk that counted it would land on it.
		*walk = {.walk = AcquireWalk::Unlaunched,
		         .outputType = OutputType::SYNTH,
		         .availability = Availability::ANY,
		         .includeSong = false,
		         .dirPath = getInstrumentFolder(OutputType::SYNTH)};
		return true;
	case PresetConsumer::ArrangerNewInstrument:
		// ArrangerView::createNewInstrument(): the first synth preset nobody in the song uses.
		*walk = {.walk = AcquireWalk::Unlaunched,
		         .outputType = OutputType::SYNTH,
		         .availability = Availability::INSTRUMENT_UNUSED,
		         .dirPath = getInstrumentFolder(OutputType::SYNTH)};
		return true;
	case PresetConsumer::SessionNewTrack:
	case PresetConsumer::SessionNewTrackEnter:
	case PresetConsumer::SessionNewTrackSelect:
	case PresetConsumer::SessionNewTrackClone:
		// setPresetOrNextUnlaunchedOne(): the first preset of the type nobody in the song uses. A clone's type is its
		// source clip's.
		*walk = {.walk = AcquireWalk::Unlaunched,
		         .outputType = request.outputType,
		         .availability = Availability::INSTRUMENT_UNUSED,
		         .dirPath = getInstrumentFolder(request.outputType)};
		return true;
	case PresetConsumer::MinderNewInstrument:
	case PresetConsumer::ViewNewInstrument:
	case PresetConsumer::KeyboardNewInstrument:
		// InstrumentClipMinder::createNewInstrument(): a new blank Instrument, under a slot name no preset has.
		*walk = {.walk = AcquireWalk::UnusedSlot,
		         .outputType = request.outputType,
		         .dirPath = getInstrumentFolder(request.outputType)};
		return true;
	}
	return false;
}

void LoadInstrumentPresetUI::deliverAcquisition(const deluge::storage::PresetRequest& request,
                                                const deluge::storage::AcquireResult& acquired) {
	runAcquisitionFollowUp(request, acquired);
	if (acquired.loadedFromFile && acquired.instrument != nullptr
	    && !isInstrumentInList(acquired.instrument, currentSong->firstOutput)
	    && !isInstrumentInList(acquired.instrument, currentSong->firstHibernatingInstrument)) {
		currentSong->deleteOutput(acquired.instrument); // parsed for this gesture, never taken into the song
	}
}

void LoadInstrumentPresetUI::runAcquisitionFollowUp(const deluge::storage::PresetRequest& request,
                                                    const deluge::storage::AcquireResult& acquired) {
	using deluge::storage::PresetConsumer;
	// A coalesced request can carry a stale `oldInstrument` -- see isRequestInstrumentStillLive()'s
	// doc. Consumers that never set it (ArrangerNewInstrument) leave it null, so the `!= nullptr`
	// guard skips this check for them rather than abandoning every follow-up that doesn't use it.
	if (request.oldInstrument != nullptr && !isRequestInstrumentStillLive(request.oldInstrument)) {
		return;
	}
	switch (request.consumer) {
	case PresetConsumer::BlankSongPreset:
		currentSong->completeBlankPreset(acquired);
		break;
	case PresetConsumer::ArrangerNewInstrument:
		arrangerView.completeNewInstrument(request.yPressed, request.yPressedRaw, request.renderUi, acquired);
		break;
	case PresetConsumer::ArrangerChangeType:
		arrangerView.completeChangeOutputType(request.outputType, request.oldInstrument, request.yPressed, &acquired);
		break;
	case PresetConsumer::ArrangerNavigate:
		arrangerView.completeNavigateThroughPresets(request.offset, request.oldInstrument, request.yPressed, &acquired);
		break;
	case PresetConsumer::ViewChangeType:
	case PresetConsumer::ViewChangeTypeMinder:
	case PresetConsumer::SessionRowsChangeType:
	case PresetConsumer::KeyboardChangeType:
		// All four route through the same per-consumer completion changeOutputType()'s synchronous
		// MIDI/CV arm also uses, so each caller's redraw/LED tail runs from exactly one place either
		// way -- see View::completeChangeOutputTypeFor()'s doc.
		(void)view.completeChangeOutputTypeFor(request.consumer, request.outputType, request.clip, request.doBlink,
		                                       &acquired);
		break;
	case PresetConsumer::ViewNavigateClipPreset:
		view.completeNavigateThroughPresetsForInstrumentClip(request.offset, request.clip, request.doBlink, &acquired);
		break;
	case PresetConsumer::SessionChangeType:
		sessionView.completeChangeOutputType(request.outputType, request.oldInstrument, &acquired);
		break;
	case PresetConsumer::SessionNavigate:
		sessionView.completeNavigate(request.offset, request.oldInstrument, &acquired);
		break;
	case PresetConsumer::SessionNewTrack:
		sessionView.completeNewTrack(request.outputType, request.xPressed, request.yPressed, request.clipIndex,
		                             request.shiftHeld, &acquired);
		break;
	case PresetConsumer::SessionNewTrackEnter:
		sessionView.completeNewTrackGridEnter(request.outputType, request.packedXY, request.padStillPressed, &acquired);
		break;
	case PresetConsumer::SessionNewTrackSelect:
		sessionView.completeNewTrackGridSelect(request.outputType, request.packedXY, request.padStillPressed,
		                                       &acquired);
		break;
	case PresetConsumer::SessionNewTrackClone:
		sessionView.completeNewTrackClone(request.targetSection, request.sourceClip, &acquired);
		break;
	case PresetConsumer::MinderNewInstrument:
		// InstrumentClipMinder::completeNewInstrument() (the leaf underneath) reads only its `clip`
		// parameter and global Song state, never `this` -- so which InstrumentClipMinder-derived
		// instance runs it is interchangeable; instrumentClipView is used here only because it is
		// already referenced nearby, the same idiom completeChangeOutputTypeFor()'s ViewChangeTypeMinder
		// arm uses for setLedStates().
		instrumentClipView.InstrumentClipMinder::completeNewInstrument(request.clip, request.outputType, request.isDx,
		                                                               acquired);
		break;
	case PresetConsumer::ViewNewInstrument:
		instrumentClipView.completeNewInstrument(request.clip, request.outputType, request.isDx, acquired);
		break;
	case PresetConsumer::KeyboardNewInstrument:
		// Same tail as ViewNewInstrument, plus the layout refresh keyboard_screen.cpp used to run
		// eagerly off createNewInstrument()'s old synchronous return -- see PresetConsumer::
		// KeyboardNewInstrument's doc. Gated on the same "did it actually create" bool
		// completeNewInstrument() already returns, so a failed acquisition (no unused slot, OOM) does
		// not refresh the layout for nothing.
		//
		// ALSO gated on request.clip still being current: unlike the LED tail beside it (setLedStates(),
		// a pure "show the truth" read), selectLayout() both reads AND WRITES per-clip model state
		// (keyboardState.currentLayout) -- if the current clip changed between dispatch and this
		// completion running, writing through getCurrentInstrumentClip() would set the layout on the
		// WRONG clip. request.clip already survived the liveness guard above; comparing it against
		// what's current now is what makes the write itself safe, not just the pointer.
		if (instrumentClipView.completeNewInstrument(request.clip, request.outputType, request.isDx, acquired)
		    && getCurrentInstrumentClip() == request.clip) {
			keyboardScreen.completeLayoutRefresh();
		}
		break;
	}
}

ActionResult LoadInstrumentPresetUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;
	// Refused while a commit is in flight: InstrumentLoad closes the browser's gate until it ends.
	if (deluge::gui::instrumentLoad.commitInFlight()) {
		return ActionResult::DEALT_WITH;
	}

	OutputType newOutputType;

	// Load button
	if (b == LOAD) {
		return mainButtonAction(on);
	}

	// Synth button
	else if (b == SYNTH) {
		newOutputType = OutputType::SYNTH;
doChangeOutputType:
		if (listingInProgress_) {
			return ActionResult::DEALT_WITH;
		}
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
	// Refused while a commit is in flight: InstrumentLoad closes the browser's gate until it ends.
	if (deluge::gui::instrumentLoad.commitInFlight()) {
		return ActionResult::DEALT_WITH;
	}
	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {

		if (isSDRoutineActive()) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // The below needs to access the card.
		}

		currentUIMode = UI_MODE_NONE;

		FileItem* currentFileItem = getCurrentFileItem();

		// Folders don't have a context menu
		if (!currentFileItem || currentFileItem->isFolder) {
			return ActionResult::DEALT_WITH;
		}

		// We want to open the context menu to choose to reload the original file for the currently selected
		// preset in some way. So first up, make sure there is a file at that path.
		std::string filePath = getCurrentFilePath();

		// TODO(conflation-batch-4): unknown is being treated as absent here; the load/browse/favourites
		// batch gives this site its real ruling.
		bool fileExists = StorageManager::fileExists(filePath.c_str()).value_or(false);
		if (!fileExists) {
			// An unsaved (in-memory only) synth preset cannot be loaded / cloned because
			// the XML is read from disk to create the instrument. If the preset hasn't
			// been saved yet, there is no file on the SD card to read from.
			display->displayError(Error::FILE_NOT_SAVED);
			return ActionResult::DEALT_WITH;
		}

		bool available = gui::context_menu::loadInstrumentPreset.setupAndCheckAvailability();

		if (available) {
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
				message = "Instrument switched to CV channel";
				display->displayPopup(message);
			}

			close();
		}
	}

	// Or, for normal synths, kits and midi
	else {
		// Copied before setupForOutputType() changes the folder and entered text, so a failed listing puts back the
		// previous type's, not a mixture of the two. beginListing() below keeps this copy.
		captureListingRestore();
		OutputType oldOutputType = outputTypeToLoad;
		outputTypeToLoad = newOutputType;

		std::string searchFilename = setupForOutputType();
		// Route this listing through the owner too, the same way opened() does. changingOutputType_
		// tells the shared onBrowserOpened()/onListingFailed() hooks apart from opened()'s Open
		// listing so they can run this path's own follow-up/revert once the listing actually
		// completes (see onBrowserOpened() and onListingFailed()). buttonAction()'s
		// doChangeOutputType gate (listingInProgress_) still guards re-entry while this is in flight.
		outputTypeBeforeChange_ = oldOutputType;
		changingOutputType_ = true;
		bool dispatched = beginListing({.action = ListingAction::Open,
		                                .direction = 0,
		                                .filenameToStartAt = searchFilename,
		                                .defaultDir = getInstrumentFolder(outputTypeToLoad)});
		if (!dispatched) {
			// Owner queue was full - the listing never ran, so onBrowserOpened()/onListingFailed()
			// won't fire to reconcile the state set above. Revert it here so the UI is left
			// consistent (retryable via the same button - outputTypeToLoad no longer equals
			// newOutputType - and the next listing that does complete won't get misrouted by a
			// stuck changingOutputType_).
			outputTypeToLoad = outputTypeBeforeChange_;
			changingOutputType_ = false;
		}
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
	initialInstrument =
	    currentSong->getInstrumentFromPresetSlot(initialOutputType, initialChannel, initialChannelSuffix,
	                                             initialName.c_str(), initialDirPath.c_str(), false, true);

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
			initialInstrument = currentSong->getInstrumentFromPresetSlot(initialOutputType, 0, 0, initialName.c_str(),
			                                                             initialDirPath.c_str(), true, false);

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
				enteredText = initialName;
				currentDir = initialDirPath;

				// Try getting from file
				std::string filePath = getCurrentFilePath();

				// TODO(conflation-batch-4): unknown is being treated as absent here; the
				// load/browse/favourites batch gives this site its real ruling.
				bool success = StorageManager::fileExists(filePath.c_str()).value_or(false);
				if (!success) {
					return;
				}

				// Off the card, on the interaction tier: the browser closes now, and swapRevert() puts the original
				// back when InstrumentLoad has it. The gate stays closed until then, and closing the browser does not
				// cancel it.
				(void)deluge::gui::instrumentLoad.request({.commit = true,
				                                           .kind = deluge::gui::InstrumentLoadRequest::Kind::Revert,
				                                           .revert = {.path = filePath,
				                                                      .name = initialName,
				                                                      .dirPath = initialDirPath,
				                                                      .outputType = initialOutputType,
				                                                      .clip = instrumentClipToLoadFor,
				                                                      .instrumentToReplace = instrumentToReplace,
				                                                      .replaceWhole = replacedWholeInstrument,
				                                                      .needToAdd = true}});
				return;
			}

			initialInstrument->loadAllAudioFiles(true);
		}
	}

gotAnInstrument:
	swapRevert({.outputType = initialOutputType,
	            .clip = instrumentClipToLoadFor,
	            .instrumentToReplace = instrumentToReplace,
	            .replaceWhole = replacedWholeInstrument,
	            .needToAdd = needToAddInstrumentToSong},
	           initialInstrument);
}

void LoadInstrumentPresetUI::swapRevert(const deluge::gui::RevertTarget& target, Instrument* initialInstrument) {
	if (initialInstrument == nullptr) {
		return;
	}

	// If swapping whole Instrument...
	if (target.replaceWhole) {

		// We know the Instrument hasn't been added to the Song, and this call will do it
		currentSong->replaceInstrument(target.instrumentToReplace, initialInstrument);

		replacedWholeInstrument = true;
	}

	// Otherwise, just changeInstrument() for this one Clip.
	else {

		// If that Instrument wasn't already in use in the Song, copy default velocity over
		initialInstrument->defaultVelocity = target.instrumentToReplace->defaultVelocity;

		// If we're here, we know the Clip is not playing in the arranger (and doesn't even have an instance in there)

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, target.clip);

		Error error = target.clip->changeInstrument(modelStack, initialInstrument, nullptr,
		                                            InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED);
		// TODO: deal with errors!

		if (target.needToAdd) {
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

// See the header doc for why this exists: a currency check, not a lifetime one.
bool LoadInstrumentPresetUI::isRequestInstrumentStillLive(Instrument* instrument) {
	return isInstrumentInList(instrument, currentSong->firstOutput);
}

// Returns whether it was in fact an unused one that it was able to return
bool LoadInstrumentPresetUI::findUnusedSlotVariation(std::string* oldName, std::string* newName) {

	shouldInterpretNoteNames = false;

	char const* oldNameChars = oldName->c_str();
	int32_t oldNameLength = strlen(oldNameChars);

	{
		int32_t oldNumber = 1;
		(*newName) = *oldName;

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
					(*newName).resize(numberStartPos);
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
		(*newName).append(" ");

addNumber:
		for (;; oldNumber++) {
			(*newName).resize(numberStartPos);
			(*newName).append(deluge::string::fromInt(oldNumber + 1));
			char const* newNameChars = newName->c_str();

			int32_t i = searchFileItems(newNameChars);
			if (i >= static_cast<int32_t>(fileItems.size())) {
				break;
			}

			FileItem* fileItem = &fileItems[i];
			char const* fileItemNameChars = fileItem->filename.c_str();
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
//
// The card-free prologue of every browser load: what performLoad() and performLoadSynthToKit() decided before they
// touched the card. It reads the request's snapshot, never the browser's live list, and instrumentToReplace live, as
// the load before it left it.
deluge::gui::InstrumentLoadDecision
LoadInstrumentPresetUI::decideLoad(const deluge::gui::InstrumentLoadRequest& request) {
	const deluge::gui::InstrumentLoadTarget& target = request.target;
	deluge::gui::InstrumentLoadDecision decision{};

	if (!target.hasFile) {
		// Make it say "NONE" on numeric Deluge, for consistency with old times.
		decision.error = Error::FILE_NOT_FOUND;
		decision.done = true;
		return decision;
	}

	if (target.isFolder) {
		decision.done = true;
		return decision;
	}

	if (target.loadingSynthToKitRow) {
		// An unsaved (in-memory only) synth preset cannot be loaded into a kit row because the drum is read from the
		// preset's XML on the card. If the preset hasn't been saved yet, there is no file to read.
		if (!target.maybeExistsOnCard) {
			decision.error = Error::FILE_NOT_SAVED;
			decision.done = true;
		}
		return decision;
	}

	if (target.existingInstrument == instrumentToReplace && !request.clone) {
		// Happens if navigate over a folder's name (Instrument stays the same), then back onto that neighbouring
		// Instrument - you'd incorrectly get a "USED" error without this.
		decision.done = true;
		return decision;
	}

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

	// If we found an already existing Instrument object...
	if (!request.clone && target.existingInstrument != nullptr) {
		const bool wasHibernating =
		    isInstrumentInList(target.existingInstrument, currentSong->firstHibernatingInstrument);

		if (availabilityRequirement == Availability::INSTRUMENT_UNUSED) {
			if (!wasHibernating) {
				decision.error = Error::PRESET_IN_USE;
				decision.done = true;
				return decision;
			}
		}
		else if (availabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
			if (!wasHibernating && currentSong->doesOutputHaveActiveClipInSession(target.existingInstrument)) {
				decision.error = Error::PRESET_IN_USE;
				decision.done = true;
				return decision;
			}
		}

		// Ok, we can have it! This can only happen when changing a clip that is the only instance of its instrument to
		// another instrument that has an inactive clip already.
		decision.reuse = target.existingInstrument;
		decision.wasHibernating = wasHibernating;
		decision.shouldReplaceWhole = oldInstrumentShouldBeReplaced && wasHibernating;
		decision.needToAdd = wasHibernating;
		return decision;
	}

	// Or, if we need to load from file - perhaps forcibly because the user manually chose to clone...
	if (request.clone) {
		std::string fileName = target.name;
		if (!findUnusedSlotVariation(&fileName, &decision.clonedName)) {
			decision.error = Error::UNSPECIFIED;
			decision.done = true;
			return decision;
		}
	}
	decision.shouldReplaceWhole = oldInstrumentShouldBeReplaced;
	decision.needToAdd = true;
	return decision;
}

// The tail of every browser load of a synth, kit or MIDI preset, in one pass: what performLoad() did once its
// Instrument had its samples.
Error LoadInstrumentPresetUI::swapInstrument(const deluge::gui::InstrumentLoadRequest& request,
                                             const deluge::gui::InstrumentLoadDecision& decision,
                                             Instrument* newInstrument) {
	if (decision.wasHibernating) {
		currentSong->removeInstrumentFromHibernationList(newInstrument);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);

	// If swapping whole Instrument...
	if (decision.shouldReplaceWhole) {

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

		if (decision.needToAdd) {
			currentSong->addOutput(newInstrument);
		}

		changedInstrumentForClip = true;
	}

	// Check if old Instrument has been deleted, in which case need to update the appropriate FileItem.
	if (!isInstrumentInList(instrumentToReplace, currentSong->firstOutput)
	    && !isInstrumentInList(instrumentToReplace, currentSong->firstHibernatingInstrument)) {
		for (int32_t f = static_cast<int32_t>(fileItems.size()) - 1; f >= 0; f--) {
			FileItem* fileItem = &fileItems[f];
			if (fileItem->instrument == instrumentToReplace) {
				fileItem->instrument = nullptr;
				break;
			}
		}
	}

	// The FileItem cache of the loaded Instrument is not written: the listing may have been rebuilt since the load was
	// asked for, so no FileItem pointer is held across it. A later listing re-associates the Instrument with its file
	// through the song's own scan.
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
	return Error::NONE;
}

// The tail of a synth preset loaded into a kit row, in one pass: what performLoadSynthToKit() and loadSynthToDrum() did
// around the drum's load, with the old drum taken out only once the new one is ready.
Error LoadInstrumentPresetUI::swapKitRow(const deluge::gui::InstrumentLoadRequest& request, SoundDrum* newDrum) {
	Kit* kitToLoadFor = static_cast<Kit*>(instrumentToReplace);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);
	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);
	// make sure the drum isn't currently in use
	noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);
	kitToLoadFor->drumsWithRenderingActive.erase(soundDrumToReplace);
	kitToLoadFor->removeDrum(soundDrumToReplace);

	// these have to get cleared, otherwise we keep creating drums that aren't attached to note rows
	if (soundDrumToReplace != nullptr) {
		currentSong->deleteBackedUpParamManagersForModControllable(soundDrumToReplace);
		soundDrumToReplace->wontBeRenderedForAWhile();
		void* toDealloc = static_cast<void*>(soundDrumToReplace);
		soundDrumToReplace->~SoundDrum();
		deluge::memory::dealloc(toDealloc);
	}
	soundDrumToReplace = newDrum;

	// The name is the real on-card name (display-agnostic), taken from the snapshot, so it can't pick up whatever the
	// user has since scrolled onto.
	soundDrumToReplace->drumName = request.target.name;
	soundDrumToReplace->path = request.target.dirPath.c_str();
	Error error = Error::NONE;
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
	return error;
}

// Previously called "exitAndResetInstrumentToInitial()". Does just that.
void LoadInstrumentPresetUI::exitAction() {
	// A load still out would swap in behind the revert.
	deluge::gui::instrumentLoad.cancel();
	revertToInitialPreset();
	LoadUI::exitAction();
}

ActionResult LoadInstrumentPresetUI::padAction(int32_t x, int32_t y, int32_t on) {
	// Refused while a commit is in flight: InstrumentLoad closes the browser's gate until it ends.
	if (deluge::gui::instrumentLoad.commitInFlight()) {
		return ActionResult::DEALT_WITH;
	}

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
			if (isSDRoutineActive()) {
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
	// Refused while a commit is in flight: InstrumentLoad closes the browser's gate until it ends.
	if (deluge::gui::instrumentLoad.commitInFlight()) {
		return ActionResult::DEALT_WITH;
	}
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

// Caller must set currentDir before calling this.
// Caller must call emptyFileItems() at some point after calling this function.
// song may be supplied as NULL, in which case it won't be searched for Instruments; sometimes this will get called when
// the currentSong is not set up.
std::expected<FileItem*, Error>
LoadInstrumentPresetUI::findAnUnlaunchedPresetIncludingWithinSubfolders(Song* song, OutputType outputType,
                                                                        Availability availabilityRequirement) {
	// Rung 5: this reader recurses over subfolders with backtracking, so its walk cannot be split
	// across a deferral and must run start-to-finish on the storage owner. The started() term keeps
	// cooperative and host builds, which never start an owner, out of the freeze.
	//
	// FREEZE_WITH_ERROR, not assert(): a bare assert() is compiled out of the shipping build, and on
	// the Rust BSP abort() ends the process with nothing on the display. This is a tier invariant
	// whose violation corrupts Browser::fileItems across two tiers, so it must be visible on the
	// device it breaks.
	if (ALPHA_OR_BETA_VERSION && !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started()) {
		FREEZE_WITH_ERROR("TIR1");
	}

	AudioEngine::logAction("findAnUnlaunchedPresetIncludingWithinSubfolders");
	allowedFileExtensions = allowedFileExtensionsXML;

	int32_t initialDirLength = currentDir.size();

	int32_t folderIndex = -1;
	bool doingSubfolders = false;
	std::string searchNameLocalCopy;

goAgain:

	Error error = readFileItemsFromFolderAndMemory(song, outputType, getThingName(outputType),
	                                               searchNameLocalCopy.c_str(), nullptr, true);
	if (error != Error::NONE) {
		emptyFileItems();
		return std::unexpected{error};
	}

	sortFileItems();

	// If that folder-read gave us no files, that's gotta mean we got to the end of the folder.
	if (!static_cast<int32_t>(fileItems.size())) {

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
	std::string lastFileItemDisplayNameBeforeFiltering;
	auto* rightmostFileItemBeforeFiltering = &fileItems[fileItems.size() - 1];
	lastFileItemDisplayNameBeforeFiltering = rightmostFileItemBeforeFiltering->displayName();

	deleteFolderAndDuplicateItems(availabilityRequirement);

	// If we're still looking for preset / XML files, and not subfolders yet...
	if (!doingSubfolders) {

		// Look through our list of FileItems, for a preset.
		for (int32_t i = 0; i < static_cast<int32_t>(fileItems.size()); i++) {
			auto* fileItem = &fileItems[i];
			if (!fileItem->isFolder) {
				return fileItem; // We found a preset / file.
			}
		}

		// Ok, we found none. Should we do some more reading of the folder contents, to get more files, or are there no
		// more?
		if (numFileItemsDeletedAtEnd) {
			searchNameLocalCopy = lastFileItemDisplayNameBeforeFiltering; // Can't fail.
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
	for (i = 0; i < static_cast<int32_t>(fileItems.size()); i++) {
		fileItem = &fileItems[i];
		if (fileItem->isFolder) {
			goto doThisFolder;
		}
	}
	if (numFileItemsDeletedAtEnd) {
		searchNameLocalCopy = lastFileItemDisplayNameBeforeFiltering;
		goto goAgain;
	}
	else {
		return std::unexpected{Error::NO_FURTHER_FILES_THIS_DIRECTION};
	}

	if (false) {
doThisFolder:
		bool anyMoreForLater = numFileItemsDeletedAtEnd || (i < (static_cast<int32_t>(fileItems.size()) - 1));
		searchNameLocalCopy = fileItem->displayName();

		currentDir.append("/");
		currentDir.append(fileItem->filename);

		// Call self
		return D_TRY_CATCH(findAnUnlaunchedPresetIncludingWithinSubfolders(song, outputType, availabilityRequirement),
		                   error, {
			                   if (error == Error::NO_FURTHER_FILES_THIS_DIRECTION) {
				                   if (anyMoreForLater) {
					                   currentDir.resize(initialDirLength);
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
LoadInstrumentPresetUI::confirmPresetOrNextUnlaunchedOne(OutputType outputType, std::string* searchName,
                                                         Availability availabilityRequirement) {
	// Rung 5: calls findAnUnlaunchedPresetIncludingWithinSubfolders() below, so it inherits the same
	// can't-defer-mid-walk constraint and must never run on the interaction tier. Boot is the one
	// legal exception -- see the sibling check in findAnUnlaunchedPresetIncludingWithinSubfolders,
	// which also explains why this is a freeze rather than a bare assert().
	if (ALPHA_OR_BETA_VERSION && !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started()) {
		FREEZE_WITH_ERROR("TIR2");
	}

	std::string searchNameLocalCopy;
	searchNameLocalCopy = *searchName; // Can't fail.
	bool shouldJustGrabLeftmost = false;

	// This does *not* favour the currentDir, so you should exhaust all avenues before calling this.
	auto justGetAnyPreset = [&]() -> std::expected<FileItem*, Error> {
		currentDir = getInstrumentFolder(outputType);
		return findAnUnlaunchedPresetIncludingWithinSubfolders(currentSong, outputType, availabilityRequirement);
	};

doReadFiles:
	Error error =
	    readFileItemsFromFolderAndMemory(currentSong, outputType, getThingName(outputType), searchNameLocalCopy.c_str(),
	                                     nullptr, false, availabilityRequirement);

	AudioEngine::logAction("confirmPresetOrNextUnlaunchedOne");

	if (error == Error::FOLDER_DOESNT_EXIST) {
		justGetAnyPreset();
	}
	else if (error != Error::NONE) {
		return std::unexpected{error};
	}

	sortFileItems();
	if (!static_cast<int32_t>(fileItems.size())) {
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
	std::string lastFileItemDisplayNameBeforeFiltering;
	auto* rightmostFileItemBeforeFiltering = &fileItems[fileItems.size() - 1];
	lastFileItemDisplayNameBeforeFiltering = rightmostFileItemBeforeFiltering->displayName();

	deleteFolderAndDuplicateItems(availabilityRequirement);

	// If we've shot off the end of the list, that means our searched-for preset didn't exist or wasn't available, and
	// any subsequent ones which at first made it onto the (possibly truncated) list also weren't available.
	if (!static_cast<int32_t>(fileItems.size())) {
		if (numFileItemsDeletedAtEnd) { // Probably couldn'g happen anymore...
			// We have to read more FileItems, further to the right.
			searchNameLocalCopy = lastFileItemDisplayNameBeforeFiltering; // Can't fail.
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
				if (!static_cast<int32_t>(fileItems.size())) {
					return justGetAnyPreset();
				}
				// Otherwise, everything's fine and we can just take the first element.
			}
		}
	}
	return &fileItems[0];
}

/// Caller must call emptyFileItems() at some point after calling this function - unless an error is returned
/// Caller must remove OLED working animation after calling this too.
PresetNavigationResult LoadInstrumentPresetUI::doPresetNavigation(int32_t offset, Instrument* oldInstrument,
                                                                  OutputType outputType, const std::string& fromName,
                                                                  const std::string& fromDir,
                                                                  Availability availabilityRequirement) {
	// Rung 5: this reader loops over readFileItemsForFolder() with its own goto-based backtracking
	// (readAgain/moveAgain/reachedEnd below) to walk the catalog in either direction, so like its two
	// siblings its walk cannot be split across a deferral and must run start-to-finish on the storage
	// owner. Boot is the one legal exception -- see the sibling check in
	// findAnUnlaunchedPresetIncludingWithinSubfolders, which also explains why this is a freeze rather
	// than a bare assert().
	if (ALPHA_OR_BETA_VERSION && !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started()) {
		FREEZE_WITH_ERROR("TIR3");
	}

	AudioEngine::logAction("doPresetNavigation");

	currentDir = fromDir;

	PresetNavigationResult toReturn{};

	std::string oldNameString; // We only might use this later for temporary storage

	oldNameString = fromName;
	oldNameString.append(".XML");
	toReturn.error = Error::NONE;
	if (toReturn.error != Error::NONE) {
		return toReturn;
	}

readAgain:
	int32_t newCatalogSearchDirection = (offset >= 0) ? CATALOG_SEARCH_RIGHT : CATALOG_SEARCH_LEFT;
readAgainWithSameOffset:
	toReturn.error =
	    readFileItemsForFolder(getThingName(outputType), false, allowedFileExtensionsXML, oldNameString.c_str(),
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
	if (!static_cast<int32_t>(fileItems.size())) {
reachedEnd:
		// If we've reached one end, try going again from the far other end.
		if (!oldNameString.empty()) {
			oldNameString.clear();
			goto readAgainWithSameOffset;
		}
		else {
noErrorButGetOut:
			toReturn.error = Error::NO_ERROR_BUT_GET_OUT;
			emptyFileItems();
			return toReturn;
		}
	}
	else if (static_cast<int32_t>(fileItems.size()) == 1 && (&fileItems[0])->instrument == oldInstrument) {
		goto reachedEnd;
	}

	int32_t i = (offset >= 0) ? 0 : (static_cast<int32_t>(fileItems.size()) - 1);
	/*
	if (i >= static_cast<int32_t>(fileItems.size())) { // If not found *and* we'd be past the end of the list...
	    if (offset >= 0) i = 0;
	    else i = static_cast<int32_t>(fileItems.size()) - 1;
	    goto doneMoving;
	}
	else {
	    int32_t oldNameLength = strlen(oldNameChars);
	    FileItem* searchResultItem = &fileItems[i];
	    if (memcasecmp(oldNameChars, searchResultItem->displayName(), oldNameLength)) {
notFound:	if (offset < 0) {
	            i--;
	            if (i < 0) i += static_cast<int32_t>(fileItems.size());
	        }
	        goto doneMoving;
	    }
	    if (searchResultItem->filenameIncludesExtension) {
	        if (strrchr(searchResultItem->displayName(), '.') != &searchResultItem->displayName()[oldNameLength]) goto
notFound;
	    }
	    else {
	        if (searchResultItem->displayName()[oldNameLength] != 0) goto notFound;
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
				i = static_cast<int32_t>(fileItems.size()) - 1;
			}
		}
	}

	// Or if moved right off the end of the list...
	else if (i >= static_cast<int32_t>(fileItems.size())) {
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
	toReturn.fileItem = &fileItems[i];

	bool isAlreadyInSong = toReturn.fileItem->instrument && toReturn.fileItem->instrumentAlreadyInSong;
	// wrapped is here to prevent an infinite loop
	if (availabilityRequirement == Availability::INSTRUMENT_UNUSED && isAlreadyInSong && wrapped < 2) {
		goto moveAgain;
	}

	// The name it landed on is shown, and the preset loaded, by InstrumentLoad, once this walk has landed.
	toReturn.loadedFromFile = false;
	return toReturn;
}
