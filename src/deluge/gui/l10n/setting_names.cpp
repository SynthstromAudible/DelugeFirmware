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

#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "project/device_settings.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <utility>

namespace deluge::l10n {
namespace {
/// The menu-time tables: read when a menu or an Edit looks a setting up, never by the audio tier or an interrupt.
/// The device image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

using enum String;
using plugin::ParamId;
using project::SettingGroupId;

/// @name The choice lists a row's options are named by, shared where rows list the same options.
/// @{
constexpr std::array<String, 2> kArpeggiatorArpModeChoices{{
    STRING_FOR_OFF,
    STRING_FOR_ON,
}};

constexpr std::array<String, 9> kArpeggiatorChordTypeChoices{{
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

constexpr std::array<String, 3> kArpeggiatorMpeVelocityChoices{{
    STRING_FOR_DISABLED,
    STRING_FOR_PATCH_SOURCE_AFTERTOUCH,
    STRING_FOR_PATCH_SOURCE_Y,
}};

constexpr std::array<String, 9> kArpeggiatorNoteModeChoices{{
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

constexpr std::array<String, 5> kArpeggiatorOctaveModeChoices{{
    STRING_FOR_UP,
    STRING_FOR_DOWN,
    STRING_FOR_UP_DOWN,
    STRING_FOR_ALTERNATE,
    STRING_FOR_RANDOM,
}};

constexpr std::array<String, 7> kArpeggiatorPresetChoices{{
    STRING_FOR_OFF,
    STRING_FOR_UP,
    STRING_FOR_DOWN,
    STRING_FOR_BOTH,
    STRING_FOR_RANDOM,
    STRING_FOR_WALK,
    STRING_FOR_CUSTOM,
}};

constexpr std::array<String, 4> kCv2SourceChoices{{
    STRING_FOR_OFF,
    STRING_FOR_PATCH_SOURCE_Y,
    STRING_FOR_PATCH_SOURCE_AFTERTOUCH,
    STRING_FOR_VELOCITY,
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

constexpr std::array<String, 7> kLfo1TypeChoices{{
    STRING_FOR_SINE,
    STRING_FOR_TRIANGLE,
    STRING_FOR_SQUARE,
    STRING_FOR_SAW,
    STRING_FOR_SAMPLE_AND_HOLD,
    STRING_FOR_RANDOM_WALK,
    STRING_FOR_WARBLE,
}};

constexpr std::array<String, 7> kLfo1TypeShortChoices{{
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

constexpr std::array<String, 4> kModeChoices{{
    STRING_FOR_SUBTRACTIVE,
    STRING_FOR_FM,
    STRING_FOR_RINGMOD,
    STRING_FOR_DX7,
}};

constexpr std::array<String, 2> kModulator2ToModulator1Choices{{
    STRING_FOR_CARRIERS,
    STRING_FOR_MODULATOR_1,
}};

constexpr std::array<String, 4> kOsc1LoopModeChoices{{
    STRING_FOR_CUT,
    STRING_FOR_ONCE,
    STRING_FOR_LOOP,
    STRING_FOR_STRETCH,
}};

constexpr std::array<String, 2> kOsc1TimeStretchEnableChoices{{
    STRING_FOR_LINKED,
    STRING_FOR_INDEPENDENT,
}};

constexpr std::array<String, 13> kOsc1TypeChoices{{
    STRING_FOR_SINE,
    STRING_FOR_TRIANGLE,
    STRING_FOR_SQUARE,
    STRING_FOR_ANALOG_SQUARE,
    STRING_FOR_SAW,
    STRING_FOR_ANALOG_SAW,
    STRING_FOR_SUPERSAW,
    STRING_FOR_WAVETABLE,
    STRING_FOR_SAMPLE,
    STRING_FOR_DX7,
    STRING_FOR_INPUT_LEFT,
    STRING_FOR_INPUT_RIGHT,
    STRING_FOR_INPUT_STEREO,
}};

/// With no audio input plugged in, the menu lists the left input's choice alone, as "Input".
constexpr std::array<String, 13> kOsc1TypeUnpluggedChoices{{
    STRING_FOR_SINE,
    STRING_FOR_TRIANGLE,
    STRING_FOR_SQUARE,
    STRING_FOR_ANALOG_SQUARE,
    STRING_FOR_SAW,
    STRING_FOR_ANALOG_SAW,
    STRING_FOR_SUPERSAW,
    STRING_FOR_WAVETABLE,
    STRING_FOR_SAMPLE,
    STRING_FOR_DX7,
    STRING_FOR_INPUT,
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

constexpr std::array<String, 4> kSequenceDirectionChoices{{
    STRING_FOR_FORWARD,
    STRING_FOR_REVERSED,
    STRING_FOR_PING_PONG,
    STRING_FOR_NONE,
}};
/// @}

/// @name The names rows have, shared by every owner and path that shows a row alike.
/// @{
constexpr SettingNames kArpeggiatorArpModeNames{
    STRING_FOR_ARP_MODE_MENU_TITLE, STRING_FOR_ENABLED, kArpeggiatorArpModeChoices, {}};
constexpr SettingNames kArpeggiatorBassProbabilityNames{
    STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE, STRING_FOR_BASS_PROBABILITY, {}, {}};
constexpr SettingNames kArpeggiatorChordPolyphonyNames{
    STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE, STRING_FOR_CHORD_POLYPHONY, {}, {}};
constexpr SettingNames kArpeggiatorChordProbabilityNames{
    STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE, STRING_FOR_CHORD_PROBABILITY, {}, {}};
constexpr SettingNames kArpeggiatorChordTypeNames{
    STRING_FOR_ARP_CHORD_SIMULATOR_MENU_TITLE, STRING_FOR_CHORD_SIMULATOR, kArpeggiatorChordTypeChoices, {}};
constexpr SettingNames kArpeggiatorGateNames{STRING_FOR_ARP_GATE_MENU_TITLE, STRING_FOR_GATE, {}, {}};
constexpr SettingNames kArpeggiatorGlideProbabilityNames{
    STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE, STRING_FOR_GLIDE_PROBABILITY, {}, {}};
constexpr SettingNames kArpeggiatorKitArpNames{
    STRING_FOR_INCLUDE_IN_KIT_ARP, STRING_FOR_INCLUDE_IN_KIT_ARP, kArpeggiatorArpModeChoices, {}};
constexpr SettingNames kArpeggiatorMpeVelocityNames{
    STRING_FOR_VELOCITY, STRING_FOR_VELOCITY, kArpeggiatorMpeVelocityChoices, {}};
constexpr SettingNames kArpeggiatorNoteModeNames{
    STRING_FOR_ARP_NOTE_MODE_MENU_TITLE, STRING_FOR_NOTE_MODE, kArpeggiatorNoteModeChoices, {}, true};
constexpr SettingNames kArpeggiatorNoteProbabilityNames{
    STRING_FOR_NOTE_PROBABILITY_MENU_TITLE, STRING_FOR_NOTE_PROBABILITY, {}, {}};
constexpr SettingNames kArpeggiatorNumOctavesNames{
    STRING_FOR_ARP_OCTAVES_MENU_TITLE, STRING_FOR_NUMBER_OF_OCTAVES, {}, {}};
constexpr SettingNames kArpeggiatorOctaveModeNames{
    STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE, STRING_FOR_OCTAVE_MODE, kArpeggiatorOctaveModeChoices, {}, true};
constexpr SettingNames kArpeggiatorPresetNames{
    STRING_FOR_ARP_PRESET_MENU_TITLE, STRING_FOR_PRESET, kArpeggiatorPresetChoices, {}};
constexpr SettingNames kArpeggiatorRandomizerLockNames{
    STRING_FOR_ARP_RANDOMIZER_LOCK_TITLE, STRING_FOR_RANDOMIZER_LOCK, kArpeggiatorArpModeChoices, {}};
constexpr SettingNames kArpeggiatorRatchetAmountNames{
    STRING_FOR_ARP_RATCHETS_MENU_TITLE, STRING_FOR_NUMBER_OF_RATCHETS, {}, {}};
constexpr SettingNames kArpeggiatorRatchetProbabilityNames{
    STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE, STRING_FOR_RATCHET_PROBABILITY, {}, {}};
constexpr SettingNames kArpeggiatorRateNames{STRING_FOR_ARP_RATE_MENU_TITLE, STRING_FOR_RATE, {}, {}};
constexpr SettingNames kArpeggiatorRhythmNames{STRING_FOR_ARP_RHYTHM_MENU_TITLE, STRING_FOR_RHYTHM, {}, {}};
constexpr SettingNames kArpeggiatorSequenceLengthNames{
    STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE, STRING_FOR_SEQUENCE_LENGTH, {}, {}};
constexpr SettingNames kArpeggiatorSpreadGateNames{
    STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE, STRING_FOR_SPREAD_GATE, {}, {}};
constexpr SettingNames kArpeggiatorSpreadOctaveNames{
    STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE, STRING_FOR_SPREAD_OCTAVE, {}, {}};
constexpr SettingNames kArpeggiatorSpreadVelocityNames{
    STRING_FOR_SPREAD_VELOCITY_MENU_TITLE, STRING_FOR_SPREAD_VELOCITY, {}, {}};
constexpr SettingNames kArpeggiatorStepRepeatNames{
    STRING_FOR_ARP_STEP_REPEAT_MENU_TITLE, STRING_FOR_STEP_REPEAT, {}, {}};
constexpr SettingNames kArpeggiatorSwapProbabilityNames{
    STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE, STRING_FOR_SWAP_PROBABILITY, {}, {}};
constexpr SettingNames kArpeggiatorSyncLevelNames{STRING_FOR_ARP_SYNC_MENU_TITLE, STRING_FOR_SYNC, {}, {}};
constexpr SettingNames kAttackNames{STRING_FOR_ATTACK, STRING_FOR_ATTACK, {}, {}};
constexpr SettingNames kAudioCompressorCompBlendNames{STRING_FOR_BLEND, STRING_FOR_BLEND, {}, {}};
constexpr SettingNames kAudioCompressorCompHPFNames{STRING_FOR_HPF, STRING_FOR_HPF, {}, {}};
constexpr SettingNames kAudioCompressorRatioNames{STRING_FOR_RATIO, STRING_FOR_RATIO, {}, {}};
constexpr SettingNames kAudioCompressorReleaseNames{STRING_FOR_RELEASE, STRING_FOR_RELEASE, {}, {}};
constexpr SettingNames kBendRangeNames{STRING_FOR_NORMAL, STRING_FOR_NORMAL, {}, {}};
/// A kit row's own bend range: `drumBendRangeMenu`, the one bend item a kit row shows.
constexpr SettingNames kDrumBendRangeNames{STRING_FOR_BEND_RANGE, STRING_FOR_BEND_RANGE, {}, {}};
constexpr SettingNames kBendRangeNames2{STRING_FOR_POLY_FINGER_MPE, STRING_FOR_POLY_FINGER_MPE, {}, {}};
constexpr SettingNames kClippingAmountNames{STRING_FOR_SATURATION, STRING_FOR_SATURATION, {}, {}};
constexpr SettingNames kCollapseAftertouchNames{
    STRING_FOR_PATCH_SOURCE_AFTERTOUCH, STRING_FOR_PATCH_SOURCE_AFTERTOUCH, {}, {}};
constexpr SettingNames kCollapseMpeNames{STRING_FOR_MPE, STRING_FOR_MPE, {}, {}};
constexpr SettingNames kCv2SourceNames{STRING_FOR_CV2_SOURCE, STRING_FOR_CV2_SOURCE, kCv2SourceChoices, {}};
constexpr SettingNames kDelayPingPongNames{
    STRING_FOR_DELAY_PINGPONG, STRING_FOR_PINGPONG, kArpeggiatorArpModeChoices, {}};
constexpr SettingNames kDelaySyncLevelNames{STRING_FOR_DELAY_SYNC, STRING_FOR_SYNC, {}, {}};
constexpr SettingNames kDelayTypeNames{STRING_FOR_DELAY_TYPE, STRING_FOR_TYPE, kDelayTypeChoices,
                                       kDelayTypeShortChoices};
constexpr std::array<String, 4> kEnvelopeModeChoices{{STRING_FOR_ADSR, STRING_FOR_AHD, STRING_FOR_AR, STRING_FOR_MPE}};
constexpr std::array<String, 2> kEnvelopeFollowChoices{{STRING_FOR_ENV_FOLLOW_Z, STRING_FOR_ENV_FOLLOW_Y}};
constexpr std::array<String, 2> kEnvelopeFollowShortChoices{
    {STRING_FOR_ENV_FOLLOW_Z_SHORT, STRING_FOR_ENV_FOLLOW_Y_SHORT}};
constexpr SettingNames kEnvelopeModeNames{STRING_FOR_ENV_MODE_MENU_TITLE, STRING_FOR_MODE, kEnvelopeModeChoices, {}};
constexpr SettingNames kEnvelopeCurveNames{STRING_FOR_ENV_CURVE_MENU_TITLE, STRING_FOR_CURVE, {}, {}};
constexpr SettingNames kEnvelopeFollowNames{STRING_FOR_ENV_FOLLOW_MENU_TITLE, STRING_FOR_FOLLOW, kEnvelopeFollowChoices,
                                            kEnvelopeFollowShortChoices};
constexpr SettingNames kDeviceDefinitionFileNameNames{
    STRING_FOR_MIDI_DEVICE_DEFINITION_LINKED, STRING_FOR_MIDI_DEVICE_DEFINITION_LINKED, {}, {}};
constexpr SettingNames kFilterRouteNames{STRING_FOR_FILTER_ROUTE, STRING_FOR_FILTER_ROUTE, kFilterRouteChoices, {}};
constexpr SettingNames kHideUnlabeledCCNames{
    STRING_FOR_MIDI_DEVICE_DEFINITION_HIDE, STRING_FOR_MIDI_DEVICE_DEFINITION_HIDE, {}, {}};
constexpr SettingNames kHpfModeNames{STRING_FOR_HPF_MODE, STRING_FOR_MODE, kHpfModeChoices, kHpfModeShortChoices};
constexpr SettingNames kInputChannelNames{STRING_FOR_AUDIO_SOURCE, STRING_FOR_AUDIO_SOURCE, {}, {}};
constexpr SettingNames kLfo1SyncLevelNames{STRING_FOR_LFO_SYNC, STRING_FOR_SYNC, {}, {}};
constexpr SettingNames kLfo1TypeNames{STRING_FOR_LFO_TYPE, STRING_FOR_SHAPE, kLfo1TypeChoices, kLfo1TypeShortChoices};
constexpr SettingNames kLinearInterpolationNames{
    STRING_FOR_AUDIO_INTERPOLATION, STRING_FOR_INTERPOLATION, kLinearInterpolationChoices, {}};
constexpr SettingNames kLpfModeNames{STRING_FOR_LPF_MODE, STRING_FOR_MODE, kLpfModeChoices, kLpfModeShortChoices};
constexpr SettingNames kMaxVoicesNames{STRING_FOR_MAX_VOICES, STRING_FOR_MAX_VOICES, {}, {}};
constexpr SettingNames kMidiBankNames{STRING_FOR_MIDI_BANK, STRING_FOR_BANK, {}, {}};
constexpr SettingNames kMidiOutputChannelNames{STRING_FOR_CHANNEL, STRING_FOR_CHANNEL, {}, {}};
constexpr SettingNames kMidiOutputNoteForDrumNames{STRING_FOR_NOTE, STRING_FOR_NOTE, {}, {}};
constexpr SettingNames kMidiPGMNames{STRING_FOR_MIDI_PGM_NUMB_MENU_TITLE, STRING_FOR_PGM, {}, {}};
constexpr SettingNames kMidiSubNames{STRING_FOR_MIDI_SUB_BANK, STRING_FOR_SUB_BANK_SHORT, {}, {}};
constexpr SettingNames kModFXTypeNames{STRING_FOR_MODFX_TYPE, STRING_FOR_TYPE, kModFXTypeChoices, {}};
constexpr SettingNames kModeNames{STRING_FOR_SYNTH_MODE, STRING_FOR_SYNTH_MODE, kModeChoices, {}};
constexpr SettingNames kModulator1RetrigPhaseNames{
    STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, STRING_FOR_RETRIGGER_PHASE, {}, {}};
constexpr SettingNames kModulator1TransposeNames{STRING_FOR_FM_MOD_TRAN_MENU_TITLE, STRING_FOR_TRANSPOSE, {}, {}};
constexpr SettingNames kModulator2ToModulator1Names{
    STRING_FOR_FM_MOD2_DEST_MENU_TITLE, STRING_FOR_DESTINATION, kModulator2ToModulator1Choices, {}, true};
constexpr SettingNames kOsc1LinearInterpolationNames{
    STRING_FOR_SAMP_INTERP_MENU_TITLE, STRING_FOR_INTERPOLATION, kLinearInterpolationChoices, {}};
constexpr SettingNames kOsc1LoopModeNames{
    STRING_FOR_SAMP_REPEAT_MENU_TITLE, STRING_FOR_REPEAT_MODE, kOsc1LoopModeChoices, {}};
constexpr SettingNames kOsc1RetrigPhaseNames{STRING_FOR_OSC_R_PHASE_MENU_TITLE, STRING_FOR_RETRIGGER_PHASE, {}, {}};
constexpr SettingNames kOsc1ReversedNames{STRING_FOR_SAMP_REVERSE_MENU_TITLE, STRING_FOR_REVERSE, {}, {}};
constexpr SettingNames kOsc1TimeStretchAmountNames{STRING_FOR_SAMP_SPEED_MENU_TITLE, STRING_FOR_SPEED, {}, {}};
constexpr SettingNames kOsc1TimeStretchEnableNames{
    STRING_FOR_PITCH_SPEED, STRING_FOR_PITCH_SPEED, kOsc1TimeStretchEnableChoices, {}};
constexpr SettingNames kOsc1TransposeNames{STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, STRING_FOR_TRANSPOSE, {}, {}};
constexpr SettingNames kOsc1TypeNames{STRING_FOR_OSC_TYPE_MENU_TITLE, STRING_FOR_TYPE, kOsc1TypeChoices, {}};
constexpr SettingNames kOsc1TypeUnpluggedNames{
    STRING_FOR_OSC_TYPE_MENU_TITLE, STRING_FOR_TYPE, kOsc1TypeUnpluggedChoices, {}};
constexpr SettingNames kOsc2OscillatorSyncNames{STRING_FOR_OSCILLATOR_SYNC, STRING_FOR_OSCILLATOR_SYNC, {}, {}};
constexpr SettingNames kOutputMpeYNames{STRING_FOR_Y_AXIS_CONVERSION, STRING_FOR_Y_AXIS_CONVERSION, {}, {}};
constexpr SettingNames kOutputRecordingFromNames{STRING_FOR_TRACK, STRING_FOR_TRACK, {}, {}};
constexpr SettingNames kPolyphonicNames{STRING_FOR_POLYPHONY, STRING_FOR_POLYPHONY, kPolyphonicChoices, {}};
constexpr SettingNames kPriorityNames{STRING_FOR_PRIORITY, STRING_FOR_PRIORITY, kPriorityChoices,
                                      kPriorityShortChoices};
constexpr SettingNames kReverbCompressorAttackNames{STRING_FOR_SIDECH_ATTACK_MENU_TITLE, STRING_FOR_ATTACK, {}, {}};
constexpr SettingNames kReverbCompressorReleaseNames{STRING_FOR_SIDECH_RELEASE_MENU_TITLE, STRING_FOR_RELEASE, {}, {}};
constexpr SettingNames kReverbCompressorShapeNames{STRING_FOR_SIDECH_SHAPE_MENU_TITLE, STRING_FOR_SHAPE, {}, {}};
constexpr SettingNames kReverbDampeningNames{STRING_FOR_DAMPING, STRING_FOR_DAMPING, {}, {}};
constexpr SettingNames kReverbLpfNames{STRING_FOR_LPF, STRING_FOR_LPF, {}, {}};
constexpr SettingNames kReverbModelNames{STRING_FOR_MODEL, STRING_FOR_MODEL, kReverbModelChoices, {}};
constexpr SettingNames kReverbPanNames{STRING_FOR_REVERB_PAN, STRING_FOR_PAN, {}, {}};
constexpr SettingNames kReverbRoomSizeNames{STRING_FOR_TIME, STRING_FOR_TIME, {}, {}};
constexpr SettingNames kReverbWidthNames{STRING_FOR_DIFFUSION, STRING_FOR_DIFFUSION, {}, {}};
constexpr SettingNames kReversedNames{STRING_FOR_REVERSE, STRING_FOR_REVERSE, {}, {}};
constexpr SettingNames kSequenceDirectionNames{
    STRING_FOR_PLAY_DIRECTION, STRING_FOR_PLAY_DIRECTION, kSequenceDirectionChoices, {}};
/// A clip's directions are the first three: it has no parent to obey.
constexpr SettingNames kClipSequenceDirectionNames{
    STRING_FOR_PLAY_DIRECTION, STRING_FOR_PLAY_DIRECTION, std::span{kSequenceDirectionChoices}.first(3), {}};
constexpr SettingNames kSideChainSendNames{STRING_FOR_SEND_TO_SIDECH_MENU_TITLE, STRING_FOR_SEND_TO_SIDECHAIN, {}, {}};
constexpr SettingNames kSidechainSyncLevelNames{STRING_FOR_SIDECHAIN_SYNC, STRING_FOR_SYNC, {}, {}};
constexpr SettingNames kStutterQuantizedNames{STRING_FOR_QUANTIZE, STRING_FOR_QUANTIZE, {}, {}};
constexpr SettingNames kSwingIntervalNames{STRING_FOR_SWING_INTERVAL, STRING_FOR_SWING_INTERVAL, {}, {}};
constexpr SettingNames kTransposeNames{STRING_FOR_MASTER_TRAN_MENU_TITLE, STRING_FOR_MASTER_TRANSPOSE, {}, {}};
constexpr SettingNames kTransposeNames2{STRING_FOR_TRANSPOSE, STRING_FOR_TRANSPOSE, {}, {}};
constexpr SettingNames kUnisonDetuneNames{STRING_FOR_UNISON_DETUNE_MENU_TITLE, STRING_FOR_UNISON_DETUNE, {}, {}};
constexpr SettingNames kUnisonNumNames{STRING_FOR_UNISON_NUMBER_MENU_TITLE, STRING_FOR_UNISON_NUMBER, {}, {}};
constexpr SettingNames kUnisonSpreadNames{
    STRING_FOR_UNISON_STEREO_SPREAD_MENU_TITLE, STRING_FOR_UNISON_STEREO_SPREAD, {}, {}};

/// @name The device's own settings' names.
/// @{
constexpr std::array<String, 2> kPatchCablePolarityChoices{{STRING_FOR_POLARITY_UNIPOLAR, STRING_FOR_POLARITY_BIPOLAR}};
constexpr SettingNames kPatchCablePolarityNames{
    STRING_FOR_DEFAULT_POLARITY, STRING_FOR_DEFAULT_POLARITY, kPatchCablePolarityChoices, {}};
constexpr SettingNames kUseSharpsNames{STRING_FOR_DEFAULT_UI_SHARPS, STRING_FOR_DEFAULT_UI_SHARPS, {}, {}};
/// The defaults page's rows. A default range's two ends are named as their one item is.
constexpr SettingNames kDefaultTempoNames{STRING_FOR_DEFAULT_TEMPO, STRING_FOR_TEMPO, {}, {}};
constexpr SettingNames kDefaultSwingNames{STRING_FOR_DEFAULT_SWING, STRING_FOR_SWING_AMOUNT, {}, {}};
constexpr SettingNames kDefaultKeyNames{STRING_FOR_DEFAULT_KEY, STRING_FOR_KEY, {}, {}};
constexpr SettingNames kDefaultMagnitudeNames{STRING_FOR_DEFAULT_RESOL_MENU_TITLE, STRING_FOR_RESOLUTION, {}, {}};
constexpr SettingNames kDefaultSwingIntervalNames{STRING_FOR_DEFAULT_SWING, STRING_FOR_SWING_INTERVAL, {}, {}};
/// The preset scales in their own order, then random and none: the scales' own names.
constexpr std::array<String, 18> kDefaultScaleChoices{{
    STRING_FOR_SCALE_MAJOR,
    STRING_FOR_SCALE_MINOR,
    STRING_FOR_SCALE_DORIAN,
    STRING_FOR_SCALE_PHRYGIAN,
    STRING_FOR_SCALE_LYDIAN,
    STRING_FOR_SCALE_MIXOLYDIAN,
    STRING_FOR_SCALE_LOCRIAN,
    STRING_FOR_SCALE_MELODIC_MINOR,
    STRING_FOR_SCALE_HARMONIC_MINOR,
    STRING_FOR_SCALE_HUNGARIAN_MINOR,
    STRING_FOR_SCALE_MARVA,
    STRING_FOR_SCALE_ARABIAN,
    STRING_FOR_SCALE_WHOLE_TONE,
    STRING_FOR_SCALE_BLUES,
    STRING_FOR_SCALE_PENTATONIC_MINOR,
    STRING_FOR_SCALE_HIRAJOSHI,
    STRING_FOR_SCALE_RANDOM,
    STRING_FOR_SCALE_NONE,
}};
constexpr SettingNames kDefaultScaleNames{STRING_FOR_INIT_SCALE, STRING_FOR_INIT_SCALE, kDefaultScaleChoices, {}};
constexpr SettingNames kDefaultVelocityNames{STRING_FOR_DEFAULT_VELOC_MENU_TITLE, STRING_FOR_VELOCITY, {}, {}};
constexpr SettingNames kDefaultBendRangeNames{STRING_FOR_DEFAULT_BEND_R, STRING_FOR_BEND_RANGE, {}, {}};
constexpr SettingNames kDefaultMetronomeVolumeNames{STRING_FOR_DEFAULT_METRO_MENU_TITLE, STRING_FOR_METRONOME, {}, {}};
constexpr std::array<String, 4> kDefaultStartupSongChoices{{
    STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE_BLANK,
    STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE_TEMPLATE,
    STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE_LAST_OPENED,
    STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE_LAST_SAVED,
}};
constexpr SettingNames kDefaultStartupSongNames{STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE,
                                                STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE,
                                                kDefaultStartupSongChoices,
                                                {}};
constexpr std::array<String, 4> kDefaultSliceModeChoices{{
    STRING_FOR_CUT,
    STRING_FOR_ONCE,
    STRING_FOR_LOOP,
    STRING_FOR_STRETCH,
}};
constexpr SettingNames kDefaultSliceModeNames{
    STRING_FOR_DEFAULT_SLICE_MODE_MENU_TITLE, STRING_FOR_DEFAULT_SLICE_MODE, kDefaultSliceModeChoices, {}};
constexpr SettingNames kDefaultCpuIndicatorNames{
    STRING_FOR_DEFAULT_HIGH_CPU_USAGE_INDICATOR, STRING_FOR_DEFAULT_HIGH_CPU_USAGE_INDICATOR, {}, {}};
constexpr SettingNames kDefaultHoldTimeNames{STRING_FOR_HOLD_TIME, STRING_FOR_HOLD_TIME, {}, {}};
constexpr SettingNames kDefaultRecordQuantizeNames{STRING_FOR_QUANTIZATION, STRING_FOR_QUANTIZATION, {}, {}};
constexpr SettingNames kDefaultRecordMarginsNames{STRING_FOR_LOOP_MARGINS, STRING_FOR_LOOP_MARGINS, {}, {}};
constexpr std::array<String, 4> kDefaultThresholdRecordingChoices{{
    STRING_FOR_DISABLED,
    STRING_FOR_LOW,
    STRING_FOR_MEDIUM,
    STRING_FOR_HIGH,
}};
constexpr SettingNames kDefaultThresholdRecordingNames{
    STRING_FOR_MODE, STRING_FOR_MODE, kDefaultThresholdRecordingChoices, {}};
constexpr std::array<String, 5> kDefaultNewClipTypeChoices{{
    STRING_FOR_NEW_CLIP_TYPE_SYNTH,
    STRING_FOR_NEW_CLIP_TYPE_KIT,
    STRING_FOR_NEW_CLIP_TYPE_MIDI,
    STRING_FOR_NEW_CLIP_TYPE_CV,
    STRING_FOR_NEW_CLIP_TYPE_AUDIO,
}};
constexpr SettingNames kDefaultNewClipTypeNames{
    STRING_FOR_DEFAULT_NEW_CLIP_TYPE, STRING_FOR_DEFAULT_NEW_CLIP_TYPE, kDefaultNewClipTypeChoices, {}};
constexpr SettingNames kDefaultUseLastClipTypeNames{
    STRING_FOR_DEFAULT_USE_LAST_CLIP_TYPE, STRING_FOR_DEFAULT_USE_LAST_CLIP_TYPE, {}, {}};
constexpr SettingNames kDefaultAutomationInterpolateNames{
    STRING_FOR_DEFAULT_AUTOMATION_INTERPOLATION, STRING_FOR_DEFAULT_AUTOMATION_INTERPOLATION, {}, {}};
constexpr SettingNames kDefaultAutomationClearNames{
    STRING_FOR_DEFAULT_AUTOMATION_CLEAR, STRING_FOR_DEFAULT_AUTOMATION_CLEAR, {}, {}};
constexpr SettingNames kDefaultAutomationShiftNames{
    STRING_FOR_DEFAULT_AUTOMATION_SHIFT, STRING_FOR_DEFAULT_AUTOMATION_SHIFT, {}, {}};
constexpr SettingNames kDefaultAutomationNudgeNoteNames{
    STRING_FOR_DEFAULT_AUTOMATION_NUDGE_NOTE, STRING_FOR_DEFAULT_AUTOMATION_NUDGE_NOTE, {}, {}};
constexpr SettingNames kDefaultAutomationNoAuditionShortcutsNames{
    STRING_FOR_DEFAULT_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS,
    STRING_FOR_DEFAULT_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS,
    {},
    {}};
/// The play cursor's forms, in the row's order: fast, off, slow.
constexpr std::array<String, 3> kPlayCursorChoices{{STRING_FOR_FAST, STRING_FOR_DISABLED, STRING_FOR_SLOW}};
constexpr SettingNames kPlayCursorNames{STRING_FOR_PLAY_CURSOR, STRING_FOR_PLAY_CURSOR, kPlayCursorChoices, {}};
constexpr std::array<String, 2> kShortcutsVersionChoices{
    {STRING_FOR_SHORTCUTS_VERSION_1, STRING_FOR_SHORTCUTS_VERSION_3}};
constexpr SettingNames kShortcutsVersionNames{
    STRING_FOR_SHORTCUTS_VER_MENU_TITLE, STRING_FOR_SHORTCUTS_VERSION, kShortcutsVersionChoices, {}};
/// The pads' and the screen's rows, each named as its menu item is.
constexpr std::array<String, 3> kTextKeyboardChoices{
    {STRING_FOR_TEXT_KEYBOARD_QWERTY, STRING_FOR_TEXT_KEYBOARD_AZERTY, STRING_FOR_TEXT_KEYBOARD_QWERTZ}};
constexpr SettingNames kTextKeyboardNames{
    STRING_FOR_KEY_LAYOUT, STRING_FOR_KEYBOARD_FOR_TEXT, kTextKeyboardChoices, {}};
/// The nine pad colours, in the rows' order.
constexpr std::array<String, 9> kPadColourChoices{{
    STRING_FOR_RED,
    STRING_FOR_GREEN,
    STRING_FOR_BLUE,
    STRING_FOR_YELLOW,
    STRING_FOR_CYAN,
    STRING_FOR_MAGENTA,
    STRING_FOR_AMBER,
    STRING_FOR_WHITE,
    STRING_FOR_PINK,
}};
constexpr SettingNames kPadColourActiveNames{STRING_FOR_ACTIVE, STRING_FOR_ACTIVE, kPadColourChoices, {}};
constexpr SettingNames kPadColourStoppedNames{STRING_FOR_STOPPED, STRING_FOR_STOPPED, kPadColourChoices, {}};
constexpr SettingNames kPadColourMutedNames{STRING_FOR_MUTED, STRING_FOR_MUTED, kPadColourChoices, {}};
constexpr SettingNames kPadColourSoloedNames{STRING_FOR_SOLOED, STRING_FOR_SOLOED, kPadColourChoices, {}};
constexpr SettingNames kPadColourFillNames{STRING_FOR_FILL, STRING_FOR_FILL, kPadColourChoices, {}};
constexpr SettingNames kPadColourOnceNames{STRING_FOR_ONCE, STRING_FOR_ONCE, kPadColourChoices, {}};
constexpr SettingNames kAccessibilityShortcutsNames{
    STRING_FOR_DEFAULT_ACCESSIBILITY_SHORTCUTS, STRING_FOR_DEFAULT_ACCESSIBILITY_SHORTCUTS, {}, {}};
constexpr std::array<String, 3> kMenuHighlightingChoices{
    {STRING_FOR_FULL_INVERSION, STRING_FOR_PARTIAL_INVERSION, STRING_FOR_NO_INVERSION}};
constexpr SettingNames kMenuHighlightingNames{STRING_FOR_DEFAULT_ACCESSIBILITY_MENU_HIGHLIGHTING,
                                              STRING_FOR_DEFAULT_ACCESSIBILITY_MENU_HIGHLIGHTING,
                                              kMenuHighlightingChoices,
                                              {}};
constexpr std::array<String, 2> kSessionLayoutChoices{
    {STRING_FOR_DEFAULT_UI_SONG_LAYOUT_ROWS, STRING_FOR_DEFAULT_UI_GRID}};
constexpr SettingNames kSessionLayoutNames{
    STRING_FOR_DEFAULT_UI_LAYOUT, STRING_FOR_DEFAULT_UI_LAYOUT, kSessionLayoutChoices, {}};
constexpr std::array<String, 3> kGridActiveModeChoices{
    {STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE_SELECTION, STRING_FOR_GREEN, STRING_FOR_BLUE}};
constexpr SettingNames kGridActiveModeNames{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE,
                                            STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE,
                                            kGridActiveModeChoices,
                                            {}};
constexpr SettingNames kGridAllowGreenSelectionNames{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ALLOW_GREEN_SELECTION,
                                                     STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ALLOW_GREEN_SELECTION,
                                                     {},
                                                     {}};
constexpr SettingNames kGridEmptyPadsUnarmNames{
    STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_UNARM, STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_UNARM, {}, {}};
constexpr SettingNames kGridEmptyPadsCreateRecNames{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_CREATE_REC,
                                                    STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_CREATE_REC,
                                                    {},
                                                    {}};
constexpr std::array<String, 3> kKeyboardLayoutChoices{
    {STRING_FOR_KEYBOARD_LAYOUT_ISOMORPHIC, STRING_FOR_KEYBOARD_LAYOUT_IN_KEY, STRING_FOR_KEYBOARD_LAYOUT_PIANO}};
constexpr SettingNames kKeyboardLayoutNames{
    STRING_FOR_DEFAULT_UI_LAYOUT, STRING_FOR_DEFAULT_UI_LAYOUT, kKeyboardLayoutChoices, {}};
constexpr SettingNames kVelocityMomentaryNames{
    STRING_FOR_DEFAULT_UI_KB_CONTROLS_VELOCITY_MOMENTARY, STRING_FOR_DEFAULT_UI_KB_CONTROLS_VELOCITY_MOMENTARY, {}, {}};
constexpr SettingNames kModwheelMomentaryNames{
    STRING_FOR_DEFAULT_UI_KB_CONTROLS_MODWHEEL_MOMENTARY, STRING_FOR_DEFAULT_UI_KB_CONTROLS_MODWHEEL_MOMENTARY, {}, {}};
constexpr std::array<String, 3> kFavouritesLayoutChoices{
    {STRING_FOR_DEFAULT_UI_DEFAULT_FAVOURITES_LAYOUT_FAVOURITES,
     STRING_FOR_DEFAULT_UI_DEFAULT_FAVOURITES_LAYOUT_FAVOURITES_AND_BANKS, STRING_FOR_OFF}};
constexpr SettingNames kFavouritesLayoutNames{
    STRING_FOR_DEFAULT_UI_FAVOURITES, STRING_FOR_DEFAULT_UI_FAVOURITES, kFavouritesLayoutChoices, {}};
constexpr SettingNames kPadBrightnessNames{
    STRING_FOR_DEFAULT_PAD_BRIGHTNESS_MENU_TITLE, STRING_FOR_DEFAULT_PAD_BRIGHTNESS, {}, {}};
constexpr std::array<String, 4> kScreensaverModeChoices{{STRING_FOR_OFF, STRING_FOR_SCREENSAVER_MODE_BLANK,
                                                         STRING_FOR_SCREENSAVER_MODE_STARSCAPE,
                                                         STRING_FOR_SCREENSAVER_MODE_DELUGE}};
constexpr SettingNames kScreensaverModeNames{
    STRING_FOR_SCREENSAVER_MODE, STRING_FOR_SCREENSAVER_MODE, kScreensaverModeChoices, {}};
constexpr SettingNames kScreensaverTimeoutNames{STRING_FOR_SCREENSAVER_TIMEOUT, STRING_FOR_SCREENSAVER_TIMEOUT, {}, {}};
constexpr std::array<String, 3> kSamplePreviewChoices{
    {STRING_FOR_DISABLED, STRING_FOR_CONDITIONAL, STRING_FOR_ENABLED}};
constexpr SettingNames kSamplePreviewNames{
    STRING_FOR_SAMPLE_PREVIEW, STRING_FOR_SAMPLE_PREVIEW, kSamplePreviewChoices, {}};
constexpr SettingNames kCountInNames{STRING_FOR_REC_COUNT_IN, STRING_FOR_COUNT_IN, {}, {}};
constexpr std::array<String, 3> kMonitorModeChoices{{STRING_FOR_CONDITIONAL, STRING_FOR_ENABLED, STRING_FOR_DISABLED}};
constexpr SettingNames kMonitorModeNames{
    STRING_FOR_MONITORING, STRING_FOR_SAMPLING_MONITORING, kMonitorModeChoices, {}};
constexpr SettingNames kUsbAudioEnabledNames{STRING_FOR_USB_AUDIO, STRING_FOR_ENABLED, {}, {}};
constexpr SettingNames kUsbDirectMonitoringNames{STRING_FOR_USB_INPUT, STRING_FOR_USB_SMART_MONITORING, {}, {}};
constexpr std::array<String, 2> kUsbOutputSourceChoices{{STRING_FOR_MAIN_MIX, STRING_FOR_LINE_IN}};
constexpr SettingNames kUsbOutputSourceNames{STRING_FOR_USB_OUTPUT, STRING_FOR_OUTPUT, kUsbOutputSourceChoices, {}};
constexpr SettingNames kTriggerInPpqnNames{STRING_FOR_INPUT_PPQN, STRING_FOR_PPQN, {}, {}};
constexpr SettingNames kTriggerInAutoStartNames{STRING_FOR_AUTO_START, STRING_FOR_AUTO_START, {}, {}};
constexpr SettingNames kTriggerOutPpqnNames{STRING_FOR_OUTPUT_PPQN, STRING_FOR_PPQN, {}, {}};
constexpr SettingNames kMidiClockOutNames{STRING_FOR_MIDI_CLOCK_OUT, STRING_FOR_OUTPUT, {}, {}};
constexpr SettingNames kMidiClockInNames{STRING_FOR_MIDI_CLOCK_IN, STRING_FOR_INPUT, {}, {}};
constexpr SettingNames kTempoMagnitudeMatchingNames{
    STRING_FOR_TEMPO_MAGNITUDE_MATCHING, STRING_FOR_TEMPO_MAGNITUDE_MATCHING, {}, {}};
/// MIDI follow's rows and the MIDI page's own. The feedback channels after the follow channels' own names are the
/// track's channel alone and with each of them, as the feedback's channel item names them.
constexpr SettingNames kFollowKitRootNoteNames{
    STRING_FOR_FOLLOW_KIT_ROOT_NOTE, STRING_FOR_FOLLOW_KIT_ROOT_NOTE, {}, {}};
constexpr std::array<String, 8> kFollowFeedbackChannelChoices{{
    STRING_FOR_NONE,
    STRING_FOR_FOLLOW_CHANNEL_A,
    STRING_FOR_FOLLOW_CHANNEL_B,
    STRING_FOR_FOLLOW_CHANNEL_C,
    STRING_FOR_FOLLOW_FEEDBACK_CHANNEL_TRACK,
    STRING_FOR_FOLLOW_FEEDBACK_CHANNEL_TRACK_AND_A,
    STRING_FOR_FOLLOW_FEEDBACK_CHANNEL_TRACK_AND_B,
    STRING_FOR_FOLLOW_FEEDBACK_CHANNEL_TRACK_AND_C,
}};
constexpr SettingNames kFollowFeedbackChannelNames{
    STRING_FOR_CHANNEL, STRING_FOR_CHANNEL, kFollowFeedbackChannelChoices, {}};
constexpr std::array<String, 4> kFollowFeedbackAutomationChoices{{
    STRING_FOR_DISABLED,
    STRING_FOR_LOW,
    STRING_FOR_MEDIUM,
    STRING_FOR_HIGH,
}};
constexpr SettingNames kFollowFeedbackAutomationNames{
    STRING_FOR_FOLLOW_FEEDBACK_AUTOMATION, STRING_FOR_FOLLOW_FEEDBACK_AUTOMATION, kFollowFeedbackAutomationChoices, {}};
constexpr SettingNames kFollowFeedbackFilterNames{
    STRING_FOR_FOLLOW_FEEDBACK_FILTER, STRING_FOR_FOLLOW_FEEDBACK_FILTER, {}, {}};
constexpr SettingNames kFollowDisplayParamNames{
    STRING_FOR_FOLLOW_DISPLAY_PARAM, STRING_FOR_FOLLOW_DISPLAY_PARAM, {}, {}};
constexpr SettingNames kMidiSelectKitRowNames{STRING_FOR_SELECT_KIT_ROW, STRING_FOR_SELECT_KIT_ROW, {}, {}};
constexpr SettingNames kMidiThruNames{STRING_FOR_MIDI_THRU, STRING_FOR_MIDI_THRU, {}, {}};
constexpr std::array<String, 2> kMidiTransposeChoices{{STRING_FOR_TRANSPOSE_INKEY, STRING_FOR_TRANSPOSE_CHROMATIC}};
constexpr SettingNames kMidiTransposeNames{STRING_FOR_TRANSPOSE, STRING_FOR_TRANSPOSE, kMidiTransposeChoices, {}};
constexpr std::array<String, 4> kMidiTakeoverChoices{
    {STRING_FOR_JUMP, STRING_FOR_PICK_UP, STRING_FOR_SCALE, STRING_FOR_RELATIVE}};
constexpr SettingNames kMidiTakeoverNames{STRING_FOR_TAKEOVER, STRING_FOR_TAKEOVER, kMidiTakeoverChoices, {}};
constexpr SettingNames kDifferentiateInputsNames{
    STRING_FOR_DIFFERENTIATE_INPUTS, STRING_FOR_DIFFERENTIATE_INPUTS, {}, {}};
/// The CV outputs' and the gate outputs' rows. Every output's row of a kind shares its names; a gate's mode lists the
/// clock or the run signal only on the gate whose row has that choice.
constexpr SettingNames kCvVoltsPerOctaveNames{
    STRING_FOR_CV_V_PER_OCTAVE_MENU_TITLE, STRING_FOR_VOLTS_PER_OCTAVE, {}, {}};
constexpr SettingNames kCvTransposeNames{STRING_FOR_CV_TRANSPOSE_MENU_TITLE, STRING_FOR_TRANSPOSE, {}, {}};
constexpr std::array<String, 2> kGateModeChoices{{STRING_FOR_V_TRIGGER, STRING_FOR_S_TRIGGER}};
constexpr SettingNames kGateModeNames{STRING_FOR_GATE_MODE_TITLE, STRING_FOR_GATE_MODE_TITLE, kGateModeChoices, {}};
constexpr std::array<String, 3> kRunGateModeChoices{
    {STRING_FOR_V_TRIGGER, STRING_FOR_S_TRIGGER, STRING_FOR_RUN_SIGNAL}};
constexpr SettingNames kRunGateModeNames{
    STRING_FOR_GATE_MODE_TITLE, STRING_FOR_GATE_MODE_TITLE, kRunGateModeChoices, {}};
constexpr std::array<String, 3> kClockGateModeChoices{{STRING_FOR_V_TRIGGER, STRING_FOR_S_TRIGGER, STRING_FOR_CLOCK}};
constexpr SettingNames kClockGateModeNames{
    STRING_FOR_GATE_MODE_TITLE, STRING_FOR_GATE_MODE_TITLE, kClockGateModeChoices, {}};
constexpr SettingNames kGateMinOffTimeNames{STRING_FOR_MINIMUM_OFF_TIME, STRING_FOR_MINIMUM_OFF_TIME, {}, {}};
/// The stem export's options: the plain and the drums' normalization share their item's name.
constexpr SettingNames kStemNormalizeNames{
    STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION, STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION, {}, {}};
constexpr SettingNames kStemToSilenceNames{
    STRING_FOR_CONFIGURE_EXPORT_STEMS_SILENCE, STRING_FOR_CONFIGURE_EXPORT_STEMS_SILENCE, {}, {}};
constexpr SettingNames kStemSongFxNames{
    STRING_FOR_CONFIGURE_EXPORT_STEMS_SONGFX, STRING_FOR_CONFIGURE_EXPORT_STEMS_SONGFX, {}, {}};
constexpr SettingNames kStemKitFxNames{
    STRING_FOR_CONFIGURE_EXPORT_STEMS_KITFX, STRING_FOR_CONFIGURE_EXPORT_STEMS_KITFX, {}, {}};
constexpr SettingNames kStemOfflineNames{
    STRING_FOR_CONFIGURE_EXPORT_STEMS_OFFLINE_RENDERING, STRING_FOR_CONFIGURE_EXPORT_STEMS_OFFLINE_RENDERING, {}, {}};
constexpr SettingNames kStemMixdownNames{
    STRING_FOR_CONFIGURE_EXPORT_STEMS_MIXDOWN, STRING_FOR_CONFIGURE_EXPORT_STEMS_MIXDOWN, {}, {}};
/// The community features' names: each row's name is its menu item's, and a feature with choices of its own names
/// them in its values' order.
constexpr std::array<String, 2> kSyncScalingActionChoices{
    {STRING_FOR_SYNC_SCALING_ACTION_SYNC_SCALING, STRING_FOR_SYNC_SCALING_ACTION_FILL}};
constexpr std::array<String, 3> kShortcutOverlayChoices{
    {STRING_FOR_OFF, STRING_FOR_SHORTCUT_OVERLAY_MENU_ONLY, STRING_FOR_SHORTCUT_OVERLAY_MENU_AND_CLIP}};
constexpr SettingNames kFeatureDrumRandomizerNames{
    STRING_FOR_COMMUNITY_FEATURE_DRUM_RANDOMIZER, STRING_FOR_COMMUNITY_FEATURE_DRUM_RANDOMIZER, {}, {}};
constexpr SettingNames kFeatureQuantizeNames{
    STRING_FOR_COMMUNITY_FEATURE_QUANTIZE, STRING_FOR_COMMUNITY_FEATURE_QUANTIZE, {}, {}};
constexpr SettingNames kFeatureFineTempoKnobNames{
    STRING_FOR_COMMUNITY_FEATURE_FINE_TEMPO_KNOB, STRING_FOR_COMMUNITY_FEATURE_FINE_TEMPO_KNOB, {}, {}};
constexpr SettingNames kFeatureCatchNotesNames{
    STRING_FOR_COMMUNITY_FEATURE_CATCH_NOTES, STRING_FOR_COMMUNITY_FEATURE_CATCH_NOTES, {}, {}};
constexpr SettingNames kFeatureDeleteUnusedKitRowsNames{
    STRING_FOR_COMMUNITY_FEATURE_DELETE_UNUSED_KIT_ROWS, STRING_FOR_COMMUNITY_FEATURE_DELETE_UNUSED_KIT_ROWS, {}, {}};
constexpr SettingNames kFeatureAltGoldenKnobDelayParamsNames{
    STRING_FOR_COMMUNITY_FEATURE_ALT_DELAY_PARAMS, STRING_FOR_COMMUNITY_FEATURE_ALT_DELAY_PARAMS, {}, {}};
constexpr SettingNames kFeatureSyncScalingActionNames{STRING_FOR_COMMUNITY_FEATURE_SYNC_SCALING_ACTION,
                                                      STRING_FOR_COMMUNITY_FEATURE_SYNC_SCALING_ACTION,
                                                      kSyncScalingActionChoices,
                                                      {}};
constexpr SettingNames kFeatureHighlightIncomingNotesNames{STRING_FOR_COMMUNITY_FEATURE_HIGHLIGHT_INCOMING_NOTES,
                                                           STRING_FOR_COMMUNITY_FEATURE_HIGHLIGHT_INCOMING_NOTES,
                                                           {},
                                                           {}};
constexpr SettingNames kFeatureDisplayNornsLayoutNames{
    STRING_FOR_COMMUNITY_FEATURE_NORNS_LAYOUT, STRING_FOR_COMMUNITY_FEATURE_NORNS_LAYOUT, {}, {}};
constexpr SettingNames kFeatureStickyShiftNames{
    STRING_FOR_COMMUNITY_FEATURE_STICKY_SHIFT, STRING_FOR_COMMUNITY_FEATURE_STICKY_SHIFT, {}, {}};
constexpr SettingNames kFeatureLightShiftNames{
    STRING_FOR_COMMUNITY_FEATURE_LIGHT_SHIFT, STRING_FOR_COMMUNITY_FEATURE_LIGHT_SHIFT, {}, {}};
constexpr SettingNames kFeatureEnableDX7EngineNames{
    STRING_FOR_COMMUNITY_FEATURE_DX7_ENGINE, STRING_FOR_COMMUNITY_FEATURE_DX7_ENGINE, {}, {}};
constexpr SettingNames kFeatureEnableKeyboardViewSidebarMenuExitNames{
    STRING_FOR_COMMUNITY_FEATURE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT,
    STRING_FOR_COMMUNITY_FEATURE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT,
    {},
    {}};
constexpr SettingNames kFeatureEnableLaunchEventPlayheadNames{
    STRING_FOR_COMMUNITY_FEATURE_LAUNCH_EVENT_PLAYHEAD, STRING_FOR_COMMUNITY_FEATURE_LAUNCH_EVENT_PLAYHEAD, {}, {}};
constexpr SettingNames kFeatureDisplayChordKeyboardNames{
    STRING_FOR_COMMUNITY_FEATURE_CHORD_KEYBOARD, STRING_FOR_COMMUNITY_FEATURE_CHORD_KEYBOARD, {}, {}};
constexpr SettingNames kFeatureAlternativePlaybackStartBehaviourNames{
    STRING_FOR_COMMUNITY_FEATURE_ALTERNATIVE_PLAYBACK_START_BEHAVIOUR,
    STRING_FOR_COMMUNITY_FEATURE_ALTERNATIVE_PLAYBACK_START_BEHAVIOUR,
    {},
    {}};
constexpr SettingNames kFeatureEnableGridViewLoopPadsNames{
    STRING_FOR_COMMUNITY_FEATURE_GRID_VIEW_LOOP_PADS, STRING_FOR_COMMUNITY_FEATURE_GRID_VIEW_LOOP_PADS, {}, {}};
constexpr SettingNames kFeatureAlternativeTapTempoBehaviourNames{
    STRING_FOR_COMMUNITY_FEATURE_ALTERNATIVE_TAP_TEMPO_BEHAVIOUR,
    STRING_FOR_COMMUNITY_FEATURE_ALTERNATIVE_TAP_TEMPO_BEHAVIOUR,
    {},
    {}};
constexpr SettingNames kFeatureEnableHorizontalMenusNames{
    STRING_FOR_COMMUNITY_FEATURE_HORIZONTAL_MENUS, STRING_FOR_COMMUNITY_FEATURE_HORIZONTAL_MENUS, {}, {}};
constexpr SettingNames kFeatureTrimFromStartOfAudioClipNames{STRING_FOR_COMMUNITY_FEATURE_TRIM_FROM_START_OF_AUDIO_CLIP,
                                                             STRING_FOR_COMMUNITY_FEATURE_TRIM_FROM_START_OF_AUDIO_CLIP,
                                                             {},
                                                             {}};
constexpr SettingNames kFeatureShowBatteryLevelNames{
    STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL, STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL, {}, {}};
constexpr SettingNames kFeatureRoundedCornersNames{
    STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS, STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS, {}, {}};
constexpr SettingNames kFeatureShortcutOverlayNames{STRING_FOR_COMMUNITY_FEATURE_SHORTCUT_OVERLAY,
                                                    STRING_FOR_COMMUNITY_FEATURE_SHORTCUT_OVERLAY,
                                                    kShortcutOverlayChoices,
                                                    {}};
/// @}

/// @brief The tables several owners' menus show alike, stored once and keyed after the owners: the arp's setting rows
/// (a clip's, a kit row's and a sound's arp), its host rows (a clip's and a kit row's), and the effects (the Deluge
/// instrument's and the global effectable's).
enum class Shared : uint8_t { ARP_SETTINGS = NamesOwner::kCount, ARP_HOST, EFFECTS };

/// @brief One row's names, under its owner's index or a shared table's.
struct Entry {
	uint8_t owner;
	std::string_view path;
	const SettingNames* names;

	constexpr Entry(NamesOwner owner, std::string_view path, const SettingNames* names)
	    : owner{owner.index()}, path{path}, names{names} {}
	constexpr Entry(Shared owner, std::string_view path, const SettingNames* names)
	    : owner{std::to_underlying(owner)}, path{path}, names{names} {}
};

/// @brief The order kEntries is searched in: owners by index (the setting groups, then the engines, then the shared
/// tables), then paths.
constexpr auto key_of(uint8_t owner, std::string_view path) {
	return std::tuple{owner, path};
}

/// Every setting row a menu shows, sorted by owner and path.
constexpr std::array<Entry, 244> kEntries{{
    Entry{SettingGroupId::SONG, "reverb/compressor/attack", &kReverbCompressorAttackNames},
    Entry{SettingGroupId::SONG, "reverb/compressor/release", &kReverbCompressorReleaseNames},
    Entry{SettingGroupId::SONG, "reverb/compressor/shape", &kReverbCompressorShapeNames},
    Entry{SettingGroupId::SONG, "reverb/dampening", &kReverbDampeningNames},
    Entry{SettingGroupId::SONG, "reverb/hpf", &kAudioCompressorCompHPFNames},
    Entry{SettingGroupId::SONG, "reverb/lpf", &kReverbLpfNames},
    Entry{SettingGroupId::SONG, "reverb/model", &kReverbModelNames},
    Entry{SettingGroupId::SONG, "reverb/pan", &kReverbPanNames},
    Entry{SettingGroupId::SONG, "reverb/roomSize", &kReverbRoomSizeNames},
    Entry{SettingGroupId::SONG, "reverb/width", &kReverbWidthNames},
    Entry{SettingGroupId::SONG, "swingInterval", &kSwingIntervalNames},
    Entry{SettingGroupId::INSTRUMENT_DEVICE, "collapseAftertouch", &kCollapseAftertouchNames},
    Entry{SettingGroupId::INSTRUMENT_DEVICE, "collapseMpe", &kCollapseMpeNames},
    Entry{SettingGroupId::INSTRUMENT_DEVICE, "cv2Source", &kCv2SourceNames},
    Entry{SettingGroupId::INSTRUMENT_DEVICE, "deviceDefinitionFileName", &kDeviceDefinitionFileNameNames},
    Entry{SettingGroupId::INSTRUMENT_DEVICE, "hideUnlabeledCC", &kHideUnlabeledCCNames},
    Entry{SettingGroupId::INSTRUMENT_DEVICE, "outputMpeY", &kOutputMpeYNames},
    Entry{SettingGroupId::NOTE_ROW, "bendRange", &kDrumBendRangeNames},
    Entry{SettingGroupId::NOTE_ROW, "sequenceDirection", &kSequenceDirectionNames},
    Entry{SettingGroupId::AUDIO_CLIP, "attack", &kAttackNames},
    Entry{SettingGroupId::AUDIO_CLIP, "linearInterpolation", &kLinearInterpolationNames},
    Entry{SettingGroupId::AUDIO_CLIP, "pitchSpeedIndependent", &kOsc1TimeStretchEnableNames},
    Entry{SettingGroupId::AUDIO_CLIP, "priority", &kPriorityNames},
    Entry{SettingGroupId::AUDIO_CLIP, "reversed", &kReversedNames},
    Entry{SettingGroupId::AUDIO_CLIP, "transpose", &kTransposeNames2},
    Entry{SettingGroupId::CLIP, "sequenceDirection", &kClipSequenceDirectionNames},
    Entry{SettingGroupId::CLIP_VALUES, "bendRange", &kBendRangeNames},
    Entry{SettingGroupId::CLIP_VALUES, "bendRangeMPE", &kBendRangeNames2},
    Entry{SettingGroupId::CLIP_VALUES, "midiBank", &kMidiBankNames},
    Entry{SettingGroupId::CLIP_VALUES, "midiPGM", &kMidiPGMNames},
    Entry{SettingGroupId::CLIP_VALUES, "midiSub", &kMidiSubNames},
    Entry{SettingGroupId::AUDIO_INPUT, "inputChannel", &kInputChannelNames},
    Entry{SettingGroupId::AUDIO_INPUT, "mode", &kInputChannelNames},
    Entry{SettingGroupId::AUDIO_INPUT, "outputRecordingFrom", &kOutputRecordingFromNames},
    Entry{SettingGroupId::SOUND_ROUTING, "midiOutput/channel", &kMidiOutputChannelNames},
    Entry{SettingGroupId::SOUND_ROUTING, "midiOutput/noteForDrum", &kMidiOutputNoteForDrumNames},
    Entry{SettingGroupId::SOUND_ROUTING, "sideChainSend", &kSideChainSendNames},
    // A DEVICE row named by a `device_path` constant shares its one stored copy with the row (`project::kStoredText`);
    // the built feature, CV and gate paths keep their own copy here, in the same SDRAM table.
    Entry{SettingGroupId::DEVICE, project::stored_text<"clock/midiIn">, &kMidiClockInNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"clock/midiOut">, &kMidiClockOutNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"clock/tempoMagnitudeMatching">, &kTempoMagnitudeMatchingNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"clock/triggerIn/autoStart">, &kTriggerInAutoStartNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"clock/triggerIn/ppqn">, &kTriggerInPpqnNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"clock/triggerOut/ppqn">, &kTriggerOutPpqnNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"cv/0/transpose">, &kCvTransposeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"cv/0/voltsPerOctave">, &kCvVoltsPerOctaveNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"cv/1/transpose">, &kCvTransposeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"cv/1/voltsPerOctave">, &kCvVoltsPerOctaveNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/automation/clear">, &kDefaultAutomationClearNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/automation/interpolate">,
          &kDefaultAutomationInterpolateNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/automation/noAuditionShortcuts">,
          &kDefaultAutomationNoAuditionShortcutsNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/automation/nudgeNote">,
          &kDefaultAutomationNudgeNoteNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/automation/shift">, &kDefaultAutomationShiftNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/bendRange">, &kDefaultBendRangeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/cpuIndicator">, &kDefaultCpuIndicatorNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/holdTime">, &kDefaultHoldTimeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/key/lower">, &kDefaultKeyNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/key/upper">, &kDefaultKeyNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/magnitude">, &kDefaultMagnitudeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/metronomeVolume">, &kDefaultMetronomeVolumeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/newClipType">, &kDefaultNewClipTypeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/patchCablePolarity">, &kPatchCablePolarityNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/recordMargins">, &kDefaultRecordMarginsNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/recordQuantize">, &kDefaultRecordQuantizeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/scale">, &kDefaultScaleNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/sliceMode">, &kDefaultSliceModeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/startupSong">, &kDefaultStartupSongNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/swing/lower">, &kDefaultSwingNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/swing/upper">, &kDefaultSwingNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/swingInterval">, &kDefaultSwingIntervalNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/tempo/lower">, &kDefaultTempoNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/tempo/upper">, &kDefaultTempoNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/thresholdRecording">,
          &kDefaultThresholdRecordingNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/useLastClipType">, &kDefaultUseLastClipTypeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/useSharps">, &kUseSharpsNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"defaults/velocity">, &kDefaultVelocityNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/EnableDX7Engine">, &kFeatureEnableDX7EngineNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/altGoldenKnobDelayParams">,
          &kFeatureAltGoldenKnobDelayParamsNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/alternativePlaybackStartBehaviour">,
          &kFeatureAlternativePlaybackStartBehaviourNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/alternativeTapTempoBehaviour">,
          &kFeatureAlternativeTapTempoBehaviourNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/catchNotes">, &kFeatureCatchNotesNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/deleteUnusedKitRows">,
          &kFeatureDeleteUnusedKitRowsNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/displayChordKeyboard">,
          &kFeatureDisplayChordKeyboardNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/displayNornsLayout">,
          &kFeatureDisplayNornsLayoutNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/drumRandomizer">, &kFeatureDrumRandomizerNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/enableGridViewLoopPads">,
          &kFeatureEnableGridViewLoopPadsNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/enableHorizontalMenus">,
          &kFeatureEnableHorizontalMenusNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/enableKeyboardViewSidebarMenuExit">,
          &kFeatureEnableKeyboardViewSidebarMenuExitNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/enableLaunchEventPlayhead">,
          &kFeatureEnableLaunchEventPlayheadNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/fineTempoKnob">, &kFeatureFineTempoKnobNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/highlightIncomingNotes">,
          &kFeatureHighlightIncomingNotesNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/lightShift">, &kFeatureLightShiftNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/quantize">, &kFeatureQuantizeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/roundedCorners">, &kFeatureRoundedCornersNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/shortcutOverlay">, &kFeatureShortcutOverlayNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/showBatteryLevel">, &kFeatureShowBatteryLevelNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/stickyShift">, &kFeatureStickyShiftNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/syncScalingAction">, &kFeatureSyncScalingActionNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"features/trimFromStartOfAudioClip">,
          &kFeatureTrimFromStartOfAudioClipNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"gate/0/mode">, &kGateModeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"gate/1/mode">, &kGateModeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"gate/2/mode">, &kRunGateModeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"gate/3/mode">, &kClockGateModeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"gate/minOffTime">, &kGateMinOffTimeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midi/differentiateInputs">, &kDifferentiateInputsNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midi/selectKitRow">, &kMidiSelectKitRowNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midi/takeover">, &kMidiTakeoverNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midi/thru">, &kMidiThruNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midi/transpose">, &kMidiTransposeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midiFollow/displayParam">, &kFollowDisplayParamNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midiFollow/feedback/automation">,
          &kFollowFeedbackAutomationNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midiFollow/feedback/channel">, &kFollowFeedbackChannelNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midiFollow/feedback/filter">, &kFollowFeedbackFilterNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"midiFollow/kitRootNote">, &kFollowKitRootNoteNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"pads/colour/active">, &kPadColourActiveNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"pads/colour/fill">, &kPadColourFillNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"pads/colour/muted">, &kPadColourMutedNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"pads/colour/once">, &kPadColourOnceNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"pads/colour/soloed">, &kPadColourSoloedNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"pads/colour/stopped">, &kPadColourStoppedNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"pads/playCursor">, &kPlayCursorNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"pads/shortcutsVersion">, &kShortcutsVersionNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"pads/textKeyboardLayout">, &kTextKeyboardNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"recording/countIn">, &kCountInNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"recording/monitorMode">, &kMonitorModeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"stemExport/kitFx">, &kStemKitFxNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"stemExport/mixdown">, &kStemMixdownNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"stemExport/normalize">, &kStemNormalizeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"stemExport/normalizeDrums">, &kStemNormalizeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"stemExport/offline">, &kStemOfflineNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"stemExport/songFx">, &kStemSongFxNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"stemExport/toSilence">, &kStemToSilenceNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/accessibilityShortcuts">, &kAccessibilityShortcutsNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/favouritesLayout">, &kFavouritesLayoutNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/grid/activeMode">, &kGridActiveModeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/grid/allowGreenSelection">, &kGridAllowGreenSelectionNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/grid/emptyPadsCreateRec">, &kGridEmptyPadsCreateRecNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/grid/emptyPadsUnarm">, &kGridEmptyPadsUnarmNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/keyboard/modwheelMomentary">, &kModwheelMomentaryNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/keyboard/velocityMomentary">, &kVelocityMomentaryNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/keyboardLayout">, &kKeyboardLayoutNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/menuHighlighting">, &kMenuHighlightingNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/padBrightness">, &kPadBrightnessNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/samplePreview">, &kSamplePreviewNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/screensaver/mode">, &kScreensaverModeNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/screensaver/timeout">, &kScreensaverTimeoutNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"ui/sessionLayout">, &kSessionLayoutNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"usbAudio/directMonitoring">, &kUsbDirectMonitoringNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"usbAudio/enabled">, &kUsbAudioEnabledNames},
    Entry{SettingGroupId::DEVICE, project::stored_text<"usbAudio/outputSource">, &kUsbOutputSourceNames},
    Entry{plugin::kDelugeInstrument, "dx7/retrigPhase", &kOsc1RetrigPhaseNames},
    Entry{plugin::kDelugeInstrument, "dx7/transpose", &kOsc1TransposeNames},
    Entry{plugin::kDelugeInstrument, "envelope1Settings/curve", &kEnvelopeCurveNames},
    Entry{plugin::kDelugeInstrument, "envelope1Settings/follow", &kEnvelopeFollowNames},
    Entry{plugin::kDelugeInstrument, "envelope1Settings/mode", &kEnvelopeModeNames},
    Entry{plugin::kDelugeInstrument, "envelope2Settings/curve", &kEnvelopeCurveNames},
    Entry{plugin::kDelugeInstrument, "envelope2Settings/follow", &kEnvelopeFollowNames},
    Entry{plugin::kDelugeInstrument, "envelope2Settings/mode", &kEnvelopeModeNames},
    Entry{plugin::kDelugeInstrument, "envelope3Settings/curve", &kEnvelopeCurveNames},
    Entry{plugin::kDelugeInstrument, "envelope3Settings/follow", &kEnvelopeFollowNames},
    Entry{plugin::kDelugeInstrument, "envelope3Settings/mode", &kEnvelopeModeNames},
    Entry{plugin::kDelugeInstrument, "envelope4Settings/curve", &kEnvelopeCurveNames},
    Entry{plugin::kDelugeInstrument, "envelope4Settings/follow", &kEnvelopeFollowNames},
    Entry{plugin::kDelugeInstrument, "envelope4Settings/mode", &kEnvelopeModeNames},
    Entry{plugin::kDelugeInstrument, "lfo1/syncLevel", &kLfo1SyncLevelNames},
    Entry{plugin::kDelugeInstrument, "lfo1/type", &kLfo1TypeNames},
    Entry{plugin::kDelugeInstrument, "lfo2/syncLevel", &kLfo1SyncLevelNames},
    Entry{plugin::kDelugeInstrument, "lfo2/type", &kLfo1TypeNames},
    Entry{plugin::kDelugeInstrument, "lfo3/syncLevel", &kLfo1SyncLevelNames},
    Entry{plugin::kDelugeInstrument, "lfo3/type", &kLfo1TypeNames},
    Entry{plugin::kDelugeInstrument, "lfo4/syncLevel", &kLfo1SyncLevelNames},
    Entry{plugin::kDelugeInstrument, "lfo4/type", &kLfo1TypeNames},
    Entry{plugin::kDelugeInstrument, "maxVoices", &kMaxVoicesNames},
    Entry{plugin::kDelugeInstrument, "mode", &kModeNames},
    Entry{plugin::kDelugeInstrument, "modulator1/retrigPhase", &kModulator1RetrigPhaseNames},
    Entry{plugin::kDelugeInstrument, "modulator1/transpose", &kModulator1TransposeNames},
    Entry{plugin::kDelugeInstrument, "modulator2/retrigPhase", &kModulator1RetrigPhaseNames},
    Entry{plugin::kDelugeInstrument, "modulator2/toModulator1", &kModulator2ToModulator1Names},
    Entry{plugin::kDelugeInstrument, "modulator2/transpose", &kModulator1TransposeNames},
    Entry{plugin::kDelugeInstrument, "osc1/linearInterpolation", &kOsc1LinearInterpolationNames},
    Entry{plugin::kDelugeInstrument, "osc1/loopMode", &kOsc1LoopModeNames},
    Entry{plugin::kDelugeInstrument, "osc1/retrigPhase", &kOsc1RetrigPhaseNames},
    Entry{plugin::kDelugeInstrument, "osc1/reversed", &kOsc1ReversedNames},
    Entry{plugin::kDelugeInstrument, "osc1/timeStretchAmount", &kOsc1TimeStretchAmountNames},
    Entry{plugin::kDelugeInstrument, "osc1/timeStretchEnable", &kOsc1TimeStretchEnableNames},
    Entry{plugin::kDelugeInstrument, "osc1/transpose", &kOsc1TransposeNames},
    Entry{plugin::kDelugeInstrument, "osc1/type", &kOsc1TypeNames},
    Entry{plugin::kDelugeInstrument, "osc2/linearInterpolation", &kOsc1LinearInterpolationNames},
    Entry{plugin::kDelugeInstrument, "osc2/loopMode", &kOsc1LoopModeNames},
    Entry{plugin::kDelugeInstrument, "osc2/oscillatorSync", &kOsc2OscillatorSyncNames},
    Entry{plugin::kDelugeInstrument, "osc2/retrigPhase", &kOsc1RetrigPhaseNames},
    Entry{plugin::kDelugeInstrument, "osc2/reversed", &kOsc1ReversedNames},
    Entry{plugin::kDelugeInstrument, "osc2/timeStretchAmount", &kOsc1TimeStretchAmountNames},
    Entry{plugin::kDelugeInstrument, "osc2/timeStretchEnable", &kOsc1TimeStretchEnableNames},
    Entry{plugin::kDelugeInstrument, "osc2/transpose", &kOsc1TransposeNames},
    Entry{plugin::kDelugeInstrument, "osc2/type", &kOsc1TypeNames},
    Entry{plugin::kDelugeInstrument, "polyphonic", &kPolyphonicNames},
    Entry{plugin::kDelugeInstrument, "transpose", &kTransposeNames},
    Entry{plugin::kDelugeInstrument, "unison/detune", &kUnisonDetuneNames},
    Entry{plugin::kDelugeInstrument, "unison/num", &kUnisonNumNames},
    Entry{plugin::kDelugeInstrument, "unison/spread", &kUnisonSpreadNames},
    Entry{plugin::kDelugeInstrument, "voicePriority", &kPriorityNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/arpMode", &kArpeggiatorArpModeNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/chordType", &kArpeggiatorChordTypeNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/kitArp", &kArpeggiatorKitArpNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/mpeVelocity", &kArpeggiatorMpeVelocityNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/noteMode", &kArpeggiatorNoteModeNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/numOctaves", &kArpeggiatorNumOctavesNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/octaveMode", &kArpeggiatorOctaveModeNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/preset", &kArpeggiatorPresetNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/randomizerLock", &kArpeggiatorRandomizerLockNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/stepRepeat", &kArpeggiatorStepRepeatNames},
    Entry{Shared::ARP_SETTINGS, "arpeggiator/syncLevel", &kArpeggiatorSyncLevelNames},
    Entry{Shared::ARP_HOST, "arpeggiator/bassProbability", &kArpeggiatorBassProbabilityNames},
    Entry{Shared::ARP_HOST, "arpeggiator/chordPolyphony", &kArpeggiatorChordPolyphonyNames},
    Entry{Shared::ARP_HOST, "arpeggiator/chordProbability", &kArpeggiatorChordProbabilityNames},
    Entry{Shared::ARP_HOST, "arpeggiator/gate", &kArpeggiatorGateNames},
    Entry{Shared::ARP_HOST, "arpeggiator/glideProbability", &kArpeggiatorGlideProbabilityNames},
    Entry{Shared::ARP_HOST, "arpeggiator/noteProbability", &kArpeggiatorNoteProbabilityNames},
    Entry{Shared::ARP_HOST, "arpeggiator/ratchetAmount", &kArpeggiatorRatchetAmountNames},
    Entry{Shared::ARP_HOST, "arpeggiator/ratchetProbability", &kArpeggiatorRatchetProbabilityNames},
    Entry{Shared::ARP_HOST, "arpeggiator/rate", &kArpeggiatorRateNames},
    Entry{Shared::ARP_HOST, "arpeggiator/rhythm", &kArpeggiatorRhythmNames},
    Entry{Shared::ARP_HOST, "arpeggiator/sequenceLength", &kArpeggiatorSequenceLengthNames},
    Entry{Shared::ARP_HOST, "arpeggiator/spreadGate", &kArpeggiatorSpreadGateNames},
    Entry{Shared::ARP_HOST, "arpeggiator/spreadOctave", &kArpeggiatorSpreadOctaveNames},
    Entry{Shared::ARP_HOST, "arpeggiator/spreadVelocity", &kArpeggiatorSpreadVelocityNames},
    Entry{Shared::ARP_HOST, "arpeggiator/swapProbability", &kArpeggiatorSwapProbabilityNames},
    Entry{Shared::EFFECTS, "audioCompressor/attack", &kAttackNames},
    Entry{Shared::EFFECTS, "audioCompressor/compBlend", &kAudioCompressorCompBlendNames},
    Entry{Shared::EFFECTS, "audioCompressor/compHPF", &kAudioCompressorCompHPFNames},
    Entry{Shared::EFFECTS, "audioCompressor/ratio", &kAudioCompressorRatioNames},
    Entry{Shared::EFFECTS, "audioCompressor/release", &kAudioCompressorReleaseNames},
    Entry{Shared::EFFECTS, "clippingAmount", &kClippingAmountNames},
    Entry{Shared::EFFECTS, "delay/pingPong", &kDelayPingPongNames},
    Entry{Shared::EFFECTS, "delay/syncLevel", &kDelaySyncLevelNames},
    Entry{Shared::EFFECTS, "delay/type", &kDelayTypeNames},
    Entry{Shared::EFFECTS, "filterRoute", &kFilterRouteNames},
    Entry{Shared::EFFECTS, "hpfMode", &kHpfModeNames},
    Entry{Shared::EFFECTS, "lpfMode", &kLpfModeNames},
    Entry{Shared::EFFECTS, "modFXType", &kModFXTypeNames},
    Entry{Shared::EFFECTS, "sidechain/attack", &kReverbCompressorAttackNames},
    Entry{Shared::EFFECTS, "sidechain/release", &kReverbCompressorReleaseNames},
    Entry{Shared::EFFECTS, "sidechain/syncLevel", &kSidechainSyncLevelNames},
    Entry{Shared::EFFECTS, "stutter/quantized", &kStutterQuantizedNames},
}};

static_assert(std::ranges::adjacent_find(kEntries,
                                         [](const Entry& a, const Entry& b) {
	                                         return key_of(a.owner, a.path) >= key_of(b.owner, b.path);
                                         })
                  == kEntries.end(),
              "kEntries is searched by owner and path: keep it sorted, with no key twice");

/// The rows whose names differ while no audio input is plugged in, sorted as kEntries is.
constexpr std::array<Entry, 2> kUnpluggedEntries{{
    Entry{plugin::kDelugeInstrument, "osc1/type", &kOsc1TypeUnpluggedNames},
    Entry{plugin::kDelugeInstrument, "osc2/type", &kOsc1TypeUnpluggedNames},
}};
static_assert(std::ranges::adjacent_find(kUnpluggedEntries,
                                         [](const Entry& a, const Entry& b) {
	                                         return key_of(a.owner, a.path) >= key_of(b.owner, b.path);
                                         })
                  == kUnpluggedEntries.end(),
              "kUnpluggedEntries is searched by owner and path: keep it sorted, with no key twice");

/// @brief A param's name.
struct ParamName {
	ParamId id;
	String name;
};

/// The Deluge instrument's own params, sorted by id: the shared ones aside.
constexpr std::array<ParamName, 56> kInstrumentParamNames{{
    ParamName{ParamId{0}, STRING_FOR_PARAM_LOCAL_OSC_A_VOLUME},              // oscAVolume
    ParamName{ParamId{1}, STRING_FOR_PARAM_LOCAL_OSC_B_VOLUME},              // oscBVolume
    ParamName{ParamId{2}, STRING_FOR_PARAM_LOCAL_VOLUME},                    // volume
    ParamName{ParamId{3}, STRING_FOR_PARAM_LOCAL_NOISE_VOLUME},              // noiseVolume
    ParamName{ParamId{4}, STRING_FOR_PARAM_LOCAL_MODULATOR_0_VOLUME},        // modulator1Volume
    ParamName{ParamId{5}, STRING_FOR_PARAM_LOCAL_MODULATOR_1_VOLUME},        // modulator2Volume
    ParamName{ParamId{6}, STRING_FOR_WAVEFOLDER},                            // waveFold
    ParamName{ParamId{7}, STRING_FOR_PARAM_LOCAL_MODULATOR_0_FEEDBACK},      // modulator1Feedback
    ParamName{ParamId{8}, STRING_FOR_PARAM_LOCAL_MODULATOR_1_FEEDBACK},      // modulator2Feedback
    ParamName{ParamId{9}, STRING_FOR_PARAM_LOCAL_CARRIER_0_FEEDBACK},        // carrier1Feedback
    ParamName{ParamId{10}, STRING_FOR_PARAM_LOCAL_CARRIER_1_FEEDBACK},       // carrier2Feedback
    ParamName{ParamId{11}, STRING_FOR_PARAM_LOCAL_LPF_RESONANCE},            // lpfResonance
    ParamName{ParamId{12}, STRING_FOR_PARAM_LOCAL_HPF_RESONANCE},            // hpfResonance
    ParamName{ParamId{13}, STRING_FOR_PARAM_LOCAL_ENV_0_SUSTAIN},            // env1Sustain
    ParamName{ParamId{14}, STRING_FOR_PARAM_LOCAL_ENV_1_SUSTAIN},            // env2Sustain
    ParamName{ParamId{15}, STRING_FOR_PARAM_LOCAL_ENV_2_SUSTAIN},            // env3Sustain
    ParamName{ParamId{16}, STRING_FOR_PARAM_LOCAL_ENV_3_SUSTAIN},            // env4Sustain
    ParamName{ParamId{17}, STRING_FOR_PARAM_LOCAL_LPF_MORPH},                // lpfMorph
    ParamName{ParamId{18}, STRING_FOR_PARAM_LOCAL_HPF_MORPH},                // hpfMorph
    ParamName{ParamId{19}, STRING_FOR_PARAM_LOCAL_OSC_A_PHASE_WIDTH},        // oscAPhaseWidth
    ParamName{ParamId{20}, STRING_FOR_PARAM_LOCAL_OSC_B_PHASE_WIDTH},        // oscBPhaseWidth
    ParamName{ParamId{21}, STRING_FOR_PARAM_LOCAL_OSC_A_WAVE_INDEX},         // oscAWavetablePosition
    ParamName{ParamId{22}, STRING_FOR_PARAM_LOCAL_OSC_B_WAVE_INDEX},         // oscBWavetablePosition
    ParamName{ParamId{23}, STRING_FOR_PARAM_LOCAL_PAN},                      // pan
    ParamName{ParamId{24}, STRING_FOR_PARAM_LOCAL_LPF_FREQ},                 // lpfFrequency
    ParamName{ParamId{25}, STRING_FOR_PARAM_LOCAL_PITCH_ADJUST},             // pitch
    ParamName{ParamId{26}, STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST},       // oscAPitch
    ParamName{ParamId{27}, STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST},       // oscBPitch
    ParamName{ParamId{28}, STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST}, // modulator1Pitch
    ParamName{ParamId{29}, STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST}, // modulator2Pitch
    ParamName{ParamId{30}, STRING_FOR_PARAM_LOCAL_HPF_FREQ},                 // hpfFrequency
    ParamName{ParamId{31}, STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ_1},         // lfo2Rate
    ParamName{ParamId{32}, STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ_2},         // lfo4Rate
    ParamName{ParamId{33}, STRING_FOR_PARAM_LOCAL_ENV_0_ATTACK},             // env1Attack
    ParamName{ParamId{34}, STRING_FOR_PARAM_LOCAL_ENV_1_ATTACK},             // env2Attack
    ParamName{ParamId{35}, STRING_FOR_PARAM_LOCAL_ENV_2_ATTACK},             // env3Attack
    ParamName{ParamId{36}, STRING_FOR_PARAM_LOCAL_ENV_3_ATTACK},             // env4Attack
    ParamName{ParamId{37}, STRING_FOR_PARAM_LOCAL_ENV_0_DECAY},              // env1Decay
    ParamName{ParamId{38}, STRING_FOR_PARAM_LOCAL_ENV_1_DECAY},              // env2Decay
    ParamName{ParamId{39}, STRING_FOR_PARAM_LOCAL_ENV_2_DECAY},              // env3Decay
    ParamName{ParamId{40}, STRING_FOR_PARAM_LOCAL_ENV_3_DECAY},              // env4Decay
    ParamName{ParamId{41}, STRING_FOR_PARAM_LOCAL_ENV_0_RELEASE},            // env1Release
    ParamName{ParamId{42}, STRING_FOR_PARAM_LOCAL_ENV_1_RELEASE},            // env2Release
    ParamName{ParamId{43}, STRING_FOR_PARAM_LOCAL_ENV_2_RELEASE},            // env3Release
    ParamName{ParamId{44}, STRING_FOR_PARAM_LOCAL_ENV_3_RELEASE},            // env4Release
    ParamName{ParamId{45}, STRING_FOR_PARAM_GLOBAL_VOLUME_POST_FX},          // volumePostFX
    ParamName{ParamId{46}, STRING_FOR_PARAM_GLOBAL_VOLUME_POST_REVERB_SEND}, // volumePostReverbSend
    ParamName{ParamId{47}, STRING_FOR_PARAM_GLOBAL_REVERB_AMOUNT},           // reverbAmount
    ParamName{ParamId{48}, STRING_FOR_PARAM_GLOBAL_MOD_FX_DEPTH},            // modFXDepth
    ParamName{ParamId{49}, STRING_FOR_PARAM_GLOBAL_DELAY_FEEDBACK},          // delayFeedback
    ParamName{ParamId{50}, STRING_FOR_PARAM_GLOBAL_DELAY_RATE},              // delayRate
    ParamName{ParamId{51}, STRING_FOR_PARAM_GLOBAL_MOD_FX_RATE},             // modFXRate
    ParamName{ParamId{52}, STRING_FOR_PARAM_GLOBAL_LFO_FREQ_1},              // lfo1Rate
    ParamName{ParamId{53}, STRING_FOR_PARAM_GLOBAL_LFO_FREQ_2},              // lfo3Rate
    ParamName{ParamId{54}, STRING_FOR_PARAM_GLOBAL_ARP_RATE},                // arpRate
    ParamName{ParamId{116}, STRING_FOR_PORTAMENTO},                          // portamento
}};

/// The global effectable's own params, sorted by id: the shared ones aside.
constexpr std::array<ParamName, 17> kGlobalEffectableParamNames{{
    ParamName{ParamId{116}, STRING_FOR_MOD_FX_RATE},         // modFXRate
    ParamName{ParamId{117}, STRING_FOR_MOD_FX_DEPTH},        // modFXDepth
    ParamName{ParamId{118}, STRING_FOR_DELAY_RATE},          // delayRate
    ParamName{ParamId{119}, STRING_FOR_DELAY_AMOUNT},        // delayFeedback
    ParamName{ParamId{120}, STRING_FOR_ARP_RATE_MENU_TITLE}, // arpRate
    ParamName{ParamId{121}, STRING_FOR_PAN},                 // pan
    ParamName{ParamId{122}, STRING_FOR_LPF_FREQUENCY},       // lpfFrequency
    ParamName{ParamId{123}, STRING_FOR_LPF_RESONANCE},       // lpfResonance
    ParamName{ParamId{124}, STRING_FOR_LPF_MORPH},           // lpfMorph
    ParamName{ParamId{125}, STRING_FOR_HPF_FREQUENCY},       // hpfFrequency
    ParamName{ParamId{126}, STRING_FOR_HPF_RESONANCE},       // hpfResonance
    ParamName{ParamId{127}, STRING_FOR_HPF_MORPH},           // hpfMorph
    ParamName{ParamId{128}, STRING_FOR_REVERB_AMOUNT},       // reverbAmount
    ParamName{ParamId{129}, STRING_FOR_MASTER_LEVEL},        // volume
    ParamName{ParamId{130}, STRING_FOR_SIDECHAIN_LEVEL},     // sidechainCompressorVolume
    ParamName{ParamId{131}, STRING_FOR_MASTER_PITCH},        // pitchAdjust
    ParamName{ParamId{132}, STRING_FOR_TEMPO},               // tempo
}};

/// The params both engines have under one id and name, sorted by id: each engine's lookup falls through to them.
constexpr std::array<ParamName, 26> kSharedParamNames{{
    ParamName{ParamId{90}, STRING_FOR_STUTTER_RATE},                        // stutterRate
    ParamName{ParamId{91}, STRING_FOR_BASS},                                // bass
    ParamName{ParamId{92}, STRING_FOR_TREBLE},                              // treble
    ParamName{ParamId{93}, STRING_FOR_BASS_FREQUENCY},                      // bassFreq
    ParamName{ParamId{94}, STRING_FOR_TREBLE_FREQUENCY},                    // trebleFreq
    ParamName{ParamId{95}, STRING_FOR_DECIMATION},                          // sampleRateReduction
    ParamName{ParamId{96}, STRING_FOR_BITCRUSH},                            // bitcrushAmount
    ParamName{ParamId{97}, STRING_FOR_MODFX_OFFSET},                        // modFXOffset
    ParamName{ParamId{98}, STRING_FOR_MODFX_FEEDBACK},                      // modFXFeedback
    ParamName{ParamId{99}, STRING_FOR_SIDECHAIN_SHAPE},                     // compressorShape
    ParamName{ParamId{100}, STRING_FOR_THRESHOLD},                          // compressorThreshold
    ParamName{ParamId{101}, STRING_FOR_ARP_GATE_MENU_TITLE},                // arpGate
    ParamName{ParamId{102}, STRING_FOR_ARP_RHYTHM_MENU_TITLE},              // rhythm
    ParamName{ParamId{103}, STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE},     // sequenceLength
    ParamName{ParamId{104}, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE},     // chordPolyphony
    ParamName{ParamId{105}, STRING_FOR_ARP_RATCHETS_MENU_TITLE},            // ratchetAmount
    ParamName{ParamId{106}, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE},        // noteProbability
    ParamName{ParamId{107}, STRING_FOR_REVERSE_PROBABILITY_MENU_TITLE},     // reverseProbability
    ParamName{ParamId{108}, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE},    // bassProbability
    ParamName{ParamId{109}, STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE},    // swapProbability
    ParamName{ParamId{110}, STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE},   // glideProbability
    ParamName{ParamId{111}, STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE},   // chordProbability
    ParamName{ParamId{112}, STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE}, // ratchetProbability
    ParamName{ParamId{113}, STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE},         // spreadGate
    ParamName{ParamId{114}, STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE},       // spreadOctave
    ParamName{ParamId{115}, STRING_FOR_SPREAD_VELOCITY_MENU_TITLE},         // spreadVelocity
}};

constexpr auto id_of(const ParamName& param) {
	return param.id;
}

constexpr bool sorted_and_distinct(std::span<const ParamName> table) {
	return std::ranges::is_sorted(table, std::ranges::less{}, id_of)
	       && std::ranges::adjacent_find(table, std::ranges::equal_to{}, id_of) == table.end();
}
static_assert(sorted_and_distinct(kInstrumentParamNames) && sorted_and_distinct(kGlobalEffectableParamNames)
              && sorted_and_distinct(kSharedParamNames));

/// @brief Whether @p table names none of the shared ids, so falling through to the shared names changes no answer.
constexpr bool shares_no_id(std::span<const ParamName> table) {
	return std::ranges::none_of(table, [](const ParamName& param) {
		return std::ranges::binary_search(kSharedParamNames, param.id, std::ranges::less{}, id_of);
	});
}
static_assert(shares_no_id(kInstrumentParamNames) && shares_no_id(kGlobalEffectableParamNames));

/// @brief @p id's name in @p table, else in the shared names.
String name_in(std::span<const ParamName> table, ParamId id) {
	for (const std::span<const ParamName> names : {table, std::span<const ParamName>{kSharedParamNames}}) {
		const auto it = std::ranges::lower_bound(names, id, std::ranges::less{}, id_of);
		if (it != names.end() && it->id == id) {
			return it->name;
		}
	}
	return STRING_FOR_NONE;
}

/// @brief The row of @p entries (kEntries unless given) at @p owner's index and @p path, or nullptr.
constexpr const SettingNames* find_entry(uint8_t owner, std::string_view path,
                                         std::span<const Entry> entries = kEntries) {
	const auto key = key_of(owner, path);
	const auto it = std::ranges::lower_bound(entries, key, std::ranges::less{},
	                                         [](const Entry& e) { return key_of(e.owner, e.path); });
	return it != entries.end() && key_of(it->owner, it->path) == key ? it->names : nullptr;
}

/// @brief The shared tables the owner at @p owner's index shows besides its own rows.
constexpr std::span<const Shared> shared_of(uint8_t owner) {
	static constexpr std::array kArp{Shared::ARP_SETTINGS, Shared::ARP_HOST};
	static constexpr std::array kInstrument{Shared::ARP_SETTINGS, Shared::EFFECTS};
	static constexpr std::array kEffects{Shared::EFFECTS};
	if (owner == NamesOwner{SettingGroupId::CLIP_ARP}.index()
	    || owner == NamesOwner{SettingGroupId::LANE_ARP}.index()) {
		return kArp;
	}
	if (owner == NamesOwner{plugin::kDelugeInstrument}.index()) {
		return kInstrument;
	}
	if (owner == NamesOwner{plugin::kGlobalEffectable}.index()) {
		return kEffects;
	}
	return {};
}

static_assert(std::ranges::none_of(kEntries,
                                   [](const Entry& e) {
	                                   return std::ranges::any_of(shared_of(e.owner), [&](Shared shared) {
		                                   return find_entry(std::to_underlying(shared), e.path) != nullptr;
	                                   });
                                   }),
              "an owner's own row would hide a shared table's row of the same path");

} // namespace menu_tables
} // namespace

const SettingNames* setting_names(NamesOwner owner, std::string_view path) {
	if (const SettingNames* names = find_entry(owner.index(), path)) {
		return names;
	}
	for (const Shared shared : shared_of(owner.index())) {
		if (const SettingNames* names = find_entry(std::to_underlying(shared), path)) {
			return names;
		}
	}
	return nullptr;
}

const SettingNames* setting_names(NamesOwner owner, std::string_view path, const plugin::SettingContext& context) {
	if (!context.audio_inputs_present) {
		if (const SettingNames* names = find_entry(owner.index(), path, kUnpluggedEntries)) {
			return names;
		}
	}
	return setting_names(owner, path);
}

String param_name(plugin::PluginId engine, ParamId id, const ParamNameContext& state) {
	// The ladder filters repurpose morph, so legacy names it for what it does there; the SVF modes really morph.
	if (engine == plugin::kDelugeInstrument) {
		if (id == instruments::classic::param::kLpfMorph
		    && state.lpf_mode <= effects::FilterMode::TRANSISTOR_24DB_DRIVE) {
			return STRING_FOR_LPF_DRIVE;
		}
		if (id == instruments::classic::param::kHpfMorph && state.hpf_mode == effects::FilterMode::HPLADDER) {
			return STRING_FOR_HPF_FM;
		}
		return name_in(kInstrumentParamNames, id);
	}
	if (engine == plugin::kGlobalEffectable) {
		if (id == host::chains::param::kLpfMorph && state.lpf_mode <= effects::FilterMode::TRANSISTOR_24DB_DRIVE) {
			return STRING_FOR_LPF_DRIVE;
		}
		if (id == host::chains::param::kHpfMorph && state.hpf_mode == effects::FilterMode::HPLADDER) {
			return STRING_FOR_HPF_FM;
		}
		return name_in(kGlobalEffectableParamNames, id);
	}
	return STRING_FOR_NONE;
}

} // namespace deluge::l10n
