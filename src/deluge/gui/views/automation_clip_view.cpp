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

#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/numeric_driver.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/consequence/consequence_note_row_mute.h"
#include "model/drum/drum.h"
#include "model/drum/kit.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/note/copied_note_row.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <new>
#include <string.h>

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

//#define comingSoonMenu (MenuItem*)0xFFFFFFFF

AutomationClipView automationClipView{};

AutomationClipView::AutomationClipView() {

	numEditPadPresses = 0;

	for (int i = 0; i < kEditPadPressBufferSize; i++) {
		editPadPresses[i].isActive = false;
	}

	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		numEditPadPressesPerNoteRowOnScreen[yDisplay] = 0;
		lastAuditionedVelocityOnScreen[yDisplay] = 255;
		auditionPadIsPressed[yDisplay] = 0;
	}

	auditioningSilently = false;
	timeLastEditPadPress = 0;
	//newDrumOptionSelected = false;
	firstCopiedNoteRow = NULL;
	lastSelectedParamID = 255;
	lastSelectedParamX = 255;
	lastSelectedParamY = 255;
	lastSelectedParamArrayPosition = 0;
	lastSelectedMidiCC = 255;
	lastSelectedMidiX = 255;
	lastSelectedMidiY = 255;
	lastEditPadPressXDisplay = 255;
	clipClear = 0;
	drawLine = 0;
	flashShortcuts = 0;
	notePassthrough = 0;
	overlayNotes = 0;
	interpolateOn = 0;

}

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

const uint32_t paramsForAutomation[41] = {
		::Param::Global::VOLUME_POST_FX, ::Param::Local::PITCH_ADJUST, ::Param::Local::PAN, //Master Volume, Pan, Pitch
		::Param::Local::LPF_FREQ, ::Param::Local::LPF_RESONANCE, //LPF Cutoff, Resonance
		::Param::Local::HPF_FREQ, ::Param::Local::HPF_RESONANCE, //HPF Cutoff, Resonance
		::Param::Global::REVERB_AMOUNT, //Reverb Amount
		::Param::Global::DELAY_RATE, ::Param::Global::DELAY_FEEDBACK, //Delay Rate, Feedback
		::Param::Global::VOLUME_POST_REVERB_SEND, //Sidechain Send
		::Param::Local::OSC_A_VOLUME, ::Param::Local::OSC_A_PITCH_ADJUST, ::Param::Local::OSC_A_PHASE_WIDTH,
			::Param::Local::CARRIER_0_FEEDBACK, ::Param::Local::OSC_A_WAVE_INDEX, //OSC 1 Volume, Pitch, Phase Width, Carrier Feedback, Wave Index
		::Param::Local::OSC_B_VOLUME, ::Param::Local::OSC_B_PITCH_ADJUST, ::Param::Local::OSC_B_PHASE_WIDTH,
			::Param::Local::CARRIER_1_FEEDBACK, ::Param::Local::OSC_B_WAVE_INDEX, //OSC 2 Volume, Pitch, Phase Width, Carrier Feedback, Wave Index
		::Param::Local::MODULATOR_0_VOLUME, ::Param::Local::MODULATOR_0_PITCH_ADJUST, ::Param::Local::MODULATOR_0_FEEDBACK, //FM Mod 1 Volume, Pitch, Feedback
		::Param::Local::MODULATOR_1_VOLUME, ::Param::Local::MODULATOR_1_PITCH_ADJUST, ::Param::Local::MODULATOR_1_FEEDBACK, //FM Mod 2 Volume, Pitch, Feedback
		::Param::Local::ENV_0_ATTACK, ::Param::Local::ENV_0_DECAY, ::Param::Local::ENV_0_SUSTAIN, ::Param::Local::ENV_0_RELEASE, //Env 1 ADSR
		::Param::Local::ENV_1_ATTACK, ::Param::Local::ENV_1_DECAY, ::Param::Local::ENV_1_SUSTAIN, ::Param::Local::ENV_1_RELEASE, //Env 2 ADSR
		::Param::Global::LFO_FREQ, ::Param::Local::LFO_LOCAL_FREQ, //LFO 1 Freq, LFO 2 Freq
		::Param::Global::MOD_FX_DEPTH, ::Param::Global::MOD_FX_RATE, //Mod FX Depth, Rate
		::Param::Global::ARP_RATE, //Arp Rate
		::Param::Local::NOISE_VOLUME //Noise
};

const uint32_t paramShortcutsForAutomation[16][8] = {
	    // Post V3
	    {0xFFFFFFFF, 0xFFFFFFFFL, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
	    {::Param::Local::OSC_A_VOLUME, ::Param::Local::OSC_A_PITCH_ADJUST, 0xFFFFFFFF, ::Param::Local::OSC_A_PHASE_WIDTH, 0xFFFFFFFF, ::Param::Local::CARRIER_0_FEEDBACK,
	    		::Param::Local::OSC_A_WAVE_INDEX, ::Param::Local::NOISE_VOLUME},
	    {::Param::Local::OSC_B_VOLUME, ::Param::Local::OSC_B_PITCH_ADJUST, 0xFFFFFFFF, ::Param::Local::OSC_B_PHASE_WIDTH, 0xFFFFFFFF, ::Param::Local::CARRIER_1_FEEDBACK,
	    		::Param::Local::OSC_B_WAVE_INDEX, 0xFFFFFFFF},
	    {::Param::Local::MODULATOR_0_VOLUME, ::Param::Local::MODULATOR_0_PITCH_ADJUST, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	    		::Param::Local::MODULATOR_0_FEEDBACK, 0xFFFFFFFF, 0xFFFFFFFF},
	    {::Param::Local::MODULATOR_1_VOLUME, ::Param::Local::MODULATOR_1_PITCH_ADJUST, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	    		::Param::Local::MODULATOR_1_FEEDBACK, 0xFFFFFFFF, 0xFFFFFFFF},
	    {::Param::Global::VOLUME_POST_FX, 0xFFFFFFFF, ::Param::Local::PITCH_ADJUST, ::Param::Local::PAN, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
	    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
	    {::Param::Local::ENV_0_RELEASE, ::Param::Local::ENV_0_SUSTAIN, ::Param::Local::ENV_0_DECAY, ::Param::Local::ENV_0_ATTACK, 0xFFFFFFFF, 0xFFFFFFFF, ::Param::Local::LPF_RESONANCE, ::Param::Local::LPF_FREQ},
	    {::Param::Local::ENV_1_RELEASE, ::Param::Local::ENV_1_SUSTAIN, ::Param::Local::ENV_1_DECAY, ::Param::Local::ENV_1_ATTACK, 0xFFFFFFFF, 0xFFFFFFFF, ::Param::Local::HPF_RESONANCE, ::Param::Local::HPF_FREQ},
	    {0xFFFFFFFF, 0xFFFFFFFF, ::Param::Global::VOLUME_POST_REVERB_SEND, 0xFFFFFFFF,
	    		0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
	    {::Param::Global::ARP_RATE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	    		0xFFFFFFFF},
	    {::Param::Global::LFO_FREQ, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, ::Param::Global::MOD_FX_DEPTH,
	    		::Param::Global::MOD_FX_RATE},
	    {::Param::Local::LFO_LOCAL_FREQ, 0xFFFFFFFF, 0xFFFFFFFF, ::Param::Global::REVERB_AMOUNT, 0xFFFFFFFF, 0xFFFFFFFF,
	    		0xFFFFFFFF, 0xFFFFFFFF},
	    {::Param::Global::DELAY_RATE, 0xFFFFFFFF, 0xFFFFFFFF, ::Param::Global::DELAY_FEEDBACK, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}
};

const uint32_t midiCCShortcutsForAutomation[16][8] = {
		    // Post V3
		    {112, 96, 80, 64, 48, 32, 16, 0},
			{113, 97, 81, 65, 49, 33, 17, 1},
			{114, 98, 82, 66, 50, 34, 18, 2},
			{115, 99, 83, 67, 51, 35, 19, 3},
			{116, 100, 84, 68, 52, 36, 20, 4},
			{117, 101, 85, 69, 53, 37, 21, 5},
			{118, 102, 86, 70, 54, 38, 22, 6},
			{119, 103, 87, 71, 55, 39, 23, 7},
			{0xFFFFFFFF, 104, 88, 72, 56, 40, 24, 8},
			{0xFFFFFFFF, 105, 89, 73, 57, 41, 25, 9},
			{0xFFFFFFFF, 106, 90, 74, 58, 42, 26, 10},
			{0xFFFFFFFF, 107, 91, 75, 59, 43, 27, 11},
			{0xFFFFFFFF, 108, 92, 76, 60, 44, 28, 12},
			{0xFFFFFFFF, 109, 93, 77, 61, 45, 29, 13},
			{120, 110, 94, 78, 62, 46, 30, 14},
			{121, 111, 95, 79, 63, 47, 31, 15}
};

const uint32_t padShortcutsForInterpolation[16][8] = { 0 };

bool AutomationClipView::opened() {

	char const* displayText;
	displayText = "opened";
	numericDriver.displayPopup(displayText);

	openedInBackground();

	InstrumentClipMinder::opened();

	focusRegained();

	return true;
}

// Initializes some stuff to begin a new editing session
void AutomationClipView::focusRegained() {

	char const* displayText;
	displayText = "focusRegained";
	numericDriver.displayPopup(displayText);

	ClipView::focusRegained();

	auditioningSilently = false; // Necessary?

	InstrumentClipMinder::focusRegained();

	setLedStates();
}

void AutomationClipView::openedInBackground() {

	getCurrentClip()->onAutomationClipView = true;
	getCurrentClip()->onKeyboardScreen = false;

	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	recalculateColours();

	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	AudioEngine::logAction("AutomationClipView::beginSession 2");

	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight],
		               true);
		renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight]);
	}
	else {
		uiNeedsRendering(this);
	}
}

void AutomationClipView::setLedStates() {
	char const* displayText;
	displayText = "setLedStates";
	numericDriver.displayPopup(displayText);

	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);
	InstrumentClipMinder::setLedStates();
}

ActionResult AutomationClipView::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

//	char const* displayText;
//	displayText = "buttonAction";
//	numericDriver.displayPopup(displayText);

	// Scale mode button
	if (b == SCALE_MODE) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// Kits can't do scales!
		if (currentSong->currentClip->output->type == InstrumentType::KIT) {
			if (on) {
				indicator_leds::indicateAlertOnLed(IndicatorLED::KIT);
			}
			return ActionResult::DEALT_WITH;
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
					instrumentClipView.exitScaleModeOnButtonRelease = true;
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
				instrumentClipView.enterScaleMode(lastAuditionedYDisplay);
			}
		}
		else {
			if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
				currentUIMode = UI_MODE_NONE;
				if (getCurrentClip()->inScaleMode) {
					if (instrumentClipView.exitScaleModeOnButtonRelease) {
						instrumentClipView.exitScaleMode();
					}
				}
				else {
					instrumentClipView.enterScaleMode();
				}
			}
		}
	}

	// Song view button
	else if (b == SESSION_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentSong->lastClipInstanceEnteredStartPos != -1
			    || currentSong->currentClip->isArrangementOnlyClip()) {
				bool success = arrangerView.transitionToArrangementEditor();
				if (!success) {
					goto doOther;
				}
			}
			else {
doOther:
				instrumentClipView.transitionToSessionView();
			}
		}
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			changeRootUI(&keyboardScreen);
		}
	}

	// Clip button - exit mode
	else if (b == CLIP_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			changeRootUI(&instrumentClipView);
		}
	}

	// Wrap edit button
	else if (b == CROSS_SCREEN_EDIT) {
		if (on) {
			if (currentUIMode == UI_MODE_NONE) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				if (getCurrentClip()->wrapEditing) {
					getCurrentClip()->wrapEditing = false;
				}
				else {
					getCurrentClip()->wrapEditLevel = currentSong->xZoom[NAVIGATION_CLIP] * kDisplayWidth;
					// Ensure that there are actually multiple screens to edit across
					if (getCurrentClip()->wrapEditLevel < currentSong->currentClip->loopLength) {
						getCurrentClip()->wrapEditing = true;
					}
				}

				setLedStates();
			}
		}
	}

	// Load / kit button if auditioning
	else if (currentUIMode == UI_MODE_AUDITIONING && ((b == LOAD) || (b == KIT))
	         && (!playbackHandler.isEitherClockActive() || !playbackHandler.ticksLeftInCountIn)) {

		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// Auditioning drum
			if (currentSong->currentClip->output->type == InstrumentType::KIT) {
				instrumentClipView.cutAuditionedNotesToOne();
				int noteRowIndex;
				NoteRow* noteRow =
				    getCurrentClip()->getNoteRowOnScreen(lastAuditionedYDisplay, currentSong, &noteRowIndex);
				instrumentClipView.cancelAllAuditioning();
				if (noteRow->drum) {
					noteRow->drum->drumWontBeRenderedForAWhile();
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithNoteRow* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory)->addNoteRow(noteRowIndex, noteRow);

				instrumentClipView.enterDrumCreator(modelStack, false);
			}

			// Auditioning synth
			if (currentSong->currentClip->output->type == InstrumentType::SYNTH) {
				instrumentClipView.cancelAllAuditioning();

				bool success = soundEditor.setup(getCurrentClip(), &menu_item::fileSelectorMenu,
				                                 0); // Can't fail because we just set the selected Drum
				if (success) {
					openUI(&soundEditor);
				}
			}
		}
	}

	// Kit button. Unlike the other instrument-type buttons, whose code is in InstrumentClipMinder, this one is only allowed in the KeyboardScreen
	else if (b == KIT && currentUIMode == UI_MODE_NONE) {
		lastSelectedParamID = 255;

		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (Buttons::isNewOrShiftButtonPressed()) {
				instrumentClipView.createNewInstrument(InstrumentType::KIT);
			//	lastEditPadPressXDisplay = 255;
			}
			else {
				instrumentClipView.changeInstrumentType(InstrumentType::KIT);
			//	lastEditPadPressXDisplay = 255;
			}
		}
	}

	else if (b == KIT) {
		lastSelectedParamID = 255;
	}

	else if (b == SYNTH && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) { //this gets triggered when you change an existing clip to synth / create a new synth clip in song mode
				if (Buttons::isNewOrShiftButtonPressed()) {
					instrumentClipView.createNewInstrument(InstrumentType::SYNTH);
				//	lastEditPadPressXDisplay = 255;
				}
				else { //this gets triggered when you change clip type to synth from within inside clip view
					instrumentClipView.changeInstrumentType(InstrumentType::SYNTH);
				//	lastEditPadPressXDisplay = 255;
				}
			}
		//	else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING) {
		//		createDrumForAuditionedNoteRow(DrumType::SOUND);
		//	}
		}
	}

	else if (b == SYNTH) {
		lastSelectedParamID = 255;
	}

	else if (b == MIDI) {
		lastSelectedMidiCC = 255;

		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				instrumentClipView.changeInstrumentType(InstrumentType::MIDI_OUT);
			//	lastEditPadPressXDisplay = 255;
			}
		//	else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING) {
		//		createDrumForAuditionedNoteRow(DrumType::MIDI);
		//	}
		}
	}

	else if (b == CV) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				instrumentClipView.changeInstrumentType(InstrumentType::CV);
			//	lastEditPadPressXDisplay = 255;
			}
		//	else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING) {
		//		createDrumForAuditionedNoteRow(DrumType::GATE);
		//	}
		}
	}

	else if (b == SAVE && Buttons::isShiftButtonPressed()) {

		InstrumentClip* clip = getCurrentClip();
		Instrument* instrument = (Instrument*)clip->output;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		if (clip->output->type == InstrumentType::SYNTH) {
			//ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);
			//if (modelStackWithThreeMainThings) {
			//	ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();
			//	if (summary) {
			//		ParamSet* paramSet = (ParamSet*)summary->paramCollection;
			//		ModelStackWithAutoParam* modelStackWithParam = modelStackWithThreeMainThings->addParam(paramSet, summary, lastSelectedParamID, &paramSet->params[lastSelectedParamID]);

			ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_DELETE, false);
				modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

				numericDriver.displayPopup(HAVE_OLED ? "Automation deleted" : "DELETED");
				uiNeedsRendering(this);
			}
		}
		else if (clip->output->type == InstrumentType::MIDI_OUT) {
			//ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);
			//if (modelStackWithThreeMainThings) {
			//	ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();
			//	if (summary) {
			//		ParamSet* paramSet = (ParamSet*)summary->paramCollection;
			//		ModelStackWithAutoParam* modelStackWithParam = modelStackWithThreeMainThings->addParam(paramSet, summary, lastSelectedParamID, &paramSet->params[lastSelectedParamID]);

			ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_DELETE, false);
				modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

				numericDriver.displayPopup(HAVE_OLED ? "Automation deleted" : "DELETED");
				uiNeedsRendering(this);
			}
		}
	}

	// Horizontal encoder button
	else if (b == X_ENC) {

		// If user wants to "multiple" Clip contents
		if (on && Buttons::isShiftButtonPressed() && !isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
			if (isNoUIModeActive()) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

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
	else if (b == Y_ENC) {
		//needed for Automation
		if (on){
			if (interpolateOn == 0) {
				interpolateOn = 1;

				char* displayText;
				displayText = "Interpolation On";
				numericDriver.displayPopup(displayText);

			}
			else {
				interpolateOn = 0;

				char* displayText;
				displayText = "Interpolation Off";
				numericDriver.displayPopup(displayText);
			}
		}
		//needed for Automation
	}

	//Select encoder + Shift Button Pressed
//	else if (b == SELECT_ENC) {

//		char const* displayText;
//		displayText = "Select Encoder";
//		numericDriver.displayPopup(displayText);

//	}

	else {
passToOthers:

		ActionResult result = InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		if (result != ActionResult::NOT_DEALT_WITH) {
			return result;
		}

		return ClipView::buttonAction(b, on, inCardRoutine);


	}

	uiNeedsRendering(this);
	return ActionResult::DEALT_WITH;
}

void AutomationClipView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	// If they want to copy or paste automation...
	if (Buttons::isButtonPressed(hid::button::LEARN)) {
		if (on && currentSong->currentClip->output->type != InstrumentType::CV) {
			if (Buttons::isShiftButtonPressed()) {
				pasteAutomation(whichModEncoder);
			}
			else {
				copyAutomation(whichModEncoder);
			}
		}
	}
	//needed for Automation
/*	else {
		if (whichModEncoder == 0 && on){
			if (interpolateOn == 0) {
				interpolateOn = 1;

				char* displayText;
				displayText = "Interpolation On";
				numericDriver.displayPopup(displayText);

			}
			else {
				interpolateOn = 0;

				char* displayText;
				displayText = "Interpolation Off";
				numericDriver.displayPopup(displayText);
			}
		}

		else if (whichModEncoder == 1 && on) {
			if (currentSong->currentClip->output->type == InstrumentType::SYNTH || currentSong->currentClip->output->type == InstrumentType::KIT) {

				if (lastSelectedParamID != 255) {
					lastSelectedParamID = 255;
					lastSelectedParamX = 255;
					lastEditPadPressXDisplay = 255;

					uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
				}

			}
			else if (currentSong->currentClip->output->type == InstrumentType::MIDI_OUT) {

				if (lastSelectedMidiCC != 255) {
					lastSelectedMidiCC = 255;
					lastSelectedMidiX = 255;
					lastEditPadPressXDisplay = 255;

					uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
				}
			}
		//	uiNeedsRendering(this);
		}
		//needed for Automation

	}*/
	else {
		view.modEncoderButtonAction(whichModEncoder, on);
	}
//	uiNeedsRendering(this);
}

void AutomationClipView::copyAutomation(int whichModEncoder) {
	if (copiedParamAutomation.nodes) {
		generalMemoryAllocator.dealloc(copiedParamAutomation.nodes);
		copiedParamAutomation.nodes = NULL;
		copiedParamAutomation.numNodes = 0;
	}

	int startPos = getPosFromSquare(0);
	int endPos = getPosFromSquare(kDisplayWidth);
	if (startPos == endPos) {
		return;
	}

	if (!view.activeModControllableModelStack.modControllable) {
		return;
	}

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

void AutomationClipView::pasteAutomation(int whichModEncoder) {
	if (!copiedParamAutomation.nodes) {
		numericDriver.displayPopup(HAVE_OLED ? "No automation to paste" : "NONE");
		return;
	}

	int startPos = getPosFromSquare(0);
	int endPos = getPosFromSquare(kDisplayWidth);

	int pastedAutomationWidth = endPos - startPos;
	if (pastedAutomationWidth == 0) {
		return;
	}

	float scaleFactor = (float)pastedAutomationWidth / copiedParamAutomation.width;

	if (!view.activeModControllableModelStack.modControllable) {
		return;
	}

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
	if (playbackHandler.isEitherClockActive()) {
		currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
	}
}

void AutomationClipView::doubleClipLengthAction() {

	// If too big...
	if (currentSong->currentClip->loopLength > (kMaxSequenceLength >> 1)) {
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

void AutomationClipView::selectEncoderAction(int8_t offset) {

	// User may be trying to edit noteCode...
	if (currentUIMode == UI_MODE_AUDITIONING) {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {

			if (playbackHandler.isEitherClockActive() && playbackHandler.ticksLeftInCountIn) {
				return;
			}

			instrumentClipView.cutAuditionedNotesToOne();
			instrumentClipView.offsetNoteCodeAction(offset);
		}
	}

	// Or set / create a new Drum
	else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			drumForNewNoteRow = instrumentClipView.flipThroughAvailableDrums(offset, drumForNewNoteRow, true);
			//setSelectedDrum(drumForNewNoteRow); // Can't - it doesn't have a NoteRow, and so we don't really know where its ParamManager is!
			instrumentClipView.drawDrumName(drumForNewNoteRow);
		}
	}

	//needed for Automation
	//If the user is holding down shift while turning select, change midi CC or param ID
	else if (Buttons::isShiftButtonPressed()) {

		lastSelectedParamX = 255;
		lastSelectedMidiX = 255;

		if (currentSong->currentClip->output->type == InstrumentType::SYNTH || currentSong->currentClip->output->type == InstrumentType::KIT) {

			if (lastSelectedParamID == 255) {
				lastSelectedParamID = paramsForAutomation[0];
				lastSelectedParamArrayPosition = 0;
			}
			else if ((lastSelectedParamArrayPosition + offset) < 0) {
				lastSelectedParamID = paramsForAutomation[40];
				lastSelectedParamArrayPosition = 40;
			}
			else if ((lastSelectedParamArrayPosition + offset) > 40) {
				lastSelectedParamID = paramsForAutomation[0];
				lastSelectedParamArrayPosition = 0;
			}
			else {
				lastSelectedParamID = paramsForAutomation[lastSelectedParamArrayPosition+offset];
				lastSelectedParamArrayPosition += offset;

			//	if (lastSelectedParamID == PARAM_LOCAL_VOLUME) { //removing this param from the lsit of automable params
			//		lastSelectedParamID++;
			//	}
			}

			numericDriver.displayPopup(getPatchedParamDisplayNameForOled(lastSelectedParamID));

			for (int x = 0; x < kDisplayWidth; x++) {
				for (int y = 0; y < kDisplayHeight; y++) {

					if (paramShortcutsForAutomation[x][y] == lastSelectedParamID) {

						lastSelectedParamX = x;
						lastSelectedParamY = y;

						goto flashShortcut;

					}
				}
			}
		}

		else if (currentSong->currentClip->output->type == InstrumentType::MIDI_OUT) {

			if (lastSelectedMidiCC == 255) {
				lastSelectedMidiCC = 0;
			}
			else if ((lastSelectedMidiCC + offset) < 0) {
				lastSelectedMidiCC = 121;
			}
			else if ((lastSelectedMidiCC + offset) > 121) {
				lastSelectedMidiCC = 0;
			}
			else {
				lastSelectedMidiCC += offset;
			}

			//bool automationExists = doesAutomationExistOnMIDIParam(modelStack, cc);
			InstrumentClipMinder::drawMIDIControlNumber(lastSelectedMidiCC, false);
			numericDriver.cancelPopup();

			for (int x = 0; x < kDisplayWidth; x++) {
				for (int y = 0; y < kDisplayHeight; y++) {

					if (midiCCShortcutsForAutomation[x][y] == lastSelectedMidiCC) {

						lastSelectedMidiX = x;
						lastSelectedMidiY = y;

						goto flashShortcut;

					}
				}
			}

			goto flashShortcut;
		}

		return;

		flashShortcut:

			if (lastSelectedParamX != 255) {
				soundEditor.setupShortcutBlink(lastSelectedParamX, lastSelectedParamY, 3);
				soundEditor.blinkShortcut();
			}
			else if (lastSelectedMidiX != 255) {
				soundEditor.setupShortcutBlink(lastSelectedMidiX, lastSelectedMidiY, 3);
				soundEditor.blinkShortcut();
			}
		//	else {
		//		uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
		//	}

			uiNeedsRendering(this);


			//needed for Automation
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


//needed for Automation

//straight line formula:
//A + (B-A)*T/(Distance between A and B)
//f(x) = A + (B-A)*T/(Distance between A and B)

int AutomationClipView::LERP (int A, int B, int T, int Distance) {

	int NewValue = 0;

	NewValue = (B - A) * T * 1000000;
	NewValue = NewValue / Distance;
	NewValue = NewValue / 1000000;
	NewValue = A + NewValue;

	return NewValue;

}

int AutomationClipView::LERPSweep (int A, int B, int T, int Distance) {

	int NewValue = 0;

	NewValue = (T * T) * 1000000;
	NewValue = NewValue / (Distance * Distance);
	NewValue = (B - A) * NewValue;
	NewValue = NewValue / 1000000;
	NewValue = A + NewValue;

	//A + (((((B-A)*(T*T)) << 2) / (Distance * Distance)) >> 2);

	//=(((1-(((((15-D30)^2)*1000000)/((15)^2))/1000000)^2)))

	return NewValue;
}

int AutomationClipView::LERPRoot (int A, int B, int T, int Distance) {

	int NewValue = 0;

	NewValue = (T * T) * 10000;
	NewValue = NewValue / (Distance * Distance);
	NewValue = NewValue * NewValue;
	NewValue = (10000 * 10000) - NewValue;
	NewValue = NewValue / 10000;
	NewValue = (B - A) * NewValue;
	NewValue = NewValue / 10000;
	NewValue = A + NewValue;

	return NewValue;

}

int AutomationClipView::LERPSweepDown (int A, int B, int T, int Distance) {

	int NewValue = A + (B-A)*(1-((T*T)/(Distance*Distance)));

	return NewValue;

}


int AutomationClipView::calculateKnobPosForMultiPadPress(int32_t xDisplay, int32_t firstPadX, int32_t firstPadValue, int32_t secondPadX, int32_t secondPadValue) {

	int newKnobPos = 0;

	if (xDisplay == firstPadX) {
		//set beg positon to value corresponding to Beginning Position Pad
		newKnobPos = firstPadValue;
	}

	else if (xDisplay == secondPadX) {
		//set end position to value corresponding to End Position Pad
		newKnobPos = secondPadValue;
	}

	else {
		//f(x) = A + (x - Ax) * ((B - A) / (Bx - Ax))
		newKnobPos = firstPadValue + (xDisplay - firstPadX)*((((secondPadValue - firstPadValue) * 1000000) / (secondPadX - firstPadX)) / 1000000);
	}

	newKnobPos = newKnobPos - 64;

	return newKnobPos;

}


int AutomationClipView::calculateKnobPosForSinglePadPress(int32_t yDisplay) {

	int newKnobPos = 0;

	if (yDisplay >= 0 && yDisplay < 7) {
		newKnobPos = yDisplay * 18;
	}
	else {
		newKnobPos = 127;
	}

	newKnobPos = newKnobPos - 64;

	return newKnobPos;

}

void AutomationClipView::setParameterAutomationValue(ModelStackWithAutoParam* modelStack, int32_t knobPos, int32_t squareStart, int32_t xDisplay, int32_t effectiveLength) {

	int32_t newValue =
		modelStack->paramCollection->knobPosToParamValue(knobPos, modelStack);

	uint32_t squareWidth = getSquareWidth(xDisplay, effectiveLength);

	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);

}

ModelStackWithAutoParam* AutomationClipView::getmodelStackWithParam (ModelStackWithTimelineCounter* modelStack, Clip* clip) {

	ModelStackWithAutoParam* modelStackWithParam = 0;

	if (clip->output->type == InstrumentType::SYNTH) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);

		if (modelStackWithThreeMainThings) {
			ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();

			if (summary) {
				ParamSet* paramSet = (ParamSet*)summary->paramCollection;
				modelStackWithParam = modelStackWithThreeMainThings->addParam(paramSet, summary, lastSelectedParamID, &paramSet->params[lastSelectedParamID]);
			}
		}
	}

	else if (clip->output->type == InstrumentType::MIDI_OUT) {

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);

		if (modelStackWithThreeMainThings) {
			modelStackWithParam = getParamToControlFromInputMIDIChannel(lastSelectedMidiCC, modelStackWithThreeMainThings);
		}
	}

	return modelStackWithParam;
}

void AutomationClipView::handleMultiPadPress(ModelStackWithTimelineCounter* modelStack, Clip* clip, int32_t firstPadX, int32_t firstPadY, int32_t secondPadX, int32_t secondPadY) {

	if (modelStack) {

		int firstPadValue = calculateKnobPosForSinglePadPress (firstPadY) + 64;
		int secondPadValue = calculateKnobPosForSinglePadPress (secondPadY) + 64;

		int DistanceBetweenPads = secondPadX - firstPadX;

		if (clip->output->type == InstrumentType::SYNTH) {

			ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

			if (modelStackWithParam) {

				for (int x = firstPadX; x <= secondPadX; x++) {

					uint32_t squareStart = getPosFromSquare(x);
					int effectiveLength = clip->loopLength;

					if (squareStart < effectiveLength) {

						int newKnobPos = calculateKnobPosForMultiPadPress (x, firstPadX, firstPadValue, secondPadX, secondPadValue);
						setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, x, effectiveLength);
					}
				}
			}
		}

		else if (clip->output->type == InstrumentType::MIDI_OUT) {

			ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

			if (modelStackWithParam) {

				for (int x = firstPadX; x <= secondPadX; x++) {

					uint32_t squareStart = getPosFromSquare(x);
					int effectiveLength = clip->loopLength;

					if (squareStart < effectiveLength) {

						int newKnobPos = calculateKnobPosForMultiPadPress (x, firstPadX, firstPadValue, secondPadX, secondPadValue);
						setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, x, effectiveLength);

					}
				}
			}
		}
	}

	uiNeedsRendering(this);

}

void AutomationClipView::handleSinglePadPress (ModelStackWithTimelineCounter* modelStack, Clip* clip, int32_t xDisplay, int32_t yDisplay) {

	if (clip->output->type == InstrumentType::SYNTH) { //|| clip->output->type == InstrumentType::MIDI_OUT){

		if (lastSelectedParamID == 255 && paramShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
			lastSelectedParamID = paramShortcutsForAutomation[xDisplay][yDisplay];
			numericDriver.displayPopup(getPatchedParamDisplayNameForOled(lastSelectedParamID));

			lastSelectedParamX = xDisplay;
			lastSelectedParamY = yDisplay;

			soundEditor.setupShortcutBlink(xDisplay,yDisplay,3);
			soundEditor.blinkShortcut();
		}

		else if (lastSelectedParamID != 255) {

			lastEditPadPressXDisplay = xDisplay;

			ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

			if (modelStackWithParam) {

				uint32_t squareStart = getPosFromSquare(xDisplay);
				int effectiveLength = clip->loopLength;
				if (squareStart < effectiveLength) {

					int newKnobPos = calculateKnobPosForSinglePadPress(yDisplay);
					setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength);

				}
			}
		}
	}

	else if (clip->output->type == InstrumentType::MIDI_OUT) {

		if (lastSelectedMidiCC == 255 && midiCCShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
			lastSelectedMidiCC = midiCCShortcutsForAutomation[xDisplay][yDisplay];
			InstrumentClipMinder::drawMIDIControlNumber(lastSelectedMidiCC, false); //cant use this cause it uses oled function

			lastSelectedMidiX = xDisplay;
			lastSelectedMidiY = yDisplay;

			soundEditor.setupShortcutBlink(xDisplay,yDisplay,3);
			soundEditor.blinkShortcut();
		}

		else if (lastSelectedMidiCC != 255) {

			lastEditPadPressXDisplay = xDisplay;

			ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

			if (modelStackWithParam) {

				uint32_t squareStart = getPosFromSquare(xDisplay);
				int effectiveLength = clip->loopLength;
				if (squareStart < effectiveLength) {

					int newKnobPos = calculateKnobPosForSinglePadPress(yDisplay);
					setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength);
				}
			}
		}
	}

	uiNeedsRendering(this);

}

//needed for Automation

ActionResult AutomationClipView::padAction(int x, int y, int velocity) {

//	char const* displayText;
//	displayText = "padAction";
//	numericDriver.displayPopup(displayText);

	if (x == 15 && y == 2 && velocity > 0
	    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::DrumRandomizer) == RuntimeFeatureStateToggle::On) {
		int numRandomized = 0;
		for (int i = 0; i < 8; i++) {
			if (getCurrentUI() == this && this->auditionPadIsPressed[i]) {
				if (currentSong->currentClip->output->type != InstrumentType::KIT) {
					continue;
				}
				AudioEngine::stopAnyPreviewing();
				Drum* drum = getCurrentClip()->getNoteRowOnScreen(i, currentSong)->drum;
				if (drum->type != DrumType::SOUND) {
					continue;
				}
				SoundDrum* soundDrum = (SoundDrum*)drum;
				MultiRange* r = soundDrum->sources[0].getRange(0);
				AudioFileHolder* afh = r->getAudioFileHolder();

				static int MaxFiles = 25;
				String fnArray[MaxFiles];
				char const* currentPathChars = afh->filePath.get();
				char const* slashAddress = strrchr(currentPathChars, '/');
				if (slashAddress) {
					int slashPos = (uint32_t)slashAddress - (uint32_t)currentPathChars;
					String dir;
					dir.set(&afh->filePath);
					dir.shorten(slashPos);
					FRESULT result = f_opendir(&staticDIR, dir.get());
					FilePointer thisFilePointer;
					int numSamples = 0;

					if (result != FR_OK) {
						numericDriver.displayError(ERROR_SD_CARD);
						return ActionResult::DEALT_WITH;
					}
					while (true) {
						result = f_readdir_get_filepointer(&staticDIR, &staticFNO,
						                                   &thisFilePointer); /* Read a directory item */
						if (result != FR_OK || staticFNO.fname[0] == 0) {
							break; // Break on error or end of dir
						}
						if (staticFNO.fname[0] == '.' || staticFNO.fattrib & AM_DIR
						    || !isAudioFilename(staticFNO.fname)) {
							continue; // Ignore dot entry
						}
						audioFileManager.loadAnyEnqueuedClusters();
						fnArray[numSamples].set(staticFNO.fname);
						numSamples++;
						if (numSamples >= MaxFiles) {
							break;
						}
					}

					if (numSamples >= 2) {
						soundDrum->unassignAllVoices();
						afh->setAudioFile(NULL);
						String filePath; //add slash
						filePath.set(&dir);
						int dirWithSlashLength = filePath.getLength();
						if (dirWithSlashLength) {
							filePath.concatenateAtPos("/", dirWithSlashLength);
							dirWithSlashLength++;
						}
						char const* fn = fnArray[random(numSamples - 1)].get();
						filePath.concatenateAtPos(fn, dirWithSlashLength);
						AudioEngine::stopAnyPreviewing();
						afh->filePath.set(&filePath);
						afh->loadFile(false, true, true, 1, 0, false);
						soundDrum->name.set(fn);
						numRandomized++;
						((Instrument*)currentSong->currentClip->output)->beenEdited();
					}
				}
			}
		}
		if (numRandomized > 0) {
			numericDriver.displayPopup(HAVE_OLED ? "Randomized" : "RND");
			return ActionResult::DEALT_WITH;
		}
	}

	// Edit pad action...
	if (x < kDisplayWidth) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

	//	char const* displayText;
	//	displayText = "padAction 2";
	//	numericDriver.displayPopup(displayText);

		// Perhaps the user wants to enter the SoundEditor via a shortcut. They can do this by holding an audition pad too - but this gets deactivated
		// if they've done any "euclidean" or per-NoteRow editing already by holding down that audition pad, because if they've done that, they're probably
		// not intending to deliberately go into the SoundEditor, but might be trying to edit notes. Which they currently can't do...
		if (velocity) { //&& (!isUIModeActive(UI_MODE_AUDITIONING) || !editedAnyPerNoteRowStuffSinceAuditioningBegan)) {

			bool flash = false;

			if (Buttons::isShiftButtonPressed()) { //need to check why it crashes when I press a non-shortcut pad.

				//shift + pad code goes here

				return ActionResult::DEALT_WITH;
			}
			else {
				goto doRegularEditPadActionProbably;
			}
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
	else if (x == kDisplayWidth) {
		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentSong->currentClip->output->type != InstrumentType::KIT) {
				return ActionResult::DEALT_WITH;
			}
			NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(y, currentSong);
			if (!noteRow || !noteRow->drum) {
				return ActionResult::DEALT_WITH;
			}
			view.noteRowMuteMidiLearnPadPressed(velocity, noteRow);
		}
		else if (currentSong->currentClip->output->type == InstrumentType::KIT && lastAuditionedYDisplay == y
		         && isUIModeActive(UI_MODE_AUDITIONING) && getNumNoteRowsAuditioning() == 1) {
			if (velocity) {
				if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
					enterUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
				}
				else {
					goto maybeRegularMutePress;
				}
			}
			else {
				if (isUIModeActive(UI_MODE_DRAGGING_KIT_NOTEROW)) {
					exitUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
				}
				else {
					goto maybeRegularMutePress;
				}
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
		if (x == kDisplayWidth + 1) {

			// "Learning" to this audition pad:
			if (isUIModeActiveExclusively(UI_MODE_MIDI_LEARN)) {
				if (getCurrentUI() == this) {
					if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					if (currentSong->currentClip->output->type == InstrumentType::KIT) {
						NoteRow* thisNoteRow = getCurrentClip()->getNoteRowOnScreen(y, currentSong);
						if (!thisNoteRow || !thisNoteRow->drum) {
							return ActionResult::DEALT_WITH;
						}
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
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				if (velocity
				    && currentSong->currentClip->output->type
				           != InstrumentType::KIT) { // We probably couldn't have got this far if it was a Kit, but let's just check
					if (getCurrentClip()->inScaleMode) {
						currentUIMode = UI_MODE_NONE; // So that the upcoming render of the sidebar comes out correctly
						instrumentClipView.changeRootNote(y);
						instrumentClipView.exitScaleModeOnButtonRelease = false;
					}
					else {
						instrumentClipView.enterScaleMode(y);
					}
				}
			}

			// Actual basic audition pad press:
			else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
				exitUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
				if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allowable sometimes if in card routine.
				}
				auditionPadAction(velocity, y, Buttons::isShiftButtonPressed());
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

uint8_t AutomationClipView::getEditPadPressXDisplayOnScreen(uint8_t yDisplay) {
	for (int i = 0; i < kEditPadPressBufferSize; i++) {
		if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay) {
			return editPadPresses[i].xDisplay;
		}
	}

	return 0; // Presumably impossible case
}

void AutomationClipView::editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, unsigned int xZoom) {

//	char const* displayText;
//	displayText = "editPadAction";
//	numericDriver.displayPopup(displayText);

	uint32_t squareStart = getPosFromSquare(xDisplay);

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If button down
	if (state) {

		// Don't allow further new presses if already done nudging
		if (numEditPadPresses && doneAnyNudgingSinceFirstEditPadPress) {

			char const* displayText;
			displayText = "editPadAction 1";
			numericDriver.displayPopup(displayText);

			return;
		}

		if (!isSquareDefined(xDisplay)) {

			char const* displayText;
			displayText = "editPadAction 2";
			numericDriver.displayPopup(displayText);

			return;
		}

		// Get existing NoteRow if there was one
		ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

		// If no NoteRow yet...
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) {

			// Just check we're not beyond Clip length
			if (squareStart >= clip->loopLength) {
				return;
			}

			// And create the new NoteRow
			modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(modelStack, yDisplay);
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				if (instrument->type == InstrumentType::KIT) {
					setSelectedDrum(NULL);
				}
				return;
			}

			// If that just created a new NoteRow for a Kit, then we can't undo any further back than this
			if (instrument->type == InstrumentType::KIT) {
				actionLogger.deleteAllLogs();
			}
		}

		int32_t effectiveLength = modelStackWithNoteRow->getLoopLength();

		// Now that we've definitely got a NoteRow, check against NoteRow "effective" length here (though it'll very possibly be the same as the Clip length we may have tested against above).
		if (squareStart >= effectiveLength) {
			return;
		}

		uint32_t squareWidth = getSquareWidth(xDisplay, effectiveLength);

		NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

		ParamManagerForTimeline* paramManager = NULL;
		if (instrument->type == InstrumentType::SYNTH) {
			paramManager = &clip->paramManager;	//for a synth the params are saved at clip level
		}
		else if (instrument->type == InstrumentType::KIT) {
			paramManager = &noteRow->paramManager; //for a kit, params are saved at note level
		}

		// If this is a note-length-edit press...
		//needed for Automation
		if (numEditPadPresses == 1 &&
		    ((int32_t)(timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0)) {

			int firstPadX = 255;
			int firstPadY = 255;


			// Find that original press
			int i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (editPadPresses[i].isActive) {

					firstPadX = editPadPresses[i].xDisplay;
					firstPadY = editPadPresses[i].yDisplay;

					break;
				}
			}

			if (firstPadX != 255 && firstPadY != 255) {
				handleMultiPadPress(modelStack, clip, firstPadX, firstPadY, xDisplay, yDisplay);
			}

		}
		//needed for Automation

		// Or, if this is a regular create-or-select press...
		else {

			timeLastEditPadPress = AudioEngine::audioSampleTimer;
			// Find an empty space in the press buffer, if there is one
			int i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (!editPadPresses[i].isActive) {
					break;
				}
			}
			if (i < kEditPadPressBufferSize) {

				ParamManagerForTimeline* paramManagerDummy;
				Sound* sound = instrumentClipView.getSoundForNoteRow(noteRow, &paramManagerDummy);

				uint32_t whichRowsToReRender = (1 << yDisplay);

				Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

				uint32_t desiredNoteLength = squareWidth;
				if (sound) {

					int yNote;

					if (instrument->type == InstrumentType::KIT) {
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
						while (!(desiredNoteLength & 1)) {
							desiredNoteLength >>= 1;
						}

						while (desiredNoteLength * 1.41 < sampleLengthInTicks) {
							desiredNoteLength <<= 1;
						}

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
					else if (instrument->type == InstrumentType::KIT) {
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

				// Otherwise, we've selected a pad
				else {


					//needed for Automation
					handleSinglePadPress(modelStack, clip, xDisplay, yDisplay);
					//needed for Automation

					shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;

					// If this is the first press, record the time
					if (numEditPadPresses == 0) {
						timeFirstEditPadPress = AudioEngine::audioSampleTimer;
						doneAnyNudgingSinceFirstEditPadPress = false;
						offsettingNudgeNumberDisplay = false;
						shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
					}

					if (squareType == SQUARE_BLURRED) { //this is how you delete the tails
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
					for (int m = 0; m < kNumExpressionDimensions; m++) {
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

							if (error) {
								numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
							}
						}
					}

					// Edit mod knob values for this Note's region
					int32_t distanceToNextNote = clip->getDistanceToNextNote(lastNote, modelStackWithNoteRow);

					if (instrument->type == InstrumentType::KIT) {
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
		for (i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay
			    && editPadPresses[i].xDisplay == xDisplay) {
				break;
			}
		}

		// If we found it...
		if (i < kEditPadPressBufferSize) {

			numericDriver.cancelPopup(); // Crude way of getting rid of the probability-editing permanent popup

			uint8_t velocity = editPadPresses[i].intendedVelocity;

			// Must mark it as inactive first, otherwise, the note-deletion code may do so and then we'd do it again here
			endEditPadPress(i);

			// If we're meant to be deleting it on depress...
		//	if (editPadPresses[i].deleteOnDepress
		//	    && AudioEngine::audioSampleTimer - timeLastEditPadPress < (44100 >> 1)) {

		//		ModelStackWithNoteRow* modelStackWithNoteRow =
		//		    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);

		//		Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

		//		NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

		//		int wrapEditLevel = clip->getWrapEditLevel();

		//		noteRow->clearArea(squareStart, getSquareWidth(xDisplay, modelStackWithNoteRow->getLoopLength()),
		//		                   modelStackWithNoteRow, action, wrapEditLevel);

		//		noteRow->clearMPEUpUntilNextNote(modelStackWithNoteRow, squareStart, wrapEditLevel, true);

		//		uiNeedsRendering(this, 1 << yDisplay, 0);
		//	}

			// Or if not deleting...
		//	else {
				instrument->defaultVelocity = velocity;
		//	}

			// Close last note nudge action, if there was one - so each such action is for one consistent set of notes
			actionLogger.closeAction(ACTION_NOTE_NUDGE);

			// If *all* presses are now ended
			checkIfAllEditPadPressesEnded();

			reassessAuditionStatus(yDisplay);
		}
	}
}

void AutomationClipView::endEditPadPress(uint8_t i) {
	editPadPresses[i].isActive = false;
	numEditPadPresses--;
	numEditPadPressesPerNoteRowOnScreen[editPadPresses[i].yDisplay]--;

	for (int m = 0; m < kNumExpressionDimensions; m++) {
		if (editPadPresses[i].stolenMPE[m].num) {
			generalMemoryAllocator.dealloc(editPadPresses[i].stolenMPE[m].nodes);
		}
	}
}

void AutomationClipView::checkIfAllEditPadPressesEnded(bool mayRenderSidebar) {
	if (numEditPadPresses == 0) {
		view.setModRegion();
		exitUIMode(UI_MODE_NOTES_PRESSED);
		actionLogger.closeAction(ACTION_NOTE_EDIT);
	}
}

void AutomationClipView::mutePadPress(uint8_t yDisplay) {

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
		if (clip->output->type == InstrumentType::KIT) {
fail:
			if (!wasStuttering) {
				setSelectedDrum(NULL);
			}
			return;
		}

		// Create new NoteRow.
		modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(modelStack, yDisplay);
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
			return;
		}
	}

	NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

	clip->toggleNoteRowMute(modelStackWithNoteRow);

	if (!wasStuttering && clip->output->type == InstrumentType::KIT) {
		setSelectedDrum(noteRow->drum);
	}

	uiNeedsRendering(this, 0, 1 << yDisplay);
}

ActionResult AutomationClipView::scrollVertical(int scrollAmount, bool inCardRoutine, bool draggingNoteRow) {

	char const* displayText;
	displayText = "scrollVertical";
	numericDriver.displayPopup(displayText);

	int noteRowToShiftI;
	int noteRowToSwapWithI;

	bool isKit = currentSong->currentClip->output->type == InstrumentType::KIT;

	// If a Kit...
	if (isKit) {
		// Limit scrolling
		if (scrollAmount >= 0) {
			if ((int16_t)(getCurrentClip()->yScroll + scrollAmount)
			    > (int16_t)(getCurrentClip()->getNumNoteRows() - 1)) {
				return ActionResult::DEALT_WITH;
			}
		}
		else {
			if (getCurrentClip()->yScroll + scrollAmount < 1 - kDisplayHeight) {
				return ActionResult::DEALT_WITH;
			}
		}

		// Limit how far we can shift a NoteRow
		if (draggingNoteRow) {
			noteRowToShiftI = lastAuditionedYDisplay + getCurrentClip()->yScroll;
			if (noteRowToShiftI < 0 || noteRowToShiftI >= getCurrentClip()->noteRows.getNumElements()) {
				return ActionResult::DEALT_WITH;
			}

			if (scrollAmount >= 0) {
				if (noteRowToShiftI >= getCurrentClip()->noteRows.getNumElements() - 1) {
					return ActionResult::DEALT_WITH;
				}
				noteRowToSwapWithI = noteRowToShiftI + 1;
			}
			else {
				if (noteRowToShiftI == 0) {
					return ActionResult::DEALT_WITH;
				}
				noteRowToSwapWithI = noteRowToShiftI - 1;
			}
		}
	}

	// Or if not a Kit...
	else {
		int newYNote;
		if (scrollAmount > 0) {
			newYNote = getCurrentClip()->getYNoteFromYDisplay(kDisplayHeight - 1 + scrollAmount, currentSong);
		}
		else {
			newYNote = getCurrentClip()->getYNoteFromYDisplay(scrollAmount, currentSong);
		}

		if (!getCurrentClip()->isScrollWithinRange(scrollAmount, newYNote)) {
			return ActionResult::DEALT_WITH;
		}
	}

	if (inCardRoutine && (numEditPadPresses || draggingNoteRow)) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	bool currentClipIsActive = currentSong->isClipActive(currentSong->currentClip);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Switch off any auditioned notes. But leave on the one whose NoteRow we're moving, if we are
	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
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

		for (int i = 0; i < kEditPadPressBufferSize; i++) {
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

							for (int m = 0; m < kNumExpressionDimensions; m++) {
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
	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
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
								modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(modelStack, yDisplay);
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
				instrumentClipView.drawNoteCode(yDisplay);
				if (isKit) {
					Drum* newSelectedDrum = NULL;
					NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
					if (noteRow) {
						newSelectedDrum = noteRow->drum;
					}
					setSelectedDrum(newSelectedDrum, true);
					changedActiveModControllable = !getAffectEntire();
				}

				if (currentSong->currentClip->output->type == InstrumentType::SYNTH) {
					if (getCurrentUI() == &soundEditor
					    && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
						menu_item::multiRangeMenu.noteOnToChangeRange(
						    getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong)
						    + ((SoundInstrument*)currentSong->currentClip->output)->transpose);
					}
				}

				drawnNoteCodeYet = true;
			}
		}
	}
	if (forceStoppedAnyAuditioning) {
		someAuditioningHasEnded(true);
	}

	// If presses happening, place the Notes on the newly-aligned NoteRows
	if (numEditPadPresses > 0) {

		Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
		//if (!action) return; // Couldn't happen?

		action->updateYScrollClipViewAfter(getCurrentClip());

		for (int i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {

				// Try getting existing NoteRow. If none...
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, modelStack);
				if (!modelStackWithNoteRow->getNoteRowAllowNull()) {

					if (isKit) {
						goto cancelPress;
					}

					// Try creating NoteRow
					modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(modelStack, editPadPresses[i].yDisplay);

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
						for (int m = 0; m < kNumExpressionDimensions; m++) {
							anyActualNodes += editPadPresses[i].stolenMPE[m].num;
						}

						if (anyActualNodes) {
							noteRow->paramManager.ensureExpressionParamSetExists(
							    isKit); // If this fails, we'll detect that below.
						}

						ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
						ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;

						if (mpeParams) {
							ModelStackWithParamCollection* modelStackWithParamCollection =
							    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(
							        mpeParams, mpeParamsSummary);

							int32_t distanceToNextNote = noteRow->getDistanceToNextNote(pos, modelStackWithNoteRow);
							int loopLength = modelStackWithNoteRow->getLoopLength();

							for (int m = 0; m < kNumExpressionDimensions; m++) {
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
	return ActionResult::DEALT_WITH;
}

void AutomationClipView::reassessAllAuditionStatus() {
	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		reassessAuditionStatus(yDisplay);
	}
}

void AutomationClipView::reassessAuditionStatus(uint8_t yDisplay) {
	uint32_t sampleSyncLength;
	uint8_t newVelocity = getVelocityForAudition(yDisplay, &sampleSyncLength);
	// If some change in the NoteRow's audition status (it's come on or off or had its velocity changed)...
	if (newVelocity != lastAuditionedVelocityOnScreen[yDisplay]) {

		// Switch note off if it was on
		if (lastAuditionedVelocityOnScreen[yDisplay] != 255) {
			sendAuditionNote(false, yDisplay, 127, 0);
		}

		// Switch note on if we want it on (it may have a different velocity now)
		if (newVelocity != 255) {
			sendAuditionNote(true, yDisplay, newVelocity, sampleSyncLength);
		}

		lastAuditionedVelocityOnScreen[yDisplay] = newVelocity;
	}
}

// This may send it on a different Clip, if a different one is the activeClip
void AutomationClipView::sendAuditionNote(bool on, uint8_t yDisplay, uint8_t velocity, uint32_t sampleSyncLength) {
	Instrument* instrument = (Instrument*)currentSong->currentClip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	if (instrument->type == InstrumentType::KIT) {
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
					if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
						return;
					}
				}

				if (on) {
					if (drum->type == DrumType::SOUND
					    && !modelStackWithNoteRow->getNoteRow()->paramManager.containsAnyMainParamCollections()) {
						numericDriver.freezeWithError("E325"); // Trying to catch an E313 that Vinz got
					}
					((Kit*)instrument)->beginAuditioningforDrum(modelStackWithNoteRow, drum, velocity, zeroMPEValues);

					//S* range = ((SoundDrum*)drum)->

					//SoundDrum* newDrum = new (drumMemory) SoundDrum();

					//MultisampleRange* range = (MultisampleRange*)newDrum->sources[0].getOrCreateFirstRange();

					//range->sampleHolder.startPos = nextDrumStart;

					//nextDrumStart = (uint64_t)lengthInSamples * (i + 1) / numClips;

					//range->sampleHolder.endPos = nextDrumStart;

					//uint32_t endAtBytePos = sample->audioDataStartPosBytes + sample->audioDataLengthBytes;

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

uint8_t AutomationClipView::getVelocityForAudition(uint8_t yDisplay, uint32_t* sampleSyncLength) {
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

			for (int i = 0; i < kEditPadPressBufferSize; i++) {
				if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay) {
					sum += editPadPresses[i].intendedVelocity;
					numInstances++;
					*sampleSyncLength = editPadPresses[i].intendedLength;
				}
			}
		}
	}

	if (numInstances == 0) {
		return 255;
	}
	return sum / numInstances;
}

uint8_t AutomationClipView::getNumNoteRowsAuditioning() {
	uint8_t num = 0;
	for (int i = 0; i < kDisplayHeight; i++) {
		if (auditionPadIsPressed[i]) {
			num++;
		}
	}
	return num;
}

uint8_t AutomationClipView::oneNoteAuditioning() {
	return (currentUIMode == UI_MODE_AUDITIONING && getNumNoteRowsAuditioning() == 1);
}

int AutomationClipView::getYVisualFromYDisplay(int yDisplay) {
	return yDisplay + getCurrentClip()->yScroll;
}

int AutomationClipView::getYVisualWithinOctaveFromYDisplay(int yDisplay) {
	int yVisual = getYVisualFromYDisplay(yDisplay);
	int yVisualRelativeToRoot = yVisual - currentSong->rootNote;
	int yVisualWithinOctave = yVisualRelativeToRoot % currentSong->numModeNotes;
	if (yVisualWithinOctave < 0) {
		yVisualWithinOctave += currentSong->numModeNotes;
	}
	return yVisualWithinOctave;
}

// Beware - supplying shouldRedrawStuff as false will cause the activeModControllable to *not* update! Probably never should do this anymore...
void AutomationClipView::setSelectedDrum(Drum* drum, bool shouldRedrawStuff) {

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

void AutomationClipView::auditionPadAction(int velocity, int yDisplay, bool shiftButtonDown) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	Instrument* instrument = (Instrument*)currentSong->currentClip->output;

	bool isKit = (instrument->type == InstrumentType::KIT);

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
			if (getCurrentUI() != this) {
				return;
			}

			// Press-down
			if (velocity) {

				setSelectedDrum(NULL);

				if (currentUIMode == UI_MODE_NONE) {
					currentUIMode = UI_MODE_ADDING_DRUM_NOTEROW;
					fileBrowserShouldNotPreview = shiftButtonDown;

					drumForNewNoteRow = NULL; //(Drum*)0xFFFFFFFF;
					//newDrumOptionSelected = true;
					instrumentClipView.drawDrumName(drumForNewNoteRow);

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
						    instrumentClipView.createNewNoteRowForKit(modelStackWithTimelineCounter, yDisplayOfNewNoteRow, &noteRowIndex);
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
	else if (instrument->type == InstrumentType::SYNTH) {
		if (velocity) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
				menu_item::multiRangeMenu.noteOnToChangeRange(
				    getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong)
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
						drum->recordNoteOnEarly((velocity == USE_DEFAULT_VELOCITY) ? instrument->defaultVelocity
						                                                           : velocity,
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
					    instrumentClipView.createNoteRowForYDisplay(modelStackWithTimelineCounter, yDisplay);
				}

				if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
					getCurrentClip()->recordNoteOn(modelStackWithNoteRowOnCurrentClip,
					                               (velocity == USE_DEFAULT_VELOCITY) ? instrument->defaultVelocity
					                                                                  : velocity);
					goto maybeRenderRow;
				}
			}
		}

		// Note-off
		else {

			if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
				getCurrentClip()->recordNoteOff(modelStackWithNoteRowOnCurrentClip);
maybeRenderRow:
				if (!(currentUIMode & UI_MODE_HORIZONTAL_SCROLL)) { // What about zoom too?
					uiNeedsRendering(this, 1 << yDisplay, 0);
				}
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
			if (instrument->type == InstrumentType::KIT) {
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
			int velocityToSound = velocity;
			if (velocityToSound == USE_DEFAULT_VELOCITY) {
				velocityToSound = ((Instrument*)currentSong->currentClip->output)->defaultVelocity;
			}

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
			if (shiftButtonDown || Buttons::isButtonPressed(hid::button::Y_ENC)) {

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

			instrumentClipView.drawNoteCode(yDisplay);
			lastAuditionedYDisplay = yDisplay;

			// Begin resampling / output-recording
			if (Buttons::isButtonPressed(hid::button::RECORD)
			    && audioRecorder.recordingSource == AudioInputChannel::NONE) {
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
			actionLogger.closeAction(ACTION_NOTEROW_ROTATE);
		}

		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
	}

getOut:

	// This has to happen after setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
	}
}

void AutomationClipView::cancelAllAuditioning() {
	if (isUIModeActive(UI_MODE_AUDITIONING)) {
		memset(auditionPadIsPressed, 0, sizeof(auditionPadIsPressed));
		reassessAllAuditionStatus();
		exitUIMode(UI_MODE_AUDITIONING);
		uiNeedsRendering(this, 0, 0xFFFFFFFF);
	}
}

bool AutomationClipView::isRowAuditionedByInstrument(int yDisplay) {
	if (currentSong->currentClip->output->type == InstrumentType::KIT) {
		NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
		if (!noteRow || !noteRow->drum) {
			return false;
		}
		return noteRow->drum->auditioned;
	}
	else {
		int note = getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
		return (((MelodicInstrument*)currentSong->currentClip->output)->isNoteAuditioning(note));
	}
}

void AutomationClipView::cutAuditionedNotesToOne() {
	uint32_t whichRowsNeedReRendering = 0;

	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (yDisplay != lastAuditionedYDisplay && auditionPadIsPressed[yDisplay]) {
			auditionPadIsPressed[yDisplay] = false;

			getCurrentClip()->yDisplayNoLongerAuditioning(yDisplay, currentSong);

			whichRowsNeedReRendering |= (1 << yDisplay);
		}
	}
	reassessAllAuditionStatus();
	if (whichRowsNeedReRendering) {
		uiNeedsRendering(this, 0, whichRowsNeedReRendering);
	}
}

static const uint32_t verticalScrollUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN,
                                                 UI_MODE_DRAGGING_KIT_NOTEROW, 0};

ActionResult AutomationClipView::verticalEncoderAction(int offset, bool inCardRoutine) {

	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
	}

	// If encoder button pressed
	if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
		// If user not wanting to move a noteCode, they want to transpose the key
		if (!currentUIMode && currentSong->currentClip->output->type != InstrumentType::KIT) {

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			actionLogger.deleteAllLogs();

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			// If shift button not pressed, transpose whole octave
			if (!Buttons::isShiftButtonPressed()) {
				offset = getMin((int)1, getMax((int)-1, offset));
				getCurrentClip()->transpose(offset * 12, modelStack);
				if (getCurrentClip()->isScaleModeClip()) {
					getCurrentClip()->yScroll += offset * (currentSong->numModeNotes - 12);
				}
				//numericDriver.displayPopup("OCTAVE");
			}

			// Otherwise, transpose single semitone
			else {
				// If current Clip not in scale-mode, just do it
				if (!getCurrentClip()->isScaleModeClip()) {
					getCurrentClip()->transpose(offset, modelStack);

					// If there are no scale-mode Clips at all, move the root note along as well - just in case the user wants to go back to scale mode (in which case the "previous" root note would be used to help guess what root note to go with)
					if (!currentSong->anyScaleModeClips()) {
						currentSong->rootNote += offset;
					}
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
				if (getCurrentClip()->output->type != InstrumentType::KIT) {
					goto shiftAllColour;
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
					if (auditionPadIsPressed[yDisplay]) {
						ModelStackWithNoteRow* modelStackWithNoteRow =
						    getCurrentClip()->getNoteRowOnScreen(yDisplay, modelStack);
						NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
						if (noteRow) { // This is fine. If we were in Kit mode, we could only be auditioning if there was a NoteRow already
							noteRow->colourOffset += offset;
							if (noteRow->colourOffset >= 72) {
								noteRow->colourOffset -= 72;
							}
							if (noteRow->colourOffset < 0) {
								noteRow->colourOffset += 72;
							}
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

	return ActionResult::DEALT_WITH;
}

static const uint32_t noteNudgeUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

ActionResult AutomationClipView::horizontalEncoderAction(int offset) {

	// If holding down notes
	if (isUIModeActive(UI_MODE_NOTES_PRESSED)) {

		if (!Buttons::isShiftButtonPressed()) {

			// Or, if horizontal encoder held down, nudge note
			if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
			         && isUIModeWithinRange(noteNudgeUIModes)) {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
				}
				nudgeNotes(offset);
			}
		}
		return ActionResult::DEALT_WITH;
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		if (!shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
wantToEditNoteRowLength:
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
			}

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    instrumentClipView.getOrCreateNoteRowForYDisplay(modelStack, lastAuditionedYDisplay);
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
		return ActionResult::DEALT_WITH;
	}

	// Auditioning *and* holding down <> encoder - rotate/shift just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    ((InstrumentClip*)modelStack->getTimelineCounter())
		        ->getNoteRowOnScreen(lastAuditionedYDisplay, modelStack); // Don't create

		rotateNoteRowHorizontally(modelStackWithNoteRow, offset, lastAuditionedYDisplay, true);
		shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress =
		    true; // So don't accidentally shorten row after
		editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
		return ActionResult::DEALT_WITH;
	}

	// Or, let parent deal with it
	else {
		return ClipView::horizontalEncoderAction(offset);
	}
}

void AutomationClipView::tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed) {

	playbackHandler.tempoEncoderAction(offset, encoderButtonPressed, shiftButtonPressed);

}

// Supply offset as 0 to just popup number, not change anything
void AutomationClipView::nudgeNotes(int offset) {

	shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;

	// If just popping up number, but multiple presses, we're quite limited with what intelligible stuff we can display
	if (!offset && numEditPadPresses > 1) {
		return;
	}

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
		for (int i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {

				int noteRowIndex;
				noteRow = currentClip->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
				noteRowId = currentClip->getNoteRowId(noteRow, noteRowIndex);

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);

				int32_t noteRowEffectiveLength = modelStackWithNoteRow->getLoopLength();

				newPos = editPadPresses[i].intendedPos + offset;
				if (newPos < 0) {
					newPos += noteRowEffectiveLength;
				}
				else if (newPos >= noteRowEffectiveLength) {
					newPos -= noteRowEffectiveLength;
				}

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
			if (action) {
				action->offset = offset;
			}
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
		    modelStack->addTimelineCounter(modelStack->song->currentClip);

		// For each note / pad held down...
		for (int i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {
				editPadPresses[i].deleteOnDepress = false;

				if (offset) {
					editPadPresses[i].isBlurredSquare = true; // So it doesn't get dragged along with a vertical scroll
				}

				int noteRowIndex;
				noteRow = currentClip->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
				noteRowId = currentClip->getNoteRowId(noteRow, noteRowIndex);

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);

				int32_t noteRowEffectiveLength = modelStackWithNoteRow->getLoopLength();

				newPos = editPadPresses[i].intendedPos + offset;
				if (newPos < 0) {
					newPos += noteRowEffectiveLength;
				}
				else if (newPos >= noteRowEffectiveLength) {
					newPos -= noteRowEffectiveLength;
				}

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
					if (!offset) {
						break;
					}
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
		if (!didAnySuccessfulNudging) {
			return; // Don't want to see these "multiple pads moved" messages if in fact none were moved
		}
#if HAVE_OLED
		message = (offset >= 0) ? "Nudged notes right" : "Nudged notes left";
#else
		message = (offset >= 0) ? "RIGHT" : "LEFT";
#endif
	}

	else {
		if (resultingTotalOffset >= (currentClip->loopLength >> 1)) {
			resultingTotalOffset -= currentClip->loopLength;
		}
		else if (resultingTotalOffset <= -(currentClip->loopLength >> 1)) {
			resultingTotalOffset += currentClip->loopLength;
		}

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

			if (offsettingNudgeNumberDisplay) {
				resultingTotalOffset -= squareWidth;
			}
		}

#if HAVE_OLED
		strcpy(buffer, "Note nudge: ");
		intToString(resultingTotalOffset, buffer + strlen(buffer));
		message = buffer;
#else
		if (resultingTotalOffset > 9999) {
			message = "RIGHT";
		}
		else if (resultingTotalOffset < -999) {
			message = "LEFT";
		}
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

	if (!offset) {
		return;
	}

	// If multiple presses, just abandon the mod region
	if (numEditPadPresses > 1) {
abandonModRegion:
		view.setModRegion();
	}

	// Otherwise, update it for what they actually intend
	else {

		int i = noteRow->notes.search(newPos, GREATER_OR_EQUAL);
		Note* note = noteRow->notes.getElement(i);
		if (!note || note->pos != newPos) {
			goto abandonModRegion;
		}

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

void AutomationClipView::graphicsRoutine() {
	if (!currentSong) {
		return; // Briefly, if loading a song fails, during the creation of a new blank one, this could happen.
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return;
	}

	if (PadLEDs::flashCursor == FLASH_CURSOR_OFF) {
		return;
	}

	int newTickSquare;

	bool reallyNoTickSquare = (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip)
	                           || currentUIMode == UI_MODE_EXPLODE_ANIMATION || playbackHandler.ticksLeftInCountIn);

	if (reallyNoTickSquare) {
		newTickSquare = 255;
	}
	else {
		newTickSquare = getTickSquare();
		if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
			newTickSquare = 255;
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	memset(tickSquares, newTickSquare, kDisplayHeight);

	uint8_t colours[kDisplayHeight];
	uint8_t nonMutedColour = clip->getCurrentlyRecordingLinearly() ? 2 : 0;
	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		int noteRowIndex;
		NoteRow* noteRow = clip->getNoteRowOnScreen(yDisplay, currentSong, &noteRowIndex);
		colours[yDisplay] = (noteRow && noteRow->muted) ? 1 : nonMutedColour;

		if (!reallyNoTickSquare) {
			if (noteRow && noteRow->hasIndependentPlayPos()) {

				int noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

				int rowTickSquare = getSquareFromPos(noteRow->getLivePos(modelStackWithNoteRow));
				if (rowTickSquare < 0 || rowTickSquare >= kDisplayWidth) {
					rowTickSquare = 255;
				}
				tickSquares[yDisplay] = rowTickSquare;
			}
		}
	}

	PadLEDs::setTickSquares(tickSquares, colours);
}

uint32_t AutomationClipView::getSquareWidth(int32_t square, int32_t effectiveLength) {
	int32_t squareRightEdge = getPosFromSquare(square + 1);
	return getMin(effectiveLength, squareRightEdge) - getPosFromSquare(square);
}

void AutomationClipView::flashDefaultRootNote() {
	flashDefaultRootNoteOn = !flashDefaultRootNoteOn;
	uiNeedsRendering(this, 0, 0xFFFFFFFF);
	uiTimerManager.setTimer(TIMER_DEFAULT_ROOT_NOTE, kFlashTime);
}

void AutomationClipView::recalculateColours() {
	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		recalculateColour(yDisplay);
	}
}

void AutomationClipView::recalculateColour(uint8_t yDisplay) {
	int colourOffset = 0;
	NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
	if (noteRow) {
		colourOffset = noteRow->getColourOffset(getCurrentClip());
	}
	getCurrentClip()->getMainColourFromY(getCurrentClip()->getYNoteFromYDisplay(yDisplay, currentSong), colourOffset,
	                                     rowColour[yDisplay]);
	getTailColour(rowTailColour[yDisplay], rowColour[yDisplay]);
	getBlurColour(rowBlurColour[yDisplay], rowColour[yDisplay]);
}

void AutomationClipView::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {

	if (currentUIMode & UI_MODE_HORIZONTAL_SCROLL) {
		return;
	}

	if (clip == getCurrentClip()) {
		for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong)) {
				uiNeedsRendering(this, 1 << yDisplay, 0);
			}
		}
	}
}

bool AutomationClipView::isDrumAuditioned(Drum* drum) {

	if (currentSong->currentClip->output->type != InstrumentType::KIT) {
		return false;
	}

	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (auditionPadIsPressed[yDisplay]) {
			NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
			if (noteRow && noteRow->drum == drum) {
				return true;
			}
		}
	}

	return false;
}

bool AutomationClipView::getAffectEntire() {
	return getCurrentClip()->affectEntire;
}

void AutomationClipView::tellMatrixDriverWhichRowsContainSomethingZoomable() {
	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		NoteRow* noteRow = getCurrentClip()->getNoteRowOnScreen(yDisplay, currentSong);
		PadLEDs::transitionTakingPlaceOnRow[yDisplay] = (noteRow && !noteRow->hasNoNotes());
	}
}

void AutomationClipView::notifyPlaybackBegun() {
	reassessAllAuditionStatus();
}

//*****NEEDED FOR AUTOMATION VIEW******//

bool AutomationClipView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return true;
	}

	PadLEDs::renderingLock = true;
	performActualRender(whichRows, &image[0][0][0], occupancyMask, currentSong->xScroll[NAVIGATION_CLIP],
	                    currentSong->xZoom[NAVIGATION_CLIP], kDisplayWidth, kDisplayWidth + kSideBarWidth,
	                    drawUndefinedArea);

	InstrumentClip* clip = getCurrentClip();

	if (lastSelectedParamID != 255 && (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::KIT)) {
		soundEditor.setupShortcutBlink(lastSelectedParamX, lastSelectedParamY, 3);
		soundEditor.blinkShortcut();
	}

	else if (lastSelectedMidiCC != 255 && clip->output->type == InstrumentType::MIDI_OUT) {
		soundEditor.setupShortcutBlink(lastSelectedMidiX, lastSelectedMidiY, 3);
		soundEditor.blinkShortcut();
	}

	else {
		uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
	}
	PadLEDs::renderingLock = false;

	//PadLEDs::reassessGreyout(true);

	return true;
}

//*****NEEDED FOR AUTOMATION VIEW******//

//*****NEEDED FOR AUTOMATION VIEW******//

void AutomationClipView::performActualRender(uint32_t whichRows, uint8_t* image,
                                             uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll,
                                             uint32_t xZoom, int renderWidth, int imageWidth, bool drawUndefinedArea) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	for (int yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = NULL;
		if (occupancyMask) {
			occupancyMaskOfRow = occupancyMask[yDisplay];

		}

		if (clip->output->type == InstrumentType::SYNTH && lastSelectedParamID != 255) {
			ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

			if (modelStackWithParam) {

				for (int xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {;
					uint32_t squareStart = getPosFromSquare(xDisplay);
					int32_t currentValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(squareStart, modelStackWithParam);
					int knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);
					knobPos = knobPos + 64;

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					if (knobPos == 0 || (knobPos < yDisplay * 18)) {
						pixel[0] = 0;
						pixel[1] = 0;
						pixel[2] = 0;
					}
					else if (knobPos >= yDisplay * 18) {
						memcpy(pixel,&rowColour[yDisplay],3);
					}
				}

				int32_t effectiveLength = 0;
				effectiveLength = clip->loopLength;
				clip->drawUndefinedArea(xScroll, xZoom, effectiveLength, image + (yDisplay * imageWidth * 3), occupancyMaskOfRow, renderWidth, this,
								                        currentSong->tripletsOn); // Sends image pointer for just the one row
			}
		}

		else if (clip->output->type == InstrumentType::MIDI_OUT && lastSelectedMidiCC != 255) {
			ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

			if (modelStackWithParam) {

				for (int xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					uint32_t squareStart = getPosFromSquare(xDisplay);

					int32_t currentValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(squareStart, modelStackWithParam);
					int knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);
					knobPos = knobPos + 64;

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					if (knobPos == 0 || (knobPos < yDisplay * 18)) {
						pixel[0] = 0;
						pixel[1] = 0;
						pixel[2] = 0;
					}
					else if (knobPos >= yDisplay * 18) {
						memcpy(pixel,&rowColour[yDisplay],3);
					}

				}

				int32_t effectiveLength = 0;

				effectiveLength = clip->loopLength;

				clip->drawUndefinedArea(xScroll, xZoom, effectiveLength, image + (yDisplay * imageWidth * 3), occupancyMaskOfRow, renderWidth, this,
					currentSong->tripletsOn); // Sends image pointer for just the one row
			}
		}

		else {
			if (clip->output->type == InstrumentType::SYNTH) {
				for (int xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					if (paramShortcutsForAutomation[xDisplay][yDisplay] == 0xFFFFFFFF) {

						pixel[0] = 0;
						pixel[1] = 0;
						pixel[2] = 0;

					}

					else {
						ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

						if (modelStackWithParam) {
							if (modelStackWithParam->autoParam->isAutomated()) {

								pixel [0] = 128;
								pixel [1] = 128;
								pixel [2] = 128;
								getTailColour(pixel, pixel);

							}

							else {
								memcpy(pixel,&rowTailColour[yDisplay],3);
							}
						}
					}
				}
			}

			else if (clip->output->type == InstrumentType::KIT) {

				for (int xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					if (paramShortcutsForAutomation[xDisplay][yDisplay] == 0xFFFFFFFF) {

						pixel[0] = 0;
						pixel[1] = 0;
						pixel[2] = 0;

					}

					else {
						memcpy(pixel,&rowTailColour[yDisplay],3);
					}
				}
			}

			else if (clip->output->type == InstrumentType::MIDI_OUT) {
				for (int xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					uint8_t* pixel = image + (yDisplay * imageWidth * 3) + (xDisplay * 3);

					if (midiCCShortcutsForAutomation[xDisplay][yDisplay] == 0xFFFFFFFF) {

						pixel[0] = 0;
						pixel[1] = 0;
						pixel[2] = 0;

					}

					else {

						ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

						if (modelStackWithParam) {

							if (modelStackWithParam->autoParam->isAutomated()) {
								memcpy(pixel,&rowColour[yDisplay],3);
							}

							else {
								memcpy(pixel,&rowTailColour[yDisplay],3);
							}
						}
					}
				}
			}

			else {
				memset(image + (yDisplay * imageWidth * 3), 0, renderWidth * 3);
			}
		}
	}
}

//*****NEEDED FOR AUTOMATION VIEW******//

void AutomationClipView::dontDeleteNotesOnDepress() {
	for (int i = 0; i < kEditPadPressBufferSize; i++) {
		editPadPresses[i].deleteOnDepress = false;
	}
}

//*****NEEDED FOR AUTOMATION VIEW******//

void AutomationClipView::modEncoderAction(int whichModEncoder, int offset) {

	char const* displayText;
	displayText = "modEncoderAction";
	numericDriver.displayPopup(displayText);

	dontDeleteNotesOnDepress();

	InstrumentClip* clip = getCurrentClip();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	Output* output = clip->output;

	if (output->type == InstrumentType::KIT && isUIModeActive(UI_MODE_AUDITIONING)) {

		Kit* kit = (Kit*)output;

		if (kit->selectedDrum && kit->selectedDrum->type != DrumType::SOUND) {

			if (ALPHA_OR_BETA_VERSION && !kit->activeClip) {
				numericDriver.freezeWithError("E381");
			}

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(kit->activeClip);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    ((InstrumentClip*)kit->activeClip)
			        ->getNoteRowForDrum(modelStackWithTimelineCounter,
			                            kit->selectedDrum); // The NoteRow probably doesn't get referred to...

			NonAudioDrum* drum = (NonAudioDrum*)kit->selectedDrum;

			ParamManagerForTimeline* paramManager = NULL;
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			if (noteRow) {
				paramManager = &noteRow->paramManager; // Should be NULL currently, cos it's a NonAudioDrum.
			}
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStackWithNoteRow->addOtherTwoThings(drum->toModControllable(), paramManager);

			drum->modEncoderAction(modelStackWithThreeMainThings, offset, whichModEncoder);
		}
	}

	//Or, if user holding a note(s) down, we'll adjust the value of the selected parameter being automated
	else if (currentUIMode == UI_MODE_NOTES_PRESSED) {
		if (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::KIT) {

			if (lastSelectedParamID != 255 && lastEditPadPressXDisplay != 255) {

			//	char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			//	if (modelStack) {

			//		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addOtherTwoThingsButNoNoteRow(currentSong->currentClip->output->toModControllable(), &currentSong->currentClip->paramManager);
			//		if (modelStackWithThreeMainThings) {
			//			ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();
			//			if (summary) {
			//				ParamSet* paramSet = (ParamSet*)summary->paramCollection;
			//				ModelStackWithAutoParam* modelStackWithParam = modelStackWithThreeMainThings->addParam(paramSet, summary, lastSelectedParamID, &paramSet->params[lastSelectedParamID]);

				ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

				if (modelStackWithParam) {

					uint32_t squareStart = getPosFromSquare(lastEditPadPressXDisplay);
					int effectiveLength = clip->loopLength;
					if (squareStart < effectiveLength) {

						int32_t previousValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(squareStart, modelStackWithParam);
						int knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(previousValue, modelStackWithParam);
						knobPos = knobPos + 64;

						int32_t newKnobPos = 0;

						if ((knobPos + offset) < 0) {
							newKnobPos = knobPos;
						}
						else if ((knobPos + offset) <= 127) {
							newKnobPos = knobPos + offset;
						}
						else {
							newKnobPos = knobPos;
						}

						newKnobPos = newKnobPos - 64;

									//Convert the New Knob Position to a Parameter Value
								//	int32_t newValue =
								//		modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

								//	uint32_t squareWidth = getSquareWidth(lastEditPadPressXDisplay, effectiveLength);

									//modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart, squareWidth);

									//double homogenize because there seems to be a bug in this function where some params get missed on the first call
								//	modelStackWithParam->autoParam->homogenizeRegion(modelStackWithParam, squareStart, squareWidth, newValue, automationClipView.interpolateOn, automationClipView.interpolateOn, modelStackWithParam->getLoopLength(), false);
								//	modelStackWithParam->autoParam->homogenizeRegion(modelStackWithParam, squareStart, squareWidth, newValue, automationClipView.interpolateOn, automationClipView.interpolateOn, modelStackWithParam->getLoopLength(), false);
								//	modelStackWithParam->autoParam->currentValue = newValue;
								//	modelStackWithParam->paramManager->notifyParamModifiedInSomeWay(modelStackWithParam, true, true, true);

						setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, lastEditPadPressXDisplay, effectiveLength);

									//indicator_leds::setKnobIndicatorLevel(whichModEncoder, newKnobPos + 64);

						char buffer[5];

						intToString(newKnobPos + 64, buffer);
						numericDriver.displayPopup(buffer);
					}
				}
			}
		}

		else if (clip->output->type == InstrumentType::MIDI_OUT) {

			if (lastSelectedMidiCC != 255 && lastEditPadPressXDisplay != 255) {

			//	char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			//	if (modelStack) {

			//		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);
			//		if (modelStackWithThreeMainThings) {
			//			ModelStackWithAutoParam* modelStackWithParam = getParamToControlFromInputMIDIChannel(lastSelectedMidiCC, modelStackWithThreeMainThings);

				ModelStackWithAutoParam* modelStackWithParam = getmodelStackWithParam(modelStack, clip);

				if (modelStackWithParam) {

					uint32_t squareStart = getPosFromSquare(lastEditPadPressXDisplay);
					int effectiveLength = clip->loopLength;
					if (squareStart < effectiveLength) {

						int32_t previousValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(squareStart, modelStackWithParam);
						int knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(previousValue, modelStackWithParam);
						knobPos = knobPos + 64;

						int32_t newKnobPos = 0;

						if ((knobPos + offset) < 0) {
							newKnobPos = knobPos;
						}
						else if ((knobPos + offset) <= 127) {
							newKnobPos = knobPos + offset;
						}
						else {
							newKnobPos = knobPos;
						}

						newKnobPos = newKnobPos - 64;

							//	int32_t newValue =
							//		modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

							//	uint32_t squareWidth = getSquareWidth(lastEditPadPressXDisplay, effectiveLength);

								//modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart, squareWidth);

								//double homogenize because there seems to be a bug in this function where some params get missed on the first call
							//	modelStackWithParam->autoParam->homogenizeRegion(modelStackWithParam, squareStart, squareWidth, newValue, automationClipView.interpolateOn, automationClipView.interpolateOn, modelStackWithParam->getLoopLength(), false);
							//	modelStackWithParam->autoParam->homogenizeRegion(modelStackWithParam, squareStart, squareWidth, newValue, automationClipView.interpolateOn, automationClipView.interpolateOn, modelStackWithParam->getLoopLength(), false);
							//	modelStackWithParam->autoParam->currentValue = newValue;
							//	modelStackWithParam->paramManager->notifyParamModifiedInSomeWay(modelStackWithParam, true, true, true);

						setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, lastEditPadPressXDisplay, effectiveLength);

						char buffer[5];

						intToString(newKnobPos + 64, buffer);
						numericDriver.displayPopup(buffer);
					}
				}
			}

		}

		uiNeedsRendering(this);
	}

	//ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);

	//uiNeedsRendering(this);
}

//*****NEEDED FOR AUTOMATION VIEW******//

// Check UI mode is appropriate before calling this
void AutomationClipView::rotateNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int offset, int yDisplay,
                                                   bool shouldDisplayDirectionEvenIfNoNoteRow) {

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow) {
		if (shouldDisplayDirectionEvenIfNoNoteRow) {
			goto displayMessage;
		}
		return;
	}

	{
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

		uint32_t squareWidth = getSquareWidth(0, kMaxSequenceLength);
		int32_t shiftAmount = offset * squareWidth;

		clip->shiftOnlyOneNoteRowHorizontally(modelStack, shiftAmount);

		// Render change
		if (yDisplay >= 0 && yDisplay < kDisplayHeight) {
			uiNeedsRendering(this, 1 << yDisplay, 0);
		}

		// If possible, just modify a previous Action to add this new shift amount to it.
		Action* action = actionLogger.firstAction[BEFORE];
		if (action && action->type == ACTION_NOTEROW_HORIZONTAL_SHIFT && action->openForAdditions
		    && action->currentClip == clip) {

			// If there's no Consequence in the Action, that's probably because we deleted it a previous time with the code just below.
			// Or possibly because the Action was created but there wasn't enough RAM to create the Consequence. Anyway, just go add a consequence now.
			if (!action->firstConsequence) {
				goto addConsequenceToAction;
			}

			ConsequenceNoteRowHorizontalShift* consequence =
			    (ConsequenceNoteRowHorizontalShift*)action->firstConsequence;
			if (consequence->noteRowId != modelStack->noteRowId) {
				goto getNewAction;
			}

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
void AutomationClipView::editNoteRowLength(ModelStackWithNoteRow* modelStack, int offset, int yDisplay) {

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow) {
		return;
	}
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	int32_t oldLength = modelStack->getLoopLength();

	// If we're not scrolled all the way to the right, go there now. If we were already further right than the end of this NoteRow, it's ok, we'll stay there.
	if (scrollRightToEndOfLengthIfNecessary(oldLength)) {
		return; // ActionResult::DEALT_WITH;
	}

	uint32_t squareWidth = getSquareWidth(0, kMaxSequenceLength);

	int oldNumSteps = (uint32_t)(oldLength - 1) / squareWidth + 1; // Round up
	int newNumSteps = oldNumSteps + offset;
	if (newNumSteps <= 0) {
		return;
	}
	int newLength = newNumSteps * squareWidth;
	if (newLength > kMaxSequenceLength) {
		return;
	}

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
		if (prevCons->noteRowId != modelStack->noteRowId) {
			goto editLengthWithNewAction;
		}

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
			else {
				goto editLengthWithNewAction;
			}
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
		if (!didScroll) {
			goto tryScrollingLeft;
		}
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
	if (!didScroll && yDisplay >= 0 && yDisplay < kDisplayHeight) {
		uiNeedsRendering(this, 1 << yDisplay, 0);
	}
}

void AutomationClipView::someAuditioningHasEnded(bool recalculateLastAuditionedNoteOnScreen) {
	// Try to find another auditioned NoteRow so we can show its name etc
	int i;
	for (i = 0; i < kDisplayHeight; i++) {
		if (auditionPadIsPressed[i]) {
			// Show this note's noteCode, if the noteCode we were showing before is the note we just stopped auditioning
			if (recalculateLastAuditionedNoteOnScreen) {
				instrumentClipView.drawNoteCode(i);
				lastAuditionedYDisplay = i;
			}
			break;
		}
	}

	// Or, if all auditioning now finished...
	if (i == kDisplayHeight) {
		exitUIMode(UI_MODE_AUDITIONING);
		auditioningSilently = false;

#if HAVE_OLED
		OLED::removePopup();
#else
		redrawNumericDisplay();
#endif
	}
}

bool AutomationClipView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	return instrumentClipView.renderSidebar(whichRows, image, occupancyMask);
}

// Virtual function, gets overridden.
ModelStackWithAutoParam*
AutomationClipView::getParamToControlFromInputMIDIChannel(int cc, ModelStackWithThreeMainThings* modelStack) {

	if (!modelStack
	         ->paramManager) { // Could be NULL - if the user is holding down an audition pad in Arranger, and we have no Clips
noParam:
		return modelStack->addParamCollectionAndId(NULL, NULL, 0)->addAutoParam(NULL); // "No param"
	}

	ParamCollectionSummary* summary;
	int paramId = cc;

	switch (cc) {
	case CC_NUMBER_PITCH_BEND:
		paramId = 0;
		goto expressionParam;

	case 74:
		paramId = 1;
		goto expressionParam;

	case CC_NUMBER_AFTERTOUCH:
		paramId = 2;
expressionParam:
		modelStack->paramManager->ensureExpressionParamSetExists(); // Allowed to fail
		summary = modelStack->paramManager->getExpressionParamSetSummary();
		if (!summary->paramCollection) {
			goto noParam;
		}
		break;

	case CC_NUMBER_NONE:
		goto noParam;

	default:
		summary = modelStack->paramManager->getMIDIParamCollectionSummary();
		break;
	}

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);

	return summary->paramCollection->getAutoParamFromId(
	    modelStackWithParamId,
	    true); // Yes we do want to force creating it even if we're not recording - so the level indicator can update for the user
}
