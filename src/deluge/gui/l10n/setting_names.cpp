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
#include <tuple>
#include <utility>

namespace deluge::l10n {
namespace {
/// The menu-time tables: read when a menu or an Edit looks a setting up, never by the audio tier or an interrupt.
/// The device image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

using enum String;
using engine::ParamId;
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

constexpr std::array<String, 3> kModeChoices{{
    STRING_FOR_SUBTRACTIVE,
    STRING_FOR_FM,
    STRING_FOR_RINGMOD,
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

constexpr std::array<String, 12> kOsc1TypeChoices{{
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
    STRING_FOR_ARP_NOTE_MODE_MENU_TITLE, STRING_FOR_NOTE_MODE, kArpeggiatorNoteModeChoices, {}};
constexpr SettingNames kArpeggiatorNoteProbabilityNames{
    STRING_FOR_NOTE_PROBABILITY_MENU_TITLE, STRING_FOR_NOTE_PROBABILITY, {}, {}};
constexpr SettingNames kArpeggiatorNumOctavesNames{
    STRING_FOR_ARP_OCTAVES_MENU_TITLE, STRING_FOR_NUMBER_OF_OCTAVES, {}, {}};
constexpr SettingNames kArpeggiatorOctaveModeNames{
    STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE, STRING_FOR_OCTAVE_MODE, kArpeggiatorOctaveModeChoices, {}};
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
    STRING_FOR_FM_MOD2_DEST_MENU_TITLE, STRING_FOR_DESTINATION, kModulator2ToModulator1Choices, {}};
constexpr SettingNames kOsc1LinearInterpolationNames{
    STRING_FOR_SAMP_INTERP_MENU_TITLE, STRING_FOR_INTERPOLATION, kLinearInterpolationChoices, {}};
constexpr SettingNames kOsc1LoopModeNames{EMPTY_STRING, STRING_FOR_REPEAT_MODE, kOsc1LoopModeChoices, {}};
constexpr SettingNames kOsc1RetrigPhaseNames{STRING_FOR_OSC_R_PHASE_MENU_TITLE, STRING_FOR_RETRIGGER_PHASE, {}, {}};
constexpr SettingNames kOsc1ReversedNames{STRING_FOR_SAMP_REVERSE_MENU_TITLE, STRING_FOR_REVERSE, {}, {}};
constexpr SettingNames kOsc1TimeStretchAmountNames{STRING_FOR_SAMP_SPEED_MENU_TITLE, STRING_FOR_SPEED, {}, {}};
constexpr SettingNames kOsc1TimeStretchEnableNames{
    STRING_FOR_PITCH_SPEED, STRING_FOR_PITCH_SPEED, kOsc1TimeStretchEnableChoices, {}};
constexpr SettingNames kOsc1TransposeNames{STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, STRING_FOR_TRANSPOSE, {}, {}};
constexpr SettingNames kOsc1TypeNames{STRING_FOR_OSC_TYPE_MENU_TITLE, STRING_FOR_TYPE, kOsc1TypeChoices, {}};
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
/// @}

struct Entry {
	NamesOwner owner;
	std::string_view path;
	const SettingNames* names;
};

/// @brief The order kEntries is searched in: engines by name, then groups, then paths.
constexpr auto key_of(const NamesOwner& owner, std::string_view path) {
	return std::tuple{owner.engine.name, std::to_underlying(owner.group), path};
}

/// Every setting row a menu shows, sorted by owner and path.
constexpr std::array<Entry, 172> kEntries{{
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
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/arpMode", &kArpeggiatorArpModeNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/bassProbability", &kArpeggiatorBassProbabilityNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/chordPolyphony", &kArpeggiatorChordPolyphonyNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/chordProbability", &kArpeggiatorChordProbabilityNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/chordType", &kArpeggiatorChordTypeNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/gate", &kArpeggiatorGateNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/glideProbability", &kArpeggiatorGlideProbabilityNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/kitArp", &kArpeggiatorKitArpNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/mpeVelocity", &kArpeggiatorMpeVelocityNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/noteMode", &kArpeggiatorNoteModeNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/noteProbability", &kArpeggiatorNoteProbabilityNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/numOctaves", &kArpeggiatorNumOctavesNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/octaveMode", &kArpeggiatorOctaveModeNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/preset", &kArpeggiatorPresetNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/randomizerLock", &kArpeggiatorRandomizerLockNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/ratchetAmount", &kArpeggiatorRatchetAmountNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/ratchetProbability", &kArpeggiatorRatchetProbabilityNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/rate", &kArpeggiatorRateNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/rhythm", &kArpeggiatorRhythmNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/sequenceLength", &kArpeggiatorSequenceLengthNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/spreadGate", &kArpeggiatorSpreadGateNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/spreadOctave", &kArpeggiatorSpreadOctaveNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/spreadVelocity", &kArpeggiatorSpreadVelocityNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/stepRepeat", &kArpeggiatorStepRepeatNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/swapProbability", &kArpeggiatorSwapProbabilityNames},
    Entry{SettingGroupId::CLIP_ARP, "arpeggiator/syncLevel", &kArpeggiatorSyncLevelNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/arpMode", &kArpeggiatorArpModeNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/bassProbability", &kArpeggiatorBassProbabilityNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/chordPolyphony", &kArpeggiatorChordPolyphonyNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/chordProbability", &kArpeggiatorChordProbabilityNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/chordType", &kArpeggiatorChordTypeNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/gate", &kArpeggiatorGateNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/glideProbability", &kArpeggiatorGlideProbabilityNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/kitArp", &kArpeggiatorKitArpNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/mpeVelocity", &kArpeggiatorMpeVelocityNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/noteMode", &kArpeggiatorNoteModeNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/noteProbability", &kArpeggiatorNoteProbabilityNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/numOctaves", &kArpeggiatorNumOctavesNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/octaveMode", &kArpeggiatorOctaveModeNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/preset", &kArpeggiatorPresetNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/randomizerLock", &kArpeggiatorRandomizerLockNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/ratchetAmount", &kArpeggiatorRatchetAmountNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/ratchetProbability", &kArpeggiatorRatchetProbabilityNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/rate", &kArpeggiatorRateNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/rhythm", &kArpeggiatorRhythmNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/sequenceLength", &kArpeggiatorSequenceLengthNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/spreadGate", &kArpeggiatorSpreadGateNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/spreadOctave", &kArpeggiatorSpreadOctaveNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/spreadVelocity", &kArpeggiatorSpreadVelocityNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/stepRepeat", &kArpeggiatorStepRepeatNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/swapProbability", &kArpeggiatorSwapProbabilityNames},
    Entry{SettingGroupId::LANE_ARP, "arpeggiator/syncLevel", &kArpeggiatorSyncLevelNames},
    Entry{engine::kGlobalEffectable, "audioCompressor/attack", &kAttackNames},
    Entry{engine::kGlobalEffectable, "audioCompressor/compBlend", &kAudioCompressorCompBlendNames},
    Entry{engine::kGlobalEffectable, "audioCompressor/compHPF", &kAudioCompressorCompHPFNames},
    Entry{engine::kGlobalEffectable, "audioCompressor/ratio", &kAudioCompressorRatioNames},
    Entry{engine::kGlobalEffectable, "audioCompressor/release", &kAudioCompressorReleaseNames},
    Entry{engine::kGlobalEffectable, "clippingAmount", &kClippingAmountNames},
    Entry{engine::kGlobalEffectable, "delay/pingPong", &kDelayPingPongNames},
    Entry{engine::kGlobalEffectable, "delay/syncLevel", &kDelaySyncLevelNames},
    Entry{engine::kGlobalEffectable, "delay/type", &kDelayTypeNames},
    Entry{engine::kGlobalEffectable, "filterRoute", &kFilterRouteNames},
    Entry{engine::kGlobalEffectable, "hpfMode", &kHpfModeNames},
    Entry{engine::kGlobalEffectable, "lpfMode", &kLpfModeNames},
    Entry{engine::kGlobalEffectable, "modFXType", &kModFXTypeNames},
    Entry{engine::kGlobalEffectable, "sidechain/attack", &kReverbCompressorAttackNames},
    Entry{engine::kGlobalEffectable, "sidechain/release", &kReverbCompressorReleaseNames},
    Entry{engine::kGlobalEffectable, "sidechain/syncLevel", &kSidechainSyncLevelNames},
    Entry{engine::kGlobalEffectable, "stutter/quantized", &kStutterQuantizedNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/arpMode", &kArpeggiatorArpModeNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/chordType", &kArpeggiatorChordTypeNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/kitArp", &kArpeggiatorKitArpNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/mpeVelocity", &kArpeggiatorMpeVelocityNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/noteMode", &kArpeggiatorNoteModeNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/numOctaves", &kArpeggiatorNumOctavesNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/octaveMode", &kArpeggiatorOctaveModeNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/preset", &kArpeggiatorPresetNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/randomizerLock", &kArpeggiatorRandomizerLockNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/stepRepeat", &kArpeggiatorStepRepeatNames},
    Entry{engine::kDelugeInstrument, "arpeggiator/syncLevel", &kArpeggiatorSyncLevelNames},
    Entry{engine::kDelugeInstrument, "audioCompressor/attack", &kAttackNames},
    Entry{engine::kDelugeInstrument, "audioCompressor/compBlend", &kAudioCompressorCompBlendNames},
    Entry{engine::kDelugeInstrument, "audioCompressor/compHPF", &kAudioCompressorCompHPFNames},
    Entry{engine::kDelugeInstrument, "audioCompressor/ratio", &kAudioCompressorRatioNames},
    Entry{engine::kDelugeInstrument, "audioCompressor/release", &kAudioCompressorReleaseNames},
    Entry{engine::kDelugeInstrument, "clippingAmount", &kClippingAmountNames},
    Entry{engine::kDelugeInstrument, "delay/pingPong", &kDelayPingPongNames},
    Entry{engine::kDelugeInstrument, "delay/syncLevel", &kDelaySyncLevelNames},
    Entry{engine::kDelugeInstrument, "delay/type", &kDelayTypeNames},
    Entry{engine::kDelugeInstrument, "filterRoute", &kFilterRouteNames},
    Entry{engine::kDelugeInstrument, "hpfMode", &kHpfModeNames},
    Entry{engine::kDelugeInstrument, "lfo1/syncLevel", &kLfo1SyncLevelNames},
    Entry{engine::kDelugeInstrument, "lfo1/type", &kLfo1TypeNames},
    Entry{engine::kDelugeInstrument, "lfo2/syncLevel", &kLfo1SyncLevelNames},
    Entry{engine::kDelugeInstrument, "lfo2/type", &kLfo1TypeNames},
    Entry{engine::kDelugeInstrument, "lfo3/syncLevel", &kLfo1SyncLevelNames},
    Entry{engine::kDelugeInstrument, "lfo3/type", &kLfo1TypeNames},
    Entry{engine::kDelugeInstrument, "lfo4/syncLevel", &kLfo1SyncLevelNames},
    Entry{engine::kDelugeInstrument, "lfo4/type", &kLfo1TypeNames},
    Entry{engine::kDelugeInstrument, "lpfMode", &kLpfModeNames},
    Entry{engine::kDelugeInstrument, "maxVoices", &kMaxVoicesNames},
    Entry{engine::kDelugeInstrument, "midiOutput/channel", &kMidiOutputChannelNames},
    Entry{engine::kDelugeInstrument, "midiOutput/noteForDrum", &kMidiOutputNoteForDrumNames},
    Entry{engine::kDelugeInstrument, "modFXType", &kModFXTypeNames},
    Entry{engine::kDelugeInstrument, "mode", &kModeNames},
    Entry{engine::kDelugeInstrument, "modulator1/retrigPhase", &kModulator1RetrigPhaseNames},
    Entry{engine::kDelugeInstrument, "modulator1/transpose", &kModulator1TransposeNames},
    Entry{engine::kDelugeInstrument, "modulator2/retrigPhase", &kModulator1RetrigPhaseNames},
    Entry{engine::kDelugeInstrument, "modulator2/toModulator1", &kModulator2ToModulator1Names},
    Entry{engine::kDelugeInstrument, "modulator2/transpose", &kModulator1TransposeNames},
    Entry{engine::kDelugeInstrument, "osc1/linearInterpolation", &kOsc1LinearInterpolationNames},
    Entry{engine::kDelugeInstrument, "osc1/loopMode", &kOsc1LoopModeNames},
    Entry{engine::kDelugeInstrument, "osc1/retrigPhase", &kOsc1RetrigPhaseNames},
    Entry{engine::kDelugeInstrument, "osc1/reversed", &kOsc1ReversedNames},
    Entry{engine::kDelugeInstrument, "osc1/timeStretchAmount", &kOsc1TimeStretchAmountNames},
    Entry{engine::kDelugeInstrument, "osc1/timeStretchEnable", &kOsc1TimeStretchEnableNames},
    Entry{engine::kDelugeInstrument, "osc1/transpose", &kOsc1TransposeNames},
    Entry{engine::kDelugeInstrument, "osc1/type", &kOsc1TypeNames},
    Entry{engine::kDelugeInstrument, "osc2/linearInterpolation", &kOsc1LinearInterpolationNames},
    Entry{engine::kDelugeInstrument, "osc2/loopMode", &kOsc1LoopModeNames},
    Entry{engine::kDelugeInstrument, "osc2/oscillatorSync", &kOsc2OscillatorSyncNames},
    Entry{engine::kDelugeInstrument, "osc2/retrigPhase", &kOsc1RetrigPhaseNames},
    Entry{engine::kDelugeInstrument, "osc2/reversed", &kOsc1ReversedNames},
    Entry{engine::kDelugeInstrument, "osc2/timeStretchAmount", &kOsc1TimeStretchAmountNames},
    Entry{engine::kDelugeInstrument, "osc2/timeStretchEnable", &kOsc1TimeStretchEnableNames},
    Entry{engine::kDelugeInstrument, "osc2/transpose", &kOsc1TransposeNames},
    Entry{engine::kDelugeInstrument, "osc2/type", &kOsc1TypeNames},
    Entry{engine::kDelugeInstrument, "polyphonic", &kPolyphonicNames},
    Entry{engine::kDelugeInstrument, "sideChainSend", &kSideChainSendNames},
    Entry{engine::kDelugeInstrument, "sidechain/attack", &kReverbCompressorAttackNames},
    Entry{engine::kDelugeInstrument, "sidechain/release", &kReverbCompressorReleaseNames},
    Entry{engine::kDelugeInstrument, "sidechain/syncLevel", &kSidechainSyncLevelNames},
    Entry{engine::kDelugeInstrument, "stutter/quantized", &kStutterQuantizedNames},
    Entry{engine::kDelugeInstrument, "transpose", &kTransposeNames},
    Entry{engine::kDelugeInstrument, "unison/detune", &kUnisonDetuneNames},
    Entry{engine::kDelugeInstrument, "unison/num", &kUnisonNumNames},
    Entry{engine::kDelugeInstrument, "unison/spread", &kUnisonSpreadNames},
    Entry{engine::kDelugeInstrument, "voicePriority", &kPriorityNames},
}};

static_assert(std::ranges::adjacent_find(kEntries,
                                         [](const Entry& a, const Entry& b) {
	                                         return key_of(a.owner, a.path) >= key_of(b.owner, b.path);
                                         })
                  == kEntries.end(),
              "kEntries is searched by owner and path: keep it sorted, with no key twice");

/// @brief A param's name.
struct ParamName {
	ParamId id;
	String name;
};

/// The Deluge instrument's params, sorted by id.
constexpr std::array<ParamName, 82> kInstrumentParamNames{{
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
    ParamName{ParamId{90}, STRING_FOR_STUTTER_RATE},                         // stutterRate
    ParamName{ParamId{91}, STRING_FOR_BASS},                                 // bass
    ParamName{ParamId{92}, STRING_FOR_TREBLE},                               // treble
    ParamName{ParamId{93}, STRING_FOR_BASS_FREQUENCY},                       // bassFreq
    ParamName{ParamId{94}, STRING_FOR_TREBLE_FREQUENCY},                     // trebleFreq
    ParamName{ParamId{95}, STRING_FOR_DECIMATION},                           // sampleRateReduction
    ParamName{ParamId{96}, STRING_FOR_BITCRUSH},                             // bitcrushAmount
    ParamName{ParamId{97}, STRING_FOR_MODFX_OFFSET},                         // modFXOffset
    ParamName{ParamId{98}, STRING_FOR_MODFX_FEEDBACK},                       // modFXFeedback
    ParamName{ParamId{99}, STRING_FOR_SIDECHAIN_SHAPE},                      // compressorShape
    ParamName{ParamId{100}, STRING_FOR_THRESHOLD},                           // compressorThreshold
    ParamName{ParamId{101}, STRING_FOR_ARP_GATE_MENU_TITLE},                 // arpGate
    ParamName{ParamId{102}, STRING_FOR_ARP_RHYTHM_MENU_TITLE},               // rhythm
    ParamName{ParamId{103}, STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE},      // sequenceLength
    ParamName{ParamId{104}, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE},      // chordPolyphony
    ParamName{ParamId{105}, STRING_FOR_ARP_RATCHETS_MENU_TITLE},             // ratchetAmount
    ParamName{ParamId{106}, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE},         // noteProbability
    ParamName{ParamId{107}, STRING_FOR_REVERSE_PROBABILITY_MENU_TITLE},      // reverseProbability
    ParamName{ParamId{108}, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE},     // bassProbability
    ParamName{ParamId{109}, STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE},     // swapProbability
    ParamName{ParamId{110}, STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE},    // glideProbability
    ParamName{ParamId{111}, STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE},    // chordProbability
    ParamName{ParamId{112}, STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE},  // ratchetProbability
    ParamName{ParamId{113}, STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE},          // spreadGate
    ParamName{ParamId{114}, STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE},        // spreadOctave
    ParamName{ParamId{115}, STRING_FOR_SPREAD_VELOCITY_MENU_TITLE},          // spreadVelocity
    ParamName{ParamId{116}, STRING_FOR_PORTAMENTO},                          // portamento
}};

/// The global effectable's params, sorted by id.
constexpr std::array<ParamName, 43> kGlobalEffectableParamNames{{
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
    ParamName{ParamId{116}, STRING_FOR_MOD_FX_RATE},                        // modFXRate
    ParamName{ParamId{117}, STRING_FOR_MOD_FX_DEPTH},                       // modFXDepth
    ParamName{ParamId{118}, STRING_FOR_DELAY_RATE},                         // delayRate
    ParamName{ParamId{119}, STRING_FOR_DELAY_AMOUNT},                       // delayFeedback
    ParamName{ParamId{120}, STRING_FOR_ARP_RATE_MENU_TITLE},                // arpRate
    ParamName{ParamId{121}, STRING_FOR_PAN},                                // pan
    ParamName{ParamId{122}, STRING_FOR_LPF_FREQUENCY},                      // lpfFrequency
    ParamName{ParamId{123}, STRING_FOR_LPF_RESONANCE},                      // lpfResonance
    ParamName{ParamId{124}, STRING_FOR_LPF_MORPH},                          // lpfMorph
    ParamName{ParamId{125}, STRING_FOR_HPF_FREQUENCY},                      // hpfFrequency
    ParamName{ParamId{126}, STRING_FOR_HPF_RESONANCE},                      // hpfResonance
    ParamName{ParamId{127}, STRING_FOR_HPF_MORPH},                          // hpfMorph
    ParamName{ParamId{128}, STRING_FOR_REVERB_AMOUNT},                      // reverbAmount
    ParamName{ParamId{129}, STRING_FOR_MASTER_LEVEL},                       // volume
    ParamName{ParamId{130}, STRING_FOR_SIDECHAIN_LEVEL},                    // sidechainCompressorVolume
    ParamName{ParamId{131}, STRING_FOR_MASTER_PITCH},                       // pitchAdjust
    ParamName{ParamId{132}, STRING_FOR_TEMPO},                              // tempo
}};

constexpr auto id_of(const ParamName& param) {
	return param.id;
}

static_assert(std::ranges::is_sorted(kInstrumentParamNames, std::ranges::less{}, id_of)
              && std::ranges::adjacent_find(kInstrumentParamNames, std::ranges::equal_to{}, id_of)
                     == kInstrumentParamNames.end());
static_assert(std::ranges::is_sorted(kGlobalEffectableParamNames, std::ranges::less{}, id_of)
              && std::ranges::adjacent_find(kGlobalEffectableParamNames, std::ranges::equal_to{}, id_of)
                     == kGlobalEffectableParamNames.end());

String name_in(std::span<const ParamName> table, ParamId id) {
	const auto it = std::ranges::lower_bound(table, id, std::ranges::less{}, id_of);
	return it != table.end() && it->id == id ? it->name : STRING_FOR_NONE;
}

} // namespace menu_tables
} // namespace

const SettingNames* setting_names(NamesOwner owner, std::string_view path) {
	const auto key = key_of(owner, path);
	const auto it = std::ranges::lower_bound(kEntries, key, std::ranges::less{},
	                                         [](const Entry& e) { return key_of(e.owner, e.path); });
	return it != kEntries.end() && key_of(it->owner, it->path) == key ? it->names : nullptr;
}

String param_name(engine::EngineId engine, ParamId id, const ParamNameContext& state) {
	// The ladder filters repurpose morph, so legacy names it for what it does there; the SVF modes really morph.
	if (engine == engine::kDelugeInstrument) {
		if (id == instrument::param::kLpfMorph && state.lpf_mode <= effects::FilterMode::TRANSISTOR_24DB_DRIVE) {
			return STRING_FOR_LPF_DRIVE;
		}
		if (id == instrument::param::kHpfMorph && state.hpf_mode == effects::FilterMode::HPLADDER) {
			return STRING_FOR_HPF_FM;
		}
		return name_in(kInstrumentParamNames, id);
	}
	if (engine == engine::kGlobalEffectable) {
		if (id == global_effectable::param::kLpfMorph && state.lpf_mode <= effects::FilterMode::TRANSISTOR_24DB_DRIVE) {
			return STRING_FOR_LPF_DRIVE;
		}
		if (id == global_effectable::param::kHpfMorph && state.hpf_mode == effects::FilterMode::HPLADDER) {
			return STRING_FOR_HPF_FM;
		}
		return name_in(kGlobalEffectableParamNames, id);
	}
	return STRING_FOR_NONE;
}

} // namespace deluge::l10n
