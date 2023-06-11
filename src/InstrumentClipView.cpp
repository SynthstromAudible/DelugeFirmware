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

#include <InstrumentClipView.h>
#include <ArrangerView.h>
#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <ConsequenceInstrumentClipMultiply.h>
#include <MenuItemMultiRange.h>
#include <ParamManager.h>
#include <samplebrowser.h>
#include <sounddrum.h>
#include <soundinstrument.h>
#include "soundeditor.h"
#include "functions.h"
#include "numericdriver.h"
#include "uart.h"
#include "CVEngine.h"
#include "KeyboardScreen.h"
#include "View.h"
#include "kit.h"
#include "Note.h"
#include "midiengine.h"
#include "AudioRecorder.h"
#include "Action.h"
#include "ConsequenceNoteRowMute.h"
#include "ActionLogger.h"
#include <string.h>
#include <TimelineView.h>
#include "CopiedNoteRow.h"
#include "ParamNode.h"
#include "GeneralMemoryAllocator.h"
#include "PlaybackMode.h"
#include "drum.h"
#include "MelodicInstrument.h"
#include "SampleMarkerEditor.h"
#include "MenuItemFileSelector.h"
#include <new>
#include "storagemanager.h"
#include "LoadInstrumentPresetUI.h"
#include "RenameDrumUI.h"
#include "song.h"
#include "Clip.h"
#include "ConsequenceInstrumentClipHorizontalShift.h"
#include "ConsequenceNoteArrayChange.h"
#include "MenuItemColour.h"
#include "PadLEDs.h"
#include "IndicatorLEDs.h"
#include "Buttons.h"
#include "ModelStack.h"
#include "extern.h"
#include "Encoders.h"
#include "ConsequenceNoteRowHorizontalShift.h"
#include "ConsequenceNoteRowLength.h"
#include "SessionView.h"
#include "InstrumentClip.h"
#include "AutoParam.h"
#include "ParamSet.h"
#include "NoteRow.h"
#include "uitimermanager.h"
#include "PatchCableSet.h"
#include "MIDIDrum.h"

#if HAVE_OLED
#include "oled.h"
#endif

extern "C" {
#include "cfunctions.h"
#include "sio_char.h"
}

InstrumentClipView instrumentClipView;

InstrumentClipView::InstrumentClipView() {

	numEditPadPresses = 0;

	for (int i = 0; i < editPadPressBufferSize; i++) {
		editPadPresses[i].isActive = false;
	}

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		numEditPadPressesPerNoteRowOnScreen[yDisplay] = 0;
		lastAuditionedVelocityOnScreen[yDisplay] = 255;
		auditionPadIsPressed[yDisplay] = 0;
	}

	auditioningSilently = false;
	timeLastEditPadPress = 0;
	//newDrumOptionSelected = false;
	firstCopiedNoteRow = NULL;
}

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

bool InstrumentClipView::opened() {

	openedInBackground();

	InstrumentClipMinder::opened();

	focusRegained();

	return true;
}

// Initializes some stuff to begin a new editing session
void InstrumentClipView::focusRegained() {
	ClipView::focusRegained();

	auditioningSilently = false; // Necessary?

	InstrumentClipMinder::focusRegained();

	setLedStates();
}

void InstrumentClipView::openedInBackground() {
	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	recalculateColours();

	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	AudioEngine::logAction("InstrumentClipView::beginSession 2");

	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[displayHeight], &PadLEDs::occupancyMaskStore[displayHeight],
		               true);
		renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[displayHeight], &PadLEDs::occupancyMaskStore[displayHeight]);
	}
	else {
		uiNeedsRendering(this);
	}
	getCurrentClip()->onKeyboardScreen = false;
}

void InstrumentClipView::setLedStates() {
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	IndicatorLEDs::setLedState(keyboardLedX, keyboardLedY, false);
#endif
	InstrumentClipMinder::setLedStates();
}

int InstrumentClipView::buttonAction(int x, int y, bool on, bool inCardRoutine) {
	// Scale mode button
	if (x == scaleModeButtonX && y == scaleModeButtonY) {
		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

		// Kits can't do scales!
		if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
			if (on) {
				IndicatorLEDs::indicateAlertOnLed(kitLedX, kitLedY);
			}
			return ACTION_RESULT_DEALT_WITH;
		}

		actionLogger.deleteAllLogs(); // Can't undo past this!

		if (on) {

			if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {

				// If user holding shift and we're already in scale mode, cycle through available scales
				if (Buttons::isShiftButtonPressed() && getCurrentClip()->inScaleMode) {
					cycleThroughScales();
					recalculateColours();
					uiNeedsRendering(this);
				}

				// Or, no shift button - normal behaviour
				else {
					currentUIMode = UI_MODE_SCALE_MODE_BUTTON_PRESSED;
					exitScaleModeOnButtonRelease = true;
					if (!getCurrentClip()->inScaleMode) {
						calculateDefaultRootNote(); // Calculate it now so we can show the user even before they've released the button
						flashDefaultRootNoteOn = false;
						flashDefaultRootNote();
					}
				}
			}

			// If user is auditioning just one NoteRow, we can go directly into Scale Mode and set that root note
			else if (oneNoteAuditioning() && !getCurrentClip()->inScaleMode) {
				cancelAllAuditioning();
				enterScaleMode(lastAuditionedYDisplay);
			}
		}
		else {
			if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
				currentUIMode = UI_MODE_NONE;
				if (getCurrentClip()->inScaleMode) {
					if (exitScaleModeOnButtonRelease) exitScaleMode();
				}
				else enterScaleMode();
			}
		}
	}

	// Song view button
	else if (x == sessionViewButtonX && y == sessionViewButtonY) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (currentSong->lastClipInstanceEnteredStartPos != -1
			    || currentSong->currentClip->isArrangementOnlyClip()) {
				bool success = arrangerView.transitionToArrangementEditor();
				if (!success) goto doOther;
			}
			else {
doOther:
				transitionToSessionView();
			}
		}
	}

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	// Clip view button
	else if (x == clipViewButtonX && y == clipViewButtonY) {
		if (on && Buttons::isShiftButtonPressed() && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			InstrumentClip* currentClip = getCurrentClip();

			if (currentClip->output->type == INSTRUMENT_TYPE_KIT) {
				currentClip->affectEntire = !currentClip->affectEntire;
				view.setActiveModControllableTimelineCounter(currentClip); // Refreshes LEDs and everything
			}
			else {
				changeRootUI(&keyboardScreen); // Go to keyboard screen
			}
		}
	}
#else

	// Keyboard button
	else if (x == keyboardButtonX && y == keyboardButtonY) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
				IndicatorLEDs::indicateAlertOnLed(kitLedX, kitLedY);
			}
			else changeRootUI(&keyboardScreen);
		}
	}
#endif

	// Wrap edit button
	else if (x == crossScreenEditButtonX && y == crossScreenEditButtonY) {
		if (on) {
			if (currentUIMode == UI_MODE_NONE) {
				if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

				if (getCurrentClip()->wrapEditing) getCurrentClip()->wrapEditing = false;
				else {
					getCurrentClip()->wrapEditLevel = currentSong->xZoom[NAVIGATION_CLIP] * displayWidth;
					// Ensure that there are actually multiple screens to edit across
					if (getCurrentClip()->wrapEditLevel < currentSong->currentClip->loopLength)
						getCurrentClip()->wrapEditing = true;
				}

				setLedStates();
			}
		}
	}

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	// Record button if holding audition pad
	else if (x == recordButtonX && y == recordButtonY
	         && (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING)) {
		if (on && currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT && !audioRecorder.recordingSource
		    && (!playbackHandler.isEitherClockActive() || !playbackHandler.ticksLeftInCountIn)) {

			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {

				currentUIMode = UI_MODE_NONE;

				// Make a new NoteRow
				int noteRowIndex;
				NoteRow* newNoteRow = createNewNoteRowForKit(modelStack, yDisplayOfNewNoteRow, &noteRowIndex);
				if (newNoteRow) {
					uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);

					int noteRowId = getCurrentClip()->getNoteRowId(newNoteRow, noteRowIndex);
					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, newNoteRow);

					enterDrumCreator(modelStackWithNoteRow, true);
				}
			}

			else if (currentUIMode == UI_MODE_AUDITIONING) {
				cutAuditionedNotesToOne();

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentClip()->getNoteRowOnScreen(lastAuditionedYDisplay, modelStack);

				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
				if (noteRow->drum) {
					noteRow->drum->drumWontBeRenderedForAWhile();
				}
				cancelAllAuditioning();

				enterDrumCreator(modelStackWithNoteRow, true);
			}
		}
	}
#endif

	// Back button if adding Drum
	else if (x == backButtonX && y == backButtonY && currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			currentUIMode = UI_MODE_NONE;
#if !HAVE_OLED
			InstrumentClipMinder::redrawNumericDisplay();
#endif
			uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);
		}
	}

	// Load / Kit button if creating new NoteRow for Drum
	else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW
	         && ((x == loadButtonX && y == loadButtonY) || (x == kitButtonX && y == kitButtonY))) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			currentUIMode = UI_MODE_NONE;

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			// Make a new NoteRow
			int noteRowIndex;
			NoteRow* newNoteRow = createNewNoteRowForKit(modelStack, yDisplayOfNewNoteRow, &noteRowIndex);
			if (!newNoteRow) {
				numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
				return ACTION_RESULT_DEALT_WITH;
			}

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, newNoteRow);

			enterDrumCreator(modelStackWithNoteRow, false);

			uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);
		}
	}

	// Load / kit button if auditioning
	else if (currentUIMode == UI_MODE_AUDITIONING
	         && ((x == loadButtonX && y == loadButtonY) || (x == kitButtonX && y == kitButtonY))
	         && (!playbackHandler.isEitherClockActive() || !playbackHandler.ticksLeftInCountIn)) {

		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			// Auditioning drum
			if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
				cutAuditionedNotesToOne();
				int noteRowIndex;
				NoteRow* noteRow =
				    getCurrentClip()->getNoteRowOnScreen(lastAuditionedYDisplay, currentSong, &noteRowIndex);
				cancelAllAuditioning();
				if (noteRow->drum) {
					noteRow->drum->drumWontBeRenderedForAWhile();
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithNoteRow* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory)->addNoteRow(noteRowIndex, noteRow);

				enterDrumCreator(modelStack, false);
			}

			// Auditioning synth
			if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_SYNTH) {
				cancelAllAuditioning();

				bool success = soundEditor.setup(getCurrentClip(), &fileSelectorMenu,
				                                 0); // Can't fail because we just set the selected Drum
				if (success) {
					openUI(&soundEditor);
				}
			}
		}
	}

	// Kit button. Unlike the other instrument-type buttons, whose code is in InstrumentClipMinder, this one is only allowed in the InstrumentClipView
	else if (x == kitButtonX && y == kitButtonY && currentUIMode == UI_MODE_NONE) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (Buttons::isNewOrShiftButtonPressed()) createNewInstrument(INSTRUMENT_TYPE_KIT);
			else changeInstrumentType(INSTRUMENT_TYPE_KIT);
		}
	}

	else if (x == synthButtonX && y == synthButtonY && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (currentUIMode == UI_MODE_NONE) {
				if (Buttons::isNewOrShiftButtonPressed()) createNewInstrument(INSTRUMENT_TYPE_SYNTH);
				else changeInstrumentType(INSTRUMENT_TYPE_SYNTH);
			}
			else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING) {
				createDrumForAuditionedNoteRow(DRUM_TYPE_SOUND);
			}
		}
	}

	else if (x == midiButtonX && y == midiButtonY) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (currentUIMode == UI_MODE_NONE) {
				changeInstrumentType(INSTRUMENT_TYPE_MIDI_OUT);
			}
			else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING) {
				createDrumForAuditionedNoteRow(DRUM_TYPE_MIDI);
			}
		}
	}

	else if (x == cvButtonX && y == cvButtonY) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (currentUIMode == UI_MODE_NONE) {
				changeInstrumentType(INSTRUMENT_TYPE_CV);
			}
			else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING) {
				createDrumForAuditionedNoteRow(DRUM_TYPE_GATE);
			}
		}
	}

	// Save / delete button if NoteRow held down
	else if (x == saveButtonX && y == saveButtonY && currentUIMode == UI_MODE_NOTES_PRESSED) {
		InstrumentClip* clip = getCurrentClip();

		if (on && numEditPadPresses == 1 && currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT
		    && clip->getNumNoteRows() >= 2) {

			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			int i;
			for (i = 0; i < editPadPressBufferSize; i++) {
				if (editPadPresses[i].isActive) {

					int yDisplay = editPadPresses[i].yDisplay;

					endEditPadPress(i);
					checkIfAllEditPadPressesEnded(false);
					reassessAuditionStatus(yDisplay);

					int noteRowIndex = yDisplay + clip->yScroll;

					if (ALPHA_OR_BETA_VERSION
					    && (noteRowIndex < 0 || noteRowIndex >= clip->noteRows.getNumElements())) {
						numericDriver.freezeWithError("E323");
					}

					if (clip->isActiveOnOutput()) {
						NoteRow* noteRow = clip->noteRows.getElement(noteRowIndex);
						if (noteRow->drum) {
							noteRow->drum->drumWontBeRenderedForAWhile();
						}
					}

					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithTimelineCounter* modelStack =
					    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
					clip->deleteNoteRow(modelStack, noteRowIndex);

					// Note: I should fix this - if deleting a NoteRow of a MIDI drum that we're auditioning via MIDI, this will leave a stuck note...

					// If NoteRow was bottom half of screen...
					if (yDisplay < (displayHeight >> 1)) {
						if (!noteRowIndex || clip->noteRows.getNumElements() >= (displayHeight >> 1)) clip->yScroll--;
					}

					// Or top half of screen...
					else {
						if (!noteRowIndex && clip->noteRows.getNumElements() < (displayHeight >> 1)) clip->yScroll--;
					}

					actionLogger.deleteAllLogs(); // Can't undo past this

					setSelectedDrum(NULL, true);

					recalculateColours();
					uiNeedsRendering(this);

					// Can't remember why repopulateNoteRowsOnScreen() doesn't do the sidebar automatically?

					currentUIMode = UI_MODE_NONE;

					AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;

					break;
				}
			}
		}
	}

	// Horizontal encoder button if learn button pressed. Make sure you let the "off" action slide past to the Editor
	else if (x == xEncButtonX && y == xEncButtonY && on && Buttons::isButtonPressed(learnButtonX, learnButtonY)) {
		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

		if (Buttons::isShiftButtonPressed()) {
			pasteNotes();
		}
		else {
			copyNotes();
		}
	}

	// Horizontal encoder button
	else if (x == xEncButtonX && y == xEncButtonY) {

		// If user wants to "multiple" Clip contents
		if (on && Buttons::isShiftButtonPressed()) {
			if (isNoUIModeActive()) {
				if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

				// Zoom to max if we weren't already there...
				if (!zoomToMax()) {
					// Or if we didn't need to do that, double Clip length
					doubleClipLengthAction();
				}
				else {
					displayZoomLevel();
				}
			}
			enterUIMode(
			    UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON); // Whether or not we did the "multiply" action above, we need to be in this UI mode, e.g. for rotating individual NoteRow
		}

		// Otherwise...
		else {
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				if (on) {
					nudgeNotes(0);
				}
				else {
doCancelPopup:
					numericDriver.cancelPopup();
				}
			}
			else if (isUIModeActive(UI_MODE_AUDITIONING)) {
				if (!on) {
					timeHorizontalKnobLastReleased = AudioEngine::audioSampleTimer;
					goto doCancelPopup;
				}
			}
			goto passToOthers; // For exiting the UI mode, I think
		}
	}

	// Vertical encoder button
	else if (x == yEncButtonX && y == yEncButtonY) {

		// If holding notes down...
		if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
			if (on) {
				editNoteRepeat(0); // Just pop up number - don't do anything
				goto passToOthers; // Wait, why?
			}
			else goto doCancelPopup;
		}

		// Or if auditioning...
		if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
			if (on) {

				// If in a Kit and multiple Drums auditioned, re-order them
				if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
					for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
						if (yDisplay != lastAuditionedYDisplay && auditionPadIsPressed[yDisplay]) {
							if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

							actionLogger.deleteAllLogs();
							cancelAllAuditioning();
							InstrumentClip* clip = getCurrentClip();
							clip->noteRows.repositionElement(yDisplay + clip->yScroll,
							                                 lastAuditionedYDisplay + clip->yScroll);
							recalculateColours();
							uiNeedsRendering(this);
							goto passToOthers;
						}
					}
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    ((InstrumentClip*)modelStack->getTimelineCounter())
				        ->getNoteRowOnScreen(lastAuditionedYDisplay, modelStack);

				editNumEuclideanEvents(modelStackWithNoteRow, 0,
				                       lastAuditionedYDisplay); // Just pop up number - don't do anything
				goto passToOthers;                              // Wait, why?
			}
			else goto doCancelPopup;
		}
	}

	else {
passToOthers:
		int result = InstrumentClipMinder::buttonAction(x, y, on, inCardRoutine);
		if (result != ACTION_RESULT_NOT_DEALT_WITH) return result;

		return ClipView::buttonAction(x, y, on, inCardRoutine);
	}

	return ACTION_RESULT_DEALT_WITH;
}

void InstrumentClipView::createDrumForAuditionedNoteRow(int drumType) {
	if (currentSong->currentClip->output->type != INSTRUMENT_TYPE_KIT) return;

	if (playbackHandler.isEitherClockActive() && playbackHandler.ticksLeftInCountIn) return;

	int error;
	NoteRow* noteRow;
	int noteRowIndex;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	actionLogger.deleteAllLogs();

	if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {
		currentUIMode = UI_MODE_AUDITIONING;

		// Make a new NoteRow
		noteRow = createNewNoteRowForKit(modelStack, yDisplayOfNewNoteRow, &noteRowIndex);
		if (!noteRow) {
ramError:
			error = ERROR_INSUFFICIENT_RAM;
someError:
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
			return;
		}

		uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);

		lastAuditionedYDisplay = yDisplayOfNewNoteRow;
	}

	else {
		cutAuditionedNotesToOne();
		noteRow = getCurrentClip()->getNoteRowOnScreen(lastAuditionedYDisplay, currentSong, &noteRowIndex);
		if (noteRow->drum) {
			if (drumType != DRUM_TYPE_SOUND && noteRow->drum->type == drumType)
				return; // If it's already that kind of Drum, well, no need to do it again
			noteRow->drum->drumWontBeRenderedForAWhile();
		}

		auditionPadIsPressed[lastAuditionedYDisplay] = false;
		reassessAuditionStatus(lastAuditionedYDisplay);
	}

	Drum* newDrum = storageManager.createNewDrum(drumType);
	if (!newDrum) goto ramError;

	Kit* kit = (Kit*)currentSong->currentClip->output;

	ParamManager paramManager;

	if (drumType == DRUM_TYPE_SOUND) {

		String newName;
		int error = newName.set("U");
		if (error) {
discardDrum:
			void* toDealloc = dynamic_cast<void*>(newDrum);
			newDrum->~Drum();
			generalMemoryAllocator.dealloc(toDealloc);
			goto someError;
		}

		error = kit->makeDrumNameUnique(&newName, 1);
		if (error) goto discardDrum;

		((SoundDrum*)newDrum)->name.set(&newName);

		error = paramManager.setupWithPatching();
		if (error) goto discardDrum;

		Sound::initParams(&paramManager);
		((SoundDrum*)newDrum)->setupAsBlankSynth(&paramManager);

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
		((SoundDrum*)newDrum)->modKnobs[6][0].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_PITCH_ADJUST);
#endif
	}

	kit->addDrum(newDrum);

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);

	noteRow->setDrum(newDrum, kit, modelStackWithNoteRow, NULL, &paramManager);

	kit->beenEdited();

	drawDrumName(newDrum);

	auditionPadIsPressed[lastAuditionedYDisplay] = true;
	reassessAuditionStatus(lastAuditionedYDisplay);
	setSelectedDrum(newDrum, true);
	// uiNeedsRendering(this, 0, 1 << lastAuditionedNoteOnScreen);
}

void InstrumentClipView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	// If they want to copy or paste automation...
	if (Buttons::isButtonPressed(learnButtonX, learnButtonY)) {
		if (on && currentSong->currentClip->output->type != INSTRUMENT_TYPE_CV) {
			if (Buttons::isShiftButtonPressed()) {
				pasteAutomation(whichModEncoder);
			}
			else {
				copyAutomation(whichModEncoder);
			}
		}
	}
	else {
		view.modEncoderButtonAction(whichModEncoder, on);
	}
}

void InstrumentClipView::copyAutomation(int whichModEncoder) {
	if (copiedParamAutomation.nodes) {
		generalMemoryAllocator.dealloc(copiedParamAutomation.nodes);
		copiedParamAutomation.nodes = NULL;
		copiedParamAutomation.numNodes = 0;
	}

	int startPos = getPosFromSquare(0);
	int endPos = getPosFromSquare(displayWidth);
	if (startPos == endPos) return;

	if (!view.activeModControllableModelStack.modControllable) return;

	ModelStackWithAutoParam* modelStack = view.activeModControllableModelStack.modControllable->getParamFromModEncoder(
	    whichModEncoder, &view.activeModControllableModelStack, false);
	if (modelStack && modelStack->autoParam) {

		bool isPatchCable =
		    (modelStack->paramCollection
		     == modelStack->paramManager
		            ->getPatchCableSetAllowJibberish()); // Ok this is cursed, but will work fine so long as
		                                                 // the possibly invalid memory here doesn't accidentally
		                                                 // equal modelStack->paramCollection.
		modelStack->autoParam->copy(startPos, endPos, &copiedParamAutomation, isPatchCable, modelStack);

		if (copiedParamAutomation.nodes) {
			numericDriver.displayPopup(HAVE_OLED ? "Automation copied" : "COPY");
			return;
		}
	}

	numericDriver.displayPopup(HAVE_OLED ? "No automation to copy" : "NONE");
}

void InstrumentClipView::copyNotes() {

	// Clear out previously copied stuff
	deleteCopiedNoteRows();

	int startPos = getPosFromSquare(0);
	int endPos = getPosFromSquare(displayWidth);

	copiedScreenWidth = endPos - startPos;
	if (copiedScreenWidth == 0) return;

	copiedScaleType = getCurrentClip()->getScaleType();
	copiedYNoteOfBottomRow =
	    getCurrentClip()->getYNoteFromYDisplay(0, currentSong); //currentSong->currentClip->yScroll;

	CopiedNoteRow** prevPointer = &firstCopiedNoteRow;

	for (int i = 0; i < getCurrentClip()->noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = getCurrentClip()->noteRows.getElement(i);

		// If this NoteRow has any notes...
		if (!thisNoteRow->hasNoNotes()) {

			// And if any of them are in the right zone...
			int startI = thisNoteRow->notes.search(startPos, GREATER_OR_EQUAL);
			int endI = thisNoteRow->notes.search(endPos, GREATER_OR_EQUAL);

			int numNotes = endI - startI;

			if (numNotes > 0) {

				void* copiedNoteRowMemory = generalMemoryAllocator.alloc(sizeof(CopiedNoteRow), NULL, true);
				if (!copiedNoteRowMemory) {
ramError:
					deleteCopiedNoteRows();
					numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
					return;
				}

				// Make the new CopiedNoteRow object
				CopiedNoteRow* newCopiedNoteRow = new (copiedNoteRowMemory) CopiedNoteRow();

				// Put that on the list
				*prevPointer = newCopiedNoteRow;
				prevPointer = &newCopiedNoteRow->next;

				// Allocate some memory for the notes
				newCopiedNoteRow->notes = (Note*)generalMemoryAllocator.alloc(sizeof(Note) * numNotes, NULL, true);

				if (!newCopiedNoteRow->notes) goto ramError;

				// Fill in some details for the row
				newCopiedNoteRow->numNotes = numNotes;
				newCopiedNoteRow->yNote = thisNoteRow->y;
				if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) { // yDisplay for Kits
					newCopiedNoteRow->yDisplay = i - getCurrentClip()->yScroll;
				}
				else { // Or for non-Kits
					int yVisual = currentSong->getYVisualFromYNote(thisNoteRow->y, getCurrentClip()->inScaleMode);
					newCopiedNoteRow->yDisplay = yVisual - getCurrentClip()->yScroll;
				}

				// Fill in all the Notes' details
				for (int n = 0; n < numNotes; n++) {
					Note* noteToCopy = thisNoteRow->notes.getElement(n + startI);
					Note* newNote = &newCopiedNoteRow->notes[n];
					newNote->pos = noteToCopy->pos - startPos;
					newNote->length = getMin(
					    noteToCopy->length,
					    endPos
					        - noteToCopy
					              ->pos); // Ensure we don't copy the portion of the tail that extends beyond the screen
					newNote->velocity = noteToCopy->velocity;
					newNote->probability = noteToCopy->probability;
					newNote->lift = noteToCopy->lift;
				}
			}
		}
	}

	numericDriver.displayPopup(HAVE_OLED ? "Notes copied" : "COPY");
}

void InstrumentClipView::deleteCopiedNoteRows() {
	while (firstCopiedNoteRow) {
		CopiedNoteRow* toDelete = firstCopiedNoteRow;
		firstCopiedNoteRow = firstCopiedNoteRow->next;
		toDelete->~CopiedNoteRow();
		generalMemoryAllocator.dealloc(toDelete);
	}
}

void InstrumentClipView::pasteAutomation(int whichModEncoder) {
	if (!copiedParamAutomation.nodes) {
		numericDriver.displayPopup(HAVE_OLED ? "No automation to paste" : "NONE");
		return;
	}

	int startPos = getPosFromSquare(0);
	int endPos = getPosFromSquare(displayWidth);

	int pastedAutomationWidth = endPos - startPos;
	if (pastedAutomationWidth == 0) return;

	float scaleFactor = (float)pastedAutomationWidth / copiedParamAutomation.width;

	if (!view.activeModControllableModelStack.modControllable) return;

	ModelStackWithAutoParam* modelStackWithAutoParam =
	    view.activeModControllableModelStack.modControllable->getParamFromModEncoder(
	        whichModEncoder, &view.activeModControllableModelStack, true);
	if (!modelStackWithAutoParam || !modelStackWithAutoParam->autoParam) {
		numericDriver.displayPopup(HAVE_OLED ? "Can't paste automation" : "CANT");
		return;
	}

	Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_PASTE, false);

	if (action) {
		action->recordParamChangeIfNotAlreadySnapshotted(modelStackWithAutoParam, false);
	}

	bool isPatchCable = (modelStackWithAutoParam->paramCollection
	                     == modelStackWithAutoParam->paramManager->getPatchCableSetAllowJibberish());
	// Ok this is cursed, but will work fine so long as
	// the possibly invalid memory here doesn't accidentally
	// equal modelStack->paramCollection.

	modelStackWithAutoParam->autoParam->paste(startPos, endPos, scaleFactor, modelStackWithAutoParam,
	                                          &copiedParamAutomation, isPatchCable);

	numericDriver.displayPopup(HAVE_OLED ? "Automation pasted" : "PASTE");
	if (playbackHandler.isEitherClockActive()) currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
}

void InstrumentClipView::pasteNotes() {

	if (!firstCopiedNoteRow) return;

	if (false) {
ramError:
		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		return;
	}

	int startPos = getPosFromSquare(0);
	int endPos = getPosFromSquare(displayWidth);

	int pastedScreenWidth = endPos - startPos;
	if (pastedScreenWidth == 0) return;

	int pastedScaleType = getCurrentClip()->getScaleType();

	float scaleFactor = (float)pastedScreenWidth / (uint32_t)copiedScreenWidth;

	Action* action = actionLogger.getNewAction(ACTION_NOTES_PASTE, false);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	getCurrentClip()->clearArea(modelStack, startPos, endPos, action);

	// Kit
	if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {

		for (CopiedNoteRow* thisCopiedNoteRow = firstCopiedNoteRow; thisCopiedNoteRow;
		     thisCopiedNoteRow = thisCopiedNoteRow->next) {
			int noteRowId = thisCopiedNoteRow->yDisplay + getCurrentClip()->yScroll;

			if (noteRowId < 0) continue;
			if (noteRowId >= getCurrentClip()->noteRows.getNumElements()) break;

			NoteRow* thisNoteRow = getCurrentClip()->noteRows.getElement(noteRowId);

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);

			bool success = thisNoteRow->paste(modelStackWithNoteRow, thisCopiedNoteRow, scaleFactor, endPos, action);
			if (!success) goto ramError;
		}
	}

	// Non-kit
	else {

		// If neither the source nor the destination was a kit Clip, and one had a scale and the other didn't, we want to preserve some scale information which we otherwise wouldn't
		bool shouldPreserveScale = (copiedScaleType != SCALE_TYPE_KIT && copiedScaleType != pastedScaleType);

		for (CopiedNoteRow* thisCopiedNoteRow = firstCopiedNoteRow; thisCopiedNoteRow;
		     thisCopiedNoteRow = thisCopiedNoteRow->next) {
			int yNote;

			if (shouldPreserveScale) {
				yNote = getCurrentClip()->getYNoteFromYDisplay(0, currentSong) + thisCopiedNoteRow->yNote
				        - copiedYNoteOfBottomRow;
			}
			else {
				yNote = getCurrentClip()->getYNoteFromYDisplay(thisCopiedNoteRow->yDisplay, currentSong);
			}

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    getCurrentClip()->getOrCreateNoteRowForYNote(yNote, modelStack, action, NULL);
			NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			if (!thisNoteRow) goto ramError;

			bool success = thisNoteRow->paste(modelStackWithNoteRow, thisCopiedNoteRow, scaleFactor, endPos, action);
			if (!success) goto ramError;
		}
	}

getOut:
	recalculateColours();
	uiNeedsRendering(this);
	numericDriver.displayPopup(HAVE_OLED ? "Notes pasted" : "PASTE");
}

void InstrumentClipView::doubleClipLengthAction() {

	// If too big...
	if (currentSong->currentClip->loopLength > (MAX_SEQUENCE_LENGTH >> 1)) {
		numericDriver.displayPopup(HAVE_OLED ? "Maximum length reached" : "CANT");
		return;
	}

	Action* action = actionLogger.getNewAction(ACTION_CLIP_MULTIPLY, false);

	// Add the ConsequenceClipMultiply to the Action. This must happen before calling doubleClipLength(), which may add note changes and deletions,
	// because when redoing, those have to happen after (and they'll have no effect at all, but who cares)
	if (action) {
		void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceInstrumentClipMultiply));

		if (consMemory) {
			ConsequenceInstrumentClipMultiply* newConsequence = new (consMemory) ConsequenceInstrumentClipMultiply();
			action->addConsequence(newConsequence);
		}
	}

	// Double the length, and duplicate the Clip content too
	currentSong->doubleClipLength(getCurrentClip(), action);

	zoomToMax(false);

	if (action) {
		action->xZoomClip[AFTER] = currentSong->xZoom[NAVIGATION_CLIP];
		action->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
	}

	displayZoomLevel();

#if HAVE_OLED
	OLED::consoleText("Clip multiplied");
#endif
}

void InstrumentClipView::createNewInstrument(uint8_t newInstrumentType) {

	InstrumentClipMinder::createNewInstrument(newInstrumentType);

	recalculateColours();
	uiNeedsRendering(this);

	if (newInstrumentType == INSTRUMENT_TYPE_KIT) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		NoteRow* noteRow = getCurrentClip()->noteRows.getElement(0);

		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(0, noteRow);

		enterDrumCreator(modelStackWithNoteRow);
	}
}

void InstrumentClipView::changeInstrumentType(uint8_t newInstrumentType) {

	if (currentSong->currentClip->output->type == newInstrumentType) return;

	InstrumentClipMinder::changeInstrumentType(newInstrumentType);

	recalculateColours();
	uiNeedsRendering(this);
}

void InstrumentClipView::selectEncoderAction(int8_t offset) {

	// User may be trying to edit noteCode...
	if (currentUIMode == UI_MODE_AUDITIONING) {
		if (Buttons::isButtonPressed(selectEncButtonX, selectEncButtonY)) {

			if (playbackHandler.isEitherClockActive() && playbackHandler.ticksLeftInCountIn) return;

			cutAuditionedNotesToOne();
			offsetNoteCodeAction(offset);
		}
	}

	// Or set / create a new Drum
	else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {
		if (Buttons::isButtonPressed(selectEncButtonX, selectEncButtonY)) {
			drumForNewNoteRow = flipThroughAvailableDrums(offset, drumForNewNoteRow, true);
			//setSelectedDrum(drumForNewNoteRow); // Can't - it doesn't have a NoteRow, and so we don't really know where its ParamManager is!
			drawDrumName(drumForNewNoteRow);
		}
	}

	// Or, if user holding a note(s) down, we'll adjust proability instead
	else if (currentUIMode == UI_MODE_NOTES_PRESSED) {
		adjustProbability(offset);
	}

	// Or, normal option - trying to change Instrument presets
	else {
		InstrumentClipMinder::selectEncoderAction(offset);
	}
}

const uint32_t editPadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

const uint32_t mutePadActionUIModes[] = {UI_MODE_AUDITIONING, UI_MODE_STUTTERING, 0};

const uint32_t auditionPadActionUIModes[] = {UI_MODE_AUDITIONING,
                                             UI_MODE_ADDING_DRUM_NOTEROW,
                                             UI_MODE_HORIZONTAL_SCROLL,
                                             UI_MODE_RECORD_COUNT_IN,
                                             UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON,
                                             0};

int InstrumentClipView::padAction(int x, int y, int velocity) {

	// Edit pad action...
	if (x < displayWidth) {
		if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

		// Perhaps the user wants to enter the SoundEditor via a shortcut. They can do this by holding an audition pad too - but this gets deactivated
		// if they've done any "euclidean" or per-NoteRow editing already by holding down that audition pad, because if they've done that, they're probably
		// not intending to deliberately go into the SoundEditor, but might be trying to edit notes. Which they currently can't do...
		if (velocity && (!isUIModeActive(UI_MODE_AUDITIONING) || !editedAnyPerNoteRowStuffSinceAuditioningBegan)) {

			int soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, velocity);

			if (soundEditorResult == ACTION_RESULT_NOT_DEALT_WITH) goto doRegularEditPadActionProbably;
			else return soundEditorResult;
		}

		// Regular edit-pad action
		else {
doRegularEditPadActionProbably:
			if (isUIModeWithinRange(editPadActionUIModes)) {
				editPadAction(velocity, y, x, currentSong->xZoom[NAVIGATION_CLIP]);
			}
		}
	}

	// If mute pad action
	else if (x == displayWidth) {
		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (currentSong->currentClip->output->type != INSTRUMENT_TYPE_KIT) return ACTION_RESULT_DEALT_WITH;
			NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(y, currentSong);
			if (!noteRow || !noteRow->drum) return ACTION_RESULT_DEALT_WITH;
			view.noteRowMuteMidiLearnPadPressed(velocity, noteRow);
		}
		else if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT && lastAuditionedYDisplay == y
		         && isUIModeActive(UI_MODE_AUDITIONING) && getNumNoteRowsAuditioning() == 1) {
			if (velocity) {
				if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
					enterUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
				}
				else goto maybeRegularMutePress;
			}
			else {
				if (isUIModeActive(UI_MODE_DRAGGING_KIT_NOTEROW)) {
					exitUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
				}
				else goto maybeRegularMutePress;
			}
		}
		else {
maybeRegularMutePress:
			if (isUIModeWithinRange(mutePadActionUIModes) && velocity) {
				mutePadPress(y);
			}
		}
	}

	// Audition pad action
	else {
possiblyAuditionPad:
		if (x == displayWidth + 1) {

			// "Learning" to this audition pad:
			if (isUIModeActiveExclusively(UI_MODE_MIDI_LEARN)) {
				if (getCurrentUI() == this) {
					if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

					if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
						NoteRow* thisNoteRow = getCurrentClip()->getNoteRowOnScreen(y, currentSong);
						if (!thisNoteRow || !thisNoteRow->drum) return ACTION_RESULT_DEALT_WITH;
						view.drumMidiLearnPadPressed(velocity, thisNoteRow->drum,
						                             (Kit*)currentSong->currentClip->output);
					}
					else {
						view.melodicInstrumentMidiLearnPadPressed(velocity,
						                                          (MelodicInstrument*)currentSong->currentClip->output);
					}
				}
			}

			// Changing the scale:
			else if (isUIModeActiveExclusively(UI_MODE_SCALE_MODE_BUTTON_PRESSED)) {
				if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

				if (velocity
				    && currentSong->currentClip->output->type
				           != INSTRUMENT_TYPE_KIT) { // We probably couldn't have got this far if it was a Kit, but let's just check
					if (getCurrentClip()->inScaleMode) {
						currentUIMode = UI_MODE_NONE; // So that the upcoming render of the sidebar comes out correctly
						changeRootNote(y);
						exitScaleModeOnButtonRelease = false;
					}
					else enterScaleMode(y);
				}
			}

			// Actual basic audition pad press:
			else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
				exitUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
				if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine)
					return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allowable sometimes if in card routine.
				auditionPadAction(velocity, y, Buttons::isShiftButtonPressed());
			}
		}
	}

	return ACTION_RESULT_DEALT_WITH;
}

uint8_t InstrumentClipView::getEditPadPressXDisplayOnScreen(uint8_t yDisplay) {
	for (int i = 0; i < editPadPressBufferSize; i++) {
		if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay) return editPadPresses[i].xDisplay;
	}

	return 0; // Presumably impossible case
}

void InstrumentClipView::editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, unsigned int xZoom) {

	uint32_t squareStart = getPosFromSquare(xDisplay);

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If button down
	if (state) {

		// Don't allow further new presses if already done nudging
		if (numEditPadPresses && doneAnyNudgingSinceFirstEditPadPress) return;

		if (!isSquareDefined(xDisplay)) {
			return;
		}

		// Get existing NoteRow if there was one
		ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

		// If no NoteRow yet...
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) {

			// Just check we're not beyond Clip length
			if (squareStart >= clip->loopLength) return;

			// And create the new NoteRow
			modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, yDisplay);
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				if (instrument->type == INSTRUMENT_TYPE_KIT) setSelectedDrum(NULL);
				return;
			}

			// If that just created a new NoteRow for a Kit, then we can't undo any further back than this
			if (instrument->type == INSTRUMENT_TYPE_KIT) {
				actionLogger.deleteAllLogs();
			}
		}

		int32_t effectiveLength = modelStackWithNoteRow->getLoopLength();

		// Now that we've definitely got a NoteRow, check against NoteRow "effective" length here (though it'll very possibly be the same as the Clip length we may have tested against above).
		if (squareStart >= effectiveLength) return;

		uint32_t squareWidth = getSquareWidth(xDisplay, effectiveLength);

		NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

		ParamManagerForTimeline* paramManager = NULL;
		if (instrument->type == INSTRUMENT_TYPE_SYNTH) paramManager = &clip->paramManager;
		else if (instrument->type == INSTRUMENT_TYPE_KIT) paramManager = &noteRow->paramManager;

		// If this is a note-length-edit press...
		if (numEditPadPressesPerNoteRowOnScreen[yDisplay] == 1
		    && (int32_t)(timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0
		    && clip->allowNoteTails(modelStackWithNoteRow) && getEditPadPressXDisplayOnScreen(yDisplay) < xDisplay) {

			// Find that original press
			int i;
			for (i = 0; i < editPadPressBufferSize; i++) {
				if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay) break;
			}

			// If we found it...
			if (i < editPadPressBufferSize) {

				int32_t oldLength;
				int32_t noteStartPos;

				// If multiple notes, pick the last one
				if (editPadPresses[i].isBlurredSquare) {
					int noteI = noteRow->notes.search(squareStart + squareWidth, LESS);
					Note* note = noteRow->notes.getElement(noteI);
					if (note) {
						oldLength = note->getLength();
						noteStartPos = note->pos;
					}
				}

				else {
					oldLength = editPadPresses[i].intendedLength;
					noteStartPos = editPadPresses[i].intendedPos;
				}

				// First, figure out the lengh to take the note up to the start of the pressed square. Put it in newLength
				int newLength = squareStart - noteStartPos;
				if (newLength < 0) newLength += effectiveLength; // Wrapped note

				// If current square wasn't occupied at all to begin with, fill it up
				if (oldLength <= newLength) newLength += squareWidth;

				if (newLength == 0)
					newLength = squareWidth; // Protection - otherwise we could end up with a 0-length note!

				Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

				int32_t areaStart, areaWidth;
				bool actuallyExtendNoteAtStartOfArea = (newLength > oldLength);

				if (actuallyExtendNoteAtStartOfArea) { // Increasing length

					// Make sure it doesn't eat into the next note
					int32_t maxLength = noteRow->getDistanceToNextNote(noteStartPos, modelStackWithNoteRow);
					newLength = getMin(newLength, maxLength);

					areaStart = noteStartPos;
					areaWidth = newLength;
				}

				else { // Decreasing length
					areaStart = noteStartPos + newLength;
					areaWidth = oldLength - newLength;
				}

				noteRow->clearArea(areaStart, areaWidth, modelStackWithNoteRow, action, clip->getWrapEditLevel(),
				                   actuallyExtendNoteAtStartOfArea);

				if (!editPadPresses[i].isBlurredSquare) editPadPresses[i].intendedLength = newLength;
				editPadPresses[i].deleteOnDepress = false;
				uiNeedsRendering(this, 1 << yDisplay, 0);

				if (instrument->type == INSTRUMENT_TYPE_KIT) {
					setSelectedDrum(noteRow->drum);
				}
			}
		}

		// Or, if this is a regular create-or-select press...
		else {

			timeLastEditPadPress = AudioEngine::audioSampleTimer;
			// Find an empty space in the press buffer, if there is one
			int i;
			for (i = 0; i < editPadPressBufferSize; i++) {
				if (!editPadPresses[i].isActive) break;
			}
			if (i < editPadPressBufferSize) {

				ParamManagerForTimeline* paramManagerDummy;
				Sound* sound = getSoundForNoteRow(noteRow, &paramManagerDummy);

				uint32_t whichRowsToReRender = (1 << yDisplay);

				Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

				uint32_t desiredNoteLength = squareWidth;
				if (sound) {

					int yNote;

					if (instrument->type == INSTRUMENT_TYPE_KIT) {
						yNote = 60;
					}
					else {
						yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					}

					// If a time-synced sample...
					uint32_t sampleLength = sound->hasAnyTimeStretchSyncing(paramManager, true, yNote);
					if (sampleLength) {
						uint32_t sampleLengthInTicks =
						    ((uint64_t)sampleLength << 32) / currentSong->timePerTimerTickBig;

						// Previously I was having it always jump to a "square" number, but as James Meharry pointed out, what if the Clip is deliberately a
						// non-square length?
						desiredNoteLength = effectiveLength;
						while (!(desiredNoteLength & 1))
							desiredNoteLength >>= 1;

						while (desiredNoteLength * 1.41 < sampleLengthInTicks)
							desiredNoteLength <<= 1;

						// If desired note length too long and no existing notes, extend the Clip (or if the NoteRow has independent length, do that instead).
						if (noteRow->hasNoNotes() && !clip->wrapEditing && desiredNoteLength > effectiveLength) {
							squareStart = 0;
							if (noteRow->loopLengthIfIndependent) {
								noteRow->loopLengthIfIndependent = desiredNoteLength;
							}
							else {
								currentSong->setClipLength(clip, desiredNoteLength, action);

								// Clip length changing may visually change other rows too, so must re-render them all
								whichRowsToReRender = 0xFFFFFFFF;
							}
						}
					}

					// Or if general cut-mode samples - but only for kit Clips, not synth
					else if (instrument->type == INSTRUMENT_TYPE_KIT) {
						bool anyLooping;
						sampleLength = sound->hasCutOrLoopModeSamples(paramManager, yNote, &anyLooping);
						if (sampleLength) {

							// If sample loops, we want to cut out before we get to the loop-point
							if (anyLooping) {
								desiredNoteLength = ((uint64_t)sampleLength << 32) / currentSong->timePerTimerTickBig;
							}

							// Or if sample doesn't loop, we want to extend just past the end point
							else {
								desiredNoteLength =
								    (int)((sampleLength - 2) / currentSong->getTimePerTimerTickFloat()) + 1;
							}
						}
					}

					desiredNoteLength = getMax(desiredNoteLength, squareWidth);
				}

				uint32_t maxNoteLengthHere = clip->getWrapEditLevel();
				desiredNoteLength = getMin(desiredNoteLength, maxNoteLengthHere);

				Note* firstNote;
				Note* lastNote;
				uint8_t squareType =
				    noteRow->getSquareType(squareStart, squareWidth, &firstNote, &lastNote, modelStackWithNoteRow,
				                           clip->allowNoteTails(modelStackWithNoteRow), desiredNoteLength, action,
				                           playbackHandler.isEitherClockActive() && currentSong->isClipActive(clip),
				                           isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON));

				// If error (no ram left), get out
				if (!squareType) {
					numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
					return;
				}

				// Otherwise, we've selected a note
				else {

					shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;

					// If this is the first press, record the time
					if (numEditPadPresses == 0) {
						timeFirstEditPadPress = AudioEngine::audioSampleTimer;
						doneAnyNudgingSinceFirstEditPadPress = false;
						offsettingNudgeNumberDisplay = false;
						shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
					}

					if (squareType == SQUARE_BLURRED) {
						editPadPresses[i].intendedPos = squareStart;
						editPadPresses[i].intendedLength = squareWidth;
						editPadPresses[i].deleteOnDepress = true;
					}
					else {
						editPadPresses[i].intendedPos = lastNote->pos;
						editPadPresses[i].intendedLength = lastNote->getLength();
						editPadPresses[i].deleteOnDepress =
						    (squareType == SQUARE_NOTE_HEAD || squareType == SQUARE_NOTE_TAIL_UNMODIFIED);
					}

					editPadPresses[i].isBlurredSquare = (squareType == SQUARE_BLURRED);
					editPadPresses[i].intendedVelocity = firstNote->getVelocity();
					editPadPresses[i].intendedProbability = firstNote->getProbability();
					editPadPresses[i].isActive = true;
					editPadPresses[i].yDisplay = yDisplay;
					editPadPresses[i].xDisplay = xDisplay;
					editPadPresses[i].deleteOnScroll = true;
					editPadPresses[i].mpeCachedYet = false;
					for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
						editPadPresses[i].stolenMPE[m].num = 0;
					}
					numEditPadPresses++;
					numEditPadPressesPerNoteRowOnScreen[yDisplay]++;
					enterUIMode(UI_MODE_NOTES_PRESSED);

					// If new note...
					if (squareType == SQUARE_NEW_NOTE) {

						// If we're cross-screen-editing, create other corresponding notes too
						if (clip->wrapEditing) {
							int error = noteRow->addCorrespondingNotes(
							    squareStart, desiredNoteLength, editPadPresses[i].intendedVelocity,
							    modelStackWithNoteRow, clip->allowNoteTails(modelStackWithNoteRow), action);

							if (error) numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
						}
					}

					// Edit mod knob values for this Note's region
					int32_t distanceToNextNote = clip->getDistanceToNextNote(lastNote, modelStackWithNoteRow);

					if (instrument->type == INSTRUMENT_TYPE_KIT) {
						setSelectedDrum(noteRow->drum);
					}

					// Can only set the mod region after setting the selected drum! Otherwise the params' currentValues don't end up right
					view.setModRegion(
					    firstNote->pos,
					    getMax((uint32_t)distanceToNextNote + lastNote->pos - firstNote->pos, squareWidth),
					    modelStackWithNoteRow->noteRowId);

					// Now that we're holding a note down, get set up for if the user wants to edit its MPE values.
					for (int t = 0; t < MPE_RECORD_LENGTH_FOR_NOTE_EDITING; t++) {
						mpeValuesAtHighestPressure[t][0] = 0;
						mpeValuesAtHighestPressure[t][1] = 0;
						mpeValuesAtHighestPressure[t][2] = -1; // -1 means not valid yet
					}
					mpeMostRecentPressure = 0;
					mpeRecordLastUpdateTime = AudioEngine::audioSampleTimer;

					reassessAuditionStatus(yDisplay);
				}

				// Might need to re-render row, if it was changed
				if (squareType == SQUARE_NEW_NOTE || squareType == SQUARE_NOTE_TAIL_MODIFIED) {
					uiNeedsRendering(this, whichRowsToReRender, 0);
				}
			}
		}
	}

	// Or if pad press ended...
	else {

		// Find the corresponding press, if there is one
		int i;
		for (i = 0; i < editPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay
			    && editPadPresses[i].xDisplay == xDisplay)
				break;
		}

		// If we found it...
		if (i < editPadPressBufferSize) {

			numericDriver.cancelPopup(); // Crude way of getting rid of the probability-editing permanent popup

			uint8_t velocity = editPadPresses[i].intendedVelocity;

			// Must mark it as inactive first, otherwise, the note-deletion code may do so and then we'd do it again here
			endEditPadPress(i);

			// If we're meant to be deleting it on depress...
			if (editPadPresses[i].deleteOnDepress
			    && AudioEngine::audioSampleTimer - timeLastEditPadPress < (44100 >> 1)) {

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);

				Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

				int wrapEditLevel = clip->getWrapEditLevel();

				noteRow->clearArea(squareStart, getSquareWidth(xDisplay, modelStackWithNoteRow->getLoopLength()),
				                   modelStackWithNoteRow, action, wrapEditLevel);

				noteRow->clearMPEUpUntilNextNote(modelStackWithNoteRow, squareStart, wrapEditLevel, true);

				uiNeedsRendering(this, 1 << yDisplay, 0);
			}

			// Or if not deleting...
			else {
				instrument->defaultVelocity = velocity;
			}

			// Close last note nudge action, if there was one - so each such action is for one consistent set of notes
			actionLogger.closeAction(ACTION_NOTE_NUDGE);

			// If *all* presses are now ended
			checkIfAllEditPadPressesEnded();

			reassessAuditionStatus(yDisplay);
		}
	}
}

Sound* InstrumentClipView::getSoundForNoteRow(NoteRow* noteRow, ParamManagerForTimeline** getParamManager) {
	if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_SYNTH) {
		*getParamManager = &currentSong->currentClip->paramManager;
		return (SoundInstrument*)currentSong->currentClip->output;
	}
	else if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT && noteRow && noteRow->drum
	         && noteRow->drum->type == DRUM_TYPE_SOUND) {
		if (!noteRow) return NULL;
		*getParamManager = &noteRow->paramManager;
		return (SoundDrum*)noteRow->drum;
	}
	else {
		*getParamManager = NULL;
		return NULL;
	}
}

void InstrumentClipView::endEditPadPress(uint8_t i) {
	editPadPresses[i].isActive = false;
	numEditPadPresses--;
	numEditPadPressesPerNoteRowOnScreen[editPadPresses[i].yDisplay]--;

	for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
		if (editPadPresses[i].stolenMPE[m].num) {
			generalMemoryAllocator.dealloc(editPadPresses[i].stolenMPE[m].nodes);
		}
	}
}

void InstrumentClipView::checkIfAllEditPadPressesEnded(bool mayRenderSidebar) {
	if (numEditPadPresses == 0) {
		view.setModRegion();
		exitUIMode(UI_MODE_NOTES_PRESSED);
		actionLogger.closeAction(ACTION_NOTE_EDIT);
	}
}

void InstrumentClipView::adjustVelocity(int velocityChange) {

	int velocityValue = 0;

	Action* action;
	if (HAVE_OLED || numericDriver.popupActive) {
		action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
		if (!action) return; // Necessary why?
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	for (int i = 0; i < editPadPressBufferSize; i++) {
		if (editPadPresses[i].isActive) {
			editPadPresses[i].deleteOnDepress = false;

			int noteRowIndex;
			NoteRow* noteRow =
			    getCurrentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
			int noteRowId = getCurrentClip()->getNoteRowId(noteRow, noteRowIndex);

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

			// Multiple notes in square
			if (editPadPresses[i].isBlurredSquare) {

				uint32_t velocitySumThisSquare = 0;
				uint32_t numNotesThisSquare = 0;

				int noteI = noteRow->notes.search(editPadPresses[i].intendedPos, GREATER_OR_EQUAL);
				Note* note = noteRow->notes.getElement(noteI);
				while (note && note->pos - editPadPresses[i].intendedPos < editPadPresses[i].intendedLength) {

#if HAVE_OLED
					if (OLED::isPopupPresent()) {
#else
					if (numericDriver.popupActive) {
#endif
						noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
						                                     CORRESPONDING_NOTES_ADJUST_VELOCITY, velocityChange);
					}

					if (velocityValue == 0) {
						velocityValue = note->getVelocity();
					}
					else {
						if (velocityValue != note->getVelocity()) velocityValue = 255; // Means "multiple"
					}
					numNotesThisSquare++;
					velocitySumThisSquare += note->getVelocity();

					noteI++;
					note = noteRow->notes.getElement(noteI);
				}

				editPadPresses[i].intendedVelocity =
				    velocitySumThisSquare
				    / numNotesThisSquare; // Get the average. Ideally we'd have done this when first selecting the note too, but I didn't
			}

			// Only one note in square
			else {
#if HAVE_OLED
				if (OLED::isPopupPresent()) {
#else
				if (numericDriver.popupActive) {
#endif
					editPadPresses[i].intendedVelocity =
					    getMax(1, getMin(127, (int)editPadPresses[i].intendedVelocity + velocityChange));
					noteRow->changeNotesAcrossAllScreens(editPadPresses[i].intendedPos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_ADJUST_VELOCITY, velocityChange);
				}

				if (velocityValue == 0) {
					velocityValue = editPadPresses[i].intendedVelocity;
				}
				else {
					if (velocityValue != editPadPresses[i].intendedVelocity) velocityValue = 255; // Means "multiple"
				}
			}
		}
	}

	if (velocityValue) {
		char buffer[22];
		char const* displayString;
		if (velocityValue == 255) {
			if (velocityChange >= 0) displayString = HAVE_OLED ? "Velocity increased" : "MORE";
			else displayString = HAVE_OLED ? "Velocity decreased" : "LESS";

			// Don't bother trying to think of some smart way to update lastVelocityInteractedWith. It'll get updated when user releases last press.
		}
		else {
#if HAVE_OLED
			strcpy(buffer, "Velocity: ");
			intToString(velocityValue, buffer + strlen(buffer));
#else
			intToString(velocityValue, buffer);
#endif
			displayString = buffer;
			((Instrument*)currentSong->currentClip->output)->defaultVelocity = velocityValue;
		}
#if HAVE_OLED
		OLED::popupText(displayString);
#else
		numericDriver.displayPopup(displayString, 0, true);
#endif
	}

	reassessAllAuditionStatus();
}

void InstrumentClipView::adjustProbability(int offset) {

	int probabilityValue = -1;

	bool prevBase = false;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If just one press...
	if (numEditPadPresses == 1) {
		// Find it
		for (int i = 0; i < editPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {
				editPadPresses[i].deleteOnDepress = false;

				if (editPadPresses[i].isBlurredSquare) goto multiplePresses;

				int probability = editPadPresses[i].intendedProbability;

				probabilityValue = probability & 127;
				prevBase = (probability & 128);

				// If editing, continue edit
#if HAVE_OLED
				if (OLED::isPopupPresent()) {
#else
				if (numericDriver.popupActive) {
#endif
					Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
					if (!action) return;

					// Incrementing
					if (offset == 1) {
						if (probabilityValue < NUM_PROBABILITY_VALUES + 35) {
							if (prevBase) {
								probabilityValue++;
								prevBase = false;
							}
							else {
								// See if there's a prev-base
								if (probabilityValue < NUM_PROBABILITY_VALUES
								    && getCurrentClip()->doesProbabilityExist(
								        editPadPresses[i].intendedPos, probabilityValue,
								        NUM_PROBABILITY_VALUES - probabilityValue)) {
									prevBase = true;
								}
								else {
									probabilityValue++;
								}
							}
						}
					}

					// Decrementing
					else {
						if (probabilityValue > 1 || prevBase) {
							if (prevBase) {
								prevBase = false;
							}
							else {
								probabilityValue--;
								prevBase = (probabilityValue < NUM_PROBABILITY_VALUES
								            && getCurrentClip()->doesProbabilityExist(
								                editPadPresses[i].intendedPos, probabilityValue,
								                NUM_PROBABILITY_VALUES - probabilityValue));
							}
						}
					}

					editPadPresses[i].intendedProbability = probabilityValue;
					if (prevBase) editPadPresses[i].intendedProbability |= 128;

					int noteRowIndex;
					NoteRow* noteRow =
					    getCurrentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
					int noteRowId = getCurrentClip()->getNoteRowId(noteRow, noteRowIndex);
					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

					noteRow->changeNotesAcrossAllScreens(editPadPresses[i].intendedPos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_SET_PROBABILITY,
					                                     editPadPresses[i].intendedProbability);
				}
				break;
			}
		}
	}

	// Or if multiple presses...
	else {
multiplePresses:

		int leftMostPos = 2147483647;
		int leftMostIndex;
		// Find the leftmost one. There may be more than one...
		for (int i = 0; i < editPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {
				editPadPresses[i].deleteOnDepress = false;

				// "blurred square" with multiple notes
				if (editPadPresses[i].isBlurredSquare) {
					NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong);
					int noteI = noteRow->notes.search(editPadPresses[i].intendedPos, GREATER_OR_EQUAL);
					Note* note = noteRow->notes.getElement(noteI);
					if (note) {
						editPadPresses[i].intendedProbability =
						    note->probability; // This might not have been grabbed properly initially
						if (note->pos < leftMostPos) {
							leftMostPos = note->pos;
							leftMostIndex = i;
						}
					}
				}

				// Or, just 1 note in square
				else {

					if (editPadPresses[i].intendedPos < leftMostPos) {
						leftMostPos = editPadPresses[i].intendedPos;
						leftMostIndex = i;
					}
				}
			}
		}

		// Decide the probability, based on the existing probability of the leftmost note
		probabilityValue = editPadPresses[leftMostIndex].intendedProbability & 127;
		probabilityValue += offset;
		probabilityValue = getMax(1, probabilityValue);
		probabilityValue = getMin(NUM_PROBABILITY_VALUES + 35, probabilityValue);

		Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
		if (!action) return;

		// Set the probability of the other presses, and update all probabilities with the actual notes
		for (int i = 0; i < editPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {

				// Update probability
				editPadPresses[i].intendedProbability = probabilityValue;

				int noteRowIndex;
				NoteRow* noteRow =
				    getCurrentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
				int noteRowId = getCurrentClip()->getNoteRowId(noteRow, noteRowIndex);

				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

				// "blurred square" with multiple notes
				if (editPadPresses[i].isBlurredSquare) {

					int noteI = noteRow->notes.search(editPadPresses[i].intendedPos, GREATER_OR_EQUAL);
					Note* note = noteRow->notes.getElement(noteI);
					while (note && note->pos - editPadPresses[i].intendedPos < editPadPresses[i].intendedLength) {

						// And if not one of the leftmost notes, make it a prev-base one - if we're doing actual percentage probabilities
						if (probabilityValue < NUM_PROBABILITY_VALUES && note->pos != leftMostPos)
							editPadPresses[i].intendedProbability |= 128; // This isn't perfect...
						noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
						                                     CORRESPONDING_NOTES_SET_PROBABILITY,
						                                     editPadPresses[i].intendedProbability);

						noteI++;
						note = noteRow->notes.getElement(noteI);
					}
				}
				// Or, just 1 note in square
				else {
					// And if not one of the leftmost notes, make it a prev-base one - if we're doing actual percentage probabilities
					if (probabilityValue < NUM_PROBABILITY_VALUES && editPadPresses[i].intendedPos != leftMostPos)
						editPadPresses[i].intendedProbability |= 128;
					noteRow->changeNotesAcrossAllScreens(editPadPresses[i].intendedPos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_SET_PROBABILITY,
					                                     editPadPresses[i].intendedProbability);
				}
			}
		}
	}

	if (probabilityValue != -1) {
#if HAVE_OLED
		char buffer[29];
#else
		char buffer[5];
#endif
		char* displayString;
		if (probabilityValue <= NUM_PROBABILITY_VALUES) {
#if HAVE_OLED
			strcpy(buffer, "Probability: ");
			intToString(probabilityValue * 5, buffer + strlen(buffer));
			strcat(buffer, "%");
			if (prevBase) strcat(buffer, " latching");
#else
			intToString(probabilityValue * 5, buffer);
#endif
			displayString = buffer;
		}

		// Iteration dependence
		else {

			int divisor, iterationWithinDivisor;
			dissectIterationDependence(probabilityValue, &divisor, &iterationWithinDivisor);

			int charPos = 0;

#if HAVE_OLED
			strcpy(buffer, "Iteration dependence: ");
			charPos = strlen(buffer);
#endif

			buffer[charPos++] = '1' + iterationWithinDivisor;
#if HAVE_OLED
			buffer[charPos++] = ' ';
#endif
			buffer[charPos++] = 'o';
			buffer[charPos++] = 'f';
#if HAVE_OLED
			buffer[charPos++] = ' ';
#endif
			buffer[charPos++] = '0' + divisor;
			buffer[charPos++] = 0;
		}

#if HAVE_OLED
		OLED::popupText(displayString);
#else
		numericDriver.displayPopup(displayString, 0, true, prevBase ? 3 : 255);
#endif
	}
}

void InstrumentClipView::mutePadPress(uint8_t yDisplay) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	// We do not want to change the selected Drum if stutter is happening, because the user needs to keep controlling, and eventually stop stuttering on, their current selected Drum
	bool wasStuttering = isUIModeActive(UI_MODE_STUTTERING);

	// Try getting existing NoteRow.
	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

	// If no existing NoteRow...
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {

		// For Kits, get out.
		if (clip->output->type == INSTRUMENT_TYPE_KIT) {
fail:
			if (!wasStuttering) setSelectedDrum(NULL);
			return;
		}

		// Create new NoteRow.
		modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, yDisplay);
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) return;
	}

	NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

	clip->toggleNoteRowMute(modelStackWithNoteRow);

	if (!wasStuttering && clip->output->type == INSTRUMENT_TYPE_KIT) setSelectedDrum(noteRow->drum);

	uiNeedsRendering(this, 0, 1 << yDisplay);
}

NoteRow* InstrumentClipView::createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, int yDisplay,
                                                    int* getIndex) {
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	NoteRow* newNoteRow = clip->createNewNoteRowForKit(modelStack, (yDisplay < -clip->yScroll), getIndex);
	if (!newNoteRow) return NULL; // If memory full

	recalculateColour(yDisplay);

	return newNoteRow;
}

ModelStackWithNoteRow* InstrumentClipView::getOrCreateNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack,
                                                                         int yDisplay) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, yDisplay);
	}

	return modelStackWithNoteRow;
}

ModelStackWithNoteRow* InstrumentClipView::createNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack,
                                                                    int yDisplay) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	NoteRow* noteRow = NULL;
	int noteRowId;

	// If *not* a kit
	if (clip->output->type != INSTRUMENT_TYPE_KIT) {

		noteRow = clip->createNewNoteRowForYVisual(getYVisualFromYDisplay(yDisplay), modelStack->song);

		if (!noteRow) { // If memory full
doDisplayError:
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		}
		else {
			noteRowId = noteRow->y;
		}
	}

	// Or, if a kit
	else {
		// If it's more than one row below, we can't do it
		if (yDisplay < -1 - clip->yScroll) goto getOut;

		// If it's more than one row above, we can't do it
		if (yDisplay > clip->getNumNoteRows() - clip->yScroll) goto getOut;

		noteRow = createNewNoteRowForKit(modelStack, yDisplay, &noteRowId);

		if (!noteRow) {
			goto doDisplayError;
		}

		else {
			uiNeedsRendering(this, 0, 1 << yDisplay);
		}
	}

getOut:
	return modelStack->addNoteRow(noteRowId, noteRow);
}

void InstrumentClipView::recalculateColours() {
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		recalculateColour(yDisplay);
	}
}

void InstrumentClipView::recalculateColour(uint8_t yDisplay) {
	int colourOffset = 0;
	NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
	if (noteRow) colourOffset = noteRow->getColourOffset(getCurrentClip());
	getCurrentClip()->getMainColourFromY(getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong), colourOffset,
	                                     rowColour[yDisplay]);
	getTailColour(rowTailColour[yDisplay], rowColour[yDisplay]);
	getBlurColour(rowBlurColour[yDisplay], rowColour[yDisplay]);
}

int InstrumentClipView::scrollVertical(int scrollAmount, bool inCardRoutine, bool draggingNoteRow) {
	int noteRowToShiftI;
	int noteRowToSwapWithI;

	bool isKit = currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT;

	// If a Kit...
	if (isKit) {
		// Limit scrolling
		if (scrollAmount >= 0) {
			if ((int16_t)(getCurrentClip()->yScroll + scrollAmount) > (int16_t)(getCurrentClip()->getNumNoteRows() - 1))
				return ACTION_RESULT_DEALT_WITH;
		}
		else {
			if (getCurrentClip()->yScroll + scrollAmount < 1 - displayHeight) return ACTION_RESULT_DEALT_WITH;
		}

		// Limit how far we can shift a NoteRow
		if (draggingNoteRow) {
			noteRowToShiftI = lastAuditionedYDisplay + getCurrentClip()->yScroll;
			if (noteRowToShiftI < 0 || noteRowToShiftI >= getCurrentClip()->noteRows.getNumElements())
				return ACTION_RESULT_DEALT_WITH;

			if (scrollAmount >= 0) {
				if (noteRowToShiftI >= getCurrentClip()->noteRows.getNumElements() - 1) return ACTION_RESULT_DEALT_WITH;
				noteRowToSwapWithI = noteRowToShiftI + 1;
			}
			else {
				if (noteRowToShiftI == 0) return ACTION_RESULT_DEALT_WITH;
				noteRowToSwapWithI = noteRowToShiftI - 1;
			}
		}
	}

	// Or if not a Kit...
	else {
		int newYNote;
		if (scrollAmount > 0) {
			newYNote = getCurrentClip()->getYNoteFromYDisplay(displayHeight - 1 + scrollAmount, currentSong);
		}
		else {
			newYNote = getCurrentClip()->getYNoteFromYDisplay(scrollAmount, currentSong);
		}

		if (!getCurrentClip()->isScrollWithinRange(scrollAmount, newYNote)) return ACTION_RESULT_DEALT_WITH;
	}

	if (inCardRoutine && (numEditPadPresses || draggingNoteRow)) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

	bool currentClipIsActive = currentSong->isClipActive(currentSong->currentClip);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Switch off any auditioned notes. But leave on the one whose NoteRow we're moving, if we are
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		if (lastAuditionedVelocityOnScreen[yDisplay] != 255
		    && (!draggingNoteRow || lastAuditionedYDisplay != yDisplay)) {
			sendAuditionNote(false, yDisplay, 127, 0);

			ModelStackWithNoteRow* modelStackWithNoteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (noteRow) {
				// If recording, record a note-off for this NoteRow, if one exists
				if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {
					getCurrentClip()->recordNoteOff(modelStackWithNoteRow);
				}
			}
		}
	}

	// If any presses happening, grab those Notes...
	if (numEditPadPresses) {

		Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

		for (int i = 0; i < editPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {
				if (editPadPresses[i].isBlurredSquare) {
					endEditPadPress(i); // We can't deal with multiple notes per square
					checkIfAllEditPadPressesEnded(false);
					reassessAuditionStatus(editPadPresses[i].yDisplay);
				}
				else {
					if (editPadPresses[i].deleteOnScroll) {
						int32_t pos = editPadPresses[i].intendedPos;
						ModelStackWithNoteRow* modelStackWithNoteRow =
						    getCurrentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, modelStack);
						NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRow();
						thisNoteRow->deleteNoteByPos(modelStackWithNoteRow, pos, action);

						ParamCollectionSummary* mpeParamsSummary =
						    thisNoteRow->paramManager.getExpressionParamSetSummary();
						ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
						if (mpeParams) {
							int32_t distanceToNextNote = thisNoteRow->getDistanceToNextNote(pos, modelStackWithNoteRow);
							int loopLength = modelStackWithNoteRow->getLoopLength();
							ModelStackWithParamCollection* modelStackWithParamCollection =
							    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(
							        mpeParams, mpeParamsSummary);

							for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
								StolenParamNodes* stolenNodeRecord = NULL;
								if (!editPadPresses[i].mpeCachedYet) {
									stolenNodeRecord = &editPadPresses[i].stolenMPE[m];
								}
								AutoParam* param = &mpeParams->params[m];
								ModelStackWithAutoParam* modelStackWithAutoParam =
								    modelStackWithParamCollection->addAutoParam(m, param);

								param->stealNodes(modelStackWithAutoParam, pos, distanceToNextNote, loopLength, action,
								                  stolenNodeRecord);
							}
						}

						editPadPresses[i].mpeCachedYet = true;
					}
				}
			}
		}
	}

	// Shift the selected NoteRow, if that's what we're doing. We know we're in Kit mode then
	if (draggingNoteRow) {

		actionLogger.deleteAllLogs(); // Can't undo past this!

		getCurrentClip()->noteRows.getElement(noteRowToShiftI)->y =
		    -32768; // Need to remember not to try and use the yNote value of this NoteRow if we switch back out of Kit mode
		getCurrentClip()->noteRows.swapElements(noteRowToShiftI, noteRowToSwapWithI);
	}

	// Do actual scroll
	getCurrentClip()->yScroll += scrollAmount;

	recalculateColours(); // Don't render - we'll do that after we've dealt with presses (potentially creating Notes)

	// Switch on any auditioned notes - remembering that the one we're shifting (if we are) was left on before
	bool drawnNoteCodeYet = false;
	bool forceStoppedAnyAuditioning = false;
	bool changedActiveModControllable = false;
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		if (lastAuditionedVelocityOnScreen[yDisplay] != 255) {
			// If shifting a NoteRow..
			if (draggingNoteRow && lastAuditionedYDisplay == yDisplay) {}

			// Otherwise, switch its audition back on
			else {
				// Check NoteRow exists, incase we've got a Kit
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);

				if (!isKit || modelStackWithNoteRow->getNoteRowAllowNull()) {

					if (modelStackWithNoteRow->getNoteRowAllowNull()
					    && modelStackWithNoteRow->getNoteRow()->soundingStatus == STATUS_SEQUENCED_NOTE) {}
					else {

						// Record note-on if we're recording
						if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {

							// If no NoteRow existed before, try creating one
							if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
								modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, yDisplay);
							}

							if (modelStackWithNoteRow->getNoteRowAllowNull()) {
								getCurrentClip()->recordNoteOn(
								    modelStackWithNoteRow,
								    ((Instrument*)currentSong->currentClip->output)->defaultVelocity);
							}
						}

						sendAuditionNote(
						    true, yDisplay, lastAuditionedVelocityOnScreen[yDisplay],
						    0); // Should this technically grab the note-length of the note if there is one?
					}
				}
				else {
					auditionPadIsPressed[yDisplay] = false;
					lastAuditionedVelocityOnScreen[yDisplay] = 255;
					forceStoppedAnyAuditioning = true;
				}
			}
			if (!draggingNoteRow && !drawnNoteCodeYet
			    && auditionPadIsPressed
			        [yDisplay]) { // If we're shiftingNoteRow, no need to re-draw the noteCode, because it'll be the same
				drawNoteCode(yDisplay);
				if (isKit) {
					Drum* newSelectedDrum = NULL;
					NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
					if (noteRow) newSelectedDrum = noteRow->drum;
					setSelectedDrum(newSelectedDrum, true);
					changedActiveModControllable = !getAffectEntire();
				}

				if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_SYNTH) {
					if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &multiRangeMenu) {
						multiRangeMenu.noteOnToChangeRange(
						    getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong)
						    + ((SoundInstrument*)currentSong->currentClip->output)->transpose);
					}
				}

				drawnNoteCodeYet = true;
			}
		}
	}
	if (forceStoppedAnyAuditioning) someAuditioningHasEnded(true);

	// If presses happening, place the Notes on the newly-aligned NoteRows
	if (numEditPadPresses > 0) {

		Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
		//if (!action) return; // Couldn't happen?

		action->updateYScrollClipViewAfter(getCurrentClip());

		for (int i = 0; i < editPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {

				// Try getting existing NoteRow. If none...
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, modelStack);
				if (!modelStackWithNoteRow->getNoteRowAllowNull()) {

					if (isKit) goto cancelPress;

					// Try creating NoteRow
					modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, editPadPresses[i].yDisplay);

					if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
						numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
cancelPress:
						endEditPadPress(i);
						continue;
					}
				}

				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

				int32_t pos = editPadPresses[i].intendedPos;

				bool success =
				    noteRow->attemptNoteAdd(pos, editPadPresses[i].intendedLength, editPadPresses[i].intendedVelocity,
				                            editPadPresses[i].intendedProbability, modelStackWithNoteRow, action);

				editPadPresses[i].deleteOnDepress = false;
				editPadPresses[i].deleteOnScroll = success;

				if (success) {
					if (editPadPresses[i].mpeCachedYet) {
						int anyActualNodes = 0;
						for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
							anyActualNodes += editPadPresses[i].stolenMPE[m].num;
						}

						if (anyActualNodes)
							noteRow->paramManager.ensureExpressionParamSetExists(
							    isKit); // If this fails, we'll detect that below.

						ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
						ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;

						if (mpeParams) {
							ModelStackWithParamCollection* modelStackWithParamCollection =
							    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(
							        mpeParams, mpeParamsSummary);

							int32_t distanceToNextNote = noteRow->getDistanceToNextNote(pos, modelStackWithNoteRow);
							int loopLength = modelStackWithNoteRow->getLoopLength();

							for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
								AutoParam* param = &mpeParams->params[m];
								ModelStackWithAutoParam* modelStackWithAutoParam =
								    modelStackWithParamCollection->addAutoParam(m, param);

								param->insertStolenNodes(modelStackWithAutoParam, pos, distanceToNextNote, loopLength,
								                         action, &editPadPresses[i].stolenMPE[m]);
							}
						}
					}
				}
			}
		}
		checkIfAllEditPadPressesEnded(false); // Don't allow to redraw sidebar - it's going to be redrawn below anyway
	}

	uiNeedsRendering(this); // Might be in waveform view
	return ACTION_RESULT_DEALT_WITH;
}

void InstrumentClipView::reassessAllAuditionStatus() {
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		reassessAuditionStatus(yDisplay);
	}
}

void InstrumentClipView::reassessAuditionStatus(uint8_t yDisplay) {
	uint32_t sampleSyncLength;
	uint8_t newVelocity = getVelocityForAudition(yDisplay, &sampleSyncLength);
	// If some change in the NoteRow's audition status (it's come on or off or had its velocity changed)...
	if (newVelocity != lastAuditionedVelocityOnScreen[yDisplay]) {

		// Switch note off if it was on
		if (lastAuditionedVelocityOnScreen[yDisplay] != 255) sendAuditionNote(false, yDisplay, 127, 0);

		// Switch note on if we want it on (it may have a different velocity now)
		if (newVelocity != 255) sendAuditionNote(true, yDisplay, newVelocity, sampleSyncLength);

		lastAuditionedVelocityOnScreen[yDisplay] = newVelocity;
	}
}

// This may send it on a different Clip, if a different one is the activeClip
void InstrumentClipView::sendAuditionNote(bool on, uint8_t yDisplay, uint8_t velocity, uint32_t sampleSyncLength) {
	Instrument* instrument = (Instrument*)currentSong->currentClip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	if (instrument->type == INSTRUMENT_TYPE_KIT) {
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStackWithTimelineCounter); // On *current* clip!

		NoteRow* noteRowOnCurrentClip = modelStackWithNoteRow->getNoteRowAllowNull();

		// There may be no NoteRow at all if a different Clip than the one we're viewing is the activeClip, and it can't be changed
		if (noteRowOnCurrentClip) {

			Drum* drum = noteRowOnCurrentClip->drum;

			if (drum) {

				if (currentSong->currentClip != instrument->activeClip) {
					modelStackWithTimelineCounter->setTimelineCounter(instrument->activeClip);
					modelStackWithNoteRow =
					    ((InstrumentClip*)instrument->activeClip)
					        ->getNoteRowForDrum(modelStackWithTimelineCounter, drum); // On *active* clip!
					if (!modelStackWithNoteRow->getNoteRowAllowNull()) return;
				}

				if (on) {
					if (drum->type == DRUM_TYPE_SOUND
					    && !modelStackWithNoteRow->getNoteRow()->paramManager.containsAnyMainParamCollections())
						numericDriver.freezeWithError("E325"); // Trying to catch an E313 that Vinz got
					((Kit*)instrument)->beginAuditioningforDrum(modelStackWithNoteRow, drum, velocity, zeroMPEValues);
				}
				else {
					((Kit*)instrument)->endAuditioningForDrum(modelStackWithNoteRow, drum);
				}
			}
		}
	}
	else {
		int yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);

		if (on) {
			((MelodicInstrument*)instrument)
			    ->beginAuditioningForNote(modelStack, yNote, velocity, zeroMPEValues, MIDI_CHANNEL_NONE,
			                              sampleSyncLength);
		}
		else {
			((MelodicInstrument*)instrument)->endAuditioningForNote(modelStack, yNote);
		}
	}
}

uint8_t InstrumentClipView::getVelocityForAudition(uint8_t yDisplay, uint32_t* sampleSyncLength) {
	int numInstances = 0;
	unsigned int sum = 0;
	*sampleSyncLength = 0;
	if (auditionPadIsPressed[yDisplay] && !auditioningSilently) {
		sum += ((Instrument*)currentSong->currentClip->output)->defaultVelocity;
		numInstances++;
	}
	if (!playbackHandler.playbackState && numEditPadPressesPerNoteRowOnScreen[yDisplay] > 0) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		if (makeCurrentClipActiveOnInstrumentIfPossible(modelStack)) { // Should always be true, cos playback is stopped

			for (int i = 0; i < editPadPressBufferSize; i++) {
				if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay) {
					sum += editPadPresses[i].intendedVelocity;
					numInstances++;
					*sampleSyncLength = editPadPresses[i].intendedLength;
				}
			}
		}
	}

	if (numInstances == 0) return 255;
	return sum / numInstances;
}

uint8_t InstrumentClipView::getNumNoteRowsAuditioning() {
	uint8_t num = 0;
	for (int i = 0; i < displayHeight; i++) {
		if (auditionPadIsPressed[i]) {
			num++;
		}
	}
	return num;
}

uint8_t InstrumentClipView::oneNoteAuditioning() {
	return (currentUIMode == UI_MODE_AUDITIONING && getNumNoteRowsAuditioning() == 1);
}

void InstrumentClipView::offsetNoteCodeAction(int newOffset) {

	actionLogger.deleteAllLogs(); // Can't undo past this!

	uint8_t yVisualWithinOctave;

	// If in scale mode, need to check whether we're allowed to change scale..
	if (getCurrentClip()->isScaleModeClip()) {
		newOffset = getMax(-1, getMin(1, newOffset));
		yVisualWithinOctave = getYVisualWithinOctaveFromYDisplay(lastAuditionedYDisplay);

		// If not allowed to move, blink the scale mode button to remind the user that that's why
		if (!currentSong->mayMoveModeNote(yVisualWithinOctave, newOffset)) {
			IndicatorLEDs::indicateAlertOnLed(scaleModeLedX, scaleModeLedY);
			int noteCode = getCurrentClip()->getYNoteFromYDisplay(lastAuditionedYDisplay, currentSong);
			drawActualNoteCode(noteCode); // Draw it again so that blinking stops temporarily
			return;
		}
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	ModelStackWithNoteRow* modelStackWithNoteRow = getOrCreateNoteRowForYDisplay(modelStack, lastAuditionedYDisplay);

	NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

	// If we're in Kit mode, the NoteRow will exist, or else we wouldn't be auditioning it. But if in other mode, we need to do this
	if (!noteRow) return; // Get out if NoteRow doesn't exist and can't be created

	// Stop current note-sound from the NoteRow in question
	if (playbackHandler.isEitherClockActive()) {
		noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);
	}

	// Stop the auditioning
	auditionPadIsPressed[lastAuditionedYDisplay] = false;
	reassessAuditionStatus(lastAuditionedYDisplay);

	if (currentSong->currentClip->output->type != INSTRUMENT_TYPE_KIT) {
		// If in scale mode, edit the scale
		if (getCurrentClip()->inScaleMode) {
			currentSong->changeMusicalMode(yVisualWithinOctave, newOffset);
			// If we're shifting the root note, compensate scrolling
			if (yVisualWithinOctave == 0) getCurrentClip()->yScroll += newOffset;
			recalculateColour(lastAuditionedYDisplay); // Colour will have changed slightly

doRenderRow:
			uiNeedsRendering(this, 1 << lastAuditionedYDisplay, 0);
		}

		// Otherwise, can't do anything - give error
		else {
			IndicatorLEDs::indicateAlertOnLed(scaleModeLedX, scaleModeLedY);
		}
	}

	// Switch Drums, if we're in Kit mode
	else {
		Drum* oldDrum = noteRow->drum;
		Drum* newDrum = flipThroughAvailableDrums(newOffset, oldDrum);

		if (oldDrum) oldDrum->drumWontBeRenderedForAWhile();

		noteRow->setDrum(newDrum, (Kit*)currentSong->currentClip->output, modelStackWithNoteRow);
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
		setSelectedDrum(newDrum, true);
		goto doRenderRow;
	}

	// Restart the auditioning
	auditionPadIsPressed[lastAuditionedYDisplay] = true;
	reassessAuditionStatus(lastAuditionedYDisplay);

	// Redraw the NoteCode
	drawNoteCode(lastAuditionedYDisplay);

	uiNeedsRendering(this, 0, 1 << lastAuditionedYDisplay);
}

Drum* InstrumentClipView::flipThroughAvailableDrums(int newOffset, Drum* drum, bool mayBeNone) {

	Drum* startedAtDrum = drum;
	Drum* newDrum = startedAtDrum;

	if (newOffset >= 0) {
		while (true) {
			newDrum = getNextDrum(newDrum, mayBeNone);
			// Keep going until we get back to where we started, or we're on "none" or "new", or we find an unused Drum.
			if (newDrum == startedAtDrum || newDrum == NULL || newDrum == (Drum*)0xFFFFFFFF
			    || !getCurrentClip()->getNoteRowForDrum(newDrum))
				break;
		}
	}
	else {
		Drum* lookAheadDrum = startedAtDrum;

		while (true) {
			lookAheadDrum = getNextDrum(lookAheadDrum, mayBeNone);
			// Keep going until we get back to where we started
			if (lookAheadDrum == startedAtDrum) break;

			if (lookAheadDrum == NULL || lookAheadDrum == (Drum*)0xFFFFFFFF
			    || !getCurrentClip()->getNoteRowForDrum(lookAheadDrum))
				newDrum = lookAheadDrum;
		}
	}
	return newDrum;
}

Drum* InstrumentClipView::getNextDrum(Drum* oldDrum, bool mayBeNone) {
	if (oldDrum == NULL) {
		Drum* newDrum = ((Kit*)currentSong->currentClip->output)->firstDrum;
		/*
    	if (newDrum == NULL) {
    		newDrum = (Drum*)0xFFFFFFFF;
    	}
    	*/
		return newDrum;
	}
	//if (oldDrum == (Drum*)0xFFFFFFFF) return NULL;

	Drum* nextDrum = ((SoundDrum*)oldDrum)->next;
	//if (!nextDrum) nextDrum = (Drum*)0xFFFFFFFF;
	return nextDrum;
}

int InstrumentClipView::getYVisualFromYDisplay(int yDisplay) {
	return yDisplay + getCurrentClip()->yScroll;
}

int InstrumentClipView::getYVisualWithinOctaveFromYDisplay(int yDisplay) {
	int yVisual = getYVisualFromYDisplay(yDisplay);
	int yVisualRelativeToRoot = yVisual - currentSong->rootNote;
	int yVisualWithinOctave = yVisualRelativeToRoot % currentSong->numModeNotes;
	if (yVisualWithinOctave < 0) yVisualWithinOctave += currentSong->numModeNotes;
	return yVisualWithinOctave;
}

// Beware - supplying shouldRedrawStuff as false will cause the activeModControllable to *not* update! Probably never should do this anymore...
void InstrumentClipView::setSelectedDrum(Drum* drum, bool shouldRedrawStuff) {

	if (getCurrentUI() != &soundEditor && getCurrentUI() != &sampleBrowser && getCurrentUI() != &sampleMarkerEditor
	    && getCurrentUI() != &renameDrumUI) {

		((Kit*)currentSong->currentClip->output)->selectedDrum = drum;

		if (shouldRedrawStuff) {
			view.setActiveModControllableTimelineCounter(
			    currentSong->currentClip); // Do a redraw. Obviously the Clip is the same
		}
	}

	if (shouldRedrawStuff) {
		renderingNeededRegardlessOfUI(0, 0xFFFFFFFF);
	}
}

void InstrumentClipView::auditionPadAction(int velocity, int yDisplay, bool shiftButtonDown) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	Instrument* instrument = (Instrument*)currentSong->currentClip->output;

	bool isKit = (instrument->type == INSTRUMENT_TYPE_KIT);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
	    modelStack->addTimelineCounter(currentSong->currentClip);
	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
	    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStackWithTimelineCounter);

	Drum* drum = NULL;

	// If Kit...
	if (isKit) {

		if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
			drum = modelStackWithNoteRowOnCurrentClip->getNoteRow()->drum;
		}

		// If NoteRow doesn't exist here, we'll see about creating one
		else {

			// But not if we're actually not on this screen
			if (getCurrentUI() != this) return;

			// Press-down
			if (velocity) {

				setSelectedDrum(NULL);

				if (currentUIMode == UI_MODE_NONE) {
					currentUIMode = UI_MODE_ADDING_DRUM_NOTEROW;
					fileBrowserShouldNotPreview = shiftButtonDown;

					drumForNewNoteRow = NULL; //(Drum*)0xFFFFFFFF;
					//newDrumOptionSelected = true;
					drawDrumName(drumForNewNoteRow);

					// Remember what NoteRow was pressed - and limit to being no further than 1 above or 1 below the existing NoteRows
					yDisplayOfNewNoteRow = yDisplay;
					yDisplayOfNewNoteRow = getMax((int)yDisplayOfNewNoteRow, (int)-1 - getCurrentClip()->yScroll);
					int maximum = getCurrentClip()->getNumNoteRows() - getCurrentClip()->yScroll;
					yDisplayOfNewNoteRow = getMin((int)yDisplayOfNewNoteRow, maximum);

					goto justReRender;
				}
			}

			// Press-up
			else {
				if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {
					currentUIMode = UI_MODE_NONE;

					// If the user didn't select "none"...
					if (drumForNewNoteRow) {

						// Make a new NoteRow
						int noteRowIndex;
						NoteRow* newNoteRow =
						    createNewNoteRowForKit(modelStackWithTimelineCounter, yDisplayOfNewNoteRow, &noteRowIndex);
						if (newNoteRow) {
							uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);

							ModelStackWithNoteRow* modelStackWithNoteRow =
							    modelStackWithTimelineCounter->addNoteRow(noteRowIndex, newNoteRow);
							newNoteRow->setDrum(drumForNewNoteRow, (Kit*)instrument, modelStackWithNoteRow);
							AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
						}
					}
#if HAVE_OLED
					OLED::removePopup();
#else
					redrawNumericDisplay();
#endif
justReRender:
					uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);
				}
			}

			goto getOut;
		}
	}

	// Or if synth
	else if (instrument->type == INSTRUMENT_TYPE_SYNTH) {
		if (velocity) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &multiRangeMenu) {
				multiRangeMenu.noteOnToChangeRange(getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong)
				                                   + ((SoundInstrument*)instrument)->transpose);
			}
		}
	}

	// Recording - only allowed if currentClip is activeClip
	if (clipIsActiveOnInstrument && playbackHandler.shouldRecordNotesNow()
	    && currentSong->isClipActive(currentSong->currentClip)) {

		// Note-on
		if (velocity) {

			// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
			// This is basic. For MIDI input, we do this in a couple more cases - see noteMessageReceived()
			// in MelodicInstrument and Kit
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) {
				if (isKit) {
					if (drum) {
						drum->recordNoteOnEarly(instrument->defaultVelocity,
						                        getCurrentClip()->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
					}
				}
				else {
					int yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					((MelodicInstrument*)instrument)
					    ->earlyNotes.insertElementIfNonePresent(
					        yNote, instrument->defaultVelocity,
					        getCurrentClip()->allowNoteTails(
					            modelStackWithNoteRowOnCurrentClip)); // NoteRow is allowed to be NULL in this case.
				}
			}

			else {

				// May need to create NoteRow if there wasn't one previously
				if (!modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
					modelStackWithNoteRowOnCurrentClip =
					    createNoteRowForYDisplay(modelStackWithTimelineCounter, yDisplay);
				}

				if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
					getCurrentClip()->recordNoteOn(modelStackWithNoteRowOnCurrentClip, instrument->defaultVelocity);
					goto maybeRenderRow;
				}
			}
		}

		// Note-off
		else {

			if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
				getCurrentClip()->recordNoteOff(modelStackWithNoteRowOnCurrentClip);
maybeRenderRow:
				if (!(currentUIMode & UI_MODE_HORIZONTAL_SCROLL)) // What about zoom too?
					uiNeedsRendering(this, 1 << yDisplay, 0);
			}
		}
	}

	{
		NoteRow* noteRowOnActiveClip;

		if (clipIsActiveOnInstrument) {
			noteRowOnActiveClip = modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull();
		}

		else {
			// Kit
			if (instrument->type == INSTRUMENT_TYPE_KIT) {
				noteRowOnActiveClip = ((InstrumentClip*)instrument->activeClip)->getNoteRowForDrum(drum);
			}

			// Non-kit
			else {
				int yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
				noteRowOnActiveClip = ((InstrumentClip*)instrument->activeClip)->getNoteRowForYNote(yNote);
			}
		}

		// If note on...
		if (velocity) {
			int velocityToSound = ((Instrument*)currentSong->currentClip->output)->defaultVelocity;

			auditionPadIsPressed[yDisplay] =
			    velocityToSound; // Yup, need to do this even if we're going to do a "silent" audition, so pad lights up etc.

			if (noteRowOnActiveClip) {
				// Ensure our auditioning doesn't override a note playing in the sequence
				if (playbackHandler.isEitherClockActive()
				    && noteRowOnActiveClip->soundingStatus == STATUS_SEQUENCED_NOTE) {
					goto doSilentAudition;
				}
			}

			// If won't be actually sounding Instrument...
			if (shiftButtonDown || Buttons::isButtonPressed(yEncButtonX, yEncButtonY)) {

				fileBrowserShouldNotPreview = true;
doSilentAudition:
				auditioningSilently = true;
				reassessAllAuditionStatus();
			}
			else {
				if (!auditioningSilently) {

					fileBrowserShouldNotPreview = false;

					sendAuditionNote(true, yDisplay, velocityToSound, 0);

					{ lastAuditionedVelocityOnScreen[yDisplay] = velocityToSound; }
				}
			}

			// If wasn't already auditioning...
			if (!isUIModeActive(UI_MODE_AUDITIONING)) {
				shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				editedAnyPerNoteRowStuffSinceAuditioningBegan = false;
				enterUIMode(UI_MODE_AUDITIONING);
			}

			drawNoteCode(yDisplay);
			lastAuditionedYDisplay = yDisplay;

			// Begin resampling / output-recording
			if (Buttons::isButtonPressed(recordButtonX, recordButtonY) && !audioRecorder.recordingSource) {
				audioRecorder.beginOutputRecording();
				Buttons::recordButtonPressUsedUp = true;
			}

			if (isKit) {
				setSelectedDrum(drum);
				goto getOut; // No need to redraw any squares, because setSelectedDrum() has done it
			}
		}

		// Or if auditioning this NoteRow just finished...
		else {
			if (auditionPadIsPressed[yDisplay]) {
				auditionPadIsPressed[yDisplay] = 0;
				lastAuditionedVelocityOnScreen[yDisplay] = 255;

				// Stop the note sounding - but only if a sequenced note isn't in fact being played here.
				if (!noteRowOnActiveClip || noteRowOnActiveClip->soundingStatus == STATUS_OFF) {
					sendAuditionNote(false, yDisplay, 64, 0);
				}
			}
			numericDriver.cancelPopup();   // In case euclidean stuff was being edited etc
			someAuditioningHasEnded(true); //lastAuditionedYDisplay == yDisplay);
			actionLogger.closeAction(ACTION_EUCLIDEAN_NUM_EVENTS_EDIT);
			actionLogger.closeAction(ACTION_NOTEROW_ROTATE);
		}

		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
	}

getOut:

	// This has to happen after setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		IndicatorLEDs::indicateAlertOnLed(sessionViewLedX, sessionViewLedY);
	}
}

void InstrumentClipView::cancelAllAuditioning() {
	if (isUIModeActive(UI_MODE_AUDITIONING)) {
		memset(auditionPadIsPressed, 0, sizeof(auditionPadIsPressed));
		reassessAllAuditionStatus();
		exitUIMode(UI_MODE_AUDITIONING);
		uiNeedsRendering(this, 0, 0xFFFFFFFF);
	}
}

void InstrumentClipView::enterDrumCreator(ModelStackWithNoteRow* modelStack, bool doRecording) {

	Uart::println("enterDrumCreator");

	char* prefix;
	String soundName;

	if (doRecording) prefix = "TEM"; // Means "temp". Actual "REC" name is set in audioRecorder
	else prefix = "U";

	soundName.set(prefix);

	Kit* kit = (Kit*)modelStack->song->currentClip->output;

	int error = kit->makeDrumNameUnique(&soundName, 1);
	if (error) {
doDisplayError:
		numericDriver.displayError(error);
		return;
	}

	void* memory = generalMemoryAllocator.alloc(sizeof(SoundDrum), NULL, false, true);
	if (!memory) {
		error = ERROR_INSUFFICIENT_RAM;
		goto doDisplayError;
	}

	ParamManagerForTimeline paramManager;
	error = paramManager.setupWithPatching();
	if (error) {
		generalMemoryAllocator.dealloc(memory);
		goto doDisplayError;
	}

	Sound::initParams(&paramManager);
	SoundDrum* newDrum = new (memory) SoundDrum();
	newDrum->setupAsSample(&paramManager);

	modelStack->song->backUpParamManager(newDrum, modelStack->song->currentClip, &paramManager, true);

	newDrum->name.set(&soundName);
	newDrum->nameIsDiscardable = true;

	kit->addDrum(newDrum);
	modelStack->getNoteRow()->setDrum(newDrum, kit,
	                                  modelStack); // Sets noteRow->paramManager to newDrum->backedUpParamManager

	kit->beenEdited();

	setSelectedDrum(newDrum); // Does this really need to render?

	bool success =
	    soundEditor.setup(getCurrentClip(), &fileSelectorMenu, 0); // Can't fail because we just set the selected Drum
	// TODO: what if fail because no RAM

	if (doRecording) {
		success = openUI(&audioRecorder);
		if (success) {
			audioRecorder.process();
		}
	}
	else {
		success = openUI(&sampleBrowser);
		if (success) {
			PadLEDs::
			    skipGreyoutFade(); // Greyout can't be done at same time as horizontal scroll, which is now happening probably.
			PadLEDs::sendOutSidebarColoursSoon();
		}
	}

	if (!success) {
		openUI(&soundEditor);
	}
}

void InstrumentClipView::deleteDrum(SoundDrum* drum) {

	Kit* kit = (Kit*)currentSong->currentClip->output;

	kit->removeDrum(drum);

	// Find Drum's NoteRow
	int noteRowIndex;
	NoteRow* noteRow = getCurrentClip()->getNoteRowForDrum(drum, &noteRowIndex);
	if (noteRow) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);

		// Give NoteRow another unassigned Drum, or no Drum if there are none
		noteRow->setDrum(kit->getFirstUnassignedDrum(getCurrentClip()), kit, modelStackWithNoteRow);

		if (!noteRow->drum) {
			// If NoteRow has no Notes, just delete it - if it's not the last one
			if (noteRow->hasNoNotes() && getCurrentClip()->getNumNoteRows() > 1) {
				if (noteRowIndex == 0) {
					getCurrentClip()->yScroll--;
				}

				getCurrentClip()->deleteNoteRow(modelStack, noteRowIndex);
			}
		}
	}

	// Delete the Drum we came here to delete
	currentSong->deleteBackedUpParamManagersForModControllable(drum);
	void* toDealloc = dynamic_cast<void*>(drum);
	drum->~SoundDrum();
	generalMemoryAllocator.dealloc(toDealloc);

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;

	// We should repopulateNoteRowsOnscreen() and everything, but this will only be called just before the UI sessions starts again anyway
}

void InstrumentClipView::someAuditioningHasEnded(bool recalculateLastAuditionedNoteOnScreen) {
	// Try to find another auditioned NoteRow so we can show its name etc
	int i;
	for (i = 0; i < displayHeight; i++) {
		if (auditionPadIsPressed[i]) {
			// Show this note's noteCode, if the noteCode we were showing before is the note we just stopped auditioning
			if (recalculateLastAuditionedNoteOnScreen) {
				drawNoteCode(i);
				lastAuditionedYDisplay = i;
			}
			break;
		}
	}

	// Or, if all auditioning now finished...
	if (i == displayHeight) {
		exitUIMode(UI_MODE_AUDITIONING);
		auditioningSilently = false;

#if HAVE_OLED
		OLED::removePopup();
#else
		redrawNumericDisplay();
#endif
	}
}

void InstrumentClipView::drawNoteCode(uint8_t yDisplay) {
	// Might not want to actually do this...
	if (!getCurrentUI()->toClipMinder()) return;

	if (currentSong->currentClip->output->type != INSTRUMENT_TYPE_KIT) {
		drawActualNoteCode(getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong));
	}
	else {
		drawDrumName(getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong)->drum);
	}
}

void InstrumentClipView::drawDrumName(Drum* drum, bool justPopUp) {

	char const* newText;

#if HAVE_OLED
	char buffer[30];

	if (!drum) {
		newText = "No sound";
	}
	else if (drum->type == DRUM_TYPE_SOUND) {
		newText = ((SoundDrum*)drum)->name.get();
	}
	else {
		newText = buffer;

		if (drum->type == DRUM_TYPE_GATE) {

			strcpy(buffer, "Gate channel ");
			intToString(((GateDrum*)drum)->channel + 1, &buffer[13]);
			IndicatorLEDs::blinkLed(cvLedX, cvLedY, 1, 1);
		}
		else { // MIDI
			strcpy(buffer, "MIDI channel ");
			intToString(((MIDIDrum*)drum)->channel + 1, &buffer[13]);
			strcat(buffer, ", note ");
			char* pos = strchr(buffer, 0);
			intToString(((MIDIDrum*)drum)->note, pos);

			IndicatorLEDs::blinkLed(midiLedX, midiLedY, 1, 1);
		}
	}

	OLED::popupText(newText, true);
#else

	char buffer[7];

	if (!drum) {
		newText = "NONE";

basicDisplay:
		if (justPopUp && currentUIMode != UI_MODE_AUDITIONING) numericDriver.displayPopup(newText);
		else numericDriver.setText(newText, false, 255, true);
	}
	else {
		if (drum->type != DRUM_TYPE_SOUND) {
			drum->getName(buffer);
			newText = buffer;

			if (drum->type == DRUM_TYPE_MIDI) IndicatorLEDs::blinkLed(midiLedX, midiLedY, 1, 1);
			else if (drum->type == DRUM_TYPE_GATE) IndicatorLEDs::blinkLed(cvLedX, cvLedY, 1, 1);

			goto basicDisplay;
		}

		// If we're here, it's a SoundDrum
		SoundDrum* soundDrum = (SoundDrum*)drum;

		newText = soundDrum->name.get();
		bool andAHalf;

		if (numericDriver.getEncodedPosFromLeft(99999, newText, &andAHalf) <= NUMERIC_DISPLAY_LENGTH) {
			goto basicDisplay;
		}
		numericDriver.setScrollingText(newText, 0, initialFlashTime + flashTime);
	}
#endif
}

int InstrumentClipView::setupForEnteringScaleMode(int newRootNote, int yDisplay) {
	// Having got to this function, we have recently calculated the default root note

	uiTimerManager.unsetTimer(TIMER_DEFAULT_ROOT_NOTE);
	int scrollAdjust = 0;
	uint8_t pinAnimationToYDisplay;
	uint8_t pinAnimationToYNote;

	// If user manually selected what root note they want, then we've got it easy!
	if (newRootNote != 2147483647) {
		pinAnimationToYDisplay = yDisplay;
		pinAnimationToYNote = getCurrentClip()->getYNoteFromYDisplay(
		    yDisplay, currentSong); // This is needed in case we're coming from Keyboard Screen
	}

	// Otherwise, go with the previously calculated default root note
	else {
		newRootNote = defaultRootNote;

		// If there's a root-note (or its octave) currently onscreen, pin animation to that
		for (int i = 0; i < displayHeight; i++) {
			int thisNote = getCurrentClip()->getYNoteFromYDisplay(i, currentSong);
			// If it's the root note...
			if ((int)std::abs(newRootNote - thisNote) % 12 == 0) {
				pinAnimationToYDisplay = i;
				pinAnimationToYNote = thisNote;
				goto doneLookingForRootNoteOnScreen;
			}
		}

		// Or if there wasn't an instance of the root note onscreen..
		pinAnimationToYDisplay = 2;
		pinAnimationToYNote = getCurrentClip()->getYNoteFromYDisplay(pinAnimationToYDisplay, currentSong);
	}

doneLookingForRootNoteOnScreen:

	// Need to figure out the scale first...
	getCurrentClip()->inScaleMode = true;
	currentSong->setRootNote(newRootNote, getCurrentClip()); // Computation to find out what notes in scale

	int yVisual = getCurrentClip()->getYVisualFromYNote(pinAnimationToYNote, currentSong);

	int newScroll = yVisual - pinAnimationToYDisplay;

	getCurrentClip()->deleteOldDrumNames();

	return newScroll;
}

void InstrumentClipView::enterScaleMode(uint8_t yDisplay) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	int newRootNote;
	if (yDisplay == 255) newRootNote = 2147483647;
	else newRootNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);

	int newScroll = setupForEnteringScaleMode(newRootNote, yDisplay);

	// See which NoteRows need to animate
	PadLEDs::numAnimatedRows = 0;
	for (int i = 0; i < clip->noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = clip->noteRows.getElement(i);
		int yVisualTo = clip->getYVisualFromYNote(thisNoteRow->y, currentSong);
		int yDisplayTo = yVisualTo - newScroll;
		int yDisplayFrom = thisNoteRow->y - clip->yScroll;

		// If this NoteRow is going to end up on-screen or come from on-screen...
		if ((yDisplayTo >= 0 && yDisplayTo < displayHeight) || (yDisplayFrom >= 0 && yDisplayFrom < displayHeight)) {

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(thisNoteRow->y, thisNoteRow);

			PadLEDs::animatedRowGoingTo[PadLEDs::numAnimatedRows] = yDisplayTo;
			PadLEDs::animatedRowGoingFrom[PadLEDs::numAnimatedRows] = yDisplayFrom;
			uint8_t mainColour[3];
			uint8_t tailColour[3];
			uint8_t blurColour[3];
			clip->getMainColourFromY(thisNoteRow->y, thisNoteRow->getColourOffset(clip), mainColour);
			getTailColour(tailColour, mainColour);
			getBlurColour(blurColour, mainColour);

			thisNoteRow->renderRow(
			    this, mainColour, tailColour, blurColour, &PadLEDs::imageStore[PadLEDs::numAnimatedRows][0][0],
			    PadLEDs::occupancyMaskStore[PadLEDs::numAnimatedRows], true, modelStackWithNoteRow->getLoopLength(),
			    clip->allowNoteTails(modelStackWithNoteRow), displayWidth, currentSong->xScroll[NAVIGATION_CLIP],
			    currentSong->xZoom[NAVIGATION_CLIP]);
			drawMuteSquare(thisNoteRow, PadLEDs::imageStore[PadLEDs::numAnimatedRows],
			               PadLEDs::occupancyMaskStore[PadLEDs::numAnimatedRows]);
			PadLEDs::numAnimatedRows++;
			if (PadLEDs::numAnimatedRows >= MAX_NUM_ANIMATED_ROWS) break;
		}
	}

	PadLEDs::setupInstrumentClipCollapseAnimation(false);

	clip->yScroll = newScroll;

	displayCurrentScaleName();

	// And tidy up
	recalculateColours();
	currentUIMode = UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING;
	PadLEDs::recordTransitionBegin(noteRowCollapseSpeed);
	setLedStates();

	//drawAllAuditionSquares(false);

	PadLEDs::renderNoteRowExpandOrCollapse();
}

int InstrumentClipView::setupForExitingScaleMode() {

	int scrollAdjust;
	// See if there's a root note onscreen
	bool foundRootNoteOnScreen = false;
	for (int i = 0; i < displayHeight; i++) {
		int yNote = getCurrentClip()->getYNoteFromYDisplay(i, currentSong);
		// If it's the root note...
		if ((int)std::abs(currentSong->rootNote - yNote) % 12 == 0) {
			scrollAdjust = yNote - i - getCurrentClip()->yScroll;
			foundRootNoteOnScreen = true;
			break;
		}
	}

	// Or if there wasn't an instance of the root note onscreen..
	if (!foundRootNoteOnScreen)
		scrollAdjust = getCurrentClip()->getYNoteFromYVisual(getCurrentClip()->yScroll + 1, currentSong) - 1
		               - getCurrentClip()->yScroll;

	getCurrentClip()->inScaleMode = false;

	getCurrentClip()->deleteOldDrumNames();

	return scrollAdjust;
}

void InstrumentClipView::exitScaleMode() {

	int scrollAdjust = setupForExitingScaleMode();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	// See which NoteRows need to animate
	PadLEDs::numAnimatedRows = 0;
	for (int i = 0; i < clip->noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = clip->noteRows.getElement(i);
		int yDisplayTo = thisNoteRow->y - (clip->yScroll + scrollAdjust);
		clip->inScaleMode = true;
		int yDisplayFrom = clip->getYVisualFromYNote(thisNoteRow->y, currentSong) - clip->yScroll;
		clip->inScaleMode = false;

		// If this NoteRow is going to end up on-screen or come from on-screen...
		if ((yDisplayTo >= 0 && yDisplayTo < displayHeight) || (yDisplayFrom >= 0 && yDisplayFrom < displayHeight)) {
			PadLEDs::animatedRowGoingTo[PadLEDs::numAnimatedRows] = yDisplayTo;
			PadLEDs::animatedRowGoingFrom[PadLEDs::numAnimatedRows] = yDisplayFrom;
			uint8_t mainColour[3];
			uint8_t tailColour[3];
			uint8_t blurColour[3];
			clip->getMainColourFromY(thisNoteRow->y, thisNoteRow->getColourOffset(clip), mainColour);
			getTailColour(tailColour, mainColour);
			getBlurColour(blurColour, mainColour);

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(thisNoteRow->y, thisNoteRow);

			thisNoteRow->renderRow(
			    this, mainColour, tailColour, blurColour, &PadLEDs::imageStore[PadLEDs::numAnimatedRows][0][0],
			    PadLEDs::occupancyMaskStore[PadLEDs::numAnimatedRows], true, modelStackWithNoteRow->getLoopLength(),
			    clip->allowNoteTails(modelStackWithNoteRow), displayWidth, currentSong->xScroll[NAVIGATION_CLIP],
			    currentSong->xZoom[NAVIGATION_CLIP]);
			drawMuteSquare(thisNoteRow, PadLEDs::imageStore[PadLEDs::numAnimatedRows],
			               PadLEDs::occupancyMaskStore[PadLEDs::numAnimatedRows]);
			PadLEDs::numAnimatedRows++;
			if (PadLEDs::numAnimatedRows >= MAX_NUM_ANIMATED_ROWS) break;
		}
	}

	clip->yScroll += scrollAdjust;

	PadLEDs::setupInstrumentClipCollapseAnimation(false);

	recalculateColours();
	currentUIMode = UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING;
	PadLEDs::recordTransitionBegin(noteRowCollapseSpeed);
	setLedStates();
	PadLEDs::renderNoteRowExpandOrCollapse();
}

// If called from KeyboardScreen, the newRootNote won't correspond to the yDisplay, and that's ok
void InstrumentClipView::setupChangingOfRootNote(int newRootNote, int yDisplay) {
	int oldYVisual = getYVisualFromYDisplay(yDisplay);
	int yNote = getCurrentClip()->getYNoteFromYVisual(oldYVisual, currentSong);
	currentSong->setRootNote(newRootNote, getCurrentClip()); // Computation to find out what scale etc

	int newYVisual = getCurrentClip()->getYVisualFromYNote(yNote, currentSong);
	int scrollChange = newYVisual - oldYVisual;
	getCurrentClip()->yScroll += scrollChange;
}

void InstrumentClipView::changeRootNote(uint8_t yDisplay) {

	int oldYVisual = getYVisualFromYDisplay(yDisplay);
	int newRootNote = getCurrentClip()->getYNoteFromYVisual(oldYVisual, currentSong);

	setupChangingOfRootNote(newRootNote, yDisplay);
	displayCurrentScaleName();

	recalculateColours();
	uiNeedsRendering(this);
}

bool InstrumentClipView::renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                       uint8_t occupancyMask[][displayWidth + sideBarWidth]) {
	if (!image) return true;

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) return true;

	for (int i = 0; i < displayHeight; i++) {
		if (whichRows & (1 << i)) {
			drawMuteSquare(getCurrentClip()->getNoteRowOnScreen(i, currentSong), image[i], occupancyMask[i]);
			drawAuditionSquare(i, image[i]);
		}
	}

	return true;
}

void InstrumentClipView::drawMuteSquare(NoteRow* thisNoteRow, uint8_t thisImage[][3], uint8_t thisOccupancyMask[]) {
	uint8_t* thisColour = thisImage[displayWidth];
	uint8_t* thisOccupancy = &(thisOccupancyMask[displayWidth]);

	// If user assigning MIDI controls and this NoteRow has a command assigned, flash pink
	if (view.midiLearnFlashOn && thisNoteRow && thisNoteRow->drum
	    && thisNoteRow->drum->muteMIDICommand.containsSomething()) {
		thisColour[0] = midiCommandColourRed;
		thisColour[1] = midiCommandColourGreen;
		thisColour[2] = midiCommandColourBlue;
		*thisOccupancy = 64;
	}

	else if (thisNoteRow == NULL || !thisNoteRow->muted) {
		if (thisNoteRow == NULL && currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
			memset(thisColour, 0, 3);
		}
		else {
			activeColourMenu.getRGB(thisColour);
		}
	}
	else {
		mutedColourMenu.getRGB(thisColour);
		*thisOccupancy = 64;
	}

	// If user assigning MIDI controls and has this Clip selected, flash to half brightness
	if (view.midiLearnFlashOn && thisNoteRow != NULL && view.thingPressedForMidiLearn == MIDI_LEARN_NOTEROW_MUTE
	    && thisNoteRow->drum && &thisNoteRow->drum->muteMIDICommand == view.learnedThing) {
		thisColour[0] >>= 1;
		thisColour[1] >>= 1;
		thisColour[2] >>= 1;
		*thisOccupancy = 64;
	}
}

bool InstrumentClipView::isRowAuditionedByInstrument(int yDisplay) {
	if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
		NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
		if (!noteRow || !noteRow->drum) return false;
		return noteRow->drum->auditioned;
	}
	else {
		int note = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
		return (((MelodicInstrument*)currentSong->currentClip->output)->isNoteAuditioning(note));
	}
}

void InstrumentClipView::drawAuditionSquare(uint8_t yDisplay, uint8_t thisImage[][3]) {
	uint8_t* thisColour = thisImage[displayWidth + 1];

	if (view.midiLearnFlashOn) {
		NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);

		bool midiCommandAssigned;
		if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
			midiCommandAssigned = (noteRow && noteRow->drum && noteRow->drum->midiInput.containsSomething());
		}
		else
			midiCommandAssigned =
			    (((MelodicInstrument*)currentSong->currentClip->output)->midiInput.containsSomething());

		// If MIDI command already assigned...
		if (midiCommandAssigned) {
			thisColour[0] = midiCommandColourRed;
			thisColour[1] = midiCommandColourGreen;
			thisColour[2] = midiCommandColourBlue;
		}

		// Or if not assigned but we're holding it down...
		else {
			bool holdingDown = false;
			if (view.thingPressedForMidiLearn == MIDI_LEARN_MELODIC_INSTRUMENT_INPUT) {
				holdingDown = true;
			}
			else if (view.thingPressedForMidiLearn == MIDI_LEARN_DRUM_INPUT) {
				holdingDown = (&noteRow->drum->midiInput == view.learnedThing);
			}

			if (holdingDown) {
				memcpy(thisColour, rowColour[yDisplay], 3);
				thisColour[0] >>= 1;
				thisColour[1] >>= 1;
				thisColour[2] >>= 1;
			}
			else goto drawNormally;
		}
	}

	// If audition pad pressed...
	else if (auditionPadIsPressed[yDisplay]
	         || (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW && yDisplay == yDisplayOfNewNoteRow)) {
		memcpy(thisColour, rowColour[yDisplay], 3);
		goto checkIfSelectingRanges;
	}

	else {
drawNormally:

		// Kit - draw "selected Drum"
		if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
			NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
			if (noteRow != NULL && noteRow->drum != NULL
			    && noteRow->drum == ((Kit*)currentSong->currentClip->output)->selectedDrum) {

				int totalColour =
				    (uint16_t)rowColour[yDisplay][0] + rowColour[yDisplay][1] + rowColour[yDisplay][2]; // max 765

				for (int colour = 0; colour < 3; colour++) {
					thisColour[colour] = ((int32_t)rowColour[yDisplay][colour] * (8421504 - 6500000)
					                      + ((int32_t)totalColour * (6500000 >> 5)))
					                     >> 23;
				}
				return;
			}
		}

		// Not kit
		else {

			if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
				if (flashDefaultRootNoteOn) {
					int yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					if ((uint16_t)(yNote - defaultRootNote + 120) % (uint8_t)12 == 0) {
						memcpy(thisColour, rowColour[yDisplay], 3);
						return;
					}
				}
			}
			else {

				{
					// If this is the root note, indicate
					int yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					if ((uint16_t)(yNote - currentSong->rootNote + 120) % (uint8_t)12 == 0) {
						memcpy(thisColour, rowColour[yDisplay], 3);
					}
					else {
						memset(thisColour, 0, 3);
					}
				}

checkIfSelectingRanges:
				// If we're selecting ranges...
				if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &audioRecorder
				    || (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem()->isRangeDependent())) {
					int yNote = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					if (soundEditor.isUntransposedNoteWithinRange(yNote)) {
						for (int colour = 0; colour < 3; colour++) {
							int value = (int)thisColour[colour] + 30;
							thisColour[colour] = getMin(value, 255);
						}
					}
				}

				return;
			}
		}
		memset(thisColour, 0, 3);
	}
}

void InstrumentClipView::cutAuditionedNotesToOne() {
	uint32_t whichRowsNeedReRendering = 0;

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		if (yDisplay != lastAuditionedYDisplay && auditionPadIsPressed[yDisplay]) {
			auditionPadIsPressed[yDisplay] = false;

			getCurrentClip()->yDisplayNoLongerAuditioning(yDisplay, currentSong);

			whichRowsNeedReRendering |= (1 << yDisplay);
		}
	}
	reassessAllAuditionStatus();
	if (whichRowsNeedReRendering) uiNeedsRendering(this, 0, whichRowsNeedReRendering);
}

static const uint32_t verticalScrollUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN,
                                                 UI_MODE_DRAGGING_KIT_NOTEROW, 0};

int InstrumentClipView::verticalEncoderAction(int offset, bool inCardRoutine) {

	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine)
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.

	// If encoder button pressed
	if (Buttons::isButtonPressed(yEncButtonX, yEncButtonY)) {
		// User may be trying to move a noteCode...
		if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
			/*
            if (!Buttons::isShiftButtonPressed()) { // Why'd I mandate that shift not be pressed?
                // If in kit mode, then we can do it
                if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {

                	if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

                    cutAuditionedNotesToOne();
                    return scrollVertical(offset, inCardRoutine, true); // Will delete action log in this case
                }

                // Otherwise, remind the user why they can't
                else {
                    if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_SYNTH) IndicatorLEDs::indicateAlertOnLed(synthLedX, synthLedY);
                    else IndicatorLEDs::indicateAlertOnLed(midiLedX, midiLedY); // MIDI
                }
            }
            */

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    getOrCreateNoteRowForYDisplay(modelStack, lastAuditionedYDisplay);

			editNumEuclideanEvents(modelStackWithNoteRow, offset, lastAuditionedYDisplay);
			shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;
			editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
		}

		// Or note repeat...
		else if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
			editNoteRepeat(offset);
		}

		// If user not wanting to move a noteCode, they want to transpose the key
		else if (!currentUIMode && currentSong->currentClip->output->type != INSTRUMENT_TYPE_KIT) {

			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			actionLogger.deleteAllLogs();

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			// If shift button not pressed, transpose whole octave
			if (!Buttons::isShiftButtonPressed()) {
				offset = getMin((int)1, getMax((int)-1, offset));
				getCurrentClip()->transpose(offset * 12, modelStack);
				if (getCurrentClip()->isScaleModeClip())
					getCurrentClip()->yScroll += offset * (currentSong->numModeNotes - 12);
				//numericDriver.displayPopup("OCTAVE");
			}

			// Otherwise, transpose single semitone
			else {
				// If current Clip not in scale-mode, just do it
				if (!getCurrentClip()->isScaleModeClip()) {
					getCurrentClip()->transpose(offset, modelStack);

					// If there are no scale-mode Clips at all, move the root note along as well - just in case the user wants to go back to scale mode (in which case the "previous" root note would be used to help guess what root note to go with)
					if (!currentSong->anyScaleModeClips()) currentSong->rootNote += offset;
				}

				// Otherwise, got to do all key-mode Clips
				else {
					currentSong->transposeAllScaleModeClips(offset);
				}
				//numericDriver.displayPopup("SEMITONE");
			}
		}
	}

	// Or, if shift key is pressed
	else if (Buttons::isShiftButtonPressed()) {
		uint32_t whichRowsToRender = 0;

		// If NoteRow(s) auditioned, shift its colour (Kits only)
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			if (!shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				if (getCurrentClip()->output->type != INSTRUMENT_TYPE_KIT) goto shiftAllColour;

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
					if (auditionPadIsPressed[yDisplay]) {
						ModelStackWithNoteRow* modelStackWithNoteRow =
						    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);
						NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
						if (noteRow) { // This is fine. If we were in Kit mode, we could only be auditioning if there was a NoteRow already
							noteRow->colourOffset += offset;
							if (noteRow->colourOffset >= 72) noteRow->colourOffset -= 72;
							if (noteRow->colourOffset < 0) noteRow->colourOffset += 72;
							recalculateColour(yDisplay);
							whichRowsToRender |= (1 << yDisplay);
						}
					}
				}
			}
		}

		// Otherwise, adjust whole colour spectrum
		else if (currentUIMode == UI_MODE_NONE) {
shiftAllColour:
			getCurrentClip()->colourOffset += offset;
			recalculateColours();
			whichRowsToRender = 0xFFFFFFFF;
		}

		if (whichRowsToRender) {
			uiNeedsRendering(this, whichRowsToRender, whichRowsToRender);
		}
	}

	// If neither button is pressed, we'll do vertical scrolling
	else {
		if (isUIModeWithinRange(verticalScrollUIModes)) {
			if (!shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress
			    || (!isUIModeActive(UI_MODE_NOTES_PRESSED) && !isUIModeActive(UI_MODE_AUDITIONING))) {
				bool draggingNoteRow = (isUIModeActive(UI_MODE_DRAGGING_KIT_NOTEROW));
				return scrollVertical(offset, inCardRoutine, draggingNoteRow);
			}
		}
	}

	return ACTION_RESULT_DEALT_WITH;
}

static const uint32_t noteNudgeUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

int InstrumentClipView::horizontalEncoderAction(int offset) {

	// If holding down notes
	if (isUIModeActive(UI_MODE_NOTES_PRESSED)) {

		if (!Buttons::isShiftButtonPressed()) {

			// If nothing else held down, adjust velocity
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				if (!shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
					adjustVelocity(offset);
				}
			}

			// Or, if horizontal encoder held down, nudge note
			else if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
			         && isUIModeWithinRange(noteNudgeUIModes)) {
				if (sdRoutineLock)
					return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
				nudgeNotes(offset);
			}
		}
		return ACTION_RESULT_DEALT_WITH;
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		if (!shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
wantToEditNoteRowLength:
			if (sdRoutineLock)
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    getOrCreateNoteRowForYDisplay(modelStack, lastAuditionedYDisplay);

			editNoteRowLength(modelStackWithNoteRow, offset, lastAuditionedYDisplay);
			editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
		}

		// Unlike for all other cases where we protect against the user accidentally turning the encoder more after releasing their press on it,
		// for this edit-NoteRow-length action, because it's a related action, it's quite likely that the user actually will want to do it after the yes-pressed-encoder-down
		// action, which is "rotate/shift notes in row". So, we have a 250ms timeout for this one.
		else if ((uint32_t)(AudioEngine::audioSampleTimer - timeHorizontalKnobLastReleased) >= 250 * 44) {
			shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
			goto wantToEditNoteRowLength;
		}
		return ACTION_RESULT_DEALT_WITH;
	}

	// Auditioning *and* holding down <> encoder - rotate/shift just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    ((InstrumentClip*)modelStack->getTimelineCounter())
		        ->getNoteRowOnScreen(lastAuditionedYDisplay, modelStack); // Don't create

		rotateNoteRowHorizontally(modelStackWithNoteRow, offset, lastAuditionedYDisplay, true);
		shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress =
		    true; // So don't accidentally shorten row after
		editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
		return ACTION_RESULT_DEALT_WITH;
	}

	// Or, maybe shift everything horizontally
	else if ((isNoUIModeActive() && Buttons::isButtonPressed(yEncButtonX, yEncButtonY))
	         || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	             && Buttons::isButtonPressed(clipViewButtonX, clipViewButtonY))) {
		if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
		int squareSize = getPosFromSquare(1) - getPosFromSquare(0);
		int shiftAmount = offset * squareSize;
		InstrumentClip* clip = getCurrentClip();

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		clip->shiftHorizontally(modelStack, shiftAmount);
		uiNeedsRendering(this, 0xFFFFFFFF, 0);

		// If possible, just modify a previous Action to add this new shift amount to it.
		Action* action = actionLogger.firstAction[BEFORE];
		if (action && action->type == ACTION_INSTRUMENT_CLIP_HORIZONTAL_SHIFT && action->openForAdditions
		    && action->currentClip == clip) {

			// If there's no Consequence in the Action, that's probably because we deleted it a previous time with the code just below.
			// Or possibly because the Action was created but there wasn't enough RAM to create the Consequence. Anyway, just go add a consequence now.
			if (!action->firstConsequence) goto addConsequenceToAction;

			ConsequenceInstrumentClipHorizontalShift* consequence =
			    (ConsequenceInstrumentClipHorizontalShift*)action->firstConsequence;
			consequence->amount += shiftAmount;

			// It might look tempting that if we've completed one whole loop, we could delete the Consequence because everything would be back the same -
			// but no! Remember different NoteRows might have different lengths.
		}

		// Or if no previous Action, go create a new one now.
		else {

			action = actionLogger.getNewAction(ACTION_INSTRUMENT_CLIP_HORIZONTAL_SHIFT, ACTION_ADDITION_NOT_ALLOWED);
			if (action) {
addConsequenceToAction:
				void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceInstrumentClipHorizontalShift));

				if (consMemory) {
					ConsequenceInstrumentClipHorizontalShift* newConsequence =
					    new (consMemory) ConsequenceInstrumentClipHorizontalShift(shiftAmount);
					action->addConsequence(newConsequence);
				}
			}
		}
		return ACTION_RESULT_DEALT_WITH;
	}

	// Or, let parent deal with it
	else {
		return ClipView::horizontalEncoderAction(offset);
	}
}

// Supply offset as 0 to just popup number, not change anything
void InstrumentClipView::editNoteRepeat(int offset) {

	shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;

	if (numEditPadPresses != 1) return; // Yup, you're only allowed to do this with one press at a time.

	int i;
	for (i = 0; i < editPadPressBufferSize; i++) {
		if (editPadPresses[i].isActive) {
			break;
		}
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	InstrumentClip* currentClip = (InstrumentClip*)modelStack->getTimelineCounter();

	ModelStackWithNoteRow* modelStackWithNoteRow =
	    currentClip->getNoteRowOnScreen(editPadPresses[i].yDisplay, modelStack);

	int32_t squareStart = getPosFromSquare(editPadPresses[i].xDisplay);
	int32_t squareWidth = getSquareWidth(editPadPresses[i].xDisplay, modelStackWithNoteRow->getLoopLength());

	int32_t searchTerms[2];
	searchTerms[0] = squareStart;
	searchTerms[1] = squareStart + squareWidth;
	int resultingIndexes[2];
	modelStackWithNoteRow->getNoteRow()->notes.searchDual(searchTerms, resultingIndexes);

	int oldNumNotes = resultingIndexes[1] - resultingIndexes[0];
	int newNumNotes = oldNumNotes + offset;

	// If "just displaying not editing" or unable to move any further, just display and get out
	if (!offset || newNumNotes < 1 || newNumNotes > squareWidth) {
		newNumNotes = oldNumNotes; // And just output that below without editing
	}

	else {
		editPadPresses[i].isBlurredSquare = true; // It's (probably) blurred now - better remember that.

		// See if we can do a "secret UNDO".
		Action* lastAction = actionLogger.firstAction[BEFORE];
		if (offset && lastAction && lastAction->type == ACTION_NOTE_REPEAT_EDIT && lastAction->openForAdditions
		    && lastAction->offset == -offset) {
			actionLogger.undoJustOneConsequencePerNoteRow(
			    modelStack
			        ->toWithSong()); // Only ok because we're not going to use the ModelStackWithTimelineCounter or with any more stuff again here.
		}

		else {
			Action* action = actionLogger.getNewAction(ACTION_NOTE_REPEAT_EDIT, ACTION_ADDITION_ALLOWED);
			if (action) action->offset = offset;

			modelStackWithNoteRow->getNoteRow()->editNoteRepeatAcrossAllScreens(
			    squareStart, squareWidth, modelStackWithNoteRow, action, currentClip->getWrapEditLevel(), newNumNotes);
			Uart::println("did actual note repeat edit");
		}

		uiNeedsRendering(this, 0xFFFFFFFF, 0);
		currentClip->expectEvent();
	}

#if HAVE_OLED
	char buffer[20];
	strcpy(buffer, "Note repeats: ");
	intToString(newNumNotes, buffer + strlen(buffer));
	OLED::popupText(buffer);
#else
	char buffer[12];
	intToString(newNumNotes, buffer);
	numericDriver.displayPopup(buffer, 0, true);
#endif
}

// Supply offset as 0 to just popup number, not change anything
void InstrumentClipView::nudgeNotes(int offset) {

	shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;

	// If just popping up number, but multiple presses, we're quite limited with what intelligible stuff we can display
	if (!offset && numEditPadPresses > 1) return;

	int resultingTotalOffset = 0;

	bool foundOne = false;
	int xDisplay;

	// Declare these out here so we can keep the value - we'll use this for the mod region if there was only 1 press
	int32_t newPos;
	NoteRow* noteRow;
	int noteRowId;

	bool didAnySuccessfulNudging = false;

	InstrumentClip* currentClip = getCurrentClip();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	// If the user is nudging back in the direction they just nudged, we can do a (possibly partial) undo, getting back the proper length of any notes that got trimmed etc.

	Action* lastAction = actionLogger.firstAction[BEFORE];
	if (offset && lastAction && lastAction->type == ACTION_NOTE_NUDGE && lastAction->openForAdditions
	    && lastAction->offset == -offset) {

		didAnySuccessfulNudging = true;

		actionLogger.undoJustOneConsequencePerNoteRow(modelStack);

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
		    modelStack->addTimelineCounter(modelStack->song->currentClip);

		// Still have to work out resultingTotalOffset, to display for the user
		for (int i = 0; i < editPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {

				int noteRowIndex;
				noteRow = currentClip->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
				noteRowId = currentClip->getNoteRowId(noteRow, noteRowIndex);

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);

				int32_t noteRowEffectiveLength = modelStackWithNoteRow->getLoopLength();

				newPos = editPadPresses[i].intendedPos + offset;
				if (newPos < 0) newPos += noteRowEffectiveLength;
				else if (newPos >= noteRowEffectiveLength) newPos -= noteRowEffectiveLength;

				int n = noteRow->notes.search(newPos, GREATER_OR_EQUAL);
				Note* note = noteRow->notes.getElement(n);
				if (note && note->pos == newPos) {
					editPadPresses[i].intendedPos = newPos;
				}
				else {
					newPos = editPadPresses[i].intendedPos;
				}

				if (!foundOne) {
					foundOne = true;
					xDisplay = editPadPresses[i].xDisplay;
					int32_t squareStart = getPosFromSquare(xDisplay);
					resultingTotalOffset = editPadPresses[i].intendedPos - squareStart;
				}
			}
		}
	}

	// Or, if not doing the partial-undo method, we'll just try and do a plain old nudge
	else {
		Action* action = NULL;

		if (offset) {
			action = actionLogger.getNewAction(ACTION_NOTE_NUDGE, ACTION_ADDITION_ALLOWED);
			if (action) action->offset = offset;
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
		    modelStack->addTimelineCounter(modelStack->song->currentClip);

		// For each note / pad held down...
		for (int i = 0; i < editPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {
				editPadPresses[i].deleteOnDepress = false;

				if (offset)
					editPadPresses[i].isBlurredSquare = true; // So it doesn't get dragged along with a vertical scroll

				int noteRowIndex;
				noteRow = currentClip->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
				noteRowId = currentClip->getNoteRowId(noteRow, noteRowIndex);

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);

				int32_t noteRowEffectiveLength = modelStackWithNoteRow->getLoopLength();

				newPos = editPadPresses[i].intendedPos + offset;
				if (newPos < 0) newPos += noteRowEffectiveLength;
				else if (newPos >= noteRowEffectiveLength) newPos -= noteRowEffectiveLength;

				bool gotCollision = false;

				if (offset) {
					// We're going to nudge notes across all screens, but before we do, check if this particular note is gonna collide with anything
					int searchBoundary;
					int searchDirection;
					int n;
					if (offset >= 0) { // Nudging right
						if (newPos == 0) {
							n = 0;
							goto doCompareNote;
						}
						else {
							searchBoundary = newPos;
							searchDirection = GREATER_OR_EQUAL;
doSearch:
							n = noteRow->notes.search(searchBoundary, searchDirection);
doCompareNote:
							Note* note = noteRow->notes.getElement(n);
							if (note && note->pos == newPos) {
								newPos =
								    editPadPresses[i]
								        .intendedPos; // Make it so the below code just displays the already existing offset
								gotCollision = true;
							}
						}
					}
					else { // Nudging left
						if (editPadPresses[i].intendedPos == 0) {
							n = noteRow->notes.getNumElements();
							goto doCompareNote;
						}
						else {
							searchBoundary = editPadPresses[i].intendedPos;
							searchDirection = LESS;
							goto doSearch;
						}
					}
				}

				if (!foundOne) {
					foundOne = true;
					xDisplay = editPadPresses[i].xDisplay;
					int32_t squareStart = getPosFromSquare(xDisplay);
					resultingTotalOffset = newPos - squareStart;
					if (!offset) break;
				}

				if (!gotCollision) {
					int distanceTilNext =
					    noteRow->getDistanceToNextNote(editPadPresses[i].intendedPos, modelStackWithNoteRow);

					int error =
					    noteRow->nudgeNotesAcrossAllScreens(editPadPresses[i].intendedPos, modelStackWithNoteRow,
					                                        action, currentClip->getWrapEditLevel(), offset);
					if (error) {
						numericDriver.displayError(error);
						return;
					}

					// Nudge automation at NoteRow level, while our ModelStack still has a pointer to the NoteRow
					{
						ModelStackWithThreeMainThings* modelStackWithThreeMainThingsForNoteRow =
						    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();
						noteRow->paramManager.nudgeAutomationHorizontallyAtPos(
						    editPadPresses[i].intendedPos, offset,
						    modelStackWithThreeMainThingsForNoteRow->getLoopLength(), action,
						    modelStackWithThreeMainThingsForNoteRow, distanceTilNext);
					}

					// WARNING! A bit dodgy, but at this stage, we can no longer refer to modelStackWithNoteRow, cos we're going to reuse its
					// parent ModelStackWithTimelineCounter, below.

					// Nudge automation at Clip level
					{
						int32_t lengthBeforeLoop = currentClip->getLoopLength();
						ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
						    modelStackWithTimelineCounter->addOtherTwoThingsButNoNoteRow(
						        currentClip->output->toModControllable(), &currentClip->paramManager);
						currentClip->paramManager.nudgeAutomationHorizontallyAtPos(editPadPresses[i].intendedPos,
						                                                           offset, lengthBeforeLoop, action,
						                                                           modelStackWithThreeMainThings);
					}

					editPadPresses[i].intendedPos = newPos;
					didAnySuccessfulNudging = true;
				}
				else {
					newPos = editPadPresses[i].intendedPos;
				}
			}
		}
	}

	// Now, decide what message to display ---------------------------------------------------
#if HAVE_OLED
	char buffer[24];
#else
	char buffer[5];
#endif
	char const* message;
	bool alignRight = false;

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(
	    modelStack->song
	        ->currentClip); // Can finally do this since we're not going to use the bare ModelStack for anything else

	if (numEditPadPresses > 1) {
		if (!didAnySuccessfulNudging)
			return; // Don't want to see these "multiple pads moved" messages if in fact none were moved
#if HAVE_OLED
		message = (offset >= 0) ? "Nudged notes right" : "Nudged notes left";
#else
		message = (offset >= 0) ? "RIGHT" : "LEFT";
#endif
	}

	else {
		if (resultingTotalOffset >= (currentClip->loopLength >> 1)) resultingTotalOffset -= currentClip->loopLength;
		else if (resultingTotalOffset <= -(currentClip->loopLength >> 1))
			resultingTotalOffset += currentClip->loopLength;

		if (resultingTotalOffset > 0) {

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);

			int32_t squareWidth = getSquareWidth(xDisplay, modelStackWithNoteRow->getLoopLength());
			if (resultingTotalOffset > (squareWidth >> 1)) {
				if (!doneAnyNudgingSinceFirstEditPadPress) {
					offsettingNudgeNumberDisplay = true;
				}
			}
			else {
				offsettingNudgeNumberDisplay = false;
			}

			if (offsettingNudgeNumberDisplay) resultingTotalOffset -= squareWidth;
		}

#if HAVE_OLED
		strcpy(buffer, "Note nudge: ");
		intToString(resultingTotalOffset, buffer + strlen(buffer));
		message = buffer;
#else
		if (resultingTotalOffset > 9999) message = "RIGHT";
		else if (resultingTotalOffset < -999) message = "LEFT";
		else {
			message = buffer;
			alignRight = true;
			intToString(resultingTotalOffset, buffer);
		}
#endif
	}

#if HAVE_OLED
	OLED::popupText(message);
#else
	numericDriver.displayPopup(message, 0, alignRight);
#endif

	doneAnyNudgingSinceFirstEditPadPress =
	    true; // Even if we didn't actually nudge, we want to record this for the purpose of the offsetting of the number display - see above

	if (!offset) return;

	// If multiple presses, just abandon the mod region
	if (numEditPadPresses > 1) {
abandonModRegion:
		view.setModRegion();
	}

	// Otherwise, update it for what they actually intend
	else {

		int i = noteRow->notes.search(newPos, GREATER_OR_EQUAL);
		Note* note = noteRow->notes.getElement(i);
		if (!note || note->pos != newPos) goto abandonModRegion;

		// Edit mod knob values for this Note's region
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);
		int32_t distanceToNextNote = currentClip->getDistanceToNextNote(note, modelStackWithNoteRow);
		//view.setModRegion(newPos, max((uint32_t)distanceToNextNote + lastNote->pos - firstNote->pos, squareWidth)); // This is what happens with initial press, kinda different...
		view.setModRegion(newPos, distanceToNextNote, modelStackWithNoteRow->noteRowId);
	}

	uiNeedsRendering(this, 0xFFFFFFFF, 0);

	if (playbackHandler.isEitherClockActive() && modelStackWithTimelineCounter->song->isClipActive(currentClip)) {
		currentClip->expectEvent();
		currentClip->reGetParameterAutomation(modelStackWithTimelineCounter);
	}
}

void InstrumentClipView::graphicsRoutine() {
	if (!currentSong)
		return; // Briefly, if loading a song fails, during the creation of a new blank one, this could happen.

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) return;

	if (PadLEDs::flashCursor == FLASH_CURSOR_OFF) return;

	int newTickSquare;

	bool reallyNoTickSquare = (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip)
	                           || currentUIMode == UI_MODE_EXPLODE_ANIMATION || playbackHandler.ticksLeftInCountIn);

	if (reallyNoTickSquare) {
		newTickSquare = 255;
	}
	else {
		newTickSquare = getTickSquare();
		if (newTickSquare < 0 || newTickSquare >= displayWidth) newTickSquare = 255;
	}

	uint8_t tickSquares[displayHeight];
	memset(tickSquares, newTickSquare, displayHeight);

	uint8_t colours[displayHeight];
	uint8_t nonMutedColour = clip->getCurrentlyRecordingLinearly() ? 2 : 0;
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		int noteRowIndex;
		NoteRow* noteRow = clip->getNoteRowOnScreen(yDisplay, currentSong, &noteRowIndex);
		colours[yDisplay] = (noteRow && noteRow->muted) ? 1 : nonMutedColour;

		if (!reallyNoTickSquare) {
			if (noteRow && noteRow->hasIndependentPlayPos()) {

				int noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

				int rowTickSquare = getSquareFromPos(noteRow->getLivePos(modelStackWithNoteRow));
				if (rowTickSquare < 0 || rowTickSquare >= displayWidth) rowTickSquare = 255;
				tickSquares[yDisplay] = rowTickSquare;
			}
		}
	}

	PadLEDs::setTickSquares(tickSquares, colours);
}

void InstrumentClipView::fillOffScreenImageStores() {

	uint32_t xZoom = currentSong->xZoom[NAVIGATION_CLIP];
	uint32_t xScroll = currentSong->xScroll[NAVIGATION_CLIP];

	// We're also going to fill up an extra, currently-offscreen imageStore row, with all notes currently offscreen

	int noteRowIndexBottom, noteRowIndexTop;
	if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
		noteRowIndexBottom = getCurrentClip()->yScroll;
		noteRowIndexTop = getCurrentClip()->yScroll + displayHeight;
	}
	else {
		noteRowIndexBottom =
		    getCurrentClip()->noteRows.search(getCurrentClip()->getYNoteFromYDisplay(0, currentSong), GREATER_OR_EQUAL);
		noteRowIndexTop = getCurrentClip()->noteRows.search(
		    getCurrentClip()->getYNoteFromYDisplay(displayHeight, currentSong), GREATER_OR_EQUAL);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	currentSong->currentClip->renderAsSingleRow(modelStack, this, xScroll, xZoom, PadLEDs::imageStore[0][0],
	                                            PadLEDs::occupancyMaskStore[0], false, 0, noteRowIndexBottom, 0,
	                                            displayWidth, true, false);
	currentSong->currentClip->renderAsSingleRow(modelStack, this, xScroll, xZoom,
	                                            PadLEDs::imageStore[displayHeight + 1][0],
	                                            PadLEDs::occupancyMaskStore[displayHeight + 1], false, noteRowIndexTop,
	                                            2147483647, 0, displayWidth, true, false);

	// Clear sidebar pads from offscreen image stores
	for (int x = displayWidth; x < displayWidth + sideBarWidth; x++) {
		for (int colour = 0; colour < 3; colour++) {
			PadLEDs::imageStore[0][x][colour] = 0;
			PadLEDs::imageStore[displayHeight + 1][x][colour] = 0;
		}
		PadLEDs::occupancyMaskStore[0][x] = 0;
		PadLEDs::occupancyMaskStore[displayHeight + 1][x] = 0;
	}
}

uint32_t InstrumentClipView::getSquareWidth(int32_t square, int32_t effectiveLength) {
	int32_t squareRightEdge = getPosFromSquare(square + 1);
	return getMin(effectiveLength, squareRightEdge) - getPosFromSquare(square);
}

void InstrumentClipView::flashDefaultRootNote() {
	flashDefaultRootNoteOn = !flashDefaultRootNoteOn;
	uiNeedsRendering(this, 0, 0xFFFFFFFF);
	uiTimerManager.setTimer(TIMER_DEFAULT_ROOT_NOTE, flashTime);
}

void InstrumentClipView::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {

	if (currentUIMode & UI_MODE_HORIZONTAL_SCROLL) return;

	if (clip == getCurrentClip()) {
		for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
			if (getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong)) {
				uiNeedsRendering(this, 1 << yDisplay, 0);
			}
		}
	}
}

bool InstrumentClipView::isDrumAuditioned(Drum* drum) {

	if (currentSong->currentClip->output->type != INSTRUMENT_TYPE_KIT) return false;

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		if (auditionPadIsPressed[yDisplay]) {
			NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
			if (noteRow && noteRow->drum == drum) return true;
		}
	}

	return false;
}

bool InstrumentClipView::getAffectEntire() {
	return getCurrentClip()->affectEntire;
}

void InstrumentClipView::tellMatrixDriverWhichRowsContainSomethingZoomable() {
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
		PadLEDs::transitionTakingPlaceOnRow[yDisplay] = (noteRow && !noteRow->hasNoNotes());
	}
}

void InstrumentClipView::notifyPlaybackBegun() {
	reassessAllAuditionStatus();
}

bool InstrumentClipView::renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                        uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea) {
	if (!image) return true;

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) return true;

	PadLEDs::renderingLock = true;
	performActualRender(whichRows, &image[0][0][0], occupancyMask, currentSong->xScroll[NAVIGATION_CLIP],
	                    currentSong->xZoom[NAVIGATION_CLIP], displayWidth, displayWidth + sideBarWidth,
	                    drawUndefinedArea);
	PadLEDs::renderingLock = false;

	return true;
}

// occupancyMask now optional
void InstrumentClipView::performActualRender(uint32_t whichRows, uint8_t* image,
                                             uint8_t occupancyMask[][displayWidth + sideBarWidth], int32_t xScroll,
                                             uint32_t xZoom, int renderWidth, int imageWidth, bool drawUndefinedArea) {
	InstrumentClip* clip = getCurrentClip();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {

		if (whichRows & (1 << yDisplay)) {

			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			uint8_t* occupancyMaskOfRow = NULL;
			if (occupancyMask) occupancyMaskOfRow = occupancyMask[yDisplay];

			// If row doesn't have a NoteRow, wipe it empty
			if (!noteRow) {
				memset(image, 0, renderWidth * 3);
				if (occupancyMask) memset(occupancyMaskOfRow, 0, renderWidth);
			}

			// Otherwise render the row
			else {
				noteRow->renderRow(this, rowColour[yDisplay], rowTailColour[yDisplay], rowBlurColour[yDisplay], image,
				                   occupancyMaskOfRow, true, modelStackWithNoteRow->getLoopLength(),
				                   clip->allowNoteTails(modelStackWithNoteRow), renderWidth, xScroll, xZoom, 0,
				                   renderWidth, false);
			}

			if (drawUndefinedArea) {
				int32_t effectiveLength = modelStackWithNoteRow->getLoopLength();

				clip->drawUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMaskOfRow, renderWidth, this,
				                        currentSong->tripletsOn); // Sends image pointer for just the one row
			}
		}

		image += imageWidth * 3;
	}
}

void InstrumentClipView::transitionToSessionView() {
	int transitioningToRow = sessionView.getClipPlaceOnScreen(currentSong->currentClip);

	// TODO: could probably just copy data to these...
	renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1], false);
	renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);

	currentUIMode =
	    UI_MODE_INSTRUMENT_CLIP_COLLAPSING; // Must set this after above render calls, or else they'll see it and not render

	PadLEDs::numAnimatedRows = displayHeight + 2;
	for (int y = 0; y < displayHeight + 2; y++) {
		PadLEDs::animatedRowGoingTo[y] = transitioningToRow;
		PadLEDs::animatedRowGoingFrom[y] = y - 1;
	}

	// Set occupancy masks to full for the sidebar squares in the Store
	for (int y = 0; y < displayHeight; y++) {
		PadLEDs::occupancyMaskStore[y + 1][displayWidth] = 64;
		PadLEDs::occupancyMaskStore[y + 1][displayWidth + 1] = 64;
	}

	PadLEDs::setupInstrumentClipCollapseAnimation(true);

	fillOffScreenImageStores();
	PadLEDs::recordTransitionBegin(clipCollapseSpeed);
	PadLEDs::renderClipExpandOrCollapse();
}

void InstrumentClipView::playbackEnded() {

	// Easter egg - if user's holding down a note, we want it to be edit-auditioned again now
	reassessAllAuditionStatus();
}

void InstrumentClipView::scrollFinished() {
	if (currentUIMode == UI_MODE_AUDITIONING) {
		uiNeedsRendering(
		    this, 0xFFFFFFFF,
		    0); // Needed because sometimes we initiate a scroll before reverting an Action, so we need to properly render again afterwards
	}

	else ClipView::scrollFinished();
}

void InstrumentClipView::clipNeedsReRendering(Clip* clip) {
	if (clip == getCurrentClip()) {
		uiNeedsRendering(this); // Re-renders sidebar too. Probably a good idea? Can't hurt?
	}
}

void InstrumentClipView::dontDeleteNotesOnDepress() {
	for (int i = 0; i < editPadPressBufferSize; i++) {
		editPadPresses[i].deleteOnDepress = false;
	}
}

void InstrumentClipView::modEncoderAction(int whichModEncoder, int offset) {
	dontDeleteNotesOnDepress();

	InstrumentClip* clip = getCurrentClip();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	Output* output = clip->output;

	if (output->type == INSTRUMENT_TYPE_KIT && isUIModeActive(UI_MODE_AUDITIONING)) {

		Kit* kit = (Kit*)output;

		if (kit->selectedDrum && kit->selectedDrum->type != DRUM_TYPE_SOUND) {

			if (ALPHA_OR_BETA_VERSION && !kit->activeClip) numericDriver.freezeWithError("E381");

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(kit->activeClip);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    ((InstrumentClip*)kit->activeClip)
			        ->getNoteRowForDrum(modelStackWithTimelineCounter,
			                            kit->selectedDrum); // The NoteRow probably doesn't get referred to...

			NonAudioDrum* drum = (NonAudioDrum*)kit->selectedDrum;

			ParamManagerForTimeline* paramManager = NULL;
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			if (noteRow) paramManager = &noteRow->paramManager; // Should be NULL currently, cos it's a NonAudioDrum.
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStackWithNoteRow->addOtherTwoThings(drum->toModControllable(), paramManager);

			drum->modEncoderAction(modelStackWithThreeMainThings, offset, whichModEncoder);
		}
	}

	ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
}

// Check UI mode is appropriate before calling this
void InstrumentClipView::editNumEuclideanEvents(ModelStackWithNoteRow* modelStack, int offset, int yDisplay) {

	int newNumNotes = 0;

	int32_t effectiveLength = modelStack->getLoopLength();

	uint32_t squareWidth = getSquareWidth(0, MAX_SEQUENCE_LENGTH);
	int numStepsAvailable = (uint32_t)(effectiveLength - 1) / squareWidth + 1; // Round up

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow) {
		if (!offset) goto displayNewNumNotes;
		return;
	}

	{
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

		int oldNumNotes = noteRow->notes.getNumElements();
		newNumNotes = oldNumNotes;

		if (offset) { // Or if offset is 0, we'll just display the current number, below, without changing anything
			newNumNotes += offset;
			if (newNumNotes < 0) { // If can't go lower, just display old number
justDisplayOldNumNotes:
				newNumNotes = oldNumNotes;
				goto displayNewNumNotes;
			}

			// If there aren't enough steps...
			if (newNumNotes > numStepsAvailable) {
				// If user was trying to increase num events, well they just can't
				if (offset >= 0) goto justDisplayOldNumNotes;

				// Or if they're decreasing, well decrease further
				else newNumNotes = numStepsAvailable;
			}

			// Do a "partial undo" if we can
			Action* lastAction = actionLogger.firstAction[BEFORE];
			// No need to check that lastAction was for the same Clip or anything - the Action gets "closed" manually when we stop auditioning.
			if (lastAction && lastAction->type == ACTION_EUCLIDEAN_NUM_EVENTS_EDIT && lastAction->openForAdditions
			    && lastAction->offset == -offset) {

				char modelStackMemory2[MODEL_STACK_MAX_SIZE];
				ModelStack* modelStackWithJustSong = setupModelStackWithSong(modelStackMemory2, modelStack->song);

				bool revertedWholeAction = actionLogger.undoJustOneConsequencePerNoteRow(modelStackWithJustSong);
				if (!revertedWholeAction) goto noteRowChanged;
			}

			else {
				{
					// Make new NoteVector for the new Notes, since ActionLogger should be "stealing" the old data
					NoteVector newNotes;
					if (newNumNotes) {
						int error = newNotes.insertAtIndex(0, newNumNotes); // Pre-allocate, so no errors later
						if (error) {
							numericDriver.displayError(error);
							return;
						}
					}

					// Record Action
					Action* action =
					    actionLogger.getNewAction(ACTION_EUCLIDEAN_NUM_EVENTS_EDIT, ACTION_ADDITION_ALLOWED);
					if (action) action->offset = offset;

					// Create the Notes
					for (int n = 0; n < newNumNotes; n++) {
						Note* note = newNotes.getElement(n);
						note->pos = (uint32_t)(n * numStepsAvailable) / (uint32_t)newNumNotes * squareWidth;
						note->length = squareWidth;
						note->probability = NUM_PROBABILITY_VALUES;
						note->velocity = ((Instrument*)clip->output)->defaultVelocity;
						note->lift = DEFAULT_LIFT_VALUE;
					}

					// Just make sure final note isn't too long
					if (newNumNotes) {
						Note* note = newNotes.getElement(newNumNotes - 1);
						int maxLength = effectiveLength - note->pos;
						if (note->length > maxLength) note->length = maxLength;
					}

					// Delete / steal / consequence-ize the MPE data first, because in order for partial undos to work, this has to be further down the
					// linked list of Consequences than the note-array-change that we do next, below.
					ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
					ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
					if (mpeParams) {
						ModelStackWithParamCollection* modelStackWithParamCollection =
						    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(
						        mpeParams, mpeParamsSummary);
						mpeParams->deleteAllAutomation(action, modelStackWithParamCollection);
					}

					// Record change, stealing the old note data
					if (action) {
						// We "definitely" store the change, because unusually, we may want to revert individual Consequences in the Action one by one
						action->recordNoteArrayChangeDefinitely(clip, modelStack->noteRowId, &noteRow->notes, true);
					}

					// Swap the new temporary note data into the permanent place
					noteRow->notes.swapStateWith(&newNotes);

#if ALPHA_OR_BETA_VERSION
					noteRow->notes.testSequentiality("E376");
#endif
				}

noteRowChanged:
				// Play it
				clip->expectEvent();

				// Render it
				if (yDisplay >= 0 && yDisplay < displayHeight) uiNeedsRendering(this, 1 << yDisplay, 0);
			}
		}
	}
displayNewNumNotes:
	// Tell the user about it in text
#if HAVE_OLED
	char buffer[34];
	strcpy(buffer, "Events: ");
	char* pos = strchr(buffer, 0);
	intToString(newNumNotes, pos);
	pos = strchr(buffer, 0);
	strcpy(pos, " of ");
	pos = strchr(buffer, 0);
	intToString(numStepsAvailable, pos);
	OLED::popupText(buffer);
#else
	char buffer[12];
	intToString(newNumNotes, buffer);
	numericDriver.displayPopup(buffer, 0, true);
#endif
}

// Check UI mode is appropriate before calling this
void InstrumentClipView::rotateNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int offset, int yDisplay,
                                                   bool shouldDisplayDirectionEvenIfNoNoteRow) {

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow) {
		if (shouldDisplayDirectionEvenIfNoNoteRow) goto displayMessage;
		return;
	}

	{
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

		uint32_t squareWidth = getSquareWidth(0, MAX_SEQUENCE_LENGTH);
		int32_t shiftAmount = offset * squareWidth;

		clip->shiftOnlyOneNoteRowHorizontally(modelStack, shiftAmount);

		// Render change
		if (yDisplay >= 0 && yDisplay < displayHeight) uiNeedsRendering(this, 1 << yDisplay, 0);

		// If possible, just modify a previous Action to add this new shift amount to it.
		Action* action = actionLogger.firstAction[BEFORE];
		if (action && action->type == ACTION_NOTEROW_HORIZONTAL_SHIFT && action->openForAdditions
		    && action->currentClip == clip) {

			// If there's no Consequence in the Action, that's probably because we deleted it a previous time with the code just below.
			// Or possibly because the Action was created but there wasn't enough RAM to create the Consequence. Anyway, just go add a consequence now.
			if (!action->firstConsequence) goto addConsequenceToAction;

			ConsequenceNoteRowHorizontalShift* consequence =
			    (ConsequenceNoteRowHorizontalShift*)action->firstConsequence;
			if (consequence->noteRowId != modelStack->noteRowId) goto getNewAction;

			consequence->amount += shiftAmount;
		}

		// Or if no previous Action, go create a new one now.
		else {
getNewAction:
			action = actionLogger.getNewAction(ACTION_NOTEROW_HORIZONTAL_SHIFT, ACTION_ADDITION_NOT_ALLOWED);
			if (action) {
addConsequenceToAction:
				void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceNoteRowHorizontalShift));

				if (consMemory) {
					ConsequenceNoteRowHorizontalShift* newConsequence =
					    new (consMemory) ConsequenceNoteRowHorizontalShift(modelStack->noteRowId, shiftAmount);
					action->addConsequence(newConsequence);
				}
			}
		}
	}

displayMessage:
#if HAVE_OLED
	char const* message = (offset == 1) ? "Rotated right" : "Rotated left";
	OLED::popupText(message);
#else
	char const* message = (offset == 1) ? "RIGHT" : "LEFT";
	numericDriver.displayPopup(message, 0);
#endif
}

extern bool shouldResumePlaybackOnNoteRowLengthSet;

// Check UI mode is appropriate before calling this.
// Can handle being given a NULL NoteRow, in which case it'll do nothing.
void InstrumentClipView::editNoteRowLength(ModelStackWithNoteRow* modelStack, int offset, int yDisplay) {

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow) return;
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	int32_t oldLength = modelStack->getLoopLength();

	// If we're not scrolled all the way to the right, go there now. If we were already further right than the end of this NoteRow, it's ok, we'll stay there.
	if (scrollRightToEndOfLengthIfNecessary(oldLength)) return; // ACTION_RESULT_DEALT_WITH;

	uint32_t squareWidth = getSquareWidth(0, MAX_SEQUENCE_LENGTH);

	int oldNumSteps = (uint32_t)(oldLength - 1) / squareWidth + 1; // Round up
	int newNumSteps = oldNumSteps + offset;
	if (newNumSteps <= 0) return;
	int newLength = newNumSteps * squareWidth;
	if (newLength > MAX_SEQUENCE_LENGTH) return;

	int32_t oldPos =
	    modelStack
	        ->getLastProcessedPos(); // We have to grab and remember this before an initial revert() ("secret UNDO") potentially stuffs it up
	bool hadIndependentPlayPosBefore = noteRow->hasIndependentPlayPos();
	bool didSecretUndo = false;

	// See if we can do a secret undo
	Action* prevAction = actionLogger.firstAction[BEFORE];
	if (prevAction && prevAction->openForAdditions && prevAction->type == ACTION_NOTEROW_LENGTH_EDIT
	    && prevAction->currentClip == clip) {

		ConsequenceNoteRowLength* prevCons = (ConsequenceNoteRowLength*)prevAction->firstConsequence;
		if (prevCons->noteRowId != modelStack->noteRowId) goto editLengthWithNewAction;

		// If we're recovering a bit that previously got chopped off, do secret undo to recover any chopped-off notes and automation
		if (offset == 1 && prevCons->backedUpLength > oldLength) {
			shouldResumePlaybackOnNoteRowLengthSet = false; // Ugly hack, kinda
			actionLogger.revert(BEFORE, false, false);
			shouldResumePlaybackOnNoteRowLengthSet = true;
			didSecretUndo = true;

			// If that got us to the intended length, all is good...
			if (noteRow->loopLengthIfIndependent == newLength
			    || (!noteRow->loopLengthIfIndependent && clip->loopLength == newLength)) {
possiblyDoResumePlaybackOnNoteRow:
				// Need to do the resumePlayback that we blocked happening during the revert()
				if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(clip)) {
					noteRow->resumePlayback(modelStack, true);
				}
			}

			// Otherwise, go make a new Action and get to correct length
			else goto editLengthWithNewAction;
		}

		// Otherwise, the Action / Consequence is still fine for doing a future UNDO, so just edit length as needed.
		// But we'll still pass the prevAction in, so that anything which wasn't snapshotted yet (because no Notes happened to get trimmed last time)
		// can get snapshotted now.
		else {
			noteRow->setLength(modelStack, newLength, prevAction, oldPos,
			                   hadIndependentPlayPosBefore); // Might call resumePlayback() too.
		}
	}

	else {
editLengthWithNewAction:
		Action* action = actionLogger.getNewAction(ACTION_NOTEROW_LENGTH_EDIT, false);
		if (!action) {
ramError:
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
			if (didSecretUndo) {
				// Need to do the resumePlayback that we blocked happening during the revert()
				if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(clip)) {
					noteRow->resumePlayback(modelStack, true);
				}
			}
			return;
		}

		void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceNoteRowLength));
		if (!consMemory) {
			goto ramError;
		}

		ConsequenceNoteRowLength* newConsequence =
		    new (consMemory) ConsequenceNoteRowLength(modelStack->noteRowId, newLength);
		action->addConsequence(newConsequence);

		// The ConsequenceNoteRowLength does the actual work for us for this function
		newConsequence->performChange(modelStack, action, oldPos, hadIndependentPlayPosBefore);
	}

	bool didScroll;

	// Lengthening
	if (offset == 1) {
		didScroll = scrollRightToEndOfLengthIfNecessary(newLength);
		if (!didScroll) goto tryScrollingLeft;
	}

	// Shortening
	else {
tryScrollingLeft:
		didScroll = scrollLeftIfTooFarRight(newLength);
	}

#if HAVE_OLED
	char buffer[19];
	strcpy(buffer, "Steps: ");
	intToString(newNumSteps, buffer + strlen(buffer));
	OLED::popupText(buffer);
#else
	char buffer[12];
	intToString(newNumSteps, buffer);
	numericDriver.displayPopup(buffer, 0, true);
#endif

	// Play it
	clip->expectEvent();

	// Render it
	if (!didScroll && yDisplay >= 0 && yDisplay < displayHeight) uiNeedsRendering(this, 1 << yDisplay, 0);
}

void InstrumentClipView::reportMPEInitialValuesForNoteEditing(ModelStackWithNoteRow* modelStack,
                                                              int16_t const* mpeValues) {

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();

	// MPE stuff - if editing note, we need to take note of the initial values which might have been sent before this note-on.
	if (noteRow && view.modLength && modelStack->noteRowId == view.modNoteRowId
	    && modelStack->getTimelineCounter() == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

		noteRow->paramManager.ensureExpressionParamSetExists(); // If this fails, we'll detect that below.

		ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
		ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;

		if (mpeParams) {

			ModelStackWithParamCollection* modelStackWithParamCollection =
			    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(mpeParams,
			                                                                                 mpeParamsSummary);

			for (int whichExpressionDimension = 0; whichExpressionDimension < NUM_EXPRESSION_DIMENSIONS;
			     whichExpressionDimension++) {
				mpeValuesAtHighestPressure[0][whichExpressionDimension] = mpeValues[whichExpressionDimension];
			}
		}
	}
}

void InstrumentClipView::reportMPEValueForNoteEditing(int whichExpressionimension, int32_t value) {

	// If time to move record along...
	uint32_t timeSince = AudioEngine::audioSampleTimer - mpeRecordLastUpdateTime;
	if (timeSince >= MPE_RECORD_INTERVAL_TIME) {
		mpeRecordLastUpdateTime += MPE_RECORD_INTERVAL_TIME;
		memmove(mpeValuesAtHighestPressure[1], mpeValuesAtHighestPressure[0],
		        sizeof(uint16_t) * NUM_EXPRESSION_DIMENSIONS * (MPE_RECORD_LENGTH_FOR_NOTE_EDITING - 1));
		mpeValuesAtHighestPressure[0][2] = 0; // Yes only reset the "pressure" of the new/first record
		mpeMostRecentPressure = 0;
	}

	// Always keep track of the "current" pressure value, so we can decide whether to be recording the other values.
	if (whichExpressionimension == 2) {
		mpeMostRecentPressure = value >> 16;
	}

	// And if we're still at max pressure, then yeah, record those other values.
	if (mpeMostRecentPressure >= mpeValuesAtHighestPressure[0][2]) {
		mpeValuesAtHighestPressure[0][whichExpressionimension] = value >> 16;
	}

	dontDeleteNotesOnDepress(); // We know the caller is also manually editing the AutoParam now too - this counts as an edit, so we don't want the note deleted on press-release.
}

void InstrumentClipView::reportNoteOffForMPEEditing(ModelStackWithNoteRow* modelStack) {

	NoteRow* noteRow = modelStack->getNoteRow();

	// MPE stuff for note off - if they're still "editing" a note, they'll want the values from half a second ago, or the values from when they pressed hardest.
	if (view.modLength && modelStack->noteRowId == view.modNoteRowId
	    && modelStack->getTimelineCounter() == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

		ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
		ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
		if (!mpeParams) return;

		int t = MPE_RECORD_LENGTH_FOR_NOTE_EDITING - 1;
		while (mpeValuesAtHighestPressure[t][2] == -1) {
			if (!t) return; // No data has been recorded
			t--;
		}

		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(mpeParams, mpeParamsSummary);

		for (int whichExpressionDimension = 0; whichExpressionDimension < NUM_EXPRESSION_DIMENSIONS;
		     whichExpressionDimension++) {
			AutoParam* param = &mpeParams->params[whichExpressionDimension];

			ModelStackWithAutoParam* modelStackWithAutoParam =
			    modelStackWithParamCollection->addAutoParam(whichExpressionDimension, param);

			int32_t newValue = (int32_t)mpeValuesAtHighestPressure[t][whichExpressionDimension] << 16;

			param->setValueForRegion(view.modPos, view.modLength, newValue, modelStackWithAutoParam);
		}

		dontDeleteNotesOnDepress();
	}
}
