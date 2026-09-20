/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "hid/button.h"
#include "io/file.hpp"
#include "model/sample/sample.h"
#include "util/try.h"
#include <iterator>
#include <optional>
#include <ranges>
#include <utility>
#undef __GNU_VISIBLE
#define __GNU_VISIBLE 1 // Makes strcasestr visible. Might already be the reason for the define above
#include "extern.h"
#include "gui/context_menu/sample_browser/kit.h"
#include "gui/context_menu/sample_browser/synth.h"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/slicer.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include "util/c_string.h"
#include "util/functions.h"
#include "util/string.h"
#include <cstring>

namespace params = deluge::modulation::params;
using namespace deluge;
using namespace gui;

SampleBrowser sampleBrowser{};

char const* allowedFileExtensionsAudio[] = {"WAV", "AIFF", "AIF", NULL};

SampleBrowser::SampleBrowser() {
	fileIcon = deluge::hid::display::OLED::waveIcon;
	title = "Audio files";
	shouldWrapFolderContents = false;
	qwertyAlwaysVisible = false;
	shouldInterpretNoteNamesForThisBrowser = true;
	qwertyCurrentlyDrawnOnscreen = false;
}

bool SampleBrowser::opened() {

	// Nothing carried over from a previous visit: a preview still loading when the browser closed
	// names a file in whatever folder was open then, and graphicsRoutine() starts running again the
	// moment this UI is current -- before folderContentsReady() has picked the file for THIS visit.
	cancelPreview();
	// Nor a folder import: one still out would build into whatever this visit is choosing for.
	folderImport.cancel();

	bool success = Browser::opened();
	if (!success) {
		return false;
	}

	qwertyAlwaysVisible = false;

	favouritesManager.setCategory("SAMPLES");
	favouritesChanged();
	actionLogger.deleteAllLogs();

	allowedFileExtensions = allowedFileExtensionsAudio;
	allowFoldersSharingNameWithFile = true;
	outputTypeToLoad = OutputType::NONE;
	qwertyVisible = false;
	qwertyCurrentlyDrawnOnscreen = false;

	currentlyShowingSamplePreview = false;

	autoLoadEnabled = false;

	fileIndexSelected = 0;

	if (currentUIMode == UI_MODE_AUDITIONING) {
		instrumentClipView.cancelAllAuditioning();
	}

	// No inline initSD() pre-flight — see the comment in SlotBrowser::beginSlotSession(). This runs on
	// the interaction tier, where an off-owner initSD() reports Error::SD_CARD for "ask me from the
	// owner", failing the browser on a healthy card. The dispatched listing checks the card on the
	// owner and reports real failures via Browser::onListingFailed().

	std::string currentPath;
	currentPath = soundEditor.getCurrentAudioFileHolder()->filePath;

	char const* searchFilename;

	// If currentPath is blank, or is somewhere outside of the SAMPLES folder, then default to previously manually
	// loaded sample
	if (currentPath.empty() || memcasecmp(currentPath.c_str(), "SAMPLES/", 8)) {
		currentPath = lastFilePathLoaded;

		// If that's blank too, then default to SAMPLES folder.
		if (currentPath.empty()) {
			currentDir = "SAMPLES";
			searchFilename = NULL;
			goto dissectionDone;
		}
	}

	{
		// Must dissect
		char const* currentPathChars = currentPath.c_str();
		char const* slashAddress = strrchr(currentPathChars, '/');
		if (!slashAddress) {
			searchFilename = currentPathChars;
			currentDir.clear();
		}
		else {
			int32_t slashPos = (uintptr_t)slashAddress - (uintptr_t)currentPathChars;
			searchFilename = &currentPathChars[slashPos + 1];

			currentDir = currentPathChars;
			currentDir.resize(slashPos);
		}
	}

dissectionDone:

	// The listing (and its post-listing tail, in onBrowserOpened()) happens async: dispatch it and
	// return optimistically. Failure goes through the base Browser::onListingFailed() (displayError
	// + close(), not goBackToSoundEditor(), which would left-scroll) once the listing completes.
	beginListing({.action = ListingAction::Open,
	              .direction = 1,
	              .filenameToStartAt = searchFilename ? searchFilename : "",
	              .defaultDir = "SAMPLES"});

	return true;
}

void SampleBrowser::onBrowserOpened() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, getCurrentOutputType() == OutputType::SYNTH);
	indicator_leds::setLedState(IndicatorLED::KIT, soundEditor.editingKit());

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	// soundEditor.setupShortcutBlink(soundEditor.currentSourceIndex, 5, 0);

	if (currentUIMode == UI_MODE_AUDITIONING) {
		instrumentClipView.cancelAllAuditioning();
	}

	possiblySetUpBlinking();
}

void SampleBrowser::possiblySetUpBlinking() {

	if (!qwertyVisible && !currentlyShowingSamplePreview) {
		int32_t x = 0;
		if (getCurrentClip()->type == ClipType::INSTRUMENT) {
			x = soundEditor.currentSourceIndex;
		}
		soundEditor.setupExclusiveShortcutBlink(x, 5);
	}
}

void SampleBrowser::focusRegained() {
	// displayCurrentFilename();
	indicator_leds::setLedState(IndicatorLED::SAVE, false); // In case returning from delete-file context menu
}

void SampleBrowser::folderContentsReady(int32_t entryDirection) {

	// If just one file, there's no prefix.
	if (listing.size() <= 1) {
		numCharsInPrefix = 0;
	}

	else {
		numCharsInPrefix = 65535;
		FileItem* currentFileItem = getCurrentFileItem();

		char const* currentFilenameChars = currentFileItem->filename.c_str();

		for (int32_t f = 0; numCharsInPrefix && f < listing.size(); f++) {
			FileItem* fileItem = &listing.item(f);

			for (int32_t i = 0; i < numCharsInPrefix; i++) {
				char const* thisFileName = fileItem->filename.c_str();
				if (!thisFileName[i] || thisFileName[i] != currentFilenameChars[i]) {
					numCharsInPrefix = i;
					break;
				}
			}
		}
	}

	previewIfPossible(entryDirection);
}

void SampleBrowser::currentFileChanged(int32_t movementDirection) {

	// Can start scrolling right now, while next preview loads
	if (movementDirection && (currentlyShowingSamplePreview || qwertyVisible) && !qwertyAlwaysVisible) {
		qwertyVisible = false;

		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);

		memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
		PadLEDs::horizontal::setupScroll(movementDirection, kDisplayWidth, true);
		currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
	}

	AudioEngine::stopAnyPreviewing();

	previewIfPossible(movementDirection);
}

void SampleBrowser::exitAndNeverDeleteDrum() {
	cancelPreview();
	close();
}

// Will "delete drum if possible".
void SampleBrowser::exitAction() {
	cancelPreview();

	UI* redrawUI = nullptr;

	if (!isUIOpen(&soundEditor)) {
		// If no file was selected, the user wanted to get out of creating this Drum.
		// Only if some unassigned Drums
		if (soundEditor.editingKit() && getCurrentKit()->getFirstUnassignedDrum(getCurrentInstrumentClip())
		    && soundEditor.getCurrentAudioFileHolder()->filePath.empty()) {
			instrumentClipView.deleteDrum((SoundDrum*)soundEditor.currentSound);
			redrawUI = &instrumentClipView;
		}
	}

	Browser::exitAction();

	if (redrawUI) {
		uiNeedsRendering(redrawUI);
	}
}

ActionResult SampleBrowser::timerCallback() {
	// Refused while an explicit claim or a folder import is in flight: each closes the browser's gate until it ends.
	if (sampleClaim.explicitInFlight() || folderImport.inFlight()) {
		return ActionResult::DEALT_WITH;
	}

	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {
		currentUIMode = UI_MODE_NONE;
		if (fileIndexSelected >= 0) {

			char const* errorMessage;
			gui::ContextMenu* contextMenu;

			// AudioClip
			if (getCurrentClip()->type == ClipType::AUDIO) {
				display->displayPopup(
				    deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_IMPORT_WHOLE_FOLDER_INTO_AUDIO_CLIP));
			}

			// Kit
			else if (soundEditor.editingKit()) {

				if (canImportWholeKit()) {
					contextMenu = &gui::context_menu::sample_browser::kit;
					goto considerContextMenu;
				}
				else {
					display->displayPopup(deluge::l10n::get(
					    deluge::l10n::String::STRING_FOR_CAN_ONLY_IMPORT_WHOLE_FOLDER_INTO_BRAND_NEW_KIT));
				}
			}

			// Synth
			else {
				contextMenu = &context_menu::sample_browser::synth;

considerContextMenu:
				bool available = contextMenu->setupAndCheckAvailability();

				if (available) { // Not sure if this can currently fail.
					openUI(contextMenu);
				}
				else {
					exitUIMode(UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS);
				}
			}
		}
		return ActionResult::DEALT_WITH;
	}
	else {
		return Browser::timerCallback();
	}
}

void SampleBrowser::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();

	if (!currentFileItem) {
		display->displayError(Error::FILE_NOT_FOUND);
		return;
	}

	AudioEngine::stopAnyPreviewing();

	// If it's a directory...
	if (currentFileItem->isFolder) {

		// Don't allow user to go into TEMP clips folder
		if (deluge::string::caselessEquals(currentFileItem->filename, "TEMP")
		    && deluge::string::caselessEquals(currentDir, "SAMPLES/CLIPS")) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED));
			return;
		}

		// Extremely weirdly, if we try to just put this inside the parentheses in the next line,
		// it returns an empty string (&nothing). Surely this is a compiler error??
		char const* filenameChars = currentFileItem->filename.c_str();

		Error error = goIntoFolder(filenameChars);

		if (error != Error::NONE) {
			display->displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
		}
	}

	// Or if it's an audio file...
	else {

		// If we're here, we know that the file has fully loaded

		// If user wants to slice...
		if (soundEditor.editingKit() && Buttons::isShiftButtonPressed()) {

			// Can only do this for Kit Clips, and for source 0, not 1, AND there has to be only one drum present, which
			// is assigned to the first NoteRow
			if (canImportWholeKit()) {
				display->displayPopup("SLICER");
				openUI(&slicer);
			}
			else {
				display->displayPopup(
				    deluge::l10n::get(deluge::l10n::String::STRING_FOR_CAN_ONLY_USE_SLICER_FOR_BRAND_NEW_KIT));
			}
		}

		// Otherwise, load it normally
		else {
			// The claim runs on this tier, as SampleClaim's phases; only its card-reading steps go to the storage
			// owner.
			requestClaim(1, 1, 1);
		}
	}
}

void SampleBrowser::requestClaim(int32_t mayDoPitchDetection, int32_t mayDoSingleCycle, int32_t mayDoWaveTable,
                                 void (*completion)(bool success)) {
	sampleClaim.request({
	    .path = getCurrentFilePath(),
	    .enteredText = enteredText,
	    .mayDoPitchDetection = mayDoPitchDetection,
	    .mayDoSingleCycle = mayDoSingleCycle,
	    .mayDoWaveTable = mayDoWaveTable,
	    .loadWithoutExiting = false,
	    .origin = ClaimOrigin::Explicit,
	    .completion = completion,
	});
}

void SampleBrowser::requestFolderImport(FolderImportKind kind, void (*completion)(bool success)) {
	if (sampleClaim.explicitInFlight() || folderImport.inFlight()) {
		return; // the gate refuses the presses that would ask; nothing that does is left to answer
	}
	// An auto-load claim would bind into the sound the import is replacing.
	sampleClaim.cancelAutoLoad();
	FolderImportRequest request{.kind = kind, .completion = completion};
	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem->isFolder) {
		request.folder = getCurrentFilePath();
	}
	else {
		request.folder = currentDir;
		request.previouslyViewedFilename = currentFileItem->filename.c_str();
	}
	folderImport.request(std::move(request));
}

ActionResult SampleBrowser::backButtonAction() {
	AudioEngine::stopAnyPreviewing();
	// Back either leaves the browser or goes up a directory. Either way the file this was hovering
	// is no longer the selection, and neither outcome dispatches a replacement preview to supersede
	// a load already in flight -- the same shape as the folder branch. Going up re-selects through
	// folderContentsReady(), which requests afresh.
	cancelPreview();

	return Browser::backButtonAction();
}

ActionResult SampleBrowser::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;
	// Refused while an explicit claim or a folder import is in flight: each closes the browser's gate until it ends.
	if (sampleClaim.explicitInFlight() || folderImport.inFlight()) {
		return ActionResult::DEALT_WITH;
	}

	// Save button, to delete audio file
	if (b == SAVE && Buttons::isShiftButtonPressed()) {
		if (!currentUIMode && on) {
			FileItem* currentFileItem = getCurrentFileItem();
			if (currentFileItem) {
				if (!currentFileItem->isFolder) { // This is an additional requirement only present in this class

					AudioEngine::stopAnyPreviewing();

					if (inCardRoutine) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					// Ensure sample isn't used in current song
					std::string filePath = getCurrentFilePath();

					bool allFine = audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(filePath.c_str());

					if (!allFine) {
						display->displayPopup(
						    deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUDIO_FILE_IS_USED_IN_CURRENT_SONG));
					}
					else {
						goIntoDeleteFileContextMenu();
					}
				}
			}
		}
	}

	// Horizontal encoder button
	else if (b == X_ENC) {
		if (on) {
			if (isNoUIModeActive()) {
				enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
			}
		}

		else {
			if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
				exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
			}
		}
	}

	// Record button
	else if (b == RECORD && audioRecorder.recordingSource == AudioInputChannel::NONE
	         && getCurrentClip()->type != ClipType::AUDIO) {
		if (!on || currentUIMode != UI_MODE_NONE) {
			return ActionResult::DEALT_WITH;
		}
		AudioEngine::stopAnyPreviewing();

		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		bool success = changeUISideways(&audioRecorder); // If this fails, we will become the current UI again
		if (success) {
			renderingNeededRegardlessOfUI();
			audioRecorder.process();
		}
	}

	// Load button: toggle auto-load (only for non-audio clips)
	else if (b == LOAD && getCurrentClip()->type != ClipType::AUDIO) {
		if (!on) {
			autoLoadEnabled = !autoLoadEnabled;
			indicator_leds::setLedState(IndicatorLED::LOAD, autoLoadEnabled);
		}
	}
	else if (b == KEYBOARD && on) {
		qwertyAlwaysVisible = !qwertyAlwaysVisible;
		indicator_leds::setLedState(IndicatorLED::KEYBOARD, qwertyAlwaysVisible);
		qwertyVisible = qwertyAlwaysVisible;
		if (qwertyVisible) {
			qwertyCurrentlyDrawnOnscreen = true;
			drawKeys();
		}
	}
	else {
		return Browser::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

bool SampleBrowser::canImportWholeKit() {
	return (soundEditor.editingKit() && soundEditor.currentSourceIndex == 0
	        && (SoundDrum*)getCurrentInstrumentClip()->noteRows[0].drum == soundEditor.currentSound
	        && (!getCurrentKit()->firstDrum->next));
}

std::string SampleBrowser::getCurrentFilePath() {
	std::string path = currentDir;
	if (!path.empty()) {
		path.append("/");
	}

	FileItem* currentFileItem = getCurrentFileItem();

	path.append(currentFileItem->filename);

	return path;
}

bool SampleBrowser::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {

	if (currentlyShowingSamplePreview || qwertyVisible || getRootUI() == &keyboardScreen) {
		*cols = 0b10;
	}
	else {
		*cols = 0xFFFFFFFE;
	}

	return true;
}

void SampleBrowser::previewIfPossible(int32_t movementDirection) {

	/*
	// Was this in case they've already turned the knob further?
	if (movementDirection && movementDirection * Encoders::encoders[ENCODER_THIS_CPU_SELECT].pos > 0 &&
	numFilesFoundInRightDirection > 1) { D_PRINTLN("returned 1"); return;
	}
	*/

	FileItem* currentFileItem = getCurrentFileItem();

	// Nothing to preview (empty selection or a folder) — just tear down any preview onscreen.
	if (!currentFileItem || currentFileItem->isFolder) {
		// Whatever is loading is no longer what the user is on, and this branch dispatches no
		// replacement to supersede it -- so it has to be cancelled outright, or it lands a waveform
		// (and a note-on) for a file the cursor has left, over a folder. See previewFile() on why
		// dropping the load itself is free.
		cancelPreview();
		clearPreviewDisplay(movementDirection);
		return;
	}

	previewFile(getCurrentFilePath(), movementDirection);
}

void SampleBrowser::previewFile(std::string path, int32_t movementDirection) {
	// A cursor move supersedes an auto-load claim of the file it has left.
	sampleClaim.cancelAutoLoad();

	// Only recorded: the next tick starts the load. A cursor that moves on before then replaces this at no cost, and a
	// load already started for an earlier file is replaced when this one starts.
	previewWanted_ = PreviewTarget{
	    .path = std::move(path),
	    .movementDirection = movementDirection,
	};
}

void SampleBrowser::cancelPreview() {
	// Nothing previewed, nothing to auto-load: an auto-load claim of the file being left goes too.
	sampleClaim.cancelAutoLoad();
	previewWanted_.reset();
	previewLoad_.cancel();
	previewShow_.clear_queued();
	// A show already dispatched finds its generation passed, and does nothing.
	previewGeneration_++;
}

void SampleBrowser::browserGraphicsRoutine() {
	tickPreview();
	// No UI::graphicsRoutine() tail: Browser::graphicsRoutine() makes that call, once, after this
	// returns. Repeating it here would run the base tick twice per frame.
}

void SampleBrowser::tickPreview() {
	if (!previewWanted_.has_value() && !previewLoad_.pending()) {
		return;
	}

	// Counted for the A/B gate (AB-PREVIEW-TIER): a preview load step on the storage owner.
	if (deluge::storage::Owner::on_owner()) {
		previewLoadsOnOwner_++;
	}

	if (previewWanted_.has_value()) {
		PreviewTarget target = std::move(*previewWanted_);
		previewWanted_.reset();
		startPreviewLoad(std::move(target));
	}

	if (auto outcome = previewLoad_.tick(); outcome.has_value()) {
		finishPreviewLoad(std::move(*outcome));
	}
}

void SampleBrowser::startPreviewLoad(PreviewTarget target) {
	// This more formally does the thing that actually was happening accidentally for ages, as found by Michael B.
	lastFilePathLoaded = target.path;
	previewGeneration_++;

	AudioEngine::stopAnyPreviewing(); // Also unbinds the preview's holder, so the load looks the new path up.
	MultisampleRange* range = (MultisampleRange*)AudioEngine::sampleForPreview->sources[0].getOrCreateFirstRange();
	if (range == nullptr) {
		previewLoad_.cancel();
		display->displayError(Error::INSUFFICIENT_RAM);
		return;
	}
	range->sampleHolder.filePath.assign(target.path);
	// CLUSTER_ENQUEUE, not CLUSTER_LOAD_IMMEDIATELY: the immediate mode reads the covered clusters from
	// the card SYNCHRONOUSLY, measured at 38.2-40.1 ms on device. That stalls the audio path (a 9.8 ms
	// late render, plus four more in the same second), which inflates the engine's measured render time,
	// which makes it declare an overload and force-cull a voice -- the very preview voice the fill was
	// warming. That is the preview-truncation bug; see the 2026-08-15 async-note-on-residency design
	// doc, section 4b.
	//
	// Enqueueing never blocks. The note-on then sees LOADING and defers (VoiceUnisonPartSource), so the
	// preview starts a few ms late instead of either stalling audio or dropping the voice.
	previewLoad_.request(
	    deluge::storage::SampleDemand{
	        .holder = &range->sampleHolder,
	        .manuallySelected = true,
	        .clusterLoadInstruction = CLUSTER_ENQUEUE,
	    },
	    /*context=*/nullptr, std::move(target));
}

void SampleBrowser::finishPreviewLoad(deluge::storage::LatestSampleLoad<PreviewTarget>::Outcome outcome) {
	using Result = deluge::storage::LatestSampleLoad<PreviewTarget>::Result;
	using Reason = deluge::storage::AsyncSampleLoad::Reason;

	if (outcome.result == Result::Failed) {
		// Nothing more can arrive for this load: it stopped moving, or the card cannot be read. Give up as a load that
		// could not reach the card, leaving the display as it is. An abort shows nothing: the browser never loads in
		// the mode that aborts on input.
		if (outcome.reason == Reason::Stalled) {
			previewGiveUps_++;
		}
		if (outcome.reason == Reason::Stalled || outcome.reason == Reason::CardUnusable) {
			display->displayError(Error::SD_CARD);
		}
		return;
	}

	if (outcome.error != Error::NONE) {
		display->displayError(outcome.error); // Rare, shouldn't cause later problems.
	}

	bool shouldActuallySound = false;

	// Decide if we're actually going to sound it.
	if (!instrumentClipView.fileBrowserShouldNotPreview) {
		switch (FlashStorage::sampleBrowserPreviewMode) {
		case PREVIEW_ONLY_WHILE_NOT_PLAYING:
			if (playbackHandler.playbackState) {
				break;
			}
			// No break

		case PREVIEW_ON:
			shouldActuallySound = true;
			break;
		}
	}

	if (shouldActuallySound) {
		AudioEngine::soundPreview();
	}

	if (autoLoadEnabled && getCurrentClip()->type != ClipType::AUDIO) {
		// Feature: if Load has been toggled on, then the file will be auto-loaded into the current instrument
		// as if you had confirmed with the Select encoder, but keeping the browser open.
		sampleClaim.request({
		    .path = outcome.payload.path,
		    .enteredText = enteredText,
		    .loadWithoutExiting = true,
		    .origin = ClaimOrigin::AutoLoad,
		});
	}

	// The waveform draw still reads the card, so it stays on the storage owner.
	const PreviewShow show{
	    .movementDirection = outcome.payload.movementDirection,
	    .generation = previewGeneration_,
	};
	if (previewShow_.request(show) && !deluge::storage::Owner::run(&SampleBrowser::runPreviewShowOp, this)) {
		// Owner queue full: the op won't run, so release the guard (which would otherwise wedge single-flight for ever)
		// and drop the show. The display is left as it is.
		previewShow_.reset();
	}
}

void SampleBrowser::runPreviewShowOp(void* self) {
	auto* browser = static_cast<SampleBrowser*>(self);
	const PreviewShow show = browser->previewShow_.current();

	// Checked BEFORE the work: since this was dispatched, a newer preview may have started, whose holder is already
	// unbound, so drawing now would clear the waveform meant to stay up until the new one lands; or the cursor may have
	// moved somewhere with no preview at all. Claiming or drawing a file the cursor has left is the damage.
	if (show.generation == browser->previewGeneration_) {
		browser->showPreview(show);
	}

	if (browser->previewShow_.complete().has_value()
	    && !deluge::storage::Owner::run(&SampleBrowser::runPreviewShowOp, self)) {
		browser->previewShow_.reset(); // re-dispatch dropped (see finishPreviewLoad)
	}
}

void SampleBrowser::showPreview(const PreviewShow& show) {

	bool didDraw = false;

	// If the Sample at least loaded, even if we didn't sound it, then try to render its waveform.
	if (std::ssize(AudioEngine::sampleForPreview->sources[0].ranges) >= 1) {
		AudioFile* sample =
		    ((MultisampleRange*)AudioEngine::sampleForPreview->sources[0].ranges.getElement(0))->sampleHolder.audioFile;

		if (sample) {
			uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);

			currentlyShowingSamplePreview = true;
			PadLEDs::reassessGreyout(true);

			waveformBasicNavigator.sample = (Sample*)sample;
			waveformBasicNavigator.opened();

			// If want scrolling animation
			if (show.movementDirection && !qwertyAlwaysVisible) {
				waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
				                                  waveformBasicNavigator.xZoom, PadLEDs::imageStore,
				                                  &waveformBasicNavigator.renderData);
				memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
				PadLEDs::horizontal::setupScroll(show.movementDirection, kDisplayWidth);

				currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
			}

			// Or if want instant snap render
			else {
				if ((qwertyVisible && !qwertyCurrentlyDrawnOnscreen) || qwertyAlwaysVisible) {
					drawKeys();
				}
				else if (!qwertyVisible) {
					waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
					                                  waveformBasicNavigator.xZoom, PadLEDs::image,
					                                  &waveformBasicNavigator.renderData);
					PadLEDs::sendOutMainPadColours();
				}
				qwertyCurrentlyDrawnOnscreen = qwertyVisible;
			}
			PadLEDs::sendOutSidebarColours(); // For greyout (wait what?)

			didDraw = true;
		}
	}

	if (!didDraw) {
		clearPreviewDisplay(show.movementDirection);
	}
}

void SampleBrowser::clearPreviewDisplay(int32_t movementDirection) {
	// If we need to get rid of whatever was onscreen...
	if ((currentlyShowingSamplePreview || (qwertyCurrentlyDrawnOnscreen && !qwertyVisible)) && !qwertyAlwaysVisible) {

		currentlyShowingSamplePreview = false;
		qwertyCurrentlyDrawnOnscreen = qwertyVisible;

		if (movementDirection) {
			getRootUI()->renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
			//((ViewScreen*)getRootUI())->renderToStore(0, true, false);
			if (getRootUI() != &keyboardScreen) {
				PadLEDs::reassessGreyout(true);
			}
			memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
			PadLEDs::horizontal::setupScroll(movementDirection, kDisplayWidth);
			currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
		}

		possiblySetUpBlinking();
	}
}

void SampleBrowser::scrollFinished() {
	exitUIMode(UI_MODE_HORIZONTAL_SCROLL);
}

void SampleBrowser::displayCurrentFilename() {
	if (fileIndexSelected == -1) {}

	else {}
}

ActionResult SampleBrowser::padAction(int32_t x, int32_t y, int32_t on) {
	// Refused while an explicit claim or a folder import is in flight: each closes the browser's gate until it ends.
	if (sampleClaim.explicitInFlight() || folderImport.inFlight()) {
		return ActionResult::DEALT_WITH;
	}

	// Allow auditioning
	if (x == kDisplayWidth + 1) {
		if (getRootUI() == &instrumentClipView) {
			return instrumentClipView.padAction(x, y, on);
		}
	}

	// Mute pads - exit UI
	else if (x == kDisplayWidth) { // !currentlyShowingSamplePreview ||
possiblyExit:
		if (on && !currentUIMode) {
			AudioEngine::stopAnyPreviewing();
			if (isSDRoutineActive()) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitAction();
		}
	}

	else {
		// If qwerty not visible yet, make it visible
		if (!qwertyVisible) {
			if (on && !currentUIMode) {
				if (isSDRoutineActive()) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				qwertyVisible = true;

				uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
				PadLEDs::reassessGreyout(true);

				drawKeys();

				qwertyCurrentlyDrawnOnscreen = true;

				enteredTextEditPos = 0;
				displayText(false);

				// Process first press only if its not a favourite row press to prevent blind keypresses
				if (y < favouriteRow) {
					return Browser::padAction(x, y, on);
				}
			}
		}
		// Only process the QWERTY keypress if Keyboard is visible to prevent blind keypresses
		else if (qwertyVisible) {
			return Browser::padAction(x, y, on);
		}
		else {
			return ActionResult::DEALT_WITH;
		}
	}

	return ActionResult::DEALT_WITH;
}

void SampleBrowser::autoDetectSideChainSending(SoundDrum* drum, Source* source, char const* fileName) {

	// If this looks like a kick, make it send to sidechain. Otherwise, no change
	if (source->repeatMode == SampleRepeatMode::ONCE && (strcasestr(fileName, "kick") || strcasestr(fileName, "bd"))) {
		drum->sideChainSendLevel = 2147483647;
	}
}

void SampleBrowser::audioFileIsNowSet() {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
	ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
	PatchedParamSet* paramSet = (PatchedParamSet*)summary->paramCollection;
	int32_t paramId = params::LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex;
	ModelStackWithAutoParam* modelStackWithParam =
	    modelStack->addParam(paramSet, summary, paramId, &paramSet->params[paramId]);

	// Reset osc volume, if it's not automated and was at 0. Wait but that will only do it for the current
	// ParamManager... there could be other ones...
	if (!modelStackWithParam->autoParam->containsSomething(-2147483648)) {
		modelStackWithParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithParam, 2147483647);

		// Hmm crap, we probably still do need to notify...
		//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(params::LOCAL_OSC_A_VOLUME +
		// soundEditor.currentSourceIndex, 2147483647, 0xFFFFFFFF, 0, soundEditor.currentSound, currentSong,
		// getCurrentClip(), false);
	}
}

static const uint32_t zoomUIModes[] = {UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, UI_MODE_AUDITIONING, 0};

ActionResult SampleBrowser::horizontalEncoderAction(int32_t offset) {
	// Refused while an explicit claim or a folder import is in flight: each closes the browser's gate until it ends.
	if (sampleClaim.explicitInFlight() || folderImport.inFlight()) {
		return ActionResult::DEALT_WITH;
	}

	// Or, maybe we want to scroll or zoom around the waveform...
	if (currentlyShowingSamplePreview
	    && (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) || waveformBasicNavigator.isZoomedIn())) {

		// We're quite likely going to need to read the SD card to do either scrolling or zooming
		if (isSDRoutineActive()) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// Zoom
		if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
			if (isUIModeWithinRange(zoomUIModes)) {
				waveformBasicNavigator.zoom(offset);
			}
		}

		// Scroll
		else if (isUIModeWithinRange(&zoomUIModes[1])) { // Allow during auditioning only
			bool success = waveformBasicNavigator.scroll(offset);

			if (success) {
				waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
				                                  waveformBasicNavigator.xZoom, PadLEDs::image,
				                                  &waveformBasicNavigator.renderData);
				PadLEDs::sendOutMainPadColours();
			}
		}
		return ActionResult::DEALT_WITH;
	}
	else {
		qwertyVisible = true;

		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
		PadLEDs::reassessGreyout(true);

		drawKeys();

		qwertyCurrentlyDrawnOnscreen = true;

		return Browser::horizontalEncoderAction(offset);
	}
}

ActionResult SampleBrowser::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	// Refused while an explicit claim or a folder import is in flight: each closes the browser's gate until it ends.
	if (sampleClaim.explicitInFlight() || folderImport.inFlight()) {
		return ActionResult::DEALT_WITH;
	}

	if (Buttons::isShiftButtonPressed()) {
		return Browser::verticalEncoderAction(offset, false);
	}
	if (getRootUI() == &instrumentClipView) {
		if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
			return ActionResult::DEALT_WITH;
		}
		return instrumentClipView.verticalEncoderAction(offset, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

bool SampleBrowser::canSeeViewUnderneath() {
	return !currentlyShowingSamplePreview && !qwertyVisible;
}

bool SampleBrowser::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	return (qwertyVisible || currentlyShowingSamplePreview);
}
