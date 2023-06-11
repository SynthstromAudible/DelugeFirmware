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

#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <InstrumentClipMinder.h>
#include <ParamManager.h>
#include "loadsongui.h"
#include "functions.h"
#include "numericdriver.h"
#include "uart.h"
#include <string.h>
#include <SessionView.h>
#include "GeneralMemoryAllocator.h"
#include "Session.h"
#include "Arrangement.h"
#include "View.h"
#include "ActionLogger.h"
#include "Encoders.h"
#include <new>
#include "storagemanager.h"
#include "song.h"
#include "matrixdriver.h"
#include "playbackhandler.h"
#include "uitimermanager.h"
#include "PadLEDs.h"
#include "IndicatorLEDs.h"
#include "Buttons.h"
#include "extern.h"
#include "FileItem.h"
#include "oled.h"

LoadSongUI loadSongUI;

extern void songLoaded(Song* song);
extern void setUIForLoadedSong(Song* song);
extern "C" {
void routineForSD(void);
#include "sio_char.h"
}
extern void setupBlankSong();

LoadSongUI::LoadSongUI() {
	qwertyAlwaysVisible = false;
	filePrefix = "SONG";
#if HAVE_OLED
	title = "Load song";
#endif
}

bool LoadSongUI::opened() {

	instrumentTypeToLoad = 255;
	currentDir.set(&currentSong->dirPath);

	int error = beginSlotSession(false, true);
	if (error) {
gotError:
		numericDriver.displayError(error);
		// Oh no, we're unable to read a file representing the first song. Get out quick!
		currentUIMode = UI_MODE_NONE;
		uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
		renderingNeededRegardlessOfUI(); // Otherwise we may have left the scrolling-in animation partially done
		return false;                    // Exit UI instantly
	}

	currentUIMode = UI_MODE_VERTICAL_SCROLL;
	scrollDirection = 1;
	squaresScrolled = 0;
	scrollingIntoSlot = false;
	scrollingToNothing = true;
	deletedPartsOfOldSong = false;
	timerCallback(); // Start scrolling animation out of the View

	PadLEDs::clearTickSquares();

	String searchFilename;
	searchFilename.set(&currentSong->name);

	if (!searchFilename.isEmpty()) {
		error = searchFilename.concatenate(".XML");
		if (error) goto gotError;
	}

	error = arrivedInNewFolder(0, searchFilename.get(), "SONGS");
	if (error) goto gotError;

#if SD_TEST_MODE_ENABLED_LOAD_SONGS
	currentSlot = (currentSlot + 1) % 19;
	currentSubSlot = (currentSlot == 0) ? 0 : -1;
#endif

	focusRegained();

	squaresScrolled = 0;
	scrollingIntoSlot = true;
	scrollingToNothing = false;

	if (currentUIMode != UI_MODE_VERTICAL_SCROLL) {
		currentUIMode =
		    UI_MODE_VERTICAL_SCROLL; // Have to reset this again - it might have finished the first bit of the scroll
		timerCallback();
	}

	IndicatorLEDs::setLedState(synthLedX, synthLedY, false);
	IndicatorLEDs::setLedState(kitLedX, kitLedY, false);
	IndicatorLEDs::setLedState(midiLedX, midiLedY, false);

	IndicatorLEDs::setLedState(crossScreenEditLedX, crossScreenEditLedY, false);
	IndicatorLEDs::setLedState(clipViewLedX, clipViewLedY, false);
	IndicatorLEDs::setLedState(sessionViewLedX, sessionViewLedY, false);
	IndicatorLEDs::setLedState(scaleModeLedX, scaleModeLedY, false);

	if (ALPHA_OR_BETA_VERSION && currentUIMode == UI_MODE_WAITING_FOR_NEXT_FILE_TO_LOAD) {
		numericDriver.freezeWithError("E188");
	}

	return true;
}

void LoadSongUI::folderContentsReady(int entryDirection) {

	drawSongPreview(currentUIMode == UI_MODE_VERTICAL_SCROLL);

	PadLEDs::sendOutMainPadColours();
	PadLEDs::sendOutSidebarColours();
}

void LoadSongUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();

	// If it's a directory...
	if (currentFileItem && currentFileItem->isFolder) {

		int error = goIntoFolder(currentFileItem->filename.get());

		if (error) {
			numericDriver.displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}

	else {
		LoadUI::enterKeyPress(); // Converts name to numeric-only if it was typed as text
		performLoad();
	}
}

#if HAVE_OLED
void LoadSongUI::displayArmedPopup() {
	OLED::removeWorkingAnimation();
	OLED::popupText("Song will begin...", true);
}

char loopsRemainingText[] = "Loops remaining: xxxxxxxxxxx";

void LoadSongUI::displayLoopsRemainingPopup() {
	if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		OLED::removeWorkingAnimation();
		intToString(session.numRepeatsTilLaunch, &loopsRemainingText[17]);
		OLED::popupText(loopsRemainingText, true);
	}
}
#endif

int LoadSongUI::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	// Load button or select encoder press. Unlike most (all?) other children of Browser, we override this and don't just call mainButtonAction(),
	// because unlike all the others, we need to action the load immediately on down-press rather than waiting for press-release, because of that special
	// action where you hold the button down until you want to "launch" the new song.
	if ((x == loadButtonX && y == loadButtonY) || (x == selectEncButtonX && y == selectEncButtonY)) {
		if (on) {
			if (!currentUIMode) {
				if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

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
#if HAVE_OLED
					displayArmedPopup();
#else
					sessionView.redrawNumericDisplay();
#endif
				}
			}
		}
	}

	else return LoadUI::buttonAction(x, y, on, inCardRoutine);

	return ACTION_RESULT_DEALT_WITH;
}

// Before calling this, you must set loadButtonReleased.
void LoadSongUI::performLoad() {

	FileItem* currentFileItem = getCurrentFileItem();

	if (!currentFileItem) {
		numericDriver.displayPopup(HAVE_OLED ? "No file selected" : "NONE");
		return;
	}

	actionLogger.deleteAllLogs();

	if (arrangement.hasPlaybackActive()) {
		playbackHandler.switchToSession();
	}

	int error = storageManager.openXMLFile(&currentFileItem->filePointer, "song");
	if (error) {
		numericDriver.displayError(error);
		return;
	}

	currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	IndicatorLEDs::setLedState(loadLedX, loadLedY, false);
	IndicatorLEDs::setLedState(backLedX, backLedY, false);
#if HAVE_OLED
	OLED::displayWorkingAnimation("Loading");
#else
	numericDriver.displayLoadingAnimation();
#endif

	nullifyUIs();

	deletedPartsOfOldSong = true;

	// If not currently playing, don't load both songs at once (this avoids any RAM overfilling, fragmentation etc.)
	if (!playbackHandler.isEitherClockActive()) {
		uiTimerManager.unsetTimer(
		    TIMER_PLAY_ENABLE_FLASH); // Otherwise, a timer might get called and try to access Clips that we may have deleted below (really?)
		deleteOldSongBeforeLoadingNew();
	}
	else {
		// Note: this is dodgy, but in this case we don't reset view.activeControllableClip here - we let the user keep fiddling with it. It won't get deleted.
		AudioEngine::logAction("a");
		AudioEngine::songSwapAboutToHappen();
		AudioEngine::logAction("b");
		playbackHandler.songSwapShouldPreserveTempo = Buttons::isButtonPressed(tempoEncButtonX, tempoEncButtonY);
	}

	void* songMemory = generalMemoryAllocator.alloc(sizeof(Song), NULL, false, true);
	if (!songMemory) {
ramError:
		error = ERROR_INSUFFICIENT_RAM;

someError:
		numericDriver.displayError(error);
		storageManager.closeFile();
fail:
		// If we already deleted the old song, make a new blank one. This will take us back to InstrumentClipView.
		if (!currentSong) {
			// If we're here, it's most likely because of a file error. On paper, a RAM error could be possible too.
			setupBlankSong();
			audioFileManager.deleteAnyTempRecordedSamplesFromMemory();
		}

		// Otherwise, stay here in this UI
		else {
			displayText(false);
		}
		currentUIMode = UI_MODE_NONE;
#if HAVE_OLED
		OLED::removeWorkingAnimation();
#endif
		return;
	}

	preLoadedSong = new (songMemory) Song();
	error = preLoadedSong->paramManager.setupUnpatched();
	if (error) {
gotErrorAfterCreatingSong:
		void* toDealloc = dynamic_cast<void*>(preLoadedSong);
		preLoadedSong->~Song(); // Will also delete paramManager
		generalMemoryAllocator.dealloc(toDealloc);
		preLoadedSong = NULL;
		goto someError;
	}

	GlobalEffectable::initParams(&preLoadedSong->paramManager);

	AudioEngine::logAction("c");

	// Will return false if we ran out of RAM. This isn't currently detected for while loading ParamNodes, but chances are, after failing on one of those, it'd try to
	// load something else and that would fail.
	error = preLoadedSong->readFromFile();
	if (error) goto gotErrorAfterCreatingSong;
	AudioEngine::logAction("d");

	bool success = storageManager.closeFile();

	if (!success) {
		numericDriver.displayPopup(HAVE_OLED ? "Error loading song" : "ERROR");
		goto fail;
	}

	preLoadedSong->dirPath.set(&currentDir);

	String currentFilenameWithoutExtension;
	error = currentFileItem->getFilenameWithoutExtension(&currentFilenameWithoutExtension);
	if (error) goto gotErrorAfterCreatingSong;

	error = audioFileManager.setupAlternateAudioFileDir(&audioFileManager.alternateAudioFileLoadPath, currentDir.get(),
	                                                    &currentFilenameWithoutExtension);
	if (error) goto gotErrorAfterCreatingSong;
	audioFileManager.thingBeginningLoading(THING_TYPE_SONG);

	// Search existing RAM for all samples, to lay a claim to any which will be needed for this new Song.
	// Do this before loading any new Samples from file, in case we were in danger of discarding any from RAM that we might actually want
	preLoadedSong->loadAllSamples(false);

	// Load samples from files, just for currently playing Sounds (or if not playing, then all Sounds)
	if (playbackHandler.isEitherClockActive()) preLoadedSong->loadCrucialSamplesOnly();
	else preLoadedSong->loadAllSamples(true);

	// Ensure all AudioFile Clusters needed for new song are loaded
	int count = 0; // Prevent any unforeseen loop. Not sure if that actually could happen
	while (audioFileManager.loadingQueueHasAnyLowestPriorityElements() && count < 1024) {
		audioFileManager.loadAnyEnqueuedClusters();
		routineForSD();
		count++;
	}

	preLoadedSong->name.set(&enteredText);

	Song* toDelete = currentSong;

	if (playbackHandler.isEitherClockActive()) {

		// If load button was already released while that loading was happening, arm for song-swap now
		if (!Buttons::isButtonPressed(loadButtonX, loadButtonY)) {
			bool result = session.armForSongSwap();

			// If arming couldn't really be done, e.g. because current song had no Clips currently playing, swap has already occurred
			if (!result) {
				goto swapDone;
			}

			currentUIMode = UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED;
#if HAVE_OLED
			displayArmedPopup();
#else
			sessionView.redrawNumericDisplay();
#endif
		}

		// Otherwise, set up so that the song-swap will be armed as soon as the user releases the load button
		else {
#if HAVE_OLED
			OLED::removeWorkingAnimation();
			OLED::popupText("Loading complete", true);
#else
			numericDriver.setText("DONE", false, 255, true, NULL, false, true);
#endif
			currentUIMode = UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED;
		}

		// We're now waiting, either for the user to arm, or for the arming to launch the song-swap. Get loading all the rest of the samples which weren't needed right away.
		// (though we might run out of RAM cos we haven't discarded all the old samples yet)
		AudioEngine::logAction("g");
		preLoadedSong->loadAllSamples(true);
		AudioEngine::logAction("h");

		// If any more waiting required before the song swap actually happens, do that
		while (currentUIMode != UI_MODE_LOADING_SONG_NEW_SONG_PLAYING) {
			audioFileManager.loadAnyEnqueuedClusters();
			routineForSD();
		}
	}

	else {
		playbackHandler.doSongSwap();
	}

swapDone:
#if HAVE_OLED
	OLED::displayWorkingAnimation("Loading"); // To override our popup if we did one. (Still necessary?)
#endif
	// Ok, the swap's been done, the first tick of the new song has been done, and there are potentially loads of samples wanting some data loaded. So do that immediately
	audioFileManager.loadAnyEnqueuedClusters(99999);

	// Delete the old song
	AudioEngine::logAction("i");
	if (toDelete) {
		void* toDealloc = dynamic_cast<void*>(toDelete);
		toDelete->~Song();
		generalMemoryAllocator.dealloc(toDealloc);
	}

	audioFileManager.deleteAnyTempRecordedSamplesFromMemory();

	// Try one more time to load all AudioFiles - there might be more RAM free now
	currentSong->loadAllSamples();
	AudioEngine::logAction("l");
	currentSong->markAllInstrumentsAsEdited();

	audioFileManager.thingFinishedLoading();

	PadLEDs::doGreyoutInstantly(); // This will get faded out of just below
	setUIForLoadedSong(currentSong);
	currentUIMode = UI_MODE_NONE;

#if HAVE_OLED
	OLED::removeWorkingAnimation();
#endif
}

int LoadSongUI::timerCallback() {

	// Progress vertical scrolling
	if (currentUIMode == UI_MODE_VERTICAL_SCROLL) {

		squaresScrolled++;
		int copyRow = (scrollDirection > 0) ? squaresScrolled - 1 : displayHeight - squaresScrolled;
		int startSquare = (scrollDirection > 0) ? 0 : 1;
		int endSquare = (scrollDirection > 0) ? displayHeight - 1 : 0;

		//matrixDriver.greyoutMinYDisplay = (scrollDirection > 0) ? displayHeight - squaresScrolled : squaresScrolled;

		// Move the scrolling region
		memmove(PadLEDs::image[startSquare], PadLEDs::image[1 - startSquare],
		        (displayWidth + sideBarWidth) * (displayHeight - 1) * 3);

		// And, bring in a row from the temp image (or from nowhere)
		if (scrollingToNothing) memset(PadLEDs::image[endSquare], 0, (displayWidth + sideBarWidth) * 3);
		else memcpy(PadLEDs::image[endSquare], PadLEDs::imageStore[copyRow], (displayWidth + sideBarWidth) * 3);

		if (DELUGE_MODEL != DELUGE_MODEL_40_PAD) {
			bufferPICPadsUart((scrollDirection > 0) ? 241 : 242);

			for (int x = 0; x < displayWidth + sideBarWidth; x++) {
				PadLEDs::sendRGBForOnePadFast(x, endSquare, PadLEDs::image[endSquare][x]);
			}
			uartFlushIfNotSending(UART_ITEM_PIC_PADS);
		}

		// If we've finished scrolling...
		if (squaresScrolled >= displayHeight) {
			// If exiting this UI...
			if (scrollDirection == -1) {
				exitThisUI(); // Ideally I don't think this should be allowed to be happen while in the card routine, which we're in right now...
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
			uiTimerManager.setTimer(TIMER_UI_SPECIFIC,
			                        UI_MS_PER_REFRESH_SCROLLING * 4); // *2 caused glitches occasionally
		}
getOut : {}
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
		PadLEDs::sendOutMainPadColours();
		PadLEDs::sendOutSidebarColours();
#endif
		return ACTION_RESULT_DEALT_WITH;
	}

	else {
		return LoadUI::timerCallback();
	}
}

void LoadSongUI::scrollFinished() {
	// If we were scrolling out of one song and we got here, we just need to sit back and wait for the next song to load
	if (!scrollingIntoSlot) {
		currentUIMode = UI_MODE_WAITING_FOR_NEXT_FILE_TO_LOAD;
	}

	// Or, if we've finished scrolling into a new song
	else {
		currentUIMode = UI_MODE_NONE;
	}
}

void LoadSongUI::exitActionWithError() {
	numericDriver.displayPopup(HAVE_OLED ? "SD card error" : "CARD");
	exitAction();
}

void LoadSongUI::exitThisUI() {
	currentUIMode = UI_MODE_NONE;
	close();
}

// Returns error
/*
int LoadSongUI::findNextFile(int offset) {

    //currentFileExists = true;
    int16_t slotToSearchFrom = currentSlot;
    int8_t subSlotToSearchFrom = currentSubSlot;
    char const* nameToSearchFrom = enteredText.get();

    String newName;
    bool doingSecondTry = false;

doSearch:

	int result = storageManager.findNextFile(offset,
    		&currentSlot, &currentSubSlot, &newName, &currentFileIsFolder,
			slotToSearchFrom, subSlotToSearchFrom, nameToSearchFrom,
			"SONG", currentDir.get(), &currentFilePointer, true, 255, NULL, numberEditPos);


    if (result == ERROR_NO_FURTHER_FILES_THIS_DIRECTION) {

    	if (doingSecondTry) {
			// Error - no files at all!
			currentSlot = 0;
			currentSubSlot = -1;
			enteredText.clear();
			enteredTextEditPos = 0;
			//currentFileExists = false;
			return NO_ERROR;
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
    currentFilename.set(&newName); // This will only get used in the case of a folder, so it's ok(ish) that we're ignoring the file extension.

    return NO_ERROR;
}
*/

void LoadSongUI::currentFileChanged(int movementDirection) {

	if (movementDirection) {
		qwertyVisible = false;

		// Start horizontal scrolling
		PadLEDs::setupScroll(movementDirection, displayWidth + sideBarWidth, true, displayWidth + sideBarWidth);
		for (int i = 0; i < displayHeight; i++)
			PadLEDs::transitionTakingPlaceOnRow[i] = true;
		currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
		scrollingIntoSlot = false;
		PadLEDs::renderScroll(); // The scrolling animation will begin while file is being found and loaded

		drawSongPreview(); // Scrolling continues as the file is read by this function

		currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
		scrollingIntoSlot = true;

		// Set up another horizontal scroll
		PadLEDs::setupScroll(movementDirection, displayWidth + sideBarWidth, false, displayWidth + sideBarWidth);
		for (int i = 0; i < displayHeight; i++)
			PadLEDs::transitionTakingPlaceOnRow[i] = true;
		PadLEDs::renderScroll();
	}
}

void LoadSongUI::selectEncoderAction(int8_t offset) {

	if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		session.numRepeatsTilLaunch += offset;
		if (session.numRepeatsTilLaunch < 1) session.numRepeatsTilLaunch = 1;
		else if (session.numRepeatsTilLaunch > 9999) session.numRepeatsTilLaunch = 9999;
#if HAVE_OLED
		//renderUIsForOled();
		displayLoopsRemainingPopup();
#else
		sessionView.redrawNumericDisplay();
#endif
	}

	else {

		if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_HORIZONTAL_SCROLL) {

			LoadUI::selectEncoderAction(offset);
			/*
goAgain:
			qwertyVisible = false;

			// Start horizontal scrolling
			PadLEDs::setupScroll(offset, displayWidth + sideBarWidth, true, displayWidth + sideBarWidth);
			for (int i = 0; i < displayHeight; i++) PadLEDs::transitionTakingPlaceOnRow[i] = true;
			currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
			scrollingIntoSlot = false;
			PadLEDs::renderScroll(); // The scrolling animation will begin while file is being found and loaded

			int result = findNextFile(offset);
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
			PadLEDs::setupScroll(offset, displayWidth + sideBarWidth, false, displayWidth + sideBarWidth);
			for (int i = 0; i < displayHeight; i++) PadLEDs::transitionTakingPlaceOnRow[i] = true;
			PadLEDs::renderScroll();
			*/
		}
	}
}

int LoadSongUI::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (!currentUIMode && !Buttons::isButtonPressed(yEncButtonX, yEncButtonY) && !Buttons::isShiftButtonPressed()
	    && offset < 0) {
		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		exitAction(); // Exit if your scroll down
	}

	return ACTION_RESULT_DEALT_WITH;
}

void LoadSongUI::exitAction() {

	// If parts of the old song have been deleted, sorry, there's no way we can exit without loading a new song
	if (deletedPartsOfOldSong) {
		numericDriver.displayPopup(HAVE_OLED ? "Can't return to current song, as parts have been unloaded" : "CANT");
		return;
	}

	currentUIMode = UI_MODE_VERTICAL_SCROLL;
	scrollDirection = -1;
	scrollingToNothing = false;
	squaresScrolled = 0;
	getRootUI()->renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
	getRootUI()->renderSidebar(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
	//((ViewScreen*)getRootUI())->renderToStore(0, true);
	timerCallback();
}

void LoadSongUI::drawSongPreview(bool toStore) {

	uint8_t(*imageStore)[displayWidth + sideBarWidth][3];
	if (toStore) imageStore = PadLEDs::imageStore;
	else imageStore = PadLEDs::image;

	memset(imageStore, 0, displayHeight * (displayWidth + sideBarWidth) * 3);

	FileItem* currentFileItem = getCurrentFileItem();

	if (!currentFileItem || currentFileItem->isFolder) return;

	int error = storageManager.openXMLFile(&currentFileItem->filePointer, "song", "", true);
	if (error)
		if (error) {
			numericDriver.displayError(error);
			return;
		}

	char const* tagName;
	int previewNumPads = 40;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "previewNumPads")) {
			previewNumPads = storageManager.readTagOrAttributeValueInt();
			storageManager.exitTag("previewNumPads");
		}
		else if (!strcmp(tagName, "preview")) {

			int startX, startY, endX, endY;
			int skipNumCharsAfterRow = 0;

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
			startX = startY = 0;
			endX = displayWidth + sideBarWidth;
			endY = displayHeight;
			if (previewNumPads != 40) {
				skipNumCharsAfterRow = 48;
			}
#else
			if (previewNumPads == 40) {
				startX = 4;
				endX = 14;
				startY = 2;
				endY = 6;
				memset(imageStore, 0, sizeof(imageStore));
			}
			else {
				startX = startY = 0;
				endX = displayWidth + sideBarWidth;
				endY = displayHeight;
			}

#endif

			int width = endX - startX;
			int numCharsToRead = width * 3 * 2;

			if (!storageManager.prepareToReadTagOrAttributeValueOneCharAtATime()) goto stopLoadingPreview;

			for (int y = startY; y < endY; y++) {
				char const* hexChars = storageManager.readNextCharsOfTagOrAttributeValue(numCharsToRead);
				if (!hexChars) goto stopLoadingPreview;

				for (int x = startX; x < endX; x++) {
					for (int colour = 0; colour < 3; colour++) {
						imageStore[y][x][colour] = hexToByte(hexChars);
						hexChars += 2;
					}
					greyColourOut(imageStore[y][x], imageStore[y][x], 6500000);
				}

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
				for (int i = 0; i < skipNumCharsAfterRow; i++) {
					storageManager.readNextCharOfTagOrAttributeValue();
				}
#endif
			}
			goto stopLoadingPreview;
		}
		else storageManager.exitTag(tagName);
	}
stopLoadingPreview:
	storageManager.closeFile();
}

void LoadSongUI::displayText(bool blinkImmediately) {

	LoadUI::displayText();

	if (qwertyVisible) {
		FileItem* currentFileItem = getCurrentFileItem();

		if (currentFileItem && !currentFileItem->isFolder) {
			drawSongPreview(false); // Only draw this again so we can draw the keyboard on top of it. I think...
		}
		else {
			PadLEDs::clearAllPadsWithoutSending();
		}

		drawKeys();
		PadLEDs::sendOutMainPadColours();
		PadLEDs::sendOutSidebarColours();
	}
}

int LoadSongUI::padAction(int x, int y, int on) {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	if (currentUIMode != UI_MODE_NONE || !on) return ACTION_RESULT_DEALT_WITH;
	if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
	performLoad(true);
#else

	// If QWERTY not visible yet, make it visible now
	if (!qwertyVisible) {
		if (on && !currentUIMode) {
			if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			qwertyVisible = true;
			displayText(false); // Necessary still? Not quite sure?
		}
	}

	// And process the QWERTY keypress
	if (qwertyVisible) return LoadUI::padAction(x, y, on);
	else return ACTION_RESULT_DEALT_WITH;

#endif
	return ACTION_RESULT_DEALT_WITH;
}
