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
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/l10n/l10n.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "task_scheduler.h"
#include <string.h>

LoadSongUI loadSongUI{};

extern void songLoaded(Song* song);
extern void setUIForLoadedSong(Song* song);
extern "C" {
void routineForSD(void);
}
extern void setupBlankSong();

using namespace deluge::gui;

LoadSongUI::LoadSongUI() {
	qwertyAlwaysVisible = false;
	filePrefix = "SONG";
	title = "Load song";
}

bool LoadSongUI::opened() {

	outputTypeToLoad = OutputType::NONE;
	currentDir.set(&currentSong->dirPath);

	Error error = beginSlotSession(false, true);
	if (error != Error::NONE) {
gotError:
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

	String searchFilename;
	searchFilename.set(&currentSong->name);

	if (!searchFilename.isEmpty() && !searchFilename.contains(".XML")) {
		error = searchFilename.concatenate(".XML");
		if (error != Error::NONE) {
			goto gotError;
		}
	}

	error = arrivedInNewFolder(0, searchFilename.get(), "SONGS");
	if (error != Error::NONE) {
		goto gotError;
	}

#if SD_TEST_MODE_ENABLED_LOAD_SONGS
	currentSlot = (currentSlot + 1) % 19;
	currentSubSlot = (currentSlot == 0) ? 0 : -1;
#endif

	focusRegained();

	PadLEDs::vertical::setupScroll(1, false);
	scrollingIntoSlot = true;

	if (currentUIMode != UI_MODE_VERTICAL_SCROLL) {
		currentUIMode =
		    UI_MODE_VERTICAL_SCROLL; // Have to reset this again - it might have finished the first bit of the scroll
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

	return true;
}

void LoadSongUI::folderContentsReady(int32_t entryDirection) {

	drawSongPreview(storageManager, currentUIMode == UI_MODE_VERTICAL_SCROLL);

	PadLEDs::sendOutMainPadColours();
	PadLEDs::sendOutSidebarColours();
}

void LoadSongUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();

	// If it's a directory...
	if (currentFileItem && currentFileItem->isFolder) {

		Error error = goIntoFolder(currentFileItem->filename.get());

		if (error != Error::NONE) {
			display->displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}

	else {
		LoadUI::enterKeyPress();     // Converts name to numeric-only if it was typed as text
		performLoad(storageManager); // May fail
		if (FlashStorage::defaultStartupSongMode == StartupSongMode::LASTOPENED) {
			runtimeFeatureSettings.writeSettingsToFile(storageManager);
		}
	}
}

void LoadSongUI::displayArmedPopup() {
	display->removeWorkingAnimation();
	display->popupText("Song will begin...");
}

char loopsRemainingText[] = "Loops remaining: xxxxxxxxxxx";

void LoadSongUI::displayLoopsRemainingPopup() {
	if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		display->removeWorkingAnimation();
		intToString(session.numRepeatsTilLaunch, &loopsRemainingText[17]);
		display->popupText(loopsRemainingText);
	}
}

ActionResult LoadSongUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

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
					if (display->haveOLED()) {
						displayArmedPopup();
					}
					else {
						sessionView.redrawNumericDisplay();
					}
				}
			}
		}
	}

	else {
		return LoadUI::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

// Before calling this, you must set loadButtonReleased.
void LoadSongUI::performLoad(StorageManager& bdsm) {

	FileItem* currentFileItem = getCurrentFileItem();

	if (!currentFileItem) {
		display->displayError(display->haveOLED()
		                          ? Error::FILE_NOT_FOUND
		                          : Error::NO_FURTHER_FILES_THIS_DIRECTION); // Make it say "NONE" on numeric Deluge,
		                                                                     // for consistency with old times.
		return;
	}

	actionLogger.deleteAllLogs();

	if (arrangement.hasPlaybackActive()) {
		playbackHandler.switchToSession();
	}

	Error error = storageManager.openXMLFile(&currentFileItem->filePointer, smDeserializer, "song");
	if (error != Error::NONE) {
		display->displayError(error);
		return;
	}

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

	void* songMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Song));
	if (!songMemory) {
ramError:
		error = Error::INSUFFICIENT_RAM;

someError:
		display->displayError(error);
		f_close(&smDeserializer.readFIL);

fail:
		// If we already deleted the old song, make a new blank one. This will take us back to InstrumentClipView.
		if (!currentSong) {
			// If we're here, it's most likely because of a file error. On paper, a RAM error could be possible too.
			setupBlankSong();
			audioFileManager.deleteAnyTempRecordedSamplesFromMemory();
		}


		// Otherwise, stay here in this UI

		preLoadedSong = new (songMemory) Song();
		error = preLoadedSong->paramManager.setupUnpatched();
		if (error != Error::NONE) {

			void* toDealloc = dynamic_cast<void*>(preLoadedSong);
			preLoadedSong->~Song(); // Will also delete paramManager
			delugeDealloc(toDealloc);
			preLoadedSong = NULL;
			goto someError;
		}

		GlobalEffectable::initParams(&preLoadedSong->paramManager);

		AudioEngine::logAction("c");

		// Will return false if we ran out of RAM. This isn't currently detected for while loading ParamNodes, but
		// chances are, after failing on one of those, it'd try to load something else and that would fail.
		error = preLoadedSong->readFromFile(smDeserializer);
		if (error != Error::NONE) {
			goto gotErrorAfterCreatingSong;
		}
		AudioEngine::logAction("d");

		bool success = f_close(&smDeserializer.readFIL);

		if (!success) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_ERROR_LOADING_SONG));
			goto fail;
		}

		preLoadedSong->dirPath.set(&currentDir);

		String currentFilenameWithoutExtension;
		error = currentFileItem->getFilenameWithoutExtension(&currentFilenameWithoutExtension);
		if (error != Error::NONE) {
			goto gotErrorAfterCreatingSong;
		}

		error = audioFileManager.setupAlternateAudioFileDir(&audioFileManager.alternateAudioFileLoadPath,
		                                                    currentDir.get(), &currentFilenameWithoutExtension);
		if (error != Error::NONE) {
			goto gotErrorAfterCreatingSong;
		}
		audioFileManager.thingBeginningLoading(ThingType::SONG);

		// Search existing RAM for all samples, to lay a claim to any which will be needed for this new Song.
		// Do this before loading any new Samples from file, in case we were in danger of discarding any from RAM that
		// we might actually want
		preLoadedSong->loadAllSamples(false);

		// Load samples from files, just for currently playing Sounds (or if not playing, then all Sounds)
		if (playbackHandler.isEitherClockActive()) {
			preLoadedSong->loadCrucialSamplesOnly();
		}

		else {
			displayText(false);
		}
		currentUIMode = UI_MODE_NONE;
		display->removeWorkingAnimation();
		return;
	}

	preLoadedSong = new (songMemory) Song();
	error = preLoadedSong->paramManager.setupUnpatched();
	if (error != Error::NONE) {
gotErrorAfterCreatingSong:
		void* toDealloc = dynamic_cast<void*>(preLoadedSong);
		preLoadedSong->~Song(); // Will also delete paramManager
		delugeDealloc(toDealloc);
		preLoadedSong = NULL;
		goto someError;
	}

	GlobalEffectable::initParams(&preLoadedSong->paramManager);

	AudioEngine::logAction("initialized new song");

	// Will return false if we ran out of RAM. This isn't currently detected for while loading ParamNodes, but chances
	// are, after failing on one of those, it'd try to load something else and that would fail.
	error = preLoadedSong->readFromFile(smDeserializer);
	if (error != Error::NONE) {
		goto gotErrorAfterCreatingSong;
	}
	AudioEngine::logAction("read new song from file");

	bool success = storageManager.closeFile(smDeserializer.readFIL);
	if (!success) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_ERROR_LOADING_SONG));
		goto fail;
	}

	preLoadedSong->dirPath.set(&currentDir);

	String currentFilenameWithoutExtension;
	error = currentFileItem->getFilenameWithoutExtension(&currentFilenameWithoutExtension);
	if (error != Error::NONE) {
		goto gotErrorAfterCreatingSong;
	}

	error = audioFileManager.setupAlternateAudioFileDir(&audioFileManager.alternateAudioFileLoadPath, currentDir.get(),
	                                                    &currentFilenameWithoutExtension);
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

	// Ensure all AudioFile Clusters needed for new song are loaded
	static int32_t count = 0;
	// Prevent any unforeseen loop. Not sure if that actually could happen
#ifdef USE_TASK_MANAGER
	yield([]() { return !(audioFileManager.loadingQueueHasAnyLowestPriorityElements() && count < 50); });
#else
	while (audioFileManager.loadingQueueHasAnyLowestPriorityElements() && count < 1024) {
		audioFileManager.loadAnyEnqueuedClusters();
		routineForSD();
		count++;
	}
#endif

	preLoadedSong->name.set(&enteredText);

	Song* toDelete = currentSong;

	if (playbackHandler.isEitherClockActive()) {

		// If load button was already released while that loading was happening, arm for song-swap now
		if (!Buttons::isButtonPressed(deluge::hid::button::LOAD)) {
			bool result = session.armForSongSwap();

			// If arming couldn't really be done, e.g. because current song had no Clips currently playing, swap has
			// already occurred
			if (!result) {
				goto swapDone;
			}

			currentUIMode = UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED;
			if (display->haveOLED()) {
				displayArmedPopup();
			}
			else {
				sessionView.redrawNumericDisplay();
			}
		}

		// Otherwise, set up so that the song-swap will be armed as soon as the user releases the load button
		else {
			display->removeWorkingAnimation();
			if (display->haveOLED()) {
				display->popupText("Loading complete");
			}
			else {
				display->setText("DONE", false, 255, true, NULL, false, true);
			}
			currentUIMode = UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED;
		}

		// We're now waiting, either for the user to arm, or for the arming to launch the song-swap. Get loading all the
		// rest of the samples which weren't needed right away. (though we might run out of RAM cos we haven't discarded
		// all the old samples yet)
		AudioEngine::logAction("asking for samples");
		preLoadedSong->loadAllSamples(true);
		AudioEngine::logAction("waiting for samples");
#ifdef USE_TASK_MANAGER
		yield([]() { return currentUIMode == UI_MODE_LOADING_SONG_NEW_SONG_PLAYING; });
#else
		// If any more waiting required before the song swap actually happens, do that
		while (currentUIMode != UI_MODE_LOADING_SONG_NEW_SONG_PLAYING) {
			audioFileManager.loadAnyEnqueuedClusters();
			routineForSD();
		}
#endif
	}

	else {
		playbackHandler.doSongSwap();
	}

swapDone:
	if (display->haveOLED()) {
		// To override our popup if we did one. (Still necessary?)
		deluge::hid::display::OLED::displayWorkingAnimation("Loading");
	}
	// Ok, the swap's been done, the first tick of the new song has been done, and there are potentially loads of
	// samples wanting some data loaded. So do that immediately
	audioFileManager.loadAnyEnqueuedClusters(99999);

	// Delete the old song
	AudioEngine::logAction("deleting old song");
	if (toDelete) {
		void* toDealloc = dynamic_cast<void*>(toDelete);
		toDelete->~Song();
		delugeDealloc(toDealloc);
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

// Returns error
/*
int32_t LoadSongUI::findNextFile(int32_t offset) {

    //currentFileExists = true;
    int16_t slotToSearchFrom = currentSlot;
    int8_t subSlotToSearchFrom = currentSubSlot;
    char const* nameToSearchFrom = enteredText.get();

    String newName;
    bool doingSecondTry = false;

doSearch:

    int32_t result = storageManager.findNextFile(offset,
            &currentSlot, &currentSubSlot, &newName, &currentFileIsFolder,
            slotToSearchFrom, subSlotToSearchFrom, nameToSearchFrom,
            "SONG", currentDir.get(), &currentFilePointer, true, 255, NULL, numberEditPos);


    if (result == Error::NO_FURTHER_FILES_THIS_DIRECTION) {

        if (doingSecondTry) {
            // Error - no files at all!
            currentSlot = 0;
            currentSubSlot = -1;
            enteredText.clear();
            enteredTextEditPos = 0;
            //currentFileExists = false;
            return Error::NONE;
        }

        doingSecondTry = true;

        if (offset >= 0) {
            slotToSearchFrom = -1;
            subSlotToSearchFrom = -1;
            nameToSearchFrom = NULL;
        }
        else {
            slotToSearchFrom = numInstrumentSlots;
            subSlotToSearchFrom = -1;
            nameToSearchFrom = "~";
        }
        goto doSearch;
    }
    else if (result) {
        return result;
    }


    enteredTextEditPos = getHowManyCharsAreTheSame(enteredText.get(), newName.get());
    enteredText.set(&newName);
    currentFilename.set(&newName); // This will only get used in the case of a folder, so it's ok(ish) that we're
ignoring the file extension.

    return Error::NONE;
}
*/

void LoadSongUI::currentFileChanged(int32_t movementDirection) {

	if (movementDirection) {
		qwertyVisible = false;

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

		drawSongPreview(storageManager); // Scrolling continues as the file is read by this function

		currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
		scrollingIntoSlot = true;

		// Set up another horizontal scroll
		PadLEDs::horizontal::setupScroll(movementDirection, kDisplayWidth + kSideBarWidth, false,
		                                 kDisplayWidth + kSideBarWidth);
		for (int32_t i = 0; i < kDisplayHeight; i++) {
			PadLEDs::transitionTakingPlaceOnRow[i] = true;
		}
		PadLEDs::horizontal::renderScroll();
	}
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
		if (display->haveOLED()) {
			// renderUIsForOled();
			displayLoopsRemainingPopup();
		}
		else {
			sessionView.redrawNumericDisplay();
		}
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
			if (Encoders::encoders[ENCODER_THIS_CPU_SELECT].detentPos) {
			    currentUIMode = UI_MODE_HORIZONTAL_SCROLL; // It might have been set to waitingForNextFileToLoad
			    offset = Encoders::encoders[ENCODER_THIS_CPU_SELECT].getLimitedDetentPosAndReset();
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

	return ActionResult::DEALT_WITH;
}

void LoadSongUI::exitAction() {

	// If parts of the old song have been deleted, sorry, there's no way we can exit without loading a new song
	if (deletedPartsOfOldSong) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_UNLOADED_PARTS));
		return;
	}

	currentUIMode = UI_MODE_VERTICAL_SCROLL;
	PadLEDs::vertical::setupScroll(-1, false);
	getRootUI()->renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
	getRootUI()->renderSidebar(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
	//((ViewScreen*)getRootUI())->renderToStore(0, true);
	timerCallback();
}

void LoadSongUI::drawSongPreview(StorageManager& bdsm, bool toStore) {

	RGB(*imageStore)[kDisplayWidth + kSideBarWidth];
	if (toStore) {
		imageStore = PadLEDs::imageStore;
	}
	else {
		imageStore = PadLEDs::image;
	}

	memset(imageStore, 0, kDisplayHeight * (kDisplayWidth + kSideBarWidth) * sizeof(RGB));

	FileItem* currentFileItem = getCurrentFileItem();

	if (!currentFileItem || currentFileItem->isFolder) {
		return;
	}

	Error error = bdsm.openXMLFile(&currentFileItem->filePointer, smDeserializer, "song", "", true);
	if (error != Error::NONE) {
		if (error != Error::NONE) {
			display->displayError(error);
			return;
		}
	}
	Deserializer& reader = smDeserializer;
	char const* tagName;
	int32_t previewNumPads = 40;
	while (*(tagName = reader.readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "previewNumPads")) {
			previewNumPads = reader.readTagOrAttributeValueInt();
			reader.exitTag("previewNumPads");
		}
		else if (!strcmp(tagName, "preview")) {
			int32_t skipNumCharsAfterRow = 0;

			int32_t startX = 0;
			int32_t startY = 0;
			int32_t endX = kDisplayWidth + kSideBarWidth;
			int32_t endY = kDisplayHeight;

			int32_t width = endX - startX;
			int32_t numCharsToRead = width * 3 * 2;

			if (!reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
				goto stopLoadingPreview;
			}

			for (int32_t y = startY; y < endY; y++) {
				char const* hexChars = reader.readNextCharsOfTagOrAttributeValue(numCharsToRead);
				if (!hexChars) {
					goto stopLoadingPreview;
				}

				for (int32_t x = startX; x < endX; x++) {
					for (int32_t colour = 0; colour < 3; colour++) {
						imageStore[y][x][colour] = hexToByte(hexChars);
						hexChars += 2;
					}
					imageStore[y][x] = imageStore[y][x].greyOut(6500000);
				}
			}
			goto stopLoadingPreview;
		}
		else {
			reader.exitTag(tagName);
		}
	}
stopLoadingPreview:
	bdsm.closeFile(smDeserializer.readFIL);
}

void LoadSongUI::displayText(bool blinkImmediately) {

	LoadUI::displayText();

	if (qwertyVisible) {
		FileItem* currentFileItem = getCurrentFileItem();

		drawKeys();

		PadLEDs::sendOutSidebarColours();
	}
}

ActionResult LoadSongUI::padAction(int32_t x, int32_t y, int32_t on) {
	// If QWERTY not visible yet, make it visible now
	if (!qwertyVisible) {
		if (on && !currentUIMode) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			qwertyVisible = true;
			displayText(false); // Necessary still? Not quite sure?
		}
	}

	// And process the QWERTY keypress
	if (qwertyVisible) {
		return LoadUI::padAction(x, y, on);
	}
	return ActionResult::DEALT_WITH;
}
