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
#include "model/edits/setting_edit.h"
#include "model/instrument/instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/mod_controllable/delay_type.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h"
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
			initialName = soundDrumToReplace->name();
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
	// existing preset, or skip any which aren't "available" (what ConfirmPreset does for the change-type).

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

			if (!soundDrumToReplace->name().empty()) {
				enteredText = soundDrumToReplace->name();
				searchFilename = soundDrumToReplace->name();
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
			// No copy of what the change replaced, for want of memory: outputTypeToLoad is simply reverted, and the
			// browser stays open.
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
	scrollLoadTicket_ = deluge::gui::instrumentLoad.request(
	    {.target = snapshotTarget(movementDirection), .completion = &onScrollLoaded});
}

ActionResult LoadInstrumentPresetUI::backButtonAction() {
	// BACK while the preset under the cursor is still loading drops that load, and any older scroll-load it was queued
	// to replace: the clip keeps what it had, and the browser stays where it is. Once nothing of the browser's is
	// loading, BACK goes up a folder or out, as in every browser.
	if (deluge::gui::instrumentLoad.cancel(scrollLoadTicket_) == deluge::gui::CancelResult::Cancelled) {
		return ActionResult::DEALT_WITH;
	}
	return LoadUI::backButtonAction();
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
		// goIntoFolder() begins a listing that graphicsRoutine() runs over later ticks; failure is handled by
		// the base Browser::onListingFailed() (displayError + close()) once the listing completes.
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
	deluge::gui::instrumentLoad.cancelBrowserLoads();
	LoadUI::close();
}

bool LoadInstrumentPresetUI::dispatchPresetAcquisition(const deluge::storage::PresetRequest& request) {
	return deluge::gui::instrumentLoad
	    .request({.kind = deluge::gui::InstrumentLoadRequest::Kind::Acquire, .preset = request})
	    .accepted();
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
	    && !isInstrumentInList(acquired.instrument, currentSong->live.media.first_output)
	    && !isInstrumentInList(acquired.instrument, currentSong->live.media.first_hibernating_instrument)) {
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
	case PresetConsumer::BlankSongPreset: {
		// The swap replaces the default synth, which the knobs still name if its clip was the one on screen: they move
		// on to the preset that replaced it.
		const std::optional<uint32_t> defaultSynth =
		    currentSong->blankPresetInstrument != nullptr ? currentSong->blankPresetInstrument->id() : std::nullopt;
		const auto* knobs = std::get_if<deluge::project::SoundFocus>(&view.knobFocus().owner);
		const bool knobsOnDefault = knobs != nullptr && defaultSynth
		                            && knobs->slot.kind == deluge::project::SlotAddress::Kind::OUTPUT
		                            && knobs->slot.id == *defaultSynth;
		if (currentSong->completeBlankPreset(acquired) && knobsOnDefault) {
			view.setActiveModControllableTimelineCounter(acquired.instrument->getActiveClip());
		}
		break;
	}
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
		// Same tail as ViewNewInstrument, plus the layout refresh keyboard_screen.cpp needs -- see
		// PresetConsumer::KeyboardNewInstrument's doc. Gated on the same "did it actually create" bool
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
	// The loaded preset's delays: types a file saved without worked out, reserves set, now it is in the song.
	deluge::model::settleLoadedDelays(*currentSong);
}

ActionResult LoadInstrumentPresetUI::buttonAction(deluge::hid::Button b, bool on) {
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
		return LoadUI::buttonAction(b, on);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult LoadInstrumentPresetUI::timerCallback() {
	// Refused while a commit is in flight: InstrumentLoad closes the browser's gate until it ends.
	if (deluge::gui::instrumentLoad.commitInFlight()) {
		return ActionResult::DEALT_WITH;
	}
	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {

		currentUIMode = UI_MODE_NONE;

		FileItem* currentFileItem = getCurrentFileItem();

		// Folders don't have a context menu
		if (!currentFileItem || currentFileItem->isFolder) {
			return ActionResult::DEALT_WITH;
		}

		// We want to open the context menu to choose to reload the original file for the currently selected
		// preset in some way. So first up, make sure there is a file at that path.
		//
		// The listing already answered this. `maybeExistsOnCard` is false only for an item built from an
		// unsaved in-memory instrument, which is exactly the case below -- so asking the card again would tell
		// us nothing the browser doesn't already know, and it would ask by BLOCKING: this runs from
		// uiTimerManager -> timerCallback() on the interaction tier, where a card wait is what this
		// ladder exists to stop.
		//
		// The same swap as `delete_file.cpp`'s, and as this file's own kit-row check below: `maybeExistsOnCard`
		// is a definite in-memory fact, not a "could not tell" guess.
		if (!currentFileItem->maybeExistsOnCard) {
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
		// Run this as a listing too, the same way opened() does. changingOutputType_
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
			// Refused - the listing never ran, so onBrowserOpened()/onListingFailed()
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

				// No fileExists() pre-flight: it would be a BLOCKING card open on a path the Revert below reads
				// anyway, and this runs from padAction() -> exitAction() on the interaction tier, where a card
				// wait is what this ladder exists to stop.
				//
				// It would not be load-bearing in any case: the file can vanish between the check and the load,
				// so InstrumentLoad has always had to handle a Revert whose file is gone. Asking first would only
				// make that handling look unnecessary.

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
		initialInstrument->setDefaultVelocity(target.instrumentToReplace->defaultVelocity);

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
	return isInstrumentInList(instrument, currentSong->live.media.first_output);
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

			int32_t i = listing.search(newNameChars).index;
			if (i >= listing.size()) {
				break;
			}

			FileItem* fileItem = &listing.item(i);
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
// The card-free prologue of every browser load: nothing to load, an existing or hibernating Instrument to reuse, or
// a preset to read from the card. It reads the request's snapshot, never the browser's live list, and
// instrumentToReplace live, as the load before it left it.
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
		    isInstrumentInList(target.existingInstrument, currentSong->live.media.first_hibernating_instrument);

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

// The tail of every browser load of a synth, kit or MIDI preset, in one pass, once the new Instrument has its
// samples.
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
		newInstrument->setDefaultVelocity(instrumentToReplace->defaultVelocity);

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
	if (!isInstrumentInList(instrumentToReplace, currentSong->live.media.first_output)
	    && !isInstrumentInList(instrumentToReplace, currentSong->live.media.first_hibernating_instrument)) {
		for (int32_t f = listing.size() - 1; f >= 0; f--) {
			FileItem* fileItem = &listing.item(f);
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
	// The loaded preset's delays: types a file saved without worked out, reserves set, now it is in the song.
	deluge::model::settleLoadedDelays(*currentSong);
	display->removeWorkingAnimation();
	return Error::NONE;
}

// The tail of a synth preset loaded into a kit row, in one pass, around the drum's load: the old drum is taken out
// only once the new one is ready.
Error LoadInstrumentPresetUI::swapKitRow(const deluge::gui::InstrumentLoadRequest& request, SoundDrum* newDrum) {
	Kit* kitToLoadFor = static_cast<Kit*>(instrumentToReplace);
	SoundDrum* oldDrum = soundDrumToReplace;

	newDrum->path = request.target.dirPath.c_str();

	// Steal the ParamManager that loading backed up, so its (now empty) backup entry is removed from the Song. Take it
	// out of the backups first - setDrum() backs up the old ParamManager, which can move the backups around
	ParamManagerForTimeline paramManager;
	if (currentSong->live.param_backup.getPreferablyWithClip(newDrum, instrumentClipToLoadFor, &paramManager)
	    == nullptr) {
		// The old drum stays where it was; the new one never reached the kit.
		currentSong->live.param_backup.deleteFor(newDrum);
		AudioEngine::retire(newDrum);
		return Error::FILE_CORRUPTED;
	}

	kitToLoadFor->addDrum(newDrum);
	// The name lives in the DocDrum addDrum just bound. It is the real on-card name (display-agnostic), taken from the
	// snapshot, so it can't pick up whatever the user has since scrolled onto.
	deluge::edits::laneSettingFor(*newDrum, "name",
	                              deluge::serialization::Value::makeScalar(std::string{request.target.name}));

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClipToLoadFor);
	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);
	noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);
	// Back up the old ParamManager so it's freed along with the old drum below, rather than leaked
	noteRow->setDrum(newDrum, kitToLoadFor, modelStackWithNoteRow, instrumentClipToLoadFor, &paramManager, true);

	// The same Drum is shared by NoteRows in every Clip of this Kit - move all of them onto the new drum, otherwise
	// they'd be left pointing at the old one once it's retired. They clone their ParamManager from the row above.
	if (oldDrum != nullptr) {
		for (InstrumentClip* clip : InstrumentClips::everywhere(currentSong)) {
			if (clip->output != kitToLoadFor) {
				continue;
			}
			for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
				NoteRow* thisNoteRow = &clip->noteRows[i];
				if (thisNoteRow->drum != oldDrum) {
					continue;
				}
				char otherModelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithNoteRow* otherModelStackWithNoteRow =
				    setupModelStackWithTimelineCounter(otherModelStackMemory, currentSong, clip)
				        ->addNoteRow(clip->getNoteRowId(thisNoteRow, i), thisNoteRow);
				thisNoteRow->stopCurrentlyPlayingNote(otherModelStackWithNoteRow);
				thisNoteRow->setDrum(newDrum, kitToLoadFor, otherModelStackWithNoteRow, instrumentClipToLoadFor,
				                     nullptr, true);
			}
		}

		// Nothing references the old drum now, so it can go
		kitToLoadFor->renderingDrums.remove(oldDrum);
		kitToLoadFor->removeDrum(oldDrum);
		currentSong->live.param_backup.deleteFor(oldDrum);
		oldDrum->wontBeRenderedForAWhile();
		// Out of the kit for good: the render's let-go of the drum must not write back into the kit's
		// renderingDrums, which may be freed by then (SoundDrum::setSkippingRendering skips a null kit).
		// Erase again first: a sequencer note-on between the first erase and here re-inserts the drum. The render
		// will own this set in later work.
		kitToLoadFor->renderingDrums.remove(oldDrum);
		oldDrum->kit = nullptr;
		AudioEngine::retire(oldDrum);
	}

	soundDrumToReplace = newDrum;
	kitToLoadFor->selectedDrum = newDrum;
	kitToLoadFor->beenEdited();
	// The loaded preset's delays: types a file saved without worked out, reserves set, now it is in the song.
	deluge::model::settleLoadedDelays(*currentSong);
	// The view may still have the old drum cached as its active ModControllable, and gold knobs turned while this
	// browser is open go straight to it
	view.setActiveModControllableTimelineCounter(instrumentClipToLoadFor);
	return Error::NONE;
}

// Previously called "exitAndResetInstrumentToInitial()". Does just that.
void LoadInstrumentPresetUI::exitAction() {
	// A load still out would swap in behind the revert.
	deluge::gui::instrumentLoad.cancelBrowserLoads();
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
			exitAction();
		}
	}

	else {
		return LoadUI::padAction(x, y, on);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult LoadInstrumentPresetUI::verticalEncoderAction(int32_t offset) {
	// Refused while a commit is in flight: InstrumentLoad closes the browser's gate until it ends.
	if (deluge::gui::instrumentLoad.commitInFlight()) {
		return ActionResult::DEALT_WITH;
	}
	if (Buttons::isShiftButtonPressed()) {
		LoadUI::verticalEncoderAction(offset);
	}
	if (showingAuditionPads()) {
		if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
			return ActionResult::DEALT_WITH;
		}

		ActionResult result = instrumentClipView.verticalEncoderAction(offset);

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
