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
#include "libdeluge/file_prefix.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/instrument/midi_instrument.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "scheduler_api.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/owner.h" // deluge::storage::Owner::run — run the load on the worker fiber
#include "storage/song_preview.h"
#include "storage/storage_manager.h"
#include "util/try.h"

#include <array>
#include <cstdint>
#include <span>
#include <string.h>

LoadSongUI loadSongUI{};

extern void songLoaded(Song* song);
extern void setUIForLoadedSong(Song* song);
extern void setupBlankSong();

using namespace deluge::gui;
namespace encoders = deluge::hid::encoders;

/// How long a load waits for the sample loader to settle before swapping anyway. Carried over from the
/// bounded wait this replaced, so a card that never settles still lets the song through.
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
/// std::optionals next to it this does not lean on resetprg.c's emptySection() boot memset.
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

/// @brief Whether a song path names a JSON document, decided exactly as openDelugeFile() decides it.
///
/// A buffer has no filename for the reader to infer the format from, so the caller has to say. The
/// rule is `.Json` anywhere in the path -- see StorageManager::openDelugeFile() in file_reader.cpp,
/// which is the behaviour the file-backed decode gets for free and this one has to reproduce.
bool isJsonSongPath(const std::string& path) {
	return path.find(".Json") != std::string::npos;
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
	outputTypeToLoad = OutputType::NONE;
	currentDir = currentSong->dirPath;

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
	searchFilename = currentSong->name;

	if (!searchFilename.empty() && !searchFilename.contains(".XML")) {
		searchFilename.append(".XML");
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
	// landed yet is harmless -- finishPreviewRender()'s renderingNeededRegardlessOfUI() repaints once
	// it does.
	(void)drawSongPreview(currentUIMode == UI_MODE_VERTICAL_SCROLL);

	PadLEDs::sendOutMainPadColours();
	PadLEDs::sendOutSidebarColours();
}

void LoadSongUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();

	// If it's a directory...
	if (currentFileItem && currentFileItem->isFolder) {
		// The song this was hovering is not in the folder we are entering, and nothing asks for a
		// replacement until the listing completes -- so without this, a preview landing in between
		// paints the departed folder's song over the new one's first entry. Same shape as Back.
		cancelPreview();
		// goIntoFolder() dispatches onto the storage owner; failure is handled by the base
		// Browser::onListingFailed() (displayError + exitAction) once the listing completes.
		goIntoFolder(currentFileItem->filename.c_str());
	}

	else {
		// Start the load and return: it owns its own dispatch onto the storage owner, and parks in the
		// run loop at each point it has to wait, so this handler never blocks. The post-load settings
		// write travels with it as the completion.
		beginLoad([]() {
			if (FlashStorage::defaultStartupSongMode == StartupSongMode::LASTOPENED) {
				runtimeFeatureSettings.writeSettingsToFile();
			}
		});
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

ActionResult LoadSongUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (listingInProgress_) {
		return ActionResult::DEALT_WITH;
	}

	// Load button or select encoder press. Unlike most (all?) other children of Browser, we override this and don't
	// just call mainButtonAction(), because unlike all the others, we need to action the load immediately on down-press
	// rather than waiting for press-release, because of that special action where you hold the button down until you
	// want to "launch" the new song.
	if ((b == LOAD) || (b == SELECT_ENC)) {
		if (on) {
			if (!currentUIMode) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				enterKeyPress();
			}
		}
		else {
			// If all essential samples are already loaded, we can arm right away
			if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED) {
				bool result = session.armForSongSwap();

				// If arming couldn't really be done, and song has already swapped...
				if (!result) {
					currentUIMode = UI_MODE_LOADING_SONG_NEW_SONG_PLAYING;
				}
				else {
					currentUIMode = UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED;
					displayArmedPopup();
				}
			}
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
		return LoadUI::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
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
	currentDir = currentSong->dirPath;

	int32_t currentFileIndexSelected = fileIndexSelected;

	bool songFound = false;
	do {
		fileIndexSelected = fileIndexSelected + offset;
		if (fileIndexSelected < 0) {
			fileIndexSelected = static_cast<int32_t>(fileItems.size()) - 1;
		}
		else if (fileIndexSelected >= static_cast<int32_t>(fileItems.size())) {
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
				beginLoad([]() {
					if (FlashStorage::defaultStartupSongMode == StartupSongMode::LASTOPENED) {
						runtimeFeatureSettings.writeSettingsToFile();
					}
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

void LoadSongUI::startLoadTrampoline(void*) {
	loadSongUI.startLoad();
}

void LoadSongUI::armOrSwapTrampoline(void*) {
	loadSongUI.armOrSwap();
}

void LoadSongUI::finishLoadTrampoline(void*) {
	loadSongUI.finishLoad();
}

void LoadSongUI::dispatchLoadSegment(void (*segment)(void*)) {
	LoadPhase parked = loadPhase_;
	loadPhase_ = LoadPhase::RUNNING;
	if (!deluge::storage::Owner::run(segment, nullptr)) {
		// Owner queue was full. Stay parked in the phase we came from and try again next pass -
		// the wait we just satisfied stays satisfied, so this only costs a pass.
		loadPhase_ = parked;
	}
}

void LoadSongUI::endLoad() {
	loadPhase_ = LoadPhase::NONE;
	loadToDelete_ = nullptr;
	performingLoad = false;

	// Cleared before the call so a completion that starts another load doesn't inherit this one's.
	void (*onComplete)() = loadOnComplete_;
	loadOnComplete_ = nullptr;
	if (onComplete != nullptr) {
		onComplete();
	}
}

// Before calling this, you must set loadButtonReleased.
void LoadSongUI::beginLoad(void (*onComplete)()) {
	// The browser is on its way out and the pads now belong to the load: the greyout, the loading
	// animation, and finally the new song's view. Nothing here asks for a replacement preview, so an
	// artwork read still outstanding would paint over all of that. It also frees the storage owner
	// from a preview op the synchronous backing may still have queued ahead of the load.
	cancelPreview();

	loadOnComplete_ = onComplete;
	performingLoad = true;

	// The load reads the card, so all three of its stretches run on the storage owner - including this
	// first one, which is why the call is dispatched rather than made here. Callers reach this from the
	// interaction tier (a button press, the startup task), where card access is not allowed.
	//
	// A full owner queue parks the load in START rather than abandoning it. Finishing here instead would
	// mean running the caller's completion - which writes to the card - on this interaction-tier context.
	loadPhase_ = LoadPhase::START;
	dispatchLoadSegment(&startLoadTrampoline);
}

void LoadSongUI::loadRoutine() {
	switch (loadPhase_) {
	case LoadPhase::NONE:
	case LoadPhase::RUNNING:
		return;

	case LoadPhase::START:
		dispatchLoadSegment(&startLoadTrampoline);
		return;

	case LoadPhase::SETTLE:
		// The loader queue holding no lowest-priority (failed-and-requeued) items means the async fill
		// task has settled. Observing that from here, rather than waiting on it, is the point: the fill
		// task only makes progress once we return to the run loop.
		if (deluge_resource_loader_has_lowest(GeneralMemoryAllocator::get().resourceManager())
		    && getSystemTime() < settleDeadline_) {
			return;
		}
		dispatchLoadSegment(&armOrSwapTrampoline);
		return;

	case LoadPhase::AWAIT_ARM:
		if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED
		    || currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED) {
			return;
		}
		dispatchLoadSegment(&finishLoadTrampoline);
		return;
	}
}

void LoadSongUI::startLoad() {
	FileItem* currentFileItem = getCurrentFileItem();

	if (!currentFileItem) {
		display->displayError(Error::FILE_NOT_FOUND);
		endLoad();
		return;
	}

	actionLogger.deleteAllLogs();

	if (arrangement.hasPlaybackActive()) {
		playbackHandler.switchToSession();
	}
	Error error;

	// Saved before the open, and put back at every exit below. openDocumentSynchronously() leaves
	// activeDeserializer pointing at the memory reader on EVERY path including the failed one, and
	// that reader's fileClusterBuffer IS the staged buffer released here -- code downstream writes
	// through the global without opening a document of its own (SaveSongUI's sample collection reads
	// Cluster::size, 32 KiB, into it), so leaving it pointing at freed bytes is heap corruption rather
	// than a stale read. Same discipline decodeSongPreview() documents.
	FileDeserializer* const previousDeserializer = activeDeserializer;

	std::string filePath = currentDir + "/" + currentFileItem->getFilenameWithExtension();
	error = StorageManager::openDocumentSynchronously(filePath.c_str(), "song");

	currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	indicator_leds::setLedState(IndicatorLED::LOAD, false);
	indicator_leds::setLedState(IndicatorLED::BACK, false);

	display->displayLoadingAnimationText("Loading");
	nullifyUIs();

	deletedPartsOfOldSong = true;

	// If not currently playing, don't load both songs at once (this avoids any RAM overfilling, fragmentation etc.)
	if (!playbackHandler.isEitherClockActive()) {
		// Otherwise, a timer might get called and try to access Clips that we may have deleted below (really?)
		uiTimerManager.unsetTimer(TimerName::PLAY_ENABLE_FLASH);

		deleteOldSongBeforeLoadingNew();
	}
	else {
		// Note: this is dodgy, but in this case we don't reset view.activeControllableClip here - we let the user keep
		// fiddling with it. It won't get deleted.
		AudioEngine::logAction("arming for song swap");
		AudioEngine::songSwapAboutToHappen();
		AudioEngine::logAction("song swap armed");
		playbackHandler.songSwapShouldPreserveTempo = Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC);
	}

	void* songMemory = deluge::memory::alloc_fast(sizeof(Song));
	if (!songMemory) {
ramError:
		error = Error::INSUFFICIENT_RAM;

someError:
		display->displayError(error);
		activeDeserializer->closeWriter();
		StorageManager::releaseDocumentStaging();
		activeDeserializer = previousDeserializer;
fail:
		// We couldn't load the requested song. Recover to a usable state and stop.
		//
		// The original code reconstructed a Song into `songMemory` here and loaded it (without ever swapping it in),
		// which is broken under memory pressure: songMemory is null if its initial allocation failed, or already freed
		// if we arrived via block B's error cleanup (gotErrorAfterCreatingSong) - and setupBlankSong() below can
		// reallocate that very freed block as the new currentSong, so reconstructing into (and later freeing)
		// songMemory left currentSong dangling, crashing the next load.
		if (preLoadedSong && preLoadedSong != currentSong) {
			// A half-loaded song may still occupy songMemory (e.g. we arrived from a closeWriter failure) - free it.
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

		display->removeWorkingAnimation();
		currentUIMode = UI_MODE_NONE;
		endLoad();
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

	// Will return false if we ran out of RAM. This isn't currently detected for while loading ParamNodes, but chances
	// are, after failing on one of those, it'd try to load something else and that would fail.
	error = preLoadedSong->readFromFile(*activeDeserializer);
	if (error != Error::NONE) {
		goto gotErrorAfterCreatingSong;
	}
	AudioEngine::logAction("read new song from file");

	bool success = activeDeserializer->closeWriter();
	// Nothing reads the staged bytes once the reader is closed.
	StorageManager::releaseDocumentStaging();
	activeDeserializer = previousDeserializer;
	if (!success) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_ERROR_LOADING_SONG));
		goto fail;
	}

	preLoadedSong->dirPath = currentDir;

	std::string currentFilenameWithoutExtension = currentFileItem->getFilenameWithoutExtension();

	error = audioFileManager.setupAlternateAudioFileDir(audioFileManager.alternateAudioFileLoadPath, currentDir.c_str(),
	                                                    currentFilenameWithoutExtension.c_str());
	if (error != Error::NONE) {
		goto gotErrorAfterCreatingSong;
	}

	audioFileManager.thingBeginningLoading(ThingType::SONG);

	// Search existing RAM for all samples, to lay a claim to any which will be needed for this new Song.
	// Do this before loading any new Samples from file, in case we were in danger of discarding any from RAM that we
	// might actually want
	preLoadedSong->loadAllSamples(false);

	// Load samples from files, just for currently playing Sounds (or if not playing, then all Sounds)
	if (playbackHandler.isEitherClockActive()) {
		preLoadedSong->loadCrucialSamplesOnly();
	}
	else {
		preLoadedSong->loadAllSamples(true);
	}

	// The new song's essential clusters still have to finish arriving before the swap. Park here and let
	// loadRoutine() watch for it: the fill task that does the arriving cannot run while we hold the owner.
	settleDeadline_ = getSystemTime() + kLoadSettleTimeoutSeconds;
	loadPhase_ = LoadPhase::SETTLE;
}

void LoadSongUI::armOrSwap() {
	preLoadedSong->name = enteredText;

	loadToDelete_ = currentSong;

	if (playbackHandler.isEitherClockActive()) {

		// If load button was already released while that loading was happening, arm for song-swap now
		if (!Buttons::isButtonPressed(deluge::hid::button::LOAD)) {
			bool result = session.armForSongSwap();

			// If arming couldn't really be done, e.g. because current song had no Clips currently playing, swap has
			// already occurred
			if (!result) {
				finishLoad();
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
		preLoadedSong->loadAllSamples(true);
		AudioEngine::logAction("waiting for samples");
		// Park until currentUIMode leaves the two loading modes - the arming, or the swap the arming
		// launches, is what moves it, and neither can happen while we sit on the owner.
		loadPhase_ = LoadPhase::AWAIT_ARM;
		return;
	}

	playbackHandler.doSongSwap();
	finishLoad();
}

void LoadSongUI::finishLoad() {
	// To override our popup if we did one. (Still necessary?)
	deluge::hid::display::OLED::displayWorkingAnimation("Loading");
	// The swap's been done and the first tick of the new song has been done; any samples wanting data
	// loaded are drained by the async fill task.

	// Delete the old song
	AudioEngine::logAction("deleting old song");
	if (loadToDelete_ != nullptr) {
		void* toDealloc = dynamic_cast<void*>(loadToDelete_);
		loadToDelete_->~Song();
		delugeDealloc(toDealloc);
		loadToDelete_ = nullptr;
	}

	audioFileManager.deleteAnyTempRecordedSamplesFromMemory();

	// Try one more time to load all AudioFiles - there might be more RAM free now
	currentSong->loadAllSamples();
	AudioEngine::logAction("done loading new song");
	currentSong->markAllInstrumentsAsEdited();

	audioFileManager.thingFinishedLoading();

	PadLEDs::doGreyoutInstantly(); // This will get faded out of just below
	setUIForLoadedSong(currentSong);
	currentUIMode = UI_MODE_NONE;

	display->removeWorkingAnimation();

	// for the song we just loaded, let's check if there's any midi labels we should load
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput && thisOutput->type == OutputType::MIDI_OUT) {
			MIDIInstrument* midiInstrument = (MIDIInstrument*)thisOutput;
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
	}

	endLoad();
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
		// Off the owner this render is DISPATCHED, so starting the scroll here would copy two columns
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
		session.numRepeatsTilLaunch += offset;
		if (session.numRepeatsTilLaunch < 1) {
			session.numRepeatsTilLaunch = 1;
		}
		else if (session.numRepeatsTilLaunch > 9999) {
			session.numRepeatsTilLaunch = 9999;
		}
		// renderUIsForOled();
		displayLoopsRemainingPopup();
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

ActionResult LoadSongUI::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (!currentUIMode && !Buttons::isButtonPressed(deluge::hid::button::Y_ENC) && !Buttons::isShiftButtonPressed()
	    && offset < 0) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		exitAction(); // Exit if your scroll down
	}
	if (Buttons::isShiftButtonPressed()) {
		return LoadUI::verticalEncoderAction(offset, false);
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

ActionResult LoadSongUI::backButtonAction() {
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

	dispatchPreview(PreviewTarget{currentDir + "/" + currentFileItem->getFilenameWithExtension(), toStore});
	return false; // imageStore is NOT filled yet -- the caller must not scroll from it
}

void LoadSongUI::dispatchPreview(const PreviewTarget& target) {
	if (!previewLoad_.request(target)) {
		return; // work is already out on an older target; it picks this one up on completion
	}
	drivePreview();
}

void LoadSongUI::drivePreview() {
	if (!deluge_file_prefix_async_active()) {
		// No prefix channel on this BSP/config, so the bytes have to be read the blocking way -- which
		// belongs on the storage owner. drawSongPreview() is reached from the scroll path on the
		// interaction tier, and reading there is wrong for two reasons that both still hold. Single
		// ownership: FatFS is not reentrant and the owner is the one serialised context for it.
		// Responsiveness: on the Rust BSP the efatfs entry points HOLD the calling context for the
		// whole transfer rather than yielding it (src/bsp/rust/src/efatfs_fs.rs), so a read run here
		// stalls the UI for as long as the card takes.
		//
		// NOT because the BSP refuses an off-owner open -- it used to, and that refusal (surfaced by
		// openXMLFile() as Error::FILE_NOT_FOUND) is how every scrolled entry once claimed "FILE NOT
		// FOUND" for songs that load fine, but those on_fiber() guards were deliberately removed
		// precisely because they made off-owner work fail. Off-owner reads now SUCCEED, slowly and
		// unserialised, which is why this dispatch has to be a discipline rather than a thing the
		// platform enforces -- and why Slicer::drivePreview() needs the same arm for the same reason.
		// Dispatch and let the owner render.
		if (!deluge::storage::Owner::run(&LoadSongUI::runPreviewOp, this)) {
			// The owner queue was full, so the op will never run and would wedge single-flight forever.
			// Release the guard and leave the target for graphicsRoutine() to ask for again.
			previewLoad_.release_dispatch();
		}
		return;
	}

	// The prefix channel never touches the card on this side, so the collect runs inline on whatever
	// tier called us -- no dispatch, no owner. The loop is for latest-wins hand-off: complete() returns
	// a target only if a newer one queued while this one ran, and nothing reachable from a collect asks
	// for a preview, so in practice it runs once.
	for (;;) {
		// No is_current() guard, unlike runPreviewOp(): there is no gap here for a cancellation to land
		// in. request() (or the previous complete()) made this target current on the line above, and
		// the collect runs to completion before anything else can execute. (Slicer::drivePreview()'s
		// matching inline arm keeps the guard as an invariant restatement with the same unreachable
		// false branch -- the two are the same judgement written from opposite ends, not a
		// disagreement. Both keep it for real on their owner arm, where a queued dispatch genuinely
		// can run after a cancellation.) What refuses a cancelled attempt on both is complete()'s
		// `deferred && is_current(a)`, below.
		const auto attempt = previewLoad_.attempt();
		const bool deferred = collectPreview(attempt);
		if (!previewLoad_.complete(attempt, deferred).has_value()) {
			return;
		}
	}
}

bool LoadSongUI::collectPreview(const deluge::storage::DeferredLoad<PreviewTarget>::Attempt& attempt) {
	uint32_t length = 0;
	const uint8_t status = deluge_file_prefix_collect(attempt.target.path.c_str(), songPreviewPrefix.data(),
	                                                  static_cast<uint32_t>(songPreviewPrefix.size()), &length);

	if (status == DELUGE_FILE_PREFIX_PENDING) {
		return true; // requested or being read: ask again next tick, and do NOT re-request
	}
	if (status == DELUGE_FILE_PREFIX_UNKNOWN) {
		// "The caller should request it (again)" -- the documented meaning, and never a failure to
		// report. This is also where the FIRST request for a target is issued: drawSongPreview() names
		// a song the channel has not been told about, so its opening drive lands here.
		deluge_file_prefix_request(attempt.target.path.c_str(), deluge::storage::kSongPreviewWindow);
		return true;
	}

	PreviewImage imageStore = beginPreviewRender(attempt.target.toStore);

	if (status == DELUGE_FILE_PREFIX_FAILED) {
		// The read did not happen: the file would not open, could not be sized, or the read failed.
		// Terminal, so this is NOT re-requested -- and reported, because the file backing reports the
		// same thing for the same song and the two must not disagree about whether the user hears
		// about it.
		//
		// Error::FILE_NOT_FOUND specifically, which is what the file backing's own open reports for
		// EVERY failed open (StorageManager::openXMLFile maps busy and unreadable onto it too). The
		// channel does not say which failure it was; neither does the path this stands in for, so the
		// two arms conflate identically rather than differently.
		if (imageStore != nullptr) {
			reportPreviewOutcome(deluge::storage::SongPreviewOutcome::Error, Error::FILE_NOT_FOUND);
		}
		finishPreviewRender();
		return false;
	}

	// READY, the only status left, and the only one that writes the buffer.
	//
	// A length of 0 is NOT a failure any more: a failed read comes back as FAILED above, so zero bytes
	// now means an EMPTY FILE, which is not a song and which the decode below reports as Error --
	// exactly what the file backing makes of the same file.
	if (imageStore != nullptr) {
		Error error = Error::NONE;
		const auto outcome =
		    deluge::storage::decodeSongPreview(std::span<uint8_t>{songPreviewPrefix.data(), length},
		                                       isJsonSongPath(attempt.target.path), imageStore, &error);
		reportPreviewOutcome(outcome, error);
	}
	finishPreviewRender();
	return false;
}

void LoadSongUI::runPreviewOp(void* self) {
	auto* ui = static_cast<LoadSongUI*>(self);
	const auto attempt = ui->previewLoad_.attempt();

	// Checked BEFORE the work, not merely before recording its result: between the dispatch and this
	// running, the cursor can have moved somewhere with no preview at all (a folder, an empty list, or
	// out of the browser), which asks for no replacement and so supersedes nothing. Painting the song
	// the cursor has left is the damage.
	if (ui->previewLoad_.is_current(attempt)) {
		ui->renderPreviewFromFile(attempt.target);
	}

	// Never deferred: this backing blocks until it has the answer. If a newer target arrived while the
	// read ran, it is dispatched instead (latest-wins) -- which is what the hand-rolled previewPending_
	// catch-up used to do, badly, because Coalescer::request() had dropped that newer target outright.
	if (ui->previewLoad_.complete(attempt, /*deferred=*/false).has_value()
	    && !deluge::storage::Owner::run(&LoadSongUI::runPreviewOp, self)) {
		ui->previewLoad_.release_dispatch(); // re-dispatch dropped (see drivePreview)
	}
}

void LoadSongUI::renderPreviewFromFile(const PreviewTarget& target) {
	PreviewImage imageStore = beginPreviewRender(target.toStore);
	if (imageStore != nullptr) {
		// We are on the owner, so a failure here is a real one about this file, not the storage tier
		// declining to answer -- worth showing.
		Error error = Error::NONE;
		const auto outcome = deluge::storage::decodeSongPreviewFromFile(target.path.c_str(), imageStore, &error);
		reportPreviewOutcome(outcome, error);
	}
	finishPreviewRender();
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

void LoadSongUI::finishPreviewRender() {
	// imageStore is filled now, so a scroll-in owed by currentFileChanged can finally start. Consume the
	// direction before starting, so a scroll begun here cannot be started twice if the render re-runs.
	if (pendingScrollDirection_ != 0) {
		const int32_t direction = pendingScrollDirection_;
		pendingScrollDirection_ = 0;
		beginPreviewScrollIn(direction);
	}
	// We rendered outside the normal UI render pass, so ask for a repaint - otherwise the pads keep
	// whatever was on them until something else happens to trigger one.
	renderingNeededRegardlessOfUI();
}

void LoadSongUI::browserGraphicsRoutine() {
	// Re-ask for a preview whose bytes have not arrived.
	//
	// take_pending() takes the target OUT before dispatching, not after: the drive runs INLINE from
	// here (a collect touches no card), so it completes before dispatchPreview() returns and may arm a
	// SECOND deferral -- which clearing afterwards would wipe, leaving the preview to never finish. A
	// dispatch the storage owner could not take re-arms it itself (drivePreview -> release_dispatch),
	// so it is asked for again next tick rather than dropped.
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
			if (isSDRoutineActive()) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
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
