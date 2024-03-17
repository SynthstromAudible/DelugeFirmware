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

#include "model/clip/instrument_clip_minder.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/save/save_kit_row_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/led/indicator_leds.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_transpose.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "playback/mode/arrangement.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_instrument.h"
#include "util/lookuptables/lookuptables.h"
#include <cstring>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

int16_t InstrumentClipMinder::defaultRootNote;
bool InstrumentClipMinder::exitScaleModeOnButtonRelease;
bool InstrumentClipMinder::flashDefaultRootNoteOn;
uint8_t InstrumentClipMinder::editingMIDICCForWhichModKnob;

InstrumentClipMinder::InstrumentClipMinder() {
}

void InstrumentClipMinder::selectEncoderAction(int32_t offset) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
		if (editingMIDICCForWhichModKnob < kNumPhysicalModKnobs) {
			MIDIInstrument* instrument = (MIDIInstrument*)getCurrentOutput();
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(instrument, &getCurrentInstrumentClip()->paramManager);

			int32_t newCC;

			if (!Buttons::isButtonPressed(deluge::hid::button::SELECT_ENC)) {
				newCC = instrument->changeControlNumberForModKnob(offset, editingMIDICCForWhichModKnob,
				                                                  instrument->modKnobMode);
				view.setKnobIndicatorLevels();
			}
			else {
				newCC = instrument->moveAutomationToDifferentCC(offset, editingMIDICCForWhichModKnob,
				                                                instrument->modKnobMode, modelStackWithThreeMainThings);
				if (newCC == -1) {
					display->displayPopup(
					    deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_FURTHER_UNUSED_MIDI_PARAMS));
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
	if (display->have7SEG()) {
		if (getCurrentUI()->toClipMinder()) { // Seems a redundant check now? Maybe? Or not?
			view.displayOutputName(getCurrentOutput(), false);
		}
	}
}

void InstrumentClipMinder::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	view.displayOutputName(getCurrentOutput(), false);
}

// GCC is fine with 29 or 5 for the size, but does not like that it could be either
#pragma GCC push
#pragma GCC diagnostic ignored "-Wstack-usage="
void InstrumentClipMinder::drawMIDIControlNumber(int32_t controlNumber, bool automationExists) {

	char buffer[display->haveOLED() ? 30 : 5];
	bool finish = false;
	if (controlNumber == CC_NUMBER_NONE) {
		strcpy(buffer, deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_PARAM));
	}
	else if (controlNumber == CC_NUMBER_PITCH_BEND) {
		strcpy(buffer, deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
	}
	else if (controlNumber == CC_NUMBER_AFTERTOUCH) {
		strcpy(buffer, deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
	}
	else if (controlNumber == CC_NUMBER_Y_AXIS) {
		// in mono expression this is mod wheel, and y-axis is not directly controllable
		strcpy(buffer, deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
	}
	else {
		buffer[0] = 'C';
		buffer[1] = 'C';
		if (display->haveOLED()) {
			buffer[2] = ' ';
			intToString(controlNumber, &buffer[3]);
		}
		else {
			char* numberStartPos = (controlNumber < 100) ? (buffer + 2) : (buffer + 1);
			intToString(controlNumber, numberStartPos);
		}
	}

	if (display->haveOLED()) {
		if (automationExists) {
			strcat(buffer, "\n(automated)");
		}
		display->popupText(buffer);
	}
	else {
		display->setText(buffer, true, automationExists ? 3 : 255, false);
	}
}
#pragma GCC pop
void InstrumentClipMinder::createNewInstrument(OutputType newOutputType, bool is_dx) {
	Error error;

	OutputType oldOutputType = getCurrentOutputType();

	bool shouldReplaceWholeInstrument = currentSong->shouldOldOutputBeReplaced(getCurrentInstrumentClip());

	String newName;
	char const* thingName = (newOutputType == OutputType::SYNTH) ? "SYNT" : "KIT";
	error = Browser::currentDir.set(getInstrumentFolder(newOutputType));
	if (error != Error::NONE) {
gotError:
		display->displayError(error);
		return;
	}

	error = loadInstrumentPresetUI.getUnusedSlot(newOutputType, &newName, thingName);
	if (error != Error::NONE) {
		goto gotError;
	}

	if (newName.isEmpty()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_FURTHER_UNUSED_INSTRUMENT_NUMBERS));
		return;
	}

	ParamManagerForTimeline newParamManager;
	Instrument* newInstrument = storageManager.createNewInstrument(newOutputType, &newParamManager);
	if (!newInstrument) {
		error = Error::INSUFFICIENT_RAM;
		goto gotError;
	}

	// Set dirPath.
	error = newInstrument->dirPath.set(getInstrumentFolder(newOutputType));
	if (error != Error::NONE) {
		void* toDealloc = dynamic_cast<void*>(newInstrument);
		newInstrument->~Instrument();
		delugeDealloc(toDealloc);
		goto gotError;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E059", "H059");

	getCurrentInstrumentClip()->backupPresetSlot();

	if (newOutputType == OutputType::KIT) {
		display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NEW_KIT_CREATED));
	}
	else {
		if (is_dx) {
			display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NEW_FM_SYNTH_CREATED));
		}
		else {
			display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NEW_SYNTH_CREATED));
		}
	}

	if (newOutputType == OutputType::SYNTH) {
		((SoundInstrument*)newInstrument)->setupAsBlankSynth(&newParamManager, is_dx);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If replacing whole Instrument
	if (shouldReplaceWholeInstrument) {
		// newInstrument->loadAllSamples(true); // There'll be no samples cos it's new and blank
		//  This is how we feed a ParamManager into the replaceInstrument() function
		currentSong->backUpParamManager((ModControllableAudio*)newInstrument->toModControllable(), NULL,
		                                &newParamManager, true);
		currentSong->replaceInstrument(getCurrentInstrument(), newInstrument, false);
	}

	// Or if just adding new Instrument
	else {
		// There'll be no samples cos it's new and blank
		// TODO: deal with errors
		Error error = getCurrentInstrumentClip()->changeInstrument(
		    modelStack, newInstrument, &newParamManager, InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, NULL, false);

		currentSong->addOutput(newInstrument);
	}

	newInstrument->editedByUser = true;
	newInstrument->existsOnCard = false;

	if (newOutputType == OutputType::KIT) {
		// If we weren't a Kit already...
		if (oldOutputType != OutputType::KIT) {
			getCurrentInstrumentClip()->yScroll = 0;
		}

		// Or if we were...
		else {
			getCurrentInstrumentClip()->ensureScrollWithinKitBounds();
		}
	}

	view.instrumentChanged(modelStack, newInstrument);

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E060", "H060");

	setLedStates();

	newInstrument->name.set(&newName);

	if (is_dx) {
		soundEditor.setup(getCurrentInstrumentClip(), &dxMenu, 0);
		openUI(&soundEditor);
	}

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		redrawNumericDisplay();
	}
}

void InstrumentClipMinder::displayOrLanguageChanged() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		redrawNumericDisplay();
	}
}

void InstrumentClipMinder::setLedStates() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, getCurrentOutputType() == OutputType::SYNTH);
	indicator_leds::setLedState(IndicatorLED::KIT, getCurrentOutputType() == OutputType::KIT);
	indicator_leds::setLedState(IndicatorLED::MIDI, getCurrentOutputType() == OutputType::MIDI_OUT);
	indicator_leds::setLedState(IndicatorLED::CV, getCurrentOutputType() == OutputType::CV);

	// cross screen editing doesn't currently work in automation view, so don't light it up
	if (getCurrentUI() != &automationView) {
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, getCurrentInstrumentClip()->wrapEditing);
	}
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, getCurrentInstrumentClip()->isScaleModeClip());
	indicator_leds::setLedState(IndicatorLED::BACK, false);

#ifdef currentClipStatusButtonX
	view.drawCurrentClipPad(getCurrentInstrumentClip());
#endif

	view.setLedStates();
	playbackHandler.setLedStates();
}

void InstrumentClipMinder::opened() {
}

void InstrumentClipMinder::focusRegained() {
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(getCurrentInstrumentClip());
	MIDITranspose::exitScaleModeForMIDITransposeClips();
	if (display->have7SEG()) {
		redrawNumericDisplay();
	}
}

ActionResult InstrumentClipMinder::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	// If holding save button...
	if (currentUIMode == UI_MODE_HOLDING_SAVE_BUTTON && on) {
		currentUIMode = UI_MODE_NONE;
		indicator_leds::setLedState(IndicatorLED::SAVE, false);

		if (b == SYNTH && getCurrentOutputType() == OutputType::SYNTH
		    || b == KIT && getCurrentOutputType() == OutputType::KIT
		    || b == MIDI && getCurrentOutputType() == OutputType::MIDI_OUT) {
			openUI(&saveInstrumentPresetUI);
		}
	}

	// If holding load button...
	else if (currentUIMode == UI_MODE_HOLDING_LOAD_BUTTON && on) {
		currentUIMode = UI_MODE_NONE;
		indicator_leds::setLedState(IndicatorLED::LOAD, false);
		OutputType out;
		if (b == SYNTH) {
			out = OutputType::SYNTH;
		}
		else if (b == KIT) {
			out = OutputType::KIT;
		}
		else if (b == MIDI) {
			out = OutputType::MIDI_OUT;
		}
		loadInstrumentPresetUI.setupLoadInstrument(out, getCurrentInstrument(), getCurrentInstrumentClip());
		openUI(&loadInstrumentPresetUI);
	}

	// Select button, without shift
	else if (b == SELECT_ENC && !Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if ((getCurrentOutputType() == OutputType::KIT) && (getCurrentInstrumentClip()->affectEntire)) {
				soundEditor.setupKitGlobalFXMenu = true;
			}

			if (!soundEditor.setup(getCurrentClip())) {
				return ActionResult::DEALT_WITH;
			}
			openUI(&soundEditor);
		}
	}

	// Affect-entire
	else if (b == AFFECT_ENTIRE) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (getCurrentOutputType() == OutputType::KIT) {

				getCurrentInstrumentClip()->affectEntire = !getCurrentInstrumentClip()->affectEntire;
				view.setActiveModControllableTimelineCounter(getCurrentInstrumentClip());
			}
		}
	}

	// Back button to clear Clip
	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (on) {
			// Clear Clip
			Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, getCurrentClip());

			getCurrentInstrumentClip()->clear(action, modelStack);

			// New community feature as part of Automation Clip View Implementation
			// If this is enabled, then when you are in a regular Instrument Clip View (Synth, Kit, MIDI, CV), clearing
			// a clip will only clear the Notes (automations remain intact). If this is enabled, if you want to clear
			// automations, you will enter Automation Clip View and clear the clip there. If this is enabled, the
			// message displayed on the OLED screen is adjusted to reflect the nature of what is being cleared

			if (FlashStorage::automationClear) {
				if (getCurrentUI() == &automationView) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_CLEARED));
					uiNeedsRendering(&automationView, 0xFFFFFFFF, 0);
				}
				else if (getCurrentUI() == &instrumentClipView) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTES_CLEARED));
					uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
				}
			}
			else {
				if (getCurrentUI() == &instrumentClipView) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_CLEARED));
					uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
				}
				else if (getCurrentUI() == &automationView) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_CLIP_CLEARED));
					uiNeedsRendering(&automationView, 0xFFFFFFFF, 0);
				}
			}
		}
	}

	// Which-instrument-type buttons
	else if (b == SYNTH) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (Buttons::isButtonPressed(MOD7)) { // FM
				createNewInstrument(OutputType::SYNTH, true);
			}
			else if (Buttons::isNewOrShiftButtonPressed()) {
				createNewInstrument(OutputType::SYNTH);
			}
			else {
				changeOutputType(OutputType::SYNTH);
			}
		}
	}

	else if (b == MIDI) {
		if (on && currentUIMode == UI_MODE_NONE) {
			changeOutputType(OutputType::MIDI_OUT);
		}
	}

	else if (b == CV) {
		if (on && currentUIMode == UI_MODE_NONE) {
			changeOutputType(OutputType::CV);
		}
	}

	else {
		return ClipMinder::buttonAction(b, on);
	}

	return ActionResult::DEALT_WITH;
}

void InstrumentClipMinder::changeOutputType(OutputType newOutputType) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	bool success = view.changeOutputType(newOutputType, modelStack);

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
		defaultRootNote = getCurrentInstrumentClip()->guessRootNote(currentSong, currentSong->rootNote);
	}
}

void InstrumentClipMinder::drawActualNoteCode(int16_t noteCode) {
	char noteName[5];
	int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
	noteCodeToString(noteCode, noteName, &isNatural);

	if (display->haveOLED()) {
		display->popupTextTemporary(noteName);
	}
	else {
		uint8_t drawDot = !isNatural ? 0 : 255;
		display->setText(noteName, false, drawDot, true);
	}
}

void InstrumentClipMinder::cycleThroughScales() {
	int32_t newScale = currentSong->cycleThroughScales();
	if (newScale >= NUM_PRESET_SCALES) {
		display->displayPopup(
		    deluge::l10n::get(deluge::l10n::String::STRING_FOR_CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES_IN_USE));
	}
	else {
		displayScaleName(newScale);
	}
}

// Returns if the scale could be changed or not
bool InstrumentClipMinder::setScale(int32_t newScale) {
	int32_t calculatedScale = currentSong->setPresetScale(newScale);
	if (calculatedScale >= NUM_PRESET_SCALES) {
		if (display->haveOLED() && newScale < NUM_PRESET_SCALES) {
			DEF_STACK_STRING_BUF(popupMsg, 100);
			popupMsg.append(presetScaleNames[newScale]);
			popupMsg.append(":\n");
			popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_CHANGE_SCALE));
			display->displayPopup(popupMsg.c_str());
		}
		else {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_CHANGE_SCALE));
		}
		return false;
	}
	else {
		displayScaleName(newScale);
	}
	return true;
}

void InstrumentClipMinder::displayScaleName(int32_t scale) {
	if (scale >= NUM_PRESET_SCALES) {
		// Other scale
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_OTHER_SCALE));
	}
	else {
		// Preset scale
		display->displayPopup(presetScaleNames[scale]);
	}
}

void InstrumentClipMinder::displayCurrentScaleName() {
	displayScaleName(currentSong->getCurrentPresetScale());
}

// Returns whether currentClip is now active on Output / Instrument
bool InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(ModelStack* modelStack) {
	bool clipIsActiveOnInstrument = (getCurrentInstrumentClip()->isActiveOnOutput());

	if (!clipIsActiveOnInstrument) {
		if (currentPlaybackMode->isOutputAvailable(getCurrentOutput())) {
			getCurrentOutput()->setActiveClip(modelStack->addTimelineCounter(getCurrentInstrumentClip()));
			clipIsActiveOnInstrument = true;
		}
	}

	return clipIsActiveOnInstrument;
}
