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
#include "gui/ui/load/load_midi_device_definition_ui.h"
#include "gui/ui/load/load_pattern_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/save/save_kit_row_ui.h"
#include "gui/ui/save/save_midi_device_definition_ui.h"
#include "gui/ui/save/save_pattern_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
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
#include "model/scale/preset_scales.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "playback/mode/arrangement.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_instrument.h"
#include "util/lookuptables/lookuptables.h"
#include <cstring>
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

int16_t InstrumentClipMinder::defaultRootNote;
bool InstrumentClipMinder::toggleScaleModeOnButtonRelease;
uint32_t InstrumentClipMinder::scaleButtonPressTime;
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
	if (stemExport.processStarted) {
		return;
	}
	if (display->have7SEG()) {
		if (getCurrentUI()->toClipMinder()) { // Seems a redundant check now? Maybe? Or not?
			view.displayOutputName(getCurrentOutput(), false);
		}
	}
}

void InstrumentClipMinder::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	if (stemExport.processStarted) {
		stemExport.displayStemExportProgressOLED(StemExportType::DRUM);
		return;
	}
	view.displayOutputName(getCurrentOutput(), false, getCurrentClip());
}

// GCC is fine with 29 or 5 for the size, but does not like that it could be either
#pragma GCC push
#pragma GCC diagnostic ignored "-Wstack-usage="
void InstrumentClipMinder::drawMIDIControlNumber(int32_t controlNumber, bool automationExists) {

	DEF_STACK_STRING_BUF(buffer, 30);

	bool doScroll = false;

	if (controlNumber == CC_NUMBER_NONE) {
		buffer.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_PARAM));
	}
	else if (controlNumber == CC_NUMBER_PITCH_BEND) {
		buffer.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
	}
	else if (controlNumber == CC_NUMBER_AFTERTOUCH) {
		buffer.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
	}
	else if (controlNumber == CC_NUMBER_Y_AXIS) {
		// in mono expression this is mod wheel, and y-axis is not directly controllable
		buffer.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
	}
	else {
		MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();
		bool appendedName = false;

		if (controlNumber >= 0 && controlNumber < kNumRealCCNumbers) {
			std::string_view name = midiInstrument->getNameFromCC(controlNumber);
			// if we have a name for this midi cc set by the user, display that instead of the cc number
			if (!name.empty()) {
				buffer.append(name.data());
				doScroll = name.size() > 4;
				appendedName = true;
			}
		}

		// if we don't have a midi cc name set, draw CC number instead
		if (!appendedName) {
			if (display->haveOLED()) {
				buffer.append("CC ");
				buffer.appendInt(controlNumber);
			}
			else {
				if (controlNumber < 100) {
					buffer.append("CC");
				}
				else {
					buffer.append("C");
				}
				buffer.appendInt(controlNumber);
			}
		}
	}

	if (display->haveOLED()) {
		if (automationExists) {
			buffer.append("\n");
			buffer.append("(automated)");
		}
		display->popupText(buffer.c_str());
	}
	else {
		if (doScroll) {
			display->setScrollingText(buffer.c_str(), 0, 600, -1, automationExists ? 3 : 255);
		}
		else {
			display->setText(buffer.c_str(), true, automationExists ? 3 : 255, false);
		}
	}
}
#pragma GCC pop
bool InstrumentClipMinder::createNewInstrument(OutputType newOutputType, bool is_dx) {
	Error error;

	OutputType oldOutputType = getCurrentOutputType();

	InstrumentClip* clip = getCurrentInstrumentClip();

	// don't allow clip type change if clip is not empty
	// only impose this restriction if switching to/from kit clip
	if ((oldOutputType != newOutputType) && ((oldOutputType == OutputType::KIT) || (newOutputType == OutputType::KIT))
	    && (!clip->isEmpty() || !clip->output->isEmpty())) {
		return false;
	}

	bool shouldReplaceWholeInstrument = currentSong->shouldOldOutputBeReplaced(clip);

	String newName;
	char const* thingName = (newOutputType == OutputType::SYNTH) ? "SYNT" : "KIT";
	error = Browser::currentDir.set(getInstrumentFolder(newOutputType));
	if (error != Error::NONE) {
gotError:
		display->displayError(error);
		return false;
	}

	error = loadInstrumentPresetUI.getUnusedSlot(newOutputType, &newName, thingName);
	if (error != Error::NONE) {
		goto gotError;
	}

	if (newName.isEmpty()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_FURTHER_UNUSED_INSTRUMENT_NUMBERS));
		return false;
	}

	ParamManagerForTimeline newParamManager;
	Instrument* newInstrument = StorageManager::createNewInstrument(newOutputType, &newParamManager);
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

	clip->backupPresetSlot();

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
		currentSong->backUpParamManager((ModControllableAudio*)newInstrument->toModControllable(), nullptr,
		                                &newParamManager, true);
		currentSong->replaceInstrument(getCurrentInstrument(), newInstrument, false);
	}

	// Or if just adding new Instrument
	else {
		// There'll be no samples cos it's new and blank
		// TODO: deal with errors
		Error error = clip->changeInstrument(modelStack, newInstrument, &newParamManager,
		                                     InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, nullptr, false);

		currentSong->addOutput(newInstrument);
	}

	newInstrument->editedByUser = true;
	newInstrument->existsOnCard = false;

	if (newOutputType == OutputType::KIT) {
		// If we weren't a Kit already...
		if (oldOutputType != OutputType::KIT) {
			clip->yScroll = 0;
		}

		// Or if we were...
		else {
			clip->ensureScrollWithinKitBounds();
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

	return true;
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
	OutputType outputType = getCurrentOutputType();

	indicator_leds::setLedState(IndicatorLED::SYNTH, outputType == OutputType::SYNTH);
	indicator_leds::setLedState(IndicatorLED::KIT, outputType == OutputType::KIT);
	indicator_leds::setLedState(IndicatorLED::MIDI, outputType == OutputType::MIDI_OUT);
	indicator_leds::setLedState(IndicatorLED::CV, outputType == OutputType::CV);

	InstrumentClip* clip = getCurrentInstrumentClip();

	bool inAutomationView = ((getCurrentUI() == &automationView) && !automationView.inNoteEditor());
	if (!inAutomationView) {
		// only light cross screen led up if you're in the automation view note editor or outside automation view
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, clip->wrapEditing);
	}

	if (outputType != OutputType::KIT) {
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, clip->isScaleModeClip());
	}
	indicator_leds::setLedState(IndicatorLED::BACK, false);

#ifdef currentClipStatusButtonX
	view.drawCurrentClipPad(clip);
#endif

	view.setLedStates();
	playbackHandler.setLedStates();
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

		if (getCurrentOutputType() == OutputType::MIDI_OUT && (b == MOD_ENCODER_0 || b == MOD_ENCODER_1)) {
			openUI(&saveMidiDeviceDefinitionUI);
		}
		else if (b == X_ENC) {
			// New Tracks have not Drum selected -> abort Drum action
			if (getCurrentOutputType() == OutputType::KIT && !getRootUI()->getAffectEntire()
			    && (getCurrentKit() == nullptr || getCurrentKit()->selectedDrum == nullptr)) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_PATTERN_NODRUM));
				return ActionResult::DEALT_WITH;
			}

			openUI(&savePatternUI);
		}
		else if ((b == SYNTH && getCurrentOutputType() == OutputType::SYNTH)
		         || (b == KIT && getCurrentOutputType() == OutputType::KIT)
		         || (b == MIDI && getCurrentOutputType() == OutputType::MIDI_OUT)) {
			openUI(&saveInstrumentPresetUI);
		}
	}

	// If holding load button...
	else if (currentUIMode == UI_MODE_HOLDING_LOAD_BUTTON && on) {
		if (getCurrentOutputType() == OutputType::MIDI_OUT && (b == MOD_ENCODER_0 || b == MOD_ENCODER_1)) {
			currentUIMode = UI_MODE_NONE;
			indicator_leds::setLedState(IndicatorLED::LOAD, false);
			openUI(&loadMidiDeviceDefinitionUI);
		}
		else if (b == X_ENC) {
			currentUIMode = UI_MODE_NONE;
			indicator_leds::setLedState(IndicatorLED::LOAD, false);
			// Need to stop Playback before loading a new Pattern to prevent Stuck notes on big Midi files
			playbackHandler.endPlayback();

			// New Tracks have not Drum selected -> abort Drum action
			if (getCurrentOutputType() == OutputType::KIT && !getRootUI()->getAffectEntire()
			    && (getCurrentKit() == nullptr || getCurrentKit()->selectedDrum == nullptr)) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_PATTERN_NODRUM));
				return ActionResult::DEALT_WITH;
			}

			openUI(&loadPatternUI);
			if (Buttons::isButtonPressed(deluge::hid::button::CROSS_SCREEN_EDIT)) {
				// Setup for gently pasting notes
				loadPatternUI.setupLoadPatternUI(false, false);
			}
			else if (Buttons::isButtonPressed(deluge::hid::button::SCALE_MODE)) {
				// Setup for keeping original Scale on paste
				loadPatternUI.setupLoadPatternUI(true, true);
			}
			else {
				// Default Load
				loadPatternUI.setupLoadPatternUI();
			}
		}
		else if (b == SYNTH || b == KIT || b == MIDI) {
			currentUIMode = UI_MODE_NONE;
			indicator_leds::setLedState(IndicatorLED::LOAD, false);
			OutputType newOutputType;
			if (b == SYNTH) {
				newOutputType = OutputType::SYNTH;
			}
			else if (b == KIT) {
				newOutputType = OutputType::KIT;
			}
			else if (b == MIDI) {
				newOutputType = OutputType::MIDI_OUT;
			}
			InstrumentClip* clip = getCurrentInstrumentClip();
			Output* output = clip->output;
			OutputType oldOutputType = output->type;
			Instrument* instrument = (Instrument*)output;
			// don't allow clip type change if clip is not empty
			// only impose this restriction if switching to/from kit clip
			if (!((oldOutputType != newOutputType)
			      && ((oldOutputType == OutputType::KIT) || (newOutputType == OutputType::KIT))
			      && (!clip->isEmpty() || !clip->output->isEmpty()))) {
				loadInstrumentPresetUI.setupLoadInstrument(newOutputType, instrument, clip);
				openUI(&loadInstrumentPresetUI);
			}
		}
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

			UI* currentUI = getCurrentUI();

			// If you're in Automation View, only clear automation if you're not in the Note Editor
			// or also clear Automation when default setting to only clear automation in Automation View is false
			bool clearAutomation = ((currentUI == &automationView && !automationView.inNoteEditor())
			                        || (currentUI != &automationView && !FlashStorage::automationClear));

			// If you're in Automation View, only clear Notes and MPE if you're in the Note Editor
			// Always clear Notes and MPE when you're not in Automation View
			bool clearSequenceAndMPE =
			    ((currentUI != &automationView) || (currentUI == &automationView && automationView.inNoteEditor()));

			getCurrentInstrumentClip()->clear(action, modelStack, clearAutomation, clearSequenceAndMPE);

			// New default as part of Automation Clip View Implementation
			// If this is enabled, then when you are in a regular Instrument Clip View (Synth, Kit, MIDI, CV), clearing
			// a clip will only clear the Notes (automations remain intact). If this is enabled, if you want to clear
			// automations, you will enter Automation Clip View and clear the clip there. If this is enabled, the
			// message displayed on the OLED screen is adjusted to reflect the nature of what is being cleared

			// if you're in automation view but not in the note editor, you're clearing non-MPE automation
			if (currentUI == &automationView) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_CLEARED));
			}
			// if you're not in automation view and automationClear default is on, you're only clearing Notes and MPE
			else if (FlashStorage::automationClear) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTES_CLEARED));
			}
			// if you're not in automation view and automationClear default is off, you're clearing everything
			else {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_CLIP_CLEARED));
			}

			uiNeedsRendering(currentUI, 0xFFFFFFFF, 0);
		}
	}

	// Which-instrument-type buttons
	else if (b == SYNTH) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (Buttons::isButtonPressed(MOD7)) { // FM
				createNewInstrument(OutputType::SYNTH, true);
			}
			else if (Buttons::isShiftButtonPressed()) {
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

bool InstrumentClipMinder::changeOutputType(OutputType newOutputType) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	bool success = view.changeOutputType(newOutputType, modelStack);

	if (success) {
		setLedStates(); // Might need to change the scale LED's state
	}

	return success;
}

void InstrumentClipMinder::calculateDefaultRootNote() {
	// If there are any other Clips in scale-mode, we use their root note
	if (currentSong->anyScaleModeClips()) {
		defaultRootNote = currentSong->key.rootNote;

		// Otherwise, intelligently guess the root note
	}
	else {
		defaultRootNote = getCurrentInstrumentClip()->guessRootNote(currentSong, currentSong->key.rootNote);
	}
}

void InstrumentClipMinder::drawActualNoteCode(int16_t noteCode) {
	// If we're in Chords mode, don't display the note name because the Chord class will display the chord name
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip->onKeyboardScreen
	    && ((clip->keyboardState.currentLayout == KeyboardLayoutType::KeyboardLayoutTypeChordLibrary)
	        || (clip->keyboardState.currentLayout == KeyboardLayoutType::KeyboardLayoutTypeChord))) {
		return;
	}

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
	displayScaleName(currentSong->cycleThroughScales());
}

// Returns if the scale could be changed or not
bool InstrumentClipMinder::setScale(Scale newScale) {
	Scale result = currentSong->setScale(newScale);
	if (result == NO_SCALE) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_CHANGE_SCALE));
		return false;
	}
	else {
		displayScaleName(result);
		return true;
	}
}

void InstrumentClipMinder::displayScaleName(Scale scale) {
	display->displayPopup(getScaleName(scale));
}

void InstrumentClipMinder::displayCurrentScaleName() {
	displayScaleName(currentSong->getCurrentScale());
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
