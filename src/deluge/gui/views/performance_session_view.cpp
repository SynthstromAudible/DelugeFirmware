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

#include "gui/views/performance_session_view.h"
#include "definitions_cxx.hpp"
#include "dsp/master_compressor/master_compressor.h"
#include "extern.h"
#include "gui/colour.h"
#include "gui/context_menu/audio_input_selector.h"
#include "gui/context_menu/launch_style.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_performance_layout_change.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/functions.h"
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

using namespace deluge;
using namespace gui;

const char* STRING_FOR_PERFORM_DEFAULTS_XML = "PerformanceView.XML";
const char* STRING_FOR_PERFORM_DEFAULTS_TAG = "defaults";
const char* STRING_FOR_PERFORM_DEFAULTS_FXVALUES_TAG = "defaultFXValues";
const char* STRING_FOR_PERFORM_DEFAULTS_PARAM_TAG = "param";
const char* STRING_FOR_PERFORM_DEFAULTS_NO_PARAM = "none";
const char* STRING_FOR_PERFORM_DEFAULTS_HOLD_TAG = "hold";
const char* STRING_FOR_PERFORM_DEFAULTS_HOLD_STATUS_TAG = "status";
const char* STRING_FOR_PERFORM_DEFAULTS_HOLD_RESETVALUE_TAG = "resetValue";
const char* STRING_FOR_PERFORM_DEFAULTS_ROW_TAG = "row";
const char* STRING_FOR_ON = "On";
const char* STRING_FOR_OFF = "Off";

//colours for the performance mode

const uint8_t rowColourRed[3] = {255, 0, 0};            //LPF Cutoff, Resonance
const uint8_t rowColourPastelOrange[3] = {221, 72, 13}; //HPF Cutoff, Resonance
const uint8_t rowColourPastelYellow[3] = {170, 182, 0}; //EQ Bass, Treble
const uint8_t rowColourPastelGreen[3] = {85, 182, 72};  //Reverb Amount
const uint8_t rowColourPastelBlue[3] = {51, 109, 145};  //Delay Amount, Rate
const uint8_t rowColourPastelPink[3] = {144, 72, 91};   //Mod FX Rate, Depth, Feedback, Offset
const uint8_t rowColourPink[3] = {128, 0, 128};         //Decimation, Bitcrush
const uint8_t rowColourBlue[3] = {0, 0, 255};           //Stutter

const uint8_t rowTailColourRed[3] = {53, 2, 2};           //LPF Cutoff, Resonance
const uint8_t rowTailColourPastelOrange[3] = {46, 16, 2}; //HPF Cutoff, Resonance
const uint8_t rowTailColourPastelYellow[3] = {36, 38, 2}; //EQ Bass, Treble
const uint8_t rowTailColourPastelGreen[3] = {19, 38, 16}; //Reverb Amount
const uint8_t rowTailColourPastelBlue[3] = {12, 23, 31};  //Delay Amount, Rate
const uint8_t rowTailColourPastelPink[3] = {37, 15, 37};  //Mod FX Rate, Depth, Feedback, Offset
const uint8_t rowTailColourPink[3] = {53, 0, 53};         //Decimation, Bitcrush
const uint8_t rowTailColourBlue[3] = {2, 2, 53};          //Stutter

//list of parameters available for assignment to FX columns in performance view

const ParamsForPerformance songParamsForPerformance[kNumParamsForPerformance] = {
    {Param::Kind::UNPATCHED_GLOBAL,                                    //paramKind
     Param::Unpatched::GlobalEffectable::LPF_FREQ,                     //paramID
     8,                                                                //xDisplay
     7,                                                                //yDisplay
     {rowColourRed[0], rowColourRed[1], rowColourRed[2]},              //rowColour[3]
     {rowTailColourRed[0], rowTailColourRed[1], rowTailColourRed[2]}}, //rowTailColour[3]
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::LPF_RES,
     8,
     6,
     {rowColourRed[0], rowColourRed[1], rowColourRed[2]},
     {rowTailColourRed[0], rowTailColourRed[1], rowTailColourRed[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::HPF_FREQ,
     9,
     7,
     {rowColourPastelOrange[0], rowColourPastelOrange[1], rowColourPastelOrange[2]},
     {rowTailColourPastelOrange[0], rowTailColourPastelOrange[1], rowTailColourPastelOrange[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::HPF_RES,
     9,
     6,
     {rowColourPastelOrange[0], rowColourPastelOrange[1], rowColourPastelOrange[2]},
     {rowTailColourPastelOrange[0], rowTailColourPastelOrange[1], rowTailColourPastelOrange[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::BASS,
     10,
     6,
     {rowColourPastelYellow[0], rowColourPastelYellow[1], rowColourPastelYellow[2]},
     {rowTailColourPastelYellow[0], rowTailColourPastelYellow[1], rowTailColourPastelYellow[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::TREBLE,
     11,
     6,
     {rowColourPastelYellow[0], rowColourPastelYellow[1], rowColourPastelYellow[2]},
     {rowTailColourPastelYellow[0], rowTailColourPastelYellow[1], rowTailColourPastelYellow[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT,
     13,
     3,
     {rowColourPastelGreen[0], rowColourPastelGreen[1], rowColourPastelGreen[2]},
     {rowTailColourPastelGreen[0], rowTailColourPastelGreen[1], rowTailColourPastelGreen[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::DELAY_AMOUNT,
     14,
     3,
     {rowColourPastelBlue[0], rowColourPastelBlue[1], rowColourPastelBlue[2]},
     {rowTailColourPastelBlue[0], rowTailColourPastelBlue[1], rowTailColourPastelBlue[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::DELAY_RATE,
     14,
     0,
     {rowColourPastelBlue[0], rowColourPastelBlue[1], rowColourPastelBlue[2]},
     {rowTailColourPastelBlue[0], rowTailColourPastelBlue[1], rowTailColourPastelBlue[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::MOD_FX_RATE,
     12,
     7,
     {rowColourPastelPink[0], rowColourPastelPink[1], rowColourPastelPink[2]},
     {rowTailColourPastelPink[0], rowTailColourPastelPink[1], rowTailColourPastelPink[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH,
     12,
     6,
     {rowColourPastelPink[0], rowColourPastelPink[1], rowColourPastelPink[2]},
     {rowTailColourPastelPink[0], rowTailColourPastelPink[1], rowTailColourPastelPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::MOD_FX_FEEDBACK,
     12,
     5,
     {rowColourPastelPink[0], rowColourPastelPink[1], rowColourPastelPink[2]},
     {rowTailColourPastelPink[0], rowTailColourPastelPink[1], rowTailColourPastelPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::MOD_FX_OFFSET,
     12,
     4,
     {rowColourPastelPink[0], rowColourPastelPink[1], rowColourPastelPink[2]},
     {rowTailColourPastelPink[0], rowTailColourPastelPink[1], rowTailColourPastelPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::SAMPLE_RATE_REDUCTION,
     6,
     5,
     {rowColourPink[0], rowColourPink[1], rowColourPink[2]},
     {rowTailColourPink[0], rowTailColourPink[1], rowTailColourPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::BITCRUSHING,
     6,
     6,
     {rowColourPink[0], rowColourPink[1], rowColourPink[2]},
     {rowTailColourPink[0], rowTailColourPink[1], rowTailColourPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::STUTTER_RATE,
     5,
     7,
     {rowColourBlue[0], rowColourBlue[1], rowColourBlue[2]},
     {rowTailColourBlue[0], rowTailColourBlue[1], rowTailColourBlue[2]}},
};

const ParamsForPerformance defaultLayoutForPerformance[kDisplayWidth] = {
    {Param::Kind::UNPATCHED_GLOBAL,                                    //paramKind
     Param::Unpatched::GlobalEffectable::LPF_FREQ,                     //paramID
     8,                                                                //xDisplay
     7,                                                                //yDisplay
     {rowColourRed[0], rowColourRed[1], rowColourRed[2]},              //rowColour[3]
     {rowTailColourRed[0], rowTailColourRed[1], rowTailColourRed[2]}}, //rowTailColour[3]
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::LPF_RES,
     8,
     6,
     {rowColourRed[0], rowColourRed[1], rowColourRed[2]},
     {rowTailColourRed[0], rowTailColourRed[1], rowTailColourRed[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::HPF_FREQ,
     9,
     7,
     {rowColourPastelOrange[0], rowColourPastelOrange[1], rowColourPastelOrange[2]},
     {rowTailColourPastelOrange[0], rowTailColourPastelOrange[1], rowTailColourPastelOrange[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::HPF_RES,
     9,
     6,
     {rowColourPastelOrange[0], rowColourPastelOrange[1], rowColourPastelOrange[2]},
     {rowTailColourPastelOrange[0], rowTailColourPastelOrange[1], rowTailColourPastelOrange[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::BASS,
     10,
     6,
     {rowColourPastelYellow[0], rowColourPastelYellow[1], rowColourPastelYellow[2]},
     {rowTailColourPastelYellow[0], rowTailColourPastelYellow[1], rowTailColourPastelYellow[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::TREBLE,
     11,
     6,
     {rowColourPastelYellow[0], rowColourPastelYellow[1], rowColourPastelYellow[2]},
     {rowTailColourPastelYellow[0], rowTailColourPastelYellow[1], rowTailColourPastelYellow[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT,
     13,
     3,
     {rowColourPastelGreen[0], rowColourPastelGreen[1], rowColourPastelGreen[2]},
     {rowTailColourPastelGreen[0], rowTailColourPastelGreen[1], rowTailColourPastelGreen[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::DELAY_AMOUNT,
     14,
     3,
     {rowColourPastelBlue[0], rowColourPastelBlue[1], rowColourPastelBlue[2]},
     {rowTailColourPastelBlue[0], rowTailColourPastelBlue[1], rowTailColourPastelBlue[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::DELAY_RATE,
     14,
     0,
     {rowColourPastelBlue[0], rowColourPastelBlue[1], rowColourPastelBlue[2]},
     {rowTailColourPastelBlue[0], rowTailColourPastelBlue[1], rowTailColourPastelBlue[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::MOD_FX_RATE,
     12,
     7,
     {rowColourPastelPink[0], rowColourPastelPink[1], rowColourPastelPink[2]},
     {rowTailColourPastelPink[0], rowTailColourPastelPink[1], rowTailColourPastelPink[2]}},
    {Param::Kind::UNPATCHED_GLOBAL,
     Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH,
     12,
     6,
     {rowColourPastelPink[0], rowColourPastelPink[1], rowColourPastelPink[2]},
     {rowTailColourPastelPink[0], rowTailColourPastelPink[1], rowTailColourPastelPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::MOD_FX_FEEDBACK,
     12,
     5,
     {rowColourPastelPink[0], rowColourPastelPink[1], rowColourPastelPink[2]},
     {rowTailColourPastelPink[0], rowTailColourPastelPink[1], rowTailColourPastelPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::MOD_FX_OFFSET,
     12,
     4,
     {rowColourPastelPink[0], rowColourPastelPink[1], rowColourPastelPink[2]},
     {rowTailColourPastelPink[0], rowTailColourPastelPink[1], rowTailColourPastelPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::SAMPLE_RATE_REDUCTION,
     6,
     5,
     {rowColourPink[0], rowColourPink[1], rowColourPink[2]},
     {rowTailColourPink[0], rowTailColourPink[1], rowTailColourPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::BITCRUSHING,
     6,
     6,
     {rowColourPink[0], rowColourPink[1], rowColourPink[2]},
     {rowTailColourPink[0], rowTailColourPink[1], rowTailColourPink[2]}},
    {Param::Kind::UNPATCHED_SOUND,
     Param::Unpatched::STUTTER_RATE,
     5,
     7,
     {rowColourBlue[0], rowColourBlue[1], rowColourBlue[2]},
     {rowTailColourBlue[0], rowTailColourBlue[1], rowTailColourBlue[2]}},
};

//mapping shortcuts to paramKind
const Param::Kind paramKindShortcutsForPerformanceView[kDisplayWidth][kDisplayHeight] = {
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::NONE, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::NONE, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::NONE, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::NONE, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::NONE, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::NONE, Param::Kind::UNPATCHED_SOUND},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::UNPATCHED_SOUND, Param::Kind::UNPATCHED_SOUND, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::NONE, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::UNPATCHED_GLOBAL, Param::Kind::UNPATCHED_GLOBAL},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::UNPATCHED_GLOBAL, Param::Kind::UNPATCHED_GLOBAL},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::UNPATCHED_SOUND, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::UNPATCHED_SOUND, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::UNPATCHED_SOUND,
     Param::Kind::UNPATCHED_SOUND, Param::Kind::UNPATCHED_GLOBAL, Param::Kind::UNPATCHED_GLOBAL},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::UNPATCHED_GLOBAL, Param::Kind::NONE,
     Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE},
    {Param::Kind::UNPATCHED_GLOBAL, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::UNPATCHED_GLOBAL,
     Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE},
    {Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE, Param::Kind::NONE,
     Param::Kind::NONE, Param::Kind::NONE},
};

//mapping shortcuts to paramID
const uint32_t paramIDShortcutsForPerformanceView[kDisplayWidth][kDisplayHeight] = {
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, kNoParamIDShortcut, Param::Unpatched::STUTTER_RATE},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     Param::Unpatched::SAMPLE_RATE_REDUCTION, Param::Unpatched::BITCRUSHING, kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, Param::Unpatched::GlobalEffectable::LPF_RES, Param::Unpatched::GlobalEffectable::LPF_FREQ},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, Param::Unpatched::GlobalEffectable::HPF_RES, Param::Unpatched::GlobalEffectable::HPF_FREQ},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, Param::Unpatched::BASS, kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, Param::Unpatched::TREBLE, kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, Param::Unpatched::MOD_FX_OFFSET,
     Param::Unpatched::MOD_FX_FEEDBACK, Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH,
     Param::Unpatched::GlobalEffectable::MOD_FX_RATE},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT,
     kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut},
    {Param::Unpatched::GlobalEffectable::DELAY_RATE, kNoParamIDShortcut, kNoParamIDShortcut,
     Param::Unpatched::GlobalEffectable::DELAY_AMOUNT, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut},
    {kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut,
     kNoParamIDShortcut, kNoParamIDShortcut, kNoParamIDShortcut},
};

PerformanceSessionView performanceSessionView{};

//initialize variables
PerformanceSessionView::PerformanceSessionView() {
	successfullyReadDefaultsFromFile = false;

	anyChangesToSave = false;

	defaultEditingMode = false;

	layoutVariant = 1;

	onFXDisplay = false;

	performanceLayoutBackedUp = false;

	justExitedSoundEditor = false;

	initPadPress(firstPadPress);
	initPadPress(lastPadPress);
	initPadPress(backupLastPadPress);

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		initFXPress(fxPress[xDisplay]);
		initFXPress(backupFXPress[xDisplay]);
		initFXPress(backupXMLDefaultFXPress[xDisplay]);

		initLayout(layoutForPerformance[xDisplay]);
		initLayout(backupLayoutForPerformance[xDisplay]);
		initLayout(backupXMLDefaultLayoutForPerformance[xDisplay]);

		initDefaultFXValues(xDisplay);
	}
}

void PerformanceSessionView::initPadPress(PadPress& padPress) {
	padPress.isActive = false;
	padPress.xDisplay = kNoSelection;
	padPress.yDisplay = kNoSelection;
	padPress.paramKind = Param::Kind::NONE;
	padPress.paramID = kNoSelection;
}

void PerformanceSessionView::initFXPress(FXColumnPress& columnPress) {
	columnPress.previousKnobPosition = kNoSelection;
	columnPress.currentKnobPosition = kNoSelection;
	columnPress.yDisplay = kNoSelection;
	columnPress.timeLastPadPress = 0;
	columnPress.padPressHeld = false;
}

void PerformanceSessionView::initLayout(ParamsForPerformance& layout) {
	layout.paramKind = Param::Kind::NONE;
	layout.paramID = kNoSelection;
	layout.xDisplay = kNoSelection;
	layout.yDisplay = kNoSelection;
	layout.rowColour[0] = 0;
	layout.rowColour[1] = 0;
	layout.rowColour[2] = 0;
	layout.rowTailColour[0] = 0;
	layout.rowTailColour[1] = 0;
	layout.rowTailColour[2] = 0;
}

void PerformanceSessionView::initDefaultFXValues(int32_t xDisplay) {
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		int32_t defaultFXValue = calculateKnobPosForSinglePadPress(yDisplay);
		defaultFXValues[xDisplay][yDisplay] = defaultFXValue;
		backupXMLDefaultFXValues[xDisplay][yDisplay] = defaultFXValue;
	}
}

bool PerformanceSessionView::opened() {
	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		PadLEDs::skipGreyoutFade();
	}

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	focusRegained();

	return true;
}

void PerformanceSessionView::focusRegained() {
	currentSong->affectEntire = true;

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);

	if (!successfullyReadDefaultsFromFile) {
		readDefaultsFromFile();
	}

	setLedStates();

	updateLayoutChangeStatus();

	if (defaultEditingMode) {
		indicator_leds::blinkLed(IndicatorLED::KEYBOARD);
	}

	if (display->have7SEG()) {
		redrawNumericDisplay();
	}

	uiNeedsRendering(this);
}

void PerformanceSessionView::graphicsRoutine() {
	static int counter = 0;
	if (currentUIMode == UI_MODE_NONE) {
		int32_t modKnobMode = -1;
		bool editingComp = false;
		if (view.activeModControllableModelStack.modControllable) {
			uint8_t* modKnobModePointer = view.activeModControllableModelStack.modControllable->getModKnobMode();
			if (modKnobModePointer) {
				modKnobMode = *modKnobModePointer;
				editingComp = view.activeModControllableModelStack.modControllable->isEditingComp();
			}
		}
		if (modKnobMode == 4 && editingComp) { //upper
			counter = (counter + 1) % 5;
			if (counter == 0) {
				uint8_t gr = AudioEngine::mastercompressor.gainReduction;

				indicator_leds::setMeterLevel(1, gr); //Gain Reduction LED
			}
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];

	// Nothing to do here but clear since we don't render playhead
	memset(&tickSquares, 255, sizeof(tickSquares));
	memset(&colours, 255, sizeof(colours));
	PadLEDs::setTickSquares(tickSquares, colours);
}

ActionResult PerformanceSessionView::timerCallback() {
	return ActionResult::DEALT_WITH;
}

bool PerformanceSessionView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                            bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (!occupancyMask) {
		return true;
	}

	PadLEDs::renderingLock = true;

	// erase current image as it will be refreshed
	memset(image, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth) * 3);

	// erase current occupancy mask as it will be refreshed
	memset(occupancyMask, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	performActualRender(whichRows, &image[0][0][0], occupancyMask, currentSong->xScroll[NAVIGATION_CLIP],
	                    currentSong->xZoom[NAVIGATION_CLIP], kDisplayWidth, kDisplayWidth + kSideBarWidth,
	                    drawUndefinedArea);

	PadLEDs::renderingLock = false;

	return true;
}

/// render performance mode
void PerformanceSessionView::performActualRender(uint32_t whichRows, uint8_t* image,
                                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                 int32_t xScroll, uint32_t xZoom, int32_t renderWidth,
                                                 int32_t imageWidth, bool drawUndefinedArea) {

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];

		renderRow(image + (yDisplay * imageWidth * 3), occupancyMaskOfRow, yDisplay);
	}
}

/// render every column, one row at a time
void PerformanceSessionView::renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		uint8_t* pixel = image + (xDisplay * 3);

		if (editingParam) {
			//if you're in param editing mode, highlight shortcuts for performance view params
			//if param has been assigned to an FX column, highlight it white, otherwise highlight it grey
			if (isPadShortcut(xDisplay, yDisplay)) {
				if (isParamAssignedToFXColumn(paramKindShortcutsForPerformanceView[xDisplay][yDisplay],
				                              paramIDShortcutsForPerformanceView[xDisplay][yDisplay])) {
					pixel[0] = 130;
					pixel[1] = 120;
					pixel[2] = 130;
				}
				else {
					pixel[0] = kUndefinedGreyShade;
					pixel[1] = kUndefinedGreyShade;
					pixel[2] = kUndefinedGreyShade;
				}
			}
			//if you're in param editing mode and pressing a shortcut pad, highlight the columns
			//that the param is assigned to the colour of that FX column
			if (firstPadPress.isActive) {
				if ((layoutForPerformance[xDisplay].paramKind == firstPadPress.paramKind)
				    && (layoutForPerformance[xDisplay].paramID == firstPadPress.paramID)) {
					memcpy(pixel, &layoutForPerformance[xDisplay].rowTailColour, 3);
				}
			}
		}
		else {
			//elsewhere in performance view, if an FX column has not been assigned a param,
			//highlight the column grey
			if (layoutForPerformance[xDisplay].paramID == kNoSelection) {
				pixel[0] = kUndefinedGreyShade;
				pixel[1] = kUndefinedGreyShade;
				pixel[2] = kUndefinedGreyShade;
			}
			else {
				//if you're currently pressing an FX column, highlight it a bright colour
				if ((fxPress[xDisplay].currentKnobPosition != kNoSelection)
				    && (fxPress[xDisplay].padPressHeld == false)) {
					memcpy(pixel, &layoutForPerformance[xDisplay].rowColour, 3);
				}
				//if you're not currently pressing an FX column, highlight it a dimmer colour
				else {
					memcpy(pixel, &layoutForPerformance[xDisplay].rowTailColour, 3);
				}

				//if you're currently pressing an FX column, highlight the pad you're pressing white
				if ((fxPress[xDisplay].currentKnobPosition == defaultFXValues[xDisplay][yDisplay])
				    && (fxPress[xDisplay].yDisplay == yDisplay)) {
					pixel[0] = 130;
					pixel[1] = 120;
					pixel[2] = 130;
				}
			}
		}

		occupancyMask[xDisplay] = 64;
	}
}

/// check if a param has been assinged to any of the FX columns
bool PerformanceSessionView::isParamAssignedToFXColumn(Param::Kind paramKind, int32_t paramID) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		if ((layoutForPerformance[xDisplay].paramKind == paramKind)
		    && (layoutForPerformance[xDisplay].paramID == paramID)) {
			return true;
		}
	}
	return false;
}

/// nothing to render in sidebar (yet)
bool PerformanceSessionView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	return true;
}

/// render performance view display on opening
void PerformanceSessionView::renderViewDisplay() {
	if (defaultEditingMode) {
		if (display->haveOLED()) {
			deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

			//render "Performance View" at top of OLED screen
			deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORM_VIEW), yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			yPos = yPos + 12;

			char const* editingModeType;

			//render "Param" or "Value" in the middle of the OLED screen
			if (editingParam) {
				editingModeType = l10n::get(l10n::String::STRING_FOR_PERFORM_EDIT_PARAM);
			}
			else {
				editingModeType = l10n::get(l10n::String::STRING_FOR_PERFORM_EDIT_VALUE);
			}

			deluge::hid::display::OLED::drawStringCentred(editingModeType, yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			yPos = yPos + 12;

			//render "Editing Mode" at the bottom of the OLED screen
			deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORM_EDITOR), yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			deluge::hid::display::OLED::sendMainImage();
		}
		else {
			display->setScrollingText(l10n::get(l10n::String::STRING_FOR_PERFORM_EDITOR));
		}
	}
	else {
		if (display->haveOLED()) {
			deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

			yPos = yPos + 12;

			//Render "Performance View" in the middle of the OLED screen
			deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORM_VIEW), yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			deluge::hid::display::OLED::sendMainImage();
		}
		else {
			display->setScrollingText(l10n::get(l10n::String::STRING_FOR_PERFORM_VIEW));
		}
	}
	onFXDisplay = false;
}

/// Render Parameter Name and Value set when using Performance Pads
void PerformanceSessionView::renderFXDisplay(Param::Kind paramKind, int32_t paramID, int32_t knobPos) {
	if (editingParam) {
		//display parameter name
		char parameterName[30];
		strncpy(parameterName, getParamDisplayName(paramKind, paramID), 29);
		if (display->haveOLED()) {
			deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
			yPos = yPos + 12;

			deluge::hid::display::OLED::drawStringCentred(parameterName, yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			deluge::hid::display::OLED::sendMainImage();
		}
		else {
			display->setScrollingText(parameterName);
		}
	}
	else {
		if (display->haveOLED()) {
			deluge::hid::display::OLED::clearMainImage();

			//display parameter name
			char parameterName[30];
			strncpy(parameterName, getParamDisplayName(paramKind, paramID), 29);

#if OLED_MAIN_HEIGHT_PIXELS == 64
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
			deluge::hid::display::OLED::drawStringCentred(parameterName, yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			//display parameter value
			yPos = yPos + 24;

			char buffer[5];
			intToString(knobPos, buffer);
			deluge::hid::display::OLED::drawStringCentred(buffer, yPos, deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			deluge::hid::display::OLED::sendMainImage();
		}
		//7Seg Display
		else {
			char buffer[5];
			intToString(knobPos, buffer);
			display->displayPopup(buffer, 3, true);
		}
	}
	onFXDisplay = true;
}

void PerformanceSessionView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	renderViewDisplay();
}

void PerformanceSessionView::redrawNumericDisplay() {
	renderViewDisplay();
}

void PerformanceSessionView::setLedStates() {
	setCentralLEDStates();  //inherited from session view
	view.setLedStates();    //inherited from session view
	view.setModLedStates(); //inherited from session view

	//performanceView specific LED settings
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, true);

	if (currentSong->lastClipInstanceEnteredStartPos != -1) {
		indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW);
	}
}

void PerformanceSessionView::setCentralLEDStates() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::BACK, false);
}

ActionResult PerformanceSessionView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	// Clip-view button
	if (b == CLIP_VIEW) {
		if (on && ((currentUIMode == UI_MODE_NONE) || isUIModeActive(UI_MODE_STUTTERING))
		    && playbackHandler.recording != RECORDING_ARRANGEMENT) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			releaseStutter(modelStack);
			sessionView.transitionToViewForClip(); // May fail if no currentClip
		}
	}

	// Song-view button without shift

	// Arranger view button, or if there isn't one then song view button
#ifdef arrangerViewButtonX
	else if (b == arrangerView) {
#else
	else if (b == SESSION_VIEW && !Buttons::isShiftButtonPressed()) {
#endif
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		bool lastSessionButtonActiveState = sessionButtonActive;
		sessionButtonActive = on;

		// Press with special modes
		if (on) {
			sessionButtonUsed = false;

			// If holding record button...
			if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
				Buttons::recordButtonPressUsedUp = true;

				// Make sure we weren't already playing...
				if (!playbackHandler.playbackState) {

					Action* action = actionLogger.getNewAction(ACTION_ARRANGEMENT_RECORD, false);

					arrangerView.xScrollWhenPlaybackStarted = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
					if (action) {
						action->posToClearArrangementFrom = arrangerView.xScrollWhenPlaybackStarted;
					}

					currentSong->clearArrangementBeyondPos(
					    arrangerView.xScrollWhenPlaybackStarted,
					    action); // Want to do this before setting up playback or place new instances
					int32_t error =
					    currentSong->placeFirstInstancesOfActiveClips(arrangerView.xScrollWhenPlaybackStarted);

					if (error) {
						display->displayError(error);
						return ActionResult::DEALT_WITH;
					}
					playbackHandler.recording = RECORDING_ARRANGEMENT;
					playbackHandler.setupPlaybackUsingInternalClock();

					arrangement.playbackStartedAtPos =
					    arrangerView.xScrollWhenPlaybackStarted; // Have to do this after setting up playback

					indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
					indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW, 255, 1);
					sessionButtonUsed = true;
				}
			}
		}
		// Release without special mode
		else if (!on && ((currentUIMode == UI_MODE_NONE) || isUIModeActive(UI_MODE_STUTTERING))) {
			if (lastSessionButtonActiveState && !sessionButtonActive && !sessionButtonUsed
			    && !sessionView.gridFirstPadActive()) {

				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					currentSong->endInstancesOfActiveClips(playbackHandler.getActualArrangementRecordPos());
					// Must call before calling getArrangementRecordPos(), cos that detaches the cloned Clip
					currentSong->resumeClipsClonedForArrangementRecording();
					playbackHandler.recording = RECORDING_OFF;
					view.setModLedStates();
					playbackHandler.setLedStates();
				}

				sessionButtonUsed = false;
			}
		}
	}

	//clear and reset held params
	else if (b == BACK && isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (on) {
			backupPerformanceLayout();
			resetPerformanceView(modelStack);
			logPerformanceLayoutChange();
		}
	}

	//save performance view layout
	else if (b == SAVE) {
		if (on) {
			savePerformanceViewLayout();
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_SAVED));
		}
	}

	//load performance view layout
	else if (b == LOAD) {
		if (on) {
			loadPerformanceViewLayout();
			renderViewDisplay();
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_LOADED));
		}
	}

	//enter "Perform FX" soundEditor menu
	else if ((b == SELECT_ENC) && !Buttons::isShiftButtonPressed()) {
		if (on) {

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				return ActionResult::DEALT_WITH;
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			display->setNextTransitionDirection(1);
			soundEditor.setup();
			openUI(&soundEditor);
		}
	}

	//enter exit Horizontal Encoder Button Press UI Mode
	//(not used yet, will be though!)
	else if (b == X_ENC) {
		if (on) {
			enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}
		else {
			if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
				exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
			}
		}
	}

	//enter/exit Performance View when used on its own
	//enter/cycle/exit editing modes when used while holding shift button
	else if (b == KEYBOARD) {
		if (on) {
			if (Buttons::isShiftButtonPressed()) {
				if (defaultEditingMode && editingParam) {
					defaultEditingMode = false;
					editingParam = false;
					indicator_leds::setLedState(IndicatorLED::KEYBOARD, true);
				}
				else {
					if (!defaultEditingMode) {
						indicator_leds::blinkLed(IndicatorLED::KEYBOARD);
					}
					else {
						editingParam = true;
					}
					defaultEditingMode = true;
				}
				updateLayoutChangeStatus();
				renderViewDisplay();
				uiNeedsRendering(this);
			}
			else {
				releaseStutter(modelStack);
				if (currentSong->lastClipInstanceEnteredStartPos != -1) {
					changeRootUI(&arrangerView);
				}
				else {
					changeRootUI(&sessionView);
				}
			}
		}
	}

	//disable button presses for Vertical encoder
	//disable back button press since undo doesn't work well in this view atm.
	else if (b == Y_ENC) { //|| (b == BACK)) {
		return ActionResult::DEALT_WITH;
	}

	else {
		ActionResult buttonActionResult;
		buttonActionResult = TimelineView::buttonAction(b, on, inCardRoutine);

		//release stutter if you press play - stutter needs to be turned on after playback is running
		//re-render grid if undoing an action (e.g. you previously loaded layout)
		if (on && (b == PLAY || b == BACK)) {
			if (b == PLAY) {
				releaseStutter(modelStack);
			}
			uiNeedsRendering(this);
		}
		return buttonActionResult;
	}
	return ActionResult::DEALT_WITH;
}

ActionResult PerformanceSessionView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	if (!justExitedSoundEditor) {
		//if pad was pressed in main deluge grid (not sidebar)
		if (xDisplay < kDisplayWidth) {
			if (on) {
				//if it's a shortcut press, enter soundEditor menu for that parameter
				if (Buttons::isShiftButtonPressed()) {
					return soundEditor.potentialShortcutPadAction(xDisplay, yDisplay, on);
				}
			}
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStack =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			//if not in param editor (so, regular performance view or value editor)
			if (!editingParam) {
				if (layoutForPerformance[xDisplay].paramID == kNoSelection) {
					return ActionResult::DEALT_WITH;
				}
				normalPadAction(modelStack, xDisplay, yDisplay, on);
			}
			//editing mode & editing parameter FX assignments
			else {
				paramEditorPadAction(modelStack, xDisplay, yDisplay, on);
			}
			uiNeedsRendering(this); //re-render pads
		}
	}
	else if (!on) {
		justExitedSoundEditor = false;
	}
	return ActionResult::DEALT_WITH;
}

/// process pad actions in the normal performance view or value editor
void PerformanceSessionView::normalPadAction(ModelStackWithThreeMainThings* modelStack, int32_t xDisplay,
                                             int32_t yDisplay, int32_t on) {
	//obtain Param::Kind, ParamID corresponding to the column pressed on performance grid
	Param::Kind lastSelectedParamKind = layoutForPerformance[xDisplay].paramKind; //kind;
	int32_t lastSelectedParamID = layoutForPerformance[xDisplay].paramID;

	//pressing a pad
	if (on) {
		//no need to pad press action if you've already processed it previously and pad was held
		if (fxPress[xDisplay].yDisplay != yDisplay) {
			backupPerformanceLayout();
			//check if there a previously held press for this parameter in another column and disable it
			//also transfer the previous value for that held pad to this new pad column press
			for (int32_t i = 0; i < kDisplayWidth; i++) {
				if (i != xDisplay) {
					if ((layoutForPerformance[i].paramKind == lastSelectedParamKind)
					    && (layoutForPerformance[i].paramID == lastSelectedParamID)) {
						fxPress[xDisplay].previousKnobPosition = fxPress[i].previousKnobPosition;
						initFXPress(fxPress[i]);
					}
				}
			}
			padPressAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, yDisplay,
			               !defaultEditingMode);
		}
	}
	//releasing a pad
	else {
		//if releasing a pad with "held" status shortly after being given that status
		//or releasing a pad that was not in "held" status but was a longer press and release
		if ((isParamStutter(lastSelectedParamKind, lastSelectedParamID) && lastPadPress.isActive)
		    || (fxPress[xDisplay].padPressHeld
		        && ((AudioEngine::audioSampleTimer - fxPress[xDisplay].timeLastPadPress) < kHoldTime))
		    || ((fxPress[xDisplay].previousKnobPosition != kNoSelection) && (fxPress[xDisplay].yDisplay == yDisplay)
		        && ((AudioEngine::audioSampleTimer - fxPress[xDisplay].timeLastPadPress) >= kHoldTime))) {

			padReleaseAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, !defaultEditingMode);
		}
		//if releasing a pad that was quickly pressed, give it held status
		else if ((fxPress[xDisplay].previousKnobPosition != kNoSelection) && (fxPress[xDisplay].yDisplay == yDisplay)
		         && ((AudioEngine::audioSampleTimer - fxPress[xDisplay].timeLastPadPress) < kHoldTime)) {
			fxPress[xDisplay].padPressHeld = true;
			logPerformanceLayoutChange();
		}
		updateLayoutChangeStatus();
	}

	//if you're in editing mode and not editing a param, pressing an FX column will open soundEditor menu
	//if a parameter has been assigned to that FX column
	if (defaultEditingMode && on) {
		int32_t lastSelectedParamShortcutX = layoutForPerformance[lastPadPress.xDisplay].xDisplay;
		int32_t lastSelectedParamShortcutY = layoutForPerformance[lastPadPress.xDisplay].yDisplay;

		//if you're not already in soundEditor, enter soundEditor
		//or if you're already in soundEditor, check if you're in the right menu
		if ((getCurrentUI() != &soundEditor)
		    || ((getCurrentUI() == &soundEditor)
		        && (soundEditor.getCurrentMenuItem()
		            != paramShortcutsForSongView[lastSelectedParamShortcutX][lastSelectedParamShortcutY]))) {
			soundEditor.potentialShortcutPadAction(layoutForPerformance[xDisplay].xDisplay,
			                                       layoutForPerformance[xDisplay].yDisplay, on);
		}
		//otherwise no need to do anything as you're already displaying the menu for the parameter
	}
}

void PerformanceSessionView::padPressAction(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind,
                                            int32_t paramID, int32_t xDisplay, int32_t yDisplay, bool renderDisplay) {
	if (setParameterValue(modelStack, paramKind, paramID, xDisplay, defaultFXValues[xDisplay][yDisplay],
	                      renderDisplay)) {
		//if pressing a new pad in a column, reset held status
		fxPress[xDisplay].padPressHeld = false;

		//save row yDisplay of current pad press in column xDisplay
		fxPress[xDisplay].yDisplay = yDisplay;

		//save time of current pad press in column xDisplay
		fxPress[xDisplay].timeLastPadPress = AudioEngine::audioSampleTimer;

		//update current knob position
		fxPress[xDisplay].currentKnobPosition = defaultFXValues[xDisplay][yDisplay];

		//save xDisplay, yDisplay, paramKind and paramID currently being edited
		lastPadPress.isActive = true;
		lastPadPress.xDisplay = xDisplay;
		lastPadPress.yDisplay = yDisplay;
		lastPadPress.paramKind = paramKind;
		lastPadPress.paramID = paramID;
	}
}

void PerformanceSessionView::padReleaseAction(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind,
                                              int32_t paramID, int32_t xDisplay, bool renderDisplay) {
	if (setParameterValue(modelStack, paramKind, paramID, xDisplay, fxPress[xDisplay].previousKnobPosition,
	                      renderDisplay)) {
		initFXPress(fxPress[xDisplay]);
		initPadPress(lastPadPress);
	}
}

/// process pad actions in the param editor
void PerformanceSessionView::paramEditorPadAction(ModelStackWithThreeMainThings* modelStack, int32_t xDisplay,
                                                  int32_t yDisplay, int32_t on) {
	//pressing a pad
	if (on) {
		//if you haven't yet pressed and are holding a param shortcut pad on the param overview
		if (!firstPadPress.isActive) {
			if (isPadShortcut(xDisplay, yDisplay)) {
				firstPadPress.isActive = true;
				firstPadPress.paramKind = paramKindShortcutsForPerformanceView[xDisplay][yDisplay];
				firstPadPress.paramID = paramIDShortcutsForPerformanceView[xDisplay][yDisplay];
				firstPadPress.xDisplay = xDisplay;
				firstPadPress.yDisplay = yDisplay;
				renderFXDisplay(firstPadPress.paramKind, firstPadPress.paramID);
			}
		}
		//if you are holding a param shortcut pad and are now pressing a pad in an FX column
		else {
			backupPerformanceLayout();
			//if the FX column you are pressing is currently assigned to a different param or no param
			if ((layoutForPerformance[xDisplay].paramKind != firstPadPress.paramKind)
			    || (layoutForPerformance[xDisplay].paramID != firstPadPress.paramID)
			    || (layoutForPerformance[xDisplay].xDisplay != firstPadPress.xDisplay)
			    || (layoutForPerformance[xDisplay].yDisplay != firstPadPress.yDisplay)) {

				//remove any existing holds from the FX column before assigning a new param
				resetFXColumn(modelStack, xDisplay);

				//assign new param to the FX column
				layoutForPerformance[xDisplay].paramKind = firstPadPress.paramKind;
				layoutForPerformance[xDisplay].paramID = firstPadPress.paramID;
				layoutForPerformance[xDisplay].xDisplay = firstPadPress.xDisplay;
				layoutForPerformance[xDisplay].yDisplay = firstPadPress.yDisplay;

				//assign new colour to the FX column based on the new param assigned
				for (int32_t i = 0; i < kNumParamsForPerformance; i++) {
					if ((songParamsForPerformance[i].paramKind == firstPadPress.paramKind)
					    && (songParamsForPerformance[i].paramID == firstPadPress.paramID)) {
						memcpy(&layoutForPerformance[xDisplay].rowColour, &songParamsForPerformance[i].rowColour, 3);
						memcpy(&layoutForPerformance[xDisplay].rowTailColour,
						       &songParamsForPerformance[i].rowTailColour, 3);
						break;
					}
				}
			}
			//if you have already assigned the same param to the FX column, pressing the column will remove it
			else {
				//remove any existing holds from the FX column before clearing param from column
				resetFXColumn(modelStack, xDisplay);

				//remove param from FX column
				initLayout(layoutForPerformance[xDisplay]);
			}
			logPerformanceLayoutChange();
			updateLayoutChangeStatus();
		}
	}
	//releasing a pad
	else {
		if ((firstPadPress.xDisplay == xDisplay) && (firstPadPress.yDisplay == yDisplay)) {
			initPadPress(firstPadPress);
			renderViewDisplay();
		}
	}
}

/// check if pad press corresponds to a shortcut pad on the grid
bool PerformanceSessionView::isPadShortcut(int32_t xDisplay, int32_t yDisplay) {
	if ((paramKindShortcutsForPerformanceView[xDisplay][yDisplay] != Param::Kind::NONE)
	    && (paramIDShortcutsForPerformanceView[xDisplay][yDisplay] != kNoParamIDShortcut)) {
		return true;
	}
	return false;
}

/// backup performance layout so changes can be undone / redone later
void PerformanceSessionView::backupPerformanceLayout() {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		if (successfullyReadDefaultsFromFile) {
			memcpy(&backupFXPress[xDisplay], &fxPress[xDisplay], sizeFXPress);
		}
		memcpy(&backupLayoutForPerformance[xDisplay], &layoutForPerformance[xDisplay], sizeParamsForPerformance);
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			backupDefaultFXValues[xDisplay][yDisplay] = defaultFXValues[xDisplay][yDisplay];
		}
	}
	memcpy(&backupLastPadPress, &lastPadPress, sizePadPress);
	performanceLayoutBackedUp = true;
}

/// used in conjunction with backupPerformanceLayout to log changes
/// while in Performance View so that you can undo/redo them afters
void PerformanceSessionView::logPerformanceLayoutChange() {
	if (anyChangesToLog()) {
		actionLogger.recordPerformanceLayoutChange(&backupLastPadPress, &lastPadPress, &backupFXPress[0], &fxPress[0],
		                                           &backupLayoutForPerformance[0], &layoutForPerformance[0],
		                                           backupDefaultFXValues, defaultFXValues);
		actionLogger.closeAction(ACTION_PARAM_UNAUTOMATED_VALUE_CHANGE);
	}
}

/// check if there are any changes that needed to be logged in action logger for undo/redo mechanism to work
bool PerformanceSessionView::anyChangesToLog() {
	if (performanceLayoutBackedUp) {
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			if (backupFXPress[xDisplay].previousKnobPosition != fxPress[xDisplay].previousKnobPosition) {
				return true;
			}
			else if (backupFXPress[xDisplay].currentKnobPosition != fxPress[xDisplay].currentKnobPosition) {
				return true;
			}
			else if (backupFXPress[xDisplay].yDisplay != fxPress[xDisplay].yDisplay) {
				return true;
			}
			else if (backupFXPress[xDisplay].timeLastPadPress != fxPress[xDisplay].timeLastPadPress) {
				return true;
			}
			else if (backupFXPress[xDisplay].padPressHeld != fxPress[xDisplay].padPressHeld) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].paramKind != layoutForPerformance[xDisplay].paramKind) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].paramID != layoutForPerformance[xDisplay].paramID) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].xDisplay != layoutForPerformance[xDisplay].xDisplay) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].yDisplay != layoutForPerformance[xDisplay].yDisplay) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].rowColour[0] != layoutForPerformance[xDisplay].rowColour[0]) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].rowColour[1] != layoutForPerformance[xDisplay].rowColour[1]) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].rowColour[2] != layoutForPerformance[xDisplay].rowColour[2]) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].rowTailColour[0]
			         != layoutForPerformance[xDisplay].rowTailColour[0]) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].rowTailColour[1]
			         != layoutForPerformance[xDisplay].rowTailColour[1]) {
				return true;
			}
			else if (backupLayoutForPerformance[xDisplay].rowTailColour[2]
			         != layoutForPerformance[xDisplay].rowTailColour[2]) {
				return true;
			}
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				if (backupDefaultFXValues[xDisplay][yDisplay] != defaultFXValues[xDisplay][yDisplay]) {
					return true;
				}
			}
		}
	}
	return false;
}

/// called when you press <> + back
/// in param editor, it will clear existing param mappings
/// in regular performance view or value editor, it will clear held pads and reset param values to pre-held state
void PerformanceSessionView::resetPerformanceView(ModelStackWithThreeMainThings* modelStack) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		if (editingParam) {
			initLayout(layoutForPerformance[xDisplay]);
		}
		else if (fxPress[xDisplay].padPressHeld) {
			//obtain Param::Kind and ParamID corresponding to the column in focus (xDisplay)
			Param::Kind lastSelectedParamKind = layoutForPerformance[xDisplay].paramKind; //kind;
			int32_t lastSelectedParamID = layoutForPerformance[xDisplay].paramID;

			if (lastSelectedParamID != kNoSelection) {
				padReleaseAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, false);
			}
		}
	}
	updateLayoutChangeStatus();
	renderViewDisplay();
	uiNeedsRendering(this);
}

/// resets a single FX column to remove held status
/// and reset the param value assigned to that FX column to pre-held state
void PerformanceSessionView::resetFXColumn(ModelStackWithThreeMainThings* modelStack, int32_t xDisplay) {
	if (fxPress[xDisplay].padPressHeld) {
		//obtain Param::Kind and ParamID corresponding to the column in focus (xDisplay)
		Param::Kind lastSelectedParamKind = layoutForPerformance[xDisplay].paramKind; //kind;
		int32_t lastSelectedParamID = layoutForPerformance[xDisplay].paramID;

		if (lastSelectedParamID != kNoSelection) {
			padReleaseAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, false);
		}

		if (!editingParam) {
			uiNeedsRendering(this);
		}
	}
	updateLayoutChangeStatus();
}

/// check if parameter is stutter
bool PerformanceSessionView::isParamStutter(Param::Kind paramKind, int32_t paramID) {
	if ((paramKind == Param::Kind::UNPATCHED_SOUND) && (paramID == Param::Unpatched::STUTTER_RATE)) {
		return true;
	}
	return false;
}

/// check if stutter is active and release it if it is
void PerformanceSessionView::releaseStutter(ModelStackWithThreeMainThings* modelStack) {
	if (isUIModeActive(UI_MODE_STUTTERING)) {
		padReleaseAction(modelStack, Param::Kind::UNPATCHED_SOUND, Param::Unpatched::STUTTER_RATE,
		                 lastPadPress.xDisplay, false);
	}
}

/// this will set a new value for a parameter
/// if we're dealing with stutter, it will check if stutter is active and end the stutter first
/// if we're dealing with stutter, it will change the stutter rate value and then begin stutter
/// if you're in the value editor, pressing a column and changing the value will also open the sound editor
/// menu for the parameter to show you the current value in the menu
/// in regular performance view, this function will also update the parameter value shown on the display
bool PerformanceSessionView::setParameterValue(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind,
                                               int32_t paramID, int32_t xDisplay, int32_t knobPos, bool renderDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStack, paramID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		if (modelStackWithParam->getTimelineCounter()
		    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

			//if switching to a new pad in the stutter column and stuttering is already active
			//e.g. it means a pad was held before, end previous stutter before starting stutter again
			if ((paramKind == Param::Kind::UNPATCHED_SOUND) && (paramID == Param::Unpatched::STUTTER_RATE)
			    && (isUIModeActive(UI_MODE_STUTTERING))) {
				((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
				    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
			}

			if (fxPress[xDisplay].previousKnobPosition == kNoSelection) {
				int32_t oldParameterValue =
				    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);
				fxPress[xDisplay].previousKnobPosition =
				    modelStackWithParam->paramCollection->paramValueToKnobPos(oldParameterValue, modelStackWithParam);
			}

			int32_t newParameterValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(knobPos, modelStackWithParam);

			modelStackWithParam->autoParam->setValuePossiblyForRegion(newParameterValue, modelStackWithParam,
			                                                          view.modPos, view.modLength);

			if ((paramKind == Param::Kind::UNPATCHED_SOUND) && (paramID == Param::Unpatched::STUTTER_RATE)
			    && (fxPress[xDisplay].previousKnobPosition != knobPos)) {
				((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
				    ->beginStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
			}

			if (renderDisplay) {
				int32_t valueForDisplay = view.calculateKnobPosForDisplay(paramKind, paramID, knobPos + kKnobPosOffset);
				renderFXDisplay(paramKind, paramID, valueForDisplay);
			}

			return true;
		}
	}

	return false;
}

/// get the current value for a parameter and update display if value is different than currently shown
/// update current value stored
void PerformanceSessionView::getParameterValue(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind,
                                               int32_t paramID, int32_t xDisplay, bool renderDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStack, paramID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		if (modelStackWithParam->getTimelineCounter()
		    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

			int32_t value = modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);

			int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);

			if (renderDisplay && (fxPress[xDisplay].currentKnobPosition != knobPos)) {
				int32_t valueForDisplay = view.calculateKnobPosForDisplay(paramKind, paramID, knobPos + kKnobPosOffset);
				renderFXDisplay(paramKind, paramID, valueForDisplay);
			}

			if (fxPress[xDisplay].currentKnobPosition != knobPos) {
				fxPress[xDisplay].currentKnobPosition = knobPos;
			}
		}
	}
}

/// get's the modelstack for the parameters that are being edited
ModelStackWithAutoParam* PerformanceSessionView::getModelStackWithParam(ModelStackWithThreeMainThings* modelStack,
                                                                        int32_t paramID) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (modelStack) {
		ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();

		if (summary) {
			ParamSet* paramSet = (ParamSet*)summary->paramCollection;
			modelStackWithParam = modelStack->addParam(paramSet, summary, paramID, &paramSet->params[paramID]);
		}
	}

	return modelStackWithParam;
}

/// converts grid pad press yDisplay into a knobPosition value
/// this will likely need to be customized based on the parameter to create some more param appropriate ranges
int32_t PerformanceSessionView::calculateKnobPosForSinglePadPress(int32_t yDisplay) {
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

/// Used to edit a pad's value in editing mode
void PerformanceSessionView::selectEncoderAction(int8_t offset) {
	if (lastPadPress.isActive && defaultEditingMode && !editingParam && (getCurrentUI() == &soundEditor)) {
		backupPerformanceLayout();

		int32_t lastSelectedParamShortcutX = layoutForPerformance[lastPadPress.xDisplay].xDisplay;
		int32_t lastSelectedParamShortcutY = layoutForPerformance[lastPadPress.xDisplay].yDisplay;

		if (soundEditor.getCurrentMenuItem()
		    == paramShortcutsForSongView[lastSelectedParamShortcutX][lastSelectedParamShortcutY]) {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStack =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			getParameterValue(modelStack, lastPadPress.paramKind, lastPadPress.paramID, lastPadPress.xDisplay, false);

			defaultFXValues[lastPadPress.xDisplay][lastPadPress.yDisplay] =
			    calculateKnobPosForSelectEncoderTurn(fxPress[lastPadPress.xDisplay].currentKnobPosition, offset);

			if (setParameterValue(modelStack, lastPadPress.paramKind, lastPadPress.paramID, lastPadPress.xDisplay,
			                      defaultFXValues[lastPadPress.xDisplay][lastPadPress.yDisplay], false)) {
				logPerformanceLayoutChange();
				updateLayoutChangeStatus();
			}
			goto exit;
		}
	}
	if (getCurrentUI() == &soundEditor) {
		soundEditor.getCurrentMenuItem()->selectEncoderAction(offset);
	}
exit:
	return;
}

/// used to calculate new knobPos when you turn the select encoder
int32_t PerformanceSessionView::calculateKnobPosForSelectEncoderTurn(int32_t knobPos, int32_t offset) {

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

ActionResult PerformanceSessionView::horizontalEncoderAction(int32_t offset) {
	return ActionResult::DEALT_WITH;
}

ActionResult PerformanceSessionView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}

/// why do I need this? (code won't compile without it)
uint32_t PerformanceSessionView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

/// why do I need this? (code won't compile without it)
uint32_t PerformanceSessionView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

/// updates the display if the mod encoder has just updated the same parameter currently being held / last held
/// if no param is currently being held, it will reset the display to just show "Performance View"
void PerformanceSessionView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (getCurrentUI() == this) { //This routine may also be called from the Arranger view
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);

		if (!defaultEditingMode) {
			if (lastPadPress.isActive) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack =
				    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

				getParameterValue(modelStack, lastPadPress.paramKind, lastPadPress.paramID, lastPadPress.xDisplay);
			}
			else if (onFXDisplay) {
				renderViewDisplay();
			}
		}
	}
}

/// used to reset stutter if it's already active
void PerformanceSessionView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	//release stutter if it's already active before beginning stutter again
	if (on) {
		int32_t modKnobMode = -1;
		if (view.activeModControllableModelStack.modControllable) {
			uint8_t* modKnobModePointer = view.activeModControllableModelStack.modControllable->getModKnobMode();
			if (modKnobModePointer) {
				modKnobMode = *modKnobModePointer;

				// Stutter section
				if ((modKnobMode == 6) && (whichModEncoder == 1)) {
					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithThreeMainThings* modelStack =
					    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

					releaseStutter(modelStack);

					uiNeedsRendering(this);

					if (onFXDisplay) {
						renderViewDisplay();
					}
				}
			}
		}
	}
	if (isUIModeActive(UI_MODE_STUTTERING) && lastPadPress.isActive
	    && isParamStutter(lastPadPress.paramKind, lastPadPress.paramID)) {
		return;
	}
	else {
		UI::modEncoderButtonAction(whichModEncoder, on);
	}
}

void PerformanceSessionView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
}

/// this compares the last loaded XML file defaults to the current layout in performance view
/// to determine if there are any unsaved changes
void PerformanceSessionView::updateLayoutChangeStatus() {
	anyChangesToSave = false;

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		if (backupXMLDefaultLayoutForPerformance[xDisplay].paramKind != layoutForPerformance[xDisplay].paramKind) {
			anyChangesToSave = true;
			break;
		}
		else if (backupXMLDefaultLayoutForPerformance[xDisplay].paramID != layoutForPerformance[xDisplay].paramID) {
			anyChangesToSave = true;
			break;
		}
		else if (backupXMLDefaultFXPress[xDisplay].padPressHeld != fxPress[xDisplay].padPressHeld) {
			anyChangesToSave = true;
			break;
		}
		else if (backupXMLDefaultFXPress[xDisplay].yDisplay != fxPress[xDisplay].yDisplay) {
			anyChangesToSave = true;
			break;
		}
		else if (backupXMLDefaultFXPress[xDisplay].previousKnobPosition != fxPress[xDisplay].previousKnobPosition) {
			anyChangesToSave = true;
			break;
		}
		else {
			for (int32_t yDisplay = kDisplayHeight - 1; yDisplay >= 0; yDisplay--) {
				if (backupXMLDefaultFXValues[xDisplay][yDisplay] != defaultFXValues[xDisplay][yDisplay]) {
					anyChangesToSave = true;
					break;
				}
			}
		}
	}

	if (defaultEditingMode) {
		if (anyChangesToSave) {
			indicator_leds::blinkLed(IndicatorLED::SAVE);
		}
		else {
			indicator_leds::setLedState(IndicatorLED::SAVE, false);
		}
	}
	else {
		indicator_leds::setLedState(IndicatorLED::SAVE, false);
	}

	return;
}

/// update saved perfomance view layout and update saved changes status
void PerformanceSessionView::savePerformanceViewLayout() {
	writeDefaultsToFile();
	updateLayoutChangeStatus();
}

/// create default XML file and write defaults
/// I should check if file exists before creating one
void PerformanceSessionView::writeDefaultsToFile() {
	//PerformanceView.xml
	int32_t error = storageManager.createXMLFile(STRING_FOR_PERFORM_DEFAULTS_XML, true);
	if (error) {
		return;
	}

	//<defaults>
	storageManager.writeOpeningTagBeginning(STRING_FOR_PERFORM_DEFAULTS_TAG);
	storageManager.writeOpeningTagEnd();

	//<defaultFXValues>
	storageManager.writeOpeningTagBeginning(STRING_FOR_PERFORM_DEFAULTS_FXVALUES_TAG);
	storageManager.writeOpeningTagEnd();

	writeDefaultFXValuesToFile();

	storageManager.writeClosingTag(STRING_FOR_PERFORM_DEFAULTS_FXVALUES_TAG);

	storageManager.writeClosingTag(STRING_FOR_PERFORM_DEFAULTS_TAG);

	storageManager.closeFileAfterWriting();

	anyChangesToSave = false;
}

/// creates "FX1 - FX16 tags"
/// limiting # of FX to the # of columns on the grid (16 = kDisplayWidth)
/// could expand # of FX in the future if we allow user to selected from a larger bank of FX / build their own FX
void PerformanceSessionView::writeDefaultFXValuesToFile() {
	char tagName[10];
	tagName[0] = 'F';
	tagName[1] = 'X';
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		intToString(xDisplay + 1, &tagName[2]);
		storageManager.writeOpeningTagBeginning(tagName);
		storageManager.writeOpeningTagEnd();
		writeDefaultFXParamToFile(xDisplay);
		writeDefaultFXRowValuesToFile(xDisplay);
		writeDefaultFXHoldStatusToFile(xDisplay);
		storageManager.writeClosingTag(tagName);
	}
}

/// convert paramID to a paramName to write to XML
void PerformanceSessionView::writeDefaultFXParamToFile(int32_t xDisplay) {
	char const* paramName;

	if (layoutForPerformance[xDisplay].paramKind == Param::Kind::UNPATCHED_GLOBAL) {
		paramName = GlobalEffectable::paramToString(Param::Unpatched::START + layoutForPerformance[xDisplay].paramID);
	}
	else if (layoutForPerformance[xDisplay].paramKind == Param::Kind::UNPATCHED_SOUND) {
		paramName =
		    ModControllableAudio::paramToString(Param::Unpatched::START + layoutForPerformance[xDisplay].paramID);
	}
	else {
		paramName = STRING_FOR_PERFORM_DEFAULTS_NO_PARAM;
	}
	//<param>
	storageManager.writeTag(STRING_FOR_PERFORM_DEFAULTS_PARAM_TAG, paramName);

	backupXMLDefaultLayoutForPerformance[xDisplay].paramKind = layoutForPerformance[xDisplay].paramKind;
	backupXMLDefaultLayoutForPerformance[xDisplay].paramID = layoutForPerformance[xDisplay].paramID;
}

/// creates "8 - 1 row # tags within a "row" tag"
/// limiting # of rows to the # of rows on the grid (8 = kDisplayHeight)
void PerformanceSessionView::writeDefaultFXRowValuesToFile(int32_t xDisplay) {
	//<row>
	storageManager.writeOpeningTagBeginning(STRING_FOR_PERFORM_DEFAULTS_ROW_TAG);
	storageManager.writeOpeningTagEnd();
	char rowNumber[5];
	//creates tags from row 8 down to row 1
	for (int32_t yDisplay = kDisplayHeight - 1; yDisplay >= 0; yDisplay--) {
		intToString(yDisplay + 1, rowNumber);
		storageManager.writeTag(rowNumber, defaultFXValues[xDisplay][yDisplay] + kKnobPosOffset);

		backupXMLDefaultFXValues[xDisplay][yDisplay] = defaultFXValues[xDisplay][yDisplay];
	}
	storageManager.writeClosingTag(STRING_FOR_PERFORM_DEFAULTS_ROW_TAG);
}

/// for each FX column, write the held status, what row is being held, and what previous value was
/// (previous value is used to reset param after you remove the held status)
void PerformanceSessionView::writeDefaultFXHoldStatusToFile(int32_t xDisplay) {
	//<hold>
	storageManager.writeOpeningTagBeginning(STRING_FOR_PERFORM_DEFAULTS_HOLD_TAG);
	storageManager.writeOpeningTagEnd();

	if (fxPress[xDisplay].padPressHeld) {
		//<status>
		storageManager.writeTag(STRING_FOR_PERFORM_DEFAULTS_HOLD_STATUS_TAG, STRING_FOR_ON);
		//<row>
		storageManager.writeTag(STRING_FOR_PERFORM_DEFAULTS_ROW_TAG, fxPress[xDisplay].yDisplay + 1);
		//<resetValue>
		storageManager.writeTag(STRING_FOR_PERFORM_DEFAULTS_HOLD_RESETVALUE_TAG,
		                        fxPress[xDisplay].previousKnobPosition + kKnobPosOffset);

		backupXMLDefaultFXPress[xDisplay].padPressHeld = fxPress[xDisplay].padPressHeld;
		backupXMLDefaultFXPress[xDisplay].yDisplay = fxPress[xDisplay].yDisplay;
		backupXMLDefaultFXPress[xDisplay].previousKnobPosition = fxPress[xDisplay].previousKnobPosition;
	}
	else {
		//<status>
		storageManager.writeTag(STRING_FOR_PERFORM_DEFAULTS_HOLD_STATUS_TAG, STRING_FOR_OFF);
		//<row>
		storageManager.writeTag(STRING_FOR_PERFORM_DEFAULTS_ROW_TAG, kNoSelection);
		//<resetValue>
		storageManager.writeTag(STRING_FOR_PERFORM_DEFAULTS_HOLD_RESETVALUE_TAG, kNoSelection);

		backupXMLDefaultFXPress[xDisplay].padPressHeld = false;
		backupXMLDefaultFXPress[xDisplay].yDisplay = kNoSelection;
		backupXMLDefaultFXPress[xDisplay].previousKnobPosition = kNoSelection;
	}

	storageManager.writeClosingTag(STRING_FOR_PERFORM_DEFAULTS_HOLD_TAG);
}

/// backup current layout, load saved layout, log layout change, update change status
void PerformanceSessionView::loadPerformanceViewLayout() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	backupPerformanceLayout();
	resetPerformanceView(modelStack);
	readDefaultsFromFile();
	logPerformanceLayoutChange();
	updateLayoutChangeStatus();
}

/// read defaults from XML
void PerformanceSessionView::readDefaultsFromFile() {
	FilePointer fp;
	//PerformanceView.XML
	bool success = storageManager.fileExists(STRING_FOR_PERFORM_DEFAULTS_XML, &fp);
	if (!success) {
		loadDefaultLayout();
		return;
	}

	//<defaults>
	int32_t error = storageManager.openXMLFile(&fp, STRING_FOR_PERFORM_DEFAULTS_TAG);
	if (error) {
		loadDefaultLayout();
		return;
	}

	char const* tagName;
	//step into the <defaultFXValues> tag
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, STRING_FOR_PERFORM_DEFAULTS_FXVALUES_TAG)) {
			readDefaultFXValuesFromFile();
		}
		storageManager.exitTag();
	}

	storageManager.closeFile();

	if (!successfullyReadDefaultsFromFile) {
		backupPerformanceLayout();
		logPerformanceLayoutChange();
	}

	successfullyReadDefaultsFromFile = true;
	uiNeedsRendering(this);
}

/// if no XML file exists, load default layout (paramKind, paramID, xDisplay, yDisplay, rowColour, rowTailColour)
void PerformanceSessionView::loadDefaultLayout() {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		memcpy(&layoutForPerformance[xDisplay], &defaultLayoutForPerformance[xDisplay], sizeParamsForPerformance);
		memcpy(&backupLayoutForPerformance[xDisplay], &defaultLayoutForPerformance[xDisplay], sizeParamsForPerformance);
		memcpy(&backupXMLDefaultLayoutForPerformance[xDisplay], &defaultLayoutForPerformance[xDisplay],
		       sizeParamsForPerformance);
	}
}

void PerformanceSessionView::readDefaultFXValuesFromFile() {
	char const* tagName;
	char tagNameFX[5];
	tagNameFX[0] = 'F';
	tagNameFX[1] = 'X';

	//loop through all FX number tags
	//<FX#>
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//find the FX number that the tag corresponds to
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			intToString(xDisplay + 1, &tagNameFX[2]);

			if (!strcmp(tagName, tagNameFX)) {
				readDefaultFXParamAndRowValuesFromFile(xDisplay);
				break;
			}
		}
		storageManager.exitTag();
	}
}

void PerformanceSessionView::readDefaultFXParamAndRowValuesFromFile(int32_t xDisplay) {
	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//<param>
		if (!strcmp(tagName, STRING_FOR_PERFORM_DEFAULTS_PARAM_TAG)) {
			readDefaultFXParamFromFile(xDisplay);
		}
		//<row>
		else if (!strcmp(tagName, STRING_FOR_PERFORM_DEFAULTS_ROW_TAG)) {
			readDefaultFXRowNumberValuesFromFile(xDisplay);
		}
		//<hold>
		else if (!strcmp(tagName, STRING_FOR_PERFORM_DEFAULTS_HOLD_TAG)) {
			readDefaultFXHoldStatusFromFile(xDisplay);
		}
		storageManager.exitTag();
	}
}

/// compares param name from <param> tag to the list of params available for use in performance view
/// if param is found, it loads the layout info for that param into the view (paramKind, paramID, xDisplay, yDisplay, rowColour, rowTailColour)
void PerformanceSessionView::readDefaultFXParamFromFile(int32_t xDisplay) {
	char const* paramName;
	char const* tagName = storageManager.readTagOrAttributeValue();

	for (int32_t i = 0; i < kNumParamsForPerformance; i++) {
		if (songParamsForPerformance[i].paramKind == Param::Kind::UNPATCHED_GLOBAL) {
			paramName = GlobalEffectable::paramToString(Param::Unpatched::START + songParamsForPerformance[i].paramID);
		}
		else if (songParamsForPerformance[i].paramKind == Param::Kind::UNPATCHED_SOUND) {
			paramName =
			    ModControllableAudio::paramToString(Param::Unpatched::START + songParamsForPerformance[i].paramID);
		}
		if (!strcmp(tagName, paramName)) {
			memcpy(&layoutForPerformance[xDisplay], &songParamsForPerformance[i], sizeParamsForPerformance);

			memcpy(&backupXMLDefaultLayoutForPerformance[xDisplay], &layoutForPerformance[xDisplay],
			       sizeParamsForPerformance);
			break;
		}
	}
}

void PerformanceSessionView::readDefaultFXRowNumberValuesFromFile(int32_t xDisplay) {
	char const* tagName;
	char rowNumber[5];
	//loop through all row <#> number tags
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//find the row number that the tag corresponds to
		//reads from row 8 down to row 1
		for (int32_t yDisplay = kDisplayHeight - 1; yDisplay >= 0; yDisplay--) {
			intToString(yDisplay + 1, rowNumber);
			if (!strcmp(tagName, rowNumber)) {
				defaultFXValues[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt() - kKnobPosOffset;

				//check if a value greater than 64 was entered as a default value in xml file
				if (defaultFXValues[xDisplay][yDisplay] > kKnobPosOffset) {
					defaultFXValues[xDisplay][yDisplay] = kKnobPosOffset;
				}

				backupXMLDefaultFXValues[xDisplay][yDisplay] = defaultFXValues[xDisplay][yDisplay];

				break;
			}
		}
		storageManager.exitTag();
	}
}

void PerformanceSessionView::readDefaultFXHoldStatusFromFile(int32_t xDisplay) {
	char const* tagName;
	//loop through the hold tags
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//<status>
		if (!strcmp(tagName, STRING_FOR_PERFORM_DEFAULTS_HOLD_STATUS_TAG)) {
			char const* holdStatus = storageManager.readTagOrAttributeValue();
			if (!strcmp(holdStatus, l10n::get(l10n::String::STRING_FOR_ON))) {
				if (!isParamStutter(layoutForPerformance[xDisplay].paramKind, layoutForPerformance[xDisplay].paramID)) {
					fxPress[xDisplay].padPressHeld = true;
					fxPress[xDisplay].timeLastPadPress = AudioEngine::audioSampleTimer;

					backupXMLDefaultFXPress[xDisplay].padPressHeld = fxPress[xDisplay].padPressHeld;
					backupXMLDefaultFXPress[xDisplay].timeLastPadPress = fxPress[xDisplay].timeLastPadPress;
				}
			}
		}
		//<row>
		else if (!strcmp(tagName, STRING_FOR_PERFORM_DEFAULTS_ROW_TAG)) {
			int32_t yDisplay = storageManager.readTagOrAttributeValueInt();
			if ((yDisplay >= 1) && (yDisplay <= 8)) {
				fxPress[xDisplay].yDisplay = yDisplay - 1;
				fxPress[xDisplay].currentKnobPosition = defaultFXValues[xDisplay][fxPress[xDisplay].yDisplay];

				backupXMLDefaultFXPress[xDisplay].yDisplay = fxPress[xDisplay].yDisplay;
			}
		}
		//<resetValue>
		else if (!strcmp(tagName, STRING_FOR_PERFORM_DEFAULTS_HOLD_RESETVALUE_TAG)) {
			fxPress[xDisplay].previousKnobPosition = storageManager.readTagOrAttributeValueInt() - kKnobPosOffset;
			//check if a value greater than 64 was entered as a default value in xml file
			if (fxPress[xDisplay].previousKnobPosition > kKnobPosOffset) {
				fxPress[xDisplay].previousKnobPosition = kKnobPosOffset;
			}
			backupXMLDefaultFXPress[xDisplay].previousKnobPosition = fxPress[xDisplay].previousKnobPosition;
		}
		storageManager.exitTag();
	}
	if (fxPress[xDisplay].padPressHeld) {
		//set the value associated with the held pad
		if ((fxPress[xDisplay].currentKnobPosition != kNoSelection)
		    && (fxPress[xDisplay].previousKnobPosition != kNoSelection)) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStack =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			if ((layoutForPerformance[xDisplay].paramKind != Param::Kind::NONE)
			    && (layoutForPerformance[xDisplay].paramID != kNoSelection)) {
				setParameterValue(modelStack, layoutForPerformance[xDisplay].paramKind,
				                  layoutForPerformance[xDisplay].paramID, xDisplay,
				                  defaultFXValues[xDisplay][fxPress[xDisplay].yDisplay], false);
			}
		}
	}
	else {
		initFXPress(fxPress[xDisplay]);
	}
}
