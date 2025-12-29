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
#include "gui/ui/rename/rename_midi_cc_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "io/midi/midi_transpose.h"
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
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
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
#include "util/comparison.h"
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

constexpr int32_t kNumNonGlobalParamsForAutomation = 83;
constexpr int32_t kNumGlobalParamsForAutomation = 39;
constexpr int32_t kParamNodeWidth = 3;

// synth and kit rows FX - sorted in the order that Parameters are scrolled through on the display
const std::array<std::pair<params::Kind, ParamType>, kNumNonGlobalParamsForAutomation> nonGlobalParamsForAutomation{{
    // Master Volume, Pitch, Pan
    {params::Kind::PATCHED, params::GLOBAL_VOLUME_POST_FX},
    {params::Kind::PATCHED, params::LOCAL_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_PAN},
    // LPF Cutoff, Resonance, Morph
    {params::Kind::PATCHED, params::LOCAL_LPF_FREQ},
    {params::Kind::PATCHED, params::LOCAL_LPF_RESONANCE},
    {params::Kind::PATCHED, params::LOCAL_LPF_MORPH},
    // HPF Cutoff, Resonance, Morph
    {params::Kind::PATCHED, params::LOCAL_HPF_FREQ},
    {params::Kind::PATCHED, params::LOCAL_HPF_RESONANCE},
    {params::Kind::PATCHED, params::LOCAL_HPF_MORPH},
    // Bass, Bass Freq
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS_FREQ},
    // Treble, Treble Freq
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_TREBLE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_TREBLE_FREQ},
    // Reverb Amount
    {params::Kind::PATCHED, params::GLOBAL_REVERB_AMOUNT},
    // Delay Rate, Amount
    {params::Kind::PATCHED, params::GLOBAL_DELAY_RATE},
    {params::Kind::PATCHED, params::GLOBAL_DELAY_FEEDBACK},
    // Sidechain Shape
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SIDECHAIN_SHAPE},
    // Decimation, Bitcrush, Wavefolder
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SAMPLE_RATE_REDUCTION},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BITCRUSHING},
    {params::Kind::PATCHED, params::LOCAL_FOLD},
    // OSC 1 Volume, Pitch, Pulse Width, Carrier Feedback, Wave Index
    {params::Kind::PATCHED, params::LOCAL_OSC_A_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_PHASE_WIDTH},
    {params::Kind::PATCHED, params::LOCAL_CARRIER_0_FEEDBACK},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_WAVE_INDEX},
    // OSC 2 Volume, Pitch, Pulse Width, Carrier Feedback, Wave Index
    {params::Kind::PATCHED, params::LOCAL_OSC_B_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_PHASE_WIDTH},
    {params::Kind::PATCHED, params::LOCAL_CARRIER_1_FEEDBACK},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_WAVE_INDEX},
    // FM Mod 1 Volume, Pitch, Feedback
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_FEEDBACK},
    // FM Mod 2 Volume, Pitch, Feedback
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_FEEDBACK},
    // Env 1 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_0_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_RELEASE},
    // Env 2 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_1_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_RELEASE},
    // Env 3 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_2_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_2_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_2_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_2_RELEASE},
    // Env 4 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_3_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_3_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_3_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_3_RELEASE},
    // LFO 1
    {params::Kind::PATCHED, params::GLOBAL_LFO_FREQ_1},
    // LFO 2
    {params::Kind::PATCHED, params::LOCAL_LFO_LOCAL_FREQ_1},
    // LFO 3
    {params::Kind::PATCHED, params::GLOBAL_LFO_FREQ_2},
    // LFO 4
    {params::Kind::PATCHED, params::LOCAL_LFO_LOCAL_FREQ_2},
    // Mod FX Offset, Feedback, Depth, Rate
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_OFFSET},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_FEEDBACK},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_DEPTH},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_RATE},
    // Arp Rate, Gate, Rhythm, Chord Polyphony, Sequence Length, Ratchet Amount, Note Prob, Bass Prob, Chord Prob,
    // Ratchet Prob, Spread Gate, Spread Octave, Spread Velocity
    {params::Kind::PATCHED, params::GLOBAL_ARP_RATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_GATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SPREAD_GATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SPREAD_OCTAVE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SPREAD_VELOCITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RATCHET_AMOUNT},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RATCHET_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_CHORD_POLYPHONY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_CHORD_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_NOTE_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_BASS_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SWAP_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_GLIDE_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_REVERSE_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RHYTHM},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SEQUENCE_LENGTH},
    // Noise
    {params::Kind::PATCHED, params::LOCAL_NOISE_VOLUME},
    // Portamento
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_PORTAMENTO},
    // Stutter Rate
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_STUTTER_RATE},
    // Compressor Threshold
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_COMPRESSOR_THRESHOLD},
    // Mono Expression: X - Pitch Bend
    {params::Kind::EXPRESSION, Expression::X_PITCH_BEND},
    // Mono Expression: Y - Mod Wheel
    {params::Kind::EXPRESSION, Expression::Y_SLIDE_TIMBRE},
    // Mono Expression: Z - Channel Pressure
    {params::Kind::EXPRESSION, Expression::Z_PRESSURE},
}};

// global FX - sorted in the order that Parameters are scrolled through on the display
// used with kit affect entire, audio clips, and arranger
const std::array<std::pair<params::Kind, ParamType>, kNumGlobalParamsForAutomation> globalParamsForAutomation{{
    // Master Volume, Pitch, Pan
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_PITCH_ADJUST},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_PAN},
    // LPF Cutoff, Resonance
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_RES},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_MORPH},
    // HPF Cutoff, Resonance
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_FREQ},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_RES},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_MORPH},
    // Bass, Bass Freq
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BASS},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BASS_FREQ},
    // Treble, Treble Freq
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_TREBLE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_TREBLE_FREQ},
    // Reverb Amount
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_REVERB_SEND_AMOUNT},
    // Delay Rate, Amount
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_DELAY_RATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_DELAY_AMOUNT},
    // Sidechain Send, Shape
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SIDECHAIN_VOLUME},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SIDECHAIN_SHAPE},
    // Decimation, Bitcrush
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SAMPLE_RATE_REDUCTION},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BITCRUSHING},
    // Mod FX Offset, Feedback, Depth, Rate
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_OFFSET},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_FEEDBACK},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_DEPTH},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_RATE},
    // Stutter Rate
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE},
    // Compressor Threshold
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_COMPRESSOR_THRESHOLD},
    // Arp Rate, Gate, Rhythm, Chord Polyphony, Sequence Length, Ratchet Amount, Note Prob, Bass Prob, Chord Prob,
    // Ratchet Prob, Spread Gate, Spread Octave, Spread Velocity
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_GATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_SPREAD_GATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SPREAD_VELOCITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RATCHET_AMOUNT},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RATCHET_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_NOTE_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_BASS_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_SWAP_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_GLIDE_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_REVERSE_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RHYTHM},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_SEQUENCE_LENGTH},
}};

// VU meter style colours for the automation editor

const RGB rowColour[kDisplayHeight] = {{0, 255, 0},   {36, 219, 0}, {73, 182, 0}, {109, 146, 0},
                                       {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0}};

const RGB rowTailColour[kDisplayHeight] = {{2, 53, 2},  {9, 46, 2},  {17, 38, 2}, {24, 31, 2},
                                           {31, 24, 2}, {38, 17, 2}, {46, 9, 2},  {53, 2, 2}};

const RGB rowBlurColour[kDisplayHeight] = {{71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57},
                                           {76, 70, 53},  {77, 60, 48},  {78, 49, 44}, {79, 39, 39}};

const RGB rowBipolarDownColour[kDisplayHeight / 2] = {{255, 0, 0}, {182, 73, 0}, {73, 182, 0}, {0, 255, 0}};

const RGB rowBipolarDownTailColour[kDisplayHeight / 2] = {{53, 2, 2}, {38, 17, 2}, {17, 38, 2}, {2, 53, 2}};

const RGB rowBipolarDownBlurColour[kDisplayHeight / 2] = {{79, 39, 39}, {77, 60, 48}, {73, 90, 62}, {71, 111, 71}};

// colours for the velocity editor

const RGB velocityRowColour[kDisplayHeight] = {{0, 0, 255},   {36, 0, 219}, {73, 0, 182}, {109, 0, 146},
                                               {146, 0, 109}, {182, 0, 73}, {219, 0, 36}, {255, 0, 0}};

const RGB velocityRowTailColour[kDisplayHeight] = {{2, 2, 53},  {9, 2, 46},  {17, 2, 38}, {24, 2, 31},
                                                   {31, 2, 24}, {38, 2, 17}, {46, 2, 9},  {53, 2, 2}};

const RGB velocityRowBlurColour[kDisplayHeight] = {{71, 71, 111}, {72, 66, 101}, {73, 62, 90}, {74, 57, 80},
                                                   {76, 53, 70},  {77, 48, 60},  {78, 44, 49}, {79, 39, 39}};

// lookup tables for the values that are set when you press the pads in each row of the grid
const int32_t nonPatchCablePadPressValues[kDisplayHeight] = {0, 18, 37, 55, 73, 91, 110, 128};
const int32_t patchCablePadPressValues[kDisplayHeight] = {-128, -90, -60, -30, 30, 60, 90, 128};

// lookup tables for the min value of each pad's value range used to display automation on each row of the grid
const int32_t nonPatchCableMinPadDisplayValues[kDisplayHeight] = {0, 17, 33, 49, 65, 81, 97, 113};
const int32_t patchCableMinPadDisplayValues[kDisplayHeight] = {-128, -96, -64, -32, 1, 33, 65, 97};

// lookup tables for the max value of each pad's value range used to display automation on each row of the grid
const int32_t nonPatchCableMaxPadDisplayValues[kDisplayHeight] = {16, 32, 48, 64, 80, 96, 112, 128};
const int32_t patchCableMaxPadDisplayValues[kDisplayHeight] = {-97, -65, -33, -1, 32, 64, 96, 128};

// summary of pad ranges and press values (format: MIN < PRESS < MAX)
// patch cable:
// y = 7 ::   97 <  128 < 128
// y = 6 ::   65 <   90 <  96
// y = 5 ::   33 <   60 <  64
// y = 4 ::    1 <   30 <  32
// y = 3 ::  -32 <  -30 <  -1
// y = 2 ::  -64 <  -60 < -33
// y = 1 ::  -96 <  -90 < -65
// y = 0 :: -128 < -128 < -97

// non-patch cable:
// y = 7 :: 113 < 128 < 128
// y = 6 ::  97 < 110 < 112
// y = 5 ::  81 <  91 <  96
// y = 4 ::  65 <  73 <  80
// y = 3 ::  49 <  55 <  64
// y = 2 ::  33 <  37 <  48
// y = 1 ::  17 <  18 <  32
// y = 0 ::  0  <   0 <  16

// shortcuts for toggling interpolation and pad selection mode
constexpr uint8_t kInterpolationShortcutX = 0;
constexpr uint8_t kInterpolationShortcutY = 6;
constexpr uint8_t kPadSelectionShortcutX = 0;
constexpr uint8_t kPadSelectionShortcutY = 7;
constexpr uint8_t kVelocityShortcutX = 15;
constexpr uint8_t kVelocityShortcutY = 1;

PLACE_SDRAM_BSS AutomationView automationView{};

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
	// used to set parameter shortcut blinking
	parameterShortcutBlinking = false;
	// used to set interpolation shortcut blinking
	interpolationShortcutBlinking = false;
	// used to set pad selection shortcut blinking
	padSelectionShortcutBlinking = false;
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
	onMenuView = false;
	navSysId = NAVIGATION_CLIP;

	initMIDICCShortcutsForAutomation();
	midiCCShortcutsLoaded = false;

	automationParamType = AutomationParamType::PER_SOUND;

	probabilityChanged = false;
	timeSelectKnobLastReleased = 0;
}

void AutomationView::initMIDICCShortcutsForAutomation() {
	for (int x = 0; x < kDisplayWidth; x++) {
		for (int y = 0; y < kDisplayHeight; y++) {
			uint8_t ccNumber = MIDI_CC_NONE;
			uint32_t paramId = patchedParamShortcuts[x][y];
			if (paramId != kNoParamID) {
				ccNumber = midiFollow.soundParamToCC[paramId];
				if (ccNumber == MIDI_CC_NONE) {
					ccNumber = midiFollow.globalParamToCC[paramId];
				}
			}
			if (ccNumber == MIDI_CC_NONE) {
				paramId = unpatchedNonGlobalParamShortcuts[x][y];
				if (paramId != kNoParamID) {
					ccNumber = midiFollow.soundParamToCC[paramId + params::UNPATCHED_START];
					if (ccNumber == MIDI_CC_NONE) {
						ccNumber = midiFollow.globalParamToCC[paramId];
					}
				}
			}
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
	midiCCShortcutsForAutomation[15][7] = CC_NUMBER_Y_AXIS;
}

// called everytime you open up the automation view
bool AutomationView::opened() {
	initializeView();

	openedInBackground();

	focusRegained();

	return true;
}

void AutomationView::initializeView() {
	navSysId = getNavSysId();

	if (!midiCCShortcutsLoaded) {
		initMIDICCShortcutsForAutomation();
		midiCCShortcutsLoaded = true;
	}

	// grab the default setting for interpolation
	interpolation = FlashStorage::automationInterpolate;

	// re-initialize pad selection mode (so you start with the default automation editor)
	initPadSelection();

	// let the view know if we're dealing with an automation parameter or a note parameter
	setAutomationParamType();

	InstrumentClip* clip = getCurrentInstrumentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	if (!onArrangerView) {
		// only applies to instrument clips (not audio)
		if (clip) {
			// check if we for some reason, left the automation view, then switched clip types, then came back in
			// if you did that...reset the parameter selection and save the current parameter type selection
			// so we can check this again next time it happens
			if (outputType != clip->lastSelectedOutputType) {
				if (inAutomationEditor()) {
					initParameterSelection();
				}

				clip->lastSelectedOutputType = outputType;
			}

			// if we're in a kit, we want to make sure the param selected is valid for current context
			// e.g. only UNPATCHED_GLOBAL param kind's can be used with Kit Affect Entire enabled
			if ((outputType == OutputType::KIT) && (clip->lastSelectedParamKind != params::Kind::NONE)) {
				if (clip->lastSelectedParamKind == params::Kind::UNPATCHED_GLOBAL) {
					clip->affectEntire = true;
				}
				else {
					clip->affectEntire = false;
				}
			}

			// if you're not in note editor, turn led off if it's on
			if (clip->wrapEditing) {
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, inNoteEditor());
			}
		}
	}

	// if we're in the note editor and we're in a kit,
	// check that the lastAuditionedYDisplay is in sync with the selected drum
	if (inNoteEditor()) {
		if (outputType == OutputType::KIT) {
			potentiallyVerticalScrollToSelectedDrum(clip, output);
		}
	}
}

// Initializes some stuff to begin a new editing session
void AutomationView::focusRegained() {
	if (onArrangerView) {
		indicator_leds::setLedState(IndicatorLED::BACK, false);
		indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);
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
			// check if patch cable previously selected is still valid
			// if not we'll reset parameter selection and go back to overview
			if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
				bool patchCableExists = false;
				ParamManagerForTimeline* paramManager = clip->getCurrentParamManager();
				if (paramManager) {
					PatchCableSet* set = paramManager->getPatchCableSetAllowJibberish();
					// make sure it's not jiberish
					if (set) {
						PatchSource s;
						ParamDescriptor destinationParamDescriptor;
						set->dissectParamId(clip->lastSelectedParamID, &destinationParamDescriptor, &s);
						if (set->getPatchCableIndex(s, destinationParamDescriptor) != kNoSelection) {
							patchCableExists = true;
						}
					}
				}
				if (!patchCableExists) {
					initParameterSelection();
				}
			}
			instrumentClipView.auditioningSilently = false; // Necessary?
			InstrumentClipMinder::focusRegained();
			instrumentClipView.setLedStates();
		}
	}

	// don't reset shortcut blinking if were still in the menu
	if (getCurrentUI() == this) {
		// blink timer got reset by view.focusRegained() above
		parameterShortcutBlinking = false;
		interpolationShortcutBlinking = false;
		padSelectionShortcutBlinking = false;
		instrumentClipView.noteRowBlinking = false;
		// remove patch cable blink frequencies
		soundEditor.resetSourceBlinks();
		// possibly restablish parameter shortcut blinking (if parameter is selected)
		blinkShortcuts();
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

	AudioEngine::routineWithClusterLoading();

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

	// setup interpolation shortcut blinking when entering automation view from menu
	if (onMenuView && interpolation) {
		blinkInterpolationShortcut();
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
	// if we changed probability, then a pop-up may be currently stuck on display
	// if more than half a second has past since last knob turn, cancel the pop-up
	if (probabilityChanged
	    && ((uint32_t)(AudioEngine::audioSampleTimer - timeSelectKnobLastReleased) >= (kSampleRate / 2))) {
		display->cancelPopup();
		probabilityChanged = false;
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
bool AutomationView::possiblyRefreshAutomationEditorGrid(Clip* clip, params::Kind paramKind, int32_t paramID) {
	bool doRefreshGrid = false;
	if (clip && !automationView.onArrangerView) {
		if ((clip->lastSelectedParamID == paramID) && (clip->lastSelectedParamKind == paramKind)) {
			doRefreshGrid = true;
		}
	}
	else if (automationView.onArrangerView) {
		if ((currentSong->lastSelectedParamID == paramID) && (currentSong->lastSelectedParamKind == paramKind)) {
			doRefreshGrid = true;
		}
	}
	if (doRefreshGrid) {
		uiNeedsRendering(this);
		return true;
	}
	return false;
}

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

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		return true;
	}

	PadLEDs::renderingLock = true;

	Clip* clip = getCurrentClip();
	if (!onArrangerView && clip->type == ClipType::INSTRUMENT) {
		instrumentClipView.recalculateColours();
	}

	// erase current occupancy mask as it will be refreshed
	memset(occupancyMask, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	performActualRender(image, occupancyMask, currentSong->xScroll[navSysId], currentSong->xZoom[navSysId],
	                    kDisplayWidth, kDisplayWidth + kSideBarWidth, drawUndefinedArea);

	PadLEDs::renderingLock = false;

	return true;
}

// determines whether you should render the automation editor, automation overview or just render some love <3
void AutomationView::performActualRender(RGB image[][kDisplayWidth + kSideBarWidth],
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
	ModelStackWithNoteRow* modelStackWithNoteRow = nullptr;
	int32_t effectiveLength = 0;
	SquareInfo rowSquareInfo[kDisplayWidth];

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
		if (inNoteEditor()) {
			modelStackWithNoteRow = ((InstrumentClip*)clip)
			                            ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
			                                                 modelStackWithTimelineCounter); // don't create
			effectiveLength = modelStackWithNoteRow->getLoopLength();
			if (modelStackWithNoteRow->getNoteRowAllowNull()) {
				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
				noteRow->getRowSquareInfo(effectiveLength, rowSquareInfo);
			}
		}
	}

	if (!inNoteEditor()) {
		effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);
	}

	params::Kind kind = params::Kind::NONE;
	bool isBipolar = false;

	// if we have a valid model stack with param
	// get the param Kind and param bipolar status
	// so that it can be passed through the automation editor rendering
	// calls below
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		kind = modelStackWithParam->paramCollection->getParamKind();
		isBipolar = isParamBipolar(kind, modelStackWithParam->paramId);
	}

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		// only render if:
		// you're on arranger view
		// you're not in a CV clip type
		// you're not in a kit where you haven't selected a drum and you haven't selected affect entire either
		// you're not in a kit where no sound drum has been selected and you're not editing velocity
		// you're in a kit where midi or CV sound drum has been selected and you're editing velocity
		if (onArrangerView || !(outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
			bool isMIDICVDrum = false;
			if (outputType == OutputType::KIT && !getAffectEntire()) {
				isMIDICVDrum = (((Kit*)output)->selectedDrum
				                && ((((Kit*)output)->selectedDrum->type == DrumType::MIDI)
				                    || (((Kit*)output)->selectedDrum->type == DrumType::GATE)));
			}

			// if parameter has been selected, show Automation Editor
			if (inAutomationEditor() && !isMIDICVDrum) {
				renderAutomationEditor(modelStackWithParam, clip, image, occupancyMask, renderWidth, xScroll, xZoom,
				                       effectiveLength, xDisplay, drawUndefinedArea, kind, isBipolar);
			}

			// if note parameter has been selected, show Note Editor
			else if (inNoteEditor()) {
				renderNoteEditor(modelStackWithNoteRow, (InstrumentClip*)clip, image, occupancyMask, renderWidth,
				                 xScroll, xZoom, effectiveLength, xDisplay, drawUndefinedArea, rowSquareInfo[xDisplay]);
			}

			// if not editing a parameter, show Automation Overview
			else {
				renderAutomationOverview(modelStackWithTimelineCounter, modelStackWithThreeMainThings, clip, outputType,
				                         image, occupancyMask, xDisplay, isMIDICVDrum);
			}
		}
		else {
			PadLEDs::clearColumnWithoutSending(xDisplay);
		}
	}
}

// renders automation overview
void AutomationView::renderAutomationOverview(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                              ModelStackWithThreeMainThings* modelStackWithThreeMainThings, Clip* clip,
                                              OutputType outputType, RGB image[][kDisplayWidth + kSideBarWidth],
                                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
                                              bool isMIDICVDrum) {
	bool singleSoundDrum = (outputType == OutputType::KIT && !getAffectEntire()) && !isMIDICVDrum;
	bool affectEntireKit = (outputType == OutputType::KIT && getAffectEntire());
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		RGB& pixel = image[yDisplay][xDisplay];

		if (!isMIDICVDrum) {
			ModelStackWithAutoParam* modelStackWithParam = nullptr;

			if (!onArrangerView && (outputType == OutputType::SYNTH || singleSoundDrum)) {
				if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					modelStackWithParam =
					    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip,
					                                  patchedParamShortcuts[xDisplay][yDisplay], params::Kind::PATCHED);
				}

				else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					// don't make portamento available for automation in kit rows
					if ((outputType == OutputType::KIT)
					    && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == params::UNPATCHED_PORTAMENTO)) {
						pixel = colours::black; // erase pad
						continue;
					}

					modelStackWithParam = getModelStackWithParamForClip(
					    modelStackWithTimelineCounter, clip, unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay],
					    params::Kind::UNPATCHED_SOUND);
				}

				else if (params::isPatchCableShortcut(xDisplay, yDisplay)) {
					ParamDescriptor paramDescriptor;
					params::getPatchCableFromShortcut(xDisplay, yDisplay, &paramDescriptor);

					modelStackWithParam = getModelStackWithParamForClip(
					    modelStackWithTimelineCounter, clip, paramDescriptor.data, params::Kind::PATCH_CABLE);
				}
				// expression params, so sounds or midi/cv, or a single drum
				else if (params::expressionParamFromShortcut(xDisplay, yDisplay) != kNoParamID) {
					uint32_t paramID = params::expressionParamFromShortcut(xDisplay, yDisplay);
					if (paramID != kNoParamID) {
						modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip,
						                                                    paramID, params::Kind::EXPRESSION);
					}
				}
			}

			else if ((onArrangerView || (outputType == OutputType::AUDIO) || affectEntireKit)) {
				int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];
				if (paramID != kNoParamID) {
					if (onArrangerView) {
						// don't make pitch adjust or sidechain available for automation in arranger
						if ((paramID == params::UNPATCHED_PITCH_ADJUST)
						    || (paramID == params::UNPATCHED_SIDECHAIN_SHAPE)
						    || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME)
						    || (paramID >= params::UNPATCHED_FIRST_ARP_PARAM
						        && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
						    || (paramID == params::UNPATCHED_ARP_RATE)) {
							pixel = colours::black; // erase pad
							continue;
						}
						modelStackWithParam =
						    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, paramID);
					}
					else {
						if (outputType == OutputType::AUDIO
						    && ((paramID >= params::UNPATCHED_FIRST_ARP_PARAM
						         && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
						        || paramID == params::UNPATCHED_ARP_RATE)) {
							pixel = colours::black; // erase pad
							continue;
						}
						modelStackWithParam =
						    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, paramID);
					}
				}
			}

			else if (outputType == OutputType::MIDI_OUT) {
				if (midiCCShortcutsForAutomation[xDisplay][yDisplay] != kNoParamID) {
					modelStackWithParam = getModelStackWithParamForClip(
					    modelStackWithTimelineCounter, clip, midiCCShortcutsForAutomation[xDisplay][yDisplay]);
				}
			}
			else if (outputType == OutputType::CV) {
				uint32_t paramID = params::expressionParamFromShortcut(xDisplay, yDisplay);
				if (paramID != kNoParamID) {
					modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, paramID,
					                                                    params::Kind::EXPRESSION);
				}
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

				occupancyMask[yDisplay][xDisplay] = 64;
			}
			else {
				pixel = colours::black; // erase pad
			}
		}
		else {
			pixel = colours::black; // erase pad
		}

		if (!onArrangerView && !(outputType == OutputType::KIT && getAffectEntire())
		    && clip->type == ClipType::INSTRUMENT) {
			// highlight velocity pad
			if (xDisplay == kVelocityShortcutX && yDisplay == kVelocityShortcutY) {
				pixel = colours::grey;
				occupancyMask[yDisplay][xDisplay] = 64;
			}
		}
	}
}

// gets the length of the clip, renders the pads corresponding to current parameter values set up to the
// clip length renders the undefined area of the clip that the user can't interact with
void AutomationView::renderAutomationEditor(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                            RGB image[][kDisplayWidth + kSideBarWidth],
                                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth,
                                            int32_t xScroll, uint32_t xZoom, int32_t effectiveLength, int32_t xDisplay,
                                            bool drawUndefinedArea, params::Kind kind, bool isBipolar) {
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		renderAutomationColumn(modelStackWithParam, image, occupancyMask, effectiveLength, xDisplay,
		                       modelStackWithParam->autoParam->isAutomated(), xScroll, xZoom, kind, isBipolar);
	}
	if (drawUndefinedArea) {
		renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth, this,
		                    currentSong->tripletsOn, xDisplay);
	}
}

/// render each square in each column of the automation editor grid
void AutomationView::renderAutomationColumn(ModelStackWithAutoParam* modelStackWithParam,
                                            RGB image[][kDisplayWidth + kSideBarWidth],
                                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                            int32_t lengthToDisplay, int32_t xDisplay, bool isAutomated,
                                            int32_t xScroll, int32_t xZoom, params::Kind kind, bool isBipolar) {

	uint32_t squareStart = getMiddlePosFromSquare(xDisplay, lengthToDisplay, xScroll, xZoom);
	int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;

	// iterate through each square
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (isBipolar) {
			renderAutomationBipolarSquare(image, occupancyMask, xDisplay, yDisplay, isAutomated, kind, knobPos);
		}
		else {
			renderAutomationUnipolarSquare(image, occupancyMask, xDisplay, yDisplay, isAutomated, knobPos);
		}
	}
}

/// render column for bipolar params - e.g. pan, pitch, patch cable
void AutomationView::renderAutomationBipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
                                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                   int32_t xDisplay, int32_t yDisplay, bool isAutomated,
                                                   params::Kind kind, int32_t knobPos) {
	RGB& pixel = image[yDisplay][xDisplay];

	int32_t middleKnobPos;

	// for patch cable that has a range of -128 to + 128, the middle point is 0
	if (kind == params::Kind::PATCH_CABLE) {
		middleKnobPos = 0;
	}
	// for non-patch cable that has a range of 0 to 128, the middle point is 64
	else {
		middleKnobPos = 64;
	}

	// if it's bipolar, only render grid rows above or below middle value
	if (((knobPos > middleKnobPos) && (yDisplay < 4)) || ((knobPos < middleKnobPos) && (yDisplay > 3))) {
		pixel = colours::black; // erase pad
		return;
	}

	bool doRender = false;

	// determine whether or not you should render a row based on current value
	if (knobPos != middleKnobPos) {
		if (kind == params::Kind::PATCH_CABLE) {
			if (knobPos > middleKnobPos) {
				doRender = (knobPos >= patchCableMinPadDisplayValues[yDisplay]);
			}
			else {
				doRender = (knobPos <= patchCableMaxPadDisplayValues[yDisplay]);
			}
		}
		else {
			if (knobPos > middleKnobPos) {
				doRender = (knobPos >= nonPatchCableMinPadDisplayValues[yDisplay]);
			}
			else {
				doRender = (knobPos <= nonPatchCableMaxPadDisplayValues[yDisplay]);
			}
		}
	}

	// render automation lane
	if (doRender) {
		if (isAutomated) { // automated, render bright colour
			if (knobPos > middleKnobPos) {
				pixel = rowBipolarDownColour[-yDisplay + 7];
			}
			else {
				pixel = rowBipolarDownColour[yDisplay];
			}
		}
		else { // not automated, render less bright tail colour
			if (knobPos > middleKnobPos) {
				pixel = rowBipolarDownTailColour[-yDisplay + 7];
			}
			else {
				pixel = rowBipolarDownTailColour[yDisplay];
			}
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
	else {
		pixel = colours::black; // erase pad
	}

	// pad selection mode, render cursor
	if (padSelectionOn && ((xDisplay == leftPadSelectedX) || (xDisplay == rightPadSelectedX))) {
		if (doRender) {
			if (knobPos > middleKnobPos) {
				pixel = rowBipolarDownBlurColour[-yDisplay + 7];
			}
			else {
				pixel = rowBipolarDownBlurColour[yDisplay];
			}
		}
		else {
			pixel = colours::grey;
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
}

/// render column for unipolar params (e.g. not pan, pitch, or patch cables)
void AutomationView::renderAutomationUnipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
                                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                    int32_t xDisplay, int32_t yDisplay, bool isAutomated,
                                                    int32_t knobPos) {
	RGB& pixel = image[yDisplay][xDisplay];

	// determine whether or not you should render a row based on current value
	bool doRender = false;
	if (knobPos) {
		doRender = (knobPos >= nonPatchCableMinPadDisplayValues[yDisplay]);
	}

	// render square
	if (doRender) {
		if (isAutomated) { // automated, render bright colour
			pixel = rowColour[yDisplay];
		}
		else { // not automated, render less bright tail colour
			pixel = rowTailColour[yDisplay];
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
	else {
		pixel = colours::black; // erase pad
	}

	// pad selection mode, render cursor
	if (padSelectionOn && ((xDisplay == leftPadSelectedX) || (xDisplay == rightPadSelectedX))) {
		if (doRender) {
			pixel = rowBlurColour[yDisplay];
		}
		else {
			pixel = colours::grey;
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
}

// gets the length of the note row, renders the pads corresponding to current note parameter values set up to the
// note row length renders the undefined area of the note row that the user can't interact with
void AutomationView::renderNoteEditor(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
                                      RGB image[][kDisplayWidth + kSideBarWidth],
                                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth,
                                      int32_t xScroll, uint32_t xZoom, int32_t effectiveLength, int32_t xDisplay,
                                      bool drawUndefinedArea, SquareInfo& squareInfo) {
	if (modelStackWithNoteRow->getNoteRowAllowNull()) {
		renderNoteColumn(modelStackWithNoteRow, clip, image, occupancyMask, xDisplay, xScroll, xZoom, squareInfo);
	}
	if (drawUndefinedArea) {
		renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth, this,
		                    currentSong->tripletsOn, xDisplay);
	}
}

/// render each square in each column of the note editor grid
void AutomationView::renderNoteColumn(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
                                      RGB image[][kDisplayWidth + kSideBarWidth],
                                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
                                      int32_t xScroll, int32_t xZoom, SquareInfo& squareInfo) {
	int32_t value = 0;

	if (automationParamType == AutomationParamType::NOTE_VELOCITY) {
		value = squareInfo.averageVelocity;
	}

	// iterate through each square
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		renderNoteSquare(image, occupancyMask, xDisplay, yDisplay, squareInfo.squareType, value);
	}
}

/// render column for note parameter
void AutomationView::renderNoteSquare(RGB image[][kDisplayWidth + kSideBarWidth],
                                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
                                      int32_t yDisplay, uint8_t squareType, int32_t value) {
	RGB& pixel = image[yDisplay][xDisplay];
	bool doRender = false;

	if (squareType == SQUARE_NO_NOTE) {
		pixel = colours::black; // erase pad
	}
	else {
		// render square
		if (value >= nonPatchCableMinPadDisplayValues[yDisplay]) {
			doRender = true;
			if (squareType == SQUARE_NOTE_HEAD) {
				pixel = velocityRowColour[yDisplay];
			}
			else if (squareType == SQUARE_NOTE_TAIL) {
				pixel = velocityRowTailColour[yDisplay];
			}
			else if (squareType == SQUARE_BLURRED) {
				pixel = velocityRowBlurColour[yDisplay];
			}
			occupancyMask[yDisplay][xDisplay] = 64;
		}
		else {
			pixel = colours::black; // erase pad
		}
	}
}

// occupancyMask now optional
void AutomationView::renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay,
                                         RGB image[][kDisplayWidth + kSideBarWidth],
                                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t imageWidth,
                                         TimelineView* timelineView, bool tripletsOnHere, int32_t xDisplay) {
	// If the visible pane extends beyond the end of the Clip, draw it as grey
	int32_t greyStart = timelineView->getSquareFromPos(lengthToDisplay - 1, nullptr, xScroll, xZoom) + 1;

	if (greyStart < 0) {
		greyStart = 0; // This actually happened in a song of Marek's, due to another bug, but best to check
		               // for this
	}

	if (greyStart <= xDisplay) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			image[yDisplay][xDisplay] = colours::grey;
			occupancyMask[yDisplay][xDisplay] = 64;
		}
	}

	if (tripletsOnHere && timelineView->supportsTriplets()) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (!timelineView->isSquareDefined(xDisplay, xScroll, xZoom)) {
				image[yDisplay][xDisplay] = colours::grey;

				if (occupancyMask) {
					occupancyMask[yDisplay][xDisplay] = 64;
				}
			}
		}
	}
}

// defers to arranger, audio clip or instrument clip sidebar render functions
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

- on OLED it renders Parameter Name, Automation Status and Parameter Value (for selected Pad or the
current value for the Parameter for the last selected Mod Position)
- on 7SEG it renders Parameter name if no pad is selected or mod encoder is turned. If selecting pad it
displays the pads value for as long as you hold the pad. if turning mod encoder, it displays value while
turning mod encoder. after value displaying is finished, it displays scrolling parameter name again.

This function replaces the two functions that were previously called:

DisplayParameterValue
DisplayParameterName */

void AutomationView::renderDisplay(int32_t knobPosLeft, int32_t knobPosRight, bool modEncoderAction) {
	// don't refresh display if we're not current in the automation view UI
	// (e.g. if you're editing automation while in the menu)
	if (getCurrentUI() != this) {
		return;
	}

	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you're not in a MIDI instrument clip, convert the knobPos to the same range as the menu (0-50)
	if (inAutomationEditor() && (onArrangerView || outputType != OutputType::MIDI_OUT)) {
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
		renderDisplayOLED(clip, output, outputType, knobPosLeft, knobPosRight);
	}
	// 7SEG Display
	else {
		renderDisplay7SEG(clip, output, outputType, knobPosLeft, modEncoderAction);
	}
}

void AutomationView::renderDisplayOLED(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft,
                                       int32_t knobPosRight) {
	deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
	hid::display::OLED::clearMainImage();

	if (onAutomationOverview()) {
		renderAutomationOverviewDisplayOLED(canvas, output, outputType);
	}
	else {
		if (inAutomationEditor()) {
			renderAutomationEditorDisplayOLED(canvas, clip, outputType, knobPosLeft, knobPosRight);
		}
		else {
			renderNoteEditorDisplayOLED(canvas, (InstrumentClip*)clip, outputType, knobPosLeft, knobPosRight);
		}
	}

	deluge::hid::display::OLED::markChanged();
}

void AutomationView::renderAutomationOverviewDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                         Output* output, OutputType outputType) {
	// align string to vertically to the centre of the display
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 24;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 15;
#endif

	// display Automation Overview
	char const* overviewText;
	if (!onArrangerView && (outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
		overviewText = l10n::get(l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE);
		deluge::hid::display::OLED::drawPermanentPopupLookingText(overviewText);
	}
	else {
		overviewText = l10n::get(l10n::String::STRING_FOR_AUTOMATION_OVERVIEW);
		canvas.drawStringCentred(overviewText, yPos, kTextSpacingX, kTextSpacingY);
	}
}

void AutomationView::renderAutomationEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, Clip* clip,
                                                       OutputType outputType, int32_t knobPosLeft,
                                                       int32_t knobPosRight) {
	// display parameter name
	DEF_STACK_STRING_BUF(parameterName, 30);
	getAutomationParameterName(clip, outputType, parameterName);

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	canvas.drawStringCentredShrinkIfNecessary(parameterName.c_str(), yPos, kTextSpacingX, kTextSpacingY);

	// display automation status
	yPos = yPos + 12;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
	}

	char const* isAutomated;

	// check if Parameter is currently automated so that the automation status can be drawn on
	// the screen with the Parameter Name
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		if (modelStackWithParam->autoParam->isAutomated()) {
			isAutomated = l10n::get(l10n::String::STRING_FOR_AUTOMATION_ON);
		}
		else {
			isAutomated = l10n::get(l10n::String::STRING_FOR_AUTOMATION_OFF);
		}
	}

	canvas.drawStringCentred(isAutomated, yPos, kTextSpacingX, kTextSpacingY);

	// display parameter value
	yPos = yPos + 12;

	if (knobPosRight != kNoSelection) {
		char bufferLeft[10];
		bufferLeft[0] = 'L';
		bufferLeft[1] = ':';
		bufferLeft[2] = ' ';
		intToString(knobPosLeft, &bufferLeft[3]);
		canvas.drawString(bufferLeft, 0, yPos, kTextSpacingX, kTextSpacingY);

		char bufferRight[10];
		bufferRight[0] = 'R';
		bufferRight[1] = ':';
		bufferRight[2] = ' ';
		intToString(knobPosRight, &bufferRight[3]);
		canvas.drawStringAlignRight(bufferRight, yPos, kTextSpacingX, kTextSpacingY);
	}
	else {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		canvas.drawStringCentred(buffer, yPos, kTextSpacingX, kTextSpacingY);
	}
}

void AutomationView::renderNoteEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                 InstrumentClip* clip, OutputType outputType, int32_t knobPosLeft,
                                                 int32_t knobPosRight) {
	// display note parameter name
	DEF_STACK_STRING_BUF(parameterName, 30);
	if (automationParamType == AutomationParamType::NOTE_VELOCITY) {
		parameterName.append("Velocity");
	}

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	canvas.drawStringCentredShrinkIfNecessary(parameterName.c_str(), yPos, kTextSpacingX, kTextSpacingY);

	// display note / drum name
	yPos = yPos + 12;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	bool isKit = outputType == OutputType::KIT;

	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
	                                                                        modelStack); // don't create
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (!isKit) {
			modelStackWithNoteRow =
			    instrumentClipView.createNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);
		}
	}

	char noteRowName[50];

	if (modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (isKit) {
			DEF_STACK_STRING_BUF(drumName, 50);
			instrumentClipView.getDrumName(modelStackWithNoteRow->getNoteRow()->drum, drumName);
			strncpy(noteRowName, drumName.c_str(), 49);
		}
		else {
			int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
			noteCodeToString(modelStackWithNoteRow->getNoteRow()->getNoteCode(), noteRowName, &isNatural);
		}
	}
	else {
		if (isKit) {
			strncpy(noteRowName, "(Select Drum)", 49);
		}
		else {
			strncpy(noteRowName, "(Select Note)", 49);
		}
	}

	canvas.drawStringCentred(noteRowName, yPos, kTextSpacingX, kTextSpacingY);

	// display parameter value
	yPos = yPos + 12;

	if (automationParamType == AutomationParamType::NOTE_VELOCITY) {
		if (knobPosRight != kNoSelection) {
			char bufferLeft[10];
			bufferLeft[0] = 'L';
			bufferLeft[1] = ':';
			bufferLeft[2] = ' ';
			intToString(knobPosLeft, &bufferLeft[3]);
			canvas.drawString(bufferLeft, 0, yPos, kTextSpacingX, kTextSpacingY);

			char bufferRight[10];
			bufferRight[0] = 'R';
			bufferRight[1] = ':';
			bufferRight[2] = ' ';
			intToString(knobPosRight, &bufferRight[3]);
			canvas.drawStringAlignRight(bufferRight, yPos, kTextSpacingX, kTextSpacingY);
		}
		else if (knobPosLeft != kNoSelection) {
			char buffer[5];
			intToString(knobPosLeft, buffer);
			canvas.drawStringCentred(buffer, yPos, kTextSpacingX, kTextSpacingY);
		}
		else {
			char buffer[5];
			intToString(getCurrentInstrument()->defaultVelocity, buffer);
			canvas.drawStringCentred(buffer, yPos, kTextSpacingX, kTextSpacingY);
		}
	}
}

void AutomationView::renderDisplay7SEG(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft,
                                       bool modEncoderAction) {
	// display OVERVIEW
	if (onAutomationOverview()) {
		renderAutomationOverviewDisplay7SEG(output, outputType);
	}
	else {
		if (inAutomationEditor()) {
			renderAutomationEditorDisplay7SEG(clip, outputType, knobPosLeft, modEncoderAction);
		}
		else {
			renderNoteEditorDisplay7SEG((InstrumentClip*)clip, outputType, knobPosLeft);
		}
	}
}

void AutomationView::renderAutomationOverviewDisplay7SEG(Output* output, OutputType outputType) {
	char const* overviewText;
	if (!onArrangerView && (outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
		overviewText = l10n::get(l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE);
	}
	else {
		overviewText = l10n::get(l10n::String::STRING_FOR_AUTOMATION);
	}
	display->setScrollingText(overviewText);
}

void AutomationView::renderAutomationEditorDisplay7SEG(Clip* clip, OutputType outputType, int32_t knobPosLeft,
                                                       bool modEncoderAction) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (onArrangerView) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
	}

	bool padSelected = (!padSelectionOn && isUIModeActive(UI_MODE_NOTES_PRESSED)) || padSelectionOn;

	/* check if you're holding a pad
	 * if yes, store pad press knob position in lastPadSelectedKnobPos
	 * so that it can be used next time as the knob position if returning here
	 * to display parameter value after another popup has been cancelled (e.g. audition pad)
	 */
	if (padSelected) {
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

	bool isAutomated =
	    modelStackWithParam && modelStackWithParam->autoParam && modelStackWithParam->autoParam->isAutomated();
	bool playbackStarted = playbackHandler.isEitherClockActive();

	// display parameter value if knobPos is provided
	if ((knobPosLeft != kNoSelection) && (padSelected || (playbackStarted && isAutomated) || modEncoderAction)) {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		if (modEncoderAction && !padSelected) {
			display->displayPopup(buffer, 3, true);
		}
		else {
			display->setText(buffer, true, 255, false);
		}
	}
	// display parameter name
	else if (knobPosLeft == kNoSelection) {
		DEF_STACK_STRING_BUF(parameterName, 30);
		getAutomationParameterName(clip, outputType, parameterName);
		// if playback is running and there is automation, the screen will display the
		// current automation value at the playhead position
		// when changing to a parameter with automation, flash the parameter name first
		// before the value is displayed
		// otherwise if there's no automation, just scroll the parameter name
		if (padSelected || (playbackStarted && isAutomated)) {
			display->displayPopup(parameterName.c_str(), 3, true, isAutomated ? 3 : 255);
		}
		else {
			display->setScrollingText(parameterName.c_str(), 0, 600, -1, isAutomated ? 3 : 255);
		}
	}
}

void AutomationView::renderNoteEditorDisplay7SEG(InstrumentClip* clip, OutputType outputType, int32_t knobPosLeft) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	bool isKit = outputType == OutputType::KIT;

	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
	                                                                        modelStack); // don't create
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (!isKit) {
			modelStackWithNoteRow =
			    instrumentClipView.createNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);
		}
	}

	if (knobPosLeft != kNoSelection) {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		display->setText(buffer, true, 255, false);
	}
	else {
		// display note / drum name
		char noteRowName[50];
		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			if (isKit) {
				DEF_STACK_STRING_BUF(drumName, 50);
				instrumentClipView.getDrumName(modelStackWithNoteRow->getNoteRow()->drum, drumName);
				strncpy(noteRowName, drumName.c_str(), 49);
			}
			else {
				int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
				noteCodeToString(modelStackWithNoteRow->getNoteRow()->getNoteCode(), noteRowName, &isNatural);
			}
		}
		else {
			if (isKit) {
				strncpy(noteRowName, "(Select Drum)", 49);
			}
			else {
				strncpy(noteRowName, "(Select Note)", 49);
			}
		}
		display->setScrollingText(noteRowName);
	}
}

// get's the name of the Parameter being edited so it can be displayed on the screen
void AutomationView::getAutomationParameterName(Clip* clip, OutputType outputType, StringBuf& parameterName) {
	if (onArrangerView || outputType != OutputType::MIDI_OUT) {
		params::Kind lastSelectedParamKind = params::Kind::NONE;
		int32_t lastSelectedParamID = kNoSelection;
		PatchSource lastSelectedPatchSource = PatchSource::NONE;
		if (onArrangerView) {
			lastSelectedParamKind = currentSong->lastSelectedParamKind;
			lastSelectedParamID = currentSong->lastSelectedParamID;
		}
		else {
			lastSelectedParamKind = clip->lastSelectedParamKind;
			lastSelectedParamID = clip->lastSelectedParamID;
			lastSelectedPatchSource = clip->lastSelectedPatchSource;
		}
		if (lastSelectedParamKind == params::Kind::PATCH_CABLE) {
			PatchSource source2 = PatchSource::NONE;
			ParamDescriptor paramDescriptor;
			paramDescriptor.data = lastSelectedParamID;
			if (!paramDescriptor.hasJustOneSource()) {
				source2 = paramDescriptor.getTopLevelSource();
			}

			parameterName.append(sourceToStringShort(lastSelectedPatchSource));

			if (display->haveOLED()) {
				parameterName.append(" -> ");
			}
			else {
				parameterName.append(" - ");
			}

			if (source2 != PatchSource::NONE) {
				parameterName.append(sourceToStringShort(source2));
				parameterName.append(display->haveOLED() ? " -> " : " - ");
			}

			parameterName.append(params::getPatchedParamShortName(lastSelectedParamID));
		}
		else {
			parameterName.append(getParamDisplayName(lastSelectedParamKind, lastSelectedParamID));
		}
	}
	else {
		if (clip->lastSelectedParamID == CC_NUMBER_NONE) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_PARAM));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_PITCH_BEND) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_AFTERTOUCH) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
		}
		else if (clip->lastSelectedParamID == CC_EXTERNAL_MOD_WHEEL || clip->lastSelectedParamID == CC_NUMBER_Y_AXIS) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
		}
		else {
			MIDIInstrument* midiInstrument = (MIDIInstrument*)clip->output;
			bool appendedName = false;

			if (clip->lastSelectedParamID >= 0 && clip->lastSelectedParamID < kNumRealCCNumbers) {
				std::string_view name = midiInstrument->getNameFromCC(clip->lastSelectedParamID);
				// if we have a name for this midi cc set by the user, display that instead of the cc number
				if (!name.empty()) {
					parameterName.append(name.data());
					appendedName = true;
				}
			}

			// if we don't have a midi cc name set, draw CC number instead
			if (!appendedName) {
				if (display->haveOLED()) {
					parameterName.append("CC ");
					parameterName.appendInt(clip->lastSelectedParamID);
				}
				else {
					if (clip->lastSelectedParamID < 100) {
						parameterName.append("CC");
					}
					else {
						parameterName.append("C");
					}
					parameterName.appendInt(clip->lastSelectedParamID);
				}
			}
		}
	}
}

// adjust the LED meters and update the display

/*updated function for displaying automation when playback is enabled (called from ui_timer_manager).
Also used internally in the automation instrument clip view for updating the display and led
indicators.*/

void AutomationView::displayAutomation(bool padSelected, bool updateDisplay) {
	if ((!padSelectionOn && !isUIModeActive(UI_MODE_NOTES_PRESSED)) || padSelected) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			Clip* clip = getCurrentClip();

			modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {

			if (modelStackWithParam->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, view.modPos) + kKnobPosOffset;

				bool displayValue = updateDisplay
				                    && (display->haveOLED()
				                        || (display->have7SEG() && inAutomationEditor()
				                            && (playbackHandler.isEitherClockActive() || padSelected)));

				// update value on the screen when playing back automation
				// don't update value displayed if there's no automation unless instructed to update display
				// don't update value displayed when playback is stopped
				if (displayValue) {
					renderDisplay(knobPos);
				}
				// on 7SEG re-render parameter name under certain circumstances
				// e.g. when entering pad selection mode, when stopping playback
				else {
					renderDisplay();
				}

				setAutomationKnobIndicatorLevels(modelStackWithParam, knobPos, knobPos);
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
		return instrumentClipView.handleScaleButtonAction(on, inCardRoutine);
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
	// if you're holding shift or holding an audition pad while pressed clip, don't exit out of
	// automation view reset parameter selection and short blinking instead
	else if (b == CLIP_VIEW) {
		handleClipButtonAction(on, isAudioClip);
	}

	// Auto scrolling
	// Or Cross Screen Note Editing in Note Editor
	// Does not currently work for Automation
	else if (b == CROSS_SCREEN_EDIT) {
		// toggle auto scroll or cross screen editing
		if (onArrangerView || inNoteEditor()) {
			handleCrossScreenButtonAction(on);
		}
		// don't toggle for automation editing
		else {
			return ActionResult::DEALT_WITH;
		}
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == KIT) {
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
	// Not relevant for arranger view
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

	// Vertical encoder button
	// Not relevant for audio clip
	else if (b == Y_ENC && !isAudioClip) {
		handleVerticalEncoderButtonAction(on);
	}

	// Select encoder
	// if you're not pressing shift and press down on the select encoder, enter sound menu
	else if (!Buttons::isShiftButtonPressed() && b == SELECT_ENC) {
		handleSelectEncoderButtonAction(on);
	}

	else {
passToOthers:
		// if you're entering settings menu
		if (on && (b == SELECT_ENC) && Buttons::isShiftButtonPressed()) {
			if (padSelectionOn) {
				initPadSelection();
			}
		}

		// if you just toggle playback off, re-render 7SEG display
		if (!on && (b == PLAY) && display->have7SEG() && inAutomationEditor() && !padSelectionOn
		    && !playbackHandler.isEitherClockActive()) {
			renderDisplay();
		}

		uiNeedsRendering(this);

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

		// when you press affect entire, the parameter selection needs to reset
		// do this here because affect entire state may have just changed
		if (on && b == AFFECT_ENTIRE) {
			initParameterSelection();
			blinkShortcuts();
		}

		return result;
	}

	if (on && (b != KEYBOARD && b != CLIP_VIEW && b != SESSION_VIEW)) {
		uiNeedsRendering(this);
	}

	return ActionResult::DEALT_WITH;
}

// called by button action if b == SESSION_VIEW
void AutomationView::handleSessionButtonAction(Clip* clip, bool on) {
	// if shift is pressed, go back to automation overview
	if (on && Buttons::isShiftButtonPressed()) {
		initParameterSelection();
		blinkShortcuts();
		uiNeedsRendering(this);
	}
	// go back to song / arranger view
	else if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		if (padSelectionOn) {
			initPadSelection();
		}
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
			sessionView.transitionToSessionView();
		}
		resetShortcutBlinking();
	}
}

// called by button action if b == KEYBOARD
void AutomationView::handleKeyboardButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		if (padSelectionOn) {
			initPadSelection();
		}
		changeRootUI(&keyboardScreen);
		// reset blinking if you're leaving automation view for keyboard view
		// blinking will be reset when you come back
		resetShortcutBlinking();
	}
}

// called by button action if b == CLIP_VIEW
void AutomationView::handleClipButtonAction(bool on, bool isAudioClip) {
	// if audition pad or shift is pressed, go back to automation overview
	if (on && (currentUIMode == UI_MODE_AUDITIONING || Buttons::isShiftButtonPressed())) {
		initParameterSelection();
		blinkShortcuts();
		uiNeedsRendering(this);
	}
	// go back to clip view
	else if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		if (padSelectionOn) {
			initPadSelection();
		}
		if (isAudioClip) {
			changeRootUI(&audioClipView);
		}
		else {
			changeRootUI(&instrumentClipView);
		}
		resetShortcutBlinking();
	}
}

// call by button action if b == CROSS_SCREEN_EDIT
void AutomationView::handleCrossScreenButtonAction(bool on) {
	if (!on && currentUIMode == UI_MODE_NONE) {
		// if another button wasn't pressed while cross screen was held
		if (Buttons::considerCrossScreenReleaseForCrossScreenMode) {
			if (onArrangerView) {
				currentSong->arrangerAutoScrollModeActive = !currentSong->arrangerAutoScrollModeActive;
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, currentSong->arrangerAutoScrollModeActive);

				if (currentSong->arrangerAutoScrollModeActive) {
					arrangerView.reassessWhetherDoingAutoScroll();
				}
				else {
					arrangerView.doingAutoScrollNow = false;
				}
			}
			else {
				InstrumentClip* clip = getCurrentInstrumentClip();
				if (clip) {
					if (clip->wrapEditing) {
						clip->wrapEditing = false;
					}
					else {
						clip->wrapEditLevel = currentSong->xZoom[NAVIGATION_CLIP] * kDisplayWidth;
						// Ensure that there are actually multiple screens to edit across
						if (clip->wrapEditLevel < clip->loopLength) {
							clip->wrapEditing = true;
						}
						// If in we're in the note editor, we can check if the note row has multiple screens
						else if (inNoteEditor()) {
							char modelStackMemory[MODEL_STACK_MAX_SIZE];
							ModelStackWithTimelineCounter* modelStack =
							    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
							ModelStackWithNoteRow* modelStackWithNoteRow =
							    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
							                             modelStack); // don't create
							if (clip->wrapEditLevel < modelStackWithNoteRow->getLoopLength()) {
								clip->wrapEditing = true;
							}
						}
					}

					setLedStates();
				}
			}
		}
	}
}

// called by button action if b == KIT
void AutomationView::handleKitButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to create a new instrument or change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		if (Buttons::isShiftButtonPressed()) {
			instrumentClipView.createNewInstrument(OutputType::KIT);
		}
		else {
			instrumentClipView.changeOutputType(OutputType::KIT);
		}
	}
}

// called by button action if b == SYNTH
void AutomationView::handleSynthButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to create a new instrument or change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		// this gets triggered when you change an existing clip to synth / create a new synth clip in
		// song mode
		if (Buttons::isShiftButtonPressed()) {
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
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		instrumentClipView.changeOutputType(OutputType::MIDI_OUT);
	}
}

// called by button action if b == CV
void AutomationView::handleCVButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		instrumentClipView.changeOutputType(OutputType::CV);
	}
}
// called by button action if b == X_ENC
bool AutomationView::handleHorizontalEncoderButtonAction(bool on, bool isAudioClip) {
	// copy / paste automation (same shortcut used for notes)
	if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		if (inAutomationEditor()) {
			Clip* clip = getCurrentClip();
			OutputType outputType = clip->output->type;

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
			ModelStackWithAutoParam* modelStackWithParam = nullptr;

			if (onArrangerView) {
				modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
				modelStackWithParam = currentSong->getModelStackWithParam(modelStackWithThreeMainThings,
				                                                          currentSong->lastSelectedParamID);
			}
			else {
				modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
			}
			int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

			int32_t xScroll = currentSong->xScroll[navSysId];
			int32_t xZoom = currentSong->xZoom[navSysId];

			if (Buttons::isShiftButtonPressed()) {
				// paste within Automation Editor
				pasteAutomation(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
			}
			else {
				// copy within Automation Editor
				copyAutomation(modelStackWithParam, clip, xScroll, xZoom);
			}
		}
		return false;
	}
	else if (onArrangerView) {
		return true;
	}
	else if (isAudioClip) {
		// removing time stretching by re-calculating clip length based on length of audio sample
		if (on && Buttons::isButtonPressed(deluge::hid::button::Y_ENC) && currentUIMode == UI_MODE_NONE) {
			audioClipView.setClipLengthEqualToSampleLength();
			return false;
		}
		// if shift is pressed then we're resizing the clip without time stretching
		else if (Buttons::isShiftButtonPressed()) {
			return false;
		}
		return true;
	}
	// If user wants to "multiply" Clip contents
	else if (on && Buttons::isShiftButtonPressed() && !isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)
	         && !onAutomationOverview()) {
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
		// Whether or not we did the "multiply" action above, we need to be in this UI mode, e.g. for
		// rotating individual NoteRow
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
	// only allow clearing of a clip if you're on the automation overview
	if (on && onAutomationOverview()) {
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

				// clear automation, don't clear sample and mpe
				bool clearAutomation = true;
				bool clearSequenceAndMPE = false;
				clip->clear(action, modelStack, clearAutomation, clearSequenceAndMPE);
			}
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUTOMATION_CLEARED));

			return false;
		}
		return true;
	}
	else if (on && inAutomationEditor()) {
		// delete automation of current parameter selected

		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {
			Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE);
			modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

			displayAutomation(padSelectionOn, !display->have7SEG());
		}
	}
	else if (on && inNoteEditor()) {
		Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

		// don't create note row if it doesn't exist
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    ((InstrumentClip*)clip)->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
			// don't clear automation, do clear notes and mpe
			noteRow->clear(action, modelStackWithNoteRow, false, true);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTES_CLEARED));
		}
	}
	return false;
}

// handle by button action if b == Y_ENC
void AutomationView::handleVerticalEncoderButtonAction(bool on) {
	if (on) {
		if (inNoteEditor()) {
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				// Just pop up number - don't do anything
				instrumentClipView.editNoteRepeat(0);
			}
			else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    ((InstrumentClip*)modelStack->getTimelineCounter())
				        ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, modelStack);

				// Just pop up number - don't do anything
				instrumentClipView.editNumEuclideanEvents(modelStackWithNoteRow, 0,
				                                          instrumentClipView.lastAuditionedYDisplay);
			}
		}
	}
}

// called by button action if b == SELECT_ENC and shift button is not pressed
void AutomationView::handleSelectEncoderButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		initParameterSelection();
		uiNeedsRendering(this);

		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
			return;
		}

		if ((getCurrentOutputType() == OutputType::KIT) && (getCurrentInstrumentClip()->affectEntire)) {
			soundEditor.setupKitGlobalFXMenu = true;
		}

		display->setNextTransitionDirection(1);
		Clip* clip = onArrangerView ? nullptr : getCurrentClip();
		if (soundEditor.setup(clip)) {
			openUI(&soundEditor);
		}
	}
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

	// don't interact with sidebar if VU Meter is displayed
	if (onArrangerView && x >= kDisplayWidth && view.displayVUMeter) {
		return ActionResult::DEALT_WITH;
	}

	Output* output = clip->output;
	OutputType outputType = output->type;

	// if we're in a midi clip, with a midi cc selected and we press the name shortcut
	// while holding shift, then enter the rename midi cc UI
	if (outputType == OutputType::MIDI_OUT) {
		if (Buttons::isShiftButtonPressed() && x == 11 && y == 5) {
			if (!onAutomationOverview()) {
				openUI(&renameMidiCCUI);
				return ActionResult::DEALT_WITH;
			}
		}
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	ModelStackWithNoteRow* modelStackWithNoteRow = nullptr;
	NoteRow* noteRow = nullptr;
	int32_t effectiveLength = 0;
	SquareInfo squareInfo;

	if (onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
		if (inNoteEditor()) {
			modelStackWithNoteRow = ((InstrumentClip*)clip)
			                            ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
			                                                 modelStackWithTimelineCounter); // don't create
			// does note row exist?
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				// if you're in note editor and note row doesn't exist, create it
				// don't create note rows that don't exist in kits because those are empty kit rows
				if (outputType != OutputType::KIT) {
					modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(
					    modelStackWithTimelineCounter, instrumentClipView.lastAuditionedYDisplay);
				}
			}

			if (modelStackWithNoteRow->getNoteRowAllowNull()) {
				effectiveLength = modelStackWithNoteRow->getLoopLength();
				noteRow = modelStackWithNoteRow->getNoteRow();
				noteRow->getSquareInfo(x, effectiveLength, squareInfo);
			}
		}
	}

	if (!inNoteEditor()) {
		effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);
	}

	// Edit pad action...
	if (x < kDisplayWidth) {
		return handleEditPadAction(modelStackWithParam, modelStackWithNoteRow, noteRow, clip, output, outputType,
		                           effectiveLength, x, y, velocity, squareInfo);
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

// called by pad action when pressing a pad in the main grid (x < kDisplayWidth)
ActionResult AutomationView::handleEditPadAction(ModelStackWithAutoParam* modelStackWithParam,
                                                 ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                                 Clip* clip, Output* output, OutputType outputType,
                                                 int32_t effectiveLength, int32_t x, int32_t y, int32_t velocity,
                                                 SquareInfo& squareInfo) {

	if (onArrangerView && isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		return ActionResult::DEALT_WITH;
	}

	int32_t xScroll = currentSong->xScroll[navSysId];
	int32_t xZoom = currentSong->xZoom[navSysId];

	// if the user wants to change the parameter they are editing using Shift + Pad shortcut
	// or change the parameter they are editing by press on a shortcut pad on automation overview
	// or they want to enable/disable interpolation
	// or they want to enable/disable pad selection mode
	if (shortcutPadAction(modelStackWithParam, clip, output, outputType, effectiveLength, x, y, velocity, xScroll,
	                      xZoom, squareInfo)) {
		return ActionResult::DEALT_WITH;
	}

	// regular automation / note editing action
	if (isUIModeWithinRange(editPadActionUIModes) && isSquareDefined(x, xScroll, xZoom)) {
		if (inAutomationEditor()) {
			automationEditPadAction(modelStackWithParam, clip, x, y, velocity, effectiveLength, xScroll, xZoom);
		}
		else if (inNoteEditor()) {
			if (noteRow) {
				noteEditPadAction(modelStackWithNoteRow, noteRow, (InstrumentClip*)clip, x, y, velocity,
				                  effectiveLength, squareInfo);
			}
		}
	}
	return ActionResult::DEALT_WITH;
}

/// handles shortcut pad actions, including:
/// 1) toggle interpolation on / off
/// 2) select parameter on automation overview
/// 3) select parameter using shift + shortcut pad
/// 4) select parameter using audition + shortcut pad
bool AutomationView::shortcutPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, Output* output,
                                       OutputType outputType, int32_t effectiveLength, int32_t x, int32_t y,
                                       int32_t velocity, int32_t xScroll, int32_t xZoom, SquareInfo& squareInfo) {
	if (velocity) {
		bool shortcutPress = false;
		if (Buttons::isShiftButtonPressed()
		    || (isUIModeActive(UI_MODE_AUDITIONING) && !FlashStorage::automationDisableAuditionPadShortcuts)) {

			if (!inNoteEditor()) {
				// toggle interpolation on / off
				// not relevant for note editor because interpolation doesn't apply to note params
				if ((x == kInterpolationShortcutX && y == kInterpolationShortcutY)) {
					return toggleAutomationInterpolation();
				}
				// toggle pad selection on / off
				// not relevant for note editor because pad selection mode was deemed unnecessary
				else if (inAutomationEditor() && (x == kPadSelectionShortcutX && y == kPadSelectionShortcutY)) {
					return toggleAutomationPadSelectionMode(modelStackWithParam, effectiveLength, xScroll, xZoom);
				}
			}

			shortcutPress = true;
		}
		// this means you are selecting a parameter
		if (shortcutPress || onAutomationOverview()) {
			// don't change parameters this way if we're in the menu
			if (getCurrentUI() == &automationView) {
				// make sure the context is valid for selecting a parameter
				// can't select a parameter in a kit if you haven't selected a drum
				if (onArrangerView
				    || !(outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)
				    || (outputType == OutputType::KIT && getAffectEntire())) {

					handleParameterSelection(clip, output, outputType, x, y);

					// if you're in not in note editor, turn led off if it's on
					if (((InstrumentClip*)clip)->wrapEditing) {
						indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, inNoteEditor());
					}
				}
			}

			return true;
		}
	}
	return false;
}

/// toggle automation interpolation on / off
bool AutomationView::toggleAutomationInterpolation() {
	if (interpolation) {
		interpolation = false;
		initInterpolation();
		resetInterpolationShortcutBlinking();

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_DISABLED));
	}
	else {
		interpolation = true;
		blinkInterpolationShortcut();

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_ENABLED));
	}
	return true;
}

/// toggle automation pad selection mode on / off
bool AutomationView::toggleAutomationPadSelectionMode(ModelStackWithAutoParam* modelStackWithParam,
                                                      int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	// enter/exit pad selection mode
	if (padSelectionOn) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_OFF));

		initPadSelection();
		displayAutomation(true, !display->have7SEG());
	}
	else {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_ON));

		padSelectionOn = true;
		blinkPadSelectionShortcut();

		multiPadPressSelected = false;
		multiPadPressActive = false;

		// display only left cursor initially
		leftPadSelectedX = 0;
		rightPadSelectedX = kNoSelection;

		uint32_t squareStart = getMiddlePosFromSquare(leftPadSelectedX, effectiveLength, xScroll, xZoom);

		updateAutomationModPosition(modelStackWithParam, squareStart, true, true);
	}
	uiNeedsRendering(this);
	return true;
}

// called by shortcutPadAction when it is determined that you are selecting a parameter on automation
// overview or by using a grid shortcut combo
void AutomationView::handleParameterSelection(Clip* clip, Output* output, OutputType outputType, int32_t xDisplay,
                                              int32_t yDisplay) {
	// PatchSource::Velocity shortcut
	// Enter Velocity Note Editor
	if (xDisplay == kVelocityShortcutX && yDisplay == kVelocityShortcutY) {
		if (clip->type == ClipType::INSTRUMENT) {
			// don't enter if we're in a kit with affect entire enabled
			if (!(outputType == OutputType::KIT && getAffectEntire())) {
				if (outputType == OutputType::KIT) {
					potentiallyVerticalScrollToSelectedDrum((InstrumentClip*)clip, output);
				}
				initParameterSelection(false);
				automationParamType = AutomationParamType::NOTE_VELOCITY;
				clip->lastSelectedParamShortcutX = xDisplay;
				clip->lastSelectedParamShortcutY = yDisplay;
				blinkShortcuts();
				renderDisplay();
				uiNeedsRendering(this);
				// if you're in note editor, turn led on
				if (((InstrumentClip*)clip)->wrapEditing) {
					indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, true);
				}
			}
			return;
		}
	}
	// potentially select a regular automatable parameter
	else if (!onArrangerView
	         && (outputType == OutputType::SYNTH
	             || (outputType == OutputType::KIT && !getAffectEntire() && ((Kit*)output)->selectedDrum
	                 && ((Kit*)output)->selectedDrum->type == DrumType::SOUND))
	         && ((patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID)
	             || (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)
	             || params::isPatchCableShortcut(xDisplay, yDisplay))) {
		// don't allow automation of portamento in kit's
		if ((outputType == OutputType::KIT)
		    && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == params::UNPATCHED_PORTAMENTO)) {
			return; // no parameter selected, don't re-render grid;
		}

		// if you are in a synth or a kit instrumentClip and the shortcut is valid, set current selected
		// ParamID
		if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			clip->lastSelectedParamKind = params::Kind::PATCHED;
			clip->lastSelectedParamID = patchedParamShortcuts[xDisplay][yDisplay];
		}
		else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			clip->lastSelectedParamKind = params::Kind::UNPATCHED_SOUND;
			clip->lastSelectedParamID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
		}
		else if (params::isPatchCableShortcut(xDisplay, yDisplay)) {
			ParamDescriptor paramDescriptor;
			params::getPatchCableFromShortcut(xDisplay, yDisplay, &paramDescriptor);
			clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
			clip->lastSelectedParamID = paramDescriptor.data;
			clip->lastSelectedPatchSource = paramDescriptor.getBottomLevelSource();
		}

		if (clip->lastSelectedParamKind != params::Kind::PATCH_CABLE) {
			getLastSelectedNonGlobalParamArrayPosition(clip);
		}
	}

	// if you are in arranger, an audio clip, or a kit clip with affect entire enabled
	else if ((onArrangerView || (outputType == OutputType::AUDIO)
	          || (outputType == OutputType::KIT && getAffectEntire()))
	         && (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)) {

		params::Kind paramKind = params::Kind::UNPATCHED_GLOBAL;
		int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];

		// don't allow automation of pitch adjust, or sidechain in arranger
		if (onArrangerView
		    && ((paramID == params::UNPATCHED_PITCH_ADJUST) || (paramID == params::UNPATCHED_SIDECHAIN_SHAPE)
		        || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME)
		        || (paramID >= params::UNPATCHED_FIRST_ARP_PARAM && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
		        || (paramID == params::UNPATCHED_ARP_RATE))) {
			return; // no parameter selected, don't re-render grid;
		}
		else if (outputType == OutputType::AUDIO
		         && ((paramID >= params::UNPATCHED_FIRST_ARP_PARAM && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
		             || paramID == params::UNPATCHED_ARP_RATE)) {
			return; // no parameter selected, don't re-render grid;
		}

		if (onArrangerView) {
			currentSong->lastSelectedParamKind = paramKind;
			currentSong->lastSelectedParamID = paramID;
		}
		else {
			clip->lastSelectedParamKind = paramKind;
			clip->lastSelectedParamID = paramID;
		}

		getLastSelectedGlobalParamArrayPosition(clip);
	}

	else if (outputType == OutputType::MIDI_OUT && midiCCShortcutsForAutomation[xDisplay][yDisplay] != kNoParamID) {

		// if you are in a midi clip and the shortcut is valid, set the current selected ParamID
		clip->lastSelectedParamID = midiCCShortcutsForAutomation[xDisplay][yDisplay];
	}
	// expression params, so sounds or midi/cv, or a single drum
	else if ((util::one_of(outputType, {OutputType::MIDI_OUT, OutputType::CV, OutputType::SYNTH})
	          // selected a single sound drum
	          || ((outputType == OutputType::KIT && !getAffectEntire() && ((Kit*)output)->selectedDrum
	               && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)))
	         && params::expressionParamFromShortcut(xDisplay, yDisplay) != kNoParamID) {
		clip->lastSelectedParamID = params::expressionParamFromShortcut(xDisplay, yDisplay);
		clip->lastSelectedParamKind = params::Kind::EXPRESSION;
	}

	else {
		return; // no parameter selected, don't re-render grid;
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

	resetParameterShortcutBlinking();
	if (inNoteEditor()) {
		automationParamType = AutomationParamType::PER_SOUND;
		instrumentClipView.resetSelectedNoteRowBlinking();
	}
	blinkShortcuts();
	if (display->have7SEG()) {
		renderDisplay(); // always display parameter name first, if there's automation it will show after
	}
	displayAutomation(true);
	view.setModLedStates();
	uiNeedsRendering(this);
	// turn off cross screen LED in automation editor
	if (clip && clip->type == ClipType::INSTRUMENT && ((InstrumentClip*)clip)->wrapEditing) {
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	}
}

// note edit pad action
// handles single and multi pad presses for note parameter editing (e.g. velocity)
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationView::noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                       InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity,
                                       int32_t effectiveLength, SquareInfo& squareInfo) {
	if (automationParamType == AutomationParamType::NOTE_VELOCITY) {
		velocityEditPadAction(modelStackWithNoteRow, noteRow, clip, x, y, velocity, effectiveLength, squareInfo);
	}
}

// velocity edit pad action
void AutomationView::velocityEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                           InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity,
                                           int32_t effectiveLength, SquareInfo& squareInfo) {
	// save pad selected
	leftPadSelectedX = x;

	// calculate new velocity based on Y of pad pressed
	int32_t newVelocity = getVelocityFromY(y);

	// middle pad press variables
	middlePadPressSelected = false;

	// multi pad press variables
	multiPadPressSelected = false;
	SquareInfo rowSquareInfo[kDisplayWidth];
	int32_t multiPadPressVelocityIncrement = 0;

	// update velocity editor rendering
	bool refreshVelocityEditor = false;
	bool showNewVelocity = true;

	// check for middle or multi pad press
	if (velocity && squareInfo.numNotes != 0 && instrumentClipView.numEditPadPresses == 1) {
		// Find that original press
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive) {
				// if found, calculate middle velocity between two velocity pad presses
				if (instrumentClipView.editPadPresses[i].xDisplay == x) {
					// the last pad press will have updated the default velocity
					// so get it as it will be used to calculate average between previous and new velocity
					int32_t previousVelocity = getCurrentInstrument()->defaultVelocity;

					// calculate middle velocity (average of two pad presses in a column)
					newVelocity = (newVelocity + previousVelocity) / 2;

					// update middle pad press selection indicator
					middlePadPressSelected = true;

					break;
				}
				// found a second press that isn't in the same column as the first press
				else {
					int32_t firstPadX = instrumentClipView.editPadPresses[i].xDisplay;

					// get note info on all the squares in the note row
					noteRow->getRowSquareInfo(effectiveLength, rowSquareInfo);

					// the long press logic calculates and renders the interpolation as if the press was
					// entered in a forward fashion (where the first pad is to the left of the second
					// pad). if the user happens to enter a long press backwards then we fix that entry
					// by re-ordering the pad presses so that it is forward again
					leftPadSelectedX = firstPadX > x ? x : firstPadX;
					rightPadSelectedX = firstPadX > x ? firstPadX : x;

					int32_t numSquares = 0;
					// find total number of notes in note row (excluding the first note)
					for (int32_t i = leftPadSelectedX; i <= rightPadSelectedX; i++) {
						// don't include note tails in note count
						if (rowSquareInfo[i].numNotes != 0 && rowSquareInfo[i].squareType != SQUARE_NOTE_TAIL) {
							numSquares++;
						}
					}

					//	DEF_STACK_STRING_BUF(numSquare, 50);
					//	numSquare.append("Squares: ");
					//	numSquare.appendInt(numSquares);
					//	numSquare.append("\n");

					// calculate start and end velocity for long press
					int32_t leftPadSelectedVelocity;
					int32_t rightPadSelectedVelocity;

					if (leftPadSelectedX == firstPadX) { // then left pad is the first press
						leftPadSelectedVelocity = rowSquareInfo[leftPadSelectedX].averageVelocity;
						leftPadSelectedY = getYFromVelocity(leftPadSelectedVelocity);
						rightPadSelectedVelocity = getVelocityFromY(y);
						rightPadSelectedY = y;
					}
					else { // then left pad is the second press
						leftPadSelectedVelocity = getVelocityFromY(y);
						leftPadSelectedY = y;
						rightPadSelectedVelocity = rowSquareInfo[rightPadSelectedX].averageVelocity;
						rightPadSelectedY = getYFromVelocity(rightPadSelectedVelocity);
					}

					//	numSquare.append("L: ");
					//	numSquare.appendInt(leftPadSelectedVelocity);
					//	numSquare.append(" R: ");
					//	numSquare.appendInt(rightPadSelectedVelocity);
					//	numSquare.append("\n");

					// calculate increment from first pad to last pad
					float multiPadPressVelocityIncrementFloat =
					    static_cast<float>((rightPadSelectedVelocity - leftPadSelectedVelocity)) / (numSquares - 1);
					multiPadPressVelocityIncrement =
					    static_cast<int32_t>(std::round(multiPadPressVelocityIncrementFloat));
					// if ramp is upwards, make increment positive
					if (leftPadSelectedVelocity < rightPadSelectedVelocity) {
						multiPadPressVelocityIncrement = std::abs(multiPadPressVelocityIncrement);
					}

					//	numSquare.append("Inc: ");
					//	numSquare.appendInt(multiPadPressVelocityIncrement);
					//	display->displayPopup(numSquare.c_str());

					// update multi pad press selection indicator
					multiPadPressSelected = true;
					multiPadPressActive = true;

					break;
				}
			}
		}
	}

	// if middle pad press was selected, set the velocity to middle velocity between two pads pressed
	if (middlePadPressSelected) {
		setVelocity(modelStackWithNoteRow, noteRow, x, newVelocity);
		refreshVelocityEditor = true;
	}
	// if multi pad (long) press was selected, set the velocity of all the notes between the two pad presses
	else if (multiPadPressSelected) {
		setVelocityRamp(modelStackWithNoteRow, noteRow, rowSquareInfo, multiPadPressVelocityIncrement);
		refreshVelocityEditor = true;
	}
	// otherwise, it's a regular velocity pad action
	else {
		// no existing notes in square pressed
		// add note and set velocity
		if (squareInfo.numNotes == 0) {
			addNoteWithNewVelocity(x, velocity, newVelocity);
			refreshVelocityEditor = true;
		}
		// pressing pad corresponding to note's current averageVelocity, remove note
		else if (nonPatchCableMinPadDisplayValues[y] <= squareInfo.averageVelocity
		         && squareInfo.averageVelocity <= nonPatchCableMaxPadDisplayValues[y]) {
			recordNoteEditPadAction(x, velocity);
			refreshVelocityEditor = true;
			showNewVelocity = false;
		}
		// note(s) exists, adjust velocity of existing notes
		else {
			adjustNoteVelocity(modelStackWithNoteRow, noteRow, x, velocity, newVelocity, squareInfo.squareType);
			refreshVelocityEditor = true;
		}
	}
	// if no note exists and you're trying to remove a note (y == 0 && squareInfo.numNotes == 0),
	// well no need to do anything

	if (multiPadPressActive && !isUIModeActive(UI_MODE_NOTES_PRESSED)) {
		multiPadPressActive = false;
	}

	if (refreshVelocityEditor) {
		// refresh grid and update default velocity on the display
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
		// if holding a multi pad press, render left and right velocity of the multi pad press
		if (multiPadPressActive) {
			int32_t leftPadSelectedVelocity = getVelocityFromY(leftPadSelectedY);
			int32_t rightPadSelectedVelocity = getVelocityFromY(rightPadSelectedY);
			if (display->haveOLED()) {
				renderDisplay(leftPadSelectedVelocity, rightPadSelectedVelocity);
			}
			else {
				// for 7seg, render value of last pad pressed
				renderDisplay(leftPadSelectedX == x ? leftPadSelectedVelocity : rightPadSelectedVelocity);
			}
		}
		else {
			if (velocity) {
				renderDisplay(showNewVelocity ? newVelocity : squareInfo.averageVelocity);
			}
			else {
				renderDisplay();
			}
		}
	}
}

// convert y of pad press into velocity value between 1 and 127
int32_t AutomationView::getVelocityFromY(int32_t y) {
	int32_t velocity = std::clamp<int32_t>(nonPatchCablePadPressValues[y], 1, 127);
	return velocity;
}

// convert velocity of a square into y
int32_t AutomationView::getYFromVelocity(int32_t velocity) {
	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (nonPatchCableMinPadDisplayValues[i] <= velocity && velocity <= nonPatchCableMaxPadDisplayValues[i]) {
			return i;
		}
	}
	return kNoSelection;
}

// add note and set velocity
void AutomationView::addNoteWithNewVelocity(int32_t x, int32_t velocity, int32_t newVelocity) {
	if (velocity) {
		// we change the instrument default velocity because it is used for new notes
		getCurrentInstrument()->defaultVelocity = newVelocity;
	}

	// record pad press and release
	// adds note with new velocity set
	recordNoteEditPadAction(x, velocity);
}

// adjust velocity of existing notes
void AutomationView::adjustNoteVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x,
                                        int32_t velocity, int32_t newVelocity, uint8_t squareType) {
	if (velocity) {
		// record pad press
		recordNoteEditPadAction(x, velocity);

		// adjust velocities of notes within pressed pad square
		setVelocity(modelStackWithNoteRow, noteRow, x, newVelocity);
	}
	else {
		// record pad release
		recordNoteEditPadAction(x, velocity);
	}
}

// set velocity of notes within pressed pad square
void AutomationView::setVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x,
                                 int32_t newVelocity) {
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	int32_t velocityValue = 0;

	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		bool foundPadPress = instrumentClipView.editPadPresses[i].isActive;

		// if we found an active pad press and we're looking for a pad press with a specific xDisplay
		// see if the active pad press is the one we are looking for
		if (foundPadPress && (x != kNoSelection)) {
			foundPadPress = (instrumentClipView.editPadPresses[i].xDisplay == x);
		}

		if (foundPadPress) {
			instrumentClipView.editPadPresses[i].deleteOnDepress = false;

			// Multiple notes in square
			if (instrumentClipView.editPadPresses[i].isBlurredSquare) {

				uint32_t velocitySumThisSquare = 0;
				uint32_t numNotesThisSquare = 0;

				int32_t noteI =
				    noteRow->notes.search(instrumentClipView.editPadPresses[i].intendedPos, GREATER_OR_EQUAL);
				Note* note = noteRow->notes.getElement(noteI);
				while (note
				       && note->pos - instrumentClipView.editPadPresses[i].intendedPos
				              < instrumentClipView.editPadPresses[i].intendedLength) {
					noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_SET_VELOCITY, newVelocity);

					instrumentClipView.updateVelocityValue(velocityValue, note->getVelocity());

					numNotesThisSquare++;
					velocitySumThisSquare += note->getVelocity();

					noteI++;
					note = noteRow->notes.getElement(noteI);
				}

				// Rohan: Get the average. Ideally we'd have done this when first selecting the note too, but I
				// didn't

				// Sean: not sure how getting the average when first selecting the note would help because the
				// average will change based on the velocity adjustment happening here.

				// We're adjusting the intendedVelocity here because this is the velocity that is used to audition
				// the pad press note so you can hear the velocity changes as you're holding the note down
				instrumentClipView.editPadPresses[i].intendedVelocity = velocitySumThisSquare / numNotesThisSquare;
			}

			// Only one note in square
			else {
				// We're adjusting the intendedVelocity here because this is the velocity that is used to audition
				// the pad press note so you can hear the velocity changes as you're holding the note down
				instrumentClipView.editPadPresses[i].intendedVelocity = newVelocity;
				noteRow->changeNotesAcrossAllScreens(instrumentClipView.editPadPresses[i].intendedPos,
				                                     modelStackWithNoteRow, action, CORRESPONDING_NOTES_SET_VELOCITY,
				                                     newVelocity);

				instrumentClipView.updateVelocityValue(velocityValue,
				                                       instrumentClipView.editPadPresses[i].intendedVelocity);
			}
		}
	}

	instrumentClipView.displayVelocity(velocityValue, 0);

	instrumentClipView.reassessAllAuditionStatus();
}

// set velocity of notes between pressed squares
void AutomationView::setVelocityRamp(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                     SquareInfo rowSquareInfo[kDisplayWidth], int32_t velocityIncrement) {
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	int32_t startVelocity = getVelocityFromY(leftPadSelectedY);
	int32_t velocityValue = 0;
	int32_t squaresProcessed = 0;

	for (int32_t i = leftPadSelectedX; i <= rightPadSelectedX; i++) {
		if (rowSquareInfo[i].numNotes != 0) {
			int32_t intendedPos = rowSquareInfo[i].squareStartPos;

			// Multiple notes in square
			if (rowSquareInfo[i].numNotes > 1) {
				int32_t intendedLength = rowSquareInfo[i].squareEndPos - intendedPos;

				int32_t noteI = noteRow->notes.search(intendedPos, GREATER_OR_EQUAL);

				Note* note = noteRow->notes.getElement(noteI);

				while (note && note->pos - intendedPos < intendedLength) {
					int32_t intendedVelocity =
					    std::clamp<int32_t>(startVelocity + (velocityIncrement * squaresProcessed), 1, 127);

					noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_SET_VELOCITY, intendedVelocity);

					noteI++;

					note = noteRow->notes.getElement(noteI);
				}
			}
			// one note in square
			else {
				int32_t intendedVelocity =
				    std::clamp<int32_t>(startVelocity + (velocityIncrement * squaresProcessed), 1, 127);

				noteRow->changeNotesAcrossAllScreens(intendedPos, modelStackWithNoteRow, action,
				                                     CORRESPONDING_NOTES_SET_VELOCITY, intendedVelocity);
			}

			// don't include note tails in note count
			if (rowSquareInfo[i].squareType != SQUARE_NOTE_TAIL) {
				squaresProcessed++;
			}
		}
	}
}

// call instrument clip view edit pad action function to process velocity pad press actions
void AutomationView::recordNoteEditPadAction(int32_t x, int32_t velocity) {
	instrumentClipView.editPadAction(velocity, instrumentClipView.lastAuditionedYDisplay, x,
	                                 currentSong->xZoom[NAVIGATION_CLIP]);
}

// automation edit pad action
// handles single and multi pad presses for automation editing
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationView::automationEditPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xDisplay,
                                             int32_t yDisplay, int32_t velocity, int32_t effectiveLength,
                                             int32_t xScroll, int32_t xZoom) {
	// If button down
	if (velocity) {
		// If this is a automation-length-edit press...
		// needed for Automation
		if (instrumentClipView.numEditPadPresses == 1) {

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
					recordAutomationSinglePadPress(xDisplay, yDisplay);

					multiPadPressSelected = true;
					multiPadPressActive = true;

					// the long press logic calculates and renders the interpolation as if the press was
					// entered in a forward fashion (where the first pad is to the left of the second
					// pad). if the user happens to enter a long press backwards then we fix that entry
					// by re-ordering the pad presses so that it is forward again
					leftPadSelectedX = firstPadX > xDisplay ? xDisplay : firstPadX;
					leftPadSelectedY = firstPadX > xDisplay ? yDisplay : firstPadY;
					rightPadSelectedX = firstPadX > xDisplay ? firstPadX : xDisplay;
					rightPadSelectedY = firstPadX > xDisplay ? firstPadY : yDisplay;

					// if you're not in pad selection mode, allow user to enter a long press
					if (!padSelectionOn) {
						handleAutomationMultiPadPress(modelStackWithParam, clip, leftPadSelectedX, leftPadSelectedY,
						                              rightPadSelectedX, rightPadSelectedY, effectiveLength, xScroll,
						                              xZoom);
					}
					else {
						uiNeedsRendering(this);
					}

					// set led indicators to left / right pad selection values
					// and update display
					renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom,
					                                        xDisplay);
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
			if (recordAutomationSinglePadPress(xDisplay, yDisplay)) {
				multiPadPressActive = false;
				handleAutomationSinglePadPress(modelStackWithParam, clip, xDisplay, yDisplay, effectiveLength, xScroll,
				                               xZoom);
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

		// outside pad selection mode, exit multi pad press once you've let go of the first pad in the
		// long press
		if (!padSelectionOn && multiPadPressSelected && (currentUIMode != UI_MODE_NOTES_PRESSED)) {
			initPadSelection();
		}
		// switch from long press selection to short press selection in pad selection mode
		else if (padSelectionOn && multiPadPressSelected && !multiPadPressActive
		         && (currentUIMode != UI_MODE_NOTES_PRESSED)
		         && ((AudioEngine::audioSampleTimer - instrumentClipView.timeLastEditPadPress) < kShortPressTime)) {

			multiPadPressSelected = false;

			leftPadSelectedX = xDisplay;
			rightPadSelectedX = kNoSelection;

			uiNeedsRendering(this);
		}

		if (currentUIMode != UI_MODE_NOTES_PRESSED) {
			lastPadSelectedKnobPos = kNoSelection;
			if (multiPadPressSelected) {
				renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom,
				                                        xDisplay);
			}
			else if (!padSelectionOn && !playbackHandler.isEitherClockActive()) {
				displayAutomation();
			}
		}

		middlePadPressSelected = false;
	}
}

bool AutomationView::recordAutomationSinglePadPress(int32_t xDisplay, int32_t yDisplay) {
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
			if (inAutomationEditor()) {
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    instrumentClip->getNoteRowOnScreen(y, modelStackWithTimelineCounter);

				// if we're in a kit, and you press a mute pad
				// check if it's a mute pad corresponding to the current selected drum
				// if not, change the drum selection, refresh parameter selection and go back to automation overview
				if (outputType == OutputType::KIT) {
					if (modelStackWithNoteRow->getNoteRowAllowNull()) {
						Drum* drum = modelStackWithNoteRow->getNoteRow()->drum;
						if (((Kit*)output)->selectedDrum != drum) {
							if (!getAffectEntire()) {
								initParameterSelection();
							}
						}
					}
				}
			}

			instrumentClipView.mutePadPress(y);
		}
	}
	return ActionResult::DEALT_WITH;
}

// called by pad action when pressing a pad in the audition column (x = kDisplayWidth + 1)
ActionResult AutomationView::handleAuditionPadAction(InstrumentClip* instrumentClip, Output* output,
                                                     OutputType outputType, int32_t y, int32_t velocity) {
	if (onArrangerView) {
		if (onAutomationOverview()) {
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
					view.instrumentMidiLearnPadPressed(velocity, (MelodicInstrument*)output);
				}
			}
		}

		// Actual basic audition pad press:
		else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
			/* 	special handling of audition pad action for note editing mode:

			    when we're in note editor mode, pressing audition pad changes note row selection
			    in this case, when pressing audition pad, only allow audition pad actions
			    if we're in pad selection mode as in pad selection mode we're only ever selecting
			    one column at a time, so this makes it easier to release the selection before
			    selecting a new note row
			*/

			int32_t previousY = instrumentClipView.lastAuditionedYDisplay;

			// are we in note editor mode and holding a note?
			if (previousY != y && inNoteEditor() && isUIModeActive(UI_MODE_NOTES_PRESSED)) {
				// are we also in pad selection mode and selected a column?
				if (padSelectionOn && (leftPadSelectedX != kNoSelection)) {
					// release that column that was selected
					recordNoteEditPadAction(leftPadSelectedX, 0);
				}
				// if we're not in pad selection mode or didn't select a column
				// don't process audition pad action
				else {
					return ActionResult::DEALT_WITH;
				}
			}

			// process audition pad action
			auditionPadAction(velocity, y, Buttons::isShiftButtonPressed());

			// now that we've processed audition pad action, we may now have changed note row selection
			// if note row selection has changed, and we're in pad selection mode
			// we'll re-select the previous column selection by recording a pad press
			if (previousY != instrumentClipView.lastAuditionedYDisplay && inNoteEditor() && padSelectionOn
			    && leftPadSelectedX != kNoSelection) {
				recordNoteEditPadAction(leftPadSelectedX, 1);
			}
		}
	}
	return ActionResult::DEALT_WITH;
}

// audition pad action
// not used with Audio Clip Automation View or Arranger Automation View
void AutomationView::auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown) {
	if (instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan && !velocity) {
		// in case we were editing quantize/humanize
		actionLogger.closeAction(ActionType::NOTE_NUDGE);
	}

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

	Drum* drum = nullptr;

	bool selectedDrumChanged = false;
	bool selectedRowChanged = false;
	bool drawNoteCode = false;

	// If Kit...
	if (isKit) {

		// if we're in a kit, and you press an audition pad
		// check if it's a audition pad corresponding to the current selected drum
		// also check that you're not in affect entire mode
		// if not, change the drum selection, refresh parameter selection and go back to automation
		// overview
		if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
			drum = modelStackWithNoteRowOnCurrentClip->getNoteRow()->drum;
			Drum* selectedDrum = ((Kit*)output)->selectedDrum;
			if (selectedDrum != drum) {
				selectedDrumChanged = true;
			}
		}

		// If NoteRow doesn't exist here, don't try to create one
		else {
			return;
		}
	}

	// Or if synth
	else if (outputType == OutputType::SYNTH) {
		if (velocity != 0) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
				menu_item::multiRangeMenu.noteOnToChangeRange(clip->getYNoteFromYDisplay(yDisplay, currentSong)
				                                              + ((SoundInstrument*)output)->transpose);
			}
		}
	}

	// Recording - only allowed if currentClip is activeClip
	if (clipIsActiveOnInstrument && playbackHandler.shouldRecordNotesNow() && currentSong->isClipActive(clip)) {

		// Note-on
		if (velocity != 0) {

			// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
			// This is basic. For MIDI input, we do this in a couple more cases - see
			// noteMessageReceived() in MelodicInstrument and Kit
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) {
				if (isKit) {
					if (drum) {
						drum->recordNoteOnEarly((velocity == USE_DEFAULT_VELOCITY)
						                            ? (static_cast<Instrument*>(output)->defaultVelocity)
						                            : velocity,
						                        clip->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
					}
				}
				else {
					// NoteRow is allowed to be NULL in this case.
					int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
					static_cast<MelodicInstrument*>(output)->earlyNotes[yNote] = {
					    .velocity = (velocity == USE_DEFAULT_VELOCITY)
					                    ? (static_cast<Instrument*>(output)->defaultVelocity)
					                    : static_cast<uint8_t>(velocity),
					    .still_active = clip->allowNoteTails(modelStackWithNoteRowOnCurrentClip),
					};
				}
			}

			else {

				// May need to create NoteRow if there wasn't one previously
				if (!modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {

					modelStackWithNoteRowOnCurrentClip =
					    instrumentClipView.createNoteRowForYDisplay(modelStackWithTimelineCounter, yDisplay);
				}

				if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
					clip->recordNoteOn(modelStackWithNoteRowOnCurrentClip,
					                   (velocity == USE_DEFAULT_VELOCITY)
					                       ? static_cast<Instrument*>(output)->defaultVelocity
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
				noteRowOnActiveClip = ((InstrumentClip*)output->getActiveClip())->getNoteRowForDrum(drum);
			}

			// Non-kit
			else {
				int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
				noteRowOnActiveClip = ((InstrumentClip*)output->getActiveClip())->getNoteRowForYNote(yNote);
			}
		}

		// If note on...
		if (velocity != 0) {
			int32_t velocityToSound = velocity;
			if (velocityToSound == USE_DEFAULT_VELOCITY) {
				velocityToSound = ((Instrument*)output)->defaultVelocity;
			}

			// Yup, need to do this even if we're going to do a "silent" audition, so pad lights up etc.
			instrumentClipView.auditionPadIsPressed[yDisplay] = velocityToSound;

			if (noteRowOnActiveClip != nullptr) {
				// Ensure our auditioning doesn't override a note playing in the sequence
				if (playbackHandler.isEitherClockActive() && noteRowOnActiveClip->sequenced) {
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
				instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = false;
				enterUIMode(UI_MODE_AUDITIONING);
			}

			drawNoteCode = true;

			if (!isKit && (instrumentClipView.lastAuditionedYDisplay != yDisplay)) {
				selectedRowChanged = true;
			}

			instrumentClipView.lastAuditionedYDisplay = yDisplay;

			// are we in a synth / midi / cv clip
			// and have we changed our note row selection
			if (selectedRowChanged) {
				instrumentClipView.potentiallyRefreshNoteRowMenu();
			}

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
				// Or if it's drone note, end auditioning to transfer the note's sustain to the sequencer
				if (!noteRowOnActiveClip || !noteRowOnActiveClip->sequenced
				    || noteRowOnActiveClip->isDroning(modelStackWithNoteRowOnCurrentClip->getLoopLength())) {
					instrumentClipView.sendAuditionNote(false, yDisplay, 64, 0);
				}
			}
			display->cancelPopup();
			// don't recalculateLastAuditionedNoteOnScreen if we're in the note editor because it
			// messes up the note row selection	for velocity editing
			instrumentClipView.someAuditioningHasEnded(!inNoteEditor());
			actionLogger.closeAction(ActionType::EUCLIDEAN_NUM_EVENTS_EDIT);
			actionLogger.closeAction(ActionType::NOTEROW_ROTATE);
			if (display->have7SEG()) {
				renderDisplay();
			}
		}
	}

getOut:

	if (selectedRowChanged || (selectedDrumChanged && (!getAffectEntire() || inNoteEditor()))) {
		if (inNoteEditor()) {
			renderDisplay();
			instrumentClipView.resetSelectedNoteRowBlinking();
			instrumentClipView.blinkSelectedNoteRow(0xFFFFFFFF);
		}
		else if (selectedDrumChanged) {
			initParameterSelection();
			uiNeedsRendering(this);
		}
		else {
			renderingNeededRegardlessOfUI(0, 1 << yDisplay);
		}
	}
	else {
		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
	}

	// draw note code on top of the automation view display which may have just been refreshed
	// don't draw if you're in note editor because note code is already on the display
	if (drawNoteCode && !inNoteEditor()) {
		instrumentClipView.drawNoteCode(yDisplay);
	}

	// This has to happen after instrumentClipView.setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
	}
}

// horizontal encoder actions:
// scroll left / right
// zoom in / out
// adjust clip length
// shift automations left / right
// adjust velocity in note editor
ActionResult AutomationView::horizontalEncoderAction(int32_t offset) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
	}

	if (inAutomationEditor()) {
		// exit multi pad press selection but keep single pad press selection (if it's selected)
		multiPadPressSelected = false;
		rightPadSelectedX = kNoSelection;
	}

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

	if (!onAutomationOverview()
	    && ((isNoUIModeActive() && Buttons::isButtonPressed(hid::button::Y_ENC))
	        || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	            && Buttons::isButtonPressed(hid::button::CLIP_VIEW))
	        || (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)))) {

		if (inAutomationEditor()) {
			int32_t xScroll = currentSong->xScroll[navSysId];
			int32_t xZoom = currentSong->xZoom[navSysId];
			int32_t squareSize = getPosFromSquare(1, xScroll, xZoom) - getPosFromSquare(0, xScroll, xZoom);
			int32_t shiftAmount = offset * squareSize;

			if (onArrangerView) {
				modelStackWithParam = currentSong->getModelStackWithParam(modelStackWithThreeMainThings,
				                                                          currentSong->lastSelectedParamID);
			}
			else {
				Clip* clip = getCurrentClip();
				modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
			}

			int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

			shiftAutomationHorizontally(modelStackWithParam, shiftAmount, effectiveLength);

			if (offset < 0) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_LEFT));
			}
			else if (offset > 0) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_RIGHT));
			}
		}
		else if (inNoteEditor()) {
			instrumentClipView.rotateNoteRowHorizontally(offset);
		}

		return ActionResult::DEALT_WITH;
	}

	// else if showing the Parameter selection grid menu, disable this action
	else if (onAutomationOverview()) {
		return ActionResult::DEALT_WITH;
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		instrumentClipView.editNoteRowLength(offset);
		return ActionResult::DEALT_WITH;
	}

	// fine tune note velocity
	// If holding down notes and nothing else is held down, adjust velocity
	else if (inNoteEditor() && isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
		if (automationParamType == AutomationParamType::NOTE_VELOCITY) {
			if (!instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				instrumentClipView.adjustVelocity(offset);
				renderDisplay(getCurrentInstrument()->defaultVelocity);
				uiNeedsRendering(this, 0xFFFFFFFF, 0);
			}
		}
		return ActionResult::DEALT_WITH;
	}

	// Shift and x pressed - edit length of audio clip without timestretching
	else if (getCurrentClip()->type == ClipType::AUDIO && isNoUIModeActive()
	         && Buttons::isButtonPressed(deluge::hid::button::X_ENC) && Buttons::isShiftButtonPressed()) {
		ActionResult result = audioClipView.editClipLengthWithoutTimestretching(offset);
		return result;
	}

	// Or, let parent deal with it
	else {
		ActionResult result = ClipView::horizontalEncoderAction(offset);
		return result;
	}
}

// new function created for automation instrument clip view to shift automations of the selected
// parameter previously users only had the option to shift ALL automations together as part of community
// feature i disabled automation shifting in the regular instrument clip view
void AutomationView::shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
                                                 int32_t effectiveLength) {
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		modelStackWithParam->autoParam->shiftHorizontally(offset, effectiveLength);
	}

	uiNeedsRendering(this);
}

// vertical encoder action
// no change compared to instrument clip view version
// not used with Audio Clip Automation View
ActionResult AutomationView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (onArrangerView) {
		if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
			if (Buttons::isShiftButtonPressed()) {
				currentSong->adjustMasterTransposeInterval(offset);
			}
			else {
				currentSong->transpose(offset);
			}
		}
		return ActionResult::DEALT_WITH;
	}

	if (getCurrentClip()->type == ClipType::AUDIO) {
		return ActionResult::DEALT_WITH;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	OutputType outputType = clip->output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If encoder button pressed
	if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
		if (inNoteEditor() && currentUIMode != UI_MODE_NONE) {
			// only allow editing note repeats when selecting a note
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				instrumentClipView.editNoteRepeat(offset);
			}
			// only allow euclidean while holding audition pad
			else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
				                             modelStack); // don't create
				if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
					if (clip->output->type != OutputType::KIT) {
						modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(
						    modelStack, instrumentClipView.lastAuditionedYDisplay);
					}
				}

				instrumentClipView.editNumEuclideanEvents(modelStackWithNoteRow, offset,
				                                          instrumentClipView.lastAuditionedYDisplay);
				instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;
				instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			}
		}
		// If user not wanting to move a noteCode, they want to transpose the key
		else if (!currentUIMode && outputType != OutputType::KIT) {
			actionLogger.deleteAllLogs();

			auto nudgeType = Buttons::isShiftButtonPressed() ? VerticalNudgeType::ROW : VerticalNudgeType::OCTAVE;
			clip->nudgeNotesVertically(offset, nudgeType, modelStack);

			instrumentClipView.recalculateColours();
			uiNeedsRendering(this, 0, 0xFFFFFFFF);
			if (inNoteEditor()) {
				renderDisplay();
			}
		}
	}

	// Or, if shift key is pressed
	else if (Buttons::isShiftButtonPressed()) {
		uint32_t whichRowsToRender = 0;

		// If NoteRow(s) auditioned, shift its colour (Kits only)
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			if (!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				if (outputType != OutputType::KIT) {
					goto shiftAllColour;
				}

				for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
					if (instrumentClipView.auditionPadIsPressed[yDisplay]) {
						ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);
						NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
						// This is fine. If we were in Kit mode, we could only be auditioning if there
						// was a NoteRow already
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
			if ((!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress
			     || (!isUIModeActive(UI_MODE_NOTES_PRESSED) && !isUIModeActive(UI_MODE_AUDITIONING)))
			    && (!(isUIModeActive(UI_MODE_NOTES_PRESSED) && inNoteEditor()))) {
				scrollVertical(offset, modelStack);

				// if we're in note editor scrolling vertically will change note selected
				// so we want to re-render the display to show the updated note
				if (inNoteEditor()) {
					renderDisplay();
				}
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

/// if we're entering note editor, we want the selected drum to be visible and in sync with lastAuditionedYDisplay
/// so we'll check if the yDisplay of the selectedDrum is in sync with the lastAuditionedYDisplay
/// if they're not in sync, we'll sync them up by performing a vertical scroll
void AutomationView::potentiallyVerticalScrollToSelectedDrum(InstrumentClip* clip, Output* output) {
	int32_t noteRowIndex;
	Drum* selectedDrum = ((Kit*)output)->selectedDrum;
	if (selectedDrum) {
		NoteRow* noteRow = clip->getNoteRowForDrum(selectedDrum, &noteRowIndex);
		if (noteRow) {
			int32_t lastAuditionedYDisplayScrolled = instrumentClipView.lastAuditionedYDisplay + clip->yScroll;
			if (noteRowIndex != lastAuditionedYDisplayScrolled) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				int32_t yScrollAdjustment = noteRowIndex - lastAuditionedYDisplayScrolled;
				scrollVertical(yScrollAdjustment, modelStack);
			}
		}
	}
}

// Not used with Audio Clip Automation View or Arranger Automation View
ActionResult AutomationView::scrollVertical(int32_t scrollAmount, ModelStackWithTimelineCounter* modelStack) {
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
		// if we're in the note editor we don't want to over-scroll so that selected row is not a valid note row
		if (inNoteEditor()) {
			int32_t lastAuditionedYDisplayScrolled = instrumentClipView.lastAuditionedYDisplay + scrollAmount;
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    clip->getNoteRowOnScreen(lastAuditionedYDisplayScrolled, modelStack);
			// over-scrolled, no valid note row, so return and don't do the actual scrolling
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				return ActionResult::DEALT_WITH;
			}
			// we have a valid note row, so let's set selected drum equal to previous auditioned y display
			else {
				NoteRow* noteRow = clip->getNoteRowOnScreen(lastAuditionedYDisplayScrolled, currentSong);
				if (noteRow) {
					instrumentClipView.setSelectedDrum(noteRow->drum, true);
				}
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

	// Switch off any auditioned notes. But leave on the one whose NoteRow we're moving, if we are
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if ((instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] != 255)
		    && (instrumentClipView.lastAuditionedYDisplay != yDisplay)) {
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
	}

	// Do actual scroll
	clip->yScroll += scrollAmount;

	// Don't render - we'll do that after we've dealt with presses (potentially creating Notes)
	instrumentClipView.recalculateColours();

	// Switch on any auditioned notes - remembering that the one we're shifting (if we are) was left on
	// before
	bool drawnNoteCodeYet = false;
	bool forceStoppedAnyAuditioning = false;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] != 255) {
			// switch its audition back on
			//  Check NoteRow exists, incase we've got a Kit
			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

			if (!isKit || modelStackWithNoteRow->getNoteRowAllowNull()) {

				if (modelStackWithNoteRow->getNoteRowAllowNull() && modelStackWithNoteRow->getNoteRow()->sequenced) {}
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
				/* if you're in the note editor:
				    - don't draw note code because the note code is already on the display
				    - don't update selected drum as this was done above
				*/
				if (!inNoteEditor()) {
					instrumentClipView.drawNoteCode(yDisplay);

					if (isKit) {
						Drum* newSelectedDrum = nullptr;
						NoteRow* noteRow = clip->getNoteRowOnScreen(yDisplay, currentSong);
						if (noteRow) {
							newSelectedDrum = noteRow->drum;
						}
						instrumentClipView.setSelectedDrum(newSelectedDrum, true);
					}
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
		// don't recalculateLastAuditionedNoteOnScreen if we're in the note editor because it
		// messes up the note row selection	for velocity editing
		instrumentClipView.someAuditioningHasEnded(!inNoteEditor());
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
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = getCurrentClip();
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
	}
	int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

	// if user holding a node down, we'll adjust the value of the selected parameter being automated
	if (isUIModeActive(UI_MODE_NOTES_PRESSED) || padSelectionOn) {
		if (inAutomationEditor()
		    && ((instrumentClipView.numEditPadPresses > 0
		         && ((int32_t)(instrumentClipView.timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0))
		        || padSelectionOn)) {

			if (automationModEncoderActionForSelectedPad(modelStackWithParam, whichModEncoder, offset,
			                                             effectiveLength)) {
				return;
			}
		}
		else if (inNoteEditor()) {
			goto followOnAction;
		}
	}
	// if playback is enabled and you are recording, you will be able to record in live automations for
	// the selected parameter this code is also executed if you're just changing the current value of
	// the parameter at the current mod position
	else {
		if (inAutomationEditor()) {
			automationModEncoderActionForUnselectedPad(modelStackWithParam, whichModEncoder, offset, effectiveLength);
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

bool AutomationView::automationModEncoderActionForSelectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                              int32_t whichModEncoder, int32_t offset,
                                                              int32_t effectiveLength) {
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

		// for the second pad pressed in a long press, the square start position is set to the very last
		// nodes position
		if (multiPadPressSelected && (whichModEncoder == 1)) {
			int32_t squareRightEdge = getPosFromSquare(xDisplay + 1, xScroll, xZoom);
			squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
		}
		else {
			squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
		}

		if (squareStart < effectiveLength) {

			int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, squareStart);

			int32_t newKnobPos = calculateAutomationKnobPosForModEncoderTurn(modelStackWithParam, knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a
			// value change gets recorded if newKnobPos exceeds 127, then it means current knobPos was
			// 127 and it was increased to 128. In which case, ignore value change
			if (!onArrangerView && ((clip->output->type == OutputType::MIDI_OUT) && (newKnobPos == 64))) {
				return true;
			}

			// use default interpolation settings
			initInterpolation();

			setAutomationParameterValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength,
			                            xScroll, xZoom, true);

			view.potentiallyMakeItHarderToTurnKnob(whichModEncoder, modelStackWithParam, newKnobPos);

			// once first or last pad in a multi pad press is adjusted, re-render calculate multi pad
			// press based on revised start/ending values
			if (multiPadPressSelected) {

				handleAutomationMultiPadPress(modelStackWithParam, clip, leftPadSelectedX, 0, rightPadSelectedX, 0,
				                              effectiveLength, xScroll, xZoom, true);

				renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom,
				                                        xDisplay, true);

				return true;
			}
		}
	}

	return false;
}

void AutomationView::automationModEncoderActionForUnselectedPad(ModelStackWithAutoParam* modelStackWithParam,
                                                                int32_t whichModEncoder, int32_t offset,
                                                                int32_t effectiveLength) {
	Clip* clip = getCurrentClip();

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		if (modelStackWithParam->getTimelineCounter()
		    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

			int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, view.modPos);

			int32_t newKnobPos = calculateAutomationKnobPosForModEncoderTurn(modelStackWithParam, knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a
			// value change gets recorded if newKnobPos exceeds 127, then it means current knobPos was
			// 127 and it was increased to 128. In which case, ignore value change
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

			if (!playbackHandler.isEitherClockActive() || !modelStackWithParam->autoParam->isAutomated()) {
				int32_t knobPos = newKnobPos + kKnobPosOffset;
				renderDisplay(knobPos, kNoSelection, true);
				setAutomationKnobIndicatorLevels(modelStackWithParam, knobPos, knobPos);
			}

			view.potentiallyMakeItHarderToTurnKnob(whichModEncoder, modelStackWithParam, newKnobPos);

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
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
	}
	int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);

	int32_t xScroll = currentSong->xScroll[navSysId];
	int32_t xZoom = currentSong->xZoom[navSysId];

	// If they want to copy or paste automation...
	if (Buttons::isButtonPressed(hid::button::LEARN)) {
		if (on) {
			if (Buttons::isShiftButtonPressed()) {
				// paste within Automation Editor
				if (inAutomationEditor()) {
					pasteAutomation(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
				}
				// paste on Automation Overview / Note Editor
				else {
					instrumentClipView.pasteAutomation(whichModEncoder, navSysId);
				}
			}
			else {
				// copy within Automation Editor
				if (inAutomationEditor()) {
					copyAutomation(modelStackWithParam, clip, xScroll, xZoom);
				}
				// copy on Automation Overview / Note Editor
				else {
					instrumentClipView.copyAutomation(whichModEncoder, navSysId);
				}
			}
		}
	}

	// delete automation of current parameter selected
	else if (Buttons::isShiftButtonPressed() && inAutomationEditor()) {
		if (modelStackWithParam && modelStackWithParam->autoParam) {
			Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE);
			modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

			displayAutomation(padSelectionOn, !display->have7SEG());
		}
	}

	// if we're in automation overview or note editor
	// then we want to allow toggling with mod encoder buttons to change
	// mod encoder selections
	else if (!inAutomationEditor()) {
		goto followOnAction;
	}

	uiNeedsRendering(this);
	return;

followOnAction: // it will come here when you are on the automation overview / in note editor iscreen

	view.modEncoderButtonAction(whichModEncoder, on);
	uiNeedsRendering(this);
}

void AutomationView::copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xScroll,
                                    int32_t xZoom) {
	if (copiedParamAutomation.nodes) {
		delugeDealloc(copiedParamAutomation.nodes);
		copiedParamAutomation.nodes = nullptr;
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
					renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
				}
				else {
					uint32_t squareStart = getMiddlePosFromSquare(leftPadSelectedX, effectiveLength, xScroll, xZoom);

					updateAutomationModPosition(modelStackWithParam, squareStart);
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
// used to fine tune the values of non-midi parameters
void AutomationView::selectEncoderAction(int8_t offset) {
	// 5x acceleration of select encoder when holding the shift button
	if (Buttons::isButtonPressed(deluge::hid::button::SHIFT)) {
		offset = offset * 5;
	}

	// change midi CC or param ID
	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you've selected a mod encoder (e.g. by pressing modEncoderButton) and you're in Automation
	// Overview the currentUIMode will change to Selecting Midi CC. In this case, turning select encoder
	// should allow you to change the midi CC assignment to that modEncoder
	if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
		InstrumentClipMinder::selectEncoderAction(offset);
		return;
	}
	// don't allow switching to automation editor if you're holding the audition pad in arranger
	// automation view
	else if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		return;
	}
	// edit row or note probability or iterance
	else if (inNoteEditor()) {
		// only allow adjusting probability / iterance while holding note
		if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
			instrumentClipView.handleProbabilityOrIteranceEditing(offset, false);
			timeSelectKnobLastReleased = AudioEngine::audioSampleTimer;
			probabilityChanged = true;
		}
		// only allow adjusting row probability / iterance while holding audition
		else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
			instrumentClipView.handleProbabilityOrIteranceEditing(offset, true);
			timeSelectKnobLastReleased = AudioEngine::audioSampleTimer;
			probabilityChanged = true;
		}
		return;
	}
	// if you're in a midi clip
	else if (outputType == OutputType::MIDI_OUT) {
		selectMIDICC(offset, clip);
		getLastSelectedParamShortcut(clip);
	}
	// if you're in arranger view or in a non-midi, non-cv clip (e.g. audio, synth, kit)
	else if (onArrangerView || outputType != OutputType::CV) {
		// if you're in a audio clip, a kit with affect entire enabled, or in arranger view
		if (onArrangerView || (outputType == OutputType::AUDIO)
		    || (outputType == OutputType::KIT && getAffectEntire())) {
			selectGlobalParam(offset, clip);
		}
		// if you're a synth or a kit (with affect entire off and a sound drum selected)
		else if (outputType == OutputType::SYNTH
		         || (outputType == OutputType::KIT && ((Kit*)output)->selectedDrum
		             && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)) {
			selectNonGlobalParam(offset, clip);
		}
		// don't have patch cable blinking logic figured out yet
		if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
		}
		else {
			getLastSelectedParamShortcut(clip);
		}
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
			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
		}
		int32_t effectiveLength = getEffectiveLength(modelStackWithTimelineCounter);
		int32_t xScroll = currentSong->xScroll[navSysId];
		int32_t xZoom = currentSong->xZoom[navSysId];
		renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
	}
	else {
		displayAutomation(true, !display->have7SEG());
	}
	resetParameterShortcutBlinking();
	blinkShortcuts();
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
			while ((id == params::UNPATCHED_PITCH_ADJUST || id == params::UNPATCHED_SIDECHAIN_SHAPE
			        || id == params::UNPATCHED_SIDECHAIN_VOLUME || id == params::UNPATCHED_COMPRESSOR_THRESHOLD
			        || (id >= params::UNPATCHED_FIRST_ARP_PARAM && id <= params::UNPATCHED_LAST_ARP_PARAM)
			        || id == params::UNPATCHED_ARP_RATE)) {

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
	else if (clip->output->type == OutputType::AUDIO) {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		{
			while ((id >= params::UNPATCHED_FIRST_ARP_PARAM && id <= params::UNPATCHED_LAST_ARP_PARAM)
			       || id == params::UNPATCHED_ARP_RATE) {

				if (offset < 0) {
					offset -= 1;
				}
				else if (offset > 0) {
					offset += 1;
				}
				idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
				                                        kNumGlobalParamsForAutomation);
				id = globalParamsForAutomation[idx].second;
			}
		}
		clip->lastSelectedParamID = id;
		clip->lastSelectedParamKind = kind;
		clip->lastSelectedParamArrayPosition = idx;
	}
	else {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		clip->lastSelectedParamID = id;
		clip->lastSelectedParamKind = kind;
		clip->lastSelectedParamArrayPosition = idx;
	}
	automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next synth or kit non-affect entire param
void AutomationView::selectNonGlobalParam(int32_t offset, Clip* clip) {
	bool foundPatchCable = false;
	// if we previously selected a patch cable, we'll see if there are any more to scroll through
	if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
		foundPatchCable = selectPatchCable(offset, clip);
		// did we find another patch cable?
		if (!foundPatchCable) {
			// if we haven't found a patch cable, it means we reached beginning or end of patch cable
			// list if we're scrolling right, we'll resume with selecting a regular param from beg of
			// list if we're scrolling left, we'll resume with selecting a regular param from end of
			// list to do so we re-set the last selected param array position

			// scrolling right
			if (offset > 0) {
				clip->lastSelectedParamArrayPosition = kNumNonGlobalParamsForAutomation - 1;
			}
			// scrolling left
			else if (offset < 0) {
				clip->lastSelectedParamArrayPosition = 0;
			}
		}
	}
	// if we didn't find anymore patch cables, then we'll select a regular param from the list
	if (!foundPatchCable) {
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

		// did we reach beginning or end of list?
		// if yes, then let's scroll through patch cables
		// but only if we haven't already scrolled through patch cables already above
		if ((clip->lastSelectedParamKind != params::Kind::PATCH_CABLE)
		    && (((offset > 0) && (idx < clip->lastSelectedParamArrayPosition))
		        || ((offset < 0) && (idx > clip->lastSelectedParamArrayPosition)))) {
			foundPatchCable = selectPatchCable(offset, clip);
		}

		// if we didn't find a patch cable, then we'll resume with scrolling the non-patch cable list
		if (!foundPatchCable) {
			auto [kind, id] = nonGlobalParamsForAutomation[idx];
			clip->lastSelectedParamID = id;
			clip->lastSelectedParamKind = kind;
			clip->lastSelectedParamArrayPosition = idx;
		}
	}
	automationParamType = AutomationParamType::PER_SOUND;
}

// iterate through the patch cable list to select the previous or next patch cable
// actual selecting of the patch cable is done in the selectPatchCableAtIndex function
bool AutomationView::selectPatchCable(int32_t offset, Clip* clip) {
	ParamManagerForTimeline* paramManager = clip->getCurrentParamManager();
	if (paramManager) {
		PatchCableSet* set = paramManager->getPatchCableSetAllowJibberish();
		// make sure it's not jiberish
		if (set) {
			// do we have any patch cables?
			if (set->numPatchCables > 0) {
				bool foundCurrentPatchCable = false;
				// scrolling right
				if (offset > 0) {
					// loop from beginning to end of patch cable list
					for (int i = 0; i < set->numPatchCables; i++) {
						// loop through patch cables until we've found a new one and select it
						// adjacent to current found patch cable (if we previously selected one)
						if (selectPatchCableAtIndex(clip, set, i, foundCurrentPatchCable)) {
							return true;
						}
					}
				}
				// scrolling left
				else if (offset < 0) {
					// loop from end to beginning of patch cable list
					for (int i = set->numPatchCables - 1; i >= 0; i--) {
						// loop through patch cables until we've found a new one and select it
						// adjacent to current found patch cable (if we previously selected one)
						if (selectPatchCableAtIndex(clip, set, i, foundCurrentPatchCable)) {
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

// this function does the actual selecting of a patch cable
// see if the patch cable selected is different from current one selected (or not selected)
// if we havent already selected a patch cable, we'll select this one
// if we selected one previously, we'll see if this one is adjacent to the previous one selected
// if it's adjacent to the previous one selected, we'll select this one
bool AutomationView::selectPatchCableAtIndex(Clip* clip, PatchCableSet* set, int32_t patchCableIndex,
                                             bool& foundCurrentPatchCable) {
	PatchCable* cable = &set->patchCables[patchCableIndex];
	ParamDescriptor desc = cable->destinationParamDescriptor;
	// need to add patch cable source to the descriptor so that we can get the paramId from it
	desc.addSource(cable->from);

	// if we've previously selected a patch cable, we want to start scrolling from that patch cable
	// note: the reason why we can't save the patchCableIndex to make finding the previous patch
	// cable selected easier is because the patch cable array gets re-indexed as patch cables get
	// added or removed or values change. Thus you need to search for the previous patch cable to get
	// the updated index and then you can find the adjacent patch cable in the list.
	if (desc.data == clip->lastSelectedParamID) {
		foundCurrentPatchCable = true;
	}
	// if we found the patch cable we previously selected and we found another one
	// or we hadn't selected a patch cable previously and found a patch cable
	// select the one we found
	else if ((foundCurrentPatchCable || (clip->lastSelectedParamKind != params::Kind::PATCH_CABLE))
	         && (desc.data != clip->lastSelectedParamID)) {
		clip->lastSelectedPatchSource = cable->from;
		clip->lastSelectedParamID = desc.data;
		clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
		return true;
	}
	return false;
}

// used with SelectEncoderAction to get the next midi CC
void AutomationView::selectMIDICC(int32_t offset, Clip* clip) {
	if (onAutomationOverview()) {
		clip->lastSelectedParamID = CC_NUMBER_NONE;
	}
	auto newCC = clip->lastSelectedParamID;
	newCC += offset;
	if (newCC < 0) {
		newCC = CC_NUMBER_Y_AXIS;
	}
	else if (newCC >= kNumCCExpression) {
		newCC = 0;
	}
	if (newCC == CC_EXTERNAL_MOD_WHEEL) {
		// mod wheel is actually CC_NUMBER_Y_AXIS (122) internally
		newCC += offset;
	}
	clip->lastSelectedParamID = newCC;
	automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next parameter in the list of parameters
int32_t AutomationView::getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
                                                          int32_t numParams) {
	int32_t idx;
	// if you haven't selected a parameter yet, start at the beginning of the list
	if (onAutomationOverview()) {
		idx = 0;
	}
	// if you are scrolling left and are at the beginning of the list, go to the end of the list
	else if ((lastSelectedParamArrayPosition + offset) < 0) {
		idx = numParams + offset;
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
void AutomationView::getLastSelectedParamShortcut(Clip* clip) {
	bool paramShortcutFound = false;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (onArrangerView) {
				if (unpatchedGlobalParamShortcuts[x][y] == currentSong->lastSelectedParamID) {
					currentSong->lastSelectedParamShortcutX = x;
					currentSong->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
			else if (clip->output->type == OutputType::MIDI_OUT) {
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
				        && unpatchedGlobalParamShortcuts[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == params::Kind::EXPRESSION
				        && params::expressionParamFromShortcut(x, y) == clip->lastSelectedParamID)) {
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
		if (onArrangerView) {
			currentSong->lastSelectedParamShortcutX = kNoSelection;
			currentSong->lastSelectedParamShortcutY = kNoSelection;
		}
		else {
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
		}
	}
}

void AutomationView::getLastSelectedParamArrayPosition(Clip* clip) {
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you're in arranger view or in a non-midi, non-cv clip (e.g. audio, synth, kit)
	if (onArrangerView || outputType != OutputType::CV) {
		// if you're in a audio clip, a kit with affect entire enabled, or in arranger view
		if (onArrangerView || (outputType == OutputType::AUDIO)
		    || (outputType == OutputType::KIT && getAffectEntire())) {
			getLastSelectedGlobalParamArrayPosition(clip);
		}
		// if you're a synth or a kit (with affect entire off and a drum selected)
		else if (outputType == OutputType::SYNTH
		         || (outputType == OutputType::KIT && ((Kit*)output)->selectedDrum
		             && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)) {
			getLastSelectedNonGlobalParamArrayPosition(clip);
		}
	}
}

void AutomationView::getLastSelectedNonGlobalParamArrayPosition(Clip* clip) {
	for (auto idx = 0; idx < kNumNonGlobalParamsForAutomation; idx++) {

		auto [kind, id] = nonGlobalParamsForAutomation[idx];

		if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
			clip->lastSelectedParamArrayPosition = idx;
			break;
		}
	}
}

void AutomationView::getLastSelectedGlobalParamArrayPosition(Clip* clip) {
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
void AutomationView::initParameterSelection(bool updateDisplay) {
	resetShortcutBlinking();
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
		clip->lastSelectedPatchSource = PatchSource::NONE;
		clip->lastSelectedParamArrayPosition = 0;

		// if you're on automation overview, turn led off if it's on
		if (clip->type == ClipType::INSTRUMENT && ((InstrumentClip*)clip)->wrapEditing) {
			indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
		}
	}

	automationParamType = AutomationParamType::PER_SOUND;

	// if we're going back to the Automation Overview, set the display to show "Automation Overview"
	// and update the knob indicator levels to match the master FX button selected
	display->cancelPopup();
	view.setKnobIndicatorLevels();
	view.setModLedStates();
	if (updateDisplay) {
		renderDisplay();
	}
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

	resetPadSelectionShortcutBlinking();
}

void AutomationView::initInterpolation() {

	automationView.interpolationBefore = false;
	automationView.interpolationAfter = false;
}

// get's the modelstack for the parameters that are being edited
// the model stack differs for SYNTH's, KIT's, MIDI, and Audio clip's
ModelStackWithAutoParam* AutomationView::getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack,
                                                                       Clip* clip, int32_t paramID,
                                                                       params::Kind paramKind) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (paramID == kNoParamID) {
		paramID = clip->lastSelectedParamID;
		paramKind = clip->lastSelectedParamKind;
	}

	// check if we're in the sound menu and not the settings menu
	// because in the settings menu, the menu mod controllable's aren't setup, so we don't want to use those
	bool inSoundMenu = getCurrentUI() == &soundEditor && !soundEditor.inSettingsMenu();

	modelStackWithParam =
	    clip->output->getModelStackWithParam(modelStack, clip, paramID, paramKind, getAffectEntire(), inSoundMenu);

	return modelStackWithParam;
}

// calculates the length of the arrangement timeline, clip or the length of the kit row
// if you're in a synth clip, kit clip with affect entire enabled or midi clip it returns clip length
// if you're in a kit clip with affect entire disabled and a row selected, it returns kit row length
int32_t AutomationView::getEffectiveLength(ModelStackWithTimelineCounter* modelStack) {
	Clip* clip = getCurrentClip();
	OutputType outputType = clip->output->type;

	int32_t effectiveLength = 0;

	if (onArrangerView) {
		effectiveLength = arrangerView.getMaxLength();
	}
	else if (outputType == OutputType::KIT && !getAffectEntire()) {
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

int32_t AutomationView::getNavSysId() const {
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
int32_t AutomationView::getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t squareStart) {
	// obtain value corresponding to the two pads that were pressed in a multi pad press action
	int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareStart, modelStack);
	int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(currentValue, modelStack);

	return knobPos;
}

// this function is based off the code in AutoParam::getValueAtPos, it was tweaked to just return
// interpolation status of the left node or right node (depending on the reversed parameter which is
// used to indicate what node in what direction we are looking for (e.g. we want status of left node, or
// right node, relative to the current pos we are looking at
bool AutomationView::getAutomationNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos, bool reversed) {

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

// this function writes the new values calculated by the handleAutomationSinglePadPress and
// handleAutomationMultiPadPress functions
void AutomationView::setAutomationParameterValue(ModelStackWithAutoParam* modelStack, int32_t knobPos,
                                                 int32_t squareStart, int32_t xDisplay, int32_t effectiveLength,
                                                 int32_t xScroll, int32_t xZoom, bool modEncoderAction) {

	int32_t newValue = modelStack->paramCollection->knobPosToParamValue(knobPos, modelStack);

	uint32_t squareWidth = 0;

	// for a multi pad press, the beginning and ending pad presses are set with a square width of 3 (1
	// node).
	if (multiPadPressSelected) {
		squareWidth = kParamNodeWidth;
	}
	else {
		squareWidth = getSquareWidth(xDisplay, effectiveLength, xScroll, xZoom);
	}

	// if you're doing a single pad press, you don't want the values around that single press position
	// to change they will change if those nodes around the single pad press were created with
	// interpolation turned on to fix this, re-create those nodes with their current value with
	// interpolation off

	interpolationBefore = getAutomationNodeInterpolation(modelStack, squareStart, true);
	interpolationAfter = getAutomationNodeInterpolation(modelStack, squareStart, false);

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

	// reset interpolation to false for the single pad we're changing (so that the nodes around it don't
	// also change)
	initInterpolation();

	// called twice because there was a weird bug where for some reason the first call wasn't taking
	// effect on one pad (and whatever pad it was changed every time)...super weird...calling twice
	// fixed it...
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);

	if (!onArrangerView) {
		modelStack->getTimelineCounter()->instrumentBeenEdited();
	}

	// in a multi pad press, no need to display all the values calculated
	if (!multiPadPressSelected) {
		int32_t newKnobPos = knobPos + kKnobPosOffset;
		renderDisplay(newKnobPos, kNoSelection, modEncoderAction);
		setAutomationKnobIndicatorLevels(modelStack, newKnobPos, newKnobPos);
	}

	// midi follow and midi feedback enabled
	// re-send midi cc because learned parameter value has changed
	view.sendMidiFollowFeedback(modelStack, knobPos);
}

// sets both knob indicators to the same value when pressing single pad,
// deleting automation, or displaying current parameter value
// multi pad presses don't use this function
void AutomationView::setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
                                                      int32_t knobPosRight) {
	params::Kind kind = modelStack->paramCollection->getParamKind();
	bool isBipolar = isParamBipolar(kind, modelStack->paramId);

	// if you're dealing with a patch cable which has a -128 to +128 range
	// we'll need to convert it to a 0 - 128 range for purpose of rendering on knob indicators
	if (kind == params::Kind::PATCH_CABLE) {
		knobPosLeft = view.convertPatchCableKnobPosToIndicatorLevel(knobPosLeft);
		knobPosRight = view.convertPatchCableKnobPosToIndicatorLevel(knobPosRight);
	}

	bool isBlinking = indicator_leds::isKnobIndicatorBlinking(0) || indicator_leds::isKnobIndicatorBlinking(1);

	if (!isBlinking) {
		indicator_leds::setKnobIndicatorLevel(0, knobPosLeft, isBipolar);
		indicator_leds::setKnobIndicatorLevel(1, knobPosRight, isBipolar);
	}
}

// updates the position that the active mod controllable stack is pointing to
// this sets the current value for the active parameter so that it can be auditioned
void AutomationView::updateAutomationModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart,
                                                 bool updateDisplay, bool updateIndicatorLevels) {

	if (!playbackHandler.isEitherClockActive() || padSelectionOn) {
		if (modelStack && modelStack->autoParam) {
			if (modelStack->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				view.activeModControllableModelStack.paramManager->toForTimeline()->grabValuesFromPos(
				    squareStart, &view.activeModControllableModelStack);

				int32_t knobPos = getAutomationParameterKnobPos(modelStack, squareStart) + kKnobPosOffset;

				if (updateDisplay) {
					renderDisplay(knobPos);
				}

				if (updateIndicatorLevels) {
					setAutomationKnobIndicatorLevels(modelStack, knobPos, knobPos);
				}
			}
		}
	}
}

// takes care of setting the automation value for the single pad that was pressed
void AutomationView::handleAutomationSinglePadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                    int32_t xDisplay, int32_t yDisplay, int32_t effectiveLength,
                                                    int32_t xScroll, int32_t xZoom) {

	Output* output = clip->output;
	OutputType outputType = output->type;

	// this means you are editing a parameter's value
	if (inAutomationEditor()) {
		handleAutomationParameterChange(modelStackWithParam, clip, outputType, xDisplay, yDisplay, effectiveLength,
		                                xScroll, xZoom);
	}

	uiNeedsRendering(this);
}

// called by handle single pad press when it is determined that you are editing parameter automation
// using the grid
void AutomationView::handleAutomationParameterChange(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
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

		updateAutomationModPosition(modelStackWithParam, squareStart);

		if (!multiPadPressSelected) {
			leftPadSelectedX = xDisplay;
		}
	}

	else if (modelStackWithParam && modelStackWithParam->autoParam) {

		uint32_t squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);

		if (squareStart < effectiveLength) {
			// use default interpolation settings
			initInterpolation();

			int32_t newKnobPos = calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, yDisplay);
			setAutomationParameterValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength,
			                            xScroll, xZoom);
		}
	}
}

int32_t AutomationView::calculateAutomationKnobPosForPadPress(ModelStackWithAutoParam* modelStackWithParam,
                                                              OutputType outputType, int32_t yDisplay) {

	int32_t newKnobPos = 0;
	params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

	if (middlePadPressSelected) {
		newKnobPos = calculateAutomationKnobPosForMiddlePadPress(kind, yDisplay);
	}
	else {
		newKnobPos = calculateAutomationKnobPosForSinglePadPress(kind, yDisplay);
	}

	// for Midi Clips, maxKnobPos = 127
	if (outputType == OutputType::MIDI_OUT && newKnobPos == kMaxKnobPos) {
		newKnobPos -= 1; // 128 - 1 = 127
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos
	// set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

// calculates what the new parameter value is when you press a second pad in the same column
// middle value is calculated by taking average of min and max value of the range for the two pad
// presses
int32_t AutomationView::calculateAutomationKnobPosForMiddlePadPress(params::Kind kind, int32_t yDisplay) {
	int32_t newKnobPos = 0;

	int32_t yMin = yDisplay < leftPadSelectedY ? yDisplay : leftPadSelectedY;
	int32_t yMax = yDisplay > leftPadSelectedY ? yDisplay : leftPadSelectedY;
	int32_t minKnobPos = 0;
	int32_t maxKnobPos = 0;

	if (kind == params::Kind::PATCH_CABLE) {
		minKnobPos = patchCableMinPadDisplayValues[yMin];
		maxKnobPos = patchCableMaxPadDisplayValues[yMax];
	}
	else {
		minKnobPos = nonPatchCableMinPadDisplayValues[yMin];
		maxKnobPos = nonPatchCableMaxPadDisplayValues[yMax];
	}

	newKnobPos = (minKnobPos + maxKnobPos) >> 1;

	return newKnobPos;
}

// calculates what the new parameter value is when you press a single pad
int32_t AutomationView::calculateAutomationKnobPosForSinglePadPress(params::Kind kind, int32_t yDisplay) {
	int32_t newKnobPos = 0;

	// patch cable
	if (kind == params::Kind::PATCH_CABLE) {
		newKnobPos = patchCablePadPressValues[yDisplay];
	}
	// non patch cable
	else {
		newKnobPos = nonPatchCablePadPressValues[yDisplay];
	}

	return newKnobPos;
}

// takes care of setting the automation values for the two pads pressed and the pads in between
void AutomationView::handleAutomationMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                   int32_t firstPadX, int32_t firstPadY, int32_t secondPadX,
                                                   int32_t secondPadY, int32_t effectiveLength, int32_t xScroll,
                                                   int32_t xZoom, bool modEncoderAction) {

	int32_t secondPadLeftEdge = getPosFromSquare(secondPadX, xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadLeftEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t firstPadLeftEdge = getPosFromSquare(firstPadX, xScroll, xZoom);
		int32_t secondPadRightEdge = getPosFromSquare(secondPadX + 1, xScroll, xZoom);

		int32_t firstPadValue = 0;
		int32_t secondPadValue = 0;

		// if we're updating the long press values via mod encoder action, then get current values of
		// pads pressed and re-interpolate
		if (modEncoderAction) {
			firstPadValue = getAutomationParameterKnobPos(modelStackWithParam, firstPadLeftEdge) + kKnobPosOffset;

			uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;

			secondPadValue = getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;
		}

		// otherwise if it's a regular long press, calculate values from the y position of the pads
		// pressed
		else {
			OutputType outputType = clip->output->type;
			firstPadValue =
			    calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, firstPadY) + kKnobPosOffset;
			secondPadValue =
			    calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, secondPadY) + kKnobPosOffset;
		}

		// clear existing nodes from long press range

		// reset interpolation settings to default
		initInterpolation();

		// set value for beginning pad press at the very first node position within that pad
		setAutomationParameterValue(modelStackWithParam, firstPadValue - kKnobPosOffset, firstPadLeftEdge, firstPadX,
		                            effectiveLength, xScroll, xZoom);

		// set value for ending pad press at the very last node position within that pad
		int32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		setAutomationParameterValue(modelStackWithParam, secondPadValue - kKnobPosOffset, squareStart, secondPadX,
		                            effectiveLength, xScroll, xZoom);

		// converting variables to float for more accurate interpolation calculation
		float firstPadValueFloat = static_cast<float>(firstPadValue);
		float firstPadXFloat = static_cast<float>(firstPadLeftEdge);
		float secondPadValueFloat = static_cast<float>(secondPadValue);
		float secondPadXFloat = static_cast<float>(squareStart);

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

			// if interpolation is off, values for nodes in between first and second pad will not be set
			// in a staggered/step fashion
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
void AutomationView::renderAutomationDisplayForMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                             int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
                                                             int32_t xDisplay, bool modEncoderAction) {

	int32_t secondPadLeftEdge = getPosFromSquare(rightPadSelectedX, xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadLeftEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t firstPadLeftEdge = getPosFromSquare(leftPadSelectedX, xScroll, xZoom);
		int32_t secondPadRightEdge = getPosFromSquare(rightPadSelectedX + 1, xScroll, xZoom);

		int32_t knobPosLeft = getAutomationParameterKnobPos(modelStackWithParam, firstPadLeftEdge) + kKnobPosOffset;

		uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		int32_t knobPosRight = getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;

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

		setAutomationKnobIndicatorLevels(modelStackWithParam, knobPosLeft, knobPosRight);

		// update position of mod controllable stack
		updateAutomationModPosition(modelStackWithParam, squareStart, false, false);
	}
}

// used to calculate new knobPos when you turn the mod encoders (gold knobs)
int32_t AutomationView::calculateAutomationKnobPosForModEncoderTurn(ModelStackWithAutoParam* modelStackWithParam,
                                                                    int32_t knobPos, int32_t offset) {

	// adjust the current knob so that it is within the range of 0-128 for calculation purposes
	knobPos = knobPos + kKnobPosOffset;

	int32_t newKnobPos = 0;

	if ((knobPos + offset) < 0) {
		params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
		if (kind == params::Kind::PATCH_CABLE) {
			if ((knobPos + offset) >= -kMaxKnobPos) {
				newKnobPos = knobPos + offset;
			}
			else if ((knobPos + offset) < -kMaxKnobPos) {
				newKnobPos = -kMaxKnobPos;
			}
			else {
				newKnobPos = knobPos;
			}
		}
		else {
			newKnobPos = knobPos;
		}
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

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos
	// set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

// used to render automation overview
// used to handle pad actions on automation overview
// used to disable certain actions on the automation overview screen
// e.g. doubling clip length, editing clip length
bool AutomationView::onAutomationOverview() {
	return (!inAutomationEditor() && !inNoteEditor());
}

bool AutomationView::inAutomationEditor() {
	if (onArrangerView) {
		if (currentSong->lastSelectedParamID == kNoSelection) {
			return false;
		}
	}
	else if (getCurrentClip()->lastSelectedParamID == kNoSelection) {
		return false;
	}

	return true;
}

void AutomationView::setAutomationParamType() {
	automationParamType = AutomationParamType::PER_SOUND;
	if (!inAutomationEditor()) {
		Clip* clip = getCurrentClip();
		if ((clip->lastSelectedParamShortcutX == kVelocityShortcutX)
		    && (clip->lastSelectedParamShortcutY == kVelocityShortcutY)) {
			automationParamType = AutomationParamType::NOTE_VELOCITY;
		}
	}
}

// used to check if we're automating a note row specific param type
// e.g. velocity, probability, poly expression, etc.
bool AutomationView::inNoteEditor() {
	return (automationParamType != AutomationParamType::PER_SOUND);
}

// used to determine the affect entire context
bool AutomationView::getAffectEntire() {
	// arranger view always uses affect entire
	if (onArrangerView) {
		return true;
	}
	// are you in the sound menu for a kit?
	else if (getCurrentOutputType() == OutputType::KIT && getCurrentUI() == &soundEditor
	         && !soundEditor.inSettingsMenu()) {
		// if you're in the kit global FX menu, the menu context is the same as if affect entire is enabled
		if (soundEditor.setupKitGlobalFXMenu) {
			return true;
		}
		// otherwise you're in the kit row context which is the same as if affect entire is disabled
		else {
			return false;
		}
	}
	// otherwise if you're not in the kit sound menu, use the clip affect entire state
	return getCurrentInstrumentClip()->affectEntire;
}

void AutomationView::blinkShortcuts() {
	if (getCurrentUI() == this) {
		int32_t lastSelectedParamShortcutX = kNoSelection;
		int32_t lastSelectedParamShortcutY = kNoSelection;
		if (onArrangerView) {
			lastSelectedParamShortcutX = currentSong->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = currentSong->lastSelectedParamShortcutY;
		}
		else {
			Clip* clip = getCurrentClip();
			lastSelectedParamShortcutX = clip->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = clip->lastSelectedParamShortcutY;
		}
		// if a Param has been selected for editing, blink its shortcut pad
		if (lastSelectedParamShortcutX != kNoSelection) {
			if (!parameterShortcutBlinking) {
				soundEditor.setupShortcutBlink(lastSelectedParamShortcutX, lastSelectedParamShortcutY, 10);
				soundEditor.blinkShortcut();

				parameterShortcutBlinking = true;
			}
		}
		// unset previously set blink timers if not editing a parameter
		else {
			resetParameterShortcutBlinking();
		}
	}
	if (interpolation && !inNoteEditor()) {
		if (!interpolationShortcutBlinking) {
			blinkInterpolationShortcut();
		}
	}
	else {
		resetInterpolationShortcutBlinking();
	}
	if (padSelectionOn) {
		blinkPadSelectionShortcut();
	}
	else {
		resetPadSelectionShortcutBlinking();
	}
	if (inNoteEditor()) {
		if (!instrumentClipView.noteRowBlinking) {
			instrumentClipView.blinkSelectedNoteRow();
		}
	}
	else {
		instrumentClipView.resetSelectedNoteRowBlinking();
	}
}

void AutomationView::resetShortcutBlinking() {
	soundEditor.resetSourceBlinks();
	resetParameterShortcutBlinking();
	resetInterpolationShortcutBlinking();
	resetPadSelectionShortcutBlinking();
	instrumentClipView.resetSelectedNoteRowBlinking();
}

// created this function to undo any existing parameter shortcut blinking so that it doesn't get
// rendered in automation view also created it so that you can reset blinking when a parameter is
// deselected or when you enter/exit automation view
void AutomationView::resetParameterShortcutBlinking() {
	uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	parameterShortcutBlinking = false;
}

// created this function to undo any existing interpolation shortcut blinking so that it doesn't get
// rendered in automation view also created it so that you can reset blinking when interpolation is
// turned off or when you enter/exit automation view
void AutomationView::resetInterpolationShortcutBlinking() {
	uiTimerManager.unsetTimer(TimerName::INTERPOLATION_SHORTCUT_BLINK);
	interpolationShortcutBlinking = false;
}

void AutomationView::blinkInterpolationShortcut() {
	PadLEDs::flashMainPad(kInterpolationShortcutX, kInterpolationShortcutY);
	uiTimerManager.setTimer(TimerName::INTERPOLATION_SHORTCUT_BLINK, 3000);
	interpolationShortcutBlinking = true;
}

// used to blink waveform shortcut when in pad selection mode
void AutomationView::resetPadSelectionShortcutBlinking() {
	uiTimerManager.unsetTimer(TimerName::PAD_SELECTION_SHORTCUT_BLINK);
	padSelectionShortcutBlinking = false;
}

void AutomationView::blinkPadSelectionShortcut() {
	PadLEDs::flashMainPad(kPadSelectionShortcutX, kPadSelectionShortcutY);
	uiTimerManager.setTimer(TimerName::PAD_SELECTION_SHORTCUT_BLINK, 3000);
	padSelectionShortcutBlinking = true;
}
