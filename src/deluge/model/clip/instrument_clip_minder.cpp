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

#include "gui/views/arranger_view.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip_minder.h"
#include "gui/views/instrument_clip_view.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "definitions.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"
#include "processing/engines/cv_engine.h"
#include "model/drum/kit.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence.h"
#include "model/action/action.h"
#include <string.h>
#include "playback/mode/session.h"
#include "io/debug/print.h"
#include "model/instrument/midi_instrument.h"
#include "model/instrument/cv_instrument.h"
#include "memory/general_memory_allocator.h"
#include "playback/mode/arrangement.h"
#include "modulation/midi/midi_param.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "io/midi/midi_engine.h"
#include "storage/storage_manager.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "model/song/song.h"
#include "hid/led/indicator_leds.h"
#include "hid/buttons.h"
#include "model/model_stack.h"
#include "model/clip/clip_minder.h"
#include "model/clip/instrument_clip.h"
#include "modulation/midi/midi_param_collection.h"

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

extern "C" {
#include "util/cfunctions.h"
#include "RZA1/uart/sio_char.h"
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

			if (!Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
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
	if (controlNumber == CC_NUMBER_NONE) {
		strcpy(buffer, HAVE_OLED ? "No param" : "NONE");
	}
	else if (controlNumber == CC_NUMBER_PITCH_BEND) {
		strcpy(buffer, HAVE_OLED ? "Pitch bend" : "BEND");
	}
	else if (controlNumber == CC_NUMBER_AFTERTOUCH) {
		strcpy(buffer, HAVE_OLED ? "Channel pressure" : "AFTE");
	}
	else {
		buffer[0] = 'C';
		buffer[1] = 'C';
#if HAVE_OLED
		buffer[2] = ' ';
		intToString(controlNumber, &buffer[3]);
	}
	if (automationExists) {
		strcat(buffer, "\n(automated)");
	}
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

	error = loadInstrumentPresetUI.getUnusedSlot(newInstrumentType, &newName, thingName);
	if (error) {
		goto gotError;
	}

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
	indicator_leds::setLedState(IndicatorLED::SYNTH, getCurrentClip()->output->type == INSTRUMENT_TYPE_SYNTH);
	indicator_leds::setLedState(IndicatorLED::KIT, getCurrentClip()->output->type == INSTRUMENT_TYPE_KIT);
	indicator_leds::setLedState(IndicatorLED::MIDI, getCurrentClip()->output->type == INSTRUMENT_TYPE_MIDI_OUT);
	indicator_leds::setLedState(IndicatorLED::CV, getCurrentClip()->output->type == INSTRUMENT_TYPE_CV);

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, getCurrentClip()->wrapEditing);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, getCurrentClip()->isScaleModeClip());
	indicator_leds::setLedState(IndicatorLED::BACK, false);

#ifdef currentClipStatusButtonX
	view.drawCurrentClipPad(getCurrentClip());
#endif

	view.setLedStates();
	playbackHandler.setLedStates();
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

int InstrumentClipMinder::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	// If holding save button...
	if (currentUIMode == UI_MODE_HOLDING_SAVE_BUTTON && on) {
		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		currentUIMode = UI_MODE_NONE;
		indicator_leds::setLedState(IndicatorLED::SAVE, false);

		if (b == SYNTH) {
			if (getCurrentClip()->output->type == INSTRUMENT_TYPE_SYNTH) {
yesSaveInstrument:
				openUI(&saveInstrumentPresetUI);
			}
		}

		else if (b == KIT) {
			if (getCurrentClip()->output->type == INSTRUMENT_TYPE_KIT) {
				goto yesSaveInstrument;
			}
		}
	}

	// If holding load button...
	else if (currentUIMode == UI_MODE_HOLDING_LOAD_BUTTON && on) {
		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		currentUIMode = UI_MODE_NONE;
		indicator_leds::setLedState(IndicatorLED::LOAD, false);

		if (b == SYNTH) {
			Browser::instrumentTypeToLoad = INSTRUMENT_TYPE_SYNTH;

yesLoadInstrument:
			loadInstrumentPresetUI.instrumentToReplace = (Instrument*)getCurrentClip()->output;
			loadInstrumentPresetUI.instrumentClipToLoadFor = getCurrentClip();
			openUI(&loadInstrumentPresetUI);
		}

		else if (b == KIT) {
			if (getCurrentClip()->onKeyboardScreen) {
				indicator_leds::indicateAlertOnLed(IndicatorLED::KEYBOARD);
			}
			else {
				Browser::instrumentTypeToLoad = INSTRUMENT_TYPE_KIT;
				goto yesLoadInstrument;
			}
		}
	}

	// Select button, without shift
	else if (b == SELECT_ENC && !Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (!soundEditor.setup(currentSong->currentClip)) {
				return ACTION_RESULT_DEALT_WITH;
			}
			openUI(&soundEditor);
		}
	}

	// Affect-entire
	else if (b == AFFECT_ENTIRE) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (getCurrentClip()->output->type == INSTRUMENT_TYPE_KIT) {
				if (inCardRoutine) {
					return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				getCurrentClip()->affectEntire = !getCurrentClip()->affectEntire;
				view.setActiveModControllableTimelineCounter(getCurrentClip());
			}
		}
	}

	// Back button to clear Clip
	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (on) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

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
	else if (b == SYNTH) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (Buttons::isNewOrShiftButtonPressed()) {
				createNewInstrument(INSTRUMENT_TYPE_SYNTH);
			}
			else {
				changeInstrumentType(INSTRUMENT_TYPE_SYNTH);
			}
		}
	}

	else if (b == MIDI) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			changeInstrumentType(INSTRUMENT_TYPE_MIDI_OUT);
		}
	}

	else if (b == CV) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			changeInstrumentType(INSTRUMENT_TYPE_CV);
		}
	}

	else {
		return ClipMinder::buttonAction(b, on);
	}

	return ACTION_RESULT_DEALT_WITH;
}

void InstrumentClipMinder::changeInstrumentType(int newInstrumentType) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	bool success = view.changeInstrumentType(newInstrumentType, modelStack);

	if (success) {
		setLedStates(); // Might need to change the scale LED's state
	}
}

void InstrumentClipMinder::calculateDefaultRootNote() {
	// If there are any other Clips in scale-mode, we use their root note
	if (currentSong->anyScaleModeClips()) {
		defaultRootNote = currentSong->rootNote;

		// Otherwise, intelligently guess the root note
	}
	else {
		defaultRootNote = getCurrentClip()->guessRootNote(currentSong, currentSong->rootNote);
	}
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
	if (newScale >= NUM_PRESET_SCALES) {
		numericDriver.displayPopup(HAVE_OLED ? "Custom scale with more than 7 notes in use" : "CANT");
	}
	else {
		displayScaleName(newScale);
	}
}

void InstrumentClipMinder::displayScaleName(int scale) {
	if (scale >= NUM_PRESET_SCALES) {
		numericDriver.displayPopup(HAVE_OLED ? "Other scale" : "OTHER");
	}
	else {
		numericDriver.displayPopup(presetScaleNames[scale]);
	}
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
