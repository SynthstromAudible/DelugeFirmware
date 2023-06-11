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

#define _GNU_SOURCE // Wait why?
#include <samplebrowser.h>
#include "functions.h"
#include "soundeditor.h"
#include "matrixdriver.h"
#include <AudioEngine.h>
#include "storagemanager.h"
#include "numericdriver.h"
#include "uart.h"
#include <string.h>
#include "source.h"
#include <sound.h>
#include "AudioRecorder.h"
#include <sounddrum.h>
#include "instrument.h"
#include <ParamManager.h>
#include "kit.h"
#include "Slicer.h"
#include <AudioFileManager.h>
#include <Cluster.h>
#include "WaveTable.h"
#include "NumericLayerScrollingText.h"
#include <AudioFileManager.h>
#include "ActionLogger.h"
#include "MultisampleRange.h"
#include "GeneralMemoryAllocator.h"
#include <WaveformRenderer.h>
#include "View.h"
#include "Encoders.h"
#include "KeyboardScreen.h"
#include <new>
#include <ContextMenuSampleBrowserKit.h>
#include <ContextMenuSampleBrowserSynth.h>
#include "DString.h"
#include "ContextMenuDeleteFile.h"
#include "WaveformBasicNavigator.h"
#include "uitimermanager.h"
#include <InstrumentClipView.h>
#include "song.h"
#include "AudioClip.h"
#include "AudioClipView.h"
#include "PadLEDs.h"
#include "IndicatorLEDs.h"
#include "FlashStorage.h"
#include "Buttons.h"
#include "ModelStack.h"
#include "extern.h"
#include "InstrumentClip.h"
#include "AutoParam.h"
#include "ParamSet.h"
#include "NoteRow.h"
#include "MenuItemMultiRange.h"
#include "FileItem.h"
#include "playbackhandler.h"

#if HAVE_OLED
#include "oled.h"
#endif

extern "C" {
#include "sio_char.h"
}

SampleBrowser sampleBrowser;

char const* allowedFileExtensionsAudio[] = {"WAV", "AIFF", "AIF", NULL};

SampleBrowser::SampleBrowser() {

#if HAVE_OLED
	fileIcon = OLED::waveIcon;
	title = "Audio files";
#else
	shouldWrapFolderContents = false;
#endif
	qwertyAlwaysVisible = false;
}

bool SampleBrowser::opened() {

	bool success = Browser::opened();
	if (!success) return false;

	actionLogger.deleteAllLogs();

	allowedFileExtensions = allowedFileExtensionsAudio;
	allowFoldersSharingNameWithFile = true;
	instrumentTypeToLoad = 255;
	qwertyVisible = false;
	qwertyCurrentlyDrawnOnscreen = false;

	currentlyShowingSamplePreview = false;

#if HAVE_OLED
	fileIndexSelected = 0;
#endif

	if (currentUIMode == UI_MODE_AUDITIONING) instrumentClipView.cancelAllAuditioning();

	int error = storageManager.initSD();
	if (error) {
sdError:
		numericDriver.displayError(error);
		numericDriver.setNextTransitionDirection(0); // Cancel the transition that we'll now not be doing
		return false;
	}

	String currentPath;
	currentPath.set(&soundEditor.getCurrentAudioFileHolder()->filePath);

	char const* searchFilename;

	// If currentPath is blank, or is somewhere outside of the SAMPLES folder, then default to previously manually loaded sample
	if (currentPath.isEmpty() || memcasecmp(currentPath.get(), "SAMPLES/", 8)) {
		currentPath.set(&lastFilePathLoaded);

		// If that's blank too, then default to SAMPLES folder.
		if (currentPath.isEmpty()) {
			currentDir.set("SAMPLES");
			searchFilename = NULL;
			goto dissectionDone;
		}
	}

	{
		// Must dissect
		char const* currentPathChars = currentPath.get();
		char const* slashAddress = strrchr(currentPathChars, '/');
		if (!slashAddress) {
			searchFilename = currentPathChars;
			currentDir.clear();
		}
		else {
			int slashPos = (uint32_t)slashAddress - (uint32_t)currentPathChars;
			searchFilename = &currentPathChars[slashPos + 1];

			currentDir.set(currentPathChars);
			currentDir.shorten(slashPos);
		}
	}

dissectionDone:

	error = arrivedInNewFolder(1, searchFilename, "SAMPLES");
	if (error) goto sdError;

	IndicatorLEDs::setLedState(synthLedX, synthLedY, !soundEditor.editingKit());
	IndicatorLEDs::setLedState(kitLedX, kitLedY, soundEditor.editingKit());

	IndicatorLEDs::setLedState(crossScreenEditLedX, crossScreenEditLedY, false);
	IndicatorLEDs::setLedState(sessionViewLedX, sessionViewLedY, false);
	IndicatorLEDs::setLedState(scaleModeLedX, scaleModeLedY, false);

	//soundEditor.setupShortcutBlink(soundEditor.currentSourceIndex, 5, 0);

	if (currentUIMode == UI_MODE_AUDITIONING) instrumentClipView.cancelAllAuditioning();

	possiblySetUpBlinking();

	return true;
}

void SampleBrowser::possiblySetUpBlinking() {

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	if (!qwertyVisible && !currentlyShowingSamplePreview) {
		int x = 0;
		if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) x = soundEditor.currentSourceIndex;
		soundEditor.setupExclusiveShortcutBlink(x, 5);
	}
#endif
}

void SampleBrowser::focusRegained() {
	//displayCurrentFilename();
	IndicatorLEDs::setLedState(saveLedX, saveLedY, false); // In case returning from delete-file context menu
}

void SampleBrowser::folderContentsReady(int entryDirection) {

	// If just one file, there's no prefix.
	if (fileItems.getNumElements() <= 1) {
		numCharsInPrefix = 0;
	}

	else {
		numCharsInPrefix = 65535;
		FileItem* currentFileItem = getCurrentFileItem();

		char const* currentFilenameChars = currentFileItem->filename.get();

		for (int f = 0; numCharsInPrefix && f < fileItems.getNumElements(); f++) {
			FileItem* fileItem = (FileItem*)fileItems.getElementAddress(f);

			for (int i = 0; i < numCharsInPrefix; i++) {
				char const* thisFileName = fileItem->filename.get();
				if (!thisFileName[i] || thisFileName[i] != currentFilenameChars[i]) {
					numCharsInPrefix = i;
					break;
				}
			}
		}
	}

	previewIfPossible(entryDirection);
}

void SampleBrowser::currentFileChanged(int movementDirection) {

	// Can start scrolling right now, while next preview loads
	if (movementDirection && (currentlyShowingSamplePreview || qwertyVisible)) {
		qwertyVisible = false;

		uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);

		memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
		PadLEDs::setupScroll(movementDirection, displayWidth, true);
		currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
	}

	AudioEngine::stopAnyPreviewing();

	previewIfPossible(movementDirection);
}

void SampleBrowser::exitAndNeverDeleteDrum() {
	numericDriver.setNextTransitionDirection(-1);
	close();
}

// Will "delete drum if possible".
void SampleBrowser::exitAction() {
	UI* redrawUI = NULL;

	numericDriver.setNextTransitionDirection(-1);
	if (!isUIOpen(&soundEditor)) {
		// If no file was selected, the user wanted to get out of creating this Drum.
		if (soundEditor.editingKit()
		    && ((Kit*)currentSong->currentClip->output)
		           ->getFirstUnassignedDrum((InstrumentClip*)currentSong->currentClip) // Only if some unassigned Drums
		    && soundEditor.getCurrentAudioFileHolder()->filePath.isEmpty()) {
			instrumentClipView.deleteDrum((SoundDrum*)soundEditor.currentSound);
			redrawUI = &instrumentClipView;
		}
	}

	Browser::exitAction();

	if (redrawUI) uiNeedsRendering(redrawUI);
}

int SampleBrowser::timerCallback() {

	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {
		currentUIMode = UI_MODE_NONE;
		if (fileIndexSelected >= 0) {

			char const* errorMessage;
			ContextMenu* contextMenu;

			// AudioClip
			if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
#if HAVE_OLED
				errorMessage = "Can't import whole folder into audio clip";
#endif
				goto cant;
			}

			// Kit
			else if (soundEditor.editingKit()) {

				if (canImportWholeKit()) {
					contextMenu = &contextMenuFileBrowserKit;
					goto considerContextMenu;
				}
				else {
#if HAVE_OLED
					errorMessage = "Can only import whole folder into brand-new kit";
#endif
cant:
					numericDriver.displayPopup(HAVE_OLED ? errorMessage : "CANT");
				}
			}

			// Synth
			else {
				contextMenu = &contextMenuFileBrowserSynth;

considerContextMenu:
				bool available = contextMenu->setupAndCheckAvailability();

				if (available) { // Not sure if this can currently fail.
					numericDriver.setNextTransitionDirection(1);
					openUI(contextMenu);
				}
				else {
					exitUIMode(UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS);
				}
			}
		}
		return ACTION_RESULT_DEALT_WITH;
	}
	else return Browser::timerCallback();
}

void SampleBrowser::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();

	// Make sure we're looking at a valid file / folder
	if (currentFileItem) {

		AudioEngine::stopAnyPreviewing();

		// If it's a directory...
		if (currentFileItem->isFolder) {

			// Don't allow user to go into TEMP clips folder
			if (currentFileItem->filename.equalsCaseIrrespective("TEMP")
			    && currentDir.equalsCaseIrrespective("SAMPLES/CLIPS")) {
				numericDriver.displayPopup(HAVE_OLED ? "TEMP folder can't be browsed" : "CANT");
				return;
			}

			char const* filenameChars =
			    currentFileItem->filename
			        .get(); // Extremely weirdly, if we try to just put this inside the parentheses in the next line,
			                // it returns an empty string (&nothing). Surely this is a compiler error??

			int error = goIntoFolder(filenameChars);

			if (error) {
				numericDriver.displayError(error);
				close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
				return;
			}
		}

		// Or if it's an audio file...
		else {

			// If we're here, we know that the file has fully loaded

			// If user wants to slice...
			if (Buttons::isShiftButtonPressed()) {

				// Can only do this for Kit Clips, and for source 0, not 1, AND there has to be only one drum present, which is assigned to the first NoteRow
				if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT && canImportWholeKit()) {
					numericDriver.displayPopup("SLICER");
					openUI(&slicer);
				}
				else {
					numericDriver.displayPopup(HAVE_OLED ? "Can only user slicer for brand-new kit" : "CANT");
				}
			}

			// Otherwise, load it normally
			else {
				claimCurrentFile();
			}
		}
	}
}

int SampleBrowser::backButtonAction() {
	AudioEngine::stopAnyPreviewing();

	return Browser::backButtonAction();
}

int SampleBrowser::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	// Save button, to delete audio file
	if (x == saveButtonX && y == saveButtonY && Buttons::isShiftButtonPressed()) {
		if (!currentUIMode && on) {
			FileItem* currentFileItem = getCurrentFileItem();
			if (currentFileItem) {
				if (!currentFileItem->isFolder) { // This is an additional requirement only present in this class

					AudioEngine::stopAnyPreviewing();

					if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

					// Ensure sample isn't used in current song
					String filePath;
					int error = getCurrentFilePath(&filePath);
					if (error) {
						numericDriver.displayError(error);
						return ACTION_RESULT_DEALT_WITH;
					}

					bool allFine = audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(filePath.get());

					if (!allFine) {
						numericDriver.displayPopup(
#if HAVE_OLED
						    "Audio file is used in current song"
#else
						    "USED"
#endif
						);
					}
					else {
						goIntoDeleteFileContextMenu();
					}
				}
			}
		}
	}

	// Horizontal encoder button
	else if (x == xEncButtonX && y == xEncButtonY) {
		if (on) {
			if (isNoUIModeActive()) enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}

		else {
			if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON))
				exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}
	}

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	// Record button
	else if (x == recordButtonX && y == recordButtonY && !audioRecorder.recordingSource
	         && currentSong->currentClip->type != CLIP_TYPE_AUDIO) {
		if (!on || currentUIMode != UI_MODE_NONE) return ACTION_RESULT_DEALT_WITH;
		AudioEngine::stopAnyPreviewing();

		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

		bool success = changeUISideways(&audioRecorder); // If this fails, we will become the current UI again
		if (success) {
			renderingNeededRegardlessOfUI();
			audioRecorder.process();
		}
	}
#endif

	else return Browser::buttonAction(x, y, on, inCardRoutine);

	return ACTION_RESULT_DEALT_WITH;
}

bool SampleBrowser::canImportWholeKit() {
	return (soundEditor.editingKit() && soundEditor.currentSourceIndex == 0
	        && (SoundDrum*)((InstrumentClip*)currentSong->currentClip)->noteRows.getElement(0)->drum
	               == soundEditor.currentSound
	        && (!((Kit*)currentSong->currentClip->output)->firstDrum->next));
}

int SampleBrowser::getCurrentFilePath(String* path) {
	int error;

	path->set(&currentDir);
	int oldLength = path->getLength();
	if (oldLength) {
		error = path->concatenateAtPos("/", oldLength);
		if (error) {
gotError:
			path->clear();
			return error;
		}
	}

	FileItem* currentFileItem = getCurrentFileItem();

	error = path->concatenate(&currentFileItem->filename);
	if (error) goto gotError;

	return NO_ERROR;
}

bool SampleBrowser::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {

	if (currentlyShowingSamplePreview || qwertyVisible || getRootUI() == &keyboardScreen) {
		*cols = 0b10;
	}
	else {
		*cols = 0xFFFFFFFE;
	}

	return true;
}

void SampleBrowser::previewIfPossible(int movementDirection) {

	/*
	// Was this in case they've already turned the knob further?
	if (movementDirection && movementDirection * Encoders::encoders[ENCODER_THIS_CPU_SELECT].detentPos > 0 && numFilesFoundInRightDirection > 1) {
		Uart::println("returned 1");
		return;
	}
	*/

	bool didDraw = false;

	FileItem* currentFileItem = getCurrentFileItem();

	// Preview the WAV file, if we're allowed
	if (currentFileItem && !currentFileItem->isFolder) {

		String filePath;
		int error = getCurrentFilePath(&filePath);
		if (error) {
			numericDriver.displayError(error);
			return;
		}

		// This more formally does the thing that actually was happening accidentally for ages, as found by Michael B.
		lastFilePathLoaded.set(&filePath);

		bool shouldActuallySound = false;

		// Decide if we're actually going to sound it.
		if (!instrumentClipView.fileBrowserShouldNotPreview) {
			switch (FlashStorage::sampleBrowserPreviewMode) {
			case PREVIEW_ONLY_WHILE_NOT_PLAYING:
				if (playbackHandler.playbackState) break;
				// No break

			case PREVIEW_ON:
				shouldActuallySound = true;
				break;
			}
		}

		AudioEngine::previewSample(&filePath, &currentFileItem->filePointer, shouldActuallySound);

		/*
		if (movementDirection && movementDirection * Encoders::encoders[ENCODER_THIS_CPU_SELECT].detentPos > 0 && numFilesFoundInRightDirection > 1) {
			Uart::println("returned 2");
			return;
		}
		*/

		// If the Sample at least loaded, even if we didn't sound it, then try to render its waveform.
		if (AudioEngine::sampleForPreview->sources[0].ranges.getNumElements() >= 1) {
			AudioFile* sample = ((MultisampleRange*)AudioEngine::sampleForPreview->sources[0].ranges.getElement(0))
			                        ->sampleHolder.audioFile;

			if (sample) {
				uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);

				currentlyShowingSamplePreview = true;
				PadLEDs::reassessGreyout(true);

				waveformBasicNavigator.sample = (Sample*)sample;
				waveformBasicNavigator.opened();

				// If want scrolling animation
				if (movementDirection) {
					waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
					                                  waveformBasicNavigator.xZoom, PadLEDs::imageStore,
					                                  &waveformBasicNavigator.renderData);
					memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
					PadLEDs::setupScroll(movementDirection, displayWidth);

					currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
				}

				// Or if want instant snap render
				else {
					if (qwertyVisible) {
						//drawKeysOverWaveform();
						PadLEDs::clearMainPadsWithoutSending();
						drawKeys();
					}
					else {
						waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
						                                  waveformBasicNavigator.xZoom, PadLEDs::image,
						                                  &waveformBasicNavigator.renderData);
					}
					qwertyCurrentlyDrawnOnscreen = qwertyVisible;
					PadLEDs::sendOutMainPadColours();
				}
				PadLEDs::sendOutSidebarColours(); // For greyout (wait what?)

				didDraw = true;
			}
		}
	}

	// If did not just preview a sample...
	if (!didDraw) {

		// But if we need to get rid of whatever was onscreen...
		if (currentlyShowingSamplePreview || (qwertyCurrentlyDrawnOnscreen && !qwertyVisible)) {

			currentlyShowingSamplePreview = false;
			qwertyCurrentlyDrawnOnscreen = qwertyVisible;

			if (movementDirection) {
				getRootUI()->renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
				//((ViewScreen*)getRootUI())->renderToStore(0, true, false);
				if (getRootUI() != &keyboardScreen) PadLEDs::reassessGreyout(true);
				memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
				PadLEDs::setupScroll(movementDirection, displayWidth);
				currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
			}

			possiblySetUpBlinking();
		}
	}
}

void SampleBrowser::scrollFinished() {
	exitUIMode(UI_MODE_HORIZONTAL_SCROLL);
}

void SampleBrowser::displayCurrentFilename() {
	if (fileIndexSelected == -1) {
		numericDriver.setText("----");
	}

	else {}
}

int SampleBrowser::padAction(int x, int y, int on) {

	// Allow auditioning
	if (x == displayWidth + 1) {
		if (getRootUI() == &instrumentClipView) {
			return instrumentClipView.padAction(x, y, on);
		}
	}

	// Mute pads - exit UI
	else if (x == displayWidth) { // !currentlyShowingSamplePreview ||
possiblyExit:
		if (on && !currentUIMode) {
			AudioEngine::stopAnyPreviewing();
			if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			exitAction();
		}
	}

	else {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
		goto possiblyExit;
#else
		// If qwerty not visible yet, make it visible
		if (!qwertyVisible) {
			if (on && !currentUIMode) {
				if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

				qwertyVisible = true;

				uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
				PadLEDs::reassessGreyout(true);

				if (false && currentlyShowingSamplePreview) {
					drawKeysOverWaveform();
				}
				else {
					PadLEDs::clearMainPadsWithoutSending();
					drawKeys();
				}

				qwertyCurrentlyDrawnOnscreen = true;

				PadLEDs::sendOutMainPadColours();

				enteredTextEditPos = 0;
				displayText(false);
			}
		}

		if (qwertyVisible) return QwertyUI::padAction(x, y, on);
		else return ACTION_RESULT_DEALT_WITH;
#endif
	}

	return ACTION_RESULT_DEALT_WITH;
}

void SampleBrowser::drawKeysOverWaveform() {

	// Do manual greyout on all main pads
	for (int y = 0; y < displayHeight; y++) {
		for (int x = 0; x < displayWidth; x++) {
			greyColourOut(PadLEDs::image[y][x], PadLEDs::image[y][x], 6500000);
		}
	}

	drawKeys();
}

int SampleBrowser::claimAudioFileForInstrument(bool makeWaveTableWorkAtAllCosts) {
	soundEditor.cutSound();

	AudioFileHolder* holder = soundEditor.getCurrentAudioFileHolder();
	holder->setAudioFile(NULL);
	int error = getCurrentFilePath(&holder->filePath);
	if (error) return error;

	return holder->loadFile(soundEditor.currentSource->sampleControls.reversed, true, true, CLUSTER_ENQUEUE, 0,
	                        makeWaveTableWorkAtAllCosts);
}

int SampleBrowser::claimAudioFileForAudioClip() {
	soundEditor.cutSound();

	AudioFileHolder* holder = soundEditor.getCurrentAudioFileHolder();
	holder->setAudioFile(NULL);
	int error = getCurrentFilePath(&holder->filePath);
	if (error) return error;

	bool reversed = ((AudioClip*)currentSong->currentClip)->sampleControls.reversed;
	error = holder->loadFile(reversed, true, true);

	// If there's a pre-margin, we want to set an attack-time
	if (!error && ((SampleHolder*)holder)->startPos) {
		((AudioClip*)currentSong->currentClip)->attack = AUDIO_CLIP_DEFAULT_ATTACK_IF_PRE_MARGIN;
	}

	return error;
}

// This displays any (rare) specific errors generated, then spits out just a boolean success.
// For the "may" arguments, 0 means no; 1 means auto; 2 means do definitely as the user has specifically requested it.
bool SampleBrowser::claimCurrentFile(int mayDoPitchDetection, int mayDoSingleCycle, int mayDoWaveTable) {

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		if (currentSong->currentClip->getCurrentlyRecordingLinearly()) {
			numericDriver.displayPopup(HAVE_OLED ? "Clip is recording" : "CANT");
			return false;
		}
	}

#if HAVE_OLED
	OLED::displayWorkingAnimation("Working");
#else
	numericDriver.displayLoadingAnimation();
#endif

	int error;

	// If for AudioClip...
	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {

		error = claimAudioFileForAudioClip();
		if (error) {
removeLoadingAnimationAndGetOut:
#if HAVE_OLED
			OLED::removeWorkingAnimation();
#else
			numericDriver.removeTopLayer();
#endif
			numericDriver.displayError(error);
			return false;
		}

		AudioClip* clip = (AudioClip*)currentSong->currentClip;

		uint64_t lengthInSamplesAt44 = (uint64_t)clip->sampleHolder.getDurationInSamples(true) * 44100
		                               / ((Sample*)clip->sampleHolder.audioFile)->sampleRate;
		uint32_t sampleLengthInTicks = (lengthInSamplesAt44 << 32) / currentSong->timePerTimerTickBig;

		int32_t newLength = 3;
		while (newLength * 1.41 < sampleLengthInTicks)
			newLength <<= 1;

		int32_t oldLength = clip->loopLength;

		clip->loopLength = newLength;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		clip->lengthChanged(modelStack, oldLength);

		clip->sampleHolder.transpose = 0;
		clip->sampleHolder.cents = 0;
		clip->sampleControls.reversed = false;
	}

	// Otherwise, we're something to do with an Instrument...
	else {

		soundEditor.currentSound->unassignAllVoices(); // We used to only do this if osc type wasn't already SAMPLE...

		bool makeWaveTableWorkAtAllCosts = (mayDoWaveTable == 2) || (mayDoSingleCycle == 2)
		                                   || (soundEditor.currentSound->getSynthMode() == SYNTH_MODE_RINGMOD);

		int numTypesTried = 0;

		// If we already know we want to try doing WaveTable...
		if (makeWaveTableWorkAtAllCosts
		    || (mayDoWaveTable == 1 && soundEditor.currentSource->oscType == OSC_TYPE_WAVETABLE)) {
doLoadAsWaveTable:
			numTypesTried++;

			/*
			// If multiple Ranges, then forbid the changing from Sample to WaveTable.
			if (soundEditor.currentSource->ranges.getNumElements() > 1
					&& soundEditor.currentSource->oscType == OSC_TYPE_SAMPLE) {
#if ALPHA_OR_BETA_VERSION
				if (mayDoWaveTable == 2) numericDriver.freezeWithError("E425");
#endif
				goto doLoadAsSample;
			}
			*/
			soundEditor.currentSource->setOscType(OSC_TYPE_WAVETABLE);

			error = claimAudioFileForInstrument(makeWaveTableWorkAtAllCosts);
			if (error) {
				// If word has come back that this file isn't wanting to load as a WaveTable...
				if (error == ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE
				    || error == ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO) {

					// If that was what the user really specified they wanted, and we couldn't do it, then we have to tell them no.
					if (mayDoWaveTable == 2 || numTypesTried > 1
					    || (soundEditor.currentSound->getSynthMode() == SYNTH_MODE_RINGMOD)) {
						goto removeLoadingAnimationAndGetOut;
					}

					// Or if they don't really mind, just load it as a Sample.
					else goto doLoadAsSample;
				}

				// Or any other error...
				else goto removeLoadingAnimationAndGetOut;
			}

			// Alright, if we're still here, it was successfully loaded as a WaveTable!

			if (soundEditor.currentSourceIndex == 0) { // Osc 1
				soundEditor.currentSound->modKnobs[7][1].paramDescriptor.setToHaveParamOnly(
				    PARAM_LOCAL_OSC_A_WAVE_INDEX);

				if (!soundEditor.currentSound->modKnobs[7][0].paramDescriptor.isSetToParamWithNoSource(
				        PARAM_LOCAL_OSC_B_WAVE_INDEX)) {
					soundEditor.currentSound->modKnobs[7][0].paramDescriptor.setToHaveParamAndSource(
					    PARAM_LOCAL_OSC_A_WAVE_INDEX, PATCH_SOURCE_LFO_LOCAL);
				}
			}
			else { // Osc 2
				soundEditor.currentSound->modKnobs[7][0].paramDescriptor.setToHaveParamOnly(
				    PARAM_LOCAL_OSC_B_WAVE_INDEX);
			}
			currentSong->currentClip->output->modKnobMode = 7;
			view.setKnobIndicatorLevels(); // Visually update.
			view.setModLedStates();
		}

		// Or if we want to first try doing it as a Sample (not a WaveTable)...
		else {
doLoadAsSample:
			numTypesTried++;

			/*
			// If multiple Ranges, then forbid the changing from WaveTable to Sample.
			if (soundEditor.currentSource->ranges.getNumElements() > 1
					&& soundEditor.currentSource->oscType == OSC_TYPE_WAVETABLE) {
#if ALPHA_OR_BETA_VERSION
				if (!mayDoWaveTable) numericDriver.freezeWithError("E426");
#endif
				goto doLoadAsWaveTable;
			}
			*/

			soundEditor.currentSource->setOscType(OSC_TYPE_SAMPLE);

			error = claimAudioFileForInstrument();
			if (error) goto removeLoadingAnimationAndGetOut;

			Sample* sample = (Sample*)soundEditor.getCurrentAudioFileHolder()->audioFile;

			// If the file was actually clearly a wavetable file, and we're allowed to load one, then go do that instead.
			if (mayDoWaveTable && numTypesTried <= 1 && sample->fileExplicitlySpecifiesSelfAsWaveTable) {
				goto doLoadAsWaveTable;
			}

			bool doingSingleCycleNow = false;

			int mSec = sample->getLengthInMSec();

			// If 20ms or less, and we're not a kit, then we'd like to be a single-cycle waveform.
			if (!soundEditor.editingKit() && (mayDoSingleCycle == 2 || (mayDoSingleCycle == 1 && mSec <= 20))) {

				// Ideally, we'd like to use the wavetable engine for this single-cycle-ness
				if (mayDoWaveTable && numTypesTried <= 1 && sample->numChannels == 1
				    && sample->lengthInSamples >= WAVETABLE_MIN_CYCLE_SIZE
				    && sample->lengthInSamples <= WAVETABLE_MAX_CYCLE_SIZE) {

					makeWaveTableWorkAtAllCosts =
					    true; // So that the loading functions don't just chicken out when it doesn't look all that wavetabley.
					goto doLoadAsWaveTable;
				}

				// Otherwise, set play mode to LOOP, and we'll just do single-cycle as a sample. (This is now pretty rare.)
				soundEditor.currentSource->repeatMode = SAMPLE_REPEAT_LOOP;
				doingSingleCycleNow = true;
			}

			// If time stretching or looping on (or we just decided to do single-cycle, above), leave that the case
			if (soundEditor.currentSource->repeatMode == SAMPLE_REPEAT_STRETCH
			    || soundEditor.currentSource->repeatMode == SAMPLE_REPEAT_LOOP) {}

			// Otherwise...
			else {

				// If source file had loop points set...
				if (sample->fileLoopEndSamples) {

					// If this led to an actual loop end pos, with more waveform after it, and the sample's not too long, we can do a ONCE.
					if (((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.loopEndPos && mSec < 2002) {
						soundEditor.currentSource->repeatMode = SAMPLE_REPEAT_ONCE;
					}
					else {
						soundEditor.currentSource->repeatMode = SAMPLE_REPEAT_LOOP;
					}
				}

				else {

					// If 2 seconds or less, set play mode to ONCE. Otherwise, CUT.
					soundEditor.currentSource->repeatMode = (mSec < 2002) ? SAMPLE_REPEAT_ONCE : SAMPLE_REPEAT_CUT;
				}
			}

			// If Kit...
			if (soundEditor.editingKit()) {

				SoundDrum* drum = (SoundDrum*)soundEditor.currentSound;

				autoDetectSideChainSending(drum, soundEditor.currentSource, enteredText.get());

				// Give Drum no name, momentarily. We don't want it to show up when we're searching for duplicates
				drum->name.clear();

				String newName;
				if (!numCharsInPrefix) newName.set(&enteredText);
				else {
					error = newName.set(&enteredText.get()[numCharsInPrefix]);
					if (error) goto removeLoadingAnimationAndGetOut;
				}

				Kit* kit = (Kit*)currentSong->currentClip->output;

				// Ensure Drum name isn't a duplicate, and if need be, make a new name from the fileNamePostPrefix.
				if (kit->getDrumFromName(newName.get())) {

					error = kit->makeDrumNameUnique(&newName, 2);
					if (error) goto removeLoadingAnimationAndGetOut;
				}

				drum->name.set(&newName);
			}

			// If a synth...
			else {

				// Detect pitch
				if (mayDoPitchDetection) {
					bool shouldMinimizeOctaves = (soundEditor.currentSource->ranges.getNumElements() == 1);

					((MultisampleRange*)soundEditor.currentMultiRange)
					    ->sampleHolder.setTransposeAccordingToSamplePitch(shouldMinimizeOctaves, doingSingleCycleNow);
				}

				else {
					// Otherwise, reset pitch. Popular request, late 2022.
					// https://forums.synthstrom.com/discussion/4814/v4-0-1-after-loading-a-non-c-sample-into-synth-reloading-the-sample-as-basic-doesnt-reset-pitch
					((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.transpose = 0;
					((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.setCents(0);
				}
			}

			// Anyway, by now we know we've loaded as a Sample, not a Wavetable.
			// So remove WaveTable gold knob assignments.
			bool anyChange = false;
			int p = PARAM_LOCAL_OSC_A_WAVE_INDEX + soundEditor.currentSourceIndex;
			if (soundEditor.currentSound->modKnobs[7][0].paramDescriptor.getJustTheParam() == p) {
				soundEditor.currentSound->modKnobs[7][0].paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_BITCRUSHING
				                                                                            + PARAM_UNPATCHED_SECTION);
				anyChange = true;
			}
			if (soundEditor.currentSound->modKnobs[7][1].paramDescriptor.getJustTheParam() == p) {
				soundEditor.currentSound->modKnobs[7][1].paramDescriptor.setToHaveParamOnly(
				    PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION + PARAM_UNPATCHED_SECTION);
				anyChange = true;
			}

			if (anyChange) {
				currentSong->currentClip->output->modKnobMode = 1;
				view.setKnobIndicatorLevels(); // Visually update.
				view.setModLedStates();
			}
		}

		audioFileIsNowSet();

		((Instrument*)currentSong->currentClip->output)->beenEdited();

		// If there was only one MultiRange, don't go back to the range menu (that's the BOT-TOP thing).
		if (soundEditor.currentSource->ranges.getNumElements() <= 1 && soundEditor.navigationDepth
		    && soundEditor.menuItemNavigationRecord[soundEditor.navigationDepth - 1] == &multiRangeMenu) {
			soundEditor.navigationDepth--;
		}
	}

	exitAndNeverDeleteDrum();
	uiNeedsRendering(&audioClipView);
#if HAVE_OLED
	OLED::removeWorkingAnimation();
#endif
	return true;
}

void SampleBrowser::autoDetectSideChainSending(SoundDrum* drum, Source* source, char const* fileName) {

	// If this looks like a kick, make it send to sidechain. Otherwise, no change
	if (source->repeatMode == SAMPLE_REPEAT_ONCE && (strcasestr(fileName, "kick") || strcasestr(fileName, "bd"))) {
		drum->sideChainSendLevel = 2147483647;
	}
}

void SampleBrowser::audioFileIsNowSet() {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
	ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
	PatchedParamSet* paramSet = (PatchedParamSet*)summary->paramCollection;
	int paramId = PARAM_LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex;
	ModelStackWithAutoParam* modelStackWithParam =
	    modelStack->addParam(paramSet, summary, paramId, &paramSet->params[paramId]);

	// Reset osc volume, if it's not automated and was at 0. Wait but that will only do it for the current ParamManager... there could be other ones...
	if (!modelStackWithParam->autoParam->containsSomething(-2147483648)) {
		modelStackWithParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithParam, 2147483647);

		// Hmm crap, we probably still do need to notify...
		//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(PARAM_LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex, 2147483647, 0xFFFFFFFF, 0, soundEditor.currentSound, currentSong, currentSong->currentClip, false);
	}
}

bool pitchGreaterOrEqual(Sample* a, Sample* b) {
	return (a->midiNote >= b->midiNote);
}

bool filenameGreaterOrEqual(Sample* a, Sample* b) {
	return (strcmpspecial(a->filePath.get(), b->filePath.get(), true) >= 0);
}

bool filenameGreaterOrEqualOctaveStartingFromA(Sample* a, Sample* b) {
	return (strcmpspecial(a->filePath.get(), b->filePath.get(), true, true) >= 0);
}

void sortSamples(bool (*sortFunction)(Sample*, Sample*), int numSamples, Sample*** sortAreas, int* readArea,
                 int* writeArea) {
	int numComparing = 1;

	int whichComparison = 0;

	// Go through various iterations of numComparing
	while (numComparing < numSamples) {

		AudioEngine::routineWithClusterLoading(); // --------------------------------------------------

		// And now, for this selected comparison size, do a number of comparisions
		for (int whichComparison = 0; whichComparison * numComparing * 2 < numSamples; whichComparison++) {

			int a = numComparing * (whichComparison * 2);
			int b = numComparing * (whichComparison * 2 + 1);

			for (int writeI = numComparing * whichComparison * 2;
			     writeI < numComparing * (whichComparison + 1) * 2 && writeI < numSamples; writeI++) {
				Sample* sampleA = sortAreas[*readArea][a];
				Sample* sampleB = sortAreas[*readArea][b];

				Sample** sampleWrite = &sortAreas[*writeArea][writeI];

				if (b < numComparing * (whichComparison + 1) * 2 && b < numSamples
				    && (a >= numComparing * (whichComparison * 2 + 1) || sortFunction(sampleA, sampleB))) {
					*sampleWrite = sampleB;
					b++;
				}
				else {
					*sampleWrite = sampleA;
					a++;
				}
			}
		}

		*readArea = 1 - *readArea;
		*writeArea = 1 - *writeArea;
		numComparing *= 2;
	}
}

int getNumTimesIncorrectSampleOrderSeen(int numSamples, Sample** samples) {

	int timesIncorrectOrderSeen = 0;

	for (int s = 1; s < numSamples; s++) {
		Sample* sampleA = samples[s - 1];
		Sample* sampleB = samples[s];

		if (sampleB->midiNote < sampleA->midiNote) {
			timesIncorrectOrderSeen++;
		}
	}

	Uart::print("timesIncorrectOrderSeen: ");
	Uart::println(timesIncorrectOrderSeen);

	return timesIncorrectOrderSeen;
}

bool SampleBrowser::loadAllSamplesInFolder(bool detectPitch, int* getNumSamples, Sample*** getSortArea,
                                           bool* getDoingSingleCycle, int* getPrefixAndDirLength) {

	String dirToLoad;
	uint8_t error;

	FileItem* currentFileItem = getCurrentFileItem();

	char const* previouslyViewedFilename = "";

	if (currentFileItem->isFolder) {
		error = getCurrentFilePath(&dirToLoad);
		if (error) {
			numericDriver.displayError(error);
			return false;
		}
	}
	else {
		dirToLoad.set(&currentDir);
		previouslyViewedFilename = currentFileItem->filename.get();
	}

	FRESULT result = f_opendir(&staticDIR, dirToLoad.get());
	if (result != FR_OK) {
		numericDriver.displayError(ERROR_SD_CARD);
		return false;
	}

	int numSamples = 0;

	bool doingSingleCycle = true; // Until we find a sample too long

	float commonMIDINote = -2; // -2 means no data yet. -3 means multiple different ones

	if (false) {
removeReasonsFromSamplesAndGetOut:
		// Remove reasons from any samples we loaded in just before
		for (int e = 0; e < audioFileManager.audioFiles.getNumElements(); e++) {
			AudioFile* audioFile = (AudioFile*)audioFileManager.audioFiles.getElement(e);

			if (audioFile->type == AUDIO_FILE_TYPE_SAMPLE) {
				Sample* thisSample = (Sample*)audioFile;

				// If this sample is one of the ones we loaded a moment ago...
				if (thisSample->partOfFolderBeingLoaded) {
					thisSample->partOfFolderBeingLoaded = false;
#if ALPHA_OR_BETA_VERSION
					if (thisSample->numReasonsToBeLoaded <= 0)
						numericDriver.freezeWithError("E213"); // I put this here to try and catch an E004 Luc got
#endif
					thisSample->removeReason("E392"); // Remove that temporary reason we added
				}
			}
		}

		numericDriver.displayError(error);
		return false;
	}

	AudioEngine::routineWithClusterLoading(); // --------------------------------------------------

	int numCharsInPrefixForFolderLoad = 65535;

	String filePath;
	filePath.set(&dirToLoad);
	int dirWithSlashLength = filePath.getLength();
	if (dirWithSlashLength) {
		filePath.concatenateAtPos("/", dirWithSlashLength);
		dirWithSlashLength++;
	}

	while (true) {
		audioFileManager.loadAnyEnqueuedClusters();
		FilePointer thisFilePointer;

		result = f_readdir_get_filepointer(&staticDIR, &staticFNO, &thisFilePointer); /* Read a directory item */

		if (result != FR_OK || staticFNO.fname[0] == 0) break; // Break on error or end of dir
		if (staticFNO.fname[0] == '.') continue;               // Ignore dot entry
		if (staticFNO.fattrib & AM_DIR) continue;              // Ignore folders
		if (!isAudioFilename(staticFNO.fname)) continue;       // Ignore anything that's not an audio file

		// This is a usable audio file

		// Keep investigating if there's a common prefix to all files in this folder.
		// currentFilename will be set to the name of the first file in the folder, or another one we looked at more recently
		if (numSamples > 0) {

			for (int i = 0; i < numCharsInPrefixForFolderLoad; i++) {
				if (!staticFNO.fname[i] || staticFNO.fname[i] != previouslyViewedFilename[i]) {
					numCharsInPrefixForFolderLoad = i;
					break;
				}
			}
		}

		filePath.concatenateAtPos(staticFNO.fname, dirWithSlashLength);

		Sample* newSample = (Sample*)audioFileManager.getAudioFileFromFilename(
		    &filePath, true, &error, &thisFilePointer,
		    AUDIO_FILE_TYPE_SAMPLE); // We really want to be able to pass a file pointer in here
		if (error || !newSample) {
			f_closedir(&staticDIR);
			goto removeReasonsFromSamplesAndGetOut;
		}

		newSample->addReason();
		newSample->partOfFolderBeingLoaded = true;
		if (newSample->getLengthInMSec() > 20) doingSingleCycle = false;

		if (commonMIDINote == -2) {
			commonMIDINote = newSample->midiNoteFromFile;
		}
		else if (commonMIDINote >= -1) {
			if (commonMIDINote != newSample->midiNoteFromFile) {
				commonMIDINote = -3;
			}
		}

		numSamples++;
	}
	f_closedir(&staticDIR);

	if (getPrefixAndDirLength) {
		// If just one file, there's no prefix.
		if (numSamples <= 1) {
			numCharsInPrefixForFolderLoad = 0;
		}

		*getPrefixAndDirLength = dirWithSlashLength + numCharsInPrefixForFolderLoad;
	}

	// Ok, the samples are now all in memory.

	Uart::print("loaded from folder: ");
	Uart::println(numSamples);

	// If all samples were tagged with the same MIDI note, we get suspicious and delete them.
	bool discardingMIDINoteFromFile = (numSamples > 1 && commonMIDINote >= 0);

	Sample** sortArea = (Sample**)generalMemoryAllocator.alloc(numSamples * sizeof(Sample*) * 2, NULL, false, true);
	if (!sortArea) {
		error = ERROR_INSUFFICIENT_RAM;
		goto removeReasonsFromSamplesAndGetOut;
	}

	Sample** thisSamplePointer = sortArea;

	// Go through each sample in memory that was from the folder in question, adding them to our pointer list
	int sampleI = 0;
	for (int e = 0; e < audioFileManager.audioFiles.getNumElements(); e++) {
		AudioFile* audioFile = (AudioFile*)audioFileManager.audioFiles.getElement(e);

		if (audioFile->type == AUDIO_FILE_TYPE_SAMPLE) {

			Sample* thisSample = (Sample*)audioFile;
			// If this sample is one of the ones we loaded a moment ago...
			if (thisSample->partOfFolderBeingLoaded) {
				thisSample->partOfFolderBeingLoaded = false;

				if (discardingMIDINoteFromFile) thisSample->midiNoteFromFile = -1;

				if (detectPitch) {
					thisSample->workOutMIDINote(doingSingleCycle);
				}

				*thisSamplePointer = thisSample;
				sampleI++;
				thisSamplePointer++;

				if (sampleI == numSamples) break; // Just for safety
			}
		}
	}

	numSamples = sampleI; // In case it's lower now, e.g. due to some samples' pitch detection failing

	Uart::print("successfully detected pitch: ");
	Uart::println(numSamples);

	Sample** sortAreas[2];
	sortAreas[0] = sortArea;
	sortAreas[1] = &sortArea[numSamples];

	int readArea = 0;
	int writeArea = 1;

	// Sort by filename
	sortSamples(filenameGreaterOrEqual, numSamples, sortAreas, &readArea, &writeArea);

	// If detecting pitch, do all of that
	if (detectPitch) {

#define NOTE_CHECK_ERROR_MARGIN 0.75

		int badnessRatingFromC = getNumTimesIncorrectSampleOrderSeen(numSamples, sortAreas[readArea]);
		if (!badnessRatingFromC) goto allSorted; // If that's all fine, we're done

		// If the Samples are in precisely the wrong order, something's happened like we've been interpretting a dash (-) in the filenames as a minus sign.
		// Just reverse the order.
		if (badnessRatingFromC == numSamples - 1) {
			for (int s = 0; s < (numSamples >> 1); s++) {
				Sample* temp = sortAreas[readArea][s];
				sortAreas[readArea][s] = sortAreas[readArea][numSamples - 1 - s];
				sortAreas[readArea][numSamples - 1 - s] = temp;
			}
			goto allSorted;
		}

		/*
		// Try sorting from A instead
		sortSamples(filenameGreaterOrEqualOctaveStartingFromA, numSamples, sortAreas, &readArea, &writeArea);
		int badnessRatingFromA = getNumTimesIncorrectSampleOrderSeen(numSamples, sortAreas[readArea]);
		if (!badnessRatingFromA) goto allSorted; // If that's all fine, we're done

		// If from A was actually worse than C, go back
		if (badnessRatingFromA >= badnessRatingFromC) {

			// But if C is actually bad enough, we might conclude that the filenames are irrelevant
			if ((badnessRatingFromC * 3) > numSamples) goto justSortByPitch;

			Uart::println("going back to ordering from C");
			sortSamples(filenameGreaterOrEqual, numSamples, sortAreas, &readArea, &writeArea);
		}

		// Or if A is better...
		else {

			// But if A is still actually bad enough, we might conclude that the filenames are irrelevant
			if ((badnessRatingFromA * 3) > numSamples) goto justSortByPitch;
		}
		*/

		// Ok, we're here, the samples are optimally ordered by file, but, the pitch is out.
		Uart::println("sample order by file finalized");

		float prevNote = sortAreas[readArea][0]->midiNote; // May be MIDI_NOTE_ERROR

		for (int s = 1; s < numSamples; s++) {

			prevNote += 1;

			Sample* thisSample = sortAreas[readArea][s];

			float noteHere = thisSample->midiNote;
			if (noteHere == MIDI_NOTE_ERROR) continue;

			if (noteHere < prevNote - NOTE_CHECK_ERROR_MARGIN) {

				// Ok, this one's lower than the last. Who's wrong?

				// If we correct backwards, how many would we have to redo?
				int numIncorrectBackwards = 0;

				for (int t = s - 1; t >= 0; t--) {
					Sample* thatSample = sortAreas[readArea][t];
					if (thatSample->midiNote == MIDI_NOTE_ERROR) continue;
					if (thatSample->midiNote < noteHere + t - s + NOTE_CHECK_ERROR_MARGIN) break;

					numIncorrectBackwards++; // If we're here, this note would have to be marked as incorrect
				}

				// Ok, and if we corrected forwards, how many?
				int numIncorrectForwards = 1;

				for (int t = s + 1; t < numSamples; t++) {
					Sample* thatSample = sortAreas[readArea][t];
					if (thatSample->midiNote == MIDI_NOTE_ERROR) continue;
					if (thatSample->midiNote >= prevNote + t - s - NOTE_CHECK_ERROR_MARGIN) break;

					numIncorrectForwards++;
				}

				// If we decide to correct backwards...
				if (numIncorrectBackwards < numIncorrectForwards) {
					for (int t = s - 1; t >= 0; t--) {
						Sample* thatSample = sortAreas[readArea][t];
						if (thatSample->midiNote == MIDI_NOTE_ERROR) continue;
						if (thatSample->midiNote < noteHere + t - s + NOTE_CHECK_ERROR_MARGIN) break;

						thatSample->midiNote = MIDI_NOTE_ERROR;
					}
				}

				// Or if we decide to correct forwards...
				else {
					thisSample->midiNote = MIDI_NOTE_ERROR;
					for (int t = s + 1; t < numSamples; t++) {
						Sample* thatSample = sortAreas[readArea][t];
						if (thatSample->midiNote == MIDI_NOTE_ERROR) continue;
						if (thatSample->midiNote >= prevNote + t - s - NOTE_CHECK_ERROR_MARGIN) break;

						thatSample->midiNote = MIDI_NOTE_ERROR;
					}
					continue; // Keep the old prevNote
				}
			}

			prevNote = noteHere;
		}

		prevNote = MIDI_NOTE_ERROR;

		// Ok, we've now marked a bunch of samples as having the incorrect pitch, which we know because it doesn't match the filename order.
		// So go through and correct them, now that we've got a better idea of the range they should fit in.
		for (int s = 0; s < numSamples; s++) {
			Sample* thisSample = sortAreas[readArea][s];

			if (thisSample->midiNote != MIDI_NOTE_ERROR) {
				prevNote = thisSample->midiNote;
				continue;
			}

			float nextNote = 999;
			for (int t = s + 1; t < numSamples; t++) {
				Sample* thatSample = sortAreas[readArea][t];
				if (thatSample->midiNote != MIDI_NOTE_ERROR) {
					nextNote = thatSample->midiNote - t + s;
					break;
				}
			}

			prevNote += 1;

			// Ok, we got a range to search within
			float minFreqHz;
			if (prevNote < 0) {
				minFreqHz = 20;
			}
			else {
				minFreqHz = powf(2, ((prevNote - NOTE_CHECK_ERROR_MARGIN) - 69) / 12) * 440;
			}

			float maxFreqHz;
			if (nextNote == 999) {
				maxFreqHz = 10000;
			}
			else {
				maxFreqHz = powf(2, ((nextNote + NOTE_CHECK_ERROR_MARGIN) - 69) / 12) * 440;
			}

			// If maxFreqHz is too low, it's likely no use to us, and the whole mission's probably messed up, so just call this one an error.
			// Hopefully I can improve this one day. See Michael B's Mellotron samples
			if (maxFreqHz < minFreqHz) {
				thisSample->midiNote = MIDI_NOTE_ERROR;
				continue;
			}

			Uart::print("redoing, limited to ");
			Uart::print(minFreqHz);
			Uart::print(" to ");
			Uart::println(maxFreqHz);

			thisSample->workOutMIDINote(doingSingleCycle, minFreqHz, maxFreqHz, false);

			// If didn't work, see if we can pretend we're looking for 1 octave higher, where there'd probably be a harmonic too.
			// This can help if the fundamental isn't visible - it worked on Leo's piano samples before I realised that
			// those harmonics had just been deleted by my aggressive use of a threshold
			if (thisSample->midiNote == MIDI_NOTE_ERROR) {
				minFreqHz *= 2;
				maxFreqHz *= 2;

				Uart::println("pretending an octave up...");

				thisSample->workOutMIDINote(doingSingleCycle, minFreqHz, maxFreqHz, false);

				if (thisSample->midiNote != MIDI_NOTE_ERROR) {
					thisSample->midiNote -= 12;
					goto gotNote;
				}
			}

			// If success
			else {
gotNote:
				prevNote = thisSample->midiNote;
			}
		}

justSortByPitch:
		// We've done all the correcting we can. Now re-sort by pitch
		sortSamples(pitchGreaterOrEqual, numSamples, sortAreas, &readArea, &writeArea);
	}

allSorted:
	// All sorted! If the sorted values have ended up in the secondary area, move them back to the first
	if (readArea == 1) {
		memcpy(sortArea, &sortArea[numSamples], numSamples * sizeof(Sample*));
	}

	if (getSortArea) *getSortArea = sortArea;
	if (getNumSamples) *getNumSamples = numSamples;
	if (getDoingSingleCycle) *getDoingSingleCycle = doingSingleCycle;

	return true;
}

bool SampleBrowser::importFolderAsMultisamples() {

	AudioEngine::stopAnyPreviewing();

#if HAVE_OLED
	OLED::displayWorkingAnimation("Working");
#else
	numericDriver.displayLoadingAnimation();
#endif

	int numSamples;
	bool doingSingleCycle;
	Sample** sortArea;

	bool success = loadAllSamplesInFolder(true, &numSamples, &sortArea, &doingSingleCycle);
	if (!success) {
doReturnFalse:
#if HAVE_OLED
		OLED::removeWorkingAnimation();
#endif
		return false;
	}

	Uart::println("loaded and sorted samples");

	AudioEngine::routineWithClusterLoading(); // --------------------------------------------------

	// Delete all but first pre-existing range
	int oldNumRanges = soundEditor.currentSource->ranges.getNumElements();
	for (int i = oldNumRanges - 1; i >= 1; i--) {
		soundEditor.currentSound->deleteMultiRange(soundEditor.currentSourceIndex, i);
	}

	// If we now want more than one range, be efficient by getting our array of ranges to pre-allocate all the memory it's going to use
	if (numSamples > 1) {
		soundEditor.currentSound->unassignAllVoices();
		AudioEngine::audioRoutineLocked = true;
		bool success = soundEditor.currentSource->ranges.ensureEnoughSpaceAllocated(numSamples - 1);
		AudioEngine::audioRoutineLocked = false;

		if (!success) {
			generalMemoryAllocator.dealloc(sortArea);
			for (int s = 0; s < numSamples; s++) {
				Sample* thisSample = sortArea[s];
#if ALPHA_OR_BETA_VERSION
				if (thisSample->numReasonsToBeLoaded <= 0)
					numericDriver.freezeWithError("E215"); // I put this here to try and catch an E004 Luc got
#endif
				thisSample->removeReason("E393"); // Remove that temporary reason we added above
			}
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
			goto doReturnFalse;
		}
	}

	soundEditor.setCurrentMultiRange(0);

	AudioEngine::audioRoutineLocked = false;

	// If we've ended up with some samples a whole octave higher than the others, this may be in error
	int whichSampleIsAnOctaveUp = 0;

	if (numSamples) {
		float prevNote = sortArea[0]->midiNote;
		for (int s = 1; s < numSamples; s++) {
			Sample* thisSample = sortArea[s];
			float noteHere = thisSample->midiNote;
			if (noteHere >= prevNote + 12.5 && noteHere <= prevNote + 13.5) {
				if (whichSampleIsAnOctaveUp) goto skipOctaveCorrection;
				else whichSampleIsAnOctaveUp = s;
			}
			else {
				// If there are other intervals of more than a semitone, we can't really take it for granted what's going on, so get out
				if (noteHere >= prevNote + 1.85) {
					Uart::println("aaa");
					uartPrintlnFloat(noteHere - prevNote);
					goto skipOctaveCorrection;
				}
			}

			prevNote = noteHere;
		}

		if (whichSampleIsAnOctaveUp) {
			Uart::println("correcting octaves");
			// Correct earlier ones?
			if (whichSampleIsAnOctaveUp * 2 < numSamples) {
				for (int s = 0; s < whichSampleIsAnOctaveUp; s++) {
					sortArea[s]->midiNote += 12;
				}
			}

			// Or correct later ones?
			else {
				for (int s = whichSampleIsAnOctaveUp; s < numSamples; s++) {
					sortArea[s]->midiNote -= 12;
				}
			}
		}
	}

skipOctaveCorrection:

	int rangeIndex =
	    0; // Keep this different to the sample index, just in case we need to skip a sample because it has the same pitch as a previous one. Skipping a range would leave our rangeArray with unused space allocated, but that's ok

	int lastTopNote = MIDI_NOTE_ERROR;

	int totalMSec = 0;
	int numWithFileLoopPoints = 0;
	int numWithResultingLoopEndPoints = 0;

	if (soundEditor.currentSource->oscType != OSC_TYPE_SAMPLE) {
		soundEditor.currentSound->unassignAllVoices();
		soundEditor.currentSource->setOscType(OSC_TYPE_SAMPLE);
	}

	Uart::println("creating ranges");

	for (int s = 0; s < numSamples; s++) {

		if (!(s & 31)) AudioEngine::routineWithClusterLoading(); // --------------------------------------------------

		Sample* thisSample = sortArea[s];

		if (thisSample->midiNote == MIDI_NOTE_ERROR) {
			Uart::println("dismissing 1 sample for which pitch couldn't be detected");
			// TODO: shouldn't we remove a reason here?
			continue;
		}

		int topNote = 32767;

		if (s < numSamples - 1) {
			Sample* nextSample = sortArea[s + 1];

			float midPoint = (thisSample->midiNote + nextSample->midiNote) * 0.5;
			topNote = midPoint; // Round down
			if (topNote <= lastTopNote) {
				Uart::print("skipping sample cos ");
				Uart::print(topNote);
				Uart::print(" <= ");
				Uart::println(lastTopNote);
				// TODO: shouldn't we remove a reason here?
				continue;
			}
		}

		MultisampleRange* range;
		if (rangeIndex == 0) {
			range = (MultisampleRange*)soundEditor.currentMultiRange;
		}
		else {
#if ALPHA_OR_BETA_VERSION
			if (soundEditor.currentSource->ranges.elementSize != sizeof(MultisampleRange))
				numericDriver.freezeWithError("E431");
#endif
			range = (MultisampleRange*)soundEditor.currentSource->ranges.insertMultiRange(
			    rangeIndex); // We know it's gonna succeed
		}

		Uart::print("top note: ");
		Uart::println(topNote);

		range->topNote = topNote;

		range->sampleHolder.filePath.set(&thisSample->filePath);
		range->sampleHolder.setAudioFile(thisSample, soundEditor.currentSource->sampleControls.reversed, true);
		bool rangeCoversJustOneNote = (topNote == lastTopNote + 1);
		range->sampleHolder.setTransposeAccordingToSamplePitch(false, doingSingleCycle, rangeCoversJustOneNote,
		                                                       topNote);

		totalMSec += thisSample->getLengthInMSec();
		if (thisSample->fileLoopEndSamples) numWithFileLoopPoints++;
		if (range->sampleHolder.loopEndPos) numWithResultingLoopEndPoints++;

		if (ALPHA_OR_BETA_VERSION && thisSample->numReasonsToBeLoaded <= 0)
			numericDriver.freezeWithError("E216"); // I put this here to try and catch an E004 Luc got
		thisSample->removeReason("E394");          // Remove that temporary reason we added above

		rangeIndex++;
		lastTopNote = topNote;
	}

	numSamples = rangeIndex;

	if (!numSamples) {
		numericDriver.displayPopup(HAVE_OLED ? "Error creating multisampled instrument" : "FAIL");
		goto doReturnFalse;
	}

	Uart::print("distinct ranges: ");
	Uart::println(numSamples);

	generalMemoryAllocator.dealloc(sortArea);

	audioFileIsNowSet();

	int averageMSec = totalMSec / numSamples;

	// If source files had loop points set...
	if (numWithFileLoopPoints * 2 >= numSamples) {

		// If this led to an actual loop end pos, with more waveform after it, and the sample's not too long, we can do a ONCE
		if (numWithResultingLoopEndPoints * 2 >= numSamples && averageMSec < 2002) {
			soundEditor.currentSource->repeatMode = SAMPLE_REPEAT_ONCE;
		}
		else {
			soundEditor.currentSource->repeatMode = SAMPLE_REPEAT_LOOP;
		}
	}

	// Or if no loop points set...
	else {
		// If 2 seconds or less, set play mode to ONCE. Otherwise, cut
		soundEditor.currentSource->repeatMode = (averageMSec < 2002) ? SAMPLE_REPEAT_ONCE : SAMPLE_REPEAT_CUT;
	}

	soundEditor.setCurrentMultiRange(numSamples >> 1);

	exitAndNeverDeleteDrum();
	((Instrument*)currentSong->currentClip->output)->beenEdited();

#if HAVE_OLED
	OLED::removeWorkingAnimation();
#endif
	return true;
}

bool SampleBrowser::importFolderAsKit() {

	AudioEngine::stopAnyPreviewing();

#if HAVE_OLED
	OLED::displayWorkingAnimation("Working");
#else
	numericDriver.displayLoadingAnimation();
#endif

	int numSamples;
	Sample** sortArea;

	int prefixAndDirLength;
	bool success = loadAllSamplesInFolder(false, &numSamples, &sortArea, NULL, &prefixAndDirLength);

	if (!success) {
doReturnFalse:
#if HAVE_OLED
		OLED::removeWorkingAnimation();
#endif
		return false;
	}

	Kit* kit = (Kit*)currentSong->currentClip->output;
	SoundDrum* firstDrum = (SoundDrum*)soundEditor.currentSound;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	{
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

		for (int s = 0; s < numSamples; s++) {

			Sample* thisSample = sortArea[s];

			SoundDrum* drum;
			Source* source;
			MultiRange* range;

			// If the first sample...
			if (s == 0) {

				drum = firstDrum;
				source = &drum->sources[0];
				range = source->getOrCreateFirstRange();
				if (!range) {
getOut:
					f_closedir(&staticDIR);
					numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
					goto doReturnFalse;
				}

				// Ensure osc type is "sample". For the later drums, calling setupAsSample() does this same thing
				if (soundEditor.currentSource->oscType != OSC_TYPE_SAMPLE) {
					soundEditor.currentSound->unassignAllVoices();
					soundEditor.currentSource->setOscType(OSC_TYPE_SAMPLE);
				}

				ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
				ParamSet* paramSet = (ParamSet*)summary->paramCollection;
				int paramId = PARAM_LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex;
				ModelStackWithAutoParam* modelStackWithParam =
				    modelStack->addParam(paramSet, summary, paramId, &paramSet->params[paramId]);

				// Reset osc volume, if it's not automated
				if (!modelStackWithParam->autoParam->isAutomated()) {
					modelStackWithParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithParam,
					                                                                          2147483647);
					//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(PARAM_LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex, 2147483647, 0xFFFFFFFF, 0, firstDrum, currentSong, currentSong->currentClip, false);
				}

				drum->unassignAllVoices();
			}

			// Or, for subsequent samples...
			else {

				// Make the Drum and its ParamManager

				ParamManagerForTimeline paramManager;
				int error = paramManager.setupWithPatching();
				if (error) {
					goto getOut;
				}

				void* drumMemory = generalMemoryAllocator.alloc(sizeof(SoundDrum), NULL, false, true);
				if (!drumMemory) {
					goto getOut;
				}

				drum = new (drumMemory) SoundDrum();
				source = &drum->sources[0];

				range = source->getOrCreateFirstRange();
				if (!range) {
					drum->~Drum();
					generalMemoryAllocator.dealloc(drumMemory);
					goto getOut;
				}

				Sound::initParams(&paramManager);

				kit->addDrum(drum);
				drum->setupAsSample(&paramManager);
				drum->nameIsDiscardable = true;
				currentSong->backUpParamManager(drum, currentSong->currentClip, &paramManager, true);
			}

			AudioFileHolder* holder = range->getAudioFileHolder();
			holder->setAudioFile(NULL);
			holder->filePath.set(&thisSample->filePath);
			holder->setAudioFile(thisSample, source->sampleControls.reversed, true);

			autoDetectSideChainSending(drum, source, thisSample->filePath.get());

			String newName;
			int error = newName.set(&thisSample->filePath.get()[prefixAndDirLength]);
			if (!error) {

				char const* newNameChars = newName.get();
				char const* dotAddress = strrchr(newNameChars, '.');
				if (dotAddress) {
					int dotPos = (uint32_t)dotAddress - (uint32_t)newNameChars;
					newName.shorten(dotPos);
				}

				if (kit->getDrumFromName(newName.get())) {
					error = kit->makeDrumNameUnique(&newName, 2);
					if (error) goto skipNameStuff;
				}

				drum->name.set(&newName);
			}
skipNameStuff:

			source->repeatMode = (thisSample->getLengthInMSec() < 2002) ? SAMPLE_REPEAT_ONCE : SAMPLE_REPEAT_CUT;

#if ALPHA_OR_BETA_VERSION
			if (thisSample->numReasonsToBeLoaded <= 0)
				numericDriver.freezeWithError("E217"); // I put this here to try and catch an E004 Luc got
#endif
			thisSample->removeReason("E395");
		}

		generalMemoryAllocator.dealloc(sortArea);
	}

	// Make NoteRows for all these new Drums
	((Kit*)currentSong->currentClip->output)->resetDrumTempValues();
	firstDrum->noteRowAssignedTemp = 1;
	ModelStackWithTimelineCounter* modelStack = (ModelStackWithTimelineCounter*)modelStackMemory;
	((InstrumentClip*)currentSong->currentClip)->assignDrumsToNoteRows(modelStack);

	((Instrument*)currentSong->currentClip->output)->beenEdited();

	exitAndNeverDeleteDrum();
	uiNeedsRendering(&instrumentClipView);
#if HAVE_OLED
	OLED::removeWorkingAnimation();
#endif
	return true;
}

static const uint32_t zoomUIModes[] = {UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, UI_MODE_AUDITIONING, 0};

int SampleBrowser::horizontalEncoderAction(int offset) {

	if (qwertyVisible) {
doNormal:
		return Browser::horizontalEncoderAction(offset);
	}
	else {

		// Or, maybe we want to scroll or zoom around the waveform...
		if (currentlyShowingSamplePreview
		    && (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) || waveformBasicNavigator.isZoomedIn())) {

			// We're quite likely going to need to read the SD card to do either scrolling or zooming
			if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

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
		}

		// TODO: I don't think we want this anymore...
		/*
		else {
			if (scrollingText && numericDriver.isLayerCurrentlyOnTop(scrollingText)) {
				uiTimerManager.unsetTimer(TIMER_DISPLAY);
				scrollingText->currentPos += offset;

				int maxScroll = scrollingText->length - NUMERIC_DISPLAY_LENGTH;

				if (scrollingText->currentPos < 0) scrollingText->currentPos = 0;
				if (scrollingText->currentPos > maxScroll) scrollingText->currentPos = maxScroll;

				numericDriver.render();
			}
		}
		*/

		else {
			qwertyVisible = true;
			goto doNormal;
		}
		return ACTION_RESULT_DEALT_WITH;
	}
}

int SampleBrowser::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (getRootUI() == &instrumentClipView) {
		if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(xEncButtonX, xEncButtonY))
			return ACTION_RESULT_DEALT_WITH;
		return instrumentClipView.verticalEncoderAction(offset, inCardRoutine);
	}

	return ACTION_RESULT_DEALT_WITH;
}

bool SampleBrowser::canSeeViewUnderneath() {
	return !currentlyShowingSamplePreview && !qwertyVisible;
}

bool SampleBrowser::renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                   uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea) {

	return (qwertyVisible || currentlyShowingSamplePreview);
}
