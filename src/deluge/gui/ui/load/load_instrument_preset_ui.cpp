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
		performLoad();
	}
	else {
		// opened()'s post-listing tail: focusRegained() runs here, after the listing actually
		// completes, matching the Save* browsers' convention.
		focusRegained();
	}
}

void LoadInstrumentPresetUI::onListingFailed(Error error) {
	if (changingOutputType_) {
		// changeOutputType()'s original synchronous path silently reverted outputTypeToLoad on failure
		// and left the browser open - no displayError(), no close(). Preserve that instead of falling
		// through to the base Browser::onListingFailed() (displayError + close()).
		changingOutputType_ = false;
		outputTypeToLoad = outputTypeBeforeChange_;
		return;
	}
	Browser::onListingFailed(error);
}

void LoadInstrumentPresetUI::folderContentsReady(int32_t entryDirection) {
	currentFileChanged(0);
}

// Coalesces the scroll-triggered load onto the storage worker instead of running it synchronously:
// this fires on every non-reload encoder tick while scrolling Load Synth/Kit, and a synchronous load
// would block the executor for the whole load. LatestWins collapses a fast scroll onto the settled
// preset - see runScrollLoadOp().
//
// The LoadTarget snapshot is built here, at dispatch time, from live Browser state - the ONLY point
// where it's safe to read enteredText/currentDir/getCurrentFileItem() directly, because nothing else
// runs between this tick and the snapshot being taken. Once dispatched, runScrollLoadOp()/performLoad()
// must never go back to that live state (see LoadTarget's doc): doing so would race a scroll on the UI
// task against the op's own SD-yield points, freeing state the op still holds a raw pointer to.
void LoadInstrumentPresetUI::currentFileChanged(int32_t movementDirection) {
	FileItem* currentFileItem = getCurrentFileItem();
	LoadTarget target{
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
	if (loadCoalescer_.request(target)) {
		if (!deluge::storage::Owner::run(&LoadInstrumentPresetUI::runScrollLoadOp, this)) {
			// Owner queue was full - the op never ran, so release the guard or the coalescer
			// would wedge single-flight forever (see LatestWins::reset()'s doc).
			loadCoalescer_.reset();
		}
	}
}

void LoadInstrumentPresetUI::runScrollLoadOp(void* self) {
	auto* ui = static_cast<LoadInstrumentPresetUI*>(self);
	const LoadTarget& target = ui->loadCoalescer_.current();

	// Pass the snapshot through explicitly - performLoad()/performLoadSynthToKit() must act ONLY on
	// `target` (and op-local copies derived from it) for anything that crosses their internal SD-yield
	// points, never on live Browser members, since a scroll on the UI task can run and mutate/free
	// those while this op is yielded (see LoadTarget's doc).
	ui->currentInstrumentLoadError =
	    target.loadingSynthToKitRow ? ui->performLoadSynthToKit(&target) : ui->performLoad(false, &target);
	if (ui->currentInstrumentLoadError != Error::NONE) {
		display->displayError(ui->currentInstrumentLoadError);
	}

	// If a newer scroll arrived while this ran, dispatch it (latest-wins).
	if (auto next = ui->loadCoalescer_.complete(); next.has_value()) {
		if (!deluge::storage::Owner::run(&LoadInstrumentPresetUI::runScrollLoadOp, self)) {
			ui->loadCoalescer_.reset(); // re-dispatch dropped - release (see currentFileChanged())
		}
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
		// Dispatch the commit onto the storage worker: it does its OWN authoritative
		// performLoad()/performLoadSynthToKit() rather than trusting currentInstrumentLoadError,
		// because under coalescing a scroll-load may still be in flight when SELECT_ENC arrives.
		// (A cache hit - getAudioFileFromFilename - if the scroll already warmed it; a fresh load
		// otherwise.) See runCommitOp() for the commit tail.
		deluge::storage::Owner::run_or_inline(&LoadInstrumentPresetUI::runCommitOp, this);
	}
}

void LoadInstrumentPresetUI::runCommitOp(void* self) {
	auto* ui = static_cast<LoadInstrumentPresetUI*>(self);

	ui->currentInstrumentLoadError = ui->loadingSynthToKitRow ? ui->performLoadSynthToKit() : ui->performLoad();
	if (ui->currentInstrumentLoadError != Error::NONE) {
		display->displayError(ui->currentInstrumentLoadError);
		return;
	}

	if (ui->outputTypeToLoad == OutputType::KIT && ui->showingAuditionPads()) {
		// New NoteRows have probably been created, whose colours haven't been grabbed yet.
		instrumentClipView.recalculateColours();
	}

	ui->close();
}

bool LoadInstrumentPresetUI::dispatchPresetAcquisition(const deluge::storage::PresetRequest& request) {
	if (!presetCoalescer_.request(request)) {
		return true; // superseded an in-flight op; that op will pick this up via complete()
	}
	if (!deluge::storage::Owner::run_or_inline(&LoadInstrumentPresetUI::runPresetAcquisitionOp, this)) {
		// Owner queue was full - the op never ran, so release the guard or the coalescer would
		// wedge single-flight forever (see LatestWins::reset()'s doc, and currentFileChanged()).
		presetCoalescer_.reset();
		return false;
	}
	return true;
}

void LoadInstrumentPresetUI::runPresetAcquisitionOp(void* self) {
	auto* ui = static_cast<LoadInstrumentPresetUI*>(self);
	// Counts ops that actually RUN, not dispatch attempts: a superseded request() (dispatchPresetAcquisition
	// returning early) or a dropped run_or_inline()/run() never reach here, so this must live here and
	// not in dispatchPresetAcquisition() for presetAcquisitionsRun() to measure what its callers need
	// (the Coalesce/NonAudio checks in the song-preview A/B harness).
	ui->presetAcquisitionsRun_++;
	// By value, not by reference: complete() below overwrites presetCoalescer_'s current_ in place,
	// so a reference into it would dangle once complete() runs while runAcquisitionFollowUp() still
	// needs `request` (contrast runScrollLoadOp(), which is done with its target before calling
	// complete() and so can take a reference).
	const deluge::storage::PresetRequest request = ui->presetCoalescer_.current();

	// currentDir comes from the snapshot, never from whatever the UI task left in the static:
	// doPresetNavigation writes it from oldInstrument->dirPath on entry, and the readers mutate it
	// as they descend (spec section 6).
	Browser::currentDir = request.dirPath;

	ui->runAcquisitionFollowUp(request);

	// If a newer gesture arrived while this ran, dispatch it (latest-wins).
	if (auto next = ui->presetCoalescer_.complete(); next.has_value()) {
		if (!deluge::storage::Owner::run(&LoadInstrumentPresetUI::runPresetAcquisitionOp, self)) {
			ui->presetCoalescer_.reset(); // re-dispatch dropped - release
		}
	}
}

// Runs the follow-up for whichever gesture dispatched this acquisition. Every arm runs ON THE
// OWNER, which is what makes a blocking read inside one legal, and mutating UI state from here
// matches the established pattern: runScrollLoadOp() calls displayError() and runCommitOp() calls
// recalculateColours()/close(), both on the owner. The arranger's three arms are wired (Task 3)
// and View's two are wired (Task 4); the Session arms remain empty `break;`s until Task 5.
void LoadInstrumentPresetUI::runAcquisitionFollowUp(const deluge::storage::PresetRequest& request) {
	using deluge::storage::PresetConsumer;
	// A coalesced request can carry a stale `oldInstrument` -- see isRequestInstrumentStillLive()'s
	// doc. Consumers that never set it (ArrangerNewInstrument) leave it null, so the `!= nullptr`
	// guard skips this check for them rather than abandoning every follow-up that doesn't use it.
	if (request.oldInstrument != nullptr && !isRequestInstrumentStillLive(request.oldInstrument)) {
		return;
	}
	switch (request.consumer) {
	case PresetConsumer::ArrangerNewInstrument:
		arrangerView.completeNewInstrument(request.yPressed);
		break;
	case PresetConsumer::ArrangerChangeType:
		arrangerView.completeChangeOutputType(request.outputType, request.oldInstrument, request.yPressed);
		break;
	case PresetConsumer::ArrangerNavigate:
		arrangerView.completeNavigateThroughPresets(request.offset, request.oldInstrument, request.yPressed);
		break;
	case PresetConsumer::ViewChangeType:
		(void)view.completeChangeOutputType(request.outputType, request.clip, request.doBlink);
		break;
	case PresetConsumer::ViewNavigateClipPreset:
		view.completeNavigateThroughPresetsForInstrumentClip(request.offset, request.clip, request.doBlink);
		break;
	case PresetConsumer::SessionChangeType:
	case PresetConsumer::SessionNavigate:
	case PresetConsumer::SessionNewTrack:
		break; // wired in the consumer tasks
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

				Error error = StorageManager::loadInstrumentFromFile(currentSong, instrumentClipToLoadFor,
				                                                     initialOutputType, false, &initialInstrument,
				                                                     filePath.c_str(), &initialName, &initialDirPath);
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
// `snapshot`, when non-null, is a LoadTarget recorded at dispatch time (see currentFileChanged()):
// every read below that would otherwise touch live Browser state (getCurrentFileItem(), enteredText,
// currentDir) instead comes from op-local copies taken from `snapshot`, so nothing here aliases
// Browser::fileItems or a live member across loadInstrumentFromFile()'s internal SD-yield points.
// `snapshot` is null on the live-state call sites: onBrowserOpened()'s changeOutputType() tail,
// runCommitOp(), and the clone context-menu action.
Error LoadInstrumentPresetUI::performLoad(bool doClone, const LoadTarget* snapshot) {

	// currentFileItem is only ever touched here, before the yielding load call below - never held
	// across it (see LoadTarget's doc for why that matters).
	FileItem* currentFileItem = snapshot != nullptr ? nullptr : getCurrentFileItem();
	bool hasFile;
	bool fileIsFolder;
	Instrument* fileExistingInstrument;
	std::string filePath;
	std::string fileName;
	std::string fileDirPath;

	if (snapshot != nullptr) {
		hasFile = snapshot->hasFile;
		fileIsFolder = snapshot->isFolder;
		fileExistingInstrument = snapshot->existingInstrument;
		filePath = snapshot->path;
		fileName = snapshot->name;
		fileDirPath = snapshot->dirPath;
	}
	else {
		hasFile = currentFileItem != nullptr;
		if (hasFile) {
			fileIsFolder = currentFileItem->isFolder;
			fileExistingInstrument = currentFileItem->instrument;
			filePath = getCurrentFilePath();
		}
		fileName = enteredText;
		fileDirPath = currentDir;
	}

	if (!hasFile) {
		// Make it say "NONE" on numeric Deluge, for
		// consistency with old times.
		return Error::FILE_NOT_FOUND;
	}

	if (fileIsFolder) {
		return Error::NONE;
	}
	if (fileExistingInstrument == instrumentToReplace && !doClone) {
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

	Instrument* newInstrument = fileExistingInstrument;

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

		std::string clonedName;

		if (doClone) {
			bool success = findUnusedSlotVariation(&fileName, &clonedName);
			if (!success) {
				return Error::UNSPECIFIED;
			}
		}
		Error error;
		// check if the file pointer matches the current file item
		// Browser::checkFP();

		// synth or kit - fileName/fileDirPath are op-local copies (see top of function), so the
		// yielding call below can't have its output params raced by a concurrent scroll mutating
		// enteredText/currentDir.
		error = StorageManager::loadInstrumentFromFile(currentSong, instrumentClipToLoadFor, outputTypeToLoad, false,
		                                               &newInstrument, filePath.c_str(), &fileName, &fileDirPath);

		if (error != Error::NONE) {
			return error;
		}

		shouldReplaceWholeInstrument = oldInstrumentShouldBeReplaced;
		needToAddInstrumentToSong = true;
		loadedFromFile = true;

		if (doClone) {
			newInstrument->name = clonedName;
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
		for (int32_t f = static_cast<int32_t>(fileItems.size()) - 1; f >= 0; f--) {
			FileItem* fileItem = &fileItems[f];
			if (fileItem->instrument == instrumentToReplace) {
				fileItem->instrument = nullptr;
				break;
			}
		}
	}

	// Cache the loaded Instrument on its FileItem so navigating back to it is a cache hit instead of
	// a reload. currentFileItem is only non-null on the live (non-snapshot) path, where it was taken
	// at the very top of this call, before the yielding load above, so this write can't race a
	// listing rebuild. On the snapshot path there is deliberately no live FileItem* to write through
	// here (that pointer is exactly what could have been freed by a scroll-triggered listing rebuild
	// during the yield) - this is a perf-only cache warm, so skipping it is safe: a future re-list
	// re-associates this Instrument with its FileItem via the normal Song-instrument scan, and the
	// commit op (runCommitOp) always does its own authoritative load regardless.
	if (currentFileItem != nullptr) {
		currentFileItem->instrument = newInstrument;
	}
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
			// TODO(conflation-batch-4): unknown is being treated as absent here; the
			// load/browse/favourites batch gives this site its real ruling.
			bool fileExists =
			    StorageManager::fileExists(midiInstrument->deviceDefinitionFileName.c_str()).value_or(false);
			if (fileExists) {
				StorageManager::loadMidiDeviceDefinitionFile(midiInstrument,
				                                             midiInstrument->deviceDefinitionFileName.c_str(),
				                                             &midiInstrument->deviceDefinitionFileName, false);
			}
		}
	}

	return Error::NONE;
}

// `snapshot`, when non-null, is a LoadTarget recorded at dispatch time (see currentFileChanged()) -
// see performLoad()'s doc comment for why every live-state read below is instead taken from op-local
// copies derived from it when present.
Error LoadInstrumentPresetUI::performLoadSynthToKit(const LoadTarget* snapshot) {
	Kit* kitToLoadFor = static_cast<Kit*>(instrumentToReplace);
	bool hasFile;
	bool fileIsFolder;
	bool fileMaybeExistsOnCard;
	std::string filePath;
	std::string fileName;
	std::string fileDirPath;

	if (snapshot != nullptr) {
		hasFile = snapshot->hasFile;
		fileIsFolder = snapshot->isFolder;
		fileMaybeExistsOnCard = snapshot->maybeExistsOnCard;
		filePath = snapshot->path;
		fileName = snapshot->name;
		fileDirPath = snapshot->dirPath;
	}
	else {
		FileItem* currentFileItem = getCurrentFileItem();
		hasFile = currentFileItem != nullptr;
		if (hasFile) {
			fileIsFolder = currentFileItem->isFolder;
			fileMaybeExistsOnCard = currentFileItem->maybeExistsOnCard;
			filePath = getCurrentFilePath();
		}
		fileName = enteredText;
		fileDirPath = currentDir;
	}

	if (!hasFile) {
		// Make it say "NONE" on numeric Deluge, for consistency with old times.
		return Error::FILE_NOT_FOUND;
	}

	if (fileIsFolder) {
		return Error::NONE;
	}

	// An unsaved (in-memory only) synth preset cannot be loaded into a kit row because
	// loadSynthToDrum() reads XML from disk to create a SoundDrum. If the preset hasn't
	// been saved yet, there is no file on the SD card to read from.
	if (!fileMaybeExistsOnCard) {
		return Error::FILE_NOT_SAVED;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);
	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);
	// make sure the drum isn't currently in use
	noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);
	kitToLoadFor->drumsWithRenderingActive.erase(soundDrumToReplace);
	kitToLoadFor->removeDrum(soundDrumToReplace);

	// swaps out the drum pointed to by soundDrumToReplace. fileName/fileDirPath are op-local copies
	// (see above) - loadSynthToDrum() doesn't actually read them (they're dead params below the SD
	// yield in there today), but pass the snapshot copies regardless so this call never aliases a
	// live Browser member across the yield, matching performLoad().
	Error error = StorageManager::loadSynthToDrum(currentSong, instrumentClipToLoadFor, false, &soundDrumToReplace,
	                                              filePath.c_str(), &fileName, &fileDirPath);
	if (error != Error::NONE) {
		return error;
	}
	// kitToLoadFor->addDrum(soundDrumToReplace);
	display->displayLoadingAnimationText("Loading", false, true);
	soundDrumToReplace->loadAllSamples(true);

	// fileName is the real on-card name now (display-agnostic), so it needs no reassembling. Taken
	// from the snapshot when running as the scroll-load op, so this can't pick up whatever the user
	// has since scrolled onto.
	soundDrumToReplace->drumName = fileName;
	soundDrumToReplace->path = fileDirPath.c_str();
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
                                                                  Availability availabilityRequirement, bool doBlink) {

	AudioEngine::logAction("doPresetNavigation");

	currentDir = oldInstrument->dirPath;
	OutputType outputType = oldInstrument->type;

	PresetNavigationResult toReturn{};

	std::string oldNameString; // We only might use this later for temporary storage
	std::string newName;

	oldNameString = oldInstrument->name;
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

	toReturn.loadedFromFile = false;
	bool isHibernating = toReturn.fileItem->instrument && !toReturn.fileItem->instrumentAlreadyInSong;

	if (toReturn.fileItem->instrument) {
		view.displayOutputName(toReturn.fileItem->instrument, doBlink);
	}
	else {
		newName = toReturn.fileItem->getFilenameWithoutExtension();
		oldNameString = toReturn.fileItem->displayName();
		toReturn.error = Error::NONE;
		if (toReturn.error != Error::NONE) {
			emptyFileItems();
			return toReturn;
		}
		view.drawOutputNameFromDetails(outputType, 0, 0, newName.c_str(), newName.empty(), false, doBlink);
	}

	deluge::hid::display::OLED::sendMainImage(); // Sorta cheating - bypassing the UI layered renderer.

	if (encoders::select.pending()) {
		D_PRINTLN("go again 1 --------------------------");

doPendingPresetNavigation:
		offset = encoders::select.take();

		if (toReturn.loadedFromFile) {
			currentSong->deleteOutput(toReturn.fileItem->instrument);
			toReturn.fileItem->instrument = nullptr;
		}
		goto moveAgain;
	}

	// Unlike in ClipMinder, there's no need to check whether we came back to the same Instrument, cos we've specified
	// that we were looking for "unused" ones only
	// TODO: This isn't true, it's an argument so that must have changed at some point. This logic will create a clone
	// if anything other than unused is passed in
	if (!toReturn.fileItem->instrument) {
		std::string filePath = Browser::currentDir + "/" + toReturn.fileItem->getFilenameWithExtension();
		toReturn.error = StorageManager::loadInstrumentFromFile(currentSong, nullptr, outputType, false,
		                                                        &toReturn.fileItem->instrument, filePath.c_str(),
		                                                        &newName, &Browser::currentDir);
		if (toReturn.error != Error::NONE) {
			emptyFileItems();
			return toReturn;
		}

		toReturn.loadedFromFile = true;

		if (encoders::select.pending()) {
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
	if (encoders::select.pending()) {
		D_PRINTLN("go again 3 --------------------------");
		goto doPendingPresetNavigation;
	}

	if (isHibernating) {
		currentSong->removeInstrumentFromHibernationList(toReturn.fileItem->instrument);
	}

	return toReturn;
}
