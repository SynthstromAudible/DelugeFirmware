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

#include <ArrangerView.h>
#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <ClipInstance.h>
#include <InstrumentClipMinder.h>
#include <InstrumentClipView.h>
#include <ParamManager.h>
#include <sounddrum.h>
#include <soundinstrument.h>
#include "definitions.h"
#include "uitimermanager.h"
#include "numericdriver.h"
#include "KeyboardScreen.h"
#include "soundeditor.h"
#include "View.h"
#include "CVEngine.h"
#include "kit.h"
#include "ActionLogger.h"
#include "Consequence.h"
#include "Action.h"
#include <string.h>
#include "Session.h"
#include "uart.h"
#include "MIDIInstrument.h"
#include "CVInstrument.h"
#include "GeneralMemoryAllocator.h"
#include "Arrangement.h"
#include "MIDIParam.h"
#include "saveinstrumentpresetui.h"
#include "midiengine.h"
#include "storagemanager.h"
#include "LoadInstrumentPresetUI.h"
#include "song.h"
#include "IndicatorLEDs.h"
#include "Buttons.h"
#include "ModelStack.h"
#include "ClipMinder.h"
#include "InstrumentClip.h"
#include "MIDIParamCollection.h"

#if HAVE_OLED
#include "oled.h"
#endif

extern "C" {
#include "cfunctions.h"
#include "sio_char.h"
}

int16_t InstrumentClipMinder::defaultRootNote;
bool InstrumentClipMinder::exitScaleModeOnButtonRelease;
bool InstrumentClipMinder::flashDefaultRootNoteOn;
uint8_t InstrumentClipMinder::editingMIDICCForWhichModKnob;

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

InstrumentClipMinder::InstrumentClipMinder() {
}

void InstrumentClipMinder::selectEncoderAction(int offset) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
		if (editingMIDICCForWhichModKnob < NUM_PHYSICAL_MOD_KNOBS) {
			MIDIInstrument* instrument = (MIDIInstrument*)getCurrentClip()->output;
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(instrument, &getCurrentClip()->paramManager);

			int newCC;

			if (!Buttons::isButtonPressed(selectEncButtonX, selectEncButtonY)) {
				newCC = instrument->changeControlNumberForModKnob(offset, editingMIDICCForWhichModKnob,
				                                                  instrument->modKnobMode);
				view.setKnobIndicatorLevels();
			}
			else {
				newCC = instrument->moveAutomationToDifferentCC(offset, editingMIDICCForWhichModKnob,
				                                                instrument->modKnobMode, modelStackWithThreeMainThings);
				if (newCC == -1) {
					numericDriver.displayPopup(HAVE_OLED ? "No further unused MIDI params" : "FULL");
					return;
				}
			}

			bool automationExists = instrument->doesAutomationExistOnMIDIParam(modelStackWithThreeMainThings, newCC);

			drawMIDIControlNumber(newCC, automationExists);
		}
	}
	else {
		view.navigateThroughPresetsForInstrumentClip(offset, modelStack);
	}
}

void InstrumentClipMinder::redrawNumericDisplay() {
#if HAVE_OLED
#else
	if (getCurrentUI()->toClipMinder()) { // Seems a redundant check now? Maybe? Or not?
		view.displayOutputName(getCurrentClip()->output, false);
	}
#endif
}

#if HAVE_OLED
void InstrumentClipMinder::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	view.displayOutputName(getCurrentClip()->output, false);
}
#endif

void InstrumentClipMinder::drawMIDIControlNumber(int controlNumber, bool automationExists) {

	char buffer[HAVE_OLED ? 30 : 5];
	if (controlNumber == CC_NUMBER_NONE) strcpy(buffer, HAVE_OLED ? "No param" : "NONE");
	else if (controlNumber == CC_NUMBER_PITCH_BEND) strcpy(buffer, HAVE_OLED ? "Pitch bend" : "BEND");
	else if (controlNumber == CC_NUMBER_AFTERTOUCH) strcpy(buffer, HAVE_OLED ? "Channel pressure" : "AFTE");

	else {
		buffer[0] = 'C';
		buffer[1] = 'C';
#if HAVE_OLED
		buffer[2] = ' ';
		intToString(controlNumber, &buffer[3]);
	}
	if (automationExists) strcat(buffer, "\n(automated)");
	OLED::popupText(buffer, true);

#else
		char* numberStartPos = (controlNumber < 100) ? (buffer + 2) : (buffer + 1);
		intToString(controlNumber, numberStartPos);
	}
	numericDriver.setText(buffer, true, automationExists ? 3 : 255, true);
#endif
}

void InstrumentClipMinder::createNewInstrument(int newInstrumentType) {
	int error;

	int oldInstrumentType = getCurrentClip()->output->type;

	bool shouldReplaceWholeInstrument = currentSong->canOldOutputBeReplaced(getCurrentClip());

	String newName;
	char const* thingName = (newInstrumentType == INSTRUMENT_TYPE_SYNTH) ? "SYNT" : "KIT";
	error = Browser::currentDir.set(getInstrumentFolder(newInstrumentType));
	if (error) {
gotError:
		numericDriver.displayError(error);
		return;
	}

	error = Browser::getUnusedSlot(newInstrumentType, &newName, thingName);
	if (error) goto gotError;

	if (newName.isEmpty()) {
		numericDriver.displayPopup(HAVE_OLED ? "No further unused instrument numbers" : "FULL");
		return;
	}

	ParamManagerForTimeline newParamManager;
	Instrument* newInstrument = storageManager.createNewInstrument(newInstrumentType, &newParamManager);
	if (!newInstrument) {
		error = ERROR_INSUFFICIENT_RAM;
		goto gotError;
	}

	// Set dirPath.
	error = newInstrument->dirPath.set(getInstrumentFolder(newInstrumentType));
	if (error) {
		void* toDealloc = dynamic_cast<void*>(newInstrument);
		newInstrument->~Instrument();
		generalMemoryAllocator.dealloc(toDealloc);
		goto gotError;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E059", "H059");

	getCurrentClip()->backupPresetSlot();

#if HAVE_OLED
	char const* message = (newInstrumentType == INSTRUMENT_TYPE_KIT) ? "New kit created" : "New synth created";
	OLED::consoleText(message);
#else
	char const* message = "NEW";
	numericDriver.displayPopup(message);
#endif

	if (newInstrumentType == INSTRUMENT_TYPE_SYNTH) {
		((SoundInstrument*)newInstrument)->setupAsBlankSynth(&newParamManager);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If replacing whole Instrument
	if (shouldReplaceWholeInstrument) {
		//newInstrument->loadAllSamples(true); // There'll be no samples cos it's new and blank
		currentSong->backUpParamManager(
		    (ModControllableAudio*)newInstrument->toModControllable(), NULL, &newParamManager,
		    true); // This is how we feed a ParamManager into the replaceInstrument() function
		currentSong->replaceInstrument((Instrument*)getCurrentClip()->output, newInstrument, false);
	}

	// Or if just adding new Instrument
	else {
		int error = getCurrentClip()->changeInstrument(modelStack, newInstrument, &newParamManager,
		                                               INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED, NULL,
		                                               false); // There'll be no samples cos it's new and blank
		// TODO: deal with errors

		currentSong->addOutput(newInstrument);
	}

	newInstrument->editedByUser = true;
	newInstrument->existsOnCard = false;

	if (newInstrumentType == INSTRUMENT_TYPE_KIT) {

		// If we weren't a Kit already...
		if (oldInstrumentType != INSTRUMENT_TYPE_KIT) {
			getCurrentClip()->yScroll = 0;
		}

		// Or if we were...
		else {
			getCurrentClip()->ensureScrollWithinKitBounds();
		}
	}

	view.instrumentChanged(modelStack, newInstrument);

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E060", "H060");

	setLedStates();

	newInstrument->name.set(&newName);

#if HAVE_OLED
	renderUIsForOled();
#else
	redrawNumericDisplay();
#endif
}

void InstrumentClipMinder::setLedStates() {
	IndicatorLEDs::setLedState(synthLedX, synthLedY, getCurrentClip()->output->type == INSTRUMENT_TYPE_SYNTH);
	IndicatorLEDs::setLedState(kitLedX, kitLedY, getCurrentClip()->output->type == INSTRUMENT_TYPE_KIT);
	IndicatorLEDs::setLedState(midiLedX, midiLedY, getCurrentClip()->output->type == INSTRUMENT_TYPE_MIDI_OUT);
	IndicatorLEDs::setLedState(cvLedX, cvLedY, getCurrentClip()->output->type == INSTRUMENT_TYPE_CV);

	IndicatorLEDs::setLedState(crossScreenEditLedX, crossScreenEditLedY, getCurrentClip()->wrapEditing);
	IndicatorLEDs::setLedState(scaleModeLedX, scaleModeLedY, getCurrentClip()->isScaleModeClip());
	IndicatorLEDs::setLedState(backLedX, backLedY, false);

#ifdef currentClipStatusButtonX
	view.drawCurrentClipPad(getCurrentClip());
#endif

	view.setLedStates();
	playbackHandler.setLedStates();

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	if (getCurrentClip()->output->type == INSTRUMENT_TYPE_KIT) {
		if (getCurrentClip()->affectEntire) IndicatorLEDs::blinkLed(clipViewLedX, clipViewLedY);
		else IndicatorLEDs::setLedState(clipViewLedX, clipViewLedY, true);
	}

	else {
		if (getCurrentUI() == &keyboardScreen) IndicatorLEDs::blinkLed(clipViewLedX, clipViewLedY);
		else IndicatorLEDs::setLedState(clipViewLedX, clipViewLedY, true);
	}
#endif
}

void InstrumentClipMinder::opened() {
}

void InstrumentClipMinder::focusRegained() {
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(getCurrentClip());
#if !HAVE_OLED
	redrawNumericDisplay();
#endif
}

int InstrumentClipMinder::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	// If holding save button...
	if (currentUIMode == UI_MODE_HOLDING_SAVE_BUTTON && on) {
		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		currentUIMode = UI_MODE_NONE;
		IndicatorLEDs::setLedState(saveLedX, saveLedY, false);

		if (x == synthButtonX && y == synthButtonY) {
			if (getCurrentClip()->output->type == INSTRUMENT_TYPE_SYNTH) {
yesSaveInstrument:
				openUI(&saveInstrumentPresetUI);
			}
		}

		else if (x == kitButtonX && y == kitButtonY) {
			if (getCurrentClip()->output->type == INSTRUMENT_TYPE_KIT) {
				goto yesSaveInstrument;
			}
		}
	}

	// If holding load button...
	else if (currentUIMode == UI_MODE_HOLDING_LOAD_BUTTON && on) {
		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		currentUIMode = UI_MODE_NONE;
		IndicatorLEDs::setLedState(loadLedX, loadLedY, false);

		if (x == synthButtonX && y == synthButtonY) {
			Browser::instrumentTypeToLoad = INSTRUMENT_TYPE_SYNTH;

yesLoadInstrument:
			loadInstrumentPresetUI.instrumentToReplace = (Instrument*)getCurrentClip()->output;
			loadInstrumentPresetUI.instrumentClipToLoadFor = getCurrentClip();
			openUI(&loadInstrumentPresetUI);
		}

		else if (x == kitButtonX && y == kitButtonY) {
			if (getCurrentClip()->onKeyboardScreen) {
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
				IndicatorLEDs::indicateAlertOnLed(keyboardLedX, keyboardLedX);
#endif
			}
			else {
				Browser::instrumentTypeToLoad = INSTRUMENT_TYPE_KIT;
				goto yesLoadInstrument;
			}
		}
	}

	// Select button, without shift
	else if (x == selectEncButtonX && y == selectEncButtonY && !Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			if (!soundEditor.setup(currentSong->currentClip)) return ACTION_RESULT_DEALT_WITH;
			openUI(&soundEditor);
		}
	}

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	// Affect-entire
	else if (x == affectEntireButtonX && y == affectEntireButtonY) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (getCurrentClip()->output->type == INSTRUMENT_TYPE_KIT) {
				if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

				getCurrentClip()->affectEntire = !getCurrentClip()->affectEntire;
				view.setActiveModControllableTimelineCounter(getCurrentClip());
			}
		}
	}
#endif

	// Back button to clear Clip
	else if (x == backButtonX && y == backButtonY && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			// Clear Clip
			Action* action = actionLogger.getNewAction(ACTION_CLIP_CLEAR, false);

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, currentSong->currentClip);

			getCurrentClip()->clear(action, modelStack);
			numericDriver.displayPopup(HAVE_OLED ? "Clip cleared" : "CLEAR");
			if (getCurrentUI() == &instrumentClipView) {
				uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
			}
		}
	}

	// Which-instrument-type buttons
	else if (x == synthButtonX && y == synthButtonY) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (Buttons::isNewOrShiftButtonPressed()) createNewInstrument(INSTRUMENT_TYPE_SYNTH);
			else changeInstrumentType(INSTRUMENT_TYPE_SYNTH);
		}
	}

	else if (x == midiButtonX && y == midiButtonY) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			changeInstrumentType(INSTRUMENT_TYPE_MIDI_OUT);
		}
	}

	else if (x == cvButtonX && y == cvButtonY) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			changeInstrumentType(INSTRUMENT_TYPE_CV);
		}
	}

	else return ClipMinder::buttonAction(x, y, on);

	return ACTION_RESULT_DEALT_WITH;
}

void InstrumentClipMinder::changeInstrumentType(int newInstrumentType) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	bool success = view.changeInstrumentType(newInstrumentType, modelStack);

	if (success) setLedStates(); // Might need to change the scale LED's state
}

void InstrumentClipMinder::calculateDefaultRootNote() {
	// If there are any other Clips in scale-mode, we use their root note
	if (currentSong->anyScaleModeClips()) defaultRootNote = currentSong->rootNote;

	// Otherwise, intelligently guess the root note
	else defaultRootNote = getCurrentClip()->guessRootNote(currentSong, currentSong->rootNote);
}

void InstrumentClipMinder::drawActualNoteCode(int16_t noteCode) {
	int octave = (noteCode) / 12 - 2;
	int noteCodeWithinOctave = (uint16_t)(noteCode + 120) % (uint8_t)12;

	char noteName[5];
	noteName[0] = noteCodeToNoteLetter[noteCodeWithinOctave];
	char* writePos = &noteName[1];
#if HAVE_OLED
	if (noteCodeIsSharp[noteCodeWithinOctave]) {
		*writePos = '#';
		writePos++;
	}
#endif
	intToString(octave, writePos, 1);

#if HAVE_OLED
	OLED::popupText(noteName, true);
#else
	uint8_t drawDot = noteCodeIsSharp[noteCodeWithinOctave] ? 0 : 255;
	numericDriver.setText(noteName, false, drawDot, true);
#endif
}

void InstrumentClipMinder::cycleThroughScales() {
	int newScale = currentSong->cycleThroughScales();
	if (newScale >= NUM_PRESET_SCALES)
		numericDriver.displayPopup(HAVE_OLED ? "Custom scale with more than 7 notes in use" : "CANT");
	else displayScaleName(newScale);
}

void InstrumentClipMinder::displayScaleName(int scale) {
	if (scale >= NUM_PRESET_SCALES) numericDriver.displayPopup(HAVE_OLED ? "Other scale" : "OTHER");
	else numericDriver.displayPopup(presetScaleNames[scale]);
}

void InstrumentClipMinder::displayCurrentScaleName() {
	displayScaleName(currentSong->getCurrentPresetScale());
}

// Returns whether currentClip is now active on Output / Instrument
bool InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(ModelStack* modelStack) {
	bool clipIsActiveOnInstrument = (getCurrentClip()->isActiveOnOutput());

	if (!clipIsActiveOnInstrument) {
		if (currentPlaybackMode->isOutputAvailable(getCurrentClip()->output)) {
			getCurrentClip()->output->setActiveClip(modelStack->addTimelineCounter(getCurrentClip()));
			clipIsActiveOnInstrument = true;
		}
	}

	return clipIsActiveOnInstrument;
}
