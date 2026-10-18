/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "gui/l10n/setting_names.h"

#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"

#include <algorithm>
#include <array>

namespace deluge::l10n {
namespace {

using enum String;

/// @name The choice lists a row's options are named by, shared where rows list the same options.
/// @{
constexpr std::array<String, 2> kOffOnChoices{{
    STRING_FOR_OFF,
    STRING_FOR_ON,
}};

constexpr std::array<String, 9> kArpChordTypeChoices{{
    STRING_FOR_NONE,
    STRING_FOR_FIFTH,
    STRING_FOR_SUS2,
    STRING_FOR_MINOR,
    STRING_FOR_MAJOR,
    STRING_FOR_SUS4,
    STRING_FOR_MINOR7,
    STRING_FOR_DOMINANT7,
    STRING_FOR_MAJOR7,
}};

constexpr std::array<String, 3> kArpMpeVelocityChoices{{
    STRING_FOR_DISABLED,
    STRING_FOR_PATCH_SOURCE_AFTERTOUCH,
    STRING_FOR_PATCH_SOURCE_Y,
}};

constexpr std::array<String, 9> kArpNoteModeChoices{{
    STRING_FOR_UP,
    STRING_FOR_DOWN,
    STRING_FOR_UP_DOWN,
    STRING_FOR_RANDOM,
    STRING_FOR_WALK1,
    STRING_FOR_WALK2,
    STRING_FOR_WALK3,
    STRING_FOR_AS_PLAYED,
    STRING_FOR_PATTERN,
}};

constexpr std::array<String, 5> kArpOctaveModeChoices{{
    STRING_FOR_UP,
    STRING_FOR_DOWN,
    STRING_FOR_UP_DOWN,
    STRING_FOR_ALTERNATE,
    STRING_FOR_RANDOM,
}};

constexpr std::array<String, 7> kArpPresetChoices{{
    STRING_FOR_OFF,
    STRING_FOR_UP,
    STRING_FOR_DOWN,
    STRING_FOR_BOTH,
    STRING_FOR_RANDOM,
    STRING_FOR_WALK,
    STRING_FOR_CUSTOM,
}};

constexpr std::array<String, 3> kDelayTypeChoices{{
    STRING_FOR_OFF,
    STRING_FOR_DIGITAL,
    STRING_FOR_ANALOG,
}};

constexpr std::array<String, 3> kDelayTypeShortChoices{{
    STRING_FOR_OFF,
    STRING_FOR_DIGITAL,
    STRING_FOR_ANALOG_SHORT,
}};

constexpr std::array<String, 3> kFilterRouteChoices{{
    STRING_FOR_HPF_TO_LPF,
    STRING_FOR_LPF_TO_HPF,
    STRING_FOR_PARALLEL,
}};

constexpr std::array<String, 4> kHpfModeChoices{{
    STRING_FOR_SVF_BAND,
    STRING_FOR_SVF_NOTCH,
    STRING_FOR_HPLADDER,
    STRING_FOR_OFF,
}};

constexpr std::array<String, 4> kHpfModeShortChoices{{
    STRING_FOR_SVF_BAND_SHORT,
    STRING_FOR_SVF_NOTCH_SHORT,
    STRING_FOR_12DB_LADDER_SHORT,
    STRING_FOR_OFF,
}};

constexpr std::array<String, 7> kLfoTypeChoices{{
    STRING_FOR_SINE,
    STRING_FOR_TRIANGLE,
    STRING_FOR_SQUARE,
    STRING_FOR_SAW,
    STRING_FOR_SAMPLE_AND_HOLD,
    STRING_FOR_RANDOM_WALK,
    STRING_FOR_WARBLE,
}};

constexpr std::array<String, 7> kLfoTypeShortChoices{{
    STRING_FOR_SINE,
    STRING_FOR_TRIANGLE,
    STRING_FOR_SQUARE,
    STRING_FOR_SAW,
    STRING_FOR_SAMPLE_AND_HOLD,
    STRING_FOR_RANDOM_WALK_SHORT,
    STRING_FOR_WARBLE,
}};

constexpr std::array<String, 2> kLinearInterpolationChoices{{
    STRING_FOR_LINEAR,
    STRING_FOR_SINC,
}};

constexpr std::array<String, 6> kLpfModeChoices{{
    STRING_FOR_12DB_LADDER,
    STRING_FOR_24DB_LADDER,
    STRING_FOR_DRIVE,
    STRING_FOR_SVF_BAND,
    STRING_FOR_SVF_NOTCH,
    STRING_FOR_OFF,
}};

constexpr std::array<String, 6> kLpfModeShortChoices{{
    STRING_FOR_12DB_LADDER_SHORT,
    STRING_FOR_24DB_LADDER_SHORT,
    STRING_FOR_DRIVE,
    STRING_FOR_SVF_BAND_SHORT,
    STRING_FOR_SVF_NOTCH_SHORT,
    STRING_FOR_OFF,
}};

constexpr std::array<String, 8> kModFXTypeChoices{{
    STRING_FOR_DISABLED,
    STRING_FOR_FLANGER,
    STRING_FOR_CHORUS,
    STRING_FOR_PHASER,
    STRING_FOR_STEREO_CHORUS,
    STRING_FOR_WARBLE,
    STRING_FOR_DIMENSION,
    STRING_FOR_GRAIN,
}};

constexpr std::array<String, 3> kModeChoices{{
    STRING_FOR_SUBTRACTIVE,
    STRING_FOR_FM,
    STRING_FOR_RINGMOD,
}};

constexpr std::array<String, 2> kModulator2ToModulator1Choices{{
    STRING_FOR_CARRIERS,
    STRING_FOR_MODULATOR_1,
}};

constexpr std::array<String, 4> kLoopModeChoices{{
    STRING_FOR_CUT,
    STRING_FOR_ONCE,
    STRING_FOR_LOOP,
    STRING_FOR_STRETCH,
}};

constexpr std::array<String, 2> kLinkedIndependentChoices{{
    STRING_FOR_LINKED,
    STRING_FOR_INDEPENDENT,
}};

constexpr std::array<String, 12> kOscTypeChoices{{
    STRING_FOR_SINE,
    STRING_FOR_TRIANGLE,
    STRING_FOR_SQUARE,
    STRING_FOR_ANALOG_SQUARE,
    STRING_FOR_SAW,
    STRING_FOR_ANALOG_SAW,
    STRING_FOR_WAVETABLE,
    STRING_FOR_SAMPLE,
    STRING_FOR_DX7,
    STRING_FOR_INPUT_LEFT,
    STRING_FOR_INPUT_RIGHT,
    STRING_FOR_INPUT_STEREO,
}};

constexpr std::array<String, 5> kPolyphonicChoices{{
    STRING_FOR_AUTO,
    STRING_FOR_POLYPHONIC,
    STRING_FOR_MONOPHONIC,
    STRING_FOR_LEGATO,
    STRING_FOR_CHOKE,
}};

constexpr std::array<String, 3> kPriorityChoices{{
    STRING_FOR_LOW,
    STRING_FOR_MEDIUM,
    STRING_FOR_HIGH,
}};

constexpr std::array<String, 3> kPriorityShortChoices{{
    STRING_FOR_LOW,
    STRING_FOR_MEDIUM_SHORT,
    STRING_FOR_HIGH,
}};

constexpr std::array<String, 3> kReverbModelChoices{{
    STRING_FOR_FREEVERB,
    STRING_FOR_MUTABLE,
    STRING_FOR_DIGITAL,
}};
/// @}

struct Entry {
	std::string_view path;
	SettingNames names;
};

/// Every setting row a menu shows, sorted by path. The audio clip's `transpose` is the instrument's: see
/// setting_names.
constexpr std::array<Entry, 105> kEntries{
    {{"arpeggiator/arpMode", {STRING_FOR_ARP_MODE_MENU_TITLE, STRING_FOR_ENABLED, kOffOnChoices, {}}},
     Entry{"arpeggiator/bassProbability",
           {STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE, STRING_FOR_BASS_PROBABILITY, {}, {}}},
     {"arpeggiator/chordPolyphony", {STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE, STRING_FOR_CHORD_POLYPHONY, {}, {}}},
     {"arpeggiator/chordProbability",
      {STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE, STRING_FOR_CHORD_PROBABILITY, {}, {}}},
     {"arpeggiator/chordType",
      {STRING_FOR_ARP_CHORD_SIMULATOR_MENU_TITLE, STRING_FOR_CHORD_SIMULATOR, kArpChordTypeChoices, {}}},
     {"arpeggiator/gate", {STRING_FOR_ARP_GATE_MENU_TITLE, STRING_FOR_GATE, {}, {}}},
     {"arpeggiator/glideProbability",
      {STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE, STRING_FOR_GLIDE_PROBABILITY, {}, {}}},
     {"arpeggiator/kitArp", {STRING_FOR_INCLUDE_IN_KIT_ARP, STRING_FOR_INCLUDE_IN_KIT_ARP, kOffOnChoices, {}}},
     {"arpeggiator/mpeVelocity", {STRING_FOR_VELOCITY, STRING_FOR_VELOCITY, kArpMpeVelocityChoices, {}}},
     {"arpeggiator/noteMode", {STRING_FOR_ARP_NOTE_MODE_MENU_TITLE, STRING_FOR_NOTE_MODE, kArpNoteModeChoices, {}}},
     {"arpeggiator/noteProbability", {STRING_FOR_NOTE_PROBABILITY_MENU_TITLE, STRING_FOR_NOTE_PROBABILITY, {}, {}}},
     {"arpeggiator/numOctaves", {STRING_FOR_ARP_OCTAVES_MENU_TITLE, STRING_FOR_NUMBER_OF_OCTAVES, {}, {}}},
     {"arpeggiator/octaveMode",
      {STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE, STRING_FOR_OCTAVE_MODE, kArpOctaveModeChoices, {}}},
     {"arpeggiator/preset", {STRING_FOR_ARP_PRESET_MENU_TITLE, STRING_FOR_PRESET, kArpPresetChoices, {}}},
     {"arpeggiator/randomizerLock",
      {STRING_FOR_ARP_RANDOMIZER_LOCK_TITLE, STRING_FOR_RANDOMIZER_LOCK, kOffOnChoices, {}}},
     {"arpeggiator/ratchetAmount", {STRING_FOR_ARP_RATCHETS_MENU_TITLE, STRING_FOR_NUMBER_OF_RATCHETS, {}, {}}},
     {"arpeggiator/ratchetProbability",
      {STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE, STRING_FOR_RATCHET_PROBABILITY, {}, {}}},
     {"arpeggiator/rate", {STRING_FOR_ARP_RATE_MENU_TITLE, STRING_FOR_RATE, {}, {}}},
     {"arpeggiator/rhythm", {STRING_FOR_ARP_RHYTHM_MENU_TITLE, STRING_FOR_RHYTHM, {}, {}}},
     {"arpeggiator/sequenceLength", {STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE, STRING_FOR_SEQUENCE_LENGTH, {}, {}}},
     {"arpeggiator/spreadGate", {STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE, STRING_FOR_SPREAD_GATE, {}, {}}},
     {"arpeggiator/spreadOctave", {STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE, STRING_FOR_SPREAD_OCTAVE, {}, {}}},
     {"arpeggiator/spreadVelocity", {STRING_FOR_SPREAD_VELOCITY_MENU_TITLE, STRING_FOR_SPREAD_VELOCITY, {}, {}}},
     {"arpeggiator/stepRepeat", {STRING_FOR_ARP_STEP_REPEAT_MENU_TITLE, STRING_FOR_STEP_REPEAT, {}, {}}},
     {"arpeggiator/swapProbability", {STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE, STRING_FOR_SWAP_PROBABILITY, {}, {}}},
     {"arpeggiator/syncLevel", {STRING_FOR_ARP_SYNC_MENU_TITLE, STRING_FOR_SYNC, {}, {}}},
     {"attack", {STRING_FOR_ATTACK, STRING_FOR_ATTACK, {}, {}}},
     {"audioCompressor/attack", {STRING_FOR_ATTACK, STRING_FOR_ATTACK, {}, {}}},
     {"audioCompressor/compBlend", {STRING_FOR_BLEND, STRING_FOR_BLEND, {}, {}}},
     {"audioCompressor/compHPF", {STRING_FOR_HPF, STRING_FOR_HPF, {}, {}}},
     {"audioCompressor/ratio", {STRING_FOR_RATIO, STRING_FOR_RATIO, {}, {}}},
     {"audioCompressor/release", {STRING_FOR_RELEASE, STRING_FOR_RELEASE, {}, {}}},
     {"bendRange", {STRING_FOR_NORMAL, STRING_FOR_NORMAL, {}, {}}},
     {"bendRangeMPE", {STRING_FOR_POLY_FINGER_MPE, STRING_FOR_POLY_FINGER_MPE, {}, {}}},
     {"clippingAmount", {STRING_FOR_SATURATION, STRING_FOR_SATURATION, {}, {}}},
     {"delay/pingPong", {STRING_FOR_DELAY_PINGPONG, STRING_FOR_PINGPONG, kOffOnChoices, {}}},
     {"delay/syncLevel", {STRING_FOR_DELAY_SYNC, STRING_FOR_SYNC, {}, {}}},
     {"delay/type", {STRING_FOR_DELAY_TYPE, STRING_FOR_TYPE, kDelayTypeChoices, kDelayTypeShortChoices}},
     {"filterRoute", {STRING_FOR_FILTER_ROUTE, STRING_FOR_FILTER_ROUTE, kFilterRouteChoices, {}}},
     {"hpfMode", {STRING_FOR_HPF_MODE, STRING_FOR_MODE, kHpfModeChoices, kHpfModeShortChoices}},
     {"lfo1/syncLevel", {STRING_FOR_LFO_SYNC, STRING_FOR_SYNC, {}, {}}},
     {"lfo1/type", {STRING_FOR_LFO_TYPE, STRING_FOR_SHAPE, kLfoTypeChoices, kLfoTypeShortChoices}},
     {"lfo2/syncLevel", {STRING_FOR_LFO_SYNC, STRING_FOR_SYNC, {}, {}}},
     {"lfo2/type", {STRING_FOR_LFO_TYPE, STRING_FOR_SHAPE, kLfoTypeChoices, kLfoTypeShortChoices}},
     {"lfo3/syncLevel", {STRING_FOR_LFO_SYNC, STRING_FOR_SYNC, {}, {}}},
     {"lfo3/type", {STRING_FOR_LFO_TYPE, STRING_FOR_SHAPE, kLfoTypeChoices, kLfoTypeShortChoices}},
     {"lfo4/syncLevel", {STRING_FOR_LFO_SYNC, STRING_FOR_SYNC, {}, {}}},
     {"lfo4/type", {STRING_FOR_LFO_TYPE, STRING_FOR_SHAPE, kLfoTypeChoices, kLfoTypeShortChoices}},
     {"linearInterpolation",
      {STRING_FOR_AUDIO_INTERPOLATION, STRING_FOR_INTERPOLATION, kLinearInterpolationChoices, {}}},
     {"lpfMode", {STRING_FOR_LPF_MODE, STRING_FOR_MODE, kLpfModeChoices, kLpfModeShortChoices}},
     {"maxVoices", {STRING_FOR_MAX_VOICES, STRING_FOR_MAX_VOICES, {}, {}}},
     {"midiBank", {STRING_FOR_MIDI_BANK, STRING_FOR_BANK, {}, {}}},
     {"midiOutput/channel", {STRING_FOR_CHANNEL, STRING_FOR_CHANNEL, {}, {}}},
     {"midiOutput/noteForDrum", {STRING_FOR_NOTE, STRING_FOR_NOTE, {}, {}}},
     {"midiPGM", {STRING_FOR_MIDI_PGM_NUMB_MENU_TITLE, STRING_FOR_PGM, {}, {}}},
     {"midiSub", {STRING_FOR_MIDI_SUB_BANK, STRING_FOR_SUB_BANK_SHORT, {}, {}}},
     {"modFXType", {STRING_FOR_MODFX_TYPE, STRING_FOR_TYPE, kModFXTypeChoices, {}}},
     {"mode", {STRING_FOR_SYNTH_MODE, STRING_FOR_SYNTH_MODE, kModeChoices, {}}},
     {"modulator1/retrigPhase", {STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, STRING_FOR_RETRIGGER_PHASE, {}, {}}},
     {"modulator1/transpose", {STRING_FOR_FM_MOD_TRAN_MENU_TITLE, STRING_FOR_TRANSPOSE, {}, {}}},
     {"modulator2/retrigPhase", {STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, STRING_FOR_RETRIGGER_PHASE, {}, {}}},
     {"modulator2/toModulator1",
      {STRING_FOR_FM_MOD2_DEST_MENU_TITLE, STRING_FOR_DESTINATION, kModulator2ToModulator1Choices, {}}},
     {"modulator2/transpose", {STRING_FOR_FM_MOD_TRAN_MENU_TITLE, STRING_FOR_TRANSPOSE, {}, {}}},
     {"osc1/linearInterpolation",
      {STRING_FOR_SAMP_INTERP_MENU_TITLE, STRING_FOR_INTERPOLATION, kLinearInterpolationChoices, {}}},
     {"osc1/loopMode", {EMPTY_STRING, STRING_FOR_REPEAT_MODE, kLoopModeChoices, {}}},
     {"osc1/retrigPhase", {STRING_FOR_OSC_R_PHASE_MENU_TITLE, STRING_FOR_RETRIGGER_PHASE, {}, {}}},
     {"osc1/reversed", {STRING_FOR_SAMP_REVERSE_MENU_TITLE, STRING_FOR_REVERSE, {}, {}}},
     {"osc1/timeStretchAmount", {STRING_FOR_SAMP_SPEED_MENU_TITLE, STRING_FOR_SPEED, {}, {}}},
     {"osc1/timeStretchEnable", {STRING_FOR_PITCH_SPEED, STRING_FOR_PITCH_SPEED, kLinkedIndependentChoices, {}}},
     {"osc1/transpose", {STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, STRING_FOR_TRANSPOSE, {}, {}}},
     {"osc1/type", {STRING_FOR_OSC_TYPE_MENU_TITLE, STRING_FOR_TYPE, kOscTypeChoices, {}}},
     {"osc2/linearInterpolation",
      {STRING_FOR_SAMP_INTERP_MENU_TITLE, STRING_FOR_INTERPOLATION, kLinearInterpolationChoices, {}}},
     {"osc2/loopMode", {EMPTY_STRING, STRING_FOR_REPEAT_MODE, kLoopModeChoices, {}}},
     {"osc2/oscillatorSync", {STRING_FOR_OSCILLATOR_SYNC, STRING_FOR_OSCILLATOR_SYNC, {}, {}}},
     {"osc2/retrigPhase", {STRING_FOR_OSC_R_PHASE_MENU_TITLE, STRING_FOR_RETRIGGER_PHASE, {}, {}}},
     {"osc2/reversed", {STRING_FOR_SAMP_REVERSE_MENU_TITLE, STRING_FOR_REVERSE, {}, {}}},
     {"osc2/timeStretchAmount", {STRING_FOR_SAMP_SPEED_MENU_TITLE, STRING_FOR_SPEED, {}, {}}},
     {"osc2/timeStretchEnable", {STRING_FOR_PITCH_SPEED, STRING_FOR_PITCH_SPEED, kLinkedIndependentChoices, {}}},
     {"osc2/transpose", {STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, STRING_FOR_TRANSPOSE, {}, {}}},
     {"osc2/type", {STRING_FOR_OSC_TYPE_MENU_TITLE, STRING_FOR_TYPE, kOscTypeChoices, {}}},
     {"pitchSpeedIndependent", {STRING_FOR_PITCH_SPEED, STRING_FOR_PITCH_SPEED, kLinkedIndependentChoices, {}}},
     {"polyphonic", {STRING_FOR_POLYPHONY, STRING_FOR_POLYPHONY, kPolyphonicChoices, {}}},
     {"priority", {STRING_FOR_PRIORITY, STRING_FOR_PRIORITY, kPriorityChoices, kPriorityShortChoices}},
     {"reverb/compressor/attack", {STRING_FOR_SIDECH_ATTACK_MENU_TITLE, STRING_FOR_ATTACK, {}, {}}},
     {"reverb/compressor/release", {STRING_FOR_SIDECH_RELEASE_MENU_TITLE, STRING_FOR_RELEASE, {}, {}}},
     {"reverb/compressor/shape", {STRING_FOR_SIDECH_SHAPE_MENU_TITLE, STRING_FOR_SHAPE, {}, {}}},
     {"reverb/dampening", {STRING_FOR_DAMPING, STRING_FOR_DAMPING, {}, {}}},
     {"reverb/hpf", {STRING_FOR_HPF, STRING_FOR_HPF, {}, {}}},
     {"reverb/lpf", {STRING_FOR_LPF, STRING_FOR_LPF, {}, {}}},
     {"reverb/model", {STRING_FOR_MODEL, STRING_FOR_MODEL, kReverbModelChoices, {}}},
     {"reverb/pan", {STRING_FOR_REVERB_PAN, STRING_FOR_PAN, {}, {}}},
     {"reverb/roomSize", {STRING_FOR_TIME, STRING_FOR_TIME, {}, {}}},
     {"reverb/width", {STRING_FOR_DIFFUSION, STRING_FOR_DIFFUSION, {}, {}}},
     {"reversed", {STRING_FOR_REVERSE, STRING_FOR_REVERSE, {}, {}}},
     {"sideChainSend", {STRING_FOR_SEND_TO_SIDECH_MENU_TITLE, STRING_FOR_SEND_TO_SIDECHAIN, {}, {}}},
     {"sidechain/attack", {STRING_FOR_SIDECH_ATTACK_MENU_TITLE, STRING_FOR_ATTACK, {}, {}}},
     {"sidechain/release", {STRING_FOR_SIDECH_RELEASE_MENU_TITLE, STRING_FOR_RELEASE, {}, {}}},
     {"sidechain/syncLevel", {STRING_FOR_SIDECHAIN_SYNC, STRING_FOR_SYNC, {}, {}}},
     {"stutter/quantized", {STRING_FOR_QUANTIZE, STRING_FOR_QUANTIZE, {}, {}}},
     {"swingInterval", {STRING_FOR_SWING_INTERVAL, STRING_FOR_SWING_INTERVAL, {}, {}}},
     {"transpose", {STRING_FOR_MASTER_TRAN_MENU_TITLE, STRING_FOR_MASTER_TRANSPOSE, {}, {}}},
     {"unison/detune", {STRING_FOR_UNISON_DETUNE_MENU_TITLE, STRING_FOR_UNISON_DETUNE, {}, {}}},
     {"unison/num", {STRING_FOR_UNISON_NUMBER_MENU_TITLE, STRING_FOR_UNISON_NUMBER, {}, {}}},
     {"unison/spread", {STRING_FOR_UNISON_STEREO_SPREAD_MENU_TITLE, STRING_FOR_UNISON_STEREO_SPREAD, {}, {}}},
     {"voicePriority", {STRING_FOR_PRIORITY, STRING_FOR_PRIORITY, kPriorityChoices, kPriorityShortChoices}}}};

static_assert(std::ranges::is_sorted(kEntries, {}, &Entry::path), "kEntries is searched by path: keep it sorted");

/// @brief A param's name; the table is sorted by id.
struct ParamName {
	uint32_t id;
	String name;
};

/// The names of the params the Deluge instrument and the global effectable share (their patched params, and the
/// unpatched ones they number alike), by id. Id 116 is not here: it names two params.
constexpr std::array<ParamName, 97> kParamNames{{
    {0, STRING_FOR_PARAM_LOCAL_OSC_A_VOLUME},              // oscAVolume
    {1, STRING_FOR_PARAM_LOCAL_OSC_B_VOLUME},              // oscBVolume
    {2, STRING_FOR_PARAM_LOCAL_VOLUME},                    // volume
    {3, STRING_FOR_PARAM_LOCAL_NOISE_VOLUME},              // noiseVolume
    {4, STRING_FOR_PARAM_LOCAL_MODULATOR_0_VOLUME},        // modulator1Volume
    {5, STRING_FOR_PARAM_LOCAL_MODULATOR_1_VOLUME},        // modulator2Volume
    {6, STRING_FOR_WAVEFOLDER},                            // waveFold
    {7, STRING_FOR_PARAM_LOCAL_MODULATOR_0_FEEDBACK},      // modulator1Feedback
    {8, STRING_FOR_PARAM_LOCAL_MODULATOR_1_FEEDBACK},      // modulator2Feedback
    {9, STRING_FOR_PARAM_LOCAL_CARRIER_0_FEEDBACK},        // carrier1Feedback
    {10, STRING_FOR_PARAM_LOCAL_CARRIER_1_FEEDBACK},       // carrier2Feedback
    {11, STRING_FOR_PARAM_LOCAL_LPF_RESONANCE},            // lpfResonance
    {12, STRING_FOR_PARAM_LOCAL_HPF_RESONANCE},            // hpfResonance
    {13, STRING_FOR_PARAM_LOCAL_ENV_0_SUSTAIN},            // env1Sustain
    {14, STRING_FOR_PARAM_LOCAL_ENV_1_SUSTAIN},            // env2Sustain
    {15, STRING_FOR_PARAM_LOCAL_ENV_2_SUSTAIN},            // env3Sustain
    {16, STRING_FOR_PARAM_LOCAL_ENV_3_SUSTAIN},            // env4Sustain
    {17, STRING_FOR_PARAM_LOCAL_LPF_MORPH},                // lpfMorph
    {18, STRING_FOR_PARAM_LOCAL_HPF_MORPH},                // hpfMorph
    {19, STRING_FOR_PARAM_LOCAL_OSC_A_PHASE_WIDTH},        // oscAPhaseWidth
    {20, STRING_FOR_PARAM_LOCAL_OSC_B_PHASE_WIDTH},        // oscBPhaseWidth
    {21, STRING_FOR_PARAM_LOCAL_OSC_A_WAVE_INDEX},         // oscAWavetablePosition
    {22, STRING_FOR_PARAM_LOCAL_OSC_B_WAVE_INDEX},         // oscBWavetablePosition
    {23, STRING_FOR_PARAM_LOCAL_PAN},                      // pan
    {24, STRING_FOR_PARAM_LOCAL_LPF_FREQ},                 // lpfFrequency
    {25, STRING_FOR_PARAM_LOCAL_PITCH_ADJUST},             // pitch
    {26, STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST},       // oscAPitch
    {27, STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST},       // oscBPitch
    {28, STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST}, // modulator1Pitch
    {29, STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST}, // modulator2Pitch
    {30, STRING_FOR_PARAM_LOCAL_HPF_FREQ},                 // hpfFrequency
    {31, STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ_1},         // lfo2Rate
    {32, STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ_2},         // lfo4Rate
    {33, STRING_FOR_PARAM_LOCAL_ENV_0_ATTACK},             // env1Attack
    {34, STRING_FOR_PARAM_LOCAL_ENV_1_ATTACK},             // env2Attack
    {35, STRING_FOR_PARAM_LOCAL_ENV_2_ATTACK},             // env3Attack
    {36, STRING_FOR_PARAM_LOCAL_ENV_3_ATTACK},             // env4Attack
    {37, STRING_FOR_PARAM_LOCAL_ENV_0_DECAY},              // env1Decay
    {38, STRING_FOR_PARAM_LOCAL_ENV_1_DECAY},              // env2Decay
    {39, STRING_FOR_PARAM_LOCAL_ENV_2_DECAY},              // env3Decay
    {40, STRING_FOR_PARAM_LOCAL_ENV_3_DECAY},              // env4Decay
    {41, STRING_FOR_PARAM_LOCAL_ENV_0_RELEASE},            // env1Release
    {42, STRING_FOR_PARAM_LOCAL_ENV_1_RELEASE},            // env2Release
    {43, STRING_FOR_PARAM_LOCAL_ENV_2_RELEASE},            // env3Release
    {44, STRING_FOR_PARAM_LOCAL_ENV_3_RELEASE},            // env4Release
    {45, STRING_FOR_PARAM_GLOBAL_VOLUME_POST_FX},          // volumePostFX
    {46, STRING_FOR_PARAM_GLOBAL_VOLUME_POST_REVERB_SEND}, // volumePostReverbSend
    {47, STRING_FOR_PARAM_GLOBAL_REVERB_AMOUNT},           // reverbAmount
    {48, STRING_FOR_PARAM_GLOBAL_MOD_FX_DEPTH},            // modFXDepth
    {49, STRING_FOR_PARAM_GLOBAL_DELAY_FEEDBACK},          // delayFeedback
    {50, STRING_FOR_PARAM_GLOBAL_DELAY_RATE},              // delayRate
    {51, STRING_FOR_PARAM_GLOBAL_MOD_FX_RATE},             // modFXRate
    {52, STRING_FOR_PARAM_GLOBAL_LFO_FREQ_1},              // lfo1Rate
    {53, STRING_FOR_PARAM_GLOBAL_LFO_FREQ_2},              // lfo3Rate
    {54, STRING_FOR_PARAM_GLOBAL_ARP_RATE},                // arpRate
    {90, STRING_FOR_STUTTER_RATE},                         // stutterRate
    {91, STRING_FOR_BASS},                                 // bass
    {92, STRING_FOR_TREBLE},                               // treble
    {93, STRING_FOR_BASS_FREQUENCY},                       // bassFreq
    {94, STRING_FOR_TREBLE_FREQUENCY},                     // trebleFreq
    {95, STRING_FOR_DECIMATION},                           // sampleRateReduction
    {96, STRING_FOR_BITCRUSH},                             // bitcrushAmount
    {97, STRING_FOR_MODFX_OFFSET},                         // modFXOffset
    {98, STRING_FOR_MODFX_FEEDBACK},                       // modFXFeedback
    {99, STRING_FOR_SIDECHAIN_SHAPE},                      // compressorShape
    {100, STRING_FOR_THRESHOLD},                           // compressorThreshold
    {101, STRING_FOR_ARP_GATE_MENU_TITLE},                 // arpGate
    {102, STRING_FOR_ARP_RHYTHM_MENU_TITLE},               // rhythm
    {103, STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE},      // sequenceLength
    {104, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE},      // chordPolyphony
    {105, STRING_FOR_ARP_RATCHETS_MENU_TITLE},             // ratchetAmount
    {106, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE},         // noteProbability
    {107, STRING_FOR_REVERSE_PROBABILITY_MENU_TITLE},      // reverseProbability
    {108, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE},     // bassProbability
    {109, STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE},     // swapProbability
    {110, STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE},    // glideProbability
    {111, STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE},    // chordProbability
    {112, STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE},  // ratchetProbability
    {113, STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE},          // spreadGate
    {114, STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE},        // spreadOctave
    {115, STRING_FOR_SPREAD_VELOCITY_MENU_TITLE},          // spreadVelocity
    {117, STRING_FOR_MOD_FX_DEPTH},                        // modFXDepth
    {118, STRING_FOR_DELAY_RATE},                          // delayRate
    {119, STRING_FOR_DELAY_AMOUNT},                        // delayFeedback
    {120, STRING_FOR_ARP_RATE_MENU_TITLE},                 // arpRate
    {121, STRING_FOR_PAN},                                 // pan
    {122, STRING_FOR_LPF_FREQUENCY},                       // lpfFrequency
    {123, STRING_FOR_LPF_RESONANCE},                       // lpfResonance
    {124, STRING_FOR_LPF_MORPH},                           // lpfMorph
    {125, STRING_FOR_HPF_FREQUENCY},                       // hpfFrequency
    {126, STRING_FOR_HPF_RESONANCE},                       // hpfResonance
    {127, STRING_FOR_HPF_MORPH},                           // hpfMorph
    {128, STRING_FOR_REVERB_AMOUNT},                       // reverbAmount
    {129, STRING_FOR_MASTER_LEVEL},                        // volume
    {130, STRING_FOR_SIDECHAIN_LEVEL},                     // sidechainCompressorVolume
    {131, STRING_FOR_MASTER_PITCH},                        // pitchAdjust
    {132, STRING_FOR_TEMPO},                               // tempo
}};

static_assert(std::ranges::is_sorted(kParamNames, {}, &ParamName::id), "kParamNames is searched by id: keep it sorted");

/// The one id the two engine tables give different params: the instrument's portamento, the global effectable's
/// mod-FX rate.
constexpr uint32_t kOverlappingId = 116;

static_assert(instrument::param::kPortamento.value == kOverlappingId
              && global_effectable::param::kModFXRate.value == kOverlappingId);

} // namespace

const SettingNames* setting_names(std::string_view path) {
	const auto it = std::ranges::lower_bound(kEntries, path, {}, &Entry::path);
	return it != kEntries.end() && it->path == path ? &it->names : nullptr;
}

String param_name(engine::ParamId id, const ParamNameContext& state) {
	const bool global = state.table == ParamTable::GLOBAL_EFFECTABLE;
	// The ladder filters repurpose morph, so legacy names it for what it does there; the SVF modes really morph.
	if (id == (global ? global_effectable::param::kLpfMorph : instrument::param::kLpfMorph)
	    && state.lpf_mode <= effects::FilterMode::TRANSISTOR_24DB_DRIVE) {
		return STRING_FOR_LPF_DRIVE;
	}
	if (id == (global ? global_effectable::param::kHpfMorph : instrument::param::kHpfMorph)
	    && state.hpf_mode == effects::FilterMode::HPLADDER) {
		return STRING_FOR_HPF_FM;
	}
	if (id.value == kOverlappingId) {
		return global ? STRING_FOR_MOD_FX_RATE : STRING_FOR_PORTAMENTO;
	}
	const auto it = std::ranges::lower_bound(kParamNames, id.value, {}, &ParamName::id);
	return it != kParamNames.end() && it->id == id.value ? it->name : STRING_FOR_NONE;
}

} // namespace deluge::l10n
