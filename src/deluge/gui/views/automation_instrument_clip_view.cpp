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

#include "gui/views/automation_instrument_clip_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour.h"
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
#include "util/functions.h"
#include <new>
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

using namespace deluge::gui;

const uint32_t auditionPadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, UI_MODE_HORIZONTAL_SCROLL, UI_MODE_RECORD_COUNT_IN,
                                             UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

const uint32_t editPadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

const uint32_t mutePadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

static const uint32_t verticalScrollUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN,
                                                 0};

//synth and kit rows FX - sorted in the order that Parameters are scrolled through on the display
const std::array<std::pair<Param::Kind, ParamType>, kNumNonKitAffectEntireParamsForAutomation>
    nonKitAffectEntireParamsForAutomation{{
        {Param::Kind::PATCHED, Param::Global::VOLUME_POST_FX}, //Master Volume, Pitch, Pan
        {Param::Kind::PATCHED, Param::Local::PITCH_ADJUST},
        {Param::Kind::PATCHED, Param::Local::PAN},
        {Param::Kind::PATCHED, Param::Local::LPF_FREQ}, //LPF Cutoff, Resonance, Morph
        {Param::Kind::PATCHED, Param::Local::LPF_RESONANCE},
        {Param::Kind::PATCHED, Param::Local::LPF_MORPH},
        {Param::Kind::PATCHED, Param::Local::HPF_FREQ}, //HPF Cutoff, Resonance, Morph
        {Param::Kind::PATCHED, Param::Local::HPF_RESONANCE},
        {Param::Kind::PATCHED, Param::Local::HPF_MORPH},
        {Param::Kind::UNPATCHED, Param::Unpatched::BASS}, //Bass, Bass Freq
        {Param::Kind::UNPATCHED, Param::Unpatched::BASS_FREQ},
        {Param::Kind::UNPATCHED, Param::Unpatched::TREBLE}, //Treble, Treble Freq
        {Param::Kind::UNPATCHED, Param::Unpatched::TREBLE_FREQ},
        {Param::Kind::PATCHED, Param::Global::REVERB_AMOUNT}, //Reverb Amount
        {Param::Kind::PATCHED, Param::Global::DELAY_RATE},    //Delay Rate, Amount
        {Param::Kind::PATCHED, Param::Global::DELAY_FEEDBACK},
        {Param::Kind::PATCHED, Param::Global::VOLUME_POST_REVERB_SEND}, //Sidechain Send, Shape
        {Param::Kind::UNPATCHED, Param::Unpatched::COMPRESSOR_SHAPE},
        {Param::Kind::UNPATCHED, Param::Unpatched::SAMPLE_RATE_REDUCTION}, //Decimation, Bitcrush, Wavefolder
        {Param::Kind::UNPATCHED, Param::Unpatched::BITCRUSHING},
        {Param::Kind::PATCHED, Param::Local::FOLD},
        {Param::Kind::PATCHED,
         Param::Local::OSC_A_VOLUME}, //OSC 1 Volume, Pitch, Phase Width, Carrier Feedback, Wave Index
        {Param::Kind::PATCHED, Param::Local::OSC_A_PITCH_ADJUST},
        {Param::Kind::PATCHED, Param::Local::OSC_A_PHASE_WIDTH},
        {Param::Kind::PATCHED, Param::Local::CARRIER_0_FEEDBACK},
        {Param::Kind::PATCHED,
         Param::Local::OSC_A_WAVE_INDEX}, //OSC 2 Volume, Pitch, Phase Width, Carrier Feedback, Wave Index
        {Param::Kind::PATCHED, Param::Local::OSC_B_VOLUME},
        {Param::Kind::PATCHED, Param::Local::OSC_B_PITCH_ADJUST},
        {Param::Kind::PATCHED, Param::Local::OSC_B_PHASE_WIDTH},
        {Param::Kind::PATCHED, Param::Local::CARRIER_1_FEEDBACK},
        {Param::Kind::PATCHED, Param::Local::OSC_B_WAVE_INDEX},
        {Param::Kind::PATCHED, Param::Local::MODULATOR_0_VOLUME}, //FM Mod 1 Volume, Pitch, Feedback
        {Param::Kind::PATCHED, Param::Local::MODULATOR_0_PITCH_ADJUST},
        {Param::Kind::PATCHED, Param::Local::MODULATOR_0_FEEDBACK},
        {Param::Kind::PATCHED, Param::Local::MODULATOR_1_VOLUME}, //FM Mod 2 Volume, Pitch, Feedback
        {Param::Kind::PATCHED, Param::Local::MODULATOR_1_PITCH_ADJUST},
        {Param::Kind::PATCHED, Param::Local::MODULATOR_1_FEEDBACK},
        {Param::Kind::PATCHED, Param::Local::ENV_0_ATTACK}, //Env 1 ADSR
        {Param::Kind::PATCHED, Param::Local::ENV_0_DECAY},
        {Param::Kind::PATCHED, Param::Local::ENV_0_SUSTAIN},
        {Param::Kind::PATCHED, Param::Local::ENV_0_RELEASE},
        {Param::Kind::PATCHED, Param::Local::ENV_1_ATTACK}, //Env 2 ADSR
        {Param::Kind::PATCHED, Param::Local::ENV_1_DECAY},
        {Param::Kind::PATCHED, Param::Local::ENV_1_SUSTAIN},
        {Param::Kind::PATCHED, Param::Local::ENV_1_RELEASE},
        {Param::Kind::PATCHED, Param::Global::LFO_FREQ},           //LFO 1 Freq
        {Param::Kind::PATCHED, Param::Local::LFO_LOCAL_FREQ},      //LFO 2 Freq
        {Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_OFFSET}, //Mod FX Offset, Feedback, Depth, Rate
        {Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_FEEDBACK},
        {Param::Kind::PATCHED, Param::Global::MOD_FX_DEPTH},
        {Param::Kind::PATCHED, Param::Global::MOD_FX_RATE},
        {Param::Kind::PATCHED, Param::Global::ARP_RATE}, //Arp Rate, Gate
        {Param::Kind::UNPATCHED, Param::Unpatched::Sound::ARP_GATE},
        {Param::Kind::PATCHED, Param::Local::NOISE_VOLUME},            //Noise
        {Param::Kind::UNPATCHED, Param::Unpatched::Sound::PORTAMENTO}, //Portamento
    }};

//kit affect entire FX - sorted in the order that Parameters are scrolled through on the display
const std::array<std::pair<Param::Kind, ParamType>, kNumKitAffectEntireParamsForAutomation>
    kitAffectEntireParamsForAutomation{{
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::VOLUME}, //Master Volume, Pitch, Pan
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::PITCH_ADJUST},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::PAN},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::LPF_FREQ}, //LPF Cutoff, Resonance
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::LPF_RES},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::HPF_FREQ}, //HPF Cutoff, Resonance
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::HPF_RES},
        {Param::Kind::UNPATCHED, Param::Unpatched::BASS}, //Bass, Bass Freq
        {Param::Kind::UNPATCHED, Param::Unpatched::BASS_FREQ},
        {Param::Kind::UNPATCHED, Param::Unpatched::TREBLE}, //Treble, Treble Freq
        {Param::Kind::UNPATCHED, Param::Unpatched::TREBLE_FREQ},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT}, //Reverb Amount
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::DELAY_RATE},         //Delay Rate, Amount
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::DELAY_AMOUNT},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME}, //Sidechain Send, Shape
        {Param::Kind::UNPATCHED, Param::Unpatched::COMPRESSOR_SHAPE},
        {Param::Kind::UNPATCHED, Param::Unpatched::SAMPLE_RATE_REDUCTION}, //Decimation, Bitcrush
        {Param::Kind::UNPATCHED, Param::Unpatched::BITCRUSHING},
        {Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_OFFSET}, //Mod FX Offset, Feedback, Depth, Rate
        {Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_FEEDBACK},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::MOD_FX_RATE},
        {Param::Kind::UNPATCHED, Param::Unpatched::Sound::ARP_GATE},   //Arp Gate
        {Param::Kind::UNPATCHED, Param::Unpatched::Sound::PORTAMENTO}, //Portamento
    }};

//grid sized arrays to assign automatable parameters to the grid

const uint32_t patchedParamShortcutsForAutomation[kDisplayWidth][kDisplayHeight] = {
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {Param::Local::OSC_A_VOLUME, Param::Local::OSC_A_PITCH_ADJUST, 0xFFFFFFFF, Param::Local::OSC_A_PHASE_WIDTH,
     0xFFFFFFFF, Param::Local::CARRIER_0_FEEDBACK, Param::Local::OSC_A_WAVE_INDEX, Param::Local::NOISE_VOLUME},
    {Param::Local::OSC_B_VOLUME, Param::Local::OSC_B_PITCH_ADJUST, 0xFFFFFFFF, Param::Local::OSC_B_PHASE_WIDTH,
     0xFFFFFFFF, Param::Local::CARRIER_1_FEEDBACK, Param::Local::OSC_B_WAVE_INDEX, 0xFFFFFFFF},
    {Param::Local::MODULATOR_0_VOLUME, Param::Local::MODULATOR_0_PITCH_ADJUST, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     Param::Local::MODULATOR_0_FEEDBACK, 0xFFFFFFFF, 0xFFFFFFFF},
    {Param::Local::MODULATOR_1_VOLUME, Param::Local::MODULATOR_1_PITCH_ADJUST, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     Param::Local::MODULATOR_1_FEEDBACK, 0xFFFFFFFF, 0xFFFFFFFF},
    {Param::Global::VOLUME_POST_FX, 0xFFFFFFFF, Param::Local::PITCH_ADJUST, Param::Local::PAN, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, Param::Local::FOLD},
    {Param::Local::ENV_0_RELEASE, Param::Local::ENV_0_SUSTAIN, Param::Local::ENV_0_DECAY, Param::Local::ENV_0_ATTACK,
     Param::Local::LPF_MORPH, 0xFFFFFFFF, Param::Local::LPF_RESONANCE, Param::Local::LPF_FREQ},
    {Param::Local::ENV_1_RELEASE, Param::Local::ENV_1_SUSTAIN, Param::Local::ENV_1_DECAY, Param::Local::ENV_1_ATTACK,
     Param::Local::HPF_MORPH, 0xFFFFFFFF, Param::Local::HPF_RESONANCE, Param::Local::HPF_FREQ},
    {0xFFFFFFFF, 0xFFFFFFFF, Param::Global::VOLUME_POST_REVERB_SEND, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF},
    {Param::Global::ARP_RATE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {Param::Global::LFO_FREQ, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, Param::Global::MOD_FX_DEPTH,
     Param::Global::MOD_FX_RATE},
    {Param::Local::LFO_LOCAL_FREQ, 0xFFFFFFFF, 0xFFFFFFFF, Param::Global::REVERB_AMOUNT, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF},
    {Param::Global::DELAY_RATE, 0xFFFFFFFF, 0xFFFFFFFF, Param::Global::DELAY_FEEDBACK, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}};

const uint32_t unpatchedParamShortcutsForAutomation[kDisplayWidth][kDisplayHeight] = {
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, Param::Unpatched::SAMPLE_RATE_REDUCTION,
     Param::Unpatched::BITCRUSHING, 0xFFFFFFFF},
    {Param::Unpatched::Sound::PORTAMENTO, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, Param::Unpatched::COMPRESSOR_SHAPE, 0xFFFFFFFF,
     Param::Unpatched::BASS, Param::Unpatched::BASS_FREQ},
    {0xFFFFFFFF, 0xFFFFFFFF, Param::Unpatched::Sound::ARP_GATE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     Param::Unpatched::TREBLE, Param::Unpatched::TREBLE_FREQ},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, Param::Unpatched::MOD_FX_OFFSET, Param::Unpatched::MOD_FX_FEEDBACK,
     0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}};

const uint32_t globalEffectableParamShortcutsForAutomation[kDisplayWidth][kDisplayHeight] = {
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {Param::Unpatched::GlobalEffectable::VOLUME, 0xFFFFFFFF, Param::Unpatched::GlobalEffectable::PITCH_ADJUST,
     Param::Unpatched::GlobalEffectable::PAN, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     Param::Unpatched::GlobalEffectable::LPF_RES, Param::Unpatched::GlobalEffectable::LPF_FREQ},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     Param::Unpatched::GlobalEffectable::HPF_RES, Param::Unpatched::GlobalEffectable::HPF_FREQ},
    {0xFFFFFFFF, 0xFFFFFFFF, Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH, Param::Unpatched::GlobalEffectable::MOD_FX_RATE},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF},
    {Param::Unpatched::GlobalEffectable::DELAY_RATE, 0xFFFFFFFF, 0xFFFFFFFF,
     Param::Unpatched::GlobalEffectable::DELAY_AMOUNT, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}};

//grid sized array to assign midi cc values to each pad on the grid

const uint32_t midiCCShortcutsForAutomation[kDisplayWidth][kDisplayHeight] = {

    {0, 16, 32, 48, 64, 80, 96, 112},          {1, 17, 33, 49, 65, 81, 97, 113},

    {2, 18, 34, 50, 66, 82, 98, 114},          {3, 19, 35, 51, 67, 83, 99, 115},

    {4, 20, 36, 52, 68, 84, 100, 116},         {5, 21, 37, 53, 69, 85, 101, 117},

    {6, 22, 38, 54, 70, 86, 102, 118},         {7, 23, 39, 55, 71, 87, 103, 119},

    {8, 24, 40, 56, 72, 88, 104, 0xFFFFFFFF},  {9, 25, 41, 57, 73, 89, 105, 0xFFFFFFFF},

    {10, 26, 42, 58, 74, 90, 106, 0xFFFFFFFF}, {11, 27, 43, 59, 75, 91, 107, 0xFFFFFFFF},

    {12, 28, 44, 60, 76, 92, 108, 0xFFFFFFFF}, {13, 29, 45, 61, 77, 93, 109, 0xFFFFFFFF},

    {14, 30, 46, 62, 78, 94, 110, 120},        {15, 31, 47, 63, 79, 95, 111, 121}};

//let's render some love <3

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

//VU meter style colours for the automation editor

const uint8_t rowColour[kDisplayHeight][3] = {

    {0, 255, 0}, {36, 219, 0}, {73, 182, 0}, {109, 146, 0}, {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0}};

const uint8_t rowTailColour[kDisplayHeight][3] = {

    {2, 53, 2}, {9, 46, 2}, {17, 38, 2}, {24, 31, 2}, {31, 24, 2}, {38, 17, 2}, {46, 9, 2}, {53, 2, 2}};

const uint8_t rowBlurColour[kDisplayHeight][3] = {

    {71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57}, {76, 70, 53}, {77, 60, 48}, {78, 49, 44}, {79, 39, 39}};

AutomationInstrumentClipView automationInstrumentClipView{};

AutomationInstrumentClipView::AutomationInstrumentClipView() {

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

	//initialize automation view specific variables
	interpolation = true;
	interpolationBefore = false;
	interpolationAfter = false;
	//used to prevent excessive blinking when you're scrolling with horizontal / vertical / mod encoders
	encoderAction = false;
	//used to reset shortcut blinking
	shortcutBlinking = false;
	//used to enter pad selection mode
	padSelectionOn = false;
	multiPadPressSelected = false;
	multiPadPressActive = false;
	leftPadSelectedX = kNoLastSelectedPad;
	leftPadSelectedY = kNoLastSelectedPad;
	rightPadSelectedX = kNoLastSelectedPad;
	rightPadSelectedY = kNoLastSelectedPad;
	lastPadSelectedKnobPos = kNoLastSelectedPad;
}

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

//called everytime you open up the automation view
bool AutomationInstrumentClipView::opened() {

	//grab the default setting for interpolation
	interpolation = runtimeFeatureSettings.get(RuntimeFeatureSettingType::AutomationInterpolate);

	//re-initialize pad selection mode (so you start with the default automation editor)
	initPadSelection();

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	//check if we for some reason, left the automation view, then switch clip types, then came back in
	//if you did that...reset the parameter selection and save the current parameter type selection
	//so we can check this again next time it happens
	if (instrument->type != clip->lastSelectedInstrumentType) {
		initParameterSelection();

		clip->lastSelectedInstrumentType = instrument->type;
	}

	if (clip->wrapEditing) { //turn led off if it's on
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	}

	resetShortcutBlinking();

	openedInBackground();

	InstrumentClipMinder::opened();

	focusRegained();

	return true;
}

// Initializes some stuff to begin a new editing session
void AutomationInstrumentClipView::focusRegained() {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	if (clip->lastSelectedParamID != kNoLastSelectedParamID) {
		displayParameterName(clip->lastSelectedParamID);
		displayAutomation(); //update led indicator levels
	}

	if (instrument->type == InstrumentType::CV) {
		displayCVErrorMessage();
	}

	ClipView::focusRegained();

	instrumentClipView.auditioningSilently = false; // Necessary?

	InstrumentClipMinder::focusRegained();

	instrumentClipView.setLedStates();
}

void AutomationInstrumentClipView::openedInBackground() {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	clip->onKeyboardScreen = false;

	//used when you're in song view / arranger view / keyboard view
	//(so it knows to come back to automation view)
	clip->onAutomationInstrumentClipView = true;

	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	instrumentClipView.recalculateColours();

	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	AudioEngine::logAction("AutomationInstrumentClipView::beginSession 2");

	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight],
		               true);
		instrumentClipView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
		                                 &PadLEDs::occupancyMaskStore[kDisplayHeight]);
	}
	else {
		uiNeedsRendering(this);
	}
}

//used for the play cursor
void AutomationInstrumentClipView::graphicsRoutine() {
	// Briefly, if loading a song fails, during the creation of a new blank one, this could happen.
	if (!currentSong) {
		return;
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

	int32_t newTickSquare;

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
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		int32_t noteRowIndex;
		NoteRow* noteRow = clip->getNoteRowOnScreen(yDisplay, currentSong, &noteRowIndex);
		colours[yDisplay] = (noteRow && noteRow->muted) ? 1 : nonMutedColour;

		if (!reallyNoTickSquare) {
			if (noteRow && noteRow->hasIndependentPlayPos()) {

				int32_t noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

				int32_t rowTickSquare = getSquareFromPos(noteRow->getLivePos(modelStackWithNoteRow));
				if (rowTickSquare < 0 || rowTickSquare >= kDisplayWidth) {
					rowTickSquare = 255;
				}
				tickSquares[yDisplay] = rowTickSquare;
			}
		}
	}

	PadLEDs::setTickSquares(tickSquares, colours);
}

//rendering

//called whenever you call uiNeedsRendering(this) somewhere else
//used to render automation overview, automation editor
//used to setup the shortcut blinking
bool AutomationInstrumentClipView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                  bool drawUndefinedArea) {

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

	instrumentClipView.recalculateColours();

	// erase current image as it will be refreshed
	memset(image, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth) * 3);

	// erase current occupancy mask as it will be refreshed
	memset(occupancyMask, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	performActualRender(whichRows, &image[0][0][0], occupancyMask, currentSong->xScroll[NAVIGATION_CLIP],
	                    currentSong->xZoom[NAVIGATION_CLIP], kDisplayWidth, kDisplayWidth + kSideBarWidth,
	                    drawUndefinedArea);

	InstrumentClip* clip = getCurrentClip();

	if (encoderAction == false) {
		//if a Param has been selected for editing, blink its shortcut pad
		if (clip->lastSelectedParamShortcutX != kNoLastSelectedParamShortcut) {
			if (shortcutBlinking == false) {
				memset(soundEditor.sourceShortcutBlinkFrequencies, 255,
				       sizeof(soundEditor.sourceShortcutBlinkFrequencies));
				soundEditor.setupShortcutBlink(clip->lastSelectedParamShortcutX, clip->lastSelectedParamShortcutY, 10);
				soundEditor.blinkShortcut();

				shortcutBlinking = true;
			}
		}
		//unset previously set blink timers if not editing a parameter
		else {
			resetShortcutBlinking();
		}
	}
	else {
		//doing this so the shortcut doesn't blink like crazy while turning knobs that refresh UI
		encoderAction = false;
	}

	PadLEDs::renderingLock = false;

	return true;
}

//determines whether you should render the automation editor, automation overview or just render some love <3
void AutomationInstrumentClipView::performActualRender(uint32_t whichRows, uint8_t* image,
                                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                       int32_t xScroll, uint32_t xZoom, int32_t renderWidth,
                                                       int32_t imageWidth, bool drawUndefinedArea) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];

		if (instrument->type != InstrumentType::CV
		    && !(instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()
		         && !((Kit*)instrument)->selectedDrum)) {

			//if parameter has been selected, show Automation Editor
			if (clip->lastSelectedParamID != kNoLastSelectedParamID) {

				renderAutomationEditor(modelStack, clip, instrument, image + (yDisplay * imageWidth * 3),
				                       occupancyMaskOfRow, renderWidth, xScroll, xZoom, yDisplay, drawUndefinedArea);
			}

			//if not editing a parameter, show Automation Overview
			else {

				renderAutomationOverview(modelStack, clip, instrument, image + (yDisplay * imageWidth * 3),
				                         occupancyMaskOfRow, yDisplay);
			}
		}

		else {
			if (instrument->type == InstrumentType::CV) {
				renderLove(image + (yDisplay * imageWidth * 3), occupancyMaskOfRow, yDisplay);
			}
		}
	}
}

//renders automation overview
void AutomationInstrumentClipView::renderAutomationOverview(ModelStackWithTimelineCounter* modelStack,
                                                            InstrumentClip* clip, Instrument* instrument,
                                                            uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

		uint8_t* pixel = image + (xDisplay * 3);

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if ((instrument->type == InstrumentType::SYNTH
		     || (instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()))
		    && ((patchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF)
		        || (unpatchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF))) {

			if (patchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {

				modelStackWithParam = getModelStackWithParam(
				    modelStack, clip, patchedParamShortcutsForAutomation[xDisplay][yDisplay], Param::Kind::PATCHED);
			}

			else if (unpatchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {

				modelStackWithParam = getModelStackWithParam(
				    modelStack, clip, unpatchedParamShortcutsForAutomation[xDisplay][yDisplay], Param::Kind::UNPATCHED);
			}
		}

		else if (instrument->type == InstrumentType::KIT && instrumentClipView.getAffectEntire()
		         && ((unpatchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF)
		             || (globalEffectableParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF))) {

			if (unpatchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {

				modelStackWithParam =
				    getModelStackWithParam(modelStack, clip, unpatchedParamShortcutsForAutomation[xDisplay][yDisplay]);
			}

			else if (globalEffectableParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {

				modelStackWithParam = getModelStackWithParam(
				    modelStack, clip, globalEffectableParamShortcutsForAutomation[xDisplay][yDisplay]);
			}
		}

		else if (instrument->type == InstrumentType::MIDI_OUT
		         && midiCCShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
			modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, midiCCShortcutsForAutomation[xDisplay][yDisplay]);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {
			//highlight pad white if the parameter it represents is currently automated
			if (modelStackWithParam->autoParam->isAutomated()) {
				pixel[0] = 130;
				pixel[1] = 120;
				pixel[2] = 130;
			}

			else {

				if (instrument->type == InstrumentType::MIDI_OUT
				    && midiCCShortcutsForAutomation[xDisplay][yDisplay] <= 119) {

					//formula I came up with to render pad colours from green to red across 119 Midi CC pads
					pixel[0] = 2 + (midiCCShortcutsForAutomation[xDisplay][yDisplay] * ((51 << 20) / 119)) >> 20;
					pixel[1] = 53 - ((midiCCShortcutsForAutomation[xDisplay][yDisplay] * ((51 << 20) / 119)) >> 20);
					pixel[2] = 2;
				}

				//if we're not in a midi clip, highlight the automatable pads dimly grey
				else {

					pixel[0] = kUndefinedGreyShade;
					pixel[1] = kUndefinedGreyShade;
					pixel[2] = kUndefinedGreyShade;
				}
			}

			occupancyMask[xDisplay] = 64;
		}
	}
}

//gets the length of the clip, renders the pads corresponding to current parameter values set up to the clip length
//renders the undefined area of the clip that the user can't interact with
void AutomationInstrumentClipView::renderAutomationEditor(ModelStackWithTimelineCounter* modelStack,
                                                          InstrumentClip* clip, Instrument* instrument, uint8_t* image,
                                                          uint8_t occupancyMask[], int32_t renderWidth, int32_t xScroll,
                                                          uint32_t xZoom, int32_t yDisplay, bool drawUndefinedArea) {

	ModelStackWithAutoParam* modelStackWithParam =
	    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		int32_t effectiveLength;

		if (instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()) {
			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

			effectiveLength = modelStackWithNoteRow->getLoopLength();
		}
		else {
			effectiveLength = clip->loopLength;
		}

		renderRow(modelStackWithParam, image, occupancyMask, true, effectiveLength, true, xScroll, xZoom, 0,
		          renderWidth, false, yDisplay, modelStackWithParam->autoParam->isAutomated());

		if (drawUndefinedArea == true) {

			clip->drawUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth, this,
			                        currentSong->tripletsOn);
		}
	}
}

//this function started off as a copy of the renderRow function from the NoteRow class - I replaced "notes" with "nodes"
//it worked for the most part, but there was bugs so I removed the buggy code and inserted my alternative rendering method
//which always works. hoping to bring back the other code once I've worked out the bugs.
void AutomationInstrumentClipView::renderRow(ModelStackWithAutoParam* modelStack, uint8_t* image,
                                             uint8_t occupancyMask[], bool overwriteExisting,
                                             uint32_t effectiveRowLength, bool allowNoteTails, int32_t xScroll,
                                             uint32_t xZoom, int32_t xStartNow, int32_t xEnd, bool drawRepeats,
                                             int32_t yDisplay, bool isAutomated) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

		uint32_t squareStart = getPosFromSquare(xDisplay);
		uint32_t squareWidth = instrumentClipView.getSquareWidth(xDisplay, effectiveRowLength);
		if (squareWidth != 3) {
			squareStart = squareStart + (squareWidth / 2);
		}

		int32_t knobPos = getParameterKnobPos(modelStack, squareStart) + kKnobPosOffset;

		uint8_t* pixel = image + (xDisplay * 3);

		if (knobPos > (yDisplay * kParamValueIncrementForAutomationDisplay)) {
			if (isAutomated) { //automated, render bright colour
				memcpy(pixel, &rowColour[yDisplay], 3);
			}
			else { //not automated, render less bright tail colour
				memcpy(pixel, &rowTailColour[yDisplay], 3);
			}
			occupancyMask[xDisplay] = 64;
		}

		if (padSelectionOn && ((xDisplay == leftPadSelectedX) || (xDisplay == rightPadSelectedX))) {

			if (knobPos > (yDisplay * kParamValueIncrementForAutomationDisplay)) {
				memcpy(pixel, &rowBlurColour[yDisplay], 3);
			}
			else {
				pixel[0] = kUndefinedGreyShade;
				pixel[1] = kUndefinedGreyShade;
				pixel[2] = kUndefinedGreyShade;
			}
			occupancyMask[xDisplay] = 64;
		}
	}
}

//easter egg lol. it is rendered when you press the CV clip button as you can't use automation view there
//it draws a cute heart and musical note
void AutomationInstrumentClipView::renderLove(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

		uint8_t* pixel = image + (xDisplay * 3);

		if (love[xDisplay][yDisplay] == 0xFFFFFFFF) {

			memcpy(pixel, &rowColour[yDisplay], 3);
			occupancyMask[xDisplay] = 64;
		}
	}
}

//no change compared to the instrument clip view version
bool AutomationInstrumentClipView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {

	return instrumentClipView.renderSidebar(whichRows, image, occupancyMask);
}

//adjust the LED meters
void AutomationInstrumentClipView::displayAutomation() {

	if (!multiPadPressSelected) {

		InstrumentClip* clip = getCurrentClip();

		if (clip->lastSelectedParamID != kNoLastSelectedParamID) {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				if (modelStackWithParam->getTimelineCounter()
				    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

					int32_t knobPos = getParameterKnobPos(modelStackWithParam, view.modPos);

					indicator_leds::setKnobIndicatorLevel(0, knobPos + kKnobPosOffset);
					indicator_leds::setKnobIndicatorLevel(1, knobPos + kKnobPosOffset);
				}
			}
		}
	}
}

//button action

ActionResult AutomationInstrumentClipView::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	// Scale mode button
	if (b == SCALE_MODE) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// Kits can't do scales!
		if (instrument->type == InstrumentType::KIT) {
			if (on) {
				indicator_leds::indicateAlertOnLed(IndicatorLED::KIT);
			}
			return ActionResult::DEALT_WITH;
		}

		actionLogger.deleteAllLogs(); // Can't undo past this!

		if (on && currentUIMode == UI_MODE_NONE) {
			// If user holding shift and we're already in scale mode, cycle through available scales
			if (Buttons::isShiftButtonPressed() && clip->inScaleMode) {
				cycleThroughScales();
				instrumentClipView.recalculateColours();
				uiNeedsRendering(this);
			}
			else if (clip->inScaleMode) {
				exitScaleMode();
			}
			else {
				enterScaleMode();
			}
		}
	}

	// Song view button
	else if (b == SESSION_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentSong->lastClipInstanceEnteredStartPos != -1 || clip->isArrangementOnlyClip()) {
				bool success = arrangerView.transitionToArrangementEditor();
				if (!success) {
					goto doOther;
				}
			}
			else {
doOther:
				// Transition to session view
				//code taken from the keyboard screen instead of calling the transition to session view function
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

	// Keyboard button
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			changeRootUI(&keyboardScreen);
			//reset blinking if you're leaving automation view for keyboard view
			//blinking will be reset when you come back
			resetShortcutBlinking();
		}
	}

	// Clip button - exit mode
	//if you're holding shift or holding an audition pad while pressed clip, don't exit out of automation view
	//reset parameter selection and short blinking instead
	else if (b == CLIP_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (Buttons::isShiftButtonPressed()) {
				initParameterSelection();
				uiNeedsRendering(this);
			}
			else {
				changeRootUI(&instrumentClipView);
			}
			resetShortcutBlinking();
		}
		else if (on && currentUIMode == UI_MODE_AUDITIONING) {
			initParameterSelection();
			resetShortcutBlinking();
			uiNeedsRendering(this);
		}
	}

	// Wrap edit button
	//does not currently work, added to list of future release items
	else if (b == CROSS_SCREEN_EDIT) {
		if (on) {
			if (currentUIMode == UI_MODE_NONE) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				display->displayPopup(l10n::get(l10n::String::STRING_FOR_COMING_SOON));
			}
		}
	}

	//when switching clip type, reset parameter selection and shortcut blinking
	else if (b == KIT && currentUIMode == UI_MODE_NONE) {
		if (on) {
			//don't reset anything if you're already in a KIT clip
			if (instrument->type != InstrumentType::KIT) {
				initParameterSelection();
				resetShortcutBlinking();
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (Buttons::isNewOrShiftButtonPressed()) {
				instrumentClipView.createNewInstrument(InstrumentType::KIT);
			}
			else {
				instrumentClipView.changeInstrumentType(InstrumentType::KIT);
			}
		}
	}

	//when switching clip type, reset parameter selection and shortcut blinking
	else if (b == SYNTH && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		if (on) {
			//don't reset anything if you're already in a SYNTH clip
			if (instrument->type != InstrumentType::SYNTH) {
				initParameterSelection();
				resetShortcutBlinking();
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				//this gets triggered when you change an existing clip to synth / create a new synth clip in song mode
				if (Buttons::isNewOrShiftButtonPressed()) {
					instrumentClipView.createNewInstrument(InstrumentType::SYNTH);
				}
				//this gets triggered when you change clip type to synth from within inside clip view
				else {
					instrumentClipView.changeInstrumentType(InstrumentType::SYNTH);
				}
			}
		}
	}

	//when switching clip type, reset parameter selection and shortcut blinking
	else if (b == MIDI) {
		if (on) {
			//don't reset anything if you're already in a MIDI clip
			if (instrument->type != InstrumentType::MIDI_OUT) {
				initParameterSelection();
				resetShortcutBlinking();
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				instrumentClipView.changeInstrumentType(InstrumentType::MIDI_OUT);
			}
		}
	}

	//when switching clip type, reset parameter selection and shortcut blinking
	else if (b == CV) {
		initParameterSelection();
		resetShortcutBlinking();
		displayCVErrorMessage();

		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				instrumentClipView.changeInstrumentType(InstrumentType::CV);
			}
		}
	}

	// Horizontal encoder button
	else if (b == X_ENC) {

		// If user wants to "multiple" Clip contents
		if (on && Buttons::isShiftButtonPressed() && !isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)
		    && !isOnParameterGridMenuView()) {
			if (isNoUIModeActive()) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				// Zoom to max if we weren't already there...
				if (!zoomToMax()) {
					// Or if we didn't need to do that, double Clip length
					instrumentClipView.doubleClipLengthAction();
				}
				else {
					displayZoomLevel();
				}
			}
			// Whether or not we did the "multiply" action above, we need to be in this UI mode, e.g. for rotating individual NoteRow
			enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}

		// Otherwise...
		else {
			if (isUIModeActive(UI_MODE_AUDITIONING)) {
				if (!on) {
					instrumentClipView.timeHorizontalKnobLastReleased = AudioEngine::audioSampleTimer;
				}
			}
			goto passToOthers; // For exiting the UI mode, I think
		}
	}

	//if holding horizontal encoder button down and pressing back clear automation
	//if you're on automation overview, clear all automation
	//if you're in the automation editor, clear the automation for the parameter in focus

	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		//only allow clearing of a clip if you're in the automation overview
		if (on && clip->lastSelectedParamID == kNoLastSelectedParamID) {
			goto passToOthers;
		}
		else if (on && clip->lastSelectedParamID != kNoLastSelectedParamID) {
			//delete automation of current parameter selected

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_DELETE, false);
				modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

				display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));
				setDisplayParameterNameTimer();
			}
		}
	}

	//Select encoder
	//if you're not pressing shift and press down on the select encoder, toggle interpolation on/off

	else if (!Buttons::isShiftButtonPressed() && b == SELECT_ENC) {
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
			setDisplayParameterNameTimer();
		}
	}

	//when you press affect entire in a kit, the parameter selection needs to reset
	else if (b == AFFECT_ENTIRE) {
		initParameterSelection();
		goto passToOthers;
	}

	else {
passToOthers:

		uiNeedsRendering(this);

		ActionResult result = InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		if (result != ActionResult::NOT_DEALT_WITH) {
			return result;
		}

		result = ClipView::buttonAction(b, on, inCardRoutine);

		if (on && (b == SAVE || b == LOAD)) {
			display->cancelPopup();
		}

		if (on && (b != SAVE && b != LOAD)) {
			setDisplayParameterNameTimer();
		}

		return result;
	}

	if (on && (b == KEYBOARD || b == CLIP_VIEW || b == SESSION_VIEW)) {
		display->cancelPopup();
	}

	if (on && (b != KEYBOARD && b != CLIP_VIEW && b != SESSION_VIEW)) {
		setDisplayParameterNameTimer();
		uiNeedsRendering(this);
	}

	return ActionResult::DEALT_WITH;
}

//simplified version of the InstrumentClipView::enterScaleMode function. No need to render any animation.
void AutomationInstrumentClipView::enterScaleMode(uint8_t yDisplay) {

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
	setDisplayParameterNameTimer();
}

//simplified version of the InstrumentClipView::enterScaleMode function. No need to render any animation.
void AutomationInstrumentClipView::exitScaleMode() {
	int32_t scrollAdjust = instrumentClipView.setupForExitingScaleMode();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	clip->yScroll += scrollAdjust;

	instrumentClipView.recalculateColours();
	setLedStates();
}

//pad action
//handles shortcut pad action for automation (e.g. when you press shift + pad on the grid)
//everything else is pretty much the same as instrument clip view

ActionResult AutomationInstrumentClipView::padAction(int32_t x, int32_t y, int32_t velocity) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	if (instrument->type == InstrumentType::CV) {
		displayCVErrorMessage();
		return ActionResult::DEALT_WITH;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Edit pad action...
	if (x < kDisplayWidth) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		//if the user wants to change the parameter they are editing using Shift + Pad shortcut
		if (velocity) {
			if (Buttons::isShiftButtonPressed()) {

				initPadSelection();
				handleSinglePadPress(modelStack, clip, x, y, true);

				return ActionResult::DEALT_WITH;
			}
		}
		//regular automation step editing action
		if (isUIModeWithinRange(editPadActionUIModes)) {
			editPadAction(velocity, y, x, currentSong->xZoom[NAVIGATION_CLIP]);
		}
	}

	// If mute pad action
	else if (x == kDisplayWidth) {
		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (instrument->type != InstrumentType::KIT) {
				return ActionResult::DEALT_WITH;
			}
			NoteRow* noteRow = clip->getNoteRowOnScreen(y, currentSong);
			if (!noteRow || !noteRow->drum) {
				return ActionResult::DEALT_WITH;
			}
			view.noteRowMuteMidiLearnPadPressed(velocity, noteRow);
		}
		else if (isUIModeWithinRange(mutePadActionUIModes) && velocity) {
			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(y, modelStack);

			//if we're in a kit, and you press a mute pad
			//check if it's a mute pad corresponding to the current selected drum
			//if not, change the drum selection, refresh parameter selection and go back to automation overview
			if (instrument->type == InstrumentType::KIT) {
				if (modelStackWithNoteRow->getNoteRowAllowNull()) {
					Drum* drum = modelStackWithNoteRow->getNoteRow()->drum;
					if (((Kit*)instrument)->selectedDrum != drum) {
						if (!instrumentClipView.getAffectEntire()) {
							initParameterSelection();
						}
					}
				}
			}

			instrumentClipView.mutePadPress(y);

			uiNeedsRendering(this); //re-render mute pads
		}
	}

	// Audition pad action
	else {
		if (x == kDisplayWidth + 1) {

			// "Learning" to this audition pad:
			if (isUIModeActiveExclusively(UI_MODE_MIDI_LEARN)) {
				if (getCurrentUI() == this) {
					if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					if (instrument->type == InstrumentType::KIT) {
						NoteRow* thisNoteRow = clip->getNoteRowOnScreen(y, currentSong);
						if (!thisNoteRow || !thisNoteRow->drum) {
							return ActionResult::DEALT_WITH;
						}
						view.drumMidiLearnPadPressed(velocity, thisNoteRow->drum, (Kit*)instrument);
					}
					else {
						view.melodicInstrumentMidiLearnPadPressed(velocity, (MelodicInstrument*)instrument);
					}
				}
			}

			// Actual basic audition pad press:
			else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
				if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allowable sometimes if in card routine.
				}
				auditionPadAction(velocity, y, Buttons::isShiftButtonPressed());
			}
			setDisplayParameterNameTimer();
		}
	}

	return ActionResult::DEALT_WITH;
}

//edit pad action
//handles single and multi pad presses for automation editing and for parameter selection on the automation overview
//stores pad presses in the EditPadPresses struct of the instrument clip view

void AutomationInstrumentClipView::editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, uint32_t xZoom) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If button down
	if (state) {
		if (!isSquareDefined(xDisplay)) {

			return;
		}
		// If this is a automation-length-edit press...
		//needed for Automation
		if (clip->lastSelectedParamID != kNoLastSelectedParamID && instrumentClipView.numEditPadPresses == 1) {
		    //&& ((int32_t)(instrumentClipView.timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0)) {

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

			if (firstPadX != 255 && firstPadY != 255 && firstPadX != xDisplay) {
				recordSinglePadPress(xDisplay, yDisplay);

				multiPadPressSelected = true;
				multiPadPressActive = true;

				//the long press logic calculates and renders the interpolation as if the press was entered in a forward fashion
				//(where the first pad is to the left of the second pad). if the user happens to enter a long press backwards
				//then we fix that entry by re-ordering the pad presses so that it is forward again
				leftPadSelectedX = firstPadX > xDisplay ? xDisplay : firstPadX;
				leftPadSelectedY = firstPadX > xDisplay ? yDisplay : firstPadY;
				rightPadSelectedX = firstPadX > xDisplay ? firstPadX : xDisplay;
				rightPadSelectedY = firstPadX > xDisplay ? firstPadY : yDisplay;

				//if you're not in pad selection mode, allow user to enter a long press
				if (!padSelectionOn) {
					handleMultiPadPress(modelStack, clip, leftPadSelectedX, leftPadSelectedY, rightPadSelectedX,
					                    rightPadSelectedY);
				}
				else {
					uiNeedsRendering(this);
				}

				//set led indicators to left / right pad selection values
				ModelStackWithAutoParam* modelStackWithParam =
				    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

				if (modelStackWithParam && modelStackWithParam->autoParam) {

					int32_t knobPos =
					    getParameterKnobPos(modelStackWithParam, getPosFromSquare(leftPadSelectedX)) + kKnobPosOffset;
					indicator_leds::setKnobIndicatorLevel(0, knobPos);

					int32_t effectiveLength;

					if (instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()) {
						ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

						effectiveLength = modelStackWithNoteRow->getLoopLength();
					}
					else {
						//this will differ for a kit when in note row mode
						effectiveLength = clip->loopLength;
					}

					int32_t squareRightEdge = getPosFromSquare(rightPadSelectedX + 1);
					uint32_t squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
					knobPos = getParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;
					indicator_leds::setKnobIndicatorLevel(1, knobPos);

					if (modelStackWithParam->getTimelineCounter()
						== view.activeModControllableModelStack.getTimelineCounterAllowNull()) {	

						if (leftPadSelectedX == xDisplay) {
							squareStart = getPosFromSquare(leftPadSelectedX);
						}

						view.activeModControllableModelStack.paramManager->toForTimeline()->grabValuesFromPos(
						squareStart, &view.activeModControllableModelStack);
					}

					//display pad value of second pad pressed
					knobPos = getParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;
					displayParameterValue(knobPos);
				}
			}
		}

		// Or, if this is a regular create-or-select press...
		else {
			if (currentUIMode != UI_MODE_NOTES_PRESSED) {
				multiPadPressActive = false;
			}
			if (recordSinglePadPress(xDisplay, yDisplay)) {	
				handleSinglePadPress(modelStack, clip, xDisplay, yDisplay);
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

		//outside pad selection mode, exit multi pad press once you've let go of the first pad in the long press
		if ((clip->lastSelectedParamID != kNoLastSelectedParamID) && !padSelectionOn && multiPadPressSelected
		    && (currentUIMode != UI_MODE_NOTES_PRESSED)) {
			initPadSelection();
			displayAutomation();
		}
		//switch from long press selection to short press selection in pad selection mode
		else if ((clip->lastSelectedParamID != kNoLastSelectedParamID) && padSelectionOn && multiPadPressSelected
		         && !multiPadPressActive && (currentUIMode != UI_MODE_NOTES_PRESSED)
		         && ((AudioEngine::audioSampleTimer - instrumentClipView.timeLastEditPadPress) < kShortPressTime)) {

			multiPadPressSelected = false;

			leftPadSelectedX = xDisplay;
			rightPadSelectedX = kNoLastSelectedPad;

			uiNeedsRendering(this);
		}

		if (currentUIMode != UI_MODE_NOTES_PRESSED) {
			lastPadSelectedKnobPos = kNoLastSelectedPad;
			setDisplayParameterNameTimer();
		}
	}
}

bool AutomationInstrumentClipView::recordSinglePadPress(int32_t xDisplay, int32_t yDisplay) {

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

//audition pad action
//don't think I changed anything in here compared to the instrument clip view version

void AutomationInstrumentClipView::auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	bool isKit = (instrument->type == InstrumentType::KIT);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
	    clip->getNoteRowOnScreen(yDisplay, modelStackWithTimelineCounter);

	Drum* drum = NULL;

	// If Kit...
	if (isKit) {

		//if we're in a kit, and you press an audition pad
		//check if it's a audition pad corresponding to the current selected drum
		//if not, change the drum selection, refresh parameter selection and go back to automation overview
		if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
			drum = modelStackWithNoteRowOnCurrentClip->getNoteRow()->drum;
			if (((Kit*)instrument)->selectedDrum != drum) {
				if (!instrumentClipView.getAffectEntire()) {
					initParameterSelection();
				}
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
			}

			goto getOut;
		}
	}

	// Or if synth
	else if (instrument->type == InstrumentType::SYNTH) {
		if (velocity) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
				menu_item::multiRangeMenu.noteOnToChangeRange(clip->getYNoteFromYDisplay(yDisplay, currentSong)
				                                              + ((SoundInstrument*)instrument)->transpose);
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
						drum->recordNoteOnEarly((velocity == USE_DEFAULT_VELOCITY) ? instrument->defaultVelocity
						                                                           : velocity,
						                        clip->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
					}
				}
				else {
					// NoteRow is allowed to be NULL in this case.
					int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
					((MelodicInstrument*)instrument)
					    ->earlyNotes.insertElementIfNonePresent(
					        yNote, instrument->defaultVelocity,
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
					clip->recordNoteOn(modelStackWithNoteRowOnCurrentClip,
					                   (velocity == USE_DEFAULT_VELOCITY) ? instrument->defaultVelocity : velocity);
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
			if (instrument->type == InstrumentType::KIT) {
				noteRowOnActiveClip = ((InstrumentClip*)instrument->activeClip)->getNoteRowForDrum(drum);
			}

			// Non-kit
			else {
				int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
				noteRowOnActiveClip = ((InstrumentClip*)instrument->activeClip)->getNoteRowForYNote(yNote);
			}
		}

		// If note on...
		if (velocity) {
			int32_t velocityToSound = velocity;
			if (velocityToSound == USE_DEFAULT_VELOCITY) {
				velocityToSound = instrument->defaultVelocity;
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
				//instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = false;
				enterUIMode(UI_MODE_AUDITIONING);
			}

			instrumentClipView.drawNoteCode(yDisplay);
			instrumentClipView.lastAuditionedYDisplay = yDisplay;

			// Begin resampling / output-recording
			if (Buttons::isButtonPressed(hid::button::RECORD)
			    && audioRecorder.recordingSource == AudioInputChannel::NONE) {
				audioRecorder.beginOutputRecording();
				Buttons::recordButtonPressUsedUp = true;
			}

			if (isKit) {
				instrumentClipView.setSelectedDrum(drum);
				uiNeedsRendering(this); // need to redraw automation grid squares cause selected drum may have changed
				goto getOut;
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
			instrumentClipView.someAuditioningHasEnded(true);
			actionLogger.closeAction(ACTION_NOTEROW_ROTATE);
		}

		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
		uiNeedsRendering(this);
	}

getOut:

	// This has to happen after instrumentClipView.setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
	}
}

//horizontal encoder action
//using this to shift automations left / right
ActionResult AutomationInstrumentClipView::horizontalEncoderAction(int32_t offset) {

	//exit multi pad press selection but keep single pad press selection (if it's selected)
	multiPadPressSelected = false;
	rightPadSelectedX = kNoLastSelectedPad;

	InstrumentClip* clip = getCurrentClip();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	encoderAction = true;

	if (clip->lastSelectedParamID != kNoLastSelectedParamID
	    && ((isNoUIModeActive() && Buttons::isButtonPressed(hid::button::Y_ENC))
	        || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	            && Buttons::isButtonPressed(hid::button::CLIP_VIEW))
	        || (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)))) {

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
		}

		shiftAutomationHorizontally(offset);

		if (offset < 0) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_LEFT));
		}
		else if (offset > 0) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_RIGHT));
		}

		if (offset != 0) {
			setDisplayParameterNameTimer();
		}

		return ActionResult::DEALT_WITH;
	}

	//else if showing the Parameter selection grid menu, disable this action
	else if (isOnParameterGridMenuView()) {
		return ActionResult::DEALT_WITH;
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		if (!instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
wantToEditNoteRowLength:
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
			}

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    instrumentClipView.getOrCreateNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);

			instrumentClipView.editNoteRowLength(modelStackWithNoteRow, offset,
			                                     instrumentClipView.lastAuditionedYDisplay);
			//instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			uiNeedsRendering(this);
		}

		// Unlike for all other cases where we protect against the user accidentally turning the encoder more after releasing their press on it,
		// for this edit-NoteRow-length action, because it's a related action, it's quite likely that the user actually will want to do it after the yes-pressed-encoder-down
		// action, which is "rotate/shift notes in row". So, we have a 250ms timeout for this one.
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
		setDisplayParameterNameTimer();
		return result;
	}
}

//new function created for automation instrument clip view to shift automations of the selected parameter
//previously users only had the option to shift ALL automations together
//as part of community feature i disabled automation shifting in the regular instrument clip view
void AutomationInstrumentClipView::shiftAutomationHorizontally(int32_t offset) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	ModelStackWithAutoParam* modelStackWithParam =
	    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

			uint32_t squareStart = getPosFromSquare(xDisplay);

			int32_t effectiveLength;

			if (instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()) {
				ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

				effectiveLength = modelStackWithNoteRow->getLoopLength();
			}
			else {
				//this will differ for a kit when in note row mode
				effectiveLength = clip->loopLength;
			}

			if (squareStart < effectiveLength) {
				modelStackWithParam->autoParam->shiftHorizontally(offset, effectiveLength);
			}
		}
	}

	uiNeedsRendering(this);
}

//vertical encoder action
//no change compared to instrument clip view version

ActionResult AutomationInstrumentClipView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;
	encoderAction = true;

	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
	}

	// If encoder button pressed
	if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
		// If user not wanting to move a noteCode, they want to transpose the key
		if (!currentUIMode && instrument->type != InstrumentType::KIT) {

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

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
				//display->displayPopup("OCTAVE");
			}

			// Otherwise, transpose single semitone
			else {
				// If current Clip not in scale-mode, just do it
				if (!clip->isScaleModeClip()) {
					clip->transpose(offset, modelStack);

					// If there are no scale-mode Clips at all, move the root note along as well - just in case the user wants to go back to scale mode (in which case the "previous" root note would be used to help guess what root note to go with)
					if (!currentSong->anyScaleModeClips()) {
						currentSong->rootNote += offset;
					}
				}

				// Otherwise, got to do all key-mode Clips
				else {
					currentSong->transposeAllScaleModeClips(offset);
				}
				//display->displayPopup("SEMITONE");
			}
		}
	}

	// Or, if shift key is pressed
	else if (Buttons::isShiftButtonPressed()) {
		uint32_t whichRowsToRender = 0;

		// If NoteRow(s) auditioned, shift its colour (Kits only)
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			//instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			if (!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				if (instrument->type != InstrumentType::KIT) {
					goto shiftAllColour;
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
					if (instrumentClipView.auditionPadIsPressed[yDisplay]) {
						ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);
						NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
						// This is fine. If we were in Kit mode, we could only be auditioning if there was a NoteRow already
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

				return scrollVertical(offset, inCardRoutine, false);
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

//don't think I touch anything here compared to the instrument clip view version
//so I might be able to remove this function and simply call the instrumentClipView.scrollVertical
//I'm not doing anything with it
//But this function could come in handy in the future if I implement vertical zooming

ActionResult AutomationInstrumentClipView::scrollVertical(int32_t scrollAmount, bool inCardRoutine,
                                                          bool draggingNoteRow) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	int32_t noteRowToShiftI;
	int32_t noteRowToSwapWithI;

	bool isKit = instrument->type == InstrumentType::KIT;

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

	if (inCardRoutine && (instrumentClipView.numEditPadPresses || draggingNoteRow)) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	bool currentClipIsActive = currentSong->isClipActive(clip);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Switch off any auditioned notes. But leave on the one whose NoteRow we're moving, if we are
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] != 255
		    && (!draggingNoteRow || instrumentClipView.lastAuditionedYDisplay != yDisplay)) {
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

	// Switch on any auditioned notes - remembering that the one we're shifting (if we are) was left on before
	bool drawnNoteCodeYet = false;
	bool forceStoppedAnyAuditioning = false;
	bool changedActiveModControllable = false;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] != 255) {
			// If shifting a NoteRow..
			if (draggingNoteRow && instrumentClipView.lastAuditionedYDisplay == yDisplay) {}

			// Otherwise, switch its audition back on
			else {
				// Check NoteRow exists, incase we've got a Kit
				ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

				if (!isKit || modelStackWithNoteRow->getNoteRowAllowNull()) {

					if (modelStackWithNoteRow->getNoteRowAllowNull()
					    && modelStackWithNoteRow->getNoteRow()->soundingStatus == STATUS_SEQUENCED_NOTE) {}
					else {

						// Record note-on if we're recording
						if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {

							// If no NoteRow existed before, try creating one
							if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
								modelStackWithNoteRow =
								    instrumentClipView.createNoteRowForYDisplay(modelStack, yDisplay);
							}

							if (modelStackWithNoteRow->getNoteRowAllowNull()) {
								clip->recordNoteOn(modelStackWithNoteRow, instrument->defaultVelocity);
							}
						}

						// Should this technically grab the note-length of the note if there is one?
						instrumentClipView.sendAuditionNote(
						    true, yDisplay, instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay], 0);
					}
				}
				else {
					instrumentClipView.auditionPadIsPressed[yDisplay] = false;
					instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;
					forceStoppedAnyAuditioning = true;
				}
			}
			// If we're shiftingNoteRow, no need to re-draw the noteCode, because it'll be the same
			if (!draggingNoteRow && !drawnNoteCodeYet && instrumentClipView.auditionPadIsPressed[yDisplay]) {
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

				if (instrument->type == InstrumentType::SYNTH) {
					if (getCurrentUI() == &soundEditor
					    && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
						menu_item::multiRangeMenu.noteOnToChangeRange(clip->getYNoteFromYDisplay(yDisplay, currentSong)
						                                              + ((SoundInstrument*)instrument)->transpose);
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

//mod encoder action

//used to change the value of a step when you press and hold a pad on the timeline
//used to record live automations in
void AutomationInstrumentClipView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {

	encoderAction = true;

	instrumentClipView.dontDeleteNotesOnDepress();

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	//if user holding a node down, we'll adjust the value of the selected parameter being automated
	if ((currentUIMode == UI_MODE_NOTES_PRESSED) || padSelectionOn) {

		if (clip->lastSelectedParamID != kNoLastSelectedParamID
		    && ((instrumentClipView.numEditPadPresses > 0
		         && ((int32_t)(instrumentClipView.timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0))
		        || padSelectionOn)) {

			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				int32_t xDisplay = 0;

				//for a multi pad press, adjust value of first or last pad depending on mod encoder turned
				if (multiPadPressSelected) {
					if (whichModEncoder == 0) {
						xDisplay = leftPadSelectedX;
					}
					else if (whichModEncoder == 1) {
						xDisplay = rightPadSelectedX;
					}
				}

				//if not multi pad press, but in pad selection mode, then just adjust the single selected pad
				else if (padSelectionOn) {
					xDisplay = leftPadSelectedX;
				}

				//otherwise if not in pad selection mode, adjust the value of the pad currently being held
				else {
					// find pads that are currently pressed
					int32_t i;
					for (i = 0; i < kEditPadPressBufferSize; i++) {
						if (instrumentClipView.editPadPresses[i].isActive) {
							xDisplay = instrumentClipView.editPadPresses[i].xDisplay;
						}
					}
				}

				int32_t effectiveLength = 0;

				if (instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()) {
					ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

					effectiveLength = modelStackWithNoteRow->getLoopLength();
				}
				else {
					effectiveLength = clip->loopLength;
				}

				uint32_t squareStart = 0;

				//for the second pad pressed in a long press, the square start position is set to the very last nodes position
				if (multiPadPressSelected && (whichModEncoder == 1)) {

					int32_t squareRightEdge = getPosFromSquare(xDisplay + 1);
					squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
				}
				else {
					squareStart = getPosFromSquare(xDisplay);
				}

				if (squareStart < effectiveLength) {

					int32_t knobPos = getParameterKnobPos(modelStackWithParam, squareStart);

					int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

					//use default interpolation settings
					initInterpolation();

					setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, xDisplay,
					                            effectiveLength);

					//once first or last pad in a multi pad press is adjusted, re-render calculate multi pad press based on revised start/ending values

					if (multiPadPressSelected) {

						handleMultiPadPress(modelStack, clip, leftPadSelectedX, 0, rightPadSelectedX, 0, true);

						indicator_leds::setKnobIndicatorLevel(whichModEncoder, newKnobPos + kKnobPosOffset);

						if (modelStackWithParam->getTimelineCounter()
							== view.activeModControllableModelStack.getTimelineCounterAllowNull()) {	

							view.activeModControllableModelStack.paramManager->toForTimeline()->grabValuesFromPos(
							squareStart, &view.activeModControllableModelStack);
						}

						return;
					}
				}
			}
		}

		else {
			goto followOnAction;
		}
	}

	else { //if playback is enabled and you are recording, you will be able to record in live automations for the selected parameter

		if (clip->lastSelectedParamID != kNoLastSelectedParamID) {

			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			ModelStackWithAutoParam* modelStackWithParam =
			    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				if (modelStackWithParam->getTimelineCounter()
				    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

					int32_t knobPos = getParameterKnobPos(modelStackWithParam, view.modPos);

					int32_t newKnobPos = calculateKnobPosForModEncoderTurn(knobPos, offset);

					int32_t newValue =
					    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

					//use default interpolation settings
					initInterpolation();

					modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam,
					                                                          view.modPos, view.modLength);

					modelStack->getTimelineCounter()->instrumentBeenEdited();

					displayParameterValue(newKnobPos + kKnobPosOffset);
					indicator_leds::setKnobIndicatorLevel(0, newKnobPos + kKnobPosOffset);
					indicator_leds::setKnobIndicatorLevel(1, newKnobPos + kKnobPosOffset);
				}
			}
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

//used to copy paste automation or to delete automation of the current selected parameter
void AutomationInstrumentClipView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If they want to copy or paste automation...
	if (Buttons::isButtonPressed(hid::button::LEARN)) {
		if (on && instrument->type != InstrumentType::CV) {
			if (Buttons::isShiftButtonPressed()) {
				//paste within Automation Editor
				if (clip->lastSelectedParamID != kNoLastSelectedParamID) {
					pasteAutomation();
				}
				//paste on Automation Overview
				else {
					instrumentClipView.pasteAutomation(whichModEncoder);
				}
			}
			else {
				//copy within Automation Editor
				if (clip->lastSelectedParamID != kNoLastSelectedParamID) {
					copyAutomation();
				}
				//copy on Automation Overview
				else {
					instrumentClipView.copyAutomation(whichModEncoder);
				}
			}
		}
	}

	//delete automation of current parameter selected
	else if (Buttons::isShiftButtonPressed() && clip->lastSelectedParamID != kNoLastSelectedParamID) {

		ModelStackWithAutoParam* modelStackWithParam =
		    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

		if (modelStackWithParam && modelStackWithParam->autoParam) {
			Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_DELETE, false);
			modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));
			setDisplayParameterNameTimer();
		}
	}

	//de-select multi pad press
	else if (clip->lastSelectedParamID != kNoLastSelectedParamID) {
		if (on) {
			if (padSelectionOn) {

				display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_OFF));

				initPadSelection();
				displayAutomation();
			}
			else {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_ON));

				padSelectionOn = true;
				multiPadPressSelected = false;
				multiPadPressActive = false;

				//display only left cursor initially
				leftPadSelectedX = 0;
				rightPadSelectedX = kNoLastSelectedPad;
			}
		}
	}

	uiNeedsRendering(this);
	setDisplayParameterNameTimer();
	return;

followOnAction: //it will come here when you are on the automation overview iscreen

	view.modEncoderButtonAction(whichModEncoder, on);
	uiNeedsRendering(this);
	setDisplayParameterNameTimer();
}

void AutomationInstrumentClipView::copyAutomation() {
	if (copiedParamAutomation.nodes) {
		GeneralMemoryAllocator::get().dealloc(copiedParamAutomation.nodes);
		copiedParamAutomation.nodes = NULL;
		copiedParamAutomation.numNodes = 0;
	}

	int32_t startPos = getPosFromSquare(0);
	int32_t endPos = getPosFromSquare(kDisplayWidth);
	if (startPos == endPos) {
		return;
	}

	InstrumentClip* clip = getCurrentClip();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];

	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	ModelStackWithAutoParam* modelStackWithParam =
	    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

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
			setDisplayParameterNameTimer();
			return;
		}
	}

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_AUTOMATION_TO_COPY));
	setDisplayParameterNameTimer();
}

void AutomationInstrumentClipView::pasteAutomation() {
	if (!copiedParamAutomation.nodes) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_AUTOMATION_TO_PASTE));
		setDisplayParameterNameTimer();
		return;
	}

	int32_t startPos = getPosFromSquare(0);
	int32_t endPos = getPosFromSquare(kDisplayWidth);

	int32_t pastedAutomationWidth = endPos - startPos;
	if (pastedAutomationWidth == 0) {
		return;
	}

	float scaleFactor = (float)pastedAutomationWidth / copiedParamAutomation.width;

	InstrumentClip* clip = getCurrentClip();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];

	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	ModelStackWithAutoParam* modelStackWithParam =
	    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_PASTE, false);

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
		setDisplayParameterNameTimer();

		if (playbackHandler.isEitherClockActive()) {
			currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
		}

		return;
	}

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_PASTE_AUTOMATION));
	setDisplayParameterNameTimer();
}

//select encoder action

//used to change the parameter selection and reset shortcut pad settings so that new pad can be blinked
//once parameter is selected
void AutomationInstrumentClipView::selectEncoderAction(int8_t offset) {

	//change midi CC or param ID
	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	if (instrument->type == InstrumentType::SYNTH || instrument->type == InstrumentType::KIT) {

		//if you're a kit with affect entire enabled
		if (instrument->type == InstrumentType::KIT && instrumentClipView.getAffectEntire()) {

			//if you haven't selected a parameter yet, start at the beginning of the list
			if (clip->lastSelectedParamID == kNoLastSelectedParamID) {
				auto idx = 0;
				auto [kind, id] = kitAffectEntireParamsForAutomation[idx];
				clip->lastSelectedParamID = id;
				clip->lastSelectedParamKind = kind;
				clip->lastSelectedParamArrayPosition = idx;
			}
			//if you are scrolling left and are at the beginning of the list, go to the end of the list
			else if ((clip->lastSelectedParamArrayPosition + offset) < 0) {
				auto idx = kNumKitAffectEntireParamsForAutomation - 1;
				auto [kind, id] = kitAffectEntireParamsForAutomation[idx];
				clip->lastSelectedParamID = id;
				clip->lastSelectedParamKind = kind;
				clip->lastSelectedParamArrayPosition = idx;
			}
			//if you are scrolling right and are at the end of the list, go to the beginning of the list
			else if ((clip->lastSelectedParamArrayPosition + offset) > (kNumKitAffectEntireParamsForAutomation - 1)) {
				auto idx = 0;
				auto [kind, id] = kitAffectEntireParamsForAutomation[idx];
				clip->lastSelectedParamID = id;
				clip->lastSelectedParamKind = kind;
				clip->lastSelectedParamArrayPosition = idx;
			}
			//otherwise scrolling left/right within the list
			else {
				auto idx = clip->lastSelectedParamArrayPosition + offset;
				auto [kind, id] = kitAffectEntireParamsForAutomation[idx];
				clip->lastSelectedParamID = id;
				clip->lastSelectedParamKind = kind;
				clip->lastSelectedParamArrayPosition = idx;
			}
		}

		//if you're a synth or a kit (with affect entire off and a drum selected)
		else if (instrument->type == InstrumentType::SYNTH
		         || (instrument->type == InstrumentType::KIT && ((Kit*)instrument)->selectedDrum)) {

			//if you haven't selected a parameter yet, start at the beginning of the list
			if (clip->lastSelectedParamID == kNoLastSelectedParamID) {
				auto idx = 0;
				auto [kind, id] = nonKitAffectEntireParamsForAutomation[idx];
				clip->lastSelectedParamID = id;
				clip->lastSelectedParamKind = kind;
				clip->lastSelectedParamArrayPosition = idx;
			}
			//if you are scrolling left and are at the beginning of the list, go to the end of the list
			else if ((clip->lastSelectedParamArrayPosition + offset) < 0) {
				auto idx = kNumNonKitAffectEntireParamsForAutomation - 1;
				auto [kind, id] = nonKitAffectEntireParamsForAutomation[idx];
				clip->lastSelectedParamID = id;
				clip->lastSelectedParamKind = kind;
				clip->lastSelectedParamArrayPosition = idx;
			}
			//if you are scrolling right and are at the end of the list, go to the beginning of the list
			else if ((clip->lastSelectedParamArrayPosition + offset)
			         > (kNumNonKitAffectEntireParamsForAutomation - 1)) {
				auto idx = 0;
				auto [kind, id] = nonKitAffectEntireParamsForAutomation[idx];
				clip->lastSelectedParamID = id;
				clip->lastSelectedParamKind = kind;
				clip->lastSelectedParamArrayPosition = idx;
			}
			//otherwise scrolling left/right within the list
			else {
				auto idx = clip->lastSelectedParamArrayPosition + offset;
				auto [kind, id] = nonKitAffectEntireParamsForAutomation[idx];
				clip->lastSelectedParamID = id;
				clip->lastSelectedParamKind = kind;
				clip->lastSelectedParamArrayPosition = idx;
			}
		}

		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {

				if ((clip->lastSelectedParamKind == Param::Kind::PATCHED
				     && patchedParamShortcutsForAutomation[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == Param::Kind::UNPATCHED
				        && unpatchedParamShortcutsForAutomation[x][y] == clip->lastSelectedParamID)
				    || (clip->lastSelectedParamKind == Param::Kind::GLOBAL_EFFECTABLE
				        && globalEffectableParamShortcutsForAutomation[x][y] == clip->lastSelectedParamID)) {
					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;

					goto flashShortcut;
				}
			}
		}
	}

	else if (instrument->type == InstrumentType::MIDI_OUT) {

		if (clip->lastSelectedParamID == kNoLastSelectedParamID) {
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

		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (midiCCShortcutsForAutomation[x][y] == clip->lastSelectedParamID) {

					clip->lastSelectedParamShortcutX = x;
					clip->lastSelectedParamShortcutY = y;

					goto flashShortcut;
				}
			}
		}

		goto flashShortcut;
	}

	return;

flashShortcut:

	lastPadSelectedKnobPos = kNoLastSelectedPad;
	displayParameterName(clip->lastSelectedParamID);
	displayAutomation();
	resetShortcutBlinking();
	uiNeedsRendering(this);
}

//tempo encoder action
void AutomationInstrumentClipView::tempoEncoderAction(int8_t offset, bool encoderButtonPressed,
                                                      bool shiftButtonPressed) {

	playbackHandler.tempoEncoderAction(offset, encoderButtonPressed, shiftButtonPressed);
}

//called by melodic_instrument.cpp or kit.cpp
void AutomationInstrumentClipView::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {

	instrumentClipView.noteRowChanged(clip, noteRow);
}

//called by playback_handler.cpp
void AutomationInstrumentClipView::notifyPlaybackBegun() {
	instrumentClipView.reassessAllAuditionStatus();
}

//resets the Parameter Selection which sends you back to the Automation Overview screen
//these values are saved on a clip basis
void AutomationInstrumentClipView::initParameterSelection() {
	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	initPadSelection();

	clip->lastSelectedParamID = kNoLastSelectedParamID;
	clip->lastSelectedParamKind = Param::Kind::NONE;
	clip->lastSelectedParamShortcutX = kNoLastSelectedParamShortcut;
	clip->lastSelectedParamShortcutY = kNoLastSelectedParamShortcut;
	clip->lastSelectedParamArrayPosition = 0;

	display->cancelPopup();

	//if we're going back to the Automation Overview, set the display to show Midi Channel again (7seg only)
	if (instrument->type == InstrumentType::MIDI_OUT) {

		if (display->have7SEG()) {
			if (((MIDIInstrument*)instrument)->channel < 16) {
				display->setTextAsSlot(((MIDIInstrument*)instrument)->channel + 1,
				                       ((MIDIInstrument*)instrument)->channelSuffix, false, false);
			}
			else {
				char const* text =
				    (((MIDIInstrument*)instrument)->channel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "Lower" : "Upper";
				display->setText(text, false, 255, false);
			}
		}
	}
}

void AutomationInstrumentClipView::initPadSelection() {

	padSelectionOn = false;
	multiPadPressSelected = false;
	multiPadPressActive = false;
	leftPadSelectedX = kNoLastSelectedPad;
	rightPadSelectedX = kNoLastSelectedPad;
	lastPadSelectedKnobPos = kNoLastSelectedPad;
}

void AutomationInstrumentClipView::initInterpolation() {

	automationInstrumentClipView.interpolationBefore = false;
	automationInstrumentClipView.interpolationAfter = false;
}

//get's the modelstack for the parameters that are being edited
//the model stack differs for SYNTH's, KIT's, MIDI clip's
ModelStackWithAutoParam* AutomationInstrumentClipView::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack,
                                                                              InstrumentClip* clip, int32_t paramID,
                                                                              Param::Kind paramKind) {

	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	Instrument* instrument = (Instrument*)clip->output;

	if (instrument->type == InstrumentType::SYNTH) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(instrument->toModControllable(), &clip->paramManager);

		if (modelStackWithThreeMainThings) {

			ParamCollectionSummary* summary = nullptr;

			if (paramKind == Param::Kind::PATCHED) {
				summary = modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();
			}

			else if (paramKind == Param::Kind::UNPATCHED) {
				summary = modelStackWithThreeMainThings->paramManager->getUnpatchedParamSetSummary();
			}

			if (summary) {
				ParamSet* paramSet = (ParamSet*)summary->paramCollection;
				modelStackWithParam =
				    modelStackWithThreeMainThings->addParam(paramSet, summary, paramID, &paramSet->params[paramID]);
			}
		}
	}

	else if (instrument->type == InstrumentType::KIT) {
		//for a kit we have two types of automation: with Affect Entire and without Affect Entire
		//for a kit with affect entire off, we are automating information at the noterow level
		if (!instrumentClipView.getAffectEntire()) {

			Drum* drum = ((Kit*)instrument)->selectedDrum;

			if (drum) {
				if (drum->type == DrumType::SOUND) { //no automation for MIDI or CV kit drum types

					ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

					if (modelStackWithNoteRow) {

						ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
						    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();

						if (modelStackWithThreeMainThings) {

							ParamCollectionSummary* summary = nullptr;

							if (paramKind == Param::Kind::PATCHED) {
								summary = modelStackWithThreeMainThings->paramManager->getPatchedParamSetSummary();
							}

							else if (paramKind == Param::Kind::UNPATCHED) {
								summary = modelStackWithThreeMainThings->paramManager->getUnpatchedParamSetSummary();
							}

							if (summary) {
								ParamSet* paramSet = (ParamSet*)summary->paramCollection;
								modelStackWithParam = modelStackWithThreeMainThings->addParam(
								    paramSet, summary, paramID, &paramSet->params[paramID]);
							}
						}
					}
				}
			}
		}

		else { //model stack for automating kit params when "affect entire" is enabled

			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(instrument->toModControllable(), &clip->paramManager);

			if (modelStackWithThreeMainThings) {

				ParamCollectionSummary* summary = nullptr;

				summary = modelStackWithThreeMainThings->paramManager->getUnpatchedParamSetSummary();

				if (summary) {
					ParamSet* paramSet = (ParamSet*)summary->paramCollection;
					modelStackWithParam =
					    modelStackWithThreeMainThings->addParam(paramSet, summary, paramID, &paramSet->params[paramID]);
				}
			}
		}
	}

	else if (instrument->type == InstrumentType::MIDI_OUT) {

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(instrument->toModControllable(), &clip->paramManager);

		if (modelStackWithThreeMainThings) {

			MIDIInstrument* instrument = (MIDIInstrument*)instrument;

			modelStackWithParam =
			    instrument->getParamToControlFromInputMIDIChannel(paramID, modelStackWithThreeMainThings);
		}
	}

	return modelStackWithParam;
}

//this function obtains a parameters value and converts it to a knobPos
//the knobPos is used for rendering the current parameter values in the automation editor
//it's also used for obtaining the start and end position values for a multi pad press
//and also used for increasing/decreasing parameter values with the mod encoders

int32_t AutomationInstrumentClipView::getParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t squareStart) {
	//obtain value corresponding to the two pads that were pressed in a multi pad press action
	int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareStart, modelStack);
	int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(currentValue, modelStack);

	return knobPos;
}

//this function is based off the code in AutoParam::getValueAtPos, it was tweaked to just return interpolation status
//of the left node or right node (depending on the reversed parameter which is used to indicate what node in what direction
//we are looking for (e.g. we want status of left node, or right node, relative to the current pos we are looking at
bool AutomationInstrumentClipView::getNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos,
                                                        bool reversed) {

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

//this function writes the new values calculated by the handleSinglePadPress and handleMultiPadPress functions
void AutomationInstrumentClipView::setParameterAutomationValue(ModelStackWithAutoParam* modelStack, int32_t knobPos,
                                                               int32_t squareStart, int32_t xDisplay,
                                                               int32_t effectiveLength, bool displayValue) {

	int32_t newValue = modelStack->paramCollection->knobPosToParamValue(knobPos, modelStack);

	uint32_t squareWidth = 0;

	//for a multi pad press, the beginning and ending pad presses are set with a square width of 3 (1 node).
	if (multiPadPressSelected) {
		squareWidth = kParamNodeWidth;
	}
	else {
		squareWidth = instrumentClipView.getSquareWidth(xDisplay, effectiveLength);
	}

	//if you're doing a single pad press, you don't want the values around that single press position to change
	//they will change if those nodes around the single pad press were created with interpolation turned on
	//to fix this, re-create those nodes with their current value with interpolation off

	interpolationBefore = getNodeInterpolation(modelStack, squareStart, true);
	interpolationAfter = getNodeInterpolation(modelStack, squareStart, false);

	//create a node to the left with the current interpolation status
	if ((squareStart - kParamNodeWidth) >= 0) {
		int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareStart - kParamNodeWidth, modelStack);
		modelStack->autoParam->setValuePossiblyForRegion(currentValue, modelStack, squareStart - kParamNodeWidth,
		                                                 kParamNodeWidth);
	}

	//create a node to the right with the current interpolation status
	int32_t squareRightEdge = squareStart + squareWidth;
	if (squareRightEdge < effectiveLength) {
		int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareRightEdge, modelStack);
		modelStack->autoParam->setValuePossiblyForRegion(currentValue, modelStack, squareRightEdge, kParamNodeWidth);
	}

	//reset interpolation to false for the single pad we're changing (so that the nodes around it don't also change)
	initInterpolation();

	//called twice because there was a weird bug where for some reason the first call wasn't taking effect
	//on one pad (and whatever pad it was changed every time)...super weird...calling twice fixed it...
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);

	modelStack->getTimelineCounter()->instrumentBeenEdited();

	//in a multi pad press, no need to display all the values calculated
	if (displayValue) {
		displayParameterValue(knobPos + kKnobPosOffset);
	}
}

//takes care of setting the automation value for the single pad that was pressed
void AutomationInstrumentClipView::handleSinglePadPress(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip,
                                                        int32_t xDisplay, int32_t yDisplay, bool shortcutPress) {

	Instrument* instrument = (Instrument*)clip->output;

	if ((shortcutPress || clip->lastSelectedParamID == kNoLastSelectedParamID)
	    && (!(instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()
	          && !((Kit*)instrument)->selectedDrum)
	        || (instrument->type == InstrumentType::KIT
	            && instrumentClipView.getAffectEntire()))) { //this means you are selecting a parameter

		if ((instrument->type == InstrumentType::SYNTH
		     || (instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()))
		    && ((patchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF)
		        || (unpatchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF))) {

			if (patchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
				clip->lastSelectedParamKind = Param::Kind::PATCHED;
				//if you are in a synth or a kit clip and the shortcut is valid, set current selected ParamID
				clip->lastSelectedParamID = patchedParamShortcutsForAutomation[xDisplay][yDisplay];
			}

			else if (unpatchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
				clip->lastSelectedParamKind = Param::Kind::UNPATCHED;
				//if you are in a synth or a kit clip and the shortcut is valid, set current selected ParamID
				clip->lastSelectedParamID = unpatchedParamShortcutsForAutomation[xDisplay][yDisplay];
			}

			for (auto idx = 0; idx < kNumNonKitAffectEntireParamsForAutomation; idx++) {

				auto [kind, id] = nonKitAffectEntireParamsForAutomation[idx];

				if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
					clip->lastSelectedParamArrayPosition = idx;
					break;
				}
			}
		}

		else if (instrument->type == InstrumentType::KIT && instrumentClipView.getAffectEntire()
		         && ((unpatchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF)
		             || (globalEffectableParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF))) {

			if (unpatchedParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
				clip->lastSelectedParamKind = Param::Kind::UNPATCHED;
				//if you are in a kit clip with affect entire enabled and the shortcut is valid, set current selected ParamID
				clip->lastSelectedParamID = unpatchedParamShortcutsForAutomation[xDisplay][yDisplay];
			}

			else if (globalEffectableParamShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {
				clip->lastSelectedParamKind = Param::Kind::GLOBAL_EFFECTABLE;
				//if you are in a kit clip with affect entire enabled and the shortcut is valid, set current selected ParamID
				clip->lastSelectedParamID = globalEffectableParamShortcutsForAutomation[xDisplay][yDisplay];
			}

			for (auto idx = 0; idx < kNumKitAffectEntireParamsForAutomation; idx++) {

				auto [kind, id] = kitAffectEntireParamsForAutomation[idx];

				if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
					clip->lastSelectedParamArrayPosition = idx;
					break;
				}
			}
		}

		else if (instrument->type == InstrumentType::MIDI_OUT
		         && midiCCShortcutsForAutomation[xDisplay][yDisplay] != 0xFFFFFFFF) {

			//if you are in a midi clip and the shortcut is valid, set the current selected ParamID
			clip->lastSelectedParamID = midiCCShortcutsForAutomation[xDisplay][yDisplay];
		}

		else {
			return;
		}

		//save the selected parameter ID's shortcut pad x,y coords so that you can setup the shortcut blink
		clip->lastSelectedParamShortcutX = xDisplay;
		clip->lastSelectedParamShortcutY = yDisplay;

		displayParameterName(clip->lastSelectedParamID);
		displayAutomation();
		resetShortcutBlinking();
	}

	else if (clip->lastSelectedParamID != kNoLastSelectedParamID) { //this means you are editing a parameter's value

		ModelStackWithAutoParam* modelStackWithParam =
		    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

		if (padSelectionOn) {
			//display pad's middle value

			int32_t effectiveLength;

			if (instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()) {
				ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

				effectiveLength = modelStackWithNoteRow->getLoopLength();
			}
			else {
				effectiveLength = clip->loopLength;
			}

			uint32_t squareStart = getPosFromSquare(xDisplay);

			if (!multiPadPressSelected) {
				uint32_t squareWidth = instrumentClipView.getSquareWidth(xDisplay, effectiveLength);
				if (squareWidth != 3) {
					squareStart = squareStart + (squareWidth / 2);
				}
			}

			int32_t knobPos = getParameterKnobPos(modelStackWithParam, squareStart);
			displayParameterValue(knobPos + kKnobPosOffset);

			if (modelStackWithParam && modelStackWithParam->autoParam) {

				if (modelStackWithParam->getTimelineCounter()
				    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {	

					view.activeModControllableModelStack.paramManager->toForTimeline()->grabValuesFromPos(
		    		squareStart, &view.activeModControllableModelStack);

				}
			}

			if (!multiPadPressSelected) {
				leftPadSelectedX = xDisplay;
			}
		}

		else if (modelStackWithParam && modelStackWithParam->autoParam) {

			uint32_t squareStart = getPosFromSquare(xDisplay);

			int32_t effectiveLength;

			if (instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()) {
				ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

				effectiveLength = modelStackWithNoteRow->getLoopLength();
			}
			else {
				effectiveLength = clip->loopLength;
			}

			if (squareStart < effectiveLength) {

				//use default interpolation settings
				initInterpolation();

				int32_t newKnobPos = calculateKnobPosForSinglePadPress(yDisplay);
				setParameterAutomationValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength);
			}
		}
	}

	uiNeedsRendering(this);
}

//calculates what the new parameter value is when you press a single pad
int32_t AutomationInstrumentClipView::calculateKnobPosForSinglePadPress(int32_t yDisplay) {

	int32_t newKnobPos = 0;

	//if you press bottom pad, value is 0, for all other pads except for the top pad, value = row Y * 18
	if (yDisplay < 7) {
		newKnobPos = yDisplay * kParamValueIncrementForAutomationSinglePadPress;
	}
	//if you are pressing the top pad, set the value to max (128)
	else {
		newKnobPos = kMaxKnobPos;
	}

	//in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

//takes care of setting the automation values for the two pads pressed and the pads in between
void AutomationInstrumentClipView::handleMultiPadPress(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip,
                                                       int32_t firstPadX, int32_t firstPadY, int32_t secondPadX,
                                                       int32_t secondPadY, bool modEncoderAction) {

	Instrument* instrument = (Instrument*)clip->output;

	if (modelStack) {

		ModelStackWithAutoParam* modelStackWithParam =
		    getModelStackWithParam(modelStack, clip, clip->lastSelectedParamID, clip->lastSelectedParamKind);

		if (modelStackWithParam && modelStackWithParam->autoParam) {

			int32_t effectiveLength = 0;

			if (instrument->type == InstrumentType::KIT && !instrumentClipView.getAffectEntire()) {
				ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowForSelectedDrum(modelStack);

				effectiveLength = modelStackWithNoteRow->getLoopLength();
			}
			else {
				effectiveLength = clip->loopLength;
			}

			int32_t firstPadValue = 0;
			int32_t secondPadValue = 0;

			//if we're updating the long press values via mod encoder action, then get current values of pads pressed and re-interpolate
			if (modEncoderAction) {
				firstPadValue = getParameterKnobPos(modelStackWithParam, getPosFromSquare(firstPadX)) + kKnobPosOffset;

				int32_t squareRightEdge = getPosFromSquare(secondPadX + 1);
				uint32_t squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;

				secondPadValue = getParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;
			}

			//otherwise if it's a regular long press, calculate values from the y position of the pads pressed
			else {
				firstPadValue = calculateKnobPosForSinglePadPress(firstPadY) + kKnobPosOffset;
				secondPadValue = calculateKnobPosForSinglePadPress(secondPadY) + kKnobPosOffset;
			}

			//converting variables to float for more accurate interpolation calculation
			float firstPadValueFloat = static_cast<float>(firstPadValue);
			float firstPadXFloat = static_cast<float>(getPosFromSquare(firstPadX));
			float secondPadValueFloat = static_cast<float>(secondPadValue);
			float secondPadXFloat = static_cast<float>(getPosFromSquare(secondPadX + 1) - kParamNodeWidth);

			//clear existing nodes from long press range
			int32_t squareRightEdge = getPosFromSquare(secondPadX + 1);
			//int32_t clearLength = std::min(effectiveLength, squareRightEdge) - getPosFromSquare(firstPadX);

			//reset interpolation settings to default
			initInterpolation();

			//set value for beginning pad press at the very first node position within that pad

			uint32_t squareStart = getPosFromSquare(firstPadX);
			setParameterAutomationValue(modelStackWithParam, firstPadValue - kKnobPosOffset, squareStart, firstPadX,
			                            effectiveLength, false);

			//set value for ending pad press at the very last node position within that pad
			squareStart = std::min(effectiveLength, squareRightEdge - kParamNodeWidth);
			setParameterAutomationValue(modelStackWithParam, secondPadValue - kKnobPosOffset, squareStart, secondPadX,
			                            effectiveLength, false);

			//loop from first pad to last pad, setting values for nodes in between
			//these values will serve as "key frames" for the interpolation to flow through
			for (int32_t x = firstPadX; x <= secondPadX; x++) {

				int32_t newKnobPos = 0;
				uint32_t squareWidth = 0;

				//we've already set the value for the very first node corresponding to the first pad above
				//now we will set the value for the remaining nodes within the first pad
				if (x == firstPadX) {
					squareStart = getPosFromSquare(x) + kParamNodeWidth;
					squareWidth = instrumentClipView.getSquareWidth(x, effectiveLength) - kParamNodeWidth;
				}

				//we've already set the value for the very last node corresponding to the second pad above
				//now we will set the value for the remaining nodes within the second pad
				else if (x == secondPadX) {
					squareStart = getPosFromSquare(x);
					squareWidth = instrumentClipView.getSquareWidth(x, effectiveLength) - kParamNodeWidth;
				}

				//now we will set the values for the nodes between the first and second pad's pressed
				else {
					squareStart = getPosFromSquare(x);
					squareWidth = instrumentClipView.getSquareWidth(x, effectiveLength);
				}

				//linear interpolation formula to calculate the value of the pads
				//f(x) = A + (x - Ax) * ((B - A) / (Bx - Ax))
				float newKnobPosFloat = std::round(firstPadValueFloat
				                                   + (((squareStart - firstPadXFloat) / kParamNodeWidth)
				                                      * ((secondPadValueFloat - firstPadValueFloat)
				                                         / ((secondPadXFloat - firstPadXFloat) / kParamNodeWidth))));

				newKnobPos = static_cast<int32_t>(newKnobPosFloat);
				newKnobPos = newKnobPos - kKnobPosOffset;

				//if interpolation is off, values for nodes in between first and second pad will not be set in a staggered/step fashion
				if (interpolation) {
					interpolationBefore = true;
					interpolationAfter = true;
				}

				//set value for pads in between
				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);
				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
				                                                          squareWidth);
				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
				                                                          squareWidth);

				modelStackWithParam->getTimelineCounter()->instrumentBeenEdited();
			}

			//reset interpolation settings to off
			initInterpolation();

			//render the multi pad press
			uiNeedsRendering(this);
		}
	}
}

//used to calculate new knobPos when you turn the mod encoders (gold knobs)
int32_t AutomationInstrumentClipView::calculateKnobPosForModEncoderTurn(int32_t knobPos, int32_t offset) {

	//adjust the current knob so that it is within the range of 0-128 for calculation purposes
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

	//in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

//used to disable certain actions on the automation overview screen
//e.g. doubling clip length, editing clip length
bool AutomationInstrumentClipView::isOnParameterGridMenuView() {

	InstrumentClip* clip = getCurrentClip();

	if (clip->lastSelectedParamID == kNoLastSelectedParamID) {
		return true;
	}
	return false;
}

//displays patched param names or midi cc names
void AutomationInstrumentClipView::displayParameterName(int32_t paramID) {

	if ((currentUIMode == UI_MODE_NOTES_PRESSED) && (lastPadSelectedKnobPos != kNoLastSelectedPad)) {

		displayParameterValue(lastPadSelectedKnobPos);

		return;
	}

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithAutoParam* modelStackWithParam =
	    getModelStackWithParam(modelStack, clip, paramID, clip->lastSelectedParamKind);
	bool isAutomated = false;

	//check if Parameter is currently automated so that the automation status can be drawn on the screen with the Parameter Name
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		if (modelStackWithParam->autoParam->isAutomated()) {
			isAutomated = true;
		}
	}

	if (instrument->type == InstrumentType::SYNTH || instrument->type == InstrumentType::KIT) {

		char buffer[30];

		//drawing Parameter Names on 7SEG isn't legible and not done currently, so won't do it here either
		if (display->haveOLED()) {

			if (clip->lastSelectedParamKind == Param::Kind::PATCHED) {
				strncpy(buffer, getPatchedParamDisplayNameForOLED(paramID), 29);
			}
			else if (clip->lastSelectedParamKind == Param::Kind::UNPATCHED) {
				strncpy(buffer, getUnpatchedParamDisplayNameForOLED(paramID), 29);
			}
			else if (clip->lastSelectedParamKind == Param::Kind::GLOBAL_EFFECTABLE) {
				strncpy(buffer, getGlobalEffectableParamDisplayNameForOLED(paramID), 29);
			}

			if (isAutomated) {
				strncat(buffer, "\n(automated)", 29);
			}

			display->popupText(buffer);
		}
		else {
			redrawNumericDisplay();
		}
	}

	else if (instrument->type == InstrumentType::MIDI_OUT) {

		InstrumentClipMinder::drawMIDIControlNumber(paramID, isAutomated);
	}
}

//display parameter value when it is changed
void AutomationInstrumentClipView::displayParameterValue(int32_t knobPos) {

	lastPadSelectedKnobPos = knobPos;

	char buffer[5];

	intToString(knobPos, buffer);
	if (currentUIMode == UI_MODE_NOTES_PRESSED) {
		if (display->haveOLED()) {
			display->popupText(buffer);
		}
		else {
			display->setText(buffer, false, 255, false);
		}
	}
	else {
		display->displayPopup(buffer);
		setDisplayParameterNameTimer();
	}

	if (padSelectionOn && !multiPadPressSelected) {
		indicator_leds::setKnobIndicatorLevel(0, knobPos);
		indicator_leds::setKnobIndicatorLevel(1, knobPos);
	}
}

void AutomationInstrumentClipView::displayCVErrorMessage() {
	display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_AUTOMATE_CV));
}

void AutomationInstrumentClipView::setDisplayParameterNameTimer() {

	InstrumentClip* clip = getCurrentClip();
	Instrument* instrument = (Instrument*)clip->output;

	//after you displayed a pop up with the parameter value, redisplay the parameter name on the screen
	if (clip->lastSelectedParamID != kNoLastSelectedParamID) {

		uiTimerManager.setTimer(TIMER_AUTOMATION_VIEW, 700);
	}
}

//created this function to undo any existing blinking so that it doesn't get rendered in my view
//also created it so that i can reset blinking when a parameter is deselected or when you enter/exit automation view
void AutomationInstrumentClipView::resetShortcutBlinking() {
	memset(soundEditor.sourceShortcutBlinkFrequencies, 255, sizeof(soundEditor.sourceShortcutBlinkFrequencies));
	uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
	shortcutBlinking = false;
}
