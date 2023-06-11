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

#include <ArrangerView.h>
#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <ClipInstance.h>
#include <ConsequenceClipExistence.h>
#include <ConsequenceClipInstanceChange.h>
#include <DString.h>
#include <InstrumentClip.h>
#include <InstrumentClipMinder.h>
#include <InstrumentClipView.h>
#include <ParamManager.h>
#include <sounddrum.h>
#include <SessionView.h>
#include "r_typedefs.h"

#include "song.h"
#include "matrixdriver.h"
#include "GeneralMemoryAllocator.h"
#include "numericdriver.h"
#include "instrument.h"
#include "ActionLogger.h"
#include "uart.h"
#include "View.h"
#include "Arrangement.h"
#include "KeyboardScreen.h"
#include "Session.h"
#include "MelodicInstrument.h"
#include "midiengine.h"
#include "kit.h"
#include "drum.h"
#include "MIDIInstrument.h"
#include <new>
#include "ConsequenceArrangerParamsTimeInserted.h"
#include "storagemanager.h"
#include "LoadInstrumentPresetUI.h"
#include "AudioOutput.h"
#include "AudioInputSelector.h"
#include "encoder.h"
#include "uitimermanager.h"
#include "NoteRow.h"
#include "functions.h"
#include "RenameOutputUI.h"
#include "AudioClip.h"
#include "UI.h"
#include "AudioClipView.h"
#include "WaveformRenderer.h"
#include "MenuItemColour.h"
#include "PadLEDs.h"
#include "IndicatorLEDs.h"
#include "Encoders.h"
#include "Buttons.h"
#include "definitions.h"
#include "ModelStack.h"
#include "extern.h"
#include "ParamSet.h"
#include "FileItem.h"
#include "oled.h"

extern "C" {
extern uint8_t currentlyAccessingCard;
#include "sio_char.h"
}

ArrangerView arrangerView;

ArrangerView::ArrangerView() {
	doingAutoScrollNow = false;

	lastInteractedOutputIndex = 0;
	lastInteractedPos = -1;
	lastInteractedSection = 0;
}

#if HAVE_OLED
void ArrangerView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	sessionView.renderOLED(image);
}
#endif

void ArrangerView::moveClipToSession() {
	Output* output = outputsOnScreen[yPressedEffective];
	ClipInstance* clipInstance = output->clipInstances.getElement(pressedClipInstanceIndex);
	Clip* clip = clipInstance->clip;

	// Empty ClipInstance - can't do
	if (!clip) {
		numericDriver.displayPopup(HAVE_OLED ? "Empty clip instances can't be moved to the session" : "EMPTY");
	}

	else {
		// Clip already exists in session - just go to it
		if (!clip->isArrangementOnlyClip()) {
			int index = currentSong->sessionClips.getIndexForClip(clip);
			currentSong->songViewYScroll = index - yPressedEffective;
		}

		// Or, arrangement-only Clip needs moving to session
		else {
			int intendedIndex = currentSong->songViewYScroll + yPressedEffective;

			if (intendedIndex < 0) {
				currentSong->songViewYScroll -= intendedIndex;
				intendedIndex = 0;
			}

			else if (intendedIndex > currentSong->sessionClips.getNumElements()) {
				currentSong->songViewYScroll -= intendedIndex - currentSong->sessionClips.getNumElements();
				intendedIndex = currentSong->sessionClips.getNumElements();
			}

			clip->section = currentSong->getLowestSectionWithNoSessionClipForOutput(output);
			int error = currentSong->sessionClips.insertClipAtIndex(clip, intendedIndex);
			if (error) {
				numericDriver.displayError(error);
				return;
			}
			actionLogger.deleteAllLogs();

			int oldIndex = currentSong->arrangementOnlyClips.getIndexForClip(clip);
			if (oldIndex != -1) currentSong->arrangementOnlyClips.deleteAtIndex(oldIndex);
		}

		goToSongView();

		currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
		sessionView.selectedClipYDisplay = yPressedEffective;
		sessionView.selectedClipPressYDisplay = yPressedActual;
		sessionView.selectedClipPressXDisplay = xPressed;
		sessionView.performActionOnPadRelease = false;
		view.setActiveModControllableTimelineCounter(clip);
	}
}

void ArrangerView::goToSongView() {
	currentSong->xScroll[NAVIGATION_CLIP] = currentSong->xScrollForReturnToSongView;
	currentSong->xZoom[NAVIGATION_CLIP] = currentSong->xZoomForReturnToSongView;
	changeRootUI(&sessionView);
}

int ArrangerView::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	int newInstrumentType;

	// Song button
	if (x == sessionViewButtonX && y == sessionViewButtonY) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			if (currentUIMode == UI_MODE_NONE) {
				goToSongView();
			}
			else if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW) {
				moveClipToSession();
			}
		}
	}

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	// Affect-entire button
	else if (x == affectEntireButtonX && y == affectEntireButtonY) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			currentSong->affectEntire = !currentSong->affectEntire;
			//setLedStates();
			view.setActiveModControllableTimelineCounter(currentSong);
		}
	}
#endif

	// Cross-screen button
	else if (x == crossScreenEditButtonX && y == crossScreenEditButtonY) {
		if (on && currentUIMode == UI_MODE_NONE) {
			currentSong->arrangerAutoScrollModeActive = !currentSong->arrangerAutoScrollModeActive;
			IndicatorLEDs::setLedState(crossScreenEditLedX, crossScreenEditLedY,
			                           currentSong->arrangerAutoScrollModeActive);

			if (currentSong->arrangerAutoScrollModeActive) reassessWhetherDoingAutoScroll();
			else doingAutoScrollNow = false;
		}
	}

	// Record button - adds to what MatrixDriver does with it
	else if (x == recordButtonX && y == recordButtonY) {
		if (on) {
			uiTimerManager.setTimer(TIMER_UI_SPECIFIC, 500);
			blinkOn = true;
		}
		else {
			if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
				currentUIMode = UI_MODE_NONE;
				PadLEDs::reassessGreyout(false);
				uiNeedsRendering(this, 0, 0xFFFFFFFF);
			}
		}
		return ACTION_RESULT_NOT_DEALT_WITH; // Make the MatrixDriver do its normal thing with it too
	}

	// Save/delete button with row held
	else if (x == saveButtonX && y == saveButtonY
	         && (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION
	             || currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW)) {
		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		if (on) deleteOutput();
	}

	// Select encoder button
	else if (x == selectEncButtonX && y == selectEncButtonY && !Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			changeOutputToAudio();
		}
	}

	// Which-instrument-type buttons
	else if (x == synthButtonX && y == synthButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_SYNTH;

doChangeInstrumentType:
		if (on && currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION && !Buttons::isShiftButtonPressed()) {

			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			Output* output = outputsOnScreen[yPressedEffective];

			// AudioOutputs - need to replace with Instrument
			if (output->type == OUTPUT_TYPE_AUDIO) {
				changeOutputToInstrument(newInstrumentType);
			}

			// Instruments - just change type
			else {

				// If load button held, go into LoadInstrumentPresetUI
				if (Buttons::isButtonPressed(loadButtonX, loadButtonY)) {

					// Can't do that for MIDI or CV tracks though
					if (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT || newInstrumentType == INSTRUMENT_TYPE_CV) {
						goto doActualSimpleChange;
					}

					if (!output) return ACTION_RESULT_DEALT_WITH;

					actionLogger.deleteAllLogs();

					currentUIMode = UI_MODE_NONE;
					endAudition(output);

					Browser::instrumentTypeToLoad = newInstrumentType;
					loadInstrumentPresetUI.instrumentToReplace = (Instrument*)output;
					loadInstrumentPresetUI.instrumentClipToLoadFor = NULL;
					openUI(&loadInstrumentPresetUI);
				}

				// Otherwise, just change the instrument type
				else {
doActualSimpleChange:
					changeInstrumentType(newInstrumentType);
				}
			}
		}
	}

	else if (x == kitButtonX && y == kitButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_KIT;
		goto doChangeInstrumentType;
	}

	else if (x == midiButtonX && y == midiButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_MIDI_OUT;
		goto doChangeInstrumentType;
	}

	else if (x == cvButtonX && y == cvButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_CV;
		goto doChangeInstrumentType;
	}

	// Back button with <> button held
	else if (x == backButtonX && y == backButtonY && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			clearArrangement();
		}
	}

	else return TimelineView::buttonAction(x, y, on, inCardRoutine);

	return ACTION_RESULT_DEALT_WITH;
}

void ArrangerView::deleteOutput() {

	Output* output = outputsOnScreen[yPressedEffective];
	if (!output) return;

	char const* errorMessage;

	if (currentSong->getNumOutputs() <= 1) {
		errorMessage = "Can't delete final Clip";
cant:
		numericDriver.displayPopup(HAVE_OLED ? errorMessage : "CANT");
		return;
	}

	for (int i = 0; i < output->clipInstances.getNumElements(); i++) {
		if (output->clipInstances.getElement(i)->clip) {
			errorMessage = "Delete all track's clips first";
			goto cant;
		}
	}

	if (currentSong->getSessionClipWithOutput(output)) {
		errorMessage = "Track still has clips in session";
		goto cant;
	}

	output->clipInstances.empty(); // Because none of these have Clips, this is ok
	output->cutAllSound();
	currentSong->deleteOrHibernateOutput(output);

	auditionEnded();

	repopulateOutputsOnScreen(true);
}

void ArrangerView::clearArrangement() {

	numericDriver.displayPopup(HAVE_OLED ? "Arrangement cleared" : "CLEAR");

	if (arrangement.hasPlaybackActive()) playbackHandler.endPlayback();

	Action* action = actionLogger.getNewAction(ACTION_ARRANGEMENT_CLEAR, false);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
	currentSong->paramManager.deleteAllAutomation(action, modelStack);

	// We go through deleting the ClipInstances one by one. This is actually quite inefficient, but complicated to improve on because the deletion of the Clips themselves,
	// where there are arrangement-only ones, causes the calling of output->pickAnActiveClipIfPossible. So we have to ensure that extra ClipInstances don't exist at any instant in time,
	// or else it'll look at those to pick the new activeClip, which might not exist anymore.
	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		for (int i = output->clipInstances.getNumElements() - 1; i >= 0; i--) {
			deleteClipInstance(output, i, output->clipInstances.getElement(i), action, false);
		}
	}

	uiNeedsRendering(this, 0xFFFFFFFF, 0);
}

bool ArrangerView::opened() {

	mustRedrawTickSquares = true;

	focusRegained();

	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);
	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[displayHeight], &PadLEDs::occupancyMaskStore[displayHeight]);
		renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[displayHeight], &PadLEDs::occupancyMaskStore[displayHeight]);
	}
	else {
		uiNeedsRendering(this);
	}

	return true;
}

void ArrangerView::setLedStates() {

	IndicatorLEDs::setLedState(synthLedX, synthLedY, false);
	IndicatorLEDs::setLedState(kitLedX, kitLedY, false);
	IndicatorLEDs::setLedState(midiLedX, midiLedY, false);
	IndicatorLEDs::setLedState(cvLedX, cvLedY, false);

	IndicatorLEDs::setLedState(crossScreenEditLedX, crossScreenEditLedY, currentSong->arrangerAutoScrollModeActive);

#ifdef currentClipStatusButtonX
	view.switchOffCurrentClipPad();
#endif
}

void ArrangerView::focusRegained() {

	view.focusRegained();

	repopulateOutputsOnScreen(false);

#if !HAVE_OLED
	sessionView.redrawNumericDisplay();
#endif
	if (currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW) view.setActiveModControllableTimelineCounter(currentSong);

	IndicatorLEDs::setLedState(backLedX, backLedY, false);

	IndicatorLEDs::setLedState(scaleModeLedX, scaleModeLedY, false);
	setLedStates();

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	IndicatorLEDs::setLedState(keyboardLedX, keyboardLedX, false);
#endif

	currentSong->lastClipInstanceEnteredStartPos = 0;

	if (!doingAutoScrollNow) reassessWhetherDoingAutoScroll(); // Can start, but can't stop
}

void ArrangerView::repopulateOutputsOnScreen(bool doRender) {
	// First, clear out the Outputs onscreen
	memset(outputsOnScreen, 0, sizeof(outputsOnScreen));

	Output* output = currentSong->firstOutput;
	int row = 0 - currentSong->arrangementYScroll;
	while (output) {
		if (row >= displayHeight) break;
		if (row >= 0) {
			outputsOnScreen[row] = output;
		}
		row++;
		output = output->next;
	}

	mustRedrawTickSquares = true;

	if (doRender) {
		uiNeedsRendering(this);
	}
}

bool ArrangerView::renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                 uint8_t occupancyMask[][displayWidth + sideBarWidth]) {
	if (!image) return true;

	for (int i = 0; i < displayHeight; i++) {
		if (whichRows & (1 << i)) {
			drawMuteSquare(i, image[i]);
			drawAuditionSquare(i, image[i]);
		}
	}
	return true;
}

void ArrangerView::drawMuteSquare(int yDisplay, uint8_t thisImage[][3]) {
	uint8_t* thisColour = thisImage[displayWidth];

	// If no Instrument, black
	if (!outputsOnScreen[yDisplay]) {
doBlack:
		memset(thisColour, 0, 3);
	}

	else if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING && outputsOnScreen[yDisplay]->armedForRecording) {
		if (blinkOn) {
			if (outputsOnScreen[yDisplay]->wantsToBeginArrangementRecording()) {
				thisColour[0] = 255;
				thisColour[1] = 1;
				thisColour[2] = 0;
			}
			else {
				thisColour[0] = 60;
				thisColour[1] = 25;
				thisColour[2] = 15;
			}
		}
		else {
			goto doBlack;
		}
	}

	// Soloing - blue
	else if (outputsOnScreen[yDisplay]->soloingInArrangementMode) {
		soloColourMenu.getRGB(thisColour);
	}

	// Or if not soloing...
	else {

		// Muted - yellow
		if (outputsOnScreen[yDisplay]->mutedInArrangementMode) {
			mutedColourMenu.getRGB(thisColour);
		}

		// Otherwise, green
		else {
			activeColourMenu.getRGB(thisColour);
		}

		if (currentSong->getAnyOutputsSoloingInArrangement()) {
			dimColour(thisColour);
		}
	}
}

void ArrangerView::drawAuditionSquare(int yDisplay, uint8_t thisImage[][3]) {
	uint8_t* thisColour = thisImage[displayWidth + 1];

	if (view.midiLearnFlashOn) {
		Output* output = outputsOnScreen[yDisplay];

		if (!output || output->type == OUTPUT_TYPE_AUDIO || output->type == INSTRUMENT_TYPE_KIT) goto drawNormally;

		MelodicInstrument* melodicInstrument = (MelodicInstrument*)output;

		// If MIDI command already assigned...
		if (melodicInstrument->midiInput.containsSomething()) {
			thisColour[0] = midiCommandColourRed;
			thisColour[1] = midiCommandColourGreen;
			thisColour[2] = midiCommandColourBlue;
		}

		// Or if not assigned but we're holding it down...
		else if (view.thingPressedForMidiLearn == MIDI_LEARN_MELODIC_INSTRUMENT_INPUT
		         && view.learnedThing == &melodicInstrument->midiInput) {
			thisColour[0] = 128;
			thisColour[1] = 0;
			thisColour[2] = 0;
		}
		else goto drawNormally;
	}

	else {
drawNormally:
		if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION && yDisplay == yPressedEffective) {
			thisColour[0] = 255;
			thisColour[1] = 0;
			thisColour[2] = 0;
		}
		else {
			memset(thisColour, 0, 3);
		}
	}
}

ModelStackWithNoteRow* ArrangerView::getNoteRowForAudition(ModelStack* modelStack, Kit* kit) {

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(kit->activeClip);

	ModelStackWithNoteRow* modelStackWithNoteRow;

	if (kit->activeClip) {

		InstrumentClip* instrumentClip = (InstrumentClip*)kit->activeClip;
		modelStackWithNoteRow = instrumentClip->getNoteRowForDrumName(modelStackWithTimelineCounter, "SNAR");
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
			if (kit->selectedDrum)
				modelStackWithNoteRow =
				    instrumentClip->getNoteRowForDrum(modelStackWithTimelineCounter, kit->selectedDrum);
			if (!modelStackWithNoteRow->getNoteRowAllowNull())
				modelStackWithNoteRow =
				    modelStackWithTimelineCounter->addNoteRow(0, instrumentClip->noteRows.getElement(0));
		}
	}
	else {
		modelStackWithNoteRow = modelStackWithTimelineCounter->addNoteRow(0, NULL);
	}

	return modelStackWithNoteRow;
}

Drum* ArrangerView::getDrumForAudition(Kit* kit) {
	Drum* drum = kit->getDrumFromName("SNAR");
	if (!drum) {
		drum = kit->selectedDrum;
		if (!drum) drum = kit->firstDrum;
	}

	return drum;
}

void ArrangerView::beginAudition(Output* output) {

	if (output->type == OUTPUT_TYPE_AUDIO) return;

	Instrument* instrument = (Instrument*)output;

	if (!playbackHandler.playbackState && !Buttons::isShiftButtonPressed()) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		if (instrument->type == INSTRUMENT_TYPE_KIT) {

			Kit* kit = (Kit*)instrument;
			ModelStackWithNoteRow* modelStackWithNoteRow = getNoteRowForAudition(modelStack, kit);

			Drum* drum;
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (noteRow) {
				drum = noteRow->drum;
				if (drum && drum->type == DRUM_TYPE_SOUND && !noteRow->paramManager.containsAnyMainParamCollections())
					numericDriver.freezeWithError("E324"); // Vinz got this! I may have since fixed.
			}
			else {
				drum = getDrumForAudition(kit);
			}

			if (drum) kit->beginAuditioningforDrum(modelStackWithNoteRow, drum, kit->defaultVelocity, zeroMPEValues);
		}
		else {
			int note = (currentSong->rootNote + 120) % 12;
			note += 60;
			((MelodicInstrument*)instrument)
			    ->beginAuditioningForNote(modelStack, note, instrument->defaultVelocity, zeroMPEValues);
		}
	}
}

void ArrangerView::endAudition(Output* output, bool evenIfPlaying) {

	if (output->type == OUTPUT_TYPE_AUDIO) return;

	Instrument* instrument = (Instrument*)output;

	if (!playbackHandler.playbackState || evenIfPlaying) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		if (instrument->type == INSTRUMENT_TYPE_KIT) {

			Kit* kit = (Kit*)instrument;
			ModelStackWithNoteRow* modelStackWithNoteRow = getNoteRowForAudition(modelStack, kit);

			Drum* drum;
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (noteRow) {
				drum = noteRow->drum;
			}
			else {
				drum = getDrumForAudition(kit);
			}

			if (drum) kit->endAuditioningForDrum(modelStackWithNoteRow, drum);
		}
		else {
			int note = (currentSong->rootNote + 120) % 12;
			note += 60;
			((MelodicInstrument*)instrument)->endAuditioningForNote(modelStack, note);
		}
	}
}

void ArrangerView::changeOutputToInstrument(int newInstrumentType) {

	Output* oldOutput = outputsOnScreen[yPressedEffective];
	if (oldOutput->type != OUTPUT_TYPE_AUDIO) return;

	if (currentSong->getClipWithOutput(oldOutput)) {
		numericDriver.displayPopup(HAVE_OLED ? "Audio tracks with clips can't be turned into an instrument" : "CANT");
		return;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	bool instrumentAlreadyInSong; // Will always end up false

	Instrument* newInstrument = createNewInstrument(newInstrumentType, &instrumentAlreadyInSong);
	if (!newInstrument) {
		return;
	}

	currentSong->replaceOutputLowLevel(newInstrument, oldOutput);

	outputsOnScreen[yPressedEffective] = newInstrument;

	view.displayOutputName(newInstrument);
#if HAVE_OLED
	OLED::sendMainImage();
#endif

	view.setActiveModControllableTimelineCounter(NULL);

	beginAudition(newInstrument);
}

// Loads from file, etc - doesn't truly "create"
Instrument* ArrangerView::createNewInstrument(int newInstrumentType, bool* instrumentAlreadyInSong) {
	ReturnOfConfirmPresetOrNextUnlaunchedOne result;

	result.error = Browser::currentDir.set(getInstrumentFolder(newInstrumentType));
	if (result.error) {
displayError:
		numericDriver.displayError(result.error);
		return NULL;
	}

	result = Browser::findAnUnlaunchedPresetIncludingWithinSubfolders(currentSong, newInstrumentType,
	                                                                  AVAILABILITY_INSTRUMENT_UNUSED);
	if (result.error) goto displayError;

	Instrument* newInstrument = result.fileItem->instrument;
	bool isHibernating = newInstrument && !result.fileItem->instrumentAlreadyInSong;
	*instrumentAlreadyInSong = newInstrument && result.fileItem->instrumentAlreadyInSong;

	if (!newInstrument) {
		String newPresetName;
		result.fileItem->getDisplayNameWithoutExtension(&newPresetName);
		result.error =
		    storageManager.loadInstrumentFromFile(currentSong, NULL, newInstrumentType, false, &newInstrument,
		                                          &result.fileItem->filePointer, &newPresetName, &Browser::currentDir);
	}

	Browser::emptyFileItems();

	if (result.error) goto displayError;

	if (isHibernating) currentSong->removeInstrumentFromHibernationList(newInstrument);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	newInstrument->setupWithoutActiveClip(modelStack);

	return newInstrument;
}

void ArrangerView::auditionPadAction(bool on, int y) {

	int note = (currentSong->rootNote + 120) % 12;
	note += 60;

	// Press on
	if (on) {

		if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
			endAudition(outputsOnScreen[yPressedEffective]);
			IndicatorLEDs::setLedState(synthLedX, synthLedY, false);
			IndicatorLEDs::setLedState(kitLedX, kitLedY, false);
			IndicatorLEDs::setLedState(midiLedX, midiLedY, false);
			IndicatorLEDs::setLedState(cvLedX, cvLedY, false);

			currentUIMode = UI_MODE_NONE;

			uiNeedsRendering(this, 0x00000000, 0xFFFFFFFF);

			goto doNewPress;
		}

		if (currentUIMode == UI_MODE_NONE) {
doNewPress:

			Output* output = outputsOnScreen[y];

			yPressedEffective = y;
			yPressedActual = y;

			// If nothing on this row yet, we'll add a brand new Instrument
			if (!output) {

				int minY = -currentSong->arrangementYScroll - 1;
				int maxY = -currentSong->arrangementYScroll + currentSong->getNumOutputs();

				yPressedEffective = getMax((int)yPressedEffective, minY);
				yPressedEffective = getMin((int)yPressedEffective, maxY);

				bool instrumentAlreadyInSong; // Will always end up false

				output = createNewInstrument(INSTRUMENT_TYPE_SYNTH, &instrumentAlreadyInSong);
				if (!output) return;

				if (!instrumentAlreadyInSong)
					currentSong->addOutput(
					    output,
					    (yPressedEffective == -currentSong->arrangementYScroll - 1)); // This should always be triggered

				outputsOnScreen[yPressedEffective] = output;
			}

			currentUIMode = UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION;

			view.displayOutputName(output);
#if HAVE_OLED
			OLED::sendMainImage();
#endif

			beginAudition(output);

			if (output->activeClip) {
				view.setActiveModControllableTimelineCounter(output->activeClip);
			}
			else {
				view.setActiveModControllableWithoutTimelineCounter(output->toModControllable(),
				                                                    output->getParamManager(currentSong));
			}

			uiNeedsRendering(this, 0, 1 << yPressedEffective);
		}
	}

	// Release press
	else {
		if (y == yPressedActual) {
			exitSubModeWithoutAction();
		}
	}
}

void ArrangerView::auditionEnded() {
	setNoSubMode();
	setLedStates();

#if HAVE_OLED
	renderUIsForOled();
#else
	sessionView.redrawNumericDisplay();
#endif

	view.setActiveModControllableTimelineCounter(currentSong);
}

int ArrangerView::padAction(int x, int y, int velocity) {

	if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

	Output* output = outputsOnScreen[y];

	// Audition pad
	if (x == displayWidth + 1) {
		switch (currentUIMode) {
		case UI_MODE_MIDI_LEARN:
			if (output) {
				if (output->type == OUTPUT_TYPE_AUDIO) {
					if (velocity) {
						view.endMIDILearn();
						audioInputSelector.audioOutput = (AudioOutput*)output;
						audioInputSelector.setupAndCheckAvailability();
						openUI(&audioInputSelector);
					}
				}
				else if (output->type == INSTRUMENT_TYPE_KIT) {
					if (velocity)
						numericDriver.displayPopup(HAVE_OLED ? "MIDI must be learned to kit items individually"
						                                     : "CANT");
				}
				else {
					view.melodicInstrumentMidiLearnPadPressed(velocity, (MelodicInstrument*)output);
				}
			}
			break;

		default:
			auditionPadAction(velocity, y);
			break;
		}
	}

	// Status pad
	else if (x == displayWidth) {

		if (!output) return ACTION_RESULT_DEALT_WITH;

		if (velocity) {
			uint32_t rowsToRedraw = 1 << y;

			switch (currentUIMode) {
			case UI_MODE_VIEWING_RECORD_ARMING:
				output->armedForRecording = !output->armedForRecording;
				PadLEDs::reassessGreyout(true);
				return ACTION_RESULT_DEALT_WITH; // No need to draw anything

#ifdef soloButtonX
			case UI_MODE_SOLO_BUTTON_HELD:
#else
			case UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON:
#endif // Soloing
				if (!output->soloingInArrangementMode) {

					if (arrangement.hasPlaybackActive()) {

						// If other Instruments were already soloing, or if they weren't but this instrument was muted, we'll need to tell it to start playing
						if (currentSong->getAnyOutputsSoloingInArrangement() || output->mutedInArrangementMode) {
							outputActivated(output);
						}
					}

					// If we're the first Instrument to be soloing, need to tell others they've been inadvertedly deactivated
					if (!currentSong->getAnyOutputsSoloingInArrangement()) {
						for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
							if (thisOutput != output && !thisOutput->mutedInArrangementMode) {
								outputDeactivated(thisOutput);
							}
						}
					}

					// If no other soloing previously...
					if (!currentSong->anyOutputsSoloingInArrangement) {
						currentSong->anyOutputsSoloingInArrangement = true;
						rowsToRedraw = 0xFFFFFFFF; // Redraw other mute pads
					}

					output->soloingInArrangementMode = true;
				}

				// Unsoloing
				else {
doUnsolo:
					output->soloingInArrangementMode = false;
					currentSong->reassessWhetherAnyOutputsSoloingInArrangement();

					// If no more soloing, redraw other mute pads
					if (!currentSong->anyOutputsSoloingInArrangement) {
						rowsToRedraw = 0xFFFFFFFF;
					}

					// If any other Instruments still soloing, or if we're "muted", deactivate us
					if (currentSong->getAnyOutputsSoloingInArrangement() || output->mutedInArrangementMode) {
						outputDeactivated(output);
					}

					if (arrangement.hasPlaybackActive()) {

						// If no other Instruments still soloing, re-activate all the other ones
						if (!currentSong->getAnyOutputsSoloingInArrangement()) {
							for (Output* thisOutput = currentSong->firstOutput; thisOutput;
							     thisOutput = thisOutput->next) {
								if (thisOutput != output && !thisOutput->mutedInArrangementMode) {
									outputActivated(thisOutput);
								}
							}
						}
					}
				}
				break;

			case UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION:
				// If it's the mute pad for the same row we're auditioning, don't do anything. User might be subconsciously repeating the "drag row" action for Kits in InstrumentClipView.
				if (y == yPressedEffective) break;
				goto regularMutePadPress; // Otherwise, do normal.

			case UI_MODE_NONE:
				// If the user was just quick and is actually holding the record button but the submode just hasn't changed yet...
				if (velocity && Buttons::isButtonPressed(recordButtonX, recordButtonY)) {
					output->armedForRecording = !output->armedForRecording;
					timerCallback();                 // Get into UI_MODE_VIEWING_RECORD_ARMING
					return ACTION_RESULT_DEALT_WITH; // No need to draw anything
				}
				// No break

			case UI_MODE_HOLDING_ARRANGEMENT_ROW:
regularMutePadPress:
				// If it's soloing, unsolo.
				if (output->soloingInArrangementMode) goto doUnsolo;

				// Unmuting
				if (output->mutedInArrangementMode) {
					output->mutedInArrangementMode = false;
					if (arrangement.hasPlaybackActive() && !currentSong->getAnyOutputsSoloingInArrangement()) {
						outputActivated(output);
					}
				}

				// Muting
				else {
					if (!currentSong->getAnyOutputsSoloingInArrangement()) {
						outputDeactivated(output);
					}
					output->mutedInArrangementMode = true;
				}
				break;
			}

			uiNeedsRendering(this, 0, rowsToRedraw);
			mustRedrawTickSquares = true;
		}
	}

	// Edit pad
	else {
		if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
			if (velocity) {

				// NAME shortcut
				if (x == 11 && y == 5) {
					Output* output = outputsOnScreen[yPressedEffective];
					if (output && output->type != INSTRUMENT_TYPE_MIDI_OUT && output->type != INSTRUMENT_TYPE_CV) {
						endAudition(output);
						currentUIMode = UI_MODE_NONE;
						renameOutputUI.output = output;
						openUI(&renameOutputUI);
						uiNeedsRendering(this, 0, 0xFFFFFFFF); // Stop audition pad being illuminated
					}
				}
			}
#endif
		}
		else {
			if (output) {
				editPadAction(x, y, velocity);
			}
		}
	}

	return ACTION_RESULT_DEALT_WITH;
}

void ArrangerView::outputActivated(Output* output) {

	if (output->recordingInArrangement) return;

	int32_t actualPos = arrangement.getLivePos();

	int i = output->clipInstances.search(actualPos + 1, LESS);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);
	if (clipInstance && clipInstance->pos + clipInstance->length > actualPos) {
		arrangement.resumeClipInstancePlayback(clipInstance);
	}

	playbackHandler.expectEvent(); // In case it doesn't get called by the above call instead
}

void ArrangerView::outputDeactivated(Output* output) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	output->stopAnyAuditioning(modelStack);

	if (arrangement.hasPlaybackActive()) {
		if (output->activeClip && !output->recordingInArrangement) {
			output->activeClip->expectNoFurtherTicks(currentSong);
			output->activeClip->activeIfNoSolo = false;
		}
	}
}

// For now, we're always supplying clearingWholeArrangement as false, even when we are doing that
void ArrangerView::deleteClipInstance(Output* output, int clipInstanceIndex, ClipInstance* clipInstance, Action* action,
                                      bool clearingWholeArrangement) {

	if (action) action->recordClipInstanceExistenceChange(output, clipInstance, DELETE);
	Clip* clip = clipInstance->clip;

	// Delete the ClipInstance
	if (!clearingWholeArrangement) output->clipInstances.deleteAtIndex(clipInstanceIndex);

	currentSong->deletingClipInstanceForClip(output, clip, action, !clearingWholeArrangement);
}

void ArrangerView::rememberInteractionWithClipInstance(int yDisplay, ClipInstance* clipInstance) {
	lastInteractedOutputIndex = yDisplay + currentSong->arrangementYScroll;
	lastInteractedPos = clipInstance->pos;
	lastInteractedSection = clipInstance->clip ? clipInstance->clip->section : 255;
}

void ArrangerView::editPadAction(int x, int y, bool on) {
	Output* output = outputsOnScreen[y];
	uint32_t xScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT];

	// Shift button pressed - clone ClipInstance to white / unique
	if (Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_NONE) {
			int32_t squareStart = getPosFromSquare(x, xScroll);
			int32_t squareEnd = getPosFromSquare(x + 1, xScroll);

			int i = output->clipInstances.search(squareEnd, LESS);
			ClipInstance* clipInstance = output->clipInstances.getElement(i);
			if (clipInstance && clipInstance->pos + clipInstance->length >= squareStart) {
				Clip* oldClip = clipInstance->clip;

				if (oldClip && !oldClip->isArrangementOnlyClip() && !oldClip->getCurrentlyRecordingLinearly()) {
					actionLogger.deleteAllLogs();

					int error = arrangement.doUniqueCloneOnClipInstance(clipInstance, clipInstance->length, true);
					if (error) {
						numericDriver.displayError(error);
					}
					else {
						uiNeedsRendering(this, 1 << y, 0);
					}
				}
				rememberInteractionWithClipInstance(y, clipInstance);
			}
		}
	}

	else {

		if (on) {

			int32_t squareStart = getPosFromSquare(x, xScroll);
			int32_t squareEnd = getPosFromSquare(x + 1, xScroll);

			if (squareStart >= squareEnd) numericDriver.freezeWithError("E210");

			// No previous press
			if (currentUIMode == UI_MODE_NONE) {

doNewPress:
				output->clipInstances.testSequentiality("E117");

				int i = output->clipInstances.search(squareEnd, LESS);
				ClipInstance* clipInstance = output->clipInstances.getElement(i);

				// If there was at least a ClipInstance somewhere to the left...
				if (clipInstance) {

					Clip* clip = clipInstance->clip;

					// If it's being recorded to, some special instructions
					if (clip) {

						// If recording to Clip...
						if (playbackHandler.playbackState && output->recordingInArrangement
						    && clip->getCurrentlyRecordingLinearly()) {
							if (squareStart < arrangement.getLivePos()) {
								// Can't press here to the left of the play/record cursor!
								return;
							}

							// Or, if pressed to the right
							else {
								goto makeNewInstance;
							}
						}
					}

					// Or, normal case where not recording to Clip. If it actually finishes to our left, we can still go ahead and make a new Instance here
					int32_t instanceEnd = clipInstance->pos + clipInstance->length;
					if (instanceEnd <= squareStart) goto makeNewInstance;

					// If still here, the ClipInstance overlaps this square, so select it
					pressedClipInstanceIndex = i;

					pressedHead = (clipInstance->pos >= squareStart);

					actionOnDepress = true;
				}

				// Or, if no ClipInstance anywhere to the left, make a new one
				else {
makeNewInstance:
					// Decide what Clip / section to make this new ClipInstance
					Clip* newClip;

					Output* lastOutputInteractedWith = currentSong->getOutputFromIndex(lastInteractedOutputIndex);
					int lastClipInstanceI =
					    lastOutputInteractedWith->clipInstances.search(lastInteractedPos, GREATER_OR_EQUAL);
					ClipInstance* lastClipInstance =
					    lastOutputInteractedWith->clipInstances.getElement(lastClipInstanceI);

					// Test thing
					{
						int j = output->clipInstances.search(squareStart, GREATER_OR_EQUAL);
						ClipInstance* nextClipInstance = output->clipInstances.getElement(j);
						if (nextClipInstance && nextClipInstance->pos == squareStart) {
							numericDriver.freezeWithError("E233"); // Yes, this happened to someone. Including me!!
						}
					}

					if (lastClipInstance && lastClipInstance->pos == lastInteractedPos) {

						// If same Output...
						if (lastOutputInteractedWith == output) {
							if (!lastClipInstance->clip || lastClipInstance->clip->isArrangementOnlyClip()) {
								newClip = NULL;
							}
							else {
								newClip = lastClipInstance->clip;
							}
						}

						// Or if different Output...
						else {

							// If no Clip, easy
							if (!lastClipInstance->clip) {
								newClip = NULL;
							}

							// If yes Clip, look for another one with that section
							else {
								lastInteractedSection = lastClipInstance->clip->section;
								goto getItFromSection;
							}
						}
					}
					else {
getItFromSection:
						if (lastInteractedSection == 255) {
							newClip = NULL;
						}
						else {
							newClip = currentSong->getSessionClipWithOutput(output, lastInteractedSection);

							// If that section had none, just get any old one (still might return NULL - that's fine)
							if (!newClip) {
								newClip = currentSong->getSessionClipWithOutput(output);
							}
						}
					}

					// Make the actual new ClipInstance. Do it now, after potentially looking at existing ones above, so that we don't look at this new one above
					pressedClipInstanceIndex = output->clipInstances.insertAtKey(squareStart);

					// Test thing
					{
						ClipInstance* nextInstance = output->clipInstances.getElement(pressedClipInstanceIndex + 1);
						if (nextInstance) {
							if (nextInstance->pos == squareStart) numericDriver.freezeWithError("E232");
						}
					}

					clipInstance = output->clipInstances.getElement(pressedClipInstanceIndex);
					if (!clipInstance) {
						numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
						return;
					}

					clipInstance->clip = newClip;

					if (clipInstance->clip) clipInstance->length = clipInstance->clip->loopLength;
					else clipInstance->length = DEFAULT_CLIP_LENGTH << currentSong->insideWorldTickMagnitude;

					if (clipInstance->length < 1) numericDriver.freezeWithError("E049");

					ClipInstance* nextInstance = output->clipInstances.getElement(pressedClipInstanceIndex + 1);
					if (nextInstance) {

						if (nextInstance->pos == squareStart) numericDriver.freezeWithError("E209");

						int32_t maxLength = nextInstance->pos - squareStart;
						if (clipInstance->length > maxLength) {
							clipInstance->length = maxLength;
							if (clipInstance->length < 1) numericDriver.freezeWithError("E048");
						}
					}

					if (clipInstance->length > MAX_SEQUENCE_LENGTH - clipInstance->pos) {
						clipInstance->length = MAX_SEQUENCE_LENGTH - clipInstance->pos;
						if (clipInstance->length < 1) numericDriver.freezeWithError("E045");
					}

					Action* action = actionLogger.getNewAction(ACTION_CLIP_INSTANCE_EDIT, false);
					if (action) action->recordClipInstanceExistenceChange(output, clipInstance, CREATE);

					arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length, NULL,
					                      clipInstance);

					uiNeedsRendering(this, 1 << y, 0);

					actionOnDepress = false;
					pressedHead = true;
				}

				xPressed = x;
				yPressedEffective = y;
				yPressedActual = y;
				currentUIMode = UI_MODE_HOLDING_ARRANGEMENT_ROW;
				pressTime = AudioEngine::audioSampleTimer;
				desiredLength = clipInstance->length;
				pressedClipInstanceXScrollWhenLastInValidPosition = xScroll;
				pressedClipInstanceIsInValidPosition = true;
				pressedClipInstanceOutput = output;
				view.setActiveModControllableTimelineCounter(clipInstance->clip);

				if (clipInstance->clip) {
					originallyPressedClipActualLength = clipInstance->clip->loopLength;
				}
				else {
					originallyPressedClipActualLength = clipInstance->length;
				}

				rememberInteractionWithClipInstance(y, clipInstance);
			}

			// Already pressing - length edit
			else if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW) {

				if (y != yPressedEffective) {
					if (!pressedClipInstanceIsInValidPosition) return;
					goto doNewPress;
				}

				if (x > xPressed) {

					actionOnDepress = false;

					if (!pressedClipInstanceIsInValidPosition) return;

					int32_t oldSquareStart = getPosFromSquare(xPressed);
					int32_t oldSquareEnd = getPosFromSquare(xPressed + 1);

					// Search for previously pressed ClipInstance
					ClipInstance* clipInstance = output->clipInstances.getElement(pressedClipInstanceIndex);

					int32_t lengthTilNewSquareStart = squareStart - clipInstance->pos;

					desiredLength = clipInstance->length; // I don't think this should still be here...

					// Shorten
					if (clipInstance->length > lengthTilNewSquareStart) {
						Action* action = actionLogger.getNewAction(ACTION_CLIP_INSTANCE_EDIT, true);
						if (clipInstance->clip)
							arrangement.rowEdited(output, clipInstance->pos + lengthTilNewSquareStart,
							                      clipInstance->pos + clipInstance->length, clipInstance->clip, NULL);
						clipInstance->change(action, output, clipInstance->pos, lengthTilNewSquareStart,
						                     clipInstance->clip);
					}

					// Lengthen
					else {

						int32_t oldLength = clipInstance->length;

						int32_t newLength = squareEnd - clipInstance->pos;

						// Make sure it doesn't collide with next ClipInstance
						ClipInstance* nextClipInstance = output->clipInstances.getElement(pressedClipInstanceIndex + 1);
						if (nextClipInstance) {
							int32_t maxLength = nextClipInstance->pos - clipInstance->pos;
							if (newLength > maxLength) newLength = maxLength;
						}

						if (newLength > MAX_SEQUENCE_LENGTH - clipInstance->pos)
							newLength = MAX_SEQUENCE_LENGTH - clipInstance->pos;

						// If we are in fact able to lengthen it...
						if (newLength > oldLength) {

							Action* action = actionLogger.getNewAction(ACTION_CLIP_INSTANCE_EDIT, true);

							clipInstance->change(action, output, clipInstance->pos, newLength, clipInstance->clip);
							arrangement.rowEdited(output, clipInstance->pos + oldLength,
							                      clipInstance->pos + clipInstance->length, NULL, clipInstance);
						}
					}

					desiredLength = clipInstance->length;

					uiNeedsRendering(this, 1 << y, 0);
				}
			}
		}

		// Release press
		else {

			if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW)) {

				// If also stuttering, stop that
				if (isUIModeActive(UI_MODE_STUTTERING)) {
					((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
					    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
				}

				if (x == xPressed && y == yPressedEffective) {

					// If no action to perform...
					if (!actionOnDepress || (int32_t)(AudioEngine::audioSampleTimer - pressTime) >= (44100 >> 1)) {
justGetOut:
						exitSubModeWithoutAction();
					}

					// Or if yes we do want to do some action...
					else {
						ClipInstance* clipInstance =
						    output->clipInstances.getElement(pressedClipInstanceIndex); // Can't fail, I think?

						// If pressed head, delete
						if (pressedHead) {
							view.setActiveModControllableTimelineCounter(currentSong);

							arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length,
							                      clipInstance->clip, NULL);

							Action* action = actionLogger.getNewAction(ACTION_CLIP_INSTANCE_EDIT, false);
							deleteClipInstance(output, pressedClipInstanceIndex, clipInstance, action);
							goto justGetOut;
						}

						// Otherwise, go into Clip
						else {

							// In this case, we leave the activeModControllableClip the same

							// If Clip wasn't created yet, create it first. This does both AudioClips and InstrumentClips
							if (!clipInstance->clip) {

								if (!currentSong->arrangementOnlyClips.ensureEnoughSpaceAllocated(1)) goto justGetOut;

								int size =
								    (output->type == OUTPUT_TYPE_AUDIO) ? sizeof(AudioClip) : sizeof(InstrumentClip);

								void* memory = generalMemoryAllocator.alloc(size, NULL, false, true);
								if (!memory) {
									numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
									goto justGetOut;
								}

								Clip* newClip;

								if (output->type == OUTPUT_TYPE_AUDIO) newClip = new (memory) AudioClip();
								else newClip = new (memory) InstrumentClip(currentSong);

								newClip->loopLength = clipInstance->length;
								newClip->section = 255;
								newClip->activeIfNoSolo =
								    false; // Always need to set arrangement-only Clips like this on create

								char modelStackMemory[MODEL_STACK_MAX_SIZE];
								ModelStackWithTimelineCounter* modelStack =
								    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, newClip);

								int error;

								if (output->type == OUTPUT_TYPE_AUDIO)
									error = ((AudioClip*)newClip)->setOutput(modelStack, output);

								else
									error = ((InstrumentClip*)newClip)
									            ->setInstrument((Instrument*)output, currentSong, NULL);

								if (error) {
									numericDriver.displayError(error);
									newClip->~Clip();
									generalMemoryAllocator.dealloc(memory);
									goto justGetOut;
								}

								if (output->type != OUTPUT_TYPE_AUDIO) {
									((Instrument*)output)->setupPatching(modelStack);
									((InstrumentClip*)newClip)->setupAsNewKitClipIfNecessary(modelStack);
								}

								// Possibly want to set this as the activeClip, if Instrument didn't have one yet.
								// Crucial that we do this not long after calling setInstrument, in case this is the first Clip with the Instrument
								// and we just grabbed the backedUpParamManager for it, which it might go and look for again if the audio routine
								// was called in the interim
								if (!output->activeClip) {
									output->setActiveClip(modelStack);
								}

								currentSong->arrangementOnlyClips.insertClipAtIndex(newClip, 0);

								Action* action = actionLogger.getNewAction(ACTION_CLIP_INSTANCE_EDIT, false);
								if (action)
									action->recordClipExistenceChange(currentSong, &currentSong->arrangementOnlyClips,
									                                  newClip, CREATE);

								clipInstance->change(action, output, clipInstance->pos, clipInstance->length, newClip);

								arrangement.rowEdited(output, clipInstance->pos,
								                      clipInstance->pos + clipInstance->length, NULL, clipInstance);
							}

							transitionToClipView(clipInstance);
						}
					}
				}
			}
		}
	}
}

// Only call if this is the currentUI. May be called during audio / playback routine
void ArrangerView::exitSubModeWithoutAction() {

	// First, stop any stuttering. This may then put us back in one of the subModes dealt with below
	if (isUIModeActive(UI_MODE_STUTTERING)) {
		((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
		    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
	}

	// --------------

	if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		Output* output = outputsOnScreen[yPressedEffective];
		if (output) {
			endAudition(output);
			auditionEnded();
			uiNeedsRendering(this, 0, 1 << yPressedEffective);
		}
	}

	else if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW)) {
		view.setActiveModControllableTimelineCounter(currentSong);
		setNoSubMode();
		uint32_t whichRowsNeedReRendering;
		if (outputsOnScreen[yPressedEffective] == pressedClipInstanceOutput) {
			whichRowsNeedReRendering = (1 << yPressedEffective);
		}
		else {
			whichRowsNeedReRendering = 0xFFFFFFFF;
		}
		uiNeedsRendering(this, whichRowsNeedReRendering, 0);
		uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
		actionLogger.closeAction(ACTION_CLIP_INSTANCE_EDIT);
	}

	if (isUIModeActive(UI_MODE_MIDI_LEARN)) {
		view.endMIDILearn();
	}
}

void ArrangerView::transitionToClipView(ClipInstance* clipInstance) {

	Clip* clip = clipInstance->clip;

	currentSong->currentClip = clip;
	currentSong->lastClipInstanceEnteredStartPos = clipInstance->pos;

	uint32_t xZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];
	while ((xZoom >> 1) * displayWidth >= clip->loopLength)
		xZoom >>= 1;
	currentSong->xZoom[NAVIGATION_CLIP] = xZoom;

	// If can see whole Clip at zoom level, set scroll to 0
	if (xZoom * displayWidth >= clip->loopLength) {
		currentSong->xScroll[NAVIGATION_CLIP] = 0;
	}

	// Otherwise...
	else {
		int32_t newScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT] - clipInstance->pos;
		if (newScroll < 0) newScroll = 0;
		else {
			newScroll = (uint32_t)newScroll % (uint32_t)clip->loopLength;
			newScroll = (uint32_t)newScroll / (xZoom * displayWidth) * (xZoom * displayWidth);
		}

		currentSong->xScroll[NAVIGATION_CLIP] = newScroll;
	}

	if (clip->type == CLIP_TYPE_AUDIO) {

		// If no sample, just skip directly there
		if (!((AudioClip*)clip)->sampleHolder.audioFile) {
			currentUIMode = UI_MODE_NONE;
			changeRootUI(&audioClipView);

			return;
		}
	}

	currentUIMode = UI_MODE_EXPLODE_ANIMATION;

	if (clip->type == CLIP_TYPE_AUDIO) {
		waveformRenderer.collapseAnimationToWhichRow = yPressedEffective;

		int64_t xScrollSamples;
		int64_t xZoomSamples;

		((AudioClip*)clip)
		    ->getScrollAndZoomInSamples(currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP],
		                                &xScrollSamples, &xZoomSamples);

		waveformRenderer.findPeaksPerCol((Sample*)((AudioClip*)clip)->sampleHolder.audioFile, xScrollSamples,
		                                 xZoomSamples, &((AudioClip*)clip)->renderData);

		PadLEDs::setupAudioClipCollapseOrExplodeAnimation((AudioClip*)clip);
	}

	else {

		PadLEDs::explodeAnimationYOriginBig = yPressedEffective << 16;

		// If going to KeyboardView...
		if (((InstrumentClip*)clip)->onKeyboardScreen) {
			keyboardScreen.recalculateColours();
			keyboardScreen.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);
			memset(PadLEDs::occupancyMaskStore[0], 0, displayWidth + sideBarWidth);
			memset(PadLEDs::occupancyMaskStore[displayHeight + 1], 0, displayWidth + sideBarWidth);
		}

		// Or if just regular old InstrumentClipView
		else {
			instrumentClipView.recalculateColours();
			instrumentClipView.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1],
			                                  false);
			instrumentClipView.fillOffScreenImageStores();
		}
	}

	int32_t start = instrumentClipView.getPosFromSquare(0);
	int32_t end = instrumentClipView.getPosFromSquare(displayWidth);

	int64_t xStartBig = getSquareFromPos(clipInstance->pos + start) << 16;

	int64_t clipLengthBig = ((uint64_t)clip->loopLength << 16) / currentSong->xZoom[NAVIGATION_ARRANGEMENT];

	if (clipLengthBig) {
		while (true) {
			int64_t nextPotentialStart = xStartBig + clipLengthBig;
			if ((nextPotentialStart >> 16) > xPressed) break;
			xStartBig = nextPotentialStart;
		}
	}

	PadLEDs::explodeAnimationXStartBig = xStartBig;
	PadLEDs::explodeAnimationXWidthBig = ((uint32_t)(end - start) / currentSong->xZoom[NAVIGATION_ARRANGEMENT]) << 16;

	PadLEDs::recordTransitionBegin(clipCollapseSpeed);
	PadLEDs::animationDirection = 1;
	if (clip->type == CLIP_TYPE_AUDIO) {
		PadLEDs::renderAudioClipExplodeAnimation(0);
	}
	else {
		PadLEDs::renderExplodeAnimation(0);
	}
	PadLEDs::sendOutSidebarColours(); // They'll have been cleared by the first explode render
}

// Returns false if error
bool ArrangerView::transitionToArrangementEditor() {

	Sample* sample;

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {

		// If no sample, just skip directly there
		if (!((AudioClip*)currentSong->currentClip)->sampleHolder.audioFile) {
			changeRootUI(&arrangerView);
			return true;
		}
	}

	Output* output = currentSong->currentClip->output;
	int i = output->clipInstances.search(currentSong->lastClipInstanceEnteredStartPos, GREATER_OR_EQUAL);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);
	if (!clipInstance || clipInstance->clip != currentSong->currentClip) {
		Uart::println("no go");
		return false;
	}

	int32_t start = instrumentClipView.getPosFromSquare(0);
	int32_t end = instrumentClipView.getPosFromSquare(displayWidth);

	currentUIMode = UI_MODE_EXPLODE_ANIMATION;

	memcpy(PadLEDs::imageStore[1], PadLEDs::image, (displayWidth + sideBarWidth) * displayHeight * 3);
	memcpy(PadLEDs::occupancyMaskStore[1], PadLEDs::occupancyMask, (displayWidth + sideBarWidth) * displayHeight);
	if (getCurrentUI() == &instrumentClipView) instrumentClipView.fillOffScreenImageStores();

	int outputIndex = currentSong->getOutputIndex(output);
	int yDisplay = outputIndex - currentSong->arrangementYScroll;
	if (yDisplay < 0) {
		currentSong->arrangementYScroll += yDisplay;
		yDisplay = 0;
	}
	else if (yDisplay >= displayHeight) {
		currentSong->arrangementYScroll += (yDisplay - displayHeight + 1);
		yDisplay = displayHeight - 1;
	}

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		waveformRenderer.collapseAnimationToWhichRow = yDisplay;

		PadLEDs::setupAudioClipCollapseOrExplodeAnimation((AudioClip*)currentSong->currentClip);
	}
	else {
		PadLEDs::explodeAnimationYOriginBig = yDisplay << 16;
	}

	int64_t clipLengthBig =
	    ((uint64_t)currentSong->currentClip->loopLength << 16) / currentSong->xZoom[NAVIGATION_ARRANGEMENT];
	int64_t xStartBig = getSquareFromPos(clipInstance->pos + start) << 16;

	int64_t potentialMidClip = xStartBig + (clipLengthBig >> 1);

	int numExtraRepeats = (uint32_t)(clipInstance->length - 1) / (uint32_t)currentSong->currentClip->loopLength;

	int64_t midClipDistanceFromMidDisplay;

	for (int i = 0; i < numExtraRepeats; i++) {
		if (i == 0) {
			midClipDistanceFromMidDisplay = potentialMidClip - ((displayWidth >> 1) << 16);
			if (midClipDistanceFromMidDisplay < 0) midClipDistanceFromMidDisplay = -midClipDistanceFromMidDisplay;
		}

		int64_t nextPotentialStart = xStartBig + clipLengthBig;
		potentialMidClip = nextPotentialStart + (clipLengthBig >> 1);

		int64_t newMidClipDistanceFromMidDisplay = potentialMidClip - ((displayWidth >> 1) << 16);
		if (newMidClipDistanceFromMidDisplay < 0) newMidClipDistanceFromMidDisplay = -newMidClipDistanceFromMidDisplay;

		if (newMidClipDistanceFromMidDisplay >= midClipDistanceFromMidDisplay) break;
		xStartBig = nextPotentialStart;
		midClipDistanceFromMidDisplay = newMidClipDistanceFromMidDisplay;
	}

	PadLEDs::explodeAnimationXStartBig = xStartBig;
	PadLEDs::explodeAnimationXWidthBig = ((end - start) / currentSong->xZoom[NAVIGATION_ARRANGEMENT]) << 16;

	PadLEDs::recordTransitionBegin(clipCollapseSpeed);
	PadLEDs::animationDirection = -1;

	if (getCurrentUI() == &instrumentClipView) PadLEDs::clearSideBar();

	uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, 35);

	doingAutoScrollNow = false; // May get changed back at new scroll pos soon

	return true;
}

// Wait.... kinda extreme that this seems to be able to happen during card routine even... is that dangerous?
// Seems to return whether it managed to put it in a new, valid position.
bool ArrangerView::putDraggedClipInstanceInNewPosition(Output* newOutputToDragInto) {

	uint32_t xScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
	int xMovement = xScroll - pressedClipInstanceXScrollWhenLastInValidPosition;

	ClipInstance* clipInstance = pressedClipInstanceOutput->clipInstances.getElement(pressedClipInstanceIndex);
	Clip* clip = clipInstance->clip;

	// If Output still the same
	if (newOutputToDragInto == pressedClipInstanceOutput) {

		// If back to original (well, last valid) scroll pos too, nothing to do
		if (!xMovement) {
			pressedClipInstanceIsInValidPosition = true;
			return true;
		}
	}

	// Or if Output not the same
	else {
		if (clip) {
			if (newOutputToDragInto->type != OUTPUT_TYPE_AUDIO
			    || pressedClipInstanceOutput->type != OUTPUT_TYPE_AUDIO) {
itsInvalid:
				pressedClipInstanceIsInValidPosition = false;
				blinkOn = false;
				uiTimerManager.setTimer(TIMER_UI_SPECIFIC, fastFlashTime);
				return false;
			}

			else if (currentSong->doesOutputHaveActiveClipInSession(newOutputToDragInto)) goto itsInvalid;
		}
	}

	int newStartPos = clipInstance->pos + xMovement;

	// If moved left beyond 0
	if (newStartPos < 0) goto itsInvalid;

	// If moved right beyond numerical limit
	if (newStartPos > MAX_SEQUENCE_LENGTH - clipInstance->length) goto itsInvalid;

	// See what's before
	int iPrev = newOutputToDragInto->clipInstances.search(newStartPos, LESS);
	ClipInstance* prevClipInstance = newOutputToDragInto->clipInstances.getElement(iPrev);
	if (prevClipInstance != clipInstance) {
		if (prevClipInstance) {
			if (newOutputToDragInto->recordingInArrangement) {
				if (newStartPos <= arrangement.getLivePos()) goto itsInvalid;
			}
			else {
				if (prevClipInstance->pos + prevClipInstance->length > newStartPos) goto itsInvalid;
			}
		}
	}

	// See what's after
	int iNext = iPrev + 1;
	ClipInstance* nextClipInstance = newOutputToDragInto->clipInstances.getElement(iNext);
	if (nextClipInstance != clipInstance) {
		if (nextClipInstance && nextClipInstance->pos < newStartPos + clipInstance->length) goto itsInvalid;
	}

	pressedClipInstanceIsInValidPosition = true;

	int32_t length = clipInstance->length;

	if (clip)
		arrangement.rowEdited(pressedClipInstanceOutput, clipInstance->pos, clipInstance->pos + length, clip, NULL);

	Action* action = actionLogger.getNewAction(ACTION_CLIP_INSTANCE_EDIT, true);

	// If order of elements hasn't changed and Output hasn't either...
	if (newOutputToDragInto == pressedClipInstanceOutput
	    && (prevClipInstance == clipInstance || nextClipInstance == clipInstance)) {
		clipInstance->change(action, newOutputToDragInto, newStartPos, clipInstance->length, clipInstance->clip);
	}

	// Or if it has...
	else {
		if (action) action->recordClipInstanceExistenceChange(pressedClipInstanceOutput, clipInstance, DELETE);
		pressedClipInstanceOutput->clipInstances.deleteAtIndex(pressedClipInstanceIndex);

		pressedClipInstanceIndex = newOutputToDragInto->clipInstances.insertAtKey(newStartPos);
		// TODO: error check
		clipInstance = newOutputToDragInto->clipInstances.getElement(pressedClipInstanceIndex);
		clipInstance->clip = clip;
		clipInstance->length = length;
		if (action) action->recordClipInstanceExistenceChange(newOutputToDragInto, clipInstance, CREATE);

		// And if changing output...
		if (newOutputToDragInto != pressedClipInstanceOutput && clip) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

			((AudioClip*)clip)->changeOutput(modelStack->addTimelineCounter(clip), newOutputToDragInto);

			pressedClipInstanceOutput->pickAnActiveClipIfPossible(modelStack, true);

			view.setActiveModControllableTimelineCounter(clip);
		}

		pressedClipInstanceOutput = newOutputToDragInto;
	}

	if (clip)
		arrangement.rowEdited(newOutputToDragInto, clipInstance->pos, clipInstance->pos + length, NULL, clipInstance);

	pressedClipInstanceXScrollWhenLastInValidPosition = xScroll;
	rememberInteractionWithClipInstance(yPressedEffective, clipInstance);

	uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
	return true;
}

// Returns which rows couldn't be rendered
// occupancyMask can be NULL
uint32_t ArrangerView::doActualRender(int32_t xScroll, uint32_t xZoom, uint32_t whichRows, uint8_t* image,
                                      uint8_t occupancyMask[][displayWidth + sideBarWidth], int renderWidth,
                                      int imageWidth) {
	uint32_t whichRowsCouldntBeRendered = 0;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		if (whichRows & (1 << yDisplay)) {
			uint8_t* occupancyMaskThisRow = NULL;
			if (occupancyMask) occupancyMaskThisRow = occupancyMask[yDisplay];

			bool success = renderRow(modelStack, yDisplay, xScroll, xZoom, image, occupancyMaskThisRow, renderWidth);
			if (!success) whichRowsCouldntBeRendered |= (1 << yDisplay);
		}

		image += imageWidth * 3;
	}

	return whichRowsCouldntBeRendered;
}

bool ArrangerView::renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                  uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea) {
	if (!image) return true;

	PadLEDs::renderingLock = true;

	uint32_t whichRowsCouldntBeRendered =
	    doActualRender(currentSong->xScroll[NAVIGATION_ARRANGEMENT], currentSong->xZoom[NAVIGATION_ARRANGEMENT],
	                   whichRows, &image[0][0][0], occupancyMask, displayWidth, displayWidth + sideBarWidth);

	PadLEDs::renderingLock = false;

	if (whichRowsCouldntBeRendered && image == PadLEDs::image) uiNeedsRendering(this, whichRowsCouldntBeRendered, 0);

	return true;
}

// Returns false if can't because in card routine
// occupancyMask can be NULL
bool ArrangerView::renderRow(ModelStack* modelStack, int yDisplay, int32_t xScroll, uint32_t xZoom,
                             uint8_t* imageThisRow, uint8_t thisOccupancyMask[], int renderWidth) {

	Output* output = outputsOnScreen[yDisplay];

	if (!output) {

		uint8_t* imageEnd = imageThisRow + renderWidth * 3;

		while (imageThisRow < imageEnd) {
			*(imageThisRow++) = 0;
		}

		// Occupancy mask doesn't need to be cleared in this case
		return true;
	}

	int ignoreI = -2;
	bool drawGhostClipInstanceHere = false;
	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW && !pressedClipInstanceIsInValidPosition) {
		if (yPressedEffective == yDisplay) {
			drawGhostClipInstanceHere = true;
		}
		if (output == pressedClipInstanceOutput) {
			ignoreI = pressedClipInstanceIndex;
		}
	}

	bool success =
	    renderRowForOutput(modelStack, output, xScroll, xZoom, imageThisRow, thisOccupancyMask, renderWidth, ignoreI);
	if (!success) return false;

	if (drawGhostClipInstanceHere) {

		int xMovement =
		    currentSong->xScroll[NAVIGATION_ARRANGEMENT] - pressedClipInstanceXScrollWhenLastInValidPosition;
		ClipInstance* clipInstance = pressedClipInstanceOutput->clipInstances.getElement(pressedClipInstanceIndex);
		int newStartPos = clipInstance->pos + xMovement;
		int newEndPos = newStartPos + clipInstance->length;

		bool rightOnSquare;
		int newStartSquare = getSquareFromPos(newStartPos, &rightOnSquare);
		int newEndSquare = getSquareEndFromPos(newEndPos);

		newStartSquare = getMax(newStartSquare, 0);
		newEndSquare = getMin(newEndSquare, renderWidth);

		if (blinkOn) {
			clipInstance->getColour(&imageThisRow[newStartSquare * 3]);
			int lengthInSquares = newEndSquare - newStartSquare;
			if (lengthInSquares >= 2) {
				getTailColour(&imageThisRow[newStartSquare * 3 + 3], &imageThisRow[newStartSquare * 3]);
			}
			for (int x = newStartSquare + 2; x < newEndSquare; x++) {
				memcpy(&imageThisRow[x * 3], &imageThisRow[newStartSquare * 3 + 3], 3);
			}

			if (!rightOnSquare) {
				uint8_t blurColour[3];
				getBlurColour(blurColour, &imageThisRow[newStartSquare * 3]);
				memcpy(&imageThisRow[newStartSquare * 3], blurColour, 3);
			}
		}
		else memset(&imageThisRow[newStartSquare * 3], 0, 3 * (newEndSquare - newStartSquare));
	}

	return true;
}

// Lock rendering before calling this
// Returns false if can't because in card routine
// occupancyMask can be NULL
bool ArrangerView::renderRowForOutput(ModelStack* modelStack, Output* output, int32_t xScroll, uint32_t xZoom,
                                      uint8_t* image, uint8_t occupancyMask[], int renderWidth, int ignoreI) {

	uint8_t* imageNow = image;
	uint8_t* const imageEnd = image + renderWidth * 3;

	int firstXDisplayNotLeftOf0 = 0;

	if (!output->clipInstances.getNumElements()) {
		while (imageNow < imageEnd) {
			*(imageNow++) = 0;
		}
		return true;
	}

	int32_t squareEndPos[MAX_IMAGE_STORE_WIDTH];
	int32_t searchTerms[MAX_IMAGE_STORE_WIDTH];

	for (int xDisplay = firstXDisplayNotLeftOf0; xDisplay < renderWidth; xDisplay++) {
		squareEndPos[xDisplay] = getPosFromSquare(xDisplay + 1, xScroll, xZoom);
	}

	memcpy(&searchTerms[firstXDisplayNotLeftOf0], &squareEndPos[firstXDisplayNotLeftOf0],
	       (renderWidth - firstXDisplayNotLeftOf0) * sizeof(int32_t));

	output->clipInstances.searchMultiple(&searchTerms[firstXDisplayNotLeftOf0], renderWidth - firstXDisplayNotLeftOf0);

	int farLeftPos = getPosFromSquare(firstXDisplayNotLeftOf0, xScroll, xZoom);
	int squareStartPos = farLeftPos;

	int xDisplay = firstXDisplayNotLeftOf0;

	goto squareStartPosSet;

	do {
		squareStartPos = squareEndPos[xDisplay - 1];

squareStartPosSet:
		int i = searchTerms[xDisplay] - 1; // Do "LESS"
		if (i == ignoreI) i--;
		ClipInstance* clipInstance = output->clipInstances.getElement(i);

		if (clipInstance) {
			uint8_t colour[3];
			clipInstance->getColour(colour);

			// If Instance starts exactly on square or somewhere within square, draw "head".
			// We don't do the "blur" colour in arranger - it looks too white and would be confused with white/unique instances
			if (clipInstance->pos >= squareStartPos) {
				memcpy(&image[xDisplay * 3], colour, 3);
			}

			// Otherwise...
			else {

				int instanceEnd = clipInstance->pos + clipInstance->length;

				if (output->recordingInArrangement && clipInstance->clip
				    && clipInstance->clip->getCurrentlyRecordingLinearly()) {
					instanceEnd = arrangement.getLivePos();
				}

				// See if we want to do a "tail" - which might be several squares long from here
				if (instanceEnd > squareStartPos) {

					// See how many squares long
					int squareEnd = xDisplay;
					do {
						squareStartPos = squareEndPos[squareEnd];
						squareEnd++;
					} while (instanceEnd > squareStartPos && squareEnd < renderWidth
					         && searchTerms[squareEnd] - 1 == i);

					// Draw either the blank, non-existent Clip if this Instance doesn't have one...
					if (!clipInstance->clip) {
						memset(&image[xDisplay * 3], 0, 3 * (squareEnd - xDisplay));
					}

					// Or the real Clip - for all squares in the Instance
					else {
						ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
						    modelStack->addTimelineCounter(clipInstance->clip);
						bool success = clipInstance->clip->renderAsSingleRow(
						    modelStackWithTimelineCounter, this, farLeftPos - clipInstance->pos, xZoom, image,
						    occupancyMask, false, 0, 2147483647, xDisplay, squareEnd, false, true);

						if (!success) return false;
					}

					unsigned int averageBrightnessSection = (unsigned int)colour[0] + colour[1] + colour[2];
					unsigned int sectionColour[3];
					for (int c = 0; c < 3; c++) {
						sectionColour[c] = (int)colour[c] * 140 + averageBrightnessSection * 280;
					}

					// Mix the colours for all the squares
					for (int reworkSquare = xDisplay; reworkSquare < squareEnd; reworkSquare++) {
						for (int c = 0; c < 3; c++) {
							image[reworkSquare * 3 + c] =
							    ((int)image[reworkSquare * 3 + c] * 525 + sectionColour[c]) >> 13;
						}
					}

					xDisplay = squareEnd - 1;
				}
				else goto nothing;
			}
		}

		else {
nothing:
			memset(&image[xDisplay * 3], 0, 3);
		}

		xDisplay++;
	} while (xDisplay < renderWidth);

	AudioEngine::logAction("Instrument::renderRow end");
	return true;
}

int ArrangerView::timerCallback() {
	switch (currentUIMode) {
	case UI_MODE_HOLDING_ARRANGEMENT_ROW:
		if (!pressedClipInstanceIsInValidPosition) {
			blinkOn = !blinkOn;

			uiNeedsRendering(this, 1 << yPressedEffective, 0);

			uiTimerManager.setTimer(TIMER_UI_SPECIFIC, fastFlashTime);
		}
		break;

	case UI_MODE_NONE:
		if (Buttons::isButtonPressed(recordButtonX, recordButtonY)) {
			currentUIMode = UI_MODE_VIEWING_RECORD_ARMING;
			PadLEDs::reassessGreyout(false);
		case UI_MODE_VIEWING_RECORD_ARMING:
			uiNeedsRendering(this, 0, 0xFFFFFFFF);
			blinkOn = !blinkOn;
			uiTimerManager.setTimer(TIMER_UI_SPECIFIC, fastFlashTime);
		}
		break;
	}

	return ACTION_RESULT_DEALT_WITH;
}

void ArrangerView::selectEncoderAction(int8_t offset) {

	Output* output = outputsOnScreen[yPressedEffective];

	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW) {

		actionOnDepress = false;

		if (!pressedClipInstanceIsInValidPosition) return;

		ClipInstance* clipInstance = output->clipInstances.getElement(pressedClipInstanceIndex);

		// If an arrangement-only Clip, can't do anything
		if (clipInstance->clip && clipInstance->clip->section == 255) return;

		Clip* newClip = currentSong->getNextSessionClipWithOutput(offset, output, clipInstance->clip);

		// If no other Clips to switch to...
		if (newClip == clipInstance->clip) return;

		if (clipInstance->clip)
			arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length,
			                      clipInstance->clip, NULL);

		int32_t newLength;

		if (!newClip) {
			newLength = desiredLength;
		}

		else {
			newLength = newClip->loopLength;
		}

		// Make sure it's not too long
		ClipInstance* nextClipInstance = output->clipInstances.getElement(pressedClipInstanceIndex + 1);
		if (nextClipInstance) {
			int32_t maxLength = nextClipInstance->pos - clipInstance->pos;

			if (newLength > maxLength) newLength = maxLength;
		}
		if (newLength > MAX_SEQUENCE_LENGTH - clipInstance->pos) newLength = MAX_SEQUENCE_LENGTH - clipInstance->pos;

		Action* action = actionLogger.getNewAction(ACTION_CLIP_INSTANCE_EDIT, true);
		clipInstance->change(action, output, clipInstance->pos, newLength, newClip);

		view.setActiveModControllableTimelineCounter(newClip);

		arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length, NULL, clipInstance);

		rememberInteractionWithClipInstance(yPressedEffective, clipInstance);

		uiNeedsRendering(this, 1 << yPressedEffective, 0);
	}

	else if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
		navigateThroughPresets(offset);
	}

	else if (currentUIMode == UI_MODE_NONE) {

		if (playbackHandler.playbackState) {
			if (currentPlaybackMode == &session) {
				if (session.launchEventAtSwungTickCount && session.switchToArrangementAtLaunchEvent) {
					sessionView.editNumRepeatsTilLaunch(offset);
				}
			}

			else { // Arrangement playback
				if (offset == -1 && playbackHandler.stopOutputRecordingAtLoopEnd) {
					playbackHandler.stopOutputRecordingAtLoopEnd = false;
#if HAVE_OLED
					renderUIsForOled();
#else
					sessionView.redrawNumericDisplay();
#endif
				}
			}
		}
	}
}

void ArrangerView::navigateThroughPresets(int offset) {

	Output* output = outputsOnScreen[yPressedEffective]; // Essentially, we know there is one.
	if (output->type == OUTPUT_TYPE_AUDIO) return;

	actionLogger.deleteAllLogs();

	Instrument* oldInstrument = (Instrument*)output;

	uint8_t instrumentType = oldInstrument->type;

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E063", "H063");

	// If we're in MIDI or CV mode, easy - just change the channel
	if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT || instrumentType == INSTRUMENT_TYPE_CV) {

		NonAudioInstrument* oldNonAudioInstrument = (NonAudioInstrument*)oldInstrument;

		int oldChannel = oldNonAudioInstrument->channel;
		int newChannel = oldNonAudioInstrument->channel;

		int oldChannelSuffix, newChannelSuffix;
		if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT) {
			oldChannelSuffix = ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix;
			newChannelSuffix = ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix;
		}

		// CV
		if (instrumentType == INSTRUMENT_TYPE_CV) {
			do {
				newChannel = (newChannel + offset) & (NUM_CV_CHANNELS - 1);

				if (newChannel == oldChannel) {
cantDoIt:
					numericDriver.displayPopup(HAVE_OLED ? "No free channel slots available in song" : "CANT");
					return;
				}

			} while (currentSong->getInstrumentFromPresetSlot(instrumentType, newChannel, -1, NULL, NULL, false));
		}

		// Or MIDI
		else {

			oldNonAudioInstrument->channel = -1; // Get it out of the way

			do {
				newChannelSuffix += offset;

				// Turned left
				if (offset == -1) {
					if (newChannelSuffix < -1) {
						newChannel = (newChannel + offset) & 15;
						newChannelSuffix = currentSong->getMaxMIDIChannelSuffix(newChannel);
					}
				}

				// Turned right
				else {
					if (newChannelSuffix >= 26 || newChannelSuffix > currentSong->getMaxMIDIChannelSuffix(newChannel)) {
						newChannel = (newChannel + offset) & 15;
						newChannelSuffix = -1;
					}
				}

				if (newChannel == oldChannel && newChannelSuffix == oldChannelSuffix) {
					oldNonAudioInstrument->channel = oldChannel; // Put it back
					goto cantDoIt;
				}

			} while (currentSong->getInstrumentFromPresetSlot(instrumentType, newChannel, newChannelSuffix, NULL, NULL,
			                                                  false));

			oldNonAudioInstrument->channel = oldChannel; // Put it back, before switching notes off etc
		}

		endAudition(oldNonAudioInstrument);
		if (oldNonAudioInstrument->activeClip && (playbackHandler.playbackState & PLAYBACK_CLOCK_EITHER_ACTIVE)) {
			oldNonAudioInstrument->activeClip->expectNoFurtherTicks(currentSong);
		}

		// Because these are just MIDI / CV instruments and we're changing them for all Clips, we can just change the existing Instrument object!
		oldNonAudioInstrument->channel = newChannel;
		if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT)
			((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix = newChannelSuffix;

		view.displayOutputName(oldNonAudioInstrument);
#if HAVE_OLED
		OLED::sendMainImage();
#endif
	}

	// Or if we're on a Kit or Synth...
	else {

		PresetNavigationResult results =
		    Browser::doPresetNavigation(offset, oldInstrument, AVAILABILITY_INSTRUMENT_UNUSED, true);
		if (results.error == NO_ERROR_BUT_GET_OUT) {
removeWorkingAnimationAndGetOut:
#if HAVE_OLED
			OLED::removeWorkingAnimation();
#endif
			return;
		}
		else if (results.error) {
			numericDriver.displayError(results.error);
			goto removeWorkingAnimationAndGetOut;
		}

		Instrument* newInstrument = results.fileItem->instrument;
		Browser::emptyFileItems();

		endAudition(oldInstrument);

		currentSong->replaceInstrument(oldInstrument, newInstrument);

		oldInstrument = newInstrument;
		outputsOnScreen[yPressedEffective] = newInstrument;
#if HAVE_OLED
		OLED::removeWorkingAnimation();
#else
		numericDriver.removeTopLayer();
#endif
	}

	currentSong->instrumentSwapped(oldInstrument);

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E064", "H064");

	view.setActiveModControllableTimelineCounter(oldInstrument->activeClip);

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	beginAudition(oldInstrument);
}

void ArrangerView::changeInstrumentType(int newInstrumentType) {

	Instrument* oldInstrument = (Instrument*)outputsOnScreen[yPressedEffective];
	int oldInstrumentType = oldInstrument->type;

	if (oldInstrumentType == newInstrumentType) return;

	actionLogger.deleteAllLogs(); // Can't undo past this!

	endAudition(oldInstrument);

	Instrument* newInstrument = currentSong->changeInstrumentType(oldInstrument, newInstrumentType);
	if (!newInstrument) return;

	outputsOnScreen[yPressedEffective] = newInstrument;

	IndicatorLEDs::setLedState(synthLedX, synthLedY, false);
	IndicatorLEDs::setLedState(kitLedX, kitLedY, false);
	IndicatorLEDs::setLedState(midiLedX, midiLedY, false);
	IndicatorLEDs::setLedState(cvLedX, cvLedY, false);
	view.displayOutputName(newInstrument);
#if HAVE_OLED
	OLED::sendMainImage();
#endif
	view.setActiveModControllableTimelineCounter(newInstrument->activeClip);

	beginAudition(newInstrument);
}

void ArrangerView::changeOutputToAudio() {

	Output* oldOutput = outputsOnScreen[yPressedEffective];
	if (oldOutput->type == OUTPUT_TYPE_AUDIO) return;

	if (oldOutput->clipInstances.getNumElements()) {
cant:
		numericDriver.displayPopup(HAVE_OLED ? "Instruments with clips can't be turned into audio tracks" : "CANT");
		return;
	}

	InstrumentClip* instrumentClip = (InstrumentClip*)currentSong->getClipWithOutput(oldOutput);

	if (instrumentClip) {
		if (instrumentClip->containsAnyNotes()) goto cant;
		if (currentSong->getClipWithOutput(oldOutput, false, instrumentClip))
			goto cant; // Make sure not more than 1 Clip

		// We'll do some other specific stuff below
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	endAudition(oldOutput);
	oldOutput->cutAllSound();

	AudioOutput* newOutput;
	Clip* newClip = NULL;

	// If the old Output had a Clip that we're going to replace too...
	if (instrumentClip) {
		int clipIndex = currentSong->sessionClips.getIndexForClip(instrumentClip);
		if (ALPHA_OR_BETA_VERSION && clipIndex == -1) numericDriver.freezeWithError("E266");
		newClip = currentSong->replaceInstrumentClipWithAudioClip(instrumentClip, clipIndex);

		if (!newClip) {
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
			return;
		}

		newOutput = (AudioOutput*)newClip->output;
		currentSong->arrangementYScroll--;
	}

	// Or if no old Clip, we just simply make a new Output here and don't worry about Clips
	else {

		// Suss output
		newOutput = currentSong->createNewAudioOutput(oldOutput);
		if (!newOutput) {
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
			return;
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		newOutput->setupWithoutActiveClip(modelStack);
	}

	outputsOnScreen[yPressedEffective] = newOutput;

	IndicatorLEDs::setLedState(synthLedX, synthLedY, false);
	IndicatorLEDs::setLedState(kitLedX, kitLedY, false);
	IndicatorLEDs::setLedState(midiLedX, midiLedY, false);
	IndicatorLEDs::setLedState(cvLedX, cvLedY, false);
	view.displayOutputName(newOutput);
#if HAVE_OLED
	OLED::sendMainImage();
#endif
	view.setActiveModControllableTimelineCounter(newClip);
}

static const uint32_t horizontalEncoderScrollUIModes[] = {UI_MODE_HOLDING_ARRANGEMENT_ROW, 0};

int ArrangerView::horizontalEncoderAction(int offset) {

	// Encoder button pressed...
	if (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {

		if (!Buttons::isShiftButtonPressed()) {

			int oldXZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

			int zoomMagnitude = -offset;

			// Constrain to zoom limits
			if (zoomMagnitude == -1) {
				if (oldXZoom <= 3) return ACTION_RESULT_DEALT_WITH;
				currentSong->xZoom[NAVIGATION_ARRANGEMENT] >>= 1;
			}
			else {
				if (oldXZoom >= getMaxZoom()) return ACTION_RESULT_DEALT_WITH;
				currentSong->xZoom[NAVIGATION_ARRANGEMENT] <<= 1;
			}

			int32_t oldScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT];

			int32_t newScroll;
			uint32_t newZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

			if (arrangement.hasPlaybackActive() && doingAutoScrollNow) {
				int32_t actualCurrentPos = arrangement.getLivePos();
				int32_t howFarIn = actualCurrentPos - oldScroll;
				newScroll = actualCurrentPos - increaseMagnitude(howFarIn, zoomMagnitude);
				if (newScroll < 0) newScroll = 0;
			}
			else {
				newScroll = oldScroll;
			}

			int32_t screenWidth = newZoom * displayWidth;
			if (newScroll > MAX_SEQUENCE_LENGTH - screenWidth) newScroll = MAX_SEQUENCE_LENGTH - screenWidth;

			newScroll = (uint32_t)(newScroll + (newZoom >> 1)) / newZoom * newZoom; // Rounding

			initiateXZoom(zoomMagnitude, newScroll, oldXZoom);
			displayZoomLevel();
		}
	}

	// Or shift presssed - extend or delete time
	else if (Buttons::isShiftButtonPressed()) {

		// Disallow while arranger playback active - we'd be battling autoscroll and stuff
		if (isNoUIModeActive()) {

			if (arrangement.hasPlaybackActive()) {
				IndicatorLEDs::indicateAlertOnLed(playLedX, playLedY);
			}
			else {

				int scrollAmount = offset * currentSong->xZoom[NAVIGATION_ARRANGEMENT];

				// If expanding, make sure we don't exceed length limit
				if (offset >= 0 && getMaxLength() > MAX_SEQUENCE_LENGTH - scrollAmount) return ACTION_RESULT_DEALT_WITH;

				int actionType = (offset >= 0) ? ACTION_ARRANGEMENT_TIME_EXPAND : ACTION_ARRANGEMENT_TIME_CONTRACT;

				Action* action = actionLogger.getNewAction(actionType, true);

				if (action
				    && (action->xScrollArranger[BEFORE] != currentSong->xScroll[NAVIGATION_ARRANGEMENT]
				        || action->xZoomArranger[BEFORE] != currentSong->xZoom[NAVIGATION_ARRANGEMENT])) {
					action = actionLogger.getNewAction(actionType, false);
				}

				ParamCollectionSummary* unpatchedParamsSummary =
				    currentSong->paramManager.getUnpatchedParamSetSummary();
				UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithParamCollection* modelStackWithUnpatchedParams =
				    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory)
				        ->addParamCollection(unpatchedParams, unpatchedParamsSummary);

				if (offset >= 0) {
					void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceArrangerParamsTimeInserted));
					if (consMemory) {
						ConsequenceArrangerParamsTimeInserted* consequence = new (consMemory)
						    ConsequenceArrangerParamsTimeInserted(currentSong->xScroll[NAVIGATION_ARRANGEMENT],
						                                          scrollAmount);
						action->addConsequence(consequence);
					}
					unpatchedParams->insertTime(modelStackWithUnpatchedParams,
					                            currentSong->xScroll[NAVIGATION_ARRANGEMENT], scrollAmount);
				}
				else {
					if (action) {
						unpatchedParams->backUpAllAutomatedParamsToAction(action, modelStackWithUnpatchedParams);
					}
					unpatchedParams->deleteTime(modelStackWithUnpatchedParams,
					                            currentSong->xScroll[NAVIGATION_ARRANGEMENT], -scrollAmount);
				}

				for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
					int i = thisOutput->clipInstances.search(currentSong->xScroll[NAVIGATION_ARRANGEMENT],
					                                         GREATER_OR_EQUAL);

					bool movedOneYet = false;

					// And move the successive ones.
					while (i < thisOutput->clipInstances.getNumElements()) {
						ClipInstance* instance = thisOutput->clipInstances.getElement(i);

						// If contracting time and this bit has to be deleted...
						if (offset < 0 && instance->pos + scrollAmount < currentSong->xScroll[NAVIGATION_ARRANGEMENT]) {
							deleteClipInstance(thisOutput, i, instance, action);
							// Don't increment i, because we deleted an element
						}

						// Otherwise, just move it
						else {

							int newPos = instance->pos + scrollAmount;

							// If contracting time, shorten the previous ClipInstance only if the ClipInstances we're moving will eat into its tail. Otherwise, leave the tail there.
							// Perhaps it'd make more sense to cut the tail off regardless, but possibly just due to me not thinking about it, this was not done in pre-V4 firmware,
							// and actually having it this way probably helps users.
							if (!movedOneYet && offset < 0 && i > 0) {
								movedOneYet = true;
								ClipInstance* prevInstance = (ClipInstance*)thisOutput->clipInstances.getElement(i - 1);
								int32_t maxLength = newPos - prevInstance->pos;
								if (prevInstance->length > maxLength) {
									prevInstance->change(action, thisOutput, prevInstance->pos, maxLength,
									                     prevInstance->clip);
								}
							}

							instance->change(action, thisOutput, newPos, instance->length, instance->clip);

							i++;
						}
					}
				}

				lastInteractedPos += scrollAmount;

				uiNeedsRendering(this, 0xFFFFFFFF, 0);
			}
		}
	}

	// Encoder button not pressed - we'll just scroll (and possibly drag a ClipInstance horizontally)
	else if (isUIModeWithinRange(horizontalEncoderScrollUIModes)) {

		actionOnDepress = false;

		if (offset == -1 && currentSong->xScroll[NAVIGATION_ARRANGEMENT] == 0) return ACTION_RESULT_DEALT_WITH;

		return horizontalScrollOneSquare(offset);
	}

	return ACTION_RESULT_DEALT_WITH;
}

int ArrangerView::horizontalScrollOneSquare(int direction) {
	actionOnDepress = false;

	uint32_t xZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

	int scrollAmount = direction * xZoom;

	if (scrollAmount < 0 && scrollAmount < -currentSong->xScroll[NAVIGATION_ARRANGEMENT]) {
		scrollAmount = -currentSong->xScroll[NAVIGATION_ARRANGEMENT];
	}

	int32_t newXScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT] + scrollAmount;

	// Make sure we don't scroll too far left, though I'm pretty sure we already did above?
	if (newXScroll < 0) newXScroll = 0;

	// Make sure we don't scroll too far right
	int32_t maxScroll = getMaxLength() - 1 + xZoom; // Add one extra square, and round down
	if (maxScroll < 0) maxScroll = 0;

	int32_t screenWidth = xZoom << displayWidthMagnitude;
	if (maxScroll > MAX_SEQUENCE_LENGTH - screenWidth) maxScroll = MAX_SEQUENCE_LENGTH - screenWidth;

	if (newXScroll > maxScroll) newXScroll = (uint32_t)maxScroll / xZoom * xZoom;

	if (newXScroll != currentSong->xScroll[NAVIGATION_ARRANGEMENT]) {

		bool draggingClipInstance = isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW);

		if (draggingClipInstance && sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

		currentSong->xScroll[NAVIGATION_ARRANGEMENT] = newXScroll;

		if (draggingClipInstance) {
			putDraggedClipInstanceInNewPosition(outputsOnScreen[yPressedEffective]);
		}

		uiNeedsRendering(this, 0xFFFFFFFF, 0);
		reassessWhetherDoingAutoScroll();
	}

	displayScrollPos();

	return ACTION_RESULT_DEALT_WITH;
}

// No need to check whether playback active before calling - we check for that here.
void ArrangerView::reassessWhetherDoingAutoScroll(int32_t pos) {

	doingAutoScrollNow = false;

	if (!currentSong->arrangerAutoScrollModeActive || !arrangement.hasPlaybackActive()) return;

	if (pos == -1) pos = arrangement.getLivePos();
	doingAutoScrollNow = (pos >= getPosFromSquare(0) && pos < getPosFromSquare(displayWidth));

	if (doingAutoScrollNow) {
		autoScrollNumSquaresBehind = getSquareFromPos(pos);
	}
}

int ArrangerView::verticalScrollOneSquare(int direction) {
	if (direction >= 0) { // Up
		if (currentSong->arrangementYScroll >= currentSong->getNumOutputs() - 1) return ACTION_RESULT_DEALT_WITH;
	}
	else { // Down
		if (currentSong->arrangementYScroll <= 1 - displayHeight) return ACTION_RESULT_DEALT_WITH;
	}

	Output* output;

	bool draggingWholeRow = isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION);
	bool draggingClipInstance = isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW);

	// If a Output or ClipInstance selected for dragging, limit scrolling
	if (draggingWholeRow || draggingClipInstance) {
		if (yPressedEffective != yPressedActual) return ACTION_RESULT_DEALT_WITH;

		output = outputsOnScreen[yPressedEffective];

		if (direction >= 0) { // Up
			if (output->next == NULL) return ACTION_RESULT_DEALT_WITH;
		}
		else { // Down
			if (currentSong->firstOutput == output) return ACTION_RESULT_DEALT_WITH;
		}

		if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

		actionLogger.deleteAllLogs();
	}

	currentSong->arrangementYScroll += direction;

	// If an Output is selected, drag it against the scroll
	if (draggingWholeRow) {

		// Shift Output up
		if (direction >= 0) {
			Output** prevPointer = &currentSong->firstOutput;
			while (*prevPointer != output)
				prevPointer = &((*prevPointer)->next);
			Output* higher = output->next;
			*prevPointer = higher;
			output->next = higher->next;
			higher->next = output;
		}

		// Shift Output down
		else {
			Output** prevPointer = &currentSong->firstOutput;
			while ((*prevPointer)->next != output)
				prevPointer = &((*prevPointer)->next);
			Output* lower = (*prevPointer);
			*prevPointer = output;
			lower->next = output->next;
			output->next = lower;
		}
	}

	// Or if dragging ClipInstance vertically
	else if (draggingClipInstance) {
		Output* newOutput = currentSong->getOutputFromIndex(yPressedEffective + currentSong->arrangementYScroll);

		putDraggedClipInstanceInNewPosition(newOutput);
	}

	repopulateOutputsOnScreen();

	if (isUIModeActive(UI_MODE_VIEWING_RECORD_ARMING)) PadLEDs::reassessGreyout(true);

	return ACTION_RESULT_DEALT_WITH;
}

static const uint32_t verticalEncoderUIModes[] = {UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION,
                                                  UI_MODE_HOLDING_ARRANGEMENT_ROW, UI_MODE_VIEWING_RECORD_ARMING, 0};

int ArrangerView::verticalEncoderAction(int offset, bool inCardRoutine) {

	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(yEncButtonX, yEncButtonY))
		return ACTION_RESULT_DEALT_WITH;

	if (isUIModeWithinRange(verticalEncoderUIModes)) {
		if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine)
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.

		return verticalScrollOneSquare(offset);
	}

	return ACTION_RESULT_DEALT_WITH;
}

void ArrangerView::setNoSubMode() {
	currentUIMode = UI_MODE_NONE;
	if (doingAutoScrollNow) reassessWhetherDoingAutoScroll(); // Maybe stop auto-scrolling. But don't start.
}

static const uint32_t autoScrollUIModes[] = {UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON,
                                             UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION, UI_MODE_HORIZONTAL_ZOOM, 0};

void ArrangerView::graphicsRoutine() {

	if (PadLEDs::flashCursor != FLASH_CURSOR_OFF) {

		int newTickSquare;

		if (!arrangement.hasPlaybackActive() || currentUIMode == UI_MODE_EXPLODE_ANIMATION
		    || playbackHandler.ticksLeftInCountIn) {
			newTickSquare = 255;
		}
		else {
			int32_t actualCurrentPos = arrangement.getLivePos();

			// If doing auto scroll...
			if (doingAutoScrollNow && isUIModeWithinRange(autoScrollUIModes)) {

				int32_t newScrollPos =
				    (actualCurrentPos / currentSong->xZoom[NAVIGATION_ARRANGEMENT] - autoScrollNumSquaresBehind)
				    * currentSong->xZoom[NAVIGATION_ARRANGEMENT];

				// If now is the time to scroll to a different position (usually one square)...
				if (newScrollPos != currentSong->xScroll[NAVIGATION_ARRANGEMENT]) {
					bool wasLessThanZero = (newScrollPos < 0);
					if (wasLessThanZero) newScrollPos = 0;

					currentSong->xScroll[NAVIGATION_ARRANGEMENT] = newScrollPos;
					if (wasLessThanZero) {
						autoScrollNumSquaresBehind = getSquareFromPos(actualCurrentPos);
					}

					if (PadLEDs::flashCursor == FLASH_CURSOR_FAST) {
						PadLEDs::clearTickSquares();  // Make sure new fast flashes get sent out
						mustRedrawTickSquares = true; // Make sure this gets sent below here
					}
					if (currentUIMode != UI_MODE_HORIZONTAL_ZOOM) {
						uiNeedsRendering(this, 0xFFFFFFFF, 0);
					}
				}
			}

			newTickSquare = getSquareFromPos(actualCurrentPos);

			if (newTickSquare < 0 || newTickSquare >= displayWidth) {
				newTickSquare = 255;
				doingAutoScrollNow = false;
			}
		}

		// If tick square changed (or we decided it has to be redrawn anyway)...
		if (newTickSquare != lastTickSquare || mustRedrawTickSquares) {

			uint8_t tickSquares[displayHeight];
			uint8_t colours[displayHeight];

			for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
				Output* output = outputsOnScreen[yDisplay];
				tickSquares[yDisplay] =
				    (currentSong->getAnyOutputsSoloingInArrangement() && (!output || !output->soloingInArrangementMode))
				        ? 255
				        : newTickSquare;
				colours[yDisplay] =
				    (output && output->recordingInArrangement) ? 2 : (output && output->mutedInArrangementMode ? 1 : 0);

				if (arrangement.hasPlaybackActive() && currentUIMode != UI_MODE_EXPLODE_ANIMATION) {

					// If linear recording to this Output, re-render it
					if (output->recordingInArrangement) {
						uiNeedsRendering(this, (1 << yDisplay), 0);
					}
				}
			}

			PadLEDs::setTickSquares(tickSquares, colours);
			lastTickSquare = newTickSquare;
		}
	}

	mustRedrawTickSquares = false;
}

void ArrangerView::notifyActiveClipChangedOnOutput(Output* output) {
	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION && outputsOnScreen[yPressedEffective] == output) {
		view.setActiveModControllableTimelineCounter(output->activeClip);
	}
}

static const uint32_t autoScrollPlaybackEndUIModes[] = {UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION,
                                                        UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

void ArrangerView::autoScrollOnPlaybackEnd() {

	if (doingAutoScrollNow && isUIModeWithinRange(autoScrollPlaybackEndUIModes)
	    && !Buttons::isButtonPressed(xEncButtonX,
	                                 xEncButtonY)) { // Don't do it if they're instantly restarting playback again

		uint32_t xZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];
		int32_t newScrollPos =
		    ((uint32_t)(arrangement.playbackStartedAtPos + (xZoom >> 1)) / xZoom - autoScrollNumSquaresBehind) * xZoom;

		if (newScrollPos < 0) newScrollPos = 0;

		// If that actually puts us back to near where we were scrolled to when playback began (which it usually will), just go back there exactly.
		// Added in response to Michael noting that if you do an UNDO and then also stop playback while recording e.g. MIDI to arranger,
		// it scrolls backwards twice (if you have "follow" on).
		// Actually it seems that in that situation, undoing (probably due to other mechanics that get enacted) won't let it take you further than 1 screen back from the play-cursor
		// - which just means that this is "extra" effective I guess.
		if (newScrollPos > xScrollWhenPlaybackStarted - (xZoom >> displayWidthMagnitude)
		    || newScrollPos < xScrollWhenPlaybackStarted + (xZoom >> displayWidthMagnitude)) {
			newScrollPos = xScrollWhenPlaybackStarted;
		}

		int32_t scrollDifference = newScrollPos - currentSong->xScroll[NAVIGATION_ARRANGEMENT];

		if (scrollDifference) {

			// If allowed to do a nice scrolling animation...
			if (currentUIMode == UI_MODE_NONE && getCurrentUI() == this && !PadLEDs::renderingLock) {
				bool worthDoingAnimation = initiateXScroll(newScrollPos);
				if (!worthDoingAnimation) goto sharpJump;
			}

			// Otherwise, just jump sharply
			else {
sharpJump:
				currentSong->xScroll[NAVIGATION_ARRANGEMENT] = newScrollPos;
				uiNeedsRendering(this, 0xFFFFFFFF, 0);
			}
		}
	}
}

// Returns false if too few squares to bother with animation
bool ArrangerView::initiateXScroll(int32_t newScrollPos) {
	int32_t distanceToScroll = newScrollPos - currentSong->xScroll[NAVIGATION_ARRANGEMENT];
	if (distanceToScroll < 0) distanceToScroll = -distanceToScroll;
	int squaresToScroll = distanceToScroll / currentSong->xZoom[NAVIGATION_ARRANGEMENT];
	if (squaresToScroll <= 1) return false;
	if (squaresToScroll > displayWidth) squaresToScroll = displayWidth;
	TimelineView::initiateXScroll(newScrollPos, squaresToScroll);

	return true;
}

uint32_t ArrangerView::getMaxLength() {

	uint32_t maxEndPos = 0;
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {

		if (thisOutput->recordingInArrangement) {
			maxEndPos = getMax(maxEndPos, arrangement.getLivePos());
		}

		int numElements = thisOutput->clipInstances.getNumElements();
		if (numElements) {
			ClipInstance* lastInstance = thisOutput->clipInstances.getElement(numElements - 1);
			uint32_t endPos = lastInstance->pos + lastInstance->length;
			maxEndPos = getMax(maxEndPos, endPos);
		}
	}

	return maxEndPos;
}

unsigned int ArrangerView::getMaxZoom() {
	unsigned int maxLength = getMaxLength();

	if (maxLength < (DEFAULT_ARRANGER_ZOOM << currentSong->insideWorldTickMagnitude) * displayWidth)
		return (DEFAULT_ARRANGER_ZOOM << currentSong->insideWorldTickMagnitude);

	unsigned int thisLength = displayWidth * 3;
	while (thisLength < maxLength)
		thisLength <<= 1;

	if (thisLength < (MAX_SEQUENCE_LENGTH >> 1)) thisLength <<= 1;

	int32_t maxZoom = thisLength >> displayWidthMagnitude;

	return maxZoom;
}

void ArrangerView::tellMatrixDriverWhichRowsContainSomethingZoomable() {
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		PadLEDs::transitionTakingPlaceOnRow[yDisplay] =
		    (outputsOnScreen[yDisplay] && outputsOnScreen[yDisplay]->clipInstances.getNumElements());
	}
}

void ArrangerView::scrollFinished() {
	TimelineView::scrollFinished();
	reassessWhetherDoingAutoScroll();
}

void ArrangerView::notifyPlaybackBegun() {
	mustRedrawTickSquares = true;
	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
		endAudition(outputsOnScreen[yPressedEffective], true);
	}
}

bool ArrangerView::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
		*cols = 0xFFFFFFFD;
		*rows = 0;
		for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
			if (outputsOnScreen[yDisplay] && !outputsOnScreen[yDisplay]->armedForRecording) {
				*rows |= (1 << yDisplay);
			}
		}
		return true;
	}
	else return false;
}

uint32_t ArrangerView::getGreyedOutRowsNotRepresentingOutput(Output* output) {
	uint32_t rows = 0xFFFFFFFF;
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		if (outputsOnScreen[yDisplay] == output) {
			rows &= ~(1 << yDisplay);
			break;
		}
	}
	return rows;
}

void ArrangerView::playbackEnded() {
	if (currentPlaybackMode == &arrangement) {
		autoScrollOnPlaybackEnd();
	}

	if (getCurrentUI() == &arrangerView) { // Why do we need to check this?
#if HAVE_OLED
		renderUIsForOled();
#else
		sessionView.redrawNumericDisplay();
#endif
	}
}

void ArrangerView::clipNeedsReRendering(Clip* clip) {

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		Output* output = outputsOnScreen[yDisplay];
		if (output == clip->output) {
			// In a perfect world we'd see if the Clip is actually horizontally scrolled on-screen
			uiNeedsRendering(this, (1 << yDisplay), 0);
			break;
		}
	}
}
