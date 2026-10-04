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

#pragma once

#include "engine-api/types/param.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace deluge::instrument {

/// @brief The ids of the Deluge instrument's params. Pinned forever by tests/engines/deluge/params_spec.cpp: an id
/// never changes, is never reused, and a new param appends.
namespace param {
using engine::ParamId;
inline constexpr ParamId kOscAVolume{0};
inline constexpr ParamId kOscBVolume{1};
inline constexpr ParamId kVolume{2};
inline constexpr ParamId kNoiseVolume{3};
inline constexpr ParamId kModulator1Volume{4};
inline constexpr ParamId kModulator2Volume{5};
inline constexpr ParamId kWaveFold{6};
inline constexpr ParamId kModulator1Feedback{7};
inline constexpr ParamId kModulator2Feedback{8};
inline constexpr ParamId kCarrier1Feedback{9};
inline constexpr ParamId kCarrier2Feedback{10};
inline constexpr ParamId kLpfResonance{11};
inline constexpr ParamId kHpfResonance{12};
inline constexpr ParamId kEnv1Sustain{13};
inline constexpr ParamId kEnv2Sustain{14};
inline constexpr ParamId kEnv3Sustain{15};
inline constexpr ParamId kEnv4Sustain{16};
inline constexpr ParamId kLpfMorph{17};
inline constexpr ParamId kHpfMorph{18};
inline constexpr ParamId kOscAPhaseWidth{19};
inline constexpr ParamId kOscBPhaseWidth{20};
inline constexpr ParamId kOscAWavetablePosition{21};
inline constexpr ParamId kOscBWavetablePosition{22};
inline constexpr ParamId kPan{23};
inline constexpr ParamId kLpfFrequency{24};
inline constexpr ParamId kPitch{25};
inline constexpr ParamId kOscAPitch{26};
inline constexpr ParamId kOscBPitch{27};
inline constexpr ParamId kModulator1Pitch{28};
inline constexpr ParamId kModulator2Pitch{29};
inline constexpr ParamId kHpfFrequency{30};
inline constexpr ParamId kLfo2Rate{31};
inline constexpr ParamId kLfo4Rate{32};
inline constexpr ParamId kEnv1Attack{33};
inline constexpr ParamId kEnv2Attack{34};
inline constexpr ParamId kEnv3Attack{35};
inline constexpr ParamId kEnv4Attack{36};
inline constexpr ParamId kEnv1Decay{37};
inline constexpr ParamId kEnv2Decay{38};
inline constexpr ParamId kEnv3Decay{39};
inline constexpr ParamId kEnv4Decay{40};
inline constexpr ParamId kEnv1Release{41};
inline constexpr ParamId kEnv2Release{42};
inline constexpr ParamId kEnv3Release{43};
inline constexpr ParamId kEnv4Release{44};
inline constexpr ParamId kVolumePostFX{45};
inline constexpr ParamId kVolumePostReverbSend{46};
inline constexpr ParamId kReverbAmount{47};
inline constexpr ParamId kModFXDepth{48};
inline constexpr ParamId kDelayFeedback{49};
inline constexpr ParamId kDelayRate{50};
inline constexpr ParamId kModFXRate{51};
inline constexpr ParamId kLfo1Rate{52};
inline constexpr ParamId kLfo3Rate{53};
inline constexpr ParamId kArpRate{54};
inline constexpr ParamId kStutterRate{90};
inline constexpr ParamId kBass{91};
inline constexpr ParamId kTreble{92};
inline constexpr ParamId kBassFreq{93};
inline constexpr ParamId kTrebleFreq{94};
inline constexpr ParamId kSampleRateReduction{95};
inline constexpr ParamId kBitcrushAmount{96};
inline constexpr ParamId kModFXOffset{97};
inline constexpr ParamId kModFXFeedback{98};
inline constexpr ParamId kCompressorShape{99};
inline constexpr ParamId kCompressorThreshold{100};
inline constexpr ParamId kArpGate{101};
inline constexpr ParamId kRhythm{102};
inline constexpr ParamId kSequenceLength{103};
inline constexpr ParamId kChordPolyphony{104};
inline constexpr ParamId kRatchetAmount{105};
inline constexpr ParamId kNoteProbability{106};
inline constexpr ParamId kReverseProbability{107};
inline constexpr ParamId kBassProbability{108};
inline constexpr ParamId kSwapProbability{109};
inline constexpr ParamId kGlideProbability{110};
inline constexpr ParamId kChordProbability{111};
inline constexpr ParamId kRatchetProbability{112};
inline constexpr ParamId kSpreadGate{113};
inline constexpr ParamId kSpreadOctave{114};
inline constexpr ParamId kSpreadVelocity{115};
inline constexpr ParamId kPortamento{116};
} // namespace param

/// @brief The first id of the params the runtime keeps unpatched (the sound's own effects and arpeggiator, its
/// portamento). Ids below it are the runtime's patched params, numbered as the runtime numbers them.
inline constexpr uint32_t kUnpatchedFirstId = 90;

/// @brief Every param the Deluge instrument has, in id order.
inline constexpr std::array<engine::ParamInfo, 82> kParams{{
    {.id = param::kOscAVolume,
     .file_name = "oscAVolume",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::max()},
    {.id = param::kOscBVolume,
     .file_name = "oscBVolume",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::max()},
    {.id = param::kVolume, .file_name = "volume", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kNoiseVolume,
     .file_name = "noiseVolume",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModulator1Volume,
     .file_name = "modulator1Volume",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModulator2Volume,
     .file_name = "modulator2Volume",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kWaveFold,
     .file_name = "waveFold",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModulator1Feedback,
     .file_name = "modulator1Feedback",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModulator2Feedback,
     .file_name = "modulator2Feedback",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kCarrier1Feedback,
     .file_name = "carrier1Feedback",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kCarrier2Feedback,
     .file_name = "carrier2Feedback",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kLpfResonance, .file_name = "lpfResonance", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kHpfResonance,
     .file_name = "hpfResonance",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kEnv1Sustain, .file_name = "env1Sustain", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv2Sustain,
     .file_name = "env2Sustain",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = -23},
    {.id = param::kEnv3Sustain, .file_name = "env3Sustain", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv4Sustain, .file_name = "env4Sustain", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kLpfMorph,
     .file_name = "lpfMorph",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kHpfMorph,
     .file_name = "hpfMorph",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kOscAPhaseWidth, .file_name = "oscAPhaseWidth", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kOscBPhaseWidth, .file_name = "oscBPhaseWidth", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kOscAWavetablePosition, .file_name = "oscAWavetablePosition", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kOscBWavetablePosition, .file_name = "oscBWavetablePosition", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kPan, .file_name = "pan", .scope = engine::ParamScope::PER_VOICE, .bipolar = true},
    {.id = param::kLpfFrequency, .file_name = "lpfFrequency", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kPitch, .file_name = "pitch", .scope = engine::ParamScope::PER_VOICE, .bipolar = true},
    {.id = param::kOscAPitch, .file_name = "oscAPitch", .scope = engine::ParamScope::PER_VOICE, .bipolar = true},
    {.id = param::kOscBPitch, .file_name = "oscBPitch", .scope = engine::ParamScope::PER_VOICE, .bipolar = true},
    {.id = param::kModulator1Pitch,
     .file_name = "modulator1Pitch",
     .scope = engine::ParamScope::PER_VOICE,
     .bipolar = true},
    {.id = param::kModulator2Pitch,
     .file_name = "modulator2Pitch",
     .scope = engine::ParamScope::PER_VOICE,
     .bipolar = true},
    {.id = param::kHpfFrequency,
     .file_name = "hpfFrequency",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kLfo2Rate, .file_name = "lfo2Rate", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kLfo4Rate, .file_name = "lfo4Rate", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv1Attack, .file_name = "env1Attack", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv2Attack,
     .file_name = "env2Attack",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = -429496748},
    {.id = param::kEnv3Attack, .file_name = "env3Attack", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv4Attack, .file_name = "env4Attack", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv1Decay, .file_name = "env1Decay", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv2Decay,
     .file_name = "env2Decay",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = -429496748},
    {.id = param::kEnv3Decay, .file_name = "env3Decay", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv4Decay, .file_name = "env4Decay", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv1Release, .file_name = "env1Release", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv2Release,
     .file_name = "env2Release",
     .scope = engine::ParamScope::PER_VOICE,
     .default_value = -429496748},
    {.id = param::kEnv3Release, .file_name = "env3Release", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kEnv4Release, .file_name = "env4Release", .scope = engine::ParamScope::PER_VOICE},
    {.id = param::kVolumePostFX, .file_name = "volumePostFX", .default_value = 1288490152},
    {.id = param::kVolumePostReverbSend, .file_name = "volumePostReverbSend"},
    {.id = param::kReverbAmount, .file_name = "reverbAmount", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModFXDepth, .file_name = "modFXDepth"},
    {.id = param::kDelayFeedback, .file_name = "delayFeedback", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kDelayRate, .file_name = "delayRate"},
    {.id = param::kModFXRate, .file_name = "modFXRate"},
    {.id = param::kLfo1Rate, .file_name = "lfo1Rate", .default_value = 429496702},
    {.id = param::kLfo3Rate, .file_name = "lfo3Rate", .default_value = 429496702},
    {.id = param::kArpRate, .file_name = "arpRate"},
    {.id = param::kStutterRate, .file_name = "stutterRate"},
    {.id = param::kBass, .file_name = "bass"},
    {.id = param::kTreble, .file_name = "treble"},
    {.id = param::kBassFreq, .file_name = "bassFreq"},
    {.id = param::kTrebleFreq, .file_name = "trebleFreq"},
    {.id = param::kSampleRateReduction,
     .file_name = "sampleRateReduction",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kBitcrushAmount, .file_name = "bitcrushAmount", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModFXOffset, .file_name = "modFXOffset"},
    {.id = param::kModFXFeedback, .file_name = "modFXFeedback"},
    {.id = param::kCompressorShape, .file_name = "compressorShape", .default_value = -601295438},
    {.id = param::kCompressorThreshold, .file_name = "compressorThreshold"},
    {.id = param::kArpGate, .file_name = "arpGate"},
    {.id = param::kRhythm, .file_name = "rhythm", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSequenceLength, .file_name = "sequenceLength", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kChordPolyphony, .file_name = "chordPolyphony", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kRatchetAmount, .file_name = "ratchetAmount", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kNoteProbability,
     .file_name = "noteProbability",
     .default_value = std::numeric_limits<int32_t>::max()},
    {.id = param::kReverseProbability,
     .file_name = "reverseProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kBassProbability,
     .file_name = "bassProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSwapProbability,
     .file_name = "swapProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kGlideProbability,
     .file_name = "glideProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kChordProbability,
     .file_name = "chordProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kRatchetProbability,
     .file_name = "ratchetProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSpreadGate, .file_name = "spreadGate", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSpreadOctave, .file_name = "spreadOctave", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSpreadVelocity, .file_name = "spreadVelocity", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kPortamento, .file_name = "portamento", .default_value = std::numeric_limits<int32_t>::min()},
}};

/// @brief The two runs of ids the instrument's params are stored in: its patched params, then its unpatched ones.
inline constexpr std::array<engine::ParamBlock, 2> kParamBlocks{{
    {engine::ParamId{0}, 55},
    {engine::ParamId{kUnpatchedFirstId}, 27},
}};

/// @brief Which of the runtime's two param sets holds a param.
enum class RuntimeSet : uint8_t { PATCHED, UNPATCHED };

/// @brief Where the runtime keeps a param: its set and its index there.
struct RuntimeSlot {
	RuntimeSet set{RuntimeSet::PATCHED};
	uint8_t index{0};
	[[nodiscard]] constexpr bool operator==(const RuntimeSlot&) const = default;
};

/// @brief Where the runtime keeps the param @p id, or nullopt when the instrument has no such param. This map is
/// the only way the runtime's numbering may change.
[[nodiscard]] constexpr std::optional<RuntimeSlot> runtime_slot(engine::ParamId id) {
	if (kParamBlocks[0].contains(id)) {
		return RuntimeSlot{RuntimeSet::PATCHED, static_cast<uint8_t>(id.value)};
	}
	if (kParamBlocks[1].contains(id)) {
		return RuntimeSlot{RuntimeSet::UNPATCHED, static_cast<uint8_t>(id.value - kUnpatchedFirstId)};
	}
	return std::nullopt;
}

/// @brief The id of the param the runtime keeps at @p slot.
[[nodiscard]] constexpr engine::ParamId param_id(RuntimeSlot slot) {
	return engine::ParamId{slot.set == RuntimeSet::PATCHED ? slot.index : kUnpatchedFirstId + slot.index};
}

static_assert(kParams.size() == kParamBlocks[0].count + kParamBlocks[1].count);

} // namespace deluge::instrument
