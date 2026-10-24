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

#include "gui/ui/load/load_song_ui.h"
#if DELUGE_STREAM_BENCH
#include "bench/stream_bench.h"
#endif
#include "board_config.h" // PLACE_SDRAM_BSS
#include "definitions_cxx.hpp"
#include "deluge_resource.h" // deluge_resource_loader_has_lowest
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "libdeluge/file_prefix.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/song_serialization.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/load_trace.h"
#include "storage/byte_channel.h" // noteIoTier, for the inline preview read
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/song_preview.h"
#include "storage/storage_manager.h"
#include "util/boot.h"
#include "util/try.h"
#include <libdeluge/scheduler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>
#include <string.h>

PLACE_SDRAM_BSS LoadSongUI loadSongUI{};

extern void songLoaded(Song* song);
extern void setUIForLoadedSong(Song* song);
extern void setupBlankSong();

using namespace deluge::gui;
namespace encoders = deluge::hid::encoders;

/// How long a load waits for the sample loader to settle before swapping anyway: a backstop so a card
/// that never settles still lets the song through.
constexpr double kLoadSettleTimeoutSeconds = 5.;

namespace {

/// @brief Where a collected song prefix lands on its way to the decoder.
///
/// File-scope rather than a local, and that is load-bearing rather than a size preference. While
/// `decodeSongPreview()` runs, the global `activeDeserializer` points at a reader whose cluster buffer
/// IS this array; the decode puts that global back before it returns, but the buffer must outlive
/// every read through it, and nothing about a 4 KiB automatic in a UI callback would make that
/// clearer. One is enough by construction: there is exactly one LoadSongUI and `previewLoad_` is
/// single-flight, so a second collect can never be in progress alongside the first.
///
/// PLACE_SDRAM_BSS because 4 KiB of on-chip RAM is far too dear for a buffer touched once per preview
/// and never by the audio path -- and because the reader that reads THROUGH it (smJsonBufferDeserializer
/// / smBufferDeserializer, storage_manager.cpp) already lives there, so this merely stops the two
/// halves of one decode straddling two memories. A plain byte array needs no zeroing, so unlike the
/// std::optionals next to it this does not lean on the boot's .sdram_bss zeroing (boot_mem.rs).
// Sized by the CHANNEL's capacity, not by kSongPreviewWindow, even though only kSongPreviewWindow
// bytes of it are ever requested. deluge_file_prefix_collect() requires out_cap >=
// DELUGE_FILE_PREFIX_CAPACITY whatever the caller asked for: a shorter buffer is a caller bug that
// trips a debug_assert in a dev build and silently yields blank artwork in release, rather than
// serving truncated bytes. The two constants were equal until the DX7 cartridge became a second
// consumer and the slot grew to suit it, so spelling the buffer with the window would now under-size
// it by half and lose every song's artwork on a release build.
PLACE_SDRAM_BSS std::array<uint8_t, DELUGE_FILE_PREFIX_CAPACITY> songPreviewPrefix;

// Spelled through the object rather than repeating the macro, so re-sizing the array above is what
// these check -- not a second copy of the same literal agreeing with itself.
using SongPreviewPrefixBuffer = decltype(songPreviewPrefix);
static_assert(std::tuple_size_v<SongPreviewPrefixBuffer> >= DELUGE_FILE_PREFIX_CAPACITY,
              "the preview buffer must be able to take the largest prefix the channel can hold");
static_assert(deluge::storage::kSongPreviewWindow <= std::tuple_size_v<SongPreviewPrefixBuffer>,
              "the preview cannot request more than its own buffer holds");

/// @brief Whether a song path names a JSON document.
///
/// A buffer has no filename for the reader to infer the format from, so the caller has to say.
/// Delegates rather than restating the rule: two copies of "what counts as JSON" is exactly the pair
/// that drifts, and getting it wrong routes an XML song to the JSON parser, where the failure reads
/// as a corrupt file.
bool isJsonSongPath(const std::string& path) {
	return StorageManager::documentIsJson(path.c_str());
}

} // namespace

LoadSongUI::LoadSongUI() {
	qwertyAlwaysVisible = false;
	filePrefix = "SONG";
	title = "Load song";
	performingLoad = false;
	qwertyCurrentlyDrawnOnscreen = false;
}

bool LoadSongUI::opened() {

	// Nothing carried over from a previous visit: a preview still being read when the browser closed
	// names a song in whatever folder was open then, and graphicsRoutine() starts running again the
	// moment this UI is current -- before folderContentsReady() has picked the entry for THIS visit.
	cancelPreview();

	qwertyAlwaysVisible = false;
	qwertyCurrentlyDrawnOnscreen = false;

	favouritesManager.setCategory("SONG");
	favouritesChanged();
	// The folder's arrival hides the keyboard; the listing that does it here starts on a later pass, and a favourites
	// bank landing before it would draw its row over the scroll-out and take over the scroll's timer.
	qwertyVisible = false;
	outputTypeToLoad = OutputType::NONE;
	currentDir = currentSong->project.metadata.dir_path;

	Error error = beginSlotSession(false, true);
	if (error != Error::NONE) {
		display->displayError(error);
		// Oh no, we're unable to read a file representing the first song. Get out quick!
		currentUIMode = UI_MODE_NONE;
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		renderingNeededRegardlessOfUI(); // Otherwise we may have left the scrolling-in animation partially done
		return false;                    // Exit UI instantly
	}

	currentUIMode = UI_MODE_VERTICAL_SCROLL;
	PadLEDs::vertical::setupScroll(1, true);
	scrollingIntoSlot = false;
	deletedPartsOfOldSong = false;
	timerCallback(); // Start scrolling animation out of the View

	PadLEDs::clearTickSquares();

	std::string searchFilename;
	searchFilename = currentSong->project.metadata.name;

	if (!searchFilename.empty()) {
		searchFilename.append(currentSong->project.metadata.file_extension);
	}

	// The listing happens async: dispatch it and return optimistically. Failure goes through the
	// base Browser::onListingFailed() (displayError + close()) once the listing completes.
	beginListing(
	    {.action = ListingAction::Open, .direction = 0, .filenameToStartAt = searchFilename, .defaultDir = "SONGS"});

	return true;
}

void LoadSongUI::onBrowserOpened() {
	focusRegained();

	PadLEDs::vertical::setupScroll(1, false);
	scrollingIntoSlot = true;

	if (currentUIMode != UI_MODE_VERTICAL_SCROLL) {
		currentUIMode = UI_MODE_VERTICAL_SCROLL; // Have to reset this again - it might have finished the first bit
		                                         // of the scroll
		timerCallback();
	}

	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, false);
	indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	if (ALPHA_OR_BETA_VERSION && currentUIMode == UI_MODE_WAITING_FOR_NEXT_FILE_TO_LOAD) {
		FREEZE_WITH_ERROR("E188");
	}
}

void LoadSongUI::folderContentsReady(int32_t entryDirection) {

	// Return deliberately ignored: nothing here scrolls out of imageStore, so a preview that has not
	// landed yet is harmless -- finishPreviewRender() shows it once it does.
	(void)drawSongPreview(currentUIMode == UI_MODE_VERTICAL_SCROLL);

	PadLEDs::sendOutMainPadColours();
	PadLEDs::sendOutSidebarColours();
}

namespace {
/// @brief The completion of a load the user asked for: with LASTOPENED, record the song in the settings file.
///
/// Only a load that brought a song in records one. The current song after any other ending is the one that was
/// already there, or a blank stand-in after a failure, and neither is a song the user just opened.
void recordLastOpened(SongLoadOutcome outcome) {
	if (outcome == SongLoadOutcome::Loaded && FlashStorage::defaultStartupSongMode == StartupSongMode::LASTOPENED) {
		runtimeFeatureSettings.writeSettingsToFile();
	}
}
} // namespace

void LoadSongUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();

	// If it's a directory...
	if (currentFileItem && currentFileItem->isFolder) {
		// The song this was hovering is not in the folder we are entering, and nothing asks for a
		// replacement until the listing completes -- so without this, a preview landing in between
		// paints the departed folder's song over the new one's first entry. Same shape as Back.
		cancelPreview();
		// goIntoFolder() begins a listing that graphicsRoutine() runs over later ticks; failure is handled by
		// the base Browser::onListingFailed() (displayError + exitAction) once the listing completes.
		goIntoFolder(currentFileItem->filename.c_str());
	}

	else {
		// Start the load and return: loadRoutine() advances it from the run loop, and it parks at each
		// point it has to wait, so this handler never blocks. The post-load settings
		// write travels with it as the completion.
		beginLoad(recordLastOpened);
	}
}

void LoadSongUI::displayArmedPopup() {
	display->removeWorkingAnimation();
	display->popupText("Song will begin...", PopupType::LOADING);
}

char loopsRemainingText[] = "Loops remaining: xxxxxxxxxxx";

void LoadSongUI::displayLoopsRemainingPopup() {
	if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		display->removeWorkingAnimation();
		intToString(session.numRepeatsTilLaunch, &loopsRemainingText[17]);
		display->popupText(loopsRemainingText, PopupType::LOADING);
	}
}

ActionResult LoadSongUI::buttonAction(deluge::hid::Button b, bool on) {
	using namespace deluge::hid::button;

	if (listingInProgress_) {
		// BACK still cancels a load that is staging; the listing carries on either way.
		if (b == BACK && on && !currentUIMode) {
			(void)cancelLoad();
		}
		return ActionResult::DEALT_WITH;
	}

	// Load button or select encoder press. Unlike most (all?) other children of Browser, we override this and don't
	// just call mainButtonAction(), because unlike all the others, we need to action the load immediately on down-press
	// rather than waiting for press-release, because of that special action where you hold the button down until you
	// want to "launch" the new song.
	if ((b == LOAD) || (b == SELECT_ENC)) {
		if (on) {
			if (!currentUIMode) {
				enterKeyPress();
			}
		}
		else {
			(void)loadButtonAction(b, on);
		}
	}
	else if (b == KEYBOARD && on) {
		qwertyAlwaysVisible = !qwertyAlwaysVisible;
		indicator_leds::setLedState(IndicatorLED::KEYBOARD, qwertyAlwaysVisible);
		qwertyVisible = qwertyAlwaysVisible;
		if (qwertyVisible) {
			drawKeys();
			qwertyCurrentlyDrawnOnscreen = true;
		}
	}
	else {
		return LoadUI::buttonAction(b, on);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult LoadSongUI::loadButtonAction(deluge::hid::Button b, bool on) {
	using namespace deluge::hid::button;
	if (b != LOAD && b != SELECT_ENC) {
		return ActionResult::NOT_DEALT_WITH;
	}
	// Released with every essential sample loaded: the swap the load is waiting on can be armed.
	if (!on && currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED) {
		// Not armable, because the song has already swapped...
		if (!session.armForSongSwap()) {
			currentUIMode = UI_MODE_LOADING_SONG_NEW_SONG_PLAYING;
		}
		else {
			currentUIMode = UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED;
			displayArmedPopup();
		}
	}
	return ActionResult::DEALT_WITH;
}

void LoadSongUI::armedSelectEncoderAction(int8_t offset) {
	if (currentUIMode != UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		return;
	}
	session.numRepeatsTilLaunch = std::clamp<int32_t>(session.numRepeatsTilLaunch + offset, 1, 9999);
	displayLoopsRemainingPopup();
}

bool LoadSongUI::isLoadingSong() {
	// The current open UI is this one, or there is an automatic "load next" command in progress
	return getCurrentUI() == this || performingLoad;
}

// This is the public method exposed to allow for queue loading next song while playing
void LoadSongUI::queueLoadNextSongIfAvailable(int8_t offset) {
	if (!playbackHandler.isEitherClockActive()) {
		// This method is intended just for queueing while playing
		return;
	}
	if (performingLoad) {
		// If another load is in progress, ignore this request
		return;
	}
	if (fileIndexSelected == -1) {
		// If no song is loaded/selected yet, we have nothing to do
		return;
	}
	doQueueLoadNextSongIfAvailable(offset);
}

// This method actually executes the loading of next song (by offset)
void LoadSongUI::doQueueLoadNextSongIfAvailable(int8_t offset) {
	outputTypeToLoad = OutputType::NONE;
	currentDir = currentSong->project.metadata.dir_path;

	int32_t currentFileIndexSelected = fileIndexSelected;

	bool songFound = false;
	do {
		fileIndexSelected = fileIndexSelected + offset;
		if (fileIndexSelected < 0) {
			fileIndexSelected = listing.size() - 1;
		}
		else if (fileIndexSelected >= listing.size()) {
			fileIndexSelected = 0;
		}
		Browser::setEnteredTextFromCurrentFilename();

		FileItem* currentFileItem = getCurrentFileItem();

		if (currentFileItem != nullptr) {
			// Check if it's a directory...
			if (currentFileItem->isFolder) {
				// it is a folder
				// scroll to the next item
				continue;
			}
			else {
				// if is a file, select it
				songFound = true;
				AudioEngine::logAction("beginLoad");
				// See enterKeyPress. The completion owns the post-load settings write AND the
				// UI-mode reset, so there is nothing left to do here.
				beginLoad([](SongLoadOutcome outcome) {
					recordLastOpened(outcome);
					currentUIMode = UI_MODE_NONE;
				});
				return;
			}
		}
	} while (currentFileIndexSelected != fileIndexSelected && !songFound);
	// in case we wrapped around the whole list of files and didn't find a song,
	// we just exit without doing anything else

	currentUIMode = UI_MODE_NONE;
}

void LoadSongUI::endLoad(SongLoadOutcome outcome) {
	essentialLoad_.reset();
	restLoad_.reset();
	loadPhase_ = LoadPhase::NONE;
	loadToDelete_ = nullptr;
	performingLoad = false;

	// The backstop for the staged song: every way a load can end passes through here, so a buffer that
	// survived an early return is freed exactly once and the next load starts from a clear path.
	releaseSongStaging();
	songDocument_.release();
	stagingPath_.clear();
	stagingPrevious_ = nullptr;
	stagingWait_.reset();

	// Cleared before the call so a completion that starts another load doesn't inherit this one's.
	void (*onComplete)(SongLoadOutcome) = loadOnComplete_;
	loadOnComplete_ = nullptr;
	if (onComplete != nullptr) {
		onComplete(outcome);
	}
}

// Before calling this, you must set loadButtonReleased.
void LoadSongUI::beginLoad(void (*onComplete)(SongLoadOutcome)) {
	// The browser stays live and interactive for the whole of STAGE, which for a large song is many
	// ticks. enterKeyPress() calls straight in here, and only queueLoadNextSongIfAvailable() checks
	// this flag on the way. A second ENTER would re-enter, find stagingPath_ already pinned, and carry
	// on staging the FIRST song while the user believes they chose the second -- with a channel
	// request possibly already in flight against the old buffer.
	//
	// The window spans many ticks for a large song, so this guard is real, not decorative.
	if (performingLoad) {
		return;
	}

	// The browser is on its way out and the pads now belong to the load: the greyout, the loading
	// animation, and finally the new song's view. Nothing here asks for a replacement preview, so an
	// artwork read still outstanding would paint over all of that.
	cancelPreview();

	loadOnComplete_ = onComplete;
	performingLoad = true;

	// The song is read into memory FIRST, on this tier, before any of it is parsed. Staging it a tick at
	// a time is what lets the read go through the channel at all: the Rust driver only makes progress
	// once we return to the run loop, so a request issued from inside one pass that does not yield could
	// never be answered.
	//
	// It also surfaces an unreadable-file failure before startLoad() deletes the outgoing song: after that
	// point, an unreadable file leaves the messy recovery its `fail:` block exists to perform. Staging first
	// stops the load before anything has been destroyed.
	// Raised here, not in startLoad(), because the waiting starts here: a large song stages for
	// many ticks before its parse even begins, and would otherwise do it in silence.
	display->displayLoadingAnimationText("Loading");

	loadPhase_ = LoadPhase::STAGE;

	// Attempt it once here rather than waiting for the next loadRoutine() pass. Boot loads the startup
	// song before the jobs are running, so a beginLoad() that only parked would never be
	// picked up and the device would sit at the splash screen forever.
	runStageSegment();
	// The run above may have been made outside the loop's pass, where a time it asked for wakes nothing.
	deluge::runtime::appLoop.request(jobId);
}

/// @brief Fill @ref songStaging_ from the card, a tick at a time, then move the load on to BEGIN.
///
/// Runs on the interaction tier, which is the whole point: returning to the run loop between attempts
/// is what lets the Rust driver run at all. Nothing here touches the outgoing song, so abandoning the
/// load from this phase costs only the buffer.
void LoadSongUI::runStageSegment() {
	if (stagingPath_.empty()) {
		// Wait for the browser's listing on TWO conditions, and both are needed.
		// isListingInProgress() alone is NOT sufficient: its contract is "false once it has completed
		// OR NONE WAS STARTED", so at the instant beginLoad() runs -- before any listing has begun --
		// it already reads false, and we would sail straight into a null cursor. The null-cursor test
		// is what actually waits; isListingInProgress() is what stops us reading a list mid-rebuild.
		FileItem* item = getCurrentFileItem();
		if (Browser::isListingInProgress() || item == nullptr) {
			if (stagingWait_.tick()) {
				display->displayError(Error::FILE_NOT_FOUND);
				endLoad(SongLoadOutcome::NotLoaded);
			}
			return;
		}
		// The folder the item was LISTED from, not where the browser happens to point now.
		stagingPath_ = listing.dir() + "/" + item->getFilenameWithExtension();
		stagingPrevious_ = activeDeserializer;
	}

	switch (StorageManager::stageDocument(stagingPath_.c_str(), &songStaging_)) {
	case StorageManager::StagedOpen::Deferred:
		return; // Ask again next pass.

	case StorageManager::StagedOpen::Failed:
		// Nothing has been destroyed yet, so there is nothing to recover -- say so and stop, leaving
		// the browser open so the user can pick another song.
		activeDeserializer = stagingPrevious_;
		releaseSongStaging();
		display->displayError(Error::FILE_CORRUPTED);
		endLoad(SongLoadOutcome::NotLoaded);
		return;

	case StorageManager::StagedOpen::Opened: {
#if DELUGE_STREAM_BENCH
		const uint64_t parseStart = deluge::runtime::nowUs();
#endif
		// Record-first load: parse the whole document into a Value tree now, while nothing has been destroyed,
		// so a corrupt file still leaves the browser open. BEGIN replays the tree to readFromFile.
		const Error parsed = songDocument_.open(std::span<const uint8_t>(songStaging_.buffer, songStaging_.size),
		                                        StorageManager::documentIsJson(stagingPath_.c_str()), "song");
#if DELUGE_STREAM_BENCH
		deluge::bench::noteLoadStage(deluge::runtime::nowUs() - parseStart);
#endif
		if (parsed != Error::NONE) {
			activeDeserializer = stagingPrevious_;
			releaseSongStaging();
			display->displayError(Error::FILE_CORRUPTED);
			endLoad(SongLoadOutcome::NotLoaded);
			return;
		}
		loadPhase_ = LoadPhase::BEGIN;
		return;
	}
	}
}

/// @brief Free the staged song's bytes.
///
/// Called as soon as the deserializer is finished with them, and again from endLoad() as a backstop so
/// no path that abandons a load can leak a megabyte. `stagingPath_` deliberately OUTLIVES this --
/// startLoad() still needs the name of the song it just read -- and endLoad() clears it.
void LoadSongUI::releaseSongStaging() {
	StorageManager::releaseStaging(&songStaging_);
}

void LoadSongUI::loadRoutine() {
#if DELUGE_STREAM_BENCH
	const auto phase = loadPhase_;
	const uint64_t start = deluge::runtime::nowUs();
	// Read before runPhase(), which may end the load it ticks and drop it.
	const auto* walking = phase == LoadPhase::ESSENTIAL && essentialLoad_.has_value() ? &*essentialLoad_
	                      : phase == LoadPhase::AWAIT_ARM && restLoad_.has_value()    ? &*restLoad_
	                                                                                  : nullptr;
	deluge::storage::AsyncSampleLoad::Progress walked{};
	runPhase();
	if (walking != nullptr && (phase == LoadPhase::ESSENTIAL ? essentialLoad_.has_value() : restLoad_.has_value())) {
		walked = walking->progress();
	}
	deluge::bench::noteLoadStep(static_cast<int>(phase), deluge::runtime::nowUs() - start, walked.bound,
	                            walked.emptyHolders, walked.inFlight);
#else
	runPhase();
#endif
	armNextRun();
}

void LoadSongUI::armNextRun() {
	using deluge::runtime::appLoop;
	const uint64_t now = deluge::runtime::nowUs();
	uint64_t next = deluge::runtime::kNever;
	switch (loadPhase_) {
	case LoadPhase::NONE:
		return;
	case LoadPhase::STAGE:
		if (stagingPath_.empty()) {
			// Waiting for the browser's listing.
			next = std::min(stagingWait_.deadlineUs(), now + kRecheckUs);
		}
		else {
			next = StorageManager::stagingNextRunUs(songStaging_);
		}
		break;
	case LoadPhase::BEGIN:
	case LoadPhase::ARM:
		next = 0;
		break;
	case LoadPhase::ESSENTIAL:
		next = essentialLoad_.has_value() ? essentialLoad_->nextTickUs() : 0;
		break;
	case LoadPhase::SETTLE: {
		// settleDeadline_ is on getSystemTime(), the clock the app loop's is rounded from: round up past it.
		const uint64_t deadline = static_cast<uint64_t>(std::ceil(settleDeadline_ * 1'000'000.0)) + 1;
		next = std::min(deadline, now + kRecheckUs);
		break;
	}
	case LoadPhase::AWAIT_ARM:
		next = now + kRecheckUs;
		if (restLoad_.has_value()) {
			next = std::min(next, restLoad_->nextTickUs());
		}
		break;
	case LoadPhase::FINISH:
		next = restLoad_.has_value() ? restLoad_->nextTickUs() : 0;
		break;
	}
	if (next == 0) {
		appLoop.request(jobId);
	}
	else if (next != deluge::runtime::kNever) {
		appLoop.requestAt(jobId, next);
	}
}

void LoadSongUI::runPhase() {
	switch (loadPhase_) {
	case LoadPhase::NONE:
		return;

	case LoadPhase::STAGE:
		runStageSegment();
		return;

	case LoadPhase::BEGIN:
		runBeginSegment();
		return;

	case LoadPhase::ESSENTIAL:
		tickEssential();
		return;

	case LoadPhase::SETTLE:
		// The loader queue holding no lowest-priority (failed-and-requeued) items means the async fill
		// task has settled. Observing that from here, rather than waiting on it, is the point: the fill
		// task only makes progress once we return to the run loop.
		if (deluge_resource_loader_has_lowest(GeneralMemoryAllocator::get().resourceManager())
		    && getSystemTime() < settleDeadline_) {
			return;
		}
		runArmSegment();
		return;

	case LoadPhase::ARM:
		// Only reachable if runArmSegment() parked here, which it does not; kept so the switch is total.
		runArmSegment();
		return;

	case LoadPhase::AWAIT_ARM:
		// The rest of the samples arrive WHILE the user decides, ticked here a walk at a time rather than run
		// to completion up front.
		tickRest();
		if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED
		    || currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED) {
			return;
		}
		runFinishSegment();
		return;

	case LoadPhase::FINISH:
		tickFinish();
		return;
	}
}

void LoadSongUI::runBeginSegment() {
#if DELUGE_STREAM_BENCH
	const uint64_t beginStart = deluge::runtime::nowUs();
#endif
	if (arrangement.hasPlaybackActive()) {
		playbackHandler.switchToSession();
	}

	// STAGE read the song on the interaction tier before this phase began, and parsed it into a Value tree.
	// Bytes that never arrived end the load the way a corrupt file does.
	//
	// stagingPrevious_ is what `activeDeserializer` is put back to at every exit. Nothing here points it elsewhere
	// (the song is read through a Value tree, not a memory reader), but code downstream writes through that global
	// (SaveSongUI's sample collection reads Cluster::size, 32 KiB, into it), so every exit restores it regardless.
	Error error = Error::NONE;
	if (songStaging_.buffer == nullptr) {
		songDocument_.release();
		activeDeserializer = stagingPrevious_;
		releaseSongStaging();
		display->displayError(Error::FILE_CORRUPTED);
		endLoad(SongLoadOutcome::NotLoaded);
		return;
	}

	// STAGE parsed those bytes into the Value tree the read below replays; the tree owns its own copies, so
	// the staged bytes can go now.
	releaseSongStaging();

	// Every UI is about to be taken down, and none will hear the releases of pads still held: end each held gesture
	// now, through the UIs that started it, while the song it plays is still alive. Before the mode below, which
	// would hide an audition from the release that ends it, and before the undo history goes: a release can record an
	// action (a tap on a note deletes it), and none may outlive the song it would revert.
	matrixDriver.noPressesHappening();
	actionLogger.deleteAllLogs();

	currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	indicator_leds::setLedState(IndicatorLED::LOAD, false);
	indicator_leds::setLedState(IndicatorLED::BACK, false);

	nullifyUIs();

	deletedPartsOfOldSong = true;

	// If not currently playing, don't load both songs at once (this avoids any RAM overfilling, fragmentation etc.)
	if (!playbackHandler.isEitherClockActive()) {
		// Otherwise, a timer might get called and try to access Clips that we may have deleted below (really?)
		uiTimerManager.unsetTimer(TimerName::PLAY_ENABLE_FLASH);

		deleteOldSongBeforeLoadingNew();
	}
	else {
		// The view keeps the playing song's mod controllable, so the knobs go on adjusting the song that is still
		// playing -- until the swap, whose interaction side lets go of it (deluge::playback::songSwappedOut()) before
		// FINISH frees it.
		AudioEngine::logAction("arming for song swap");
		AudioEngine::songSwapAboutToHappen();
		AudioEngine::logAction("song swap armed");
		playbackHandler.songSwapShouldPreserveTempo = Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC);
	}

#if DELUGE_STREAM_BENCH
	const uint64_t constructStart = deluge::runtime::nowUs();
#endif
	void* songMemory = deluge::memory::alloc_fast(sizeof(Song));
	if (!songMemory) {
ramError:
		error = Error::INSUFFICIENT_RAM;

someError:
		display->displayError(error);
		songDocument_.release();
		releaseSongStaging();
		activeDeserializer = stagingPrevious_;
fail:
		// We couldn't load the requested song. Recover to a usable state and stop.
		//
		// The original code reconstructed a Song into `songMemory` here and loaded it (without ever swapping it in),
		// which is broken under memory pressure: songMemory is null if its initial allocation failed, or already freed
		// if we arrived via block B's error cleanup (gotErrorAfterCreatingSong) - and setupBlankSong() below can
		// reallocate that very freed block as the new currentSong, so reconstructing into (and later freeing)
		// songMemory left currentSong dangling, crashing the next load.
		if (preLoadedSong && preLoadedSong != currentSong) {
			// A half-loaded song may still occupy songMemory (e.g. we arrived from a read failure) - free it.
			void* toDealloc = dynamic_cast<void*>(preLoadedSong);
			preLoadedSong->~Song(); // Will also delete paramManager
			delugeDealloc(toDealloc);
			preLoadedSong = nullptr;
		}

		// If we already deleted the old song, make a new blank one. This will take us back to InstrumentClipView.
		if (!currentSong) {
			setupBlankSong();
			audioFileManager.deleteAnyTempRecordedSamplesFromMemory();
		}
		// Otherwise the old song survived (a load during playback deletes it only once the swap is done), but its UIs
		// were taken down above: put its root UI back, or nothing would take input again. No swap is coming to replace
		// it, so the render's reverb update may walk it again.
		else {
			currentSong->soundsDeletedForSwap.store(false, std::memory_order_release);
			if (uiStackEmpty()) {
				setUIForLoadedSong(currentSong);
			}
		}

		display->removeWorkingAnimation();
		currentUIMode = UI_MODE_NONE;
		endLoad(SongLoadOutcome::NotLoaded);
		return;
	}

	preLoadedSong = new (songMemory) Song();
	error = preLoadedSong->paramManager.setupUnpatched();
	if (error != Error::NONE) {
gotErrorAfterCreatingSong:
		void* toDealloc = dynamic_cast<void*>(preLoadedSong);
		preLoadedSong->~Song(); // Will also delete paramManager
		delugeDealloc(toDealloc);
		preLoadedSong = nullptr;
		goto someError;
	}

	GlobalEffectable::initParams(&preLoadedSong->paramManager);

	AudioEngine::logAction("initialized new song");

#if DELUGE_STREAM_BENCH
	const uint64_t readStart = deluge::runtime::nowUs();
#endif
	// Will return false if we ran out of RAM. This isn't currently detected for while loading ParamNodes, but chances
	// are, after failing on one of those, it'd try to load something else and that would fail.
	{
		deluge::storage::ActiveReaderScope reading(songDocument_.reader());
		error = deluge::bridge::loadSong(*preLoadedSong, songDocument_.reader());
	}
	// The tree is only needed during the read.
	songDocument_.release();
	if (error != Error::NONE) {
		goto gotErrorAfterCreatingSong;
	}
	AudioEngine::logAction("read new song from file");

	activeDeserializer = stagingPrevious_;

	preLoadedSong->project.metadata.dir_path = listing.dir();

	// Taken from the path that was actually STAGED rather than from the browser's cursor. The cursor
	// is rebuilt by other tiers and may since have moved to a different song, or off the list
	// entirely -- and the song now in memory is the one this name has to describe.
	const std::string_view stagedPath{stagingPath_};
	const auto lastSlash = stagedPath.rfind('/');
	const std::string_view leaf = (lastSlash == std::string_view::npos) ? stagedPath : stagedPath.substr(lastSlash + 1);
	const auto lastDot = leaf.rfind('.');
	std::string currentFilenameWithoutExtension{(lastDot == std::string_view::npos) ? leaf : leaf.substr(0, lastDot)};
	if (lastDot != std::string_view::npos) {
		preLoadedSong->project.metadata.file_extension = leaf.substr(lastDot);
	}

	sampleLoadContext_ =
	    deluge::storage::AudioLoadContext::forSong(listing.dir().c_str(), currentFilenameWithoutExtension.c_str());

#if DELUGE_STREAM_BENCH
	const uint64_t claimStart = deluge::runtime::nowUs();
#endif
	// Search existing RAM for all samples, to lay a claim to any which will be needed for this new Song.
	// Do this before loading any new Samples from file, in case we were in danger of discarding any from RAM that we
	// might actually want
	preLoadedSong->loadAllSamples(false, &sampleLoadContext_);
#if DELUGE_STREAM_BENCH
	deluge::bench::noteLoadBegin(constructStart - beginStart, readStart - constructStart, claimStart - readStart,
	                             deluge::runtime::nowUs() - claimStart);
#endif

	// Load samples from files, just for currently playing Sounds (or if not playing, then all Sounds). This pass
	// reads the card, walking a tick at a time on this tier, through the loader every other consumer uses.
	essentialIsCrucial_ = playbackHandler.isEitherClockActive();
	if (essentialIsCrucial_) {
		// Traced on the host only, so a gate can see exactly what this pass bound before any later pass binds more.
		DELUGE_LOAD_TRACE("crucial begin");
		essentialLoad_.emplace(deluge::storage::AsyncSampleLoad::forCrucialSong(*preLoadedSong, &sampleLoadContext_));
	}
	else {
		essentialLoad_.emplace(deluge::storage::AsyncSampleLoad::forSong(*preLoadedSong, &sampleLoadContext_));
	}
	loadPhase_ = LoadPhase::ESSENTIAL;
}

void LoadSongUI::tickEssential() {
	// An end is an end, whatever its reason: whatever this pass does not bind is left for finishLoad()'s
	// mop-up pass. A Failed end displays nothing here -- the per-demand errors have already been shown, by
	// the same code that shows them elsewhere.
	if (essentialLoad_->tick().state == deluge::storage::AsyncSampleLoad::State::Loading) {
		return;
	}
	if (essentialIsCrucial_) {
		DELUGE_LOAD_TRACE("crucial end");
	}
	essentialLoad_.reset();
	deluge::bridge::recordLoadedSounds(*preLoadedSong);

	// The new song's essential clusters still have to finish arriving before the swap. Park here and let
	// loadRoutine() watch for it: the fill task that does the arriving only runs once we return to the run loop.
	settleDeadline_ = getSystemTime() + kLoadSettleTimeoutSeconds;
	loadPhase_ = LoadPhase::SETTLE;
}

void LoadSongUI::runArmSegment() {
	preLoadedSong->project.metadata.name = enteredText;

	loadToDelete_ = currentSong;

	if (playbackHandler.isEitherClockActive()) {

		// If load button was already released while that loading was happening, arm for song-swap now
		if (!Buttons::isButtonPressed(deluge::hid::button::LOAD)) {
			bool result = session.armForSongSwap();

			// If arming couldn't really be done, e.g. because current song had no Clips currently playing, swap has
			// already occurred
			if (!result) {
				runFinishSegment();
				return;
			}

			currentUIMode = UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED;
			displayArmedPopup();
		}

		// Otherwise, set up so that the song-swap will be armed as soon as the user releases the load button
		else {
			display->removeWorkingAnimation();
			display->popupText("Loading complete", PopupType::LOADING);
			currentUIMode = UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED;
		}

		// We're now waiting, either for the user to arm, or for the arming to launch the song-swap. Get loading all the
		// rest of the samples which weren't needed right away. (though we might run out of RAM cos we haven't discarded
		// all the old samples yet)
		AudioEngine::logAction("asking for samples");
		restLoad_.emplace(deluge::storage::AsyncSampleLoad::forSong(*preLoadedSong, &sampleLoadContext_));
		// Park until currentUIMode leaves the two loading modes - the arming, or the swap the arming
		// launches, is what moves it. The rest pass walks on while we wait, ticked from loadRoutine().
		loadPhase_ = LoadPhase::AWAIT_ARM;
		return;
	}

	playbackHandler.doSongSwap();
	runFinishSegment();
}

void LoadSongUI::tickRest() {
	if (restLoad_.has_value() && restLoad_->tick().state != deluge::storage::AsyncSampleLoad::State::Loading) {
		// An end is an end, whatever its reason: see tickEssential(). What did not arrive is what
		// FINISH's pass has always been for.
		restLoad_.reset();
	}
}

void LoadSongUI::runFinishSegment() {
#if DELUGE_STREAM_BENCH
	struct SegmentTimer {
		uint64_t start = deluge::runtime::nowUs();
		~SegmentTimer() { deluge::bench::noteLoadPart("finish segment", deluge::runtime::nowUs() - start); }
	} segmentTimer;
#endif
	// The swap's interaction side let go of the knobs (deluge::playback::songSwappedOut()); nothing is under them until
	// the new song's UI is up, so the panel must not go on showing the old song's values. Here rather than at the swap,
	// which an armed launch runs on the audio tier.
	indicator_leds::clearKnobIndicatorLevels();
	for (auto led : indicator_leds::modLed) {
		indicator_leds::setLedState(led, false);
	}
	// To override our popup if we did one. (Still necessary?)
	deluge::hid::display::OLED::displayWorkingAnimation("Loading");
	// The swap's been done and the first tick of the new song has been done; any samples wanting data
	// loaded are drained by the async fill task.

	// Delete the old song
	AudioEngine::logAction("deleting old song");
	if (loadToDelete_ != nullptr) {
		// No action may outlive the song it would revert. The knobs went on adjusting the outgoing song until the swap,
		// and a handler the render preempted at the swap finished on it; the empty UI stack keeps the action logger
		// from recording any of that, and this keeps it so whatever the logger's own rule becomes.
		actionLogger.deleteAllLogs();
		// Handed to the render, which lets go of it before it is freed.
		AudioEngine::retire(loadToDelete_);
		// Wait until it is freed: the retry below loads samples into the RAM the old song gives back.
		AudioEngine::awaitRetired();
		loadToDelete_ = nullptr;
	}

	audioFileManager.deleteAnyTempRecordedSamplesFromMemory();

	// Try one more time to load all AudioFiles - there might be more RAM free now. The pass the arm wait started
	// is ADOPTED rather than replaced: it is already walking this same song, and starting a second walk over it
	// would ask the card for what is on its way. A load that never armed has none, and gets one here.
	if (!restLoad_.has_value()) {
		restLoad_.emplace(deluge::storage::AsyncSampleLoad::forSong(*currentSong, &sampleLoadContext_));
	}
	loadPhase_ = LoadPhase::FINISH;
}

void LoadSongUI::tickFinish() {
#if DELUGE_STREAM_BENCH
	const uint64_t walkStart = deluge::runtime::nowUs();
#endif
	tickRest();
#if DELUGE_STREAM_BENCH
	deluge::bench::noteLoadPart("finish walk", deluge::runtime::nowUs() - walkStart);
	const uint64_t tailStart = deluge::runtime::nowUs();
	struct TailTimer {
		uint64_t start;
		~TailTimer() { deluge::bench::noteLoadPart("finish tail", deluge::runtime::nowUs() - start); }
	};
	std::optional<TailTimer> tail;
#endif
	if (restLoad_.has_value()) {
		return; // still walking
	}
#if DELUGE_STREAM_BENCH
	tail.emplace(TailTimer{tailStart});
#endif
	AudioEngine::logAction("done loading new song");
	deluge::bridge::recordLoadedSounds(*currentSong);
	currentSong->markAllInstrumentsAsEdited();

	sampleLoadContext_ = {};

	PadLEDs::doGreyoutInstantly(); // This will get faded out of just below
	setUIForLoadedSong(currentSong);
	currentUIMode = UI_MODE_NONE;

	display->removeWorkingAnimation();

	endLoad(SongLoadOutcome::Loaded);
}

ActionResult LoadSongUI::timerCallback() {
	// Progress vertical scrolling
	if (currentUIMode == UI_MODE_VERTICAL_SCROLL) {
		PadLEDs::vertical::renderScroll();

		// If we've finished scrolling...
		if (PadLEDs::vertical::squaresScrolled >= kDisplayHeight) {
			// If exiting this UI...
			if (PadLEDs::vertical::scrollDirection == -1) {
				exitThisUI(); // Ideally I don't think this should be allowed to be happen while in the card
				              // routine, which we're in right now...
			}

			// Or if just coming into this UI...
			else {
				// If we've finished scrolling right into the first song preview...
				if (scrollingIntoSlot) {
					currentUIMode = UI_MODE_NONE;
				}

				// Or if we've scrolled half way in...
				else {
					currentUIMode = UI_MODE_WAITING_FOR_NEXT_FILE_TO_LOAD;
					goto getOut;
				}
			}
		}
		else {
			// *2 caused glitches occasionally
			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, UI_MS_PER_REFRESH_SCROLLING * 4);
		}
getOut: {}
		return ActionResult::DEALT_WITH;
	}

	else {
		return LoadUI::timerCallback();
	}
}

void LoadSongUI::scrollFinished() {
	// If we were scrolling out of one song and we got here, we just need to sit back and wait for the next song to
	// load
	if (!scrollingIntoSlot) {
		currentUIMode = UI_MODE_WAITING_FOR_NEXT_FILE_TO_LOAD;
	}

	// Or, if we've finished scrolling into a new song
	else {
		currentUIMode = UI_MODE_NONE;
	}
}

void LoadSongUI::exitActionWithError() {
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_SD_CARD_ERROR));
	exitAction();
}

void LoadSongUI::exitThisUI() {
	currentUIMode = UI_MODE_NONE;
	close();
}

void LoadSongUI::currentFileChanged(int32_t movementDirection) {

	if (movementDirection && !qwertyAlwaysVisible) {
		qwertyVisible = false;
		qwertyCurrentlyDrawnOnscreen = false;

		// Start horizontal scrolling
		PadLEDs::horizontal::setupScroll(movementDirection, kDisplayWidth + kSideBarWidth, true,
		                                 kDisplayWidth + kSideBarWidth);
		for (int32_t i = 0; i < kDisplayHeight; i++) {
			PadLEDs::transitionTakingPlaceOnRow[i] = true;
		}
		currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
		scrollingIntoSlot = false;
		PadLEDs::horizontal::renderScroll(); // The scrolling animation will begin while file is being found and
		                                     // loaded

		// The scroll-in reads imageStore, so it may only start once the preview has actually filled it.
		// After boot this render lands on a later tick, so starting the scroll here would copy two columns
		// (setupScroll ticks once itself, plus the explicit renderScroll) out of an unfilled store --
		// which is what left every entry's first two columns showing the previous song.
		if (drawSongPreview()) {
			beginPreviewScrollIn(movementDirection);
		}
		else {
			pendingScrollDirection_ = movementDirection;
		}
	}
}

void LoadSongUI::beginPreviewScrollIn(int32_t movementDirection) {
	currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
	scrollingIntoSlot = true;

	PadLEDs::horizontal::setupScroll(movementDirection, kDisplayWidth + kSideBarWidth, false,
	                                 kDisplayWidth + kSideBarWidth);
	for (int32_t i = 0; i < kDisplayHeight; i++) {
		PadLEDs::transitionTakingPlaceOnRow[i] = true;
	}
	PadLEDs::horizontal::renderScroll();
}

void LoadSongUI::selectEncoderAction(int8_t offset) {

	if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		armedSelectEncoderAction(offset);
	}

	else {

		if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_HORIZONTAL_SCROLL) {

			LoadUI::selectEncoderAction(offset);
			/*
goAgain:
			qwertyVisible = false;

			// Start horizontal scrolling
			PadLEDs::setupScroll(offset, kDisplayWidth + kSideBarWidth, true, kDisplayWidth + kSideBarWidth);
			for (int32_t i = 0; i < kDisplayHeight; i++) PadLEDs::transitionTakingPlaceOnRow[i] = true;
			currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
			scrollingIntoSlot = false;
			PadLEDs::renderScroll(); // The scrolling animation will begin while file is being found and loaded

			int32_t result = findNextFile(offset);
			if (result) {
			    exitActionWithError();
			    return;
			}

			displayText(false);

			// If user turned knob while finding file, get out so the new action can be done
			if (encoders::select.pending()) {
			    currentUIMode = UI_MODE_HORIZONTAL_SCROLL; // It might have been set to waitingForNextFileToLoad
			    offset = encoders::select.take();
			    goto goAgain;
			}

			drawSongPreview(); // Scrolling continues as the file is read by this function
			currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
			scrollingIntoSlot = true;

			// Set up another horizontal scroll
			PadLEDs::setupScroll(offset, kDisplayWidth + kSideBarWidth, false, kDisplayWidth + kSideBarWidth);
			for (int32_t i = 0; i < kDisplayHeight; i++) PadLEDs::transitionTakingPlaceOnRow[i] = true;
			PadLEDs::renderScroll();
			*/
		}
	}
}

ActionResult LoadSongUI::verticalEncoderAction(int32_t offset) {
	if (!currentUIMode && !Buttons::isButtonPressed(deluge::hid::button::Y_ENC) && !Buttons::isShiftButtonPressed()
	    && offset < 0) {
		exitAction(); // Exit if your scroll down
	}
	if (Buttons::isShiftButtonPressed()) {
		return LoadUI::verticalEncoderAction(offset);
	}
	return ActionResult::DEALT_WITH;
}

void LoadSongUI::exitAction() {

	// If parts of the old song have been deleted, sorry, there's no way we can exit without loading a new song
	if (deletedPartsOfOldSong) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_UNLOADED_PARTS));
		return;
	}

	// We are leaving. Cancelled only past the refusal above, since that path stays in the browser with
	// the same entry selected and the preview it is waiting for is still the one it wants. From here
	// the scroll-out overwrites imageStore immediately, and no replacement preview is asked for, so a
	// result landing afterwards would paint the departing song over the view being scrolled back in.
	cancelPreview();

	currentUIMode = UI_MODE_VERTICAL_SCROLL;
	PadLEDs::vertical::setupScroll(-1, false);
	getRootUI()->renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
	getRootUI()->renderSidebar(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
	//((ViewScreen*)getRootUI())->renderToStore(0, true);
	timerCallback();
}

deluge::gui::CancelResult LoadSongUI::cancelLoad() {
	using deluge::gui::CancelResult;
	switch (loadPhase_) {
	case LoadPhase::NONE:
		return CancelResult::Stale;
	case LoadPhase::STAGE:
		// The staged bytes are all the load holds, and the deserializer is not pointed at them until the phase
		// ends, so endLoad()'s release is the whole of the undo.
		display->removeWorkingAnimation();
		endLoad(SongLoadOutcome::NotLoaded);
		return CancelResult::Cancelled;
	default:
		return CancelResult::Refused;
	}
}

ActionResult LoadSongUI::backButtonAction() {
	// BACK while a load is in flight means that load, not the browser: dropped while it is still staging, and
	// otherwise ignored, since leaving the browser or relisting its folder under a load would pull the UI out from
	// under it.
	switch (cancelLoad()) {
	case deluge::gui::CancelResult::Cancelled:
	case deluge::gui::CancelResult::Refused:
		return ActionResult::DEALT_WITH;
	case deluge::gui::CancelResult::Stale:
		break;
	}

	// Back either leaves the browser or goes up a directory. Either way the song this was hovering is
	// no longer the selection, and neither outcome asks for a replacement preview to supersede one
	// already being read -- the same shape as the folder branch in drawSongPreview(). Going up
	// re-selects through folderContentsReady(), which asks afresh.
	cancelPreview();

	return Browser::backButtonAction();
}

bool LoadSongUI::drawSongPreview(bool toStore) {
	FileItem* currentFileItem = getCurrentFileItem();

	// Nothing to preview. Whatever is being read is no longer what the user is on, and this branch
	// asks for no replacement to supersede it, so it has to be cancelled outright -- or it lands a
	// song's artwork over a folder. There is also nothing to wait for, so the image is blanked and
	// released to the caller's scroll right here.
	if (currentFileItem == nullptr || currentFileItem->isFolder) {
		// The direction parked for the preview being abandoned goes with it (cancelPreview clears it),
		// so it cannot fire later on top of the scroll the caller is about to start.
		cancelPreview();
		(void)beginPreviewRender(toStore);
		return true; // blank, but FILLED: a scroll may start from it now
	}

	dispatchPreview(PreviewTarget{listing.dir() + "/" + currentFileItem->getFilenameWithExtension(), toStore});
	return false; // imageStore is NOT filled yet -- the caller must not scroll from it
}

void LoadSongUI::dispatchPreview(const PreviewTarget& target) {
	if (!previewLoad_.request(target)) {
		return; // work is already out on an older target; it picks this one up on completion
	}
	drivePreview();
}

void LoadSongUI::drivePreview() {
	if (!deluge::boot::finished()) {
		// Read the file inline. During boot -- which in the unit tests, with no Rust prefix channel linked, is
		// always -- nothing would ever answer a request. After boot this arm would hold the interaction tier for
		// the whole read
		// (the efatfs entry points do not yield), which is why it is noted as a tier violation, and why it is
		// unreachable there.
		for (;;) {
			const auto attempt = previewLoad_.attempt();
			deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Browser);
			renderPreviewFromFile(attempt.target);
			if (!previewLoad_.complete(attempt, /*deferred=*/false).has_value()) {
				return;
			}
		}
	}

	// The prefix channel never touches the card on this side, so the collect runs inline on whatever
	// tier called us -- nothing is dispatched. The loop is for latest-wins hand-off: complete() returns
	// a target only if a newer one queued while this one ran, and nothing reachable from a collect asks
	// for a preview, so in practice it runs once.
	for (;;) {
		// No is_current() guard: there is no gap here for a cancellation to land in. request() (or the
		// previous complete()) made this target current on the line above, and the collect runs to
		// completion before anything else can execute. (Slicer::drivePreview()'s matching inline arm keeps
		// the guard as an invariant restatement with the same unreachable false branch -- the same
		// judgement written from the other end.) What refuses a cancelled attempt is complete()'s
		// `deferred && is_current(a)`, below.
		const auto attempt = previewLoad_.attempt();
		const bool deferred = collectPreview(attempt);
		if (!previewLoad_.complete(attempt, deferred).has_value()) {
			return;
		}
	}
}

bool LoadSongUI::collectPreview(const deluge::storage::DeferredLoad<PreviewTarget>::Attempt& attempt) {
	if (attempt.generation != previewWaitGeneration_ || attempt.target.path != previewWaitPath_) {
		// A different attempt: its wait starts from nothing.
		previewWaitGeneration_ = attempt.generation;
		previewWaitPath_ = attempt.target.path;
		previewWait_.reset();
	}

	uint32_t length = 0;
	const DelugeChannelStatus status =
	    deluge_file_prefix_collect(attempt.target.path.c_str(), songPreviewPrefix.data(),
	                               static_cast<uint32_t>(songPreviewPrefix.size()), &length);

	if (status == DELUGE_CHANNEL_PENDING || status == DELUGE_CHANNEL_UNKNOWN) {
		if (!previewWait_.tick()) {
			if (status == DELUGE_CHANNEL_UNKNOWN) {
				// "The caller should request it (again)" -- the documented meaning, and never a failure to
				// report. This is also where the FIRST request for a target is issued: drawSongPreview() names
				// a song the channel has not been told about, so its opening drive lands here.
				deluge_file_prefix_request(attempt.target.path.c_str(), deluge::storage::kSongPreviewWindow);
			}
			return true; // requested or being read: ask again next tick
		}
		// The channel has left this read unanswered for the whole bound. Give up as a read that could not reach the
		// card: blank, with the card error, and the scroll-in the cursor move owed released rather than left parked.
		// Nothing to clean up: the channel copied the path, and a late answer is displaced by the next publish. The
		// next visit to this song asks again.
		previewWait_.reset();
		PreviewImage imageStore = beginPreviewRender(attempt.target.toStore);
		if (imageStore != nullptr) {
			reportPreviewOutcome(deluge::storage::SongPreviewOutcome::Error, Error::SD_CARD);
		}
		finishPreviewRender(attempt.target.toStore);
		return false;
	}

	previewWait_.reset(); // answered: READY or FAILED
	PreviewImage imageStore = beginPreviewRender(attempt.target.toStore);

	if (status == DELUGE_CHANNEL_FAILED) {
		// The read did not happen: the file would not open, could not be sized, or the read failed.
		// Terminal, so this is NOT re-requested -- and reported, because the file backing reports the
		// same thing for the same song and the two must not disagree about whether the user hears
		// about it.
		//
		// Error::FILE_NOT_FOUND specifically, which is what the file backing's own open reports for
		// EVERY failed open (the staged opener maps busy and unreadable onto it too). The
		// channel does not say which failure it was; neither does the path this stands in for, so the
		// two arms conflate identically rather than differently.
		if (imageStore != nullptr) {
			reportPreviewOutcome(deluge::storage::SongPreviewOutcome::Error, Error::FILE_NOT_FOUND);
		}
		finishPreviewRender(attempt.target.toStore);
		return false;
	}

	// READY, the only status left, and the only one that writes the buffer.
	//
	// A length of 0 is not a failure: a failed read comes back as FAILED above, so zero bytes mean an
	// EMPTY FILE, which is not a song and which the decode below reports as Error -- exactly what the
	// file backing makes of the same file.
	if (imageStore != nullptr) {
		Error error = Error::NONE;
		const auto outcome =
		    deluge::storage::decodeSongPreview(std::span<uint8_t>{songPreviewPrefix.data(), length},
		                                       isJsonSongPath(attempt.target.path), imageStore, &error);
		reportPreviewOutcome(outcome, error);
	}
	finishPreviewRender(attempt.target.toStore);
	return false;
}

void LoadSongUI::renderPreviewFromFile(const PreviewTarget& target) {
	PreviewImage imageStore = beginPreviewRender(target.toStore);
	if (imageStore != nullptr) {
		// A blocking read that failed is a real failure about this file, not the storage tier declining
		// to answer -- worth showing.
		Error error = Error::NONE;
		const auto outcome = deluge::storage::decodeSongPreviewFromFile(target.path.c_str(), imageStore, &error);
		reportPreviewOutcome(outcome, error);
	}
	finishPreviewRender(target.toStore);
}

void LoadSongUI::reportPreviewOutcome(deluge::storage::SongPreviewOutcome outcome, Error error) {
	lastPreviewOutcome_ = outcome;
	lastPreviewError_ = (outcome == deluge::storage::SongPreviewOutcome::Error) ? error : Error::NONE;
	if (outcome == deluge::storage::SongPreviewOutcome::Error) {
		display->displayError(error);
	}
}

LoadSongUI::PreviewImage LoadSongUI::beginPreviewRender(bool toStore) {
	if (qwertyAlwaysVisible) {
		return nullptr; // the keyboard owns the pads, so nothing may be drawn over it
	}

	PreviewImage imageStore = toStore ? PadLEDs::imageStore : PadLEDs::image;
	memset(imageStore, 0, kDisplayHeight * (kDisplayWidth + kSideBarWidth) * sizeof(RGB));
	return imageStore;
}

void LoadSongUI::finishPreviewRender(bool toStore) {
	// imageStore is filled now, so a scroll-in owed by currentFileChanged can finally start. Consume the
	// direction before starting, so a scroll begun here cannot be started twice if the render re-runs.
	if (pendingScrollDirection_ != 0) {
		const int32_t direction = pendingScrollDirection_;
		pendingScrollDirection_ = 0;
		beginPreviewScrollIn(direction);
		return; // the scroll brings the preview in
	}
	if (qwertyAlwaysVisible) {
		return; // the keyboard owns the pads, and nothing was drawn
	}
	if (toStore) {
		if (currentUIMode == UI_MODE_VERTICAL_SCROLL || currentUIMode == UI_MODE_HORIZONTAL_SCROLL) {
			return; // the scroll in progress brings the preview in
		}
		// The preview landed after the scroll had taken its rows: show it as the scroll would have.
		memcpy(PadLEDs::image, PadLEDs::imageStore, sizeof(PadLEDs::image));
	}
	// Only this UI's own pads: a whole-UI repaint would draw the view underneath's sidebar over the browser.
	PadLEDs::sendOutMainPadColours();
	PadLEDs::sendOutSidebarColours();
}

void LoadSongUI::browserGraphicsRoutine() {
	// Re-ask for a preview whose bytes have not arrived.
	//
	// take_pending() takes the target OUT before dispatching, not after: the drive runs INLINE from
	// here (a collect touches no card), so it completes before dispatchPreview() returns and may arm a
	// SECOND deferral -- which clearing afterwards would wipe, leaving the preview to never finish.
	if (auto target = previewLoad_.take_pending(); target.has_value()) {
		dispatchPreview(*target);
	}
	// No UI::graphicsRoutine() tail: Browser::graphicsRoutine() makes that call, once, after this
	// returns. Repeating it here would run the base tick twice per frame.
}

void LoadSongUI::displayText(bool blinkImmediately) {

	LoadUI::displayText();

	if (qwertyVisible && !qwertyCurrentlyDrawnOnscreen) {
		FileItem* currentFileItem = getCurrentFileItem();

		drawKeys();
		qwertyCurrentlyDrawnOnscreen = true;
		PadLEDs::sendOutSidebarColours();
	}
}

ActionResult LoadSongUI::padAction(int32_t x, int32_t y, int32_t on) {
	// If QWERTY not visible yet, make it visible now
	if (!qwertyVisible) {
		if (on && !currentUIMode) {
			qwertyVisible = true;
			displayText(false); // This will also draw the QWERTY keys

			// Process first press only if its not a favourite row press to prevent blind keypresses
			if (y < favouriteRow) {
				return LoadUI::padAction(x, y, on);
			}
		}
	}

	// Only process the QWERTY keypress if Keyboard is visible to prevent blind keypresses
	else if (qwertyVisible) {
		return LoadUI::padAction(x, y, on);
	}
	return ActionResult::DEALT_WITH;
}
