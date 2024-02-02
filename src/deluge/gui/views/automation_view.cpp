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

#include "gui/views/automation_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/consequence/consequence_note_row_mute.h"
#include "model/drum/drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/instrument/midi_instrument.h"
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
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"
#include "util/cfunctions.h"
#include "util/functions.h"
#include <new>
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

namespace params = deluge::modulation::params;
using deluge::modulation::params::kNoParamID;
using deluge::modulation::params::ParamType;
using deluge::modulation::params::patchedParamShortcuts;
using deluge::modulation::params::unpatchedGlobalParamShortcuts;
using deluge::modulation::params::unpatchedNonGlobalParamShortcuts;

using namespace deluge::gui;

const uint32_t auditionPadActionUIModes[] = {UI_MODE_NOTES_PRESSED,
                                             UI_MODE_AUDITIONING,
                                             UI_MODE_HORIZONTAL_SCROLL,
                                             UI_MODE_RECORD_COUNT_IN,
                                             UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON,
                                             0};

const uint32_t editPadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

const uint32_t mutePadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

const uint32_t verticalScrollUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN, 0};

// synth and kit rows FX - sorted in the order that Parameters are scrolled through on the display
const std::array<std::pair<params::Kind, ParamType>, kNumNonGlobalParamsForAutomation> nonGlobalParamsForAutomation{{
    {params::Kind::PATCHED, params::GLOBAL_VOLUME_POST_FX}, // Master Volume, Pitch, Pan
    {params::Kind::PATCHED, params::LOCAL_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_PAN},
    {params::Kind::PATCHED, params::LOCAL_LPF_FREQ}, // LPF Cutoff, Resonance, Morph
    {params::Kind::PATCHED, params::LOCAL_LPF_RESONANCE},
    {params::Kind::PATCHED, params::LOCAL_LPF_MORPH},
    {params::Kind::PATCHED, params::LOCAL_HPF_FREQ}, // HPF Cutoff, Resonance, Morph
    {params::Kind::PATCHED, params::LOCAL_HPF_RESONANCE},
    {params::Kind::PATCHED, params::LOCAL_HPF_MORPH},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS}, // Bass, Bass Freq
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS_FREQ},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_TREBLE}, // Treble, Treble Freq
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_TREBLE_FREQ},
    {params::Kind::PATCHED, params::GLOBAL_REVERB_AMOUNT}, // Reverb Amount
    {params::Kind::PATCHED, params::GLOBAL_DELAY_RATE},    // Delay Rate, Amount
    {params::Kind::PATCHED, params::GLOBAL_DELAY_FEEDBACK},
    {params::Kind::PATCHED, params::GLOBAL_VOLUME_POST_REVERB_SEND}, // Sidechain Send, Shape
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_COMPRESSOR_SHAPE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SAMPLE_RATE_REDUCTION}, // Decimation, Bitcrush, Wavefolder
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BITCRUSHING},
    {params::Kind::PATCHED, params::LOCAL_FOLD},
    {params::Kind::PATCHED,
     params::LOCAL_OSC_A_VOLUME}, // OSC 1 Volume, Pitch, Pulse Width, Carrier Feedback, Wave Index
    {params::Kind::PATCHED, params::LOCAL_OSC_A_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_PHASE_WIDTH},
    {params::Kind::PATCHED, params::LOCAL_CARRIER_0_FEEDBACK},
    {params::Kind::PATCHED,
     params::LOCAL_OSC_A_WAVE_INDEX}, // OSC 2 Volume, Pitch, Pulse Width, Carrier Feedback, Wave Index
    {params::Kind::PATCHED, params::LOCAL_OSC_B_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_PHASE_WIDTH},
    {params::Kind::PATCHED, params::LOCAL_CARRIER_1_FEEDBACK},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_WAVE_INDEX},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_VOLUME}, // FM Mod 1 Volume, Pitch, Feedback
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_FEEDBACK},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_VOLUME}, // FM Mod 2 Volume, Pitch, Feedback
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_FEEDBACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_ATTACK}, // Env 1 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_0_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_RELEASE},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_ATTACK}, // Env 2 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_1_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_RELEASE},
    {params::Kind::PATCHED, params::GLOBAL_LFO_FREQ},                 // LFO 1 Freq
    {params::Kind::PATCHED, params::LOCAL_LFO_LOCAL_FREQ},            // LFO 2 Freq
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_OFFSET}, // Mod FX Offset, Feedback, Depth, Rate
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_FEEDBACK},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_DEPTH},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_RATE},
    {params::Kind::PATCHED, params::GLOBAL_ARP_RATE}, // Arp Rate, Gate
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_GATE},
    {params::Kind::PATCHED, params::LOCAL_NOISE_VOLUME},             // Noise
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_PORTAMENTO},   // Portamento
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_STUTTER_RATE}, // Stutter Rate
}};

// global FX - sorted in the order that Parameters are scrolled through on the display
// used with kit affect entire, audio clips, and arranger
const std::array<std::pair<params::Kind, ParamType>, kNumGlobalParamsForAutomation> globalParamsForAutomation{{
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME}, // Master Volume, Pitch, Pan
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_PITCH_ADJUST},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_PAN},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ}, // LPF Cutoff, Resonance
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_RES},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_FREQ}, // HPF Cutoff, Resonance
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_RES},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BASS}, // Bass, Bass Freq
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BASS_FREQ},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_TREBLE}, // Treble, Treble Freq
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_TREBLE_FREQ},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_REVERB_SEND_AMOUNT}, // Reverb Amount
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_DELAY_RATE},         // Delay Rate, Amount
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_DELAY_AMOUNT},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SIDECHAIN_VOLUME}, // Sidechain Send, Shape
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_COMPRESSOR_SHAPE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SAMPLE_RATE_REDUCTION}, // Decimation, Bitcrush
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BITCRUSHING},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_OFFSET}, // Mod FX Offset, Feedback, Depth, Rate
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_FEEDBACK},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_DEPTH},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_RATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE}, // Stutter Rate
}};

// let's render some love <3

const uint32_t love[kDisplayWidth][kDisplayHeight] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0},
    {0, 0, 0, 0xFFFFFFFF, 0, 0, 0xFFFFFFFF, 0},
    {0, 0, 0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF},
    {0, 0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF, 0},
    {0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF, 0, 0},
    {0, 0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF, 0},
    {0, 0, 0xFFFFFFFF, 0, 0, 0, 0, 0xFFFFFFFF},
    {0, 0, 0, 0xFFFFFFFF, 0, 0, 0xFFFFFFFF, 0},
    {0, 0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0},
    {0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0, 0, 0, 0},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0},
    {0, 0, 0, 0, 0, 0xFFFFFFFF, 0, 0},
    {0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0, 0xFFFFFFFF, 0},
    {0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0, 0, 0, 0, 0, 0, 0, 0}};

// VU meter style colours for the automation editor

const RGB rowColour[kDisplayHeight] = {
    {0, 255, 0}, {36, 219, 0}, {73, 182, 0}, {109, 146, 0}, {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0},
};

const RGB rowTailColour[kDisplayHeight] = {
    {2, 53, 2}, {9, 46, 2}, {17, 38, 2}, {24, 31, 2}, {31, 24, 2}, {38, 17, 2}, {46, 9, 2}, {53, 2, 2},
};

const RGB rowBlurColour[kDisplayHeight] = {
    {71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57}, {76, 70, 53}, {77, 60, 48}, {78, 49, 44}, {79, 39, 39},
};

AutomationView automationView{};

AutomationView::AutomationView() {

	instrumentClipView.numEditPadPresses = 0;

	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		instrumentClipView.editPadPresses[i].isActive = false;
	}

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		instrumentClipView.numEditPadPressesPerNoteRowOnScreen[yDisplay] = 0;
		instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;
		instrumentClipView.auditionPadIsPressed[yDisplay] = 0;
	}

	instrumentClipView.auditioningSilently = false;
	instrumentClipView.timeLastEditPadPress = 0;

	// initialize automation view specific variables
	interpolation = true;
	interpolationBefore = false;
	interpolationAfter = false;
	// used to prevent excessive blinking when you're scrolling with horizontal / vertical / mod encoders
	encoderAction = false;
	// used to reset shortcut blinking
	shortcutBlinking = false;
	// used to enter pad selection mode
	padSelectionOn = false;
	multiPadPressSelected = false;
	multiPadPressActive = false;
	leftPadSelectedX = kNoSelection;
	leftPadSelectedY = kNoSelection;
	rightPadSelectedX = kNoSelection;
	rightPadSelectedY = kNoSelection;
	lastPadSelectedKnobPos = kNoSelection;
	playbackStopped = false;
	onArrangerView = false;
	navSysId = NAVIGATION_CLIP;

	initMIDICCShortcutsForAutomation();
	midiCCShortcutsLoaded = false;
}

void AutomationView::initMIDICCShortcutsForAutomation() {
	for (int x = 0; x < kDisplayWidth; x++) {
		for (int y = 0; y < kDisplayHeight; y++) {
			int32_t ccNumber = midiFollow.paramToCC[x][y];
			if (ccNumber != MIDI_CC_NONE) {
				midiCCShortcutsForAutomation[x][y] = ccNumber;
			}
			else {
				midiCCShortcutsForAutomation[x][y] = kNoParamID;
			}
		}
	}

	midiCCShortcutsForAutomation[14][7] = CC_NUMBER_PITCH_BEND;
	midiCCShortcutsForAutomation[15][0] = CC_NUMBER_AFTERTOUCH;
	midiCCShortcutsForAutomation[15][7] = CC_NUMBER_MOD_WHEEL;
}

// called everytime you open up the automation view
bool AutomationView::opened() {
	navSysId = getNavSysId();

	if (!midiCCShortcutsLoaded) {
		initMIDICCShortcutsForAutomation();
		midiCCShortcutsLoaded = true;
	}

	// grab the default setting for interpolation
	interpolation = FlashStorage::automationInterpolate;

	// re-initialize pad selection mode (so you start with the default automation editor)
	initPadSelection();

	InstrumentClip* clip = getCurrentInstrumentClip();

	if (!onArrangerView) {
		// only applies to instrument clips (not audio)
		if (clip) {
			OutputType outputType = clip->output->type;
			// check if we for some reason, left the automation view, then switched clip types, then came back in
			// if you did that...reset the parameter selection and save the current parameter type selection
			// so we can check this again next time it happens
			if (outputType != clip->lastSelectedOutputType) {
				initParameterSelection();

				clip->lastSelectedOutputType = outputType;
			}

			if (clip->wrapEditing) { // turn led off if it's on
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
			}
		}
	}

	resetShortcutBlinking();

	openedInBackground();

	if (!onArrangerView) {
		// only applies to instrument clips (not audio)
		if (clip) {
			InstrumentClipMinder::opened();
		}
	}

	focusRegained();

	return true;
}

// Initializes some stuff to begin a new editing session
void AutomationView::focusRegained() {
	if (onArrangerView) {
		indicator_leds::setLedState(IndicatorLED::BACK, false);
		currentSong->affectEntire = true;
		view.focusRegained();
		view.setActiveModControllableTimelineCounter(currentSong);
	}
	else {
		ClipView::focusRegained();

		Clip* clip = getCurrentClip();
		if (clip->type == ClipType::AUDIO) {
			indicator_leds::setLedState(IndicatorLED::BACK, false);
			indicator_leds::setLedState(IndicatorLED::AFFECT_ENTIRE, true);
			view.focusRegained();
			view.setActiveModControllableTimelineCounter(clip);
		}
		else {
			instrumentClipView.auditioningSilently = false; // Necessary?
			InstrumentClipMinder::focusRegained();
			instrumentClipView.setLedStates();
		}
	}
}

void AutomationView::openedInBackground() {
	Clip* clip = getCurrentClip();

	if (!onArrangerView) {
		// used when you're in song view / arranger view / keyboard view
		//(so it knows to come back to automation view)
		clip->onAutomationClipView = true;

		if (clip->type == ClipType::INSTRUMENT) {
			((InstrumentClip*)clip)->onKeyboardScreen = false;

			instrumentClipView.recalculateColours();
		}
	}

	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	AudioEngine::logAction("AutomationView::beginSession 2");

	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight],
		               true);
		if (onArrangerView) {
			arrangerView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
			                           &PadLEDs::occupancyMaskStore[kDisplayHeight]);
		}
		else {
			clip->renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
			                    &PadLEDs::occupancyMaskStore[kDisplayHeight]);
		}
	}
	else {
		uiNeedsRendering(this);
	}
}

// used for the play cursor
void AutomationView::graphicsRoutine() {
	if (onArrangerView) {
		arrangerView.graphicsRoutine();
	}
	else {
		if (getCurrentClip()->type == ClipType::AUDIO) {
			audioClipView.graphicsRoutine();
		}
		else {
			instrumentClipView.graphicsRoutine();
		}
	}
}

// used to return whether Automation View is in the AUTOMATION_ARRANGER_VIEW UI Type, AUTOMATION_INSTRUMENT_CLIP_VIEW or
// AUTOMATION_AUDIO_CLIP_VIEW UI Type
AutomationSubType AutomationView::getAutomationSubType() {
	if (onArrangerView) {
		return AutomationSubType::ARRANGER;
	}
	else {
		if (getCurrentClip()->type == ClipType::AUDIO) {
			return AutomationSubType::AUDIO;
		}
		else {
			return AutomationSubType::INSTRUMENT;
		}
	}
}

// rendering

// called whenever you call uiNeedsRendering(this) somewhere else
// used to render automation overview, automation editor
// used to setup the shortcut blinking
bool AutomationView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	if (!image) {
		return true;
	}

	if (!occupancyMask) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return true;
	}

	PadLEDs::renderingLock = true;

	Clip* clip = getCurrentClip();
	if (!onArrangerView && clip->type == ClipType::INSTRUMENT) {
		instrumentClipView.recalculateColours();
	}

	// erase current image as it will be refreshed
	memset(image, 0, sizeof(RGB) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	// erase current occupancy mask as it will be refreshed
	memset(occupancyMask, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	performActualRender(whichRows, &image[0][0], occupancyMask, currentSong->xScroll[navSysId],
	                    currentSong->xZoom[navSysId], kDisplayWidth, kDisplayWidth + kSideBarWidth, drawUndefinedArea);

	if (encoderAction == false) {
		int32_t lastSelectedParamShortcutX = kNoSelection;
		int32_t lastSelectedParamShortcutY = kNoSelection;
		if (onArrangerView) {
			lastSelectedParamShortcutX = currentSong->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = currentSong->lastSelectedParamShortcutY;
		}
		else {
			lastSelectedParamShortcutX = clip->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = clip->lastSelectedParamShortcutY;
		}
		// if a Param has been selected for editing, blink its shortcut pad
		if (lastSelectedParamShortcutX != kNoSelection) {
			if (shortcutBlinking == false) {
				memset(soundEditor.sourceShortcutBlinkFrequencies, 255,
				       sizeof(soundEditor.sourceShortcutBlinkFrequencies));
				soundEditor.setupShortcutBlink(lastSelectedParamShortcutX, lastSelectedParamShortcutY, 10);
				soundEditor.blinkShortcut();

				shortcutBlinking = true;
			}
		}
		// unset previously set blink timers if not editing a parameter
		else {
			resetShortcutBlinking();
		}
	}
	else {
		// doing this so the shortcut doesn't blink like crazy while turning knobs that refresh UI
		encoderAction = false;
	}

	PadLEDs::renderingLock = false;

	return true;
}

// determines whether you should render the automation editor, automation overview or just render some love <3
void AutomationView::performActualRender(uint32_t whichRows, RGB* image,
                                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll,
                                         uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
                                         bool drawUndefinedArea) {

	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings,
		                                                                    currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParam(modelStackWithTimelineCounter, clip);
	}
	int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		if (whichRows & (1 << yDisplay)) {
			uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];

			if (onArrangerView
			    || (outputType != OutputType::CV
			        && !(outputType == OutputType::KIT && !instrumentClipView.getAffectEntire()
			             && !((Kit*)output)->selectedDrum))) {

				// if parameter has been selected, show Automation Editor
				if (!isOnAutomationOverview()) {
					renderAutomationEditor(modelStackWithParam, clip, image + (yDisplay * imageWidth),
					                       occupancyMaskOfRow, renderWidth, xScroll, xZoom, effectiveLength, yDisplay,
					                       drawUndefinedArea);
				}

				// if not editing a parameter, show Automation Overview
				else {
					renderAutomationOverview(modelStackWithTimelineCounter, modelStackWithThreeMainThings, clip,
					                         outputType, image + (yDisplay * imageWidth), occupancyMaskOfRow, yDisplay);
				}
			}

			else {
				if (outputType == OutputType::CV) {
					renderLove(image + (yDisplay * imageWidth), occupancyMaskOfRow, yDisplay);
				}
			}
		}
	}
}

// renders automation overview
void AutomationView::renderAutomationOverview(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                              ModelStackWithThreeMainThings* modelStackWithThreeMainThings, Clip* clip,
                                              OutputType outputType, RGB* image, uint8_t occupancyMask[],
                                              int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

		RGB& pixel = image[xDisplay];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (!onArrangerView
		    && ((outputType == OutputType::SYNTH
		         || (outputType == OutputType::KIT && !instrumentClipView.getAffectEntire()))
		        && ((patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID)
		            || (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)))) {

			if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				modelStackWithParam =
				    getModelStackWithParam(modelStackWithTimelineCounter, clip,
				                           patchedParamShortcuts[xDisplay][yDisplay], params::Kind::PATCHED);
			}

			else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				// don't make portamento available for automation in kit rows
				if ((outputType == OutputType::KIT)
				    && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == params::UNPATCHED_PORTAMENTO)) {
					continue;
				}

				modelStackWithParam = getModelStackWithParam(modelStackWithTimelineCounter, clip,
				                                             unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay],
				                                             params::Kind::UNPATCHED_SOUND);
			}
		}

		else if ((onArrangerView || (outputType == OutputType::AUDIO)
		          || (outputType == OutputType::KIT && instrumentClipView.getAffectEntire()))
		         && (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)) {
			int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];
			if (onArrangerView) {
				// don't make pitch adjust or sidechain available for automation in arranger
				if ((paramID == params::UNPATCHED_PITCH_ADJUST) || (paramID == params::UNPATCHED_COMPRESSOR_SHAPE)
				    || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME)) {
					continue;
				}
				modelStackWithParam =
				    performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings, paramID);
			}
			else {
				modelStackWithParam = getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID);
			}
		}

		else if (outputType == OutputType::MIDI_OUT && midiCCShortcutsForAutomation[xDisplay][yDisplay] != kNoParamID) {
			modelStackWithParam = getModelStackWithParam(modelStackWithTimelineCounter, clip,
			                                             midiCCShortcutsForAutomation[xDisplay][yDisplay]);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {
			// highlight pad white if the parameter it represents is currently automated
			if (modelStackWithParam->autoParam->isAutomated()) {
				pixel = {
				    .r = 130,
				    .g = 120,
				    .b = 130,
				};
			}

			else {
				pixel = colours::grey;
			}

			occupancyMask[xDisplay] = 64;
		}
	}
}

// gets the length of the clip, renders the pads corresponding to current parameter values set up to the clip length
// renders the undefined area of the clip that the user can't interact with
void AutomationView::renderAutomationEditor(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, RGB* image,
                                            uint8_t occupancyMask[], int32_t renderWidth, int32_t xScroll,
                                            uint32_t xZoom, int32_t effectiveLength, int32_t yDisplay,
                                            bool drawUndefinedArea) {
	if (modelStackWithParam && modelStackWithParam->autoParam) {

		renderRow(modelStackWithParam, image, occupancyMask, effectiveLength, yDisplay,
		          modelStackWithParam->autoParam->isAutomated(), xScroll, xZoom);

		if (drawUndefinedArea) {
			renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth, this,
			                    currentSong->tripletsOn);
		}
	}
}

// this function started off as a copy of the renderRow function from the NoteRow class - I replaced "notes" with
// "nodes" it worked for the most part, but there was bugs so I removed the buggy code and inserted my alternative
// rendering method which always works. hoping to bring back the other code once I've worked out the bugs.
void AutomationView::renderRow(ModelStackWithAutoParam* modelStackWithParam, RGB* image, uint8_t occupancyMask[],
                               int32_t lengthToDisplay, int32_t yDisplay, bool isAutomated, int32_t xScroll,
                               int32_t xZoom) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

		uint32_t squareStart = getMiddlePosFromSquare(xDisplay, lengthToDisplay, xScroll, xZoom);
		int32_t knobPos = getParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;

		RGB& pixel = image[xDisplay];

		if (knobPos > (yDisplay * kParamValueIncrementForAutomationDisplay)) {
			if (isAutomated) { // automated, render bright colour
				pixel = rowColour[yDisplay];
			}
			else { // not automated, render less bright tail colour
				pixel = rowTailColour[yDisplay];
			}
			occupancyMask[xDisplay] = 64;
		}

		if (padSelectionOn && ((xDisplay == leftPadSelectedX) || (xDisplay == rightPadSelectedX))) {

			if (knobPos > (yDisplay * kParamValueIncrementForAutomationDisplay)) {
				pixel = rowBlurColour[yDisplay];
			}
			else {
				pixel = colours::grey;
			}
			occupancyMask[xDisplay] = 64;
		}
	}
}

// occupancyMask now optional
void AutomationView::renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay, RGB* rowImage,
                                         uint8_t occupancyMask[], int32_t imageWidth, TimelineView* timelineView,
                                         bool tripletsOnHere) {
	// If the visible pane extends beyond the end of the Clip, draw it as grey
	int32_t greyStart = timelineView->getSquareFromPos(lengthToDisplay - 1, NULL, xScroll, xZoom) + 1;

	if (greyStart < 0) {
		greyStart = 0; // This actually happened in a song of Marek's, due to another bug, but best to check for this
	}

	if (greyStart < imageWidth) {
		std::fill(rowImage + greyStart, rowImage + greyStart + (imageWidth - greyStart), colours::grey);
		if (occupancyMask) {
			memset(occupancyMask + greyStart, 64, imageWidth - greyStart);
		}
	}

	if (tripletsOnHere && timelineView->supportsTriplets()) {
		for (int32_t xDisplay = 0; xDisplay < imageWidth; xDisplay++) {
			if (!timelineView->isSquareDefined(xDisplay, xScroll, xZoom)) {
				rowImage[xDisplay] = colours::grey;

				if (occupancyMask) {
					occupancyMask[xDisplay] = 64;
				}
			}
		}
	}
}

// easter egg lol. it is rendered when you press the CV clip button as you can't use automation view there
// it draws a cute heart and musical note
void AutomationView::renderLove(RGB* image, uint8_t occupancyMask[], int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

		RGB& pixel = image[xDisplay];

		if (love[xDisplay][yDisplay] == 0xFFFFFFFF) {

			pixel = rowColour[yDisplay];
			occupancyMask[xDisplay] = 64;
		}
	}
}

// defers to audio clip or instrument clip sidebar render functions
// depending on the active clip
bool AutomationView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (onArrangerView) {
		return arrangerView.renderSidebar(whichRows, image, occupancyMask);
	}
	else {
		return getCurrentClip()->renderSidebar(whichRows, image, occupancyMask);
	}
}

/*render's what is displayed on OLED or 7SEG screens when in Automation View

On Automation Overview:

- on OLED it renders "Automation Overview" (or "Can't Automate CV" if you're on a CV clip)
- on 7Seg it renders AUTO (or CANT if you're on a CV clip)

On Automation Editor:

- on OLED it renders Parameter Name, Automation Status and Parameter Value (for selected Pad or the current value for
the Parameter for the last selected Mod Position)
- on 7SEG it renders Parameter name if no pad is selected or mod encoder is turned. If selecting pad it displays the
pads value for as long as you hold the pad. if turning mod encoder, it displays value while turning mod encoder. after
value displaying is finished, it displays scrolling parameter name again.

This function replaces the two functions that were previously called:

DisplayParameterValue
DisplayParameterName */

void AutomationView::renderDisplay(int32_t knobPosLeft, int32_t knobPosRight, bool modEncoderAction) {
	Clip* clip = getCurrentClip();
	OutputType outputType = clip->output->type;

	// if you're not in a MIDI instrument clip, convert the knobPos to the same range as the menu (0-50)
	if (onArrangerView || outputType != OutputType::MIDI_OUT) {
		params::Kind lastSelectedParamKind = params::Kind::NONE;
		int32_t lastSelectedParamID = kNoSelection;
		if (onArrangerView) {
			lastSelectedParamKind = currentSong->lastSelectedParamKind;
			lastSelectedParamID = currentSong->lastSelectedParamID;
		}
		else {
			lastSelectedParamKind = clip->lastSelectedParamKind;
			lastSelectedParamID = clip->lastSelectedParamID;
		}
		if (knobPosLeft != kNoSelection) {
			knobPosLeft = view.calculateKnobPosForDisplay(lastSelectedParamKind, lastSelectedParamID, knobPosLeft);
		}
		if (knobPosRight != kNoSelection) {
			knobPosRight = view.calculateKnobPosForDisplay(lastSelectedParamKind, lastSelectedParamID, knobPosRight);
		}
	}

	// OLED Display
	if (display->haveOLED()) {
		renderDisplayOLED(clip, outputType, knobPosLeft, knobPosRight);
	}
	// 7SEG Display
	else {
		renderDisplay7SEG(clip, outputType, knobPosLeft, modEncoderAction);
	}
}

void AutomationView::renderDisplayOLED(Clip* clip, OutputType outputType, int32_t knobPosLeft, int32_t knobPosRight) {
	deluge::hid::display::OLED::clearMainImage();

	if (isOnAutomationOverview() || (outputType == OutputType::CV)) {

		// align string to vertically to the centre of the display
#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 24;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 15;
#endif

		// display Automation Overview or Can't Automate CV
		char const* overviewText;
		if (onArrangerView || outputType != OutputType::CV) {
			if (!onArrangerView
			    && (outputType == OutputType::KIT && !instrumentClipView.getAffectEntire()
			        && !((Kit*)clip->output)->selectedDrum)) {
				overviewText = l10n::get(l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE);
				deluge::hid::display::OLED::drawPermanentPopupLookingText(overviewText);
			}
			else {
				overviewText = l10n::get(l10n::String::STRING_FOR_AUTOMATION_OVERVIEW);
				deluge::hid::display::OLED::drawStringCentred(overviewText, yPos,
				                                              deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
			}
		}
		else {
			overviewText = l10n::get(l10n::String::STRING_FOR_CANT_AUTOMATE_CV);
			deluge::hid::display::OLED::drawPermanentPopupLookingText(overviewText);
		}
	}
	else if (onArrangerView || outputType != OutputType::CV) {
		// display parameter name
		char parameterName[30];
		getParameterName(clip, outputType, parameterName);

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
		deluge::hid::display::OLED::drawStringCentred(parameterName, yPos, deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		// display automation status
		yPos = yPos + 12;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings,
			                                                                    currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			modelStackWithParam = getModelStackWithParam(modelStack, clip);
		}

		char const* isAutomated;

		// check if Parameter is currently automated so that the automation status can be drawn on the screen with the
		// Parameter Name
		if (modelStackWithParam && modelStackWithParam->autoParam) {
			if (modelStackWithParam->autoParam->isAutomated()) {
				isAutomated = l10n::get(l10n::String::STRING_FOR_AUTOMATION_ON);
			}
			else {
				isAutomated = l10n::get(l10n::String::STRING_FOR_AUTOMATION_OFF);
			}
		}

		deluge::hid::display::OLED::drawStringCentred(isAutomated, yPos, deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		// display parameter value
		yPos = yPos + 12;

		if ((multiPadPressSelected) && (knobPosRight != kNoSelection)) {
			char bufferLeft[10];
			bufferLeft[0] = 'L';
			bufferLeft[1] = ':';
			bufferLeft[2] = ' ';
			intToString(knobPosLeft, &bufferLeft[3]);
			deluge::hid::display::OLED::drawString(bufferLeft, 0, yPos, deluge::hid::display::OLED::oledMainImage[0],
			                                       OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			char bufferRight[10];
			bufferRight[0] = 'R';
			bufferRight[1] = ':';
			bufferRight[2] = ' ';
			intToString(knobPosRight, &bufferRight[3]);
			deluge::hid::display::OLED::drawStringAlignRight(bufferRight, yPos,
			                                                 deluge::hid::display::OLED::oledMainImage[0],
			                                                 OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
		}
		else {
			char buffer[5];
			intToString(knobPosLeft, buffer);
			deluge::hid::display::OLED::drawStringCentred(buffer, yPos, deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
		}
	}

	deluge::hid::display::OLED::sendMainImage();
}

void AutomationView::renderDisplay7SEG(Clip* clip, OutputType outputType, int32_t knobPosLeft, bool modEncoderAction) {
	// display OVERVIEW or CANT
	if (isOnAutomationOverview() || (outputType == OutputType::CV)) {
		char const* overviewText;
		if (onArrangerView || outputType != OutputType::CV) {
			char const* overviewText;
			if (!onArrangerView
			    && (outputType == OutputType::KIT && !instrumentClipView.getAffectEntire()
			        && !((Kit*)clip->output)->selectedDrum)) {
				overviewText = l10n::get(l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE);
			}
			else {
				overviewText = l10n::get(l10n::String::STRING_FOR_AUTOMATION);
			}
		}
		else {
			overviewText = l10n::get(l10n::String::STRING_FOR_CANT_AUTOMATE_CV);
		}
		display->setScrollingText(overviewText);
	}
	else if (onArrangerView || outputType != OutputType::CV) {
		/* check if you're holding a pad
		 * if yes, store pad press knob position in lastPadSelectedKnobPos
		 * so that it can be used next time as the knob position if returning here
		 * to display parameter value after another popup has been cancelled (e.g. audition pad)
		 */
		if (isUIModeActive(UI_MODE_NOTES_PRESSED)) {
			if (knobPosLeft != kNoSelection) {
				lastPadSelectedKnobPos = knobPosLeft;
			}
			else if (lastPadSelectedKnobPos != kNoSelection) {
				params::Kind lastSelectedParamKind = params::Kind::NONE;
				int32_t lastSelectedParamID = kNoSelection;
				if (onArrangerView) {
					lastSelectedParamKind = currentSong->lastSelectedParamKind;
					lastSelectedParamID = currentSong->lastSelectedParamID;
				}
				else {
					lastSelectedParamKind = clip->lastSelectedParamKind;
					lastSelectedParamID = clip->lastSelectedParamID;
				}
				knobPosLeft =
				    view.calculateKnobPosForDisplay(lastSelectedParamKind, lastSelectedParamID, lastPadSelectedKnobPos);
			}
		}

		// display parameter value if knobPos is provided
		if (knobPosLeft != kNoSelection) {
			char buffer[5];

			intToString(knobPosLeft, buffer);

			if (isUIModeActive(UI_MODE_NOTES_PRESSED)) {
				display->setText(buffer, true, 255, false);
			}
			else if (modEncoderAction || padSelectionOn) {
				display->displayPopup(buffer, 3, true);
			}
		}
		// display parameter name
		else {
			char parameterName[30];
			getParameterName(clip, outputType, parameterName);
			display->setScrollingText(parameterName);
		}
	}
}

// get's the name of the Parameter being edited so it can be displayed on the screen
void AutomationView::getParameterName(Clip* clip, OutputType outputType, char* parameterName) {
	if (onArrangerView || outputType == OutputType::SYNTH || outputType == OutputType::KIT
	    || outputType == OutputType::AUDIO) {
		params::Kind lastSelectedParamKind = params::Kind::NONE;
		int32_t lastSelectedParamID = kNoSelection;
		if (onArrangerView) {
			lastSelectedParamKind = currentSong->lastSelectedParamKind;
			lastSelectedParamID = currentSong->lastSelectedParamID;
		}
		else {
			lastSelectedParamKind = clip->lastSelectedParamKind;
			lastSelectedParamID = clip->lastSelectedParamID;
		}
		strncpy(parameterName, getParamDisplayName(lastSelectedParamKind, lastSelectedParamID), 29);
	}
	else if (outputType == OutputType::MIDI_OUT) {
		if (clip->lastSelectedParamID == CC_NUMBER_NONE) {
			strcpy(parameterName, deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_PARAM));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_PITCH_BEND) {
			strcpy(parameterName, deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_AFTERTOUCH) {
			strcpy(parameterName, deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_MOD_WHEEL) {
			strcpy(parameterName, deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
		}
		else {
			parameterName[0] = 'C';
			parameterName[1] = 'C';
			if (display->haveOLED()) {
				parameterName[2] = ' ';
				intToString(clip->lastSelectedParamID, &parameterName[3]);
			}
			else {
				char* numberStartPos;
				if (clip->lastSelectedParamID < 10) {
					parameterName[2] = ' ';
					numberStartPos = parameterName + 3;
				}
				else {
					numberStartPos = (clip->lastSelectedParamID < 100) ? (parameterName + 2) : (parameterName + 1);
				}
				intToString(clip->lastSelectedParamID, numberStartPos);
			}
		}
	}
}

// adjust the LED meters and update the display

/*updated function for displaying automation when playback is enabled (called from ui_timer_manager).
Also used internally in the automation instrument clip view for updating the display and led indicators.*/

void AutomationView::displayAutomation(bool padSelected, bool updateDisplay) {
	if ((!padSelectionOn && !isUIModeActive(UI_MODE_NOTES_PRESSED)) || padSelected) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings,
			                                                                    currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			Clip* clip = getCurrentClip();

			modelStackWithParam = getModelStackWithParam(modelStack, clip);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {

			if (modelStackWithParam->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				int32_t knobPos = getParameterKnobPos(modelStackWithParam, view.modPos);

				// update value on the screen when playing back automation
				if (updateDisplay && !playbackStopped) {
					renderDisplay(knobPos + kKnobPosOffset);
				}
				// on 7SEG re-render parameter name under certain circumstances
				// e.g. when entering pad selection mode, when stopping playback
				else {
					renderDisplay();
					playbackStopped = false;
				}

				setKnobIndicatorLevels(knobPos + kKnobPosOffset);
			}
		}
	}
}

// button action

ActionResult AutomationView::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	using namespace hid::button;

	Clip* clip = getCurrentClip();
	bool isAudioClip = clip->type == ClipType::AUDIO;

	// these button actions are not used in the audio clip automation view
	if (isAudioClip || onArrangerView) {
		if (b == SCALE_MODE || b == KEYBOARD || b == KIT || b == SYNTH || b == MIDI || b == CV) {
			return ActionResult::DEALT_WITH;
		}
	}
	if (onArrangerView) {
		if (b == CLIP_VIEW) {
			return ActionResult::DEALT_WITH;
		}
	}

	OutputType outputType = clip->output->type;

	// Scale mode button
	if (b == SCALE_MODE) {
		if (handleScaleButtonAction((InstrumentClip*)clip, outputType, on)) {
			return ActionResult::DEALT_WITH;
		}
	}

	// Song view button
	else if (b == SESSION_VIEW) {
		handleSessionButtonAction(clip, on);
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		handleKeyboardButtonAction(on);
	}

	// Clip button - exit mode
	// if you're holding shift or holding an audition pad while pressed clip, don't exit out of automation view
	// reset parameter selection and short blinking instead
	else if (b == CLIP_VIEW) {
		handleClipButtonAction(on, isAudioClip);
	}

	// Auto scrolling
	// Only works in arranger view (for now)
	else if (b == CROSS_SCREEN_EDIT) {
		if (onArrangerView) {
			handleCrossScreenButtonAction(on);
		}
		else {
			return ActionResult::DEALT_WITH;
		}
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == KIT && currentUIMode == UI_MODE_NONE) {
		handleKitButtonAction(outputType, on);
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == SYNTH && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		handleSynthButtonAction(outputType, on);
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == MIDI) {
		handleMidiButtonAction(outputType, on);
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == CV) {
		handleCVButtonAction(outputType, on);
	}

	// Horizontal encoder button
	// Not relevant for audio clip or arranger view
	else if (b == X_ENC) {
		if (handleHorizontalEncoderButtonAction(on, isAudioClip)) {
			goto passToOthers;
		}
	}

	// if holding horizontal encoder button down and pressing back clear automation
	// if you're on automation overview, clear all automation
	// if you're in the automation editor, clear the automation for the parameter in focus
	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (handleBackAndHorizontalEncoderButtonComboAction(clip, on)) {
			goto passToOthers;
		}
	}

	// Select encoder
	// if you're not pressing shift and press down on the select encoder, toggle interpolation on/off
	else if (!Buttons::isShiftButtonPressed() && b == SELECT_ENC) {
		handleSelectEncoderButtonAction(on);
	}

	// when you press affect entire in a kit, the parameter selection needs to reset
	else if (b == AFFECT_ENTIRE) {
		initParameterSelection();
		goto passToOthers;
	}

	else {
passToOthers:
		uiNeedsRendering(this);

		if (on && (b == PLAY) && display->have7SEG() && playbackHandler.isEitherClockActive()
		    && !isOnAutomationOverview() && !padSelectionOn) {
			playbackStopped = true;
		}

		ActionResult result;
		if (onArrangerView) {
			result = TimelineView::buttonAction(b, on, inCardRoutine);
		}
		else if (isAudioClip) {
			result = ClipMinder::buttonAction(b, on);
		}
		else {
			result = InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		}
		if (result == ActionResult::NOT_DEALT_WITH) {
			result = ClipView::buttonAction(b, on, inCardRoutine);
		}

		return result;
	}

	if (on && (b != KEYBOARD && b != CLIP_VIEW && b != SESSION_VIEW)) {
		uiNeedsRendering(this);
	}

	return ActionResult::DEALT_WITH;
}

// called by button action if b == SCALE_MODE
bool AutomationView::handleScaleButtonAction(InstrumentClip* instrumentClip, OutputType outputType, bool on) {
	// Kits can't do scales!
	if (outputType == OutputType::KIT) {
		if (on) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::KIT);
		}
		return true;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	if (on && currentUIMode == UI_MODE_NONE) {
		// If user holding shift and we're already in scale mode, cycle through available scales
		if (Buttons::isShiftButtonPressed() && instrumentClip->inScaleMode) {
			cycleThroughScales();
			instrumentClipView.recalculateColours();
			uiNeedsRendering(this);
		}
		else if (instrumentClip->inScaleMode) {
			exitScaleMode();
		}
		else {
			enterScaleMode();
		}
	}
	return false;
}

// called by button action if b == SESSION_VIEW
void AutomationView::handleSessionButtonAction(Clip* clip, bool on) {
	if (on && currentUIMode == UI_MODE_NONE) {
		if (onArrangerView) {
			onArrangerView = false;
			changeRootUI(&arrangerView);
		}
		else if (currentSong->lastClipInstanceEnteredStartPos != -1 || clip->isArrangementOnlyClip()) {
			bool success = arrangerView.transitionToArrangementEditor();
			if (!success) {
				goto doOther;
			}
		}
		else {
doOther:
			// Transition to session view
			// code taken from the keyboard screen instead of calling the transition to session view function
			currentUIMode = UI_MODE_INSTRUMENT_CLIP_COLLAPSING;
			int32_t transitioningToRow = sessionView.getClipPlaceOnScreen(clip);
			memcpy(&PadLEDs::imageStore, PadLEDs::image, sizeof(PadLEDs::image));
			memcpy(&PadLEDs::occupancyMaskStore, PadLEDs::occupancyMask, sizeof(PadLEDs::occupancyMask));
			PadLEDs::numAnimatedRows = kDisplayHeight;
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				PadLEDs::animatedRowGoingTo[y] = transitioningToRow;
				PadLEDs::animatedRowGoingFrom[y] = y;
			}

			PadLEDs::setupInstrumentClipCollapseAnimation(true);
			PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
			PadLEDs::renderClipExpandOrCollapse();
		}
		resetShortcutBlinking();
	}
}

// called by button action if b == KEYBOARD
void AutomationView::handleKeyboardButtonAction(bool on) {
	if (on && currentUIMode == UI_MODE_NONE) {
		changeRootUI(&keyboardScreen);
		// reset blinking if you're leaving automation view for keyboard view
		// blinking will be reset when you come back
		resetShortcutBlinking();
	}
}

// called by button action if b == CLIP_VIEW
void AutomationView::handleClipButtonAction(bool on, bool isAudioClip) {
	if (on && currentUIMode == UI_MODE_NONE) {
		if (Buttons::isShiftButtonPressed()) {
			initParameterSelection();
			uiNeedsRendering(this);
		}
		else {
			if (isAudioClip) {
				changeRootUI(&audioClipView);
			}
			else {
				changeRootUI(&instrumentClipView);
			}
		}
		resetShortcutBlinking();
	}
	else if (on && currentUIMode == UI_MODE_AUDITIONING) {
		initParameterSelection();
		resetShortcutBlinking();
		uiNeedsRendering(this);
	}
}

// call by button action if b == CROSS_SCREEN_EDIT
void AutomationView::handleCrossScreenButtonAction(bool on) {
	if (on && currentUIMode == UI_MODE_NONE) {
		currentSong->arrangerAutoScrollModeActive = !currentSong->arrangerAutoScrollModeActive;
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, currentSong->arrangerAutoScrollModeActive);

		if (currentSong->arrangerAutoScrollModeActive) {
			arrangerView.reassessWhetherDoingAutoScroll();
		}
		else {
			arrangerView.doingAutoScrollNow = false;
		}
	}
}

// called by button action if b == KIT
void AutomationView::handleKitButtonAction(OutputType outputType, bool on) {
	if (on) {
		// don't reset anything if you're already in a KIT clip
		if (outputType != OutputType::KIT) {
			initParameterSelection();
			resetShortcutBlinking();
		}
		if (Buttons::isNewOrShiftButtonPressed()) {
			instrumentClipView.createNewInstrument(OutputType::KIT);
		}
		else {
			instrumentClipView.changeOutputType(OutputType::KIT);
		}
	}
}

// called by button action if b == SYNTH
void AutomationView::handleSynthButtonAction(OutputType outputType, bool on) {
	if (on && currentUIMode == UI_MODE_NONE) {
		// don't reset anything if you're already in a SYNTH clip
		if (outputType != OutputType::SYNTH) {
			initParameterSelection();
			resetShortcutBlinking();
		}
		// this gets triggered when you change an existing clip to synth / create a new synth clip in song mode
		if (Buttons::isNewOrShiftButtonPressed()) {
			instrumentClipView.createNewInstrument(OutputType::SYNTH);
		}
		// this gets triggered when you change clip type to synth from within inside clip view
		else {
			instrumentClipView.changeOutputType(OutputType::SYNTH);
		}
	}
}

// called by button action if b == MIDI
void AutomationView::handleMidiButtonAction(OutputType outputType, bool on) {
	if (on && currentUIMode == UI_MODE_NONE) {
		// don't reset anything if you're already in a MIDI clip
		if (outputType != OutputType::MIDI_OUT) {
			initParameterSelection();
			resetShortcutBlinking();
		}
		instrumentClipView.changeOutputType(OutputType::MIDI_OUT);
	}
}

// called by button action if b == CV
void AutomationView::handleCVButtonAction(OutputType outputType, bool on) {
	if (on && currentUIMode == UI_MODE_NONE) {
		// don't reset anything if you're already in a CV clip
		if (outputType != OutputType::CV) {
			initParameterSelection();
			resetShortcutBlinking();
			displayCVErrorMessage();
		}
		instrumentClipView.changeOutputType(OutputType::CV);
	}
}

// called by button action if b == X_ENC
bool AutomationView::handleHorizontalEncoderButtonAction(bool on, bool isAudioClip) {
	if (isAudioClip || onArrangerView) {
		return true;
	}
	// If user wants to "multiple" Clip contents
	if (on && Buttons::isShiftButtonPressed() && !isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)
	    && !isOnAutomationOverview()) {
		if (isNoUIModeActive()) {
			// Zoom to max if we weren't already there...
			if (!zoomToMax()) {
				// Or if we didn't need to do that, double Clip length
				instrumentClipView.doubleClipLengthAction();
			}
			else {
				displayZoomLevel();
			}
		}
		// Whether or not we did the "multiply" action above, we need to be in this UI mode, e.g. for rotating
		// individual NoteRow
		enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
	}

	// Otherwise...
	else {
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			if (!on) {
				instrumentClipView.timeHorizontalKnobLastReleased = AudioEngine::audioSampleTimer;
			}
		}
		return true;
	}
	return false;
}

// called by button action if b == back and UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON
bool AutomationView::handleBackAndHorizontalEncoderButtonComboAction(Clip* clip, bool on) {
	// only allow clearing of a clip if you're in the automation overview
	if (on && isOnAutomationOverview()) {
		if (clip->type == ClipType::AUDIO || onArrangerView) {
			// clear all arranger automation
			if (onArrangerView) {
				Action* action = actionLogger.getNewAction(ActionType::ARRANGEMENT_CLEAR, ActionAddition::NOT_ALLOWED);

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack =
				    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
				currentSong->paramManager.deleteAllAutomation(action, modelStack);
			}
			// clear all audio clip automation
			else {
				Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

				clip->clear(action, modelStack);
			}
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUTOMATION_CLEARED));

			return false;
		}
		return true;
	}
	else if (on && !isOnAutomationOverview()) {
		// delete automation of current parameter selected

		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings,
			                                                                    currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			modelStackWithParam = getModelStackWithParam(modelStack, clip);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {
			Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE);
			modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

			displayAutomation(padSelectionOn, !display->have7SEG());
		}
	}
	return false;
}

// called by button action if b == SELECT_ENC and shift button is not pressed
void AutomationView::handleSelectEncoderButtonAction(bool on) {
	if (on) {
		if (interpolation == RuntimeFeatureStateToggle::Off) {

			interpolation = RuntimeFeatureStateToggle::On;

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_ENABLED));
		}
		else {
			interpolation = RuntimeFeatureStateToggle::Off;
			initInterpolation();

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_DISABLED));
		}
	}
}

// simplified version of the InstrumentClipView::enterScaleMode function. No need to render any animation.
// not used with Audio Clip Automation View or Arranger Automation View
void AutomationView::enterScaleMode(uint8_t yDisplay) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	int32_t newRootNote;
	if (yDisplay == 255) {
		newRootNote = 2147483647;
	}
	else {
		newRootNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
	}

	int32_t newScroll = instrumentClipView.setupForEnteringScaleMode(newRootNote, yDisplay);

	clip->yScroll = newScroll;

	displayCurrentScaleName();

	// And tidy up
	setLedStates();
}

// simplified version of the InstrumentClipView::enterScaleMode function. No need to render any animation.
// not used with Audio Clip Automation View or Arranger Automation View
void AutomationView::exitScaleMode() {
	int32_t scrollAdjust = instrumentClipView.setupForExitingScaleMode();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	clip->yScroll += scrollAdjust;

	instrumentClipView.recalculateColours();
	setLedStates();
}

// pad action
// handles shortcut pad action for automation (e.g. when you press shift + pad on the grid)
// everything else is pretty much the same as instrument clip view
ActionResult AutomationView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	Clip* clip = getCurrentClip();

	if (clip->type == ClipType::AUDIO) {
		if (x >= kDisplayWidth) {
			return ActionResult::DEALT_WITH;
		}
	}

	Output* output = clip->output;
	OutputType outputType = output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings,
		                                                                    currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParam(modelStackWithTimelineCounter, clip);
	}
	int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

	// Edit pad action...
	if (x < kDisplayWidth) {
		return handleEditPadAction(modelStackWithParam, clip, outputType, effectiveLength, x, y, velocity);
	}
	// mute / status pad action
	else if (x == kDisplayWidth) {
		return handleMutePadAction(modelStackWithTimelineCounter, (InstrumentClip*)clip, output, outputType, y,
		                           velocity);
	}
	// Audition pad action
	else {
		if (x == kDisplayWidth + 1) {
			return handleAuditionPadAction((InstrumentClip*)clip, output, outputType, y, velocity);
		}
	}

	return ActionResult::DEALT_WITH;
}

// called by pad action when pressing a pad in the mute column (x = kDisplayWidth)
ActionResult AutomationView::handleMutePadAction(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                 InstrumentClip* instrumentClip, Output* output, OutputType outputType,
                                                 int32_t y, int32_t velocity) {
	if (onArrangerView) {
		return arrangerView.handleStatusPadAction(y, velocity, this);
	}
	else {
		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			if (outputType != OutputType::KIT) {
				return ActionResult::DEALT_WITH;
			}
			NoteRow* noteRow = instrumentClip->getNoteRowOnScreen(y, currentSong);
			if (!noteRow || !noteRow->drum) {
				return ActionResult::DEALT_WITH;
			}
			view.noteRowMuteMidiLearnPadPressed(velocity, noteRow);
		}
		else if (isUIModeWithinRange(mutePadActionUIModes) && velocity) {
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    instrumentClip->getNoteRowOnScreen(y, modelStackWithTimelineCounter);

			// if we're in a kit, and you press a mute pad
			// check if it's a mute pad corresponding to the current selected drum
			// if not, change the drum selection, refresh parameter selection and go back to automation overview
			if (outputType == OutputType::KIT) {
				if (modelStackWithNoteRow->getNoteRowAllowNull()) {
					Drum* drum = modelStackWithNoteRow->getNoteRow()->drum;
					if (((Kit*)output)->selectedDrum != drum) {
						if (!instrumentClipView.getAffectEntire()) {
							initParameterSelection();
						}
					}
				}
			}

			instrumentClipView.mutePadPress(y);

			uiNeedsRendering(this); // re-render mute pads
		}
	}
	return ActionResult::DEALT_WITH;
}

// called by pad action when pressing a pad in the main grid (x < kDisplayWidth)
ActionResult AutomationView::handleEditPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                 OutputType outputType, int32_t effectiveLength, int32_t x, int32_t y,
                                                 int32_t velocity) {
	if (outputType == OutputType::CV) {
		displayCVErrorMessage();
		return ActionResult::DEALT_WITH;
	}

	if (onArrangerView && isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		return ActionResult::DEALT_WITH;
	}

	int32_t xScroll = currentSong->xScroll[navSysId];
	int32_t xZoom = currentSong->xZoom[navSysId];

	// if the user wants to change the parameter they are editing using Shift + Pad shortcut
	if (velocity) {
		if (Buttons::isShiftButtonPressed()
		    || (isUIModeActive(UI_MODE_AUDITIONING) && !FlashStorage::automationDisableAuditionPadShortcuts)) {
			initPadSelection();
			handleSinglePadPress(modelStackWithParam, clip, x, y, effectiveLength, xScroll, xZoom, true);

			return ActionResult::DEALT_WITH;
		}
	}
	// regular automation step editing action
	if (isUIModeWithinRange(editPadActionUIModes)) {
		editPadAction(modelStackWithParam, clip, velocity, y, x, effectiveLength, xScroll, xZoom);
	}
	return ActionResult::DEALT_WITH;
}

// edit pad action
// handles single and multi pad presses for automation editing and for parameter selection on the automation overview
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationView::editPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, bool state,
                                   uint8_t yDisplay, uint8_t xDisplay, int32_t effectiveLength, int32_t xScroll,
                                   int32_t xZoom) {
	// If button down
	if (state) {
		if (!isSquareDefined(xDisplay, xScroll, xZoom)) {

			return;
		}
		// If this is a automation-length-edit press...
		// needed for Automation
		if (!isOnAutomationOverview() && instrumentClipView.numEditPadPresses == 1) {

			int32_t firstPadX = 255;
			int32_t firstPadY = 255;

			// Find that original press
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (instrumentClipView.editPadPresses[i].isActive) {

					firstPadX = instrumentClipView.editPadPresses[i].xDisplay;
					firstPadY = instrumentClipView.editPadPresses[i].yDisplay;

					break;
				}
			}

			if (firstPadX != 255 && firstPadY != 255) {
				if (firstPadX != xDisplay) {
					recordSinglePadPress(xDisplay, yDisplay);

					multiPadPressSelected = true;
					multiPadPressActive = true;

					// the long press logic calculates and renders the interpolation as if the press was entered in a
					// forward fashion (where the first pad is to the left of the second pad). if the user happens to
					// enter a long press backwards then we fix that entry by re-ordering the pad presses so that it is
					// forward again
					leftPadSelectedX = firstPadX > xDisplay ? xDisplay : firstPadX;
					leftPadSelectedY = firstPadX > xDisplay ? yDisplay : firstPadY;
					rightPadSelectedX = firstPadX > xDisplay ? firstPadX : xDisplay;
					rightPadSelectedY = firstPadX > xDisplay ? firstPadY : yDisplay;

					// if you're not in pad selection mode, allow user to enter a long press
					if (!padSelectionOn) {
						handleMultiPadPress(modelStackWithParam, clip, leftPadSelectedX, leftPadSelectedY,
						                    rightPadSelectedX, rightPadSelectedY, effectiveLength, xScroll, xZoom);
					}
					else {
						uiNeedsRendering(this);
					}

					// set led indicators to left / right pad selection values
					// and update display
					renderDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom, xDisplay);
				}
				else {
					leftPadSelectedY = firstPadY;
					middlePadPressSelected = true;
					goto singlePadPressAction;
				}
			}
		}

		// Or, if this is a regular create-or-select press...
		else {
singlePadPressAction:
			if (recordSinglePadPress(xDisplay, yDisplay)) {
				multiPadPressActive = false;
				handleSinglePadPress(modelStackWithParam, clip, xDisplay, yDisplay, effectiveLength, xScroll, xZoom);
			}
		}
	}

	// Or if pad press ended...
	else {
		// Find the corresponding press, if there is one
		int32_t i;
		for (i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive
			    && instrumentClipView.editPadPresses[i].yDisplay == yDisplay
			    && instrumentClipView.editPadPresses[i].xDisplay == xDisplay) {
				break;
			}
		}

		// If we found it...
		if (i < kEditPadPressBufferSize) {
			instrumentClipView.endEditPadPress(i);

			instrumentClipView.checkIfAllEditPadPressesEnded();
		}

		// outside pad selection mode, exit multi pad press once you've let go of the first pad in the long press
		if (!isOnAutomationOverview() && !padSelectionOn && multiPadPressSelected
		    && (currentUIMode != UI_MODE_NOTES_PRESSED)) {
			initPadSelection();
		}
		// switch from long press selection to short press selection in pad selection mode
		else if (!isOnAutomationOverview() && padSelectionOn && multiPadPressSelected && !multiPadPressActive
		         && (currentUIMode != UI_MODE_NOTES_PRESSED)
		         && ((AudioEngine::audioSampleTimer - instrumentClipView.timeLastEditPadPress) < kShortPressTime)) {

			multiPadPressSelected = false;

			leftPadSelectedX = xDisplay;
			rightPadSelectedX = kNoSelection;

			uiNeedsRendering(this);
		}

		if (!isOnAutomationOverview() && (currentUIMode != UI_MODE_NOTES_PRESSED)) {
			lastPadSelectedKnobPos = kNoSelection;
			if (multiPadPressSelected) {
				renderDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom, xDisplay);
			}
			else if (!playbackHandler.isEitherClockActive()) {
				displayAutomation(padSelectionOn, !display->have7SEG());
			}
		}

		middlePadPressSelected = false;
	}
}

bool AutomationView::recordSinglePadPress(int32_t xDisplay, int32_t yDisplay) {
	instrumentClipView.timeLastEditPadPress = AudioEngine::audioSampleTimer;
	// Find an empty space in the press buffer, if there is one
	int32_t i;
	for (i = 0; i < kEditPadPressBufferSize; i++) {
		if (!instrumentClipView.editPadPresses[i].isActive) {
			break;
		}
	}
	if (i < kEditPadPressBufferSize) {
		instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;

		// If this is the first press, record the time
		if (instrumentClipView.numEditPadPresses == 0) {
			instrumentClipView.timeFirstEditPadPress = AudioEngine::audioSampleTimer;
			instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
		}

		instrumentClipView.editPadPresses[i].isActive = true;
		instrumentClipView.editPadPresses[i].yDisplay = yDisplay;
		instrumentClipView.editPadPresses[i].xDisplay = xDisplay;
		instrumentClipView.numEditPadPresses++;
		instrumentClipView.numEditPadPressesPerNoteRowOnScreen[yDisplay]++;
		enterUIMode(UI_MODE_NOTES_PRESSED);

		return true;
	}
	return false;
}

// called by pad action when pressing a pad in the audition column (x = kDisplayWidth + 1)
ActionResult AutomationView::handleAuditionPadAction(InstrumentClip* instrumentClip, Output* output,
                                                     OutputType outputType, int32_t y, int32_t velocity) {
	if (onArrangerView) {
		if (isOnAutomationOverview()) {
			return arrangerView.handleAuditionPadAction(y, velocity, this);
		}
	}
	else {
		// "Learning" to this audition pad:
		if (isUIModeActiveExclusively(UI_MODE_MIDI_LEARN)) {
			if (getCurrentUI() == this) {
				if (outputType == OutputType::KIT) {
					NoteRow* thisNoteRow = instrumentClip->getNoteRowOnScreen(y, currentSong);
					if (!thisNoteRow || !thisNoteRow->drum) {
						return ActionResult::DEALT_WITH;
					}
					view.drumMidiLearnPadPressed(velocity, thisNoteRow->drum, (Kit*)output);
				}
				else {
					view.melodicInstrumentMidiLearnPadPressed(velocity, (MelodicInstrument*)output);
				}
			}
		}

		// Actual basic audition pad press:
		else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
			auditionPadAction(velocity, y, Buttons::isShiftButtonPressed());
		}
	}
	return ActionResult::DEALT_WITH;
}

// audition pad action
// not used with Audio Clip Automation View or Arranger Automation View
void AutomationView::auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	InstrumentClip* clip = getCurrentInstrumentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	bool isKit = (outputType == OutputType::KIT);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
	    clip->getNoteRowOnScreen(yDisplay, modelStackWithTimelineCounter);

	Drum* drum = NULL;

	bool selectedDrumChanged = false;
	bool drawNoteCode = false;

	// If Kit...
	if (isKit) {

		// if we're in a kit, and you press an audition pad
		// check if it's a audition pad corresponding to the current selected drum
		// also check that you're not in affect entire mode
		// if not, change the drum selection, refresh parameter selection and go back to automation overview
		if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
			drum = modelStackWithNoteRowOnCurrentClip->getNoteRow()->drum;
			Drum* selectedDrum = ((Kit*)output)->selectedDrum;
			if (selectedDrum != drum) {
				selectedDrumChanged = true;
			}
		}

		// If NoteRow doesn't exist here, we'll see about creating one
		else {

			// But not if we're actually not on this screen
			if (getCurrentUI() != this) {
				return;
			}

			// Press-down
			if (velocity) {
				instrumentClipView.setSelectedDrum(NULL);
				selectedDrumChanged = true;
			}

			goto getOut;
		}
	}

	// Or if synth
	else if (outputType == OutputType::SYNTH) {
		if (velocity) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
				menu_item::multiRangeMenu.noteOnToChangeRange(clip->getYNoteFromYDisplay(yDisplay, currentSong)
				                                              + ((SoundInstrument*)output)->transpose);
			}
		}
	}

	// Recording - only allowed if currentClip is activeClip
	if (clipIsActiveOnInstrument && playbackHandler.shouldRecordNotesNow() && currentSong->isClipActive(clip)) {

		// Note-on
		if (velocity) {

			// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
			// This is basic. For MIDI input, we do this in a couple more cases - see noteMessageReceived()
			// in MelodicInstrument and Kit
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) {
				if (isKit) {
					if (drum) {
						drum->recordNoteOnEarly(
						    (velocity == USE_DEFAULT_VELOCITY) ? ((Instrument*)output)->defaultVelocity : velocity,
						    clip->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
					}
				}
				else {
					// NoteRow is allowed to be NULL in this case.
					int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
					((MelodicInstrument*)output)
					    ->earlyNotes.insertElementIfNonePresent(
					        yNote, ((Instrument*)output)->defaultVelocity,
					        clip->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
				}
			}

			else {

				// May need to create NoteRow if there wasn't one previously
				if (!modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {

					modelStackWithNoteRowOnCurrentClip =
					    instrumentClipView.createNoteRowForYDisplay(modelStackWithTimelineCounter, yDisplay);
				}

				if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
					clip->recordNoteOn(modelStackWithNoteRowOnCurrentClip, (velocity == USE_DEFAULT_VELOCITY)
					                                                           ? ((Instrument*)output)->defaultVelocity
					                                                           : velocity);
				}
			}
		}

		// Note-off
		else {

			if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
				clip->recordNoteOff(modelStackWithNoteRowOnCurrentClip);
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
			if (isKit) {
				noteRowOnActiveClip = ((InstrumentClip*)output->activeClip)->getNoteRowForDrum(drum);
			}

			// Non-kit
			else {
				int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
				noteRowOnActiveClip = ((InstrumentClip*)output->activeClip)->getNoteRowForYNote(yNote);
			}
		}

		// If note on...
		if (velocity) {
			int32_t velocityToSound = velocity;
			if (velocityToSound == USE_DEFAULT_VELOCITY) {
				velocityToSound = ((Instrument*)output)->defaultVelocity;
			}

			// Yup, need to do this even if we're going to do a "silent" audition, so pad lights up etc.
			instrumentClipView.auditionPadIsPressed[yDisplay] = velocityToSound;

			if (noteRowOnActiveClip) {
				// Ensure our auditioning doesn't override a note playing in the sequence
				if (playbackHandler.isEitherClockActive()
				    && noteRowOnActiveClip->soundingStatus == STATUS_SEQUENCED_NOTE) {
					goto doSilentAudition;
				}
			}

			// If won't be actually sounding Instrument...
			if (shiftButtonDown || Buttons::isButtonPressed(hid::button::Y_ENC)) {

				instrumentClipView.fileBrowserShouldNotPreview = true;
doSilentAudition:
				instrumentClipView.auditioningSilently = true;
				instrumentClipView.reassessAllAuditionStatus();
			}
			else {
				if (!instrumentClipView.auditioningSilently) {

					instrumentClipView.fileBrowserShouldNotPreview = false;

					instrumentClipView.sendAuditionNote(true, yDisplay, velocityToSound, 0);

					{ instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = velocityToSound; }
				}
			}

			// If wasn't already auditioning...
			if (!isUIModeActive(UI_MODE_AUDITIONING)) {
				instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				// instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = false;
				enterUIMode(UI_MODE_AUDITIONING);
			}

			drawNoteCode = true;
			instrumentClipView.lastAuditionedYDisplay = yDisplay;

			// Begin resampling / output-recording
			if (Buttons::isButtonPressed(hid::button::RECORD)
			    && audioRecorder.recordingSource == AudioInputChannel::NONE) {
				audioRecorder.beginOutputRecording();
				Buttons::recordButtonPressUsedUp = true;
			}

			if (isKit) {
				instrumentClipView.setSelectedDrum(drum);
			}
		}

		// Or if auditioning this NoteRow just finished...
		else {
			if (instrumentClipView.auditionPadIsPressed[yDisplay]) {
				instrumentClipView.auditionPadIsPressed[yDisplay] = 0;
				instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;

				// Stop the note sounding - but only if a sequenced note isn't in fact being played here.
				if (!noteRowOnActiveClip || noteRowOnActiveClip->soundingStatus == STATUS_OFF) {
					instrumentClipView.sendAuditionNote(false, yDisplay, 64, 0);
				}
			}
			display->cancelPopup();
			instrumentClipView.someAuditioningHasEnded(true);
			actionLogger.closeAction(ActionType::NOTEROW_ROTATE);
			if (display->have7SEG()) {
				renderDisplay();
			}
		}
	}

getOut:

	if (selectedDrumChanged && !instrumentClipView.getAffectEntire()) {
		initParameterSelection();
		uiNeedsRendering(this); // need to redraw automation grid squares cause selected drum may have changed
	}
	else {
		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
	}

	// draw note code on top of the automation view display which may have just been refreshed
	if (drawNoteCode) {
		instrumentClipView.drawNoteCode(yDisplay);
	}

	// This has to happen after instrumentClipView.setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
	}
}

// horizontal encoder action
// using this to shift automations left / right
ActionResult AutomationView::horizontalEncoderAction(int32_t offset) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
	}

	// exit multi pad press selection but keep single pad press selection (if it's selected)
	multiPadPressSelected = false;
	rightPadSelectedX = kNoSelection;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	}

	encoderAction = true;

	if (!isOnAutomationOverview()
	    && ((isNoUIModeActive() && Buttons::isButtonPressed(hid::button::Y_ENC))
	        || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	            && Buttons::isButtonPressed(hid::button::CLIP_VIEW))
	        || (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)))) {

		int32_t xScroll = currentSong->xScroll[navSysId];
		int32_t xZoom = currentSong->xZoom[navSysId];
		int32_t squareSize = getPosFromSquare(1, xScroll, xZoom) - getPosFromSquare(0, xScroll, xZoom);
		int32_t shiftAmount = offset * squareSize;

		if (onArrangerView) {
			modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings,
			                                                                    currentSong->lastSelectedParamID);
		}
		else {
			Clip* clip = getCurrentClip();
			modelStackWithParam = getModelStackWithParam(modelStackWithTimelineCounter, clip);
		}

		int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

		shiftAutomationHorizontally(modelStackWithParam, shiftAmount, effectiveLength);

		if (offset < 0) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_LEFT));
		}
		else if (offset > 0) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_RIGHT));
		}

		return ActionResult::DEALT_WITH;
	}

	// else if showing the Parameter selection grid menu, disable this action
	else if (isOnAutomationOverview()) {
		return ActionResult::DEALT_WITH;
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		if (!instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
wantToEditNoteRowLength:
			ModelStackWithNoteRow* modelStackWithNoteRow = instrumentClipView.getOrCreateNoteRowForYDisplay(
			    modelStackWithTimelineCounter, instrumentClipView.lastAuditionedYDisplay);

			instrumentClipView.editNoteRowLength(modelStackWithNoteRow, offset,
			                                     instrumentClipView.lastAuditionedYDisplay);
			// instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			uiNeedsRendering(this);
		}

		// Unlike for all other cases where we protect against the user accidentally turning the encoder more after
		// releasing their press on it, for this edit-NoteRow-length action, because it's a related action, it's quite
		// likely that the user actually will want to do it after the yes-pressed-encoder-down action, which is
		// "rotate/shift notes in row". So, we have a 250ms timeout for this one.
		else if ((uint32_t)(AudioEngine::audioSampleTimer - instrumentClipView.timeHorizontalKnobLastReleased)
		         >= 250 * 44) {
			instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
			goto wantToEditNoteRowLength;
		}
		return ActionResult::DEALT_WITH;
	}

	// Or, let parent deal with it
	else {
		ActionResult result = ClipView::horizontalEncoderAction(offset);
		uiNeedsRendering(this);
		return result;
	}
}

// new function created for automation instrument clip view to shift automations of the selected parameter
// previously users only had the option to shift ALL automations together
// as part of community feature i disabled automation shifting in the regular instrument clip view
void AutomationView::shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
                                                 int32_t effectiveLength) {
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		modelStackWithParam->autoParam->shiftHorizontally(offset, effectiveLength);
	}

	uiNeedsRendering(this);
}

// vertical encoder action
// no change compared to instrument clip view version
// not used with Audio Clip Automation View or Arranger Automation View
ActionResult AutomationView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (getCurrentClip()->type == ClipType::AUDIO) {
		return ActionResult::DEALT_WITH;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	OutputType outputType = clip->output->type;
	encoderAction = true;

	// If encoder button pressed
	if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
		// If user not wanting to move a noteCode, they want to transpose the key
		if (!currentUIMode && outputType != OutputType::KIT) {
			actionLogger.deleteAllLogs();

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			// If shift button not pressed, transpose whole octave
			if (!Buttons::isShiftButtonPressed()) {
				offset = std::min((int32_t)1, std::max((int32_t)-1, offset));
				clip->transpose(offset * 12, modelStack);
				if (clip->isScaleModeClip()) {
					clip->yScroll += offset * (currentSong->numModeNotes - 12);
				}
				// display->displayPopup("OCTAVE");
			}

			// Otherwise, transpose single semitone
			else {
				// If current Clip not in scale-mode, just do it
				if (!clip->isScaleModeClip()) {
					clip->transpose(offset, modelStack);

					// If there are no scale-mode Clips at all, move the root note along as well - just in case the user
					// wants to go back to scale mode (in which case the "previous" root note would be used to help
					// guess what root note to go with)
					if (!currentSong->anyScaleModeClips()) {
						currentSong->rootNote += offset;
					}
				}

				// Otherwise, got to do all key-mode Clips
				else {
					currentSong->transposeAllScaleModeClips(offset);
				}
				// display->displayPopup("SEMITONE");
			}
		}
	}

	// Or, if shift key is pressed
	else if (Buttons::isShiftButtonPressed()) {
		uint32_t whichRowsToRender = 0;

		// If NoteRow(s) auditioned, shift its colour (Kits only)
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			// instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			if (!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				if (outputType != OutputType::KIT) {
					goto shiftAllColour;
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
					if (instrumentClipView.auditionPadIsPressed[yDisplay]) {
						ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);
						NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
						// This is fine. If we were in Kit mode, we could only be auditioning if there was a NoteRow
						// already
						if (noteRow) {
							noteRow->colourOffset += offset;
							if (noteRow->colourOffset >= 72) {
								noteRow->colourOffset -= 72;
							}
							if (noteRow->colourOffset < 0) {
								noteRow->colourOffset += 72;
							}
							instrumentClipView.recalculateColour(yDisplay);
							whichRowsToRender |= (1 << yDisplay);
						}
					}
				}
			}
		}

		// Otherwise, adjust whole colour spectrum
		else if (currentUIMode == UI_MODE_NONE) {
shiftAllColour:
			clip->colourOffset += offset;
			instrumentClipView.recalculateColours();
			whichRowsToRender = 0xFFFFFFFF;
		}

		if (whichRowsToRender) {
			uiNeedsRendering(this, whichRowsToRender, whichRowsToRender);
		}
	}

	// If neither button is pressed, we'll do vertical scrolling
	else {
		if (isUIModeWithinRange(verticalScrollUIModes)) {
			if (!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress
			    || (!isUIModeActive(UI_MODE_NOTES_PRESSED) && !isUIModeActive(UI_MODE_AUDITIONING))) {

				return scrollVertical(offset);
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

// don't think I touch anything here compared to the instrument clip view version
// so I might be able to remove this function and simply call the instrumentClipView.scrollVertical
// I'm not doing anything with it
// But this function could come in handy in the future if I implement vertical zooming
// Not used with Audio Clip Automation View or Arranger Automation View
ActionResult AutomationView::scrollVertical(int32_t scrollAmount) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	int32_t noteRowToShiftI;
	int32_t noteRowToSwapWithI;

	bool isKit = outputType == OutputType::KIT;

	// If a Kit...
	if (isKit) {
		// Limit scrolling
		if (scrollAmount >= 0) {
			if ((int16_t)(clip->yScroll + scrollAmount) > (int16_t)(clip->getNumNoteRows() - 1)) {
				return ActionResult::DEALT_WITH;
			}
		}
		else {
			if (clip->yScroll + scrollAmount < 1 - kDisplayHeight) {
				return ActionResult::DEALT_WITH;
			}
		}
	}

	// Or if not a Kit...
	else {
		int32_t newYNote;
		if (scrollAmount > 0) {
			newYNote = clip->getYNoteFromYDisplay(kDisplayHeight - 1 + scrollAmount, currentSong);
		}
		else {
			newYNote = clip->getYNoteFromYDisplay(scrollAmount, currentSong);
		}

		if (!clip->isScrollWithinRange(scrollAmount, newYNote)) {
			return ActionResult::DEALT_WITH;
		}
	}

	bool currentClipIsActive = currentSong->isClipActive(clip);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Switch off any auditioned notes. But leave on the one whose NoteRow we're moving, if we are
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		instrumentClipView.sendAuditionNote(false, yDisplay, 127, 0);

		ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

		if (noteRow) {
			// If recording, record a note-off for this NoteRow, if one exists
			if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {
				clip->recordNoteOff(modelStackWithNoteRow);
			}
		}
	}

	// Do actual scroll
	clip->yScroll += scrollAmount;

	// Don't render - we'll do that after we've dealt with presses (potentially creating Notes)
	instrumentClipView.recalculateColours();

	// Switch on any auditioned notes - remembering that the one we're shifting (if we are) was left on before
	bool drawnNoteCodeYet = false;
	bool forceStoppedAnyAuditioning = false;
	bool changedActiveModControllable = false;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] != 255) {
			// switch its audition back on
			//  Check NoteRow exists, incase we've got a Kit
			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

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
							clip->recordNoteOn(modelStackWithNoteRow, ((Instrument*)output)->defaultVelocity);
						}
					}

					// Should this technically grab the note-length of the note if there is one?
					instrumentClipView.sendAuditionNote(true, yDisplay,
					                                    instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay], 0);
				}
			}
			else {
				instrumentClipView.auditionPadIsPressed[yDisplay] = false;
				instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;
				forceStoppedAnyAuditioning = true;
			}
			// If we're shiftingNoteRow, no need to re-draw the noteCode, because it'll be the same
			if (!drawnNoteCodeYet && instrumentClipView.auditionPadIsPressed[yDisplay]) {
				instrumentClipView.drawNoteCode(yDisplay);
				if (isKit) {
					Drum* newSelectedDrum = NULL;
					NoteRow* noteRow = clip->getNoteRowOnScreen(yDisplay, currentSong);
					if (noteRow) {
						newSelectedDrum = noteRow->drum;
					}
					instrumentClipView.setSelectedDrum(newSelectedDrum, true);
					changedActiveModControllable = !instrumentClipView.getAffectEntire();
				}

				if (outputType == OutputType::SYNTH) {
					if (getCurrentUI() == &soundEditor
					    && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
						menu_item::multiRangeMenu.noteOnToChangeRange(clip->getYNoteFromYDisplay(yDisplay, currentSong)
						                                              + ((SoundInstrument*)output)->transpose);
					}
				}

				drawnNoteCodeYet = true;
			}
		}
	}
	if (forceStoppedAnyAuditioning) {
		instrumentClipView.someAuditioningHasEnded(true);
	}

	uiNeedsRendering(this);
	return ActionResult::DEALT_WITH;
}

// mod encoder action

// used to change the value of a step when you press and hold a pad on the timeline
// used to record live automations in
void AutomationView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings,
		                                                                    currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = getCurrentClip();
		modelStackWithParam = getModelStackWithParam(modelStackWithTimelineCounter, clip);
	}
	int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

	encoderAction = true;

	// if user holding a node down, we'll adjust the value of the selected parameter being automated
	if (isUIModeActive(UI_MODE_NOTES_PRESSED) || padSelectionOn) {
		if (!isOnAutomationOverview()
		    && ((instrumentClipView.numEditPadPresses > 0
		         && ((int32_t)(instrumentClipView.timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0))
		        || padSelectionOn)) {

			if (modEncoderActionForSelectedPad(modelStackWithParam, whichModEncoder, offset, effectiveLength)) {
				return;
			}
		}
		else {
			goto followOnAction;
		}
	}
	// if playback is enabled and you are recording, you will be able to record in live automations for the selected
	// parameter this code is also executed if you're just changing the current value of the parameter at the current
	// mod position
	else {
		if (!isOnAutomationOverview()) {
			modEncoderActionForUnselectedPad(modelStackWithParam, whichModEncoder, offset, effectiveLength);
		}
		else {
			goto followOnAction;
		}
	}

	uiNeedsRendering(this);
	return;

followOnAction:
	ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
}

bool AutomationView::modEncoderActionForSelectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                    int32_t whichModEncoder, int32_t offset, int32_t effectiveLength) {
	Clip* clip = getCurrentClip();

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		int32_t xDisplay = 0;

		// for a multi pad press, adjust value of first or last pad depending on mod encoder turned
		if (multiPadPressSelected) {
			if (whichModEncoder == 0) {
				xDisplay = leftPadSelectedX;
			}
			else if (whichModEncoder == 1) {
				xDisplay = rightPadSelectedX;
			}
		}

		// if not multi pad press, but in pad selection mode, then just adjust the single selected pad
		else if (padSelectionOn) {
			xDisplay = leftPadSelectedX;
		}

		// otherwise if not in pad selection mode, adjust the value of the pad currently being held
		else {
			// find pads that are currently pressed
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (instrumentClipView.editPadPresses[i].isActive) {
					xDisplay = instrumentClipView.editPadPresses[i].xDisplay;
				}
			}
		}

		uint32_t squareStart = 0;

		int32_t xScroll = currentSong->xScroll[navSysId];
		int32_t xZoom = currentSong->xZoom[navSysId];

		// for the second pad pressed in a long press, the square start position is set to the very last nodes position
		if (multiPadPressSelected && (whichModEncoder == 1)) {
			int32_t squareRightEdge = getPosFromSquare(xDisplay + 1, xScroll, xZoom);
			squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
		}
		else {
			squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
		}

		if (squareStart < effectiveLength) {

			int32_t knobPos = getParameterKnobPos(modelStackWithParam, squareStart);

			int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a value change gets
			// recorded if newKnobPos exceeds 127, then it means current knobPos was 127 and it was increased to 128. In
			// which case, ignore value change
			if (!onArrangerView && ((clip->output->type == OutputType::MIDI_OUT) && (newKnobPos == 64))) {
				return true;
			}

			// use default interpolation settings
			initInterpolation();

			setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength,
			                            xScroll, xZoom, true);

			// once first or last pad in a multi pad press is adjusted, re-render calculate multi pad press based on
			// revised start/ending values
			if (multiPadPressSelected) {

				handleMultiPadPress(modelStackWithParam, clip, leftPadSelectedX, 0, rightPadSelectedX, 0,
				                    effectiveLength, xScroll, xZoom, true);

				renderDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom, xDisplay,
				                              true);

				return true;
			}
		}
	}

	return false;
}

void AutomationView::modEncoderActionForUnselectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                      int32_t whichModEncoder, int32_t offset,
                                                      int32_t effectiveLength) {
	Clip* clip = getCurrentClip();

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		if (modelStackWithParam->getTimelineCounter()
		    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

			int32_t knobPos = getParameterKnobPos(modelStackWithParam, view.modPos);

			int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a value change gets
			// recorded if newKnobPos exceeds 127, then it means current knobPos was 127 and it was increased to 128. In
			// which case, ignore value change
			if (!onArrangerView && ((clip->output->type == OutputType::MIDI_OUT) && (newKnobPos == 64))) {
				return;
			}

			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

			// use default interpolation settings
			initInterpolation();

			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, view.modPos,
			                                                          view.modLength);

			if (!onArrangerView) {
				modelStackWithParam->getTimelineCounter()->instrumentBeenEdited();
			}

			if (!playbackHandler.isEitherClockActive()) {
				renderDisplay(newKnobPos + kKnobPosOffset, kNoSelection, true);
				setKnobIndicatorLevels(newKnobPos + kKnobPosOffset);
			}

			// midi follow and midi feedback enabled
			// re-send midi cc because learned parameter value has changed
			view.sendMidiFollowFeedback(modelStackWithParam, newKnobPos);
		}
	}
}

// used to copy paste automation or to delete automation of the current selected parameter
void AutomationView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	Clip* clip = getCurrentClip();
	OutputType outputType = clip->output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings,
		                                                                    currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParam(modelStackWithTimelineCounter, clip);
	}
	int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

	int32_t xScroll = currentSong->xScroll[navSysId];
	int32_t xZoom = currentSong->xZoom[navSysId];

	// If they want to copy or paste automation...
	if (Buttons::isButtonPressed(hid::button::LEARN)) {
		if (on && outputType != OutputType::CV) {
			if (Buttons::isShiftButtonPressed()) {
				// paste within Automation Editor
				if (!isOnAutomationOverview()) {
					pasteAutomation(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
				}
				// paste on Automation Overview
				else {
					instrumentClipView.pasteAutomation(whichModEncoder, navSysId);
				}
			}
			else {
				// copy within Automation Editor
				if (!isOnAutomationOverview()) {
					copyAutomation(modelStackWithParam, clip, xScroll, xZoom);
				}
				// copy on Automation Overview
				else {
					instrumentClipView.copyAutomation(whichModEncoder, navSysId);
				}
			}
		}
	}

	// delete automation of current parameter selected
	else if (Buttons::isShiftButtonPressed() && !isOnAutomationOverview()) {
		if (modelStackWithParam && modelStackWithParam->autoParam) {
			Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE);
			modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

			displayAutomation(padSelectionOn, !display->have7SEG());
		}
	}

	// enter/exit pad selection mode
	else if (!isOnAutomationOverview()) {
		if (on) {
			if (padSelectionOn) {

				display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_OFF));

				initPadSelection();
				if (!playbackHandler.isEitherClockActive()) {
					displayAutomation(true, !display->have7SEG());
				}
			}
			else {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_ON));

				padSelectionOn = true;
				multiPadPressSelected = false;
				multiPadPressActive = false;

				// display only left cursor initially
				leftPadSelectedX = 0;
				rightPadSelectedX = kNoSelection;

				uint32_t squareStart = getMiddlePosFromSquare(leftPadSelectedX, effectiveLength, xScroll, xZoom);

				updateModPosition(modelStackWithParam, squareStart, !display->have7SEG());
			}
		}
	}
	else if (isOnAutomationOverview()) {
		goto followOnAction;
	}

	uiNeedsRendering(this);
	return;

followOnAction: // it will come here when you are on the automation overview iscreen

	view.modEncoderButtonAction(whichModEncoder, on);
	uiNeedsRendering(this);
}

void AutomationView::copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xScroll,
                                    int32_t xZoom) {
	if (copiedParamAutomation.nodes) {
		delugeDealloc(copiedParamAutomation.nodes);
		copiedParamAutomation.nodes = NULL;
		copiedParamAutomation.numNodes = 0;
	}

	int32_t startPos = getPosFromSquare(0, xScroll, xZoom);
	int32_t endPos = getPosFromSquare(kDisplayWidth, xScroll, xZoom);
	if (startPos == endPos) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		bool isPatchCable = (modelStackWithParam->paramCollection
		                     == modelStackWithParam->paramManager->getPatchCableSetAllowJibberish());
		// Ok this is cursed, but will work fine so long as
		// the possibly invalid memory here doesn't accidentally
		// equal modelStack->paramCollection.

		modelStackWithParam->autoParam->copy(startPos, endPos, &copiedParamAutomation, isPatchCable,
		                                     modelStackWithParam);

		if (copiedParamAutomation.nodes) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_COPIED));
			return;
		}
	}

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_AUTOMATION_TO_COPY));
}

void AutomationView::pasteAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t effectiveLength,
                                     int32_t xScroll, int32_t xZoom) {
	if (!copiedParamAutomation.nodes) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_AUTOMATION_TO_PASTE));
		return;
	}

	int32_t startPos = getPosFromSquare(0, xScroll, xZoom);
	int32_t endPos = getPosFromSquare(kDisplayWidth, xScroll, xZoom);

	int32_t pastedAutomationWidth = endPos - startPos;
	if (pastedAutomationWidth == 0) {
		return;
	}

	float scaleFactor = (float)pastedAutomationWidth / copiedParamAutomation.width;

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_PASTE);

		if (action) {
			action->recordParamChangeIfNotAlreadySnapshotted(modelStackWithParam, false);
		}

		bool isPatchCable = (modelStackWithParam->paramCollection
		                     == modelStackWithParam->paramManager->getPatchCableSetAllowJibberish());
		// Ok this is cursed, but will work fine so long as
		// the possibly invalid memory here doesn't accidentally
		// equal modelStack->paramCollection.

		modelStackWithParam->autoParam->paste(startPos, endPos, scaleFactor, modelStackWithParam,
		                                      &copiedParamAutomation, isPatchCable);

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_PASTED));

		if (playbackHandler.isEitherClockActive()) {
			currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
		}
		else {
			if (padSelectionOn) {
				if (multiPadPressSelected) {
					renderDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
				}
				else {
					uint32_t squareStart = getMiddlePosFromSquare(leftPadSelectedX, effectiveLength, xScroll, xZoom);

					updateModPosition(modelStackWithParam, squareStart);
				}
			}
			else {
				displayAutomation();
			}
		}

		return;
	}

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_PASTE_AUTOMATION));
}

// select encoder action

// used to change the parameter selection and reset shortcut pad settings so that new pad can be blinked
// once parameter is selected
void AutomationView::selectEncoderAction(int8_t offset) {
	// change midi CC or param ID
	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you've selected a mod encoder (e.g. by pressing modEncoderButton) and you're in Automation Overview
	// the currentUIMode will change to Selecting Midi CC. In this case, turning select encoder should allow
	// you to change the midi CC assignment to that modEncoder
	if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
		InstrumentClipMinder::selectEncoderAction(offset);
		return;
	}
	// don't allow switching to automation editor if you're holding the audition pad in arranger automation view
	else if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		return;
	}
	// if you're in a synth, kit, or audio clip
	else if (onArrangerView || outputType == OutputType::SYNTH || outputType == OutputType::KIT
	         || outputType == OutputType::AUDIO) {
		// if you're in a audio clip, a kit with affect entire enabled, or in arranger view
		if (onArrangerView || (outputType == OutputType::AUDIO)
		    || (outputType == OutputType::KIT && instrumentClipView.getAffectEntire())) {
			selectGlobalParam(offset, clip);
		}
		// if you're a synth or a kit (with affect entire off and a drum selected)
		else if (outputType == OutputType::SYNTH || (outputType == OutputType::KIT && ((Kit*)output)->selectedDrum)) {
			selectNonGlobalParam(offset, clip);
		}
		getLastSelectedParamShortcut(clip, outputType);
	}
	// if you're in a midi clip
	else if (outputType == OutputType::MIDI_OUT) {
		selectMIDICC(offset, clip);
		getLastSelectedParamShortcut(clip, outputType);
	}
	// if you're in a CV clip or function is called for some other reason, do nothing
	else {
		return;
	}

	// update name on display, the LED mod indicators, and refresh the grid
	lastPadSelectedKnobPos = kNoSelection;
	if (multiPadPressSelected && padSelectionOn) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStackWithThreeMainThings,
			                                                                    currentSong->lastSelectedParamID);
		}
		else {
			modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			modelStackWithParam = getModelStackWithParam(modelStackWithTimelineCounter, clip);
		}
		int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);
		int32_t xScroll = currentSong->xScroll[navSysId];
		int32_t xZoom = currentSong->xZoom[navSysId];
		renderDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
	}
	else {
		displayAutomation(true, !display->have7SEG());
	}
	resetShortcutBlinking();
	view.setModLedStates();
	uiNeedsRendering(this);
}

// used with SelectEncoderAction to get the next arranger / audio clip / kit affect entire parameter
void AutomationView::selectGlobalParam(int32_t offset, Clip* clip) {
	if (onArrangerView) {
		auto idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		{
			while ((id == params::UNPATCHED_PITCH_ADJUST || id == params::UNPATCHED_COMPRESSOR_SHAPE
			        || id == params::UNPATCHED_SIDECHAIN_VOLUME)) {

				if (offset < 0) {
					offset -= 1;
				}
				else if (offset > 0) {
					offset += 1;
				}
				idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamArrayPosition,
				                                        kNumGlobalParamsForAutomation);
				id = globalParamsForAutomation[idx].second;
			}
		}
		currentSong->lastSelectedParamID = id;
		currentSong->lastSelectedParamKind = kind;
		currentSong->lastSelectedParamArrayPosition = idx;
	}
	else {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		clip->lastSelectedParamID = id;
		clip->lastSelectedParamKind = kind;
		clip->lastSelectedParamArrayPosition = idx;
	}
}

// used with SelectEncoderAction to get the next synth or kit non-affect entire param
void AutomationView::selectNonGlobalParam(int32_t offset, Clip* clip) {
	auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
	                                             kNumNonGlobalParamsForAutomation);
	{
		auto [kind, id] = nonGlobalParamsForAutomation[idx];
		if ((clip->output->type == OutputType::KIT) && (kind == params::Kind::UNPATCHED_SOUND)
		    && (id == params::UNPATCHED_PORTAMENTO)) {
			if (offset < 0) {
				offset -= 1;
			}
			else if (offset > 0) {
				offset += 1;
			}
			idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
			                                        kNumNonGlobalParamsForAutomation);
		}
	}
	auto [kind, id] = nonGlobalParamsForAutomation[idx];
	clip->lastSelectedParamID = id;
	clip->lastSelectedParamKind = kind;
	clip->lastSelectedParamArrayPosition = idx;
}

// used with SelectEncoderAction to get the next midi CC
void AutomationView::selectMIDICC(int32_t offset, Clip* clip) {
	if (isOnAutomationOverview()) {
		clip->lastSelectedParamID = 0;
	}
	else if ((clip->lastSelectedParamID + offset) < 0) {
		clip->lastSelectedParamID = kLastMidiCCForAutomation;
	}
	else if ((clip->lastSelectedParamID + offset) > kLastMidiCCForAutomation) {
		clip->lastSelectedParamID = 0;
	}
	else {
		clip->lastSelectedParamID += offset;
	}
}

// used with SelectEncoderAction to get the next parameter in the list of parameters
int32_t AutomationView::getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
                                                          int32_t numParams) {
	int32_t idx;
	// if you haven't selected a parameter yet, start at the beginning of the list
	if (isOnAutomationOverview()) {
		idx = 0;
	}
	// if you are scrolling left and are at the beginning of the list, go to the end of the list
	else if ((lastSelectedParamArrayPosition + offset) < 0) {
		idx = numParams - 1;
	}
	// if you are scrolling right and are at the end of the list, go to the beginning of the list
	else if ((lastSelectedParamArrayPosition + offset) > (numParams - 1)) {
		idx = 0;
	}
	// otherwise scrolling left/right within the list
	else {
		idx = lastSelectedParamArrayPosition + offset;
	}
	return idx;
}

// used with Select Encoder action to get the X, Y grid shortcut coordinates of the parameter selected
void AutomationView::getLastSelectedParamShortcut(Clip* clip, OutputType outputType) {
	bool paramShortcutFound = false;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (onArrangerView) {
				if (unpatchedGlobalParamShortcuts[x][y] == currentSong->lastSelectedParamID) {
					currentSong->lastSelectedParamShortcutX = x;
					currentSong->lastSelectedParamShortcutY = y;
				}
			}
			else if (outputType == OutputType::MIDI_OUT) {
				if (midiCCShortcutsForAutomation[x][y] == clip->lastSelectedParamID) {
					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
			else {
				if ((clip->lastSelectedParamKind == params::Kind::PATCHED
				     && patchedParamShortcuts[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == params::Kind::UNPATCHED_SOUND
				        && unpatchedNonGlobalParamShortcuts[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == params::Kind::UNPATCHED_GLOBAL
				        && unpatchedGlobalParamShortcuts[x][y] == clip->lastSelectedParamID)) {
					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
		}
		if (paramShortcutFound) {
			break;
		}
	}
	if (!paramShortcutFound) {
		clip->lastSelectedParamShortcutX = kNoSelection;
		clip->lastSelectedParamShortcutY = kNoSelection;
	}
}

// tempo encoder action
void AutomationView::tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed) {
	playbackHandler.tempoEncoderAction(offset, encoderButtonPressed, shiftButtonPressed);
}

// called by melodic_instrument.cpp or kit.cpp
void AutomationView::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {
	instrumentClipView.noteRowChanged(clip, noteRow);
}

// called by playback_handler.cpp
void AutomationView::notifyPlaybackBegun() {
	if (!onArrangerView && getCurrentClip()->type != ClipType::AUDIO) {
		instrumentClipView.reassessAllAuditionStatus();
	}
}

// resets the Parameter Selection which sends you back to the Automation Overview screen
// these values are saved on a clip basis
void AutomationView::initParameterSelection() {
	initPadSelection();

	if (onArrangerView) {
		currentSong->lastSelectedParamID = kNoSelection;
		currentSong->lastSelectedParamKind = params::Kind::NONE;
		currentSong->lastSelectedParamShortcutX = kNoSelection;
		currentSong->lastSelectedParamShortcutY = kNoSelection;
		currentSong->lastSelectedParamArrayPosition = 0;
	}
	else {
		Clip* clip = getCurrentClip();
		clip->lastSelectedParamID = kNoSelection;
		clip->lastSelectedParamKind = params::Kind::NONE;
		clip->lastSelectedParamShortcutX = kNoSelection;
		clip->lastSelectedParamShortcutY = kNoSelection;
		clip->lastSelectedParamArrayPosition = 0;
	}

	// if we're going back to the Automation Overview, set the display to show "Automation Overview"
	// and update the knob indicator levels to match the master FX button selected
	display->cancelPopup();
	renderDisplay();
	view.setKnobIndicatorLevels();
	view.setModLedStates();
}

// exit pad selection mode, reset pad press statuses
void AutomationView::initPadSelection() {

	padSelectionOn = false;
	multiPadPressSelected = false;
	multiPadPressActive = false;
	middlePadPressSelected = false;
	leftPadSelectedX = kNoSelection;
	rightPadSelectedX = kNoSelection;
	lastPadSelectedKnobPos = kNoSelection;
}

void AutomationView::initInterpolation() {

	automationView.interpolationBefore = false;
	automationView.interpolationAfter = false;
}

// get's the modelstack for the parameters that are being edited
// the model stack differs for SYNTH's, KIT's, MIDI, and Audio clip's
ModelStackWithAutoParam* AutomationView::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                                int32_t paramID, params::Kind paramKind) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	OutputType outputType = clip->output->type;

	if (paramID == kNoParamID) {
		paramID = clip->lastSelectedParamID;
		paramKind = clip->lastSelectedParamKind;
	}

	if (outputType == OutputType::SYNTH) {
		modelStackWithParam = getModelStackWithParamForSynthClip(modelStack, (InstrumentClip*)clip, paramID, paramKind);
	}

	else if (outputType == OutputType::KIT) {
		modelStackWithParam = getModelStackWithParamForKitClip(modelStack, (InstrumentClip*)clip, paramID, paramKind);
	}

	else if (outputType == OutputType::MIDI_OUT) {
		modelStackWithParam = getModelStackWithParamForMIDIClip(modelStack, (InstrumentClip*)clip, paramID);
	}

	else if (outputType == OutputType::AUDIO) {
		modelStackWithParam = getModelStackWithParamForAudioClip(modelStack, (AudioClip*)clip, paramID);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam* AutomationView::getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter* modelStack,
                                                                            InstrumentClip* clip, int32_t paramID,
                                                                            params::Kind paramKind) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);

	if (modelStackWithThreeMainThings) {
		if (paramKind == params::Kind::PATCHED) {
			modelStackWithParam = modelStackWithThreeMainThings->getPatchedAutoParamFromId(paramID);
		}

		else if (paramKind == params::Kind::UNPATCHED_SOUND) {
			modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
		}
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam* AutomationView::getModelStackWithParamForKitClip(ModelStackWithTimelineCounter* modelStack,
                                                                          InstrumentClip* clip, int32_t paramID,
                                                                          params::Kind paramKind) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	Output* output = clip->output;

	// for a kit we have two types of automation: with Affect Entire and without Affect Entire
	// for a kit with affect entire off, we are automating information at the noterow level
	if (!instrumentClipView.getAffectEntire()) {
		Drum* drum = ((Kit*)output)->selectedDrum;

		if (drum) {
			if (drum->type == DrumType::SOUND) { // no automation for MIDI or CV kit drum types

				ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

				if (modelStackWithNoteRow) {

					ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
					    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();

					if (modelStackWithThreeMainThings) {
						if (paramKind == params::Kind::PATCHED) {
							modelStackWithParam = modelStackWithThreeMainThings->getPatchedAutoParamFromId(paramID);
						}

						else if (paramKind == params::Kind::UNPATCHED_SOUND) {
							modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
						}
					}
				}
			}
		}
	}

	else { // model stack for automating kit params when "affect entire" is enabled

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &clip->paramManager);

		if (modelStackWithThreeMainThings) {
			modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
		}
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam* AutomationView::getModelStackWithParamForMIDIClip(ModelStackWithTimelineCounter* modelStack,
                                                                           InstrumentClip* clip, int32_t paramID) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	Output* output = clip->output;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &clip->paramManager);

	if (modelStackWithThreeMainThings) {
		ParamManager* paramManager = modelStackWithThreeMainThings->paramManager;

		if (paramManager && paramManager->containsAnyParamCollectionsIncludingExpression()) {
			MIDIInstrument* midiInstrument = (MIDIInstrument*)output;

			modelStackWithParam =
			    midiInstrument->getParamToControlFromInputMIDIChannel(paramID, modelStackWithThreeMainThings);
		}
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam* AutomationView::getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter* modelStack,
                                                                            AudioClip* clip, int32_t paramID) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);

	if (modelStackWithThreeMainThings) {
		modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
	}

	return modelStackWithParam;
}

// calculates the length of the clip or the length of the kit row
// if you're in a synth clip, kit clip with affect entire enabled or midi clip it returns clip length
// if you're in a kit clip with affect entire disabled and a row selected, it returns kit row length
int32_t AutomationView::getEffectiveLength(ModelStackWithTimelineCounter* modelStack) {
	Clip* clip = getCurrentClip();
	OutputType outputType = clip->output->type;

	int32_t effectiveLength = 0;

	if (onArrangerView) {
		effectiveLength = arrangerView.getMaxLength();
	}
	else if (outputType == OutputType::KIT && !instrumentClipView.getAffectEntire()) {
		ModelStackWithNoteRow* modelStackWithNoteRow = ((InstrumentClip*)clip)->getNoteRowForSelectedDrum(modelStack);

		effectiveLength = modelStackWithNoteRow->getLoopLength();
	}
	else {
		// this will differ for a kit when in note row mode
		effectiveLength = clip->loopLength;
	}

	return effectiveLength;
}

uint32_t AutomationView::getMaxLength() {
	if (onArrangerView) {
		return arrangerView.getMaxLength();
	}
	else {
		return getCurrentClip()->getMaxLength();
	}
}

uint32_t AutomationView::getMaxZoom() {
	if (onArrangerView) {
		return arrangerView.getMaxZoom();
	}
	else {
		return getCurrentClip()->getMaxZoom();
	}
}

int32_t AutomationView::getNavSysId() {
	if (onArrangerView) {
		return NAVIGATION_ARRANGEMENT;
	}
	else {
		return NAVIGATION_CLIP;
	}
}

uint32_t AutomationView::getSquareWidth(int32_t square, int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	int32_t squareRightEdge = getPosFromSquare(square + 1, xScroll, xZoom);
	return std::min(effectiveLength, squareRightEdge) - getPosFromSquare(square, xScroll, xZoom);
}

// when pressing on a single pad, you want to display the value of the middle node within that square
// as that is the most accurate value that represents that square
uint32_t AutomationView::getMiddlePosFromSquare(int32_t xDisplay, int32_t effectiveLength, int32_t xScroll,
                                                int32_t xZoom) {
	uint32_t squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
	uint32_t squareWidth = getSquareWidth(xDisplay, effectiveLength, xScroll, xZoom);
	if (squareWidth != 3) {
		squareStart = squareStart + (squareWidth / 2);
	}

	return squareStart;
}

// this function obtains a parameters value and converts it to a knobPos
// the knobPos is used for rendering the current parameter values in the automation editor
// it's also used for obtaining the start and end position values for a multi pad press
// and also used for increasing/decreasing parameter values with the mod encoders

int32_t AutomationView::getParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t squareStart) {
	// obtain value corresponding to the two pads that were pressed in a multi pad press action
	int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareStart, modelStack);
	int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(currentValue, modelStack);

	return knobPos;
}

// this function is based off the code in AutoParam::getValueAtPos, it was tweaked to just return interpolation status
// of the left node or right node (depending on the reversed parameter which is used to indicate what node in what
// direction we are looking for (e.g. we want status of left node, or right node, relative to the current pos we are
// looking at
bool AutomationView::getNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos, bool reversed) {

	if (!modelStack->autoParam->nodes.getNumElements()) {
		return false;
	}

	int32_t rightI = modelStack->autoParam->nodes.search(pos + (int32_t)!reversed, GREATER_OR_EQUAL);
	if (rightI >= modelStack->autoParam->nodes.getNumElements()) {
		rightI = 0;
	}
	ParamNode* rightNode = modelStack->autoParam->nodes.getElement(rightI);

	int32_t leftI = rightI - 1;
	if (leftI < 0) {
		leftI += modelStack->autoParam->nodes.getNumElements();
	}
	ParamNode* leftNode = modelStack->autoParam->nodes.getElement(leftI);

	if (reversed) {
		return leftNode->interpolated;
	}
	else {
		return rightNode->interpolated;
	}
}

// this function writes the new values calculated by the handleSinglePadPress and handleMultiPadPress functions
void AutomationView::setParameterAutomationValue(ModelStackWithAutoParam* modelStack, int32_t knobPos,
                                                 int32_t squareStart, int32_t xDisplay, int32_t effectiveLength,
                                                 int32_t xScroll, int32_t xZoom, bool modEncoderAction) {

	int32_t newValue = modelStack->paramCollection->knobPosToParamValue(knobPos, modelStack);

	uint32_t squareWidth = 0;

	// for a multi pad press, the beginning and ending pad presses are set with a square width of 3 (1 node).
	if (multiPadPressSelected) {
		squareWidth = kParamNodeWidth;
	}
	else {
		squareWidth = getSquareWidth(xDisplay, effectiveLength, xScroll, xZoom);
	}

	// if you're doing a single pad press, you don't want the values around that single press position to change
	// they will change if those nodes around the single pad press were created with interpolation turned on
	// to fix this, re-create those nodes with their current value with interpolation off

	interpolationBefore = getNodeInterpolation(modelStack, squareStart, true);
	interpolationAfter = getNodeInterpolation(modelStack, squareStart, false);

	// create a node to the left with the current interpolation status
	int32_t squareNodeLeftStart = squareStart - kParamNodeWidth;
	if (squareNodeLeftStart >= 0) {
		int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareNodeLeftStart, modelStack);
		modelStack->autoParam->setValuePossiblyForRegion(currentValue, modelStack, squareNodeLeftStart,
		                                                 kParamNodeWidth);
	}

	// create a node to the right with the current interpolation status
	int32_t squareNodeRightStart = squareStart + kParamNodeWidth;
	if (squareNodeRightStart < effectiveLength) {
		int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareNodeRightStart, modelStack);
		modelStack->autoParam->setValuePossiblyForRegion(currentValue, modelStack, squareNodeRightStart,
		                                                 kParamNodeWidth);
	}

	// reset interpolation to false for the single pad we're changing (so that the nodes around it don't also change)
	initInterpolation();

	// called twice because there was a weird bug where for some reason the first call wasn't taking effect
	// on one pad (and whatever pad it was changed every time)...super weird...calling twice fixed it...
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);

	if (!onArrangerView) {
		modelStack->getTimelineCounter()->instrumentBeenEdited();
	}

	// in a multi pad press, no need to display all the values calculated
	if (!multiPadPressSelected) {
		renderDisplay(knobPos + kKnobPosOffset, kNoSelection, modEncoderAction);
		setKnobIndicatorLevels(knobPos + kKnobPosOffset);
	}

	// midi follow and midi feedback enabled
	// re-send midi cc because learned parameter value has changed
	view.sendMidiFollowFeedback(modelStack, knobPos);
}

// sets both knob indicators to the same value when pressing single pad,
// deleting automation, or displaying current parameter value
// multi pad presses don't use this function
void AutomationView::setKnobIndicatorLevels(int32_t knobPos) {

	indicator_leds::setKnobIndicatorLevel(0, knobPos);
	indicator_leds::setKnobIndicatorLevel(1, knobPos);
}

// updates the position that the active mod controllable stack is pointing to
// this sets the current value for the active parameter so that it can be auditioned
void AutomationView::updateModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart, bool updateDisplay,
                                       bool updateIndicatorLevels) {

	if (!playbackHandler.isEitherClockActive() || padSelectionOn) {
		if (modelStack && modelStack->autoParam) {
			if (modelStack->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				view.activeModControllableModelStack.paramManager->toForTimeline()->grabValuesFromPos(
				    squareStart, &view.activeModControllableModelStack);

				int32_t knobPos = getParameterKnobPos(modelStack, squareStart) + kKnobPosOffset;

				if (updateDisplay) {
					renderDisplay(knobPos);
				}

				if (updateIndicatorLevels) {
					setKnobIndicatorLevels(knobPos);
				}
			}
		}
	}
}

// takes care of setting the automation value for the single pad that was pressed
void AutomationView::handleSinglePadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xDisplay,
                                          int32_t yDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
                                          bool shortcutPress) {

	Output* output = clip->output;
	OutputType outputType = output->type;

	// this means you are selecting a parameter
	if ((shortcutPress || isOnAutomationOverview())
	    && (onArrangerView
	        || !(outputType == OutputType::KIT && !instrumentClipView.getAffectEntire()
	             && !((Kit*)output)->selectedDrum)
	        || (outputType == OutputType::KIT && instrumentClipView.getAffectEntire()))) {

		if (handleParameterSelection(clip, outputType, xDisplay, yDisplay)) {
			return;
		}
	}
	// this means you are editing a parameter's value
	else if (!isOnAutomationOverview()) {
		handleParameterAutomationChange(modelStackWithParam, clip, outputType, xDisplay, yDisplay, effectiveLength,
		                                xScroll, xZoom);
	}

	uiNeedsRendering(this);
}

// called by handle single pad press when it is determined that you are selecting a parameter on automation overview
// or by using a grid shortcut combo
bool AutomationView::handleParameterSelection(Clip* clip, OutputType outputType, int32_t xDisplay, int32_t yDisplay) {
	if (!onArrangerView
	    && (outputType == OutputType::SYNTH || (outputType == OutputType::KIT && !instrumentClipView.getAffectEntire()))
	    && ((patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID)
	        || (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID))) {
		// don't allow automation of portamento in kit's
		if ((outputType == OutputType::KIT)
		    && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == params::UNPATCHED_PORTAMENTO)) {
			return true;
		}

		// if you are in a synth or a kit instrumentClip and the shortcut is valid, set current selected ParamID
		if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			clip->lastSelectedParamKind = params::Kind::PATCHED;
			clip->lastSelectedParamID = patchedParamShortcuts[xDisplay][yDisplay];
		}

		else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			clip->lastSelectedParamKind = params::Kind::UNPATCHED_SOUND;
			clip->lastSelectedParamID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
		}

		for (auto idx = 0; idx < kNumNonGlobalParamsForAutomation; idx++) {

			auto [kind, id] = nonGlobalParamsForAutomation[idx];

			if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
				clip->lastSelectedParamArrayPosition = idx;
				break;
			}
		}
	}

	// if you are in arranger, an audio clip, or a kit clip with affect entire enabled
	else if ((onArrangerView || (outputType == OutputType::AUDIO)
	          || (outputType == OutputType::KIT && instrumentClipView.getAffectEntire()))
	         && (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)) {

		params::Kind paramKind = params::Kind::UNPATCHED_GLOBAL;
		int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];

		// don't allow automation of pitch adjust, or sidechain in arranger
		if (onArrangerView && (paramID == params::UNPATCHED_PITCH_ADJUST)
		    || (paramID == params::UNPATCHED_COMPRESSOR_SHAPE) || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME)) {
			return true;
		}

		if (onArrangerView) {
			currentSong->lastSelectedParamKind = paramKind;
			currentSong->lastSelectedParamID = paramID;
		}
		else {
			clip->lastSelectedParamKind = paramKind;
			clip->lastSelectedParamID = paramID;
		}

		for (auto idx = 0; idx < kNumGlobalParamsForAutomation; idx++) {

			auto [kind, id] = globalParamsForAutomation[idx];

			if (onArrangerView) {
				if ((id == currentSong->lastSelectedParamID) && (kind == currentSong->lastSelectedParamKind)) {
					currentSong->lastSelectedParamArrayPosition = idx;
					break;
				}
			}
			else {
				if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
					clip->lastSelectedParamArrayPosition = idx;
					break;
				}
			}
		}
	}

	else if (outputType == OutputType::MIDI_OUT && midiCCShortcutsForAutomation[xDisplay][yDisplay] != kNoParamID) {

		// if you are in a midi clip and the shortcut is valid, set the current selected ParamID
		clip->lastSelectedParamID = midiCCShortcutsForAutomation[xDisplay][yDisplay];
	}

	else {
		return true;
	}

	// save the selected parameter ID's shortcut pad x,y coords so that you can setup the shortcut blink
	if (onArrangerView) {
		currentSong->lastSelectedParamShortcutX = xDisplay;
		currentSong->lastSelectedParamShortcutY = yDisplay;
	}
	else {
		clip->lastSelectedParamShortcutX = xDisplay;
		clip->lastSelectedParamShortcutY = yDisplay;
	}

	displayAutomation(true);
	resetShortcutBlinking();
	view.setModLedStates();

	return false;
}

// called by handle single pad press when it is determined that you are editing parameter automation using the grid
void AutomationView::handleParameterAutomationChange(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                     OutputType outputType, int32_t xDisplay, int32_t yDisplay,
                                                     int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	if (padSelectionOn) {
		// display pad's value
		uint32_t squareStart = 0;

		// if a long press is selected and you're checking value of start or end pad
		// display value at very first or very last node
		if (multiPadPressSelected && ((leftPadSelectedX == xDisplay) || (rightPadSelectedX == xDisplay))) {
			if (leftPadSelectedX == xDisplay) {
				squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
			}
			else {
				int32_t squareRightEdge = getPosFromSquare(rightPadSelectedX + 1, xScroll, xZoom);
				squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
			}
		}
		// display pad's middle value
		else {
			squareStart = getMiddlePosFromSquare(xDisplay, effectiveLength, xScroll, xZoom);
		}

		updateModPosition(modelStackWithParam, squareStart);

		if (!multiPadPressSelected) {
			leftPadSelectedX = xDisplay;
		}
	}

	else if (modelStackWithParam && modelStackWithParam->autoParam) {

		uint32_t squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);

		if (squareStart < effectiveLength) {
			// use default interpolation settings
			initInterpolation();

			int32_t newKnobPos = calculateKnobPosForSinglePadPress(outputType, yDisplay);
			setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength,
			                            xScroll, xZoom);
		}
	}
}

// calculates what the new parameter value is when you press a single pad
int32_t AutomationView::calculateKnobPosForSinglePadPress(OutputType outputType, int32_t yDisplay) {

	int32_t newKnobPos = 0;

	// if you press bottom pad, value is 0, for all other pads except for the top pad, value = row Y * 18
	if (yDisplay < 7) {
		if (middlePadPressSelected) {
			newKnobPos = ((yDisplay + 1) * kParamValueIncrementForAutomationDisplay);
		}
		else {
			newKnobPos = yDisplay * kParamValueIncrementForAutomationSinglePadPress;
		}
	}
	// if you are pressing the top pad, set the value to max (128)
	else {
		// for Midi Clips, maxKnobPos = 127
		if (outputType == OutputType::MIDI_OUT) {
			newKnobPos = kMaxKnobPos - 1; // 128 - 1 = 127
		}
		else {
			newKnobPos = kMaxKnobPos;
		}
	}
	if (middlePadPressSelected) {
		if (leftPadSelectedY == 0) {
			newKnobPos = newKnobPos / 2;
		}
		else {
			newKnobPos = (newKnobPos + ((leftPadSelectedY * kParamValueIncrementForAutomationDisplay) + 1)) / 2;
		}
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

// takes care of setting the automation values for the two pads pressed and the pads in between
void AutomationView::handleMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t firstPadX,
                                         int32_t firstPadY, int32_t secondPadX, int32_t secondPadY,
                                         int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
                                         bool modEncoderAction) {

	int32_t firstPadLeftEdge = getPosFromSquare(firstPadX, xScroll, xZoom);
	int32_t secondPadRightEdge = getPosFromSquare(secondPadX + 1, xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadRightEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t firstPadValue = 0;
		int32_t secondPadValue = 0;

		// if we're updating the long press values via mod encoder action, then get current values of pads pressed and
		// re-interpolate
		if (modEncoderAction) {
			firstPadValue = getParameterKnobPos(modelStackWithParam, firstPadLeftEdge) + kKnobPosOffset;

			uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;

			secondPadValue = getParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;
		}

		// otherwise if it's a regular long press, calculate values from the y position of the pads pressed
		else {
			OutputType outputType = clip->output->type;
			firstPadValue = calculateKnobPosForSinglePadPress(outputType, firstPadY) + kKnobPosOffset;
			secondPadValue = calculateKnobPosForSinglePadPress(outputType, secondPadY) + kKnobPosOffset;
		}

		// converting variables to float for more accurate interpolation calculation
		float firstPadValueFloat = static_cast<float>(firstPadValue);
		float firstPadXFloat = static_cast<float>(firstPadLeftEdge);
		float secondPadValueFloat = static_cast<float>(secondPadValue);
		float secondPadXFloat = static_cast<float>(secondPadRightEdge - kParamNodeWidth);

		// clear existing nodes from long press range

		// reset interpolation settings to default
		initInterpolation();

		// set value for beginning pad press at the very first node position within that pad
		setParameterAutomationValue(modelStackWithParam, firstPadValue - kKnobPosOffset, firstPadLeftEdge, firstPadX,
		                            effectiveLength, xScroll, xZoom);

		// set value for ending pad press at the very last node position within that pad
		int32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		setParameterAutomationValue(modelStackWithParam, secondPadValue - kKnobPosOffset, squareStart, secondPadX,
		                            effectiveLength, xScroll, xZoom);

		// loop from first pad to last pad, setting values for nodes in between
		// these values will serve as "key frames" for the interpolation to flow through
		for (int32_t x = firstPadX; x <= secondPadX; x++) {

			int32_t newKnobPos = 0;
			uint32_t squareWidth = 0;

			// we've already set the value for the very first node corresponding to the first pad above
			// now we will set the value for the remaining nodes within the first pad
			if (x == firstPadX) {
				squareStart = getPosFromSquare(x, xScroll, xZoom) + kParamNodeWidth;
				squareWidth = getSquareWidth(x, effectiveLength, xScroll, xZoom) - kParamNodeWidth;
			}

			// we've already set the value for the very last node corresponding to the second pad above
			// now we will set the value for the remaining nodes within the second pad
			else if (x == secondPadX) {
				squareStart = getPosFromSquare(x, xScroll, xZoom);
				squareWidth = getSquareWidth(x, effectiveLength, xScroll, xZoom) - kParamNodeWidth;
			}

			// now we will set the values for the nodes between the first and second pad's pressed
			else {
				squareStart = getPosFromSquare(x, xScroll, xZoom);
				squareWidth = getSquareWidth(x, effectiveLength, xScroll, xZoom);
			}

			// linear interpolation formula to calculate the value of the pads
			// f(x) = A + (x - Ax) * ((B - A) / (Bx - Ax))
			float newKnobPosFloat = std::round(firstPadValueFloat
			                                   + (((squareStart - firstPadXFloat) / kParamNodeWidth)
			                                      * ((secondPadValueFloat - firstPadValueFloat)
			                                         / ((secondPadXFloat - firstPadXFloat) / kParamNodeWidth))));

			newKnobPos = static_cast<int32_t>(newKnobPosFloat);
			newKnobPos = newKnobPos - kKnobPosOffset;

			// if interpolation is off, values for nodes in between first and second pad will not be set in a
			// staggered/step fashion
			if (interpolation) {
				interpolationBefore = true;
				interpolationAfter = true;
			}

			// set value for pads in between
			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
			                                                          squareWidth);
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
			                                                          squareWidth);

			if (!onArrangerView) {
				modelStackWithParam->getTimelineCounter()->instrumentBeenEdited();
			}
		}

		// reset interpolation settings to off
		initInterpolation();

		// render the multi pad press
		uiNeedsRendering(this);
	}
}

// new function to render display when a long press is active
// on OLED this will display the left and right position in a long press on the screen
// on 7SEG this will display the position of the last selected pad
// also updates LED indicators. bottom LED indicator = left pad, top LED indicator = right pad
void AutomationView::renderDisplayForMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                   int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
                                                   int32_t xDisplay, bool modEncoderAction) {
	int32_t firstPadLeftEdge = getPosFromSquare(leftPadSelectedX, xScroll, xZoom);
	int32_t secondPadRightEdge = getPosFromSquare(rightPadSelectedX + 1, xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadRightEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t knobPosLeft = getParameterKnobPos(modelStackWithParam, firstPadLeftEdge) + kKnobPosOffset;

		uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		int32_t knobPosRight = getParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;

		if (xDisplay != kNoSelection) {
			if (leftPadSelectedX == xDisplay) {
				squareStart = firstPadLeftEdge;
				lastPadSelectedKnobPos = knobPosLeft;
			}
			else {
				lastPadSelectedKnobPos = knobPosRight;
			}
		}

		if (display->haveOLED()) {
			renderDisplay(knobPosLeft, knobPosRight);
		}
		// display pad value of second pad pressed
		else {
			if (modEncoderAction) {
				renderDisplay(lastPadSelectedKnobPos);
			}
			else {
				renderDisplay();
			}
		}

		// update LED indicators
		indicator_leds::setKnobIndicatorLevel(0, knobPosLeft);
		indicator_leds::setKnobIndicatorLevel(1, knobPosRight);

		// update position of mod controllable stack
		updateModPosition(modelStackWithParam, squareStart, false, false);
	}
}

// used to calculate new knobPos when you turn the mod encoders (gold knobs)
int32_t AutomationView::calculateKnobPosForModEncoderTurn(int32_t knobPos, int32_t offset) {

	// adjust the current knob so that it is within the range of 0-128 for calculation purposes
	knobPos = knobPos + kKnobPosOffset;

	int32_t newKnobPos = 0;

	if ((knobPos + offset) < 0) {
		newKnobPos = knobPos;
	}
	else if ((knobPos + offset) <= kMaxKnobPos) {
		newKnobPos = knobPos + offset;
	}
	else if ((knobPos + offset) > kMaxKnobPos) {
		newKnobPos = kMaxKnobPos;
	}
	else {
		newKnobPos = knobPos;
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

// used to disable certain actions on the automation overview screen
// e.g. doubling clip length, editing clip length
bool AutomationView::isOnAutomationOverview() {
	if (onArrangerView) {
		if (currentSong->lastSelectedParamID == kNoSelection) {
			return true;
		}
	}
	else if (getCurrentClip()->lastSelectedParamID == kNoSelection) {
		return true;
	}

	return false;
}

void AutomationView::displayCVErrorMessage() {
	if (display->have7SEG()) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_AUTOMATE_CV));
	}
}

// created this function to undo any existing blinking so that it doesn't get rendered in my view
// also created it so that i can reset blinking when a parameter is deselected or when you enter/exit automation view
void AutomationView::resetShortcutBlinking() {
	memset(soundEditor.sourceShortcutBlinkFrequencies, 255, sizeof(soundEditor.sourceShortcutBlinkFrequencies));
	uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
	shortcutBlinking = false;
}
