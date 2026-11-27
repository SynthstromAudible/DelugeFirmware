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

#include "effects/param_row.h"
#include "effects/params.h"
#include "host/chains/params.h"
#include "plugin/types/param.h"
#include "plugin/types/param_kind.h"
#include "plugin/types/plugin_id.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace deluge::instruments::classic {

/// @brief The ids of the Deluge instrument's params. Pinned forever by tests/unit/instruments/classic/params_spec.cpp:
/// an id never changes, is never reused, and a new param appends.
namespace param {
using plugin::ParamId;
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

namespace detail {
inline constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
} // namespace detail

/// @brief Every param the Deluge instrument has, in id order. An effect's row is its effect param (effects/params.h)
/// at the instrument's id, default and scope.
inline constexpr std::array<plugin::ParamInfo, 82> kParams{{
    {.id = param::kOscAVolume,
     .file_name = "oscAVolume",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::max(),
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    {.id = param::kOscBVolume,
     .file_name = "oscBVolume",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::max(),
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    {.id = param::kVolume,
     .file_name = "volume",
     .scope = plugin::ParamScope::PER_VOICE,
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    {.id = param::kNoiseVolume,
     .file_name = "noiseVolume",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModulator1Volume,
     .file_name = "modulator1Volume",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::SMOOTHED},
    {.id = param::kModulator2Volume,
     .file_name = "modulator2Volume",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::SMOOTHED},
    {.id = param::kWaveFold,
     .file_name = "waveFold",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModulator1Feedback,
     .file_name = "modulator1Feedback",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::SMOOTHED},
    {.id = param::kModulator2Feedback,
     .file_name = "modulator2Feedback",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::SMOOTHED},
    {.id = param::kCarrier1Feedback,
     .file_name = "carrier1Feedback",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::SMOOTHED},
    {.id = param::kCarrier2Feedback,
     .file_name = "carrier2Feedback",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::SMOOTHED},
    effects::row(param::kLpfResonance, effects::params::kLpfResonance, {.scope = plugin::ParamScope::PER_VOICE}),
    effects::row(param::kHpfResonance, effects::params::kHpfResonance,
                 {.default_value = detail::kMin, .scope = plugin::ParamScope::PER_VOICE}),
    {.id = param::kEnv1Sustain, .file_name = "env1Sustain", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv2Sustain,
     .file_name = "env2Sustain",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = -23},
    {.id = param::kEnv3Sustain, .file_name = "env3Sustain", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv4Sustain, .file_name = "env4Sustain", .scope = plugin::ParamScope::PER_VOICE},
    effects::row(param::kLpfMorph, effects::params::kLpfMorph,
                 {.default_value = detail::kMin, .scope = plugin::ParamScope::PER_VOICE}),
    effects::row(param::kHpfMorph, effects::params::kHpfMorph,
                 {.default_value = detail::kMin, .scope = plugin::ParamScope::PER_VOICE}),
    {.id = param::kOscAPhaseWidth,
     .file_name = "oscAPhaseWidth",
     .scope = plugin::ParamScope::PER_VOICE,
     .mapping = plugin::MappingKind::UNIPOLAR},
    {.id = param::kOscBPhaseWidth,
     .file_name = "oscBPhaseWidth",
     .scope = plugin::ParamScope::PER_VOICE,
     .mapping = plugin::MappingKind::UNIPOLAR},
    {.id = param::kOscAWavetablePosition,
     .file_name = "oscAWavetablePosition",
     .scope = plugin::ParamScope::PER_VOICE,
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    {.id = param::kOscBWavetablePosition,
     .file_name = "oscBWavetablePosition",
     .scope = plugin::ParamScope::PER_VOICE,
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    {.id = param::kPan,
     .file_name = "pan",
     .scope = plugin::ParamScope::PER_VOICE,
     .mapping = plugin::MappingKind::PAN,
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT | plugin::ParamFlag::SMOOTHED_USER_ONLY},
    effects::row(param::kLpfFrequency, effects::params::kLpfFrequency,
                 {.scope = plugin::ParamScope::PER_VOICE, .extra_flags = plugin::ParamFlag::SMOOTHED_USER_ONLY}),
    {.id = param::kPitch,
     .file_name = "pitch",
     .scope = plugin::ParamScope::PER_VOICE,
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kOscAPitch,
     .file_name = "oscAPitch",
     .scope = plugin::ParamScope::PER_VOICE,
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kOscBPitch,
     .file_name = "oscBPitch",
     .scope = plugin::ParamScope::PER_VOICE,
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kModulator1Pitch,
     .file_name = "modulator1Pitch",
     .scope = plugin::ParamScope::PER_VOICE,
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kModulator2Pitch,
     .file_name = "modulator2Pitch",
     .scope = plugin::ParamScope::PER_VOICE,
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    effects::row(param::kHpfFrequency, effects::params::kHpfFrequency,
                 {.default_value = detail::kMin,
                  .scope = plugin::ParamScope::PER_VOICE,
                  .extra_flags = plugin::ParamFlag::SMOOTHED_USER_ONLY}),
    {.id = param::kLfo2Rate, .file_name = "lfo2Rate", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kLfo4Rate, .file_name = "lfo4Rate", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv1Attack, .file_name = "env1Attack", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv2Attack,
     .file_name = "env2Attack",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = -429496748},
    {.id = param::kEnv3Attack, .file_name = "env3Attack", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv4Attack, .file_name = "env4Attack", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv1Decay, .file_name = "env1Decay", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv2Decay,
     .file_name = "env2Decay",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = -429496748},
    {.id = param::kEnv3Decay, .file_name = "env3Decay", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv4Decay, .file_name = "env4Decay", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv1Release, .file_name = "env1Release", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv2Release,
     .file_name = "env2Release",
     .scope = plugin::ParamScope::PER_VOICE,
     .default_value = -429496748},
    {.id = param::kEnv3Release, .file_name = "env3Release", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kEnv4Release, .file_name = "env4Release", .scope = plugin::ParamScope::PER_VOICE},
    {.id = param::kVolumePostFX,
     .file_name = "volumePostFX",
     .default_value = 1288490152,
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    {.id = param::kVolumePostReverbSend,
     .file_name = "volumePostReverbSend",
     .flags = plugin::ParamFlag::SMOOTHED_USER_ONLY},
    effects::row(param::kReverbAmount, effects::params::kReverbSend,
                 {.default_value = detail::kMin, .extra_flags = plugin::ParamFlag::SMOOTHED_USER_ONLY}),
    effects::row(param::kModFXDepth, effects::params::kModFxDepth, {.extra_flags = plugin::ParamFlag::SMOOTHED}),
    effects::row(param::kDelayFeedback, effects::params::kDelayFeedback,
                 {.default_value = detail::kMin, .extra_flags = plugin::ParamFlag::SMOOTHED}),
    effects::row(param::kDelayRate, effects::params::kDelayRate),
    effects::row(param::kModFXRate, effects::params::kModFxRate),
    {.id = param::kLfo1Rate, .file_name = "lfo1Rate", .default_value = 429496702},
    {.id = param::kLfo3Rate, .file_name = "lfo3Rate", .default_value = 429496702},
    {.id = param::kArpRate, .file_name = "arpRate"},
    effects::row(param::kStutterRate, effects::params::kStutterRate),
    effects::row(param::kBass, effects::params::kBass),
    effects::row(param::kTreble, effects::params::kTreble),
    effects::row(param::kBassFreq, effects::params::kBassFrequency),
    effects::row(param::kTrebleFreq, effects::params::kTrebleFrequency),
    effects::row(param::kSampleRateReduction, effects::params::kSampleRateReduction, {.default_value = detail::kMin}),
    effects::row(param::kBitcrushAmount, effects::params::kBitcrushAmount, {.default_value = detail::kMin}),
    effects::row(param::kModFXOffset, effects::params::kModFxOffset),
    effects::row(param::kModFXFeedback, effects::params::kModFxFeedback),
    effects::row(param::kCompressorShape, effects::params::kSidechainShape, {.default_value = -601295438}),
    effects::row(param::kCompressorThreshold, effects::params::kCompressorThreshold),
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

/// @brief The EQ's ids, in its param order (effects::params::kEqualizerParams).
inline constexpr std::array<plugin::ParamId, 4> kEqIds{param::kBass, param::kTreble, param::kBassFreq,
                                                       param::kTrebleFreq};
/// @brief The bitcrush's ids, in its param order (effects::params::kBitcrushParams).
inline constexpr std::array<plugin::ParamId, 2> kBitcrushIds{param::kBitcrushAmount, param::kSampleRateReduction};

static_assert(effects::built_from(kParams, kEqIds, effects::params::kEqualizerParams));
static_assert(effects::built_from(kParams, kBitcrushIds, effects::params::kBitcrushParams));

/// @brief The two runs of ids the instrument's params are stored in: its patched params, then its unpatched ones.
inline constexpr std::array<plugin::ParamBlock, 2> kParamBlocks{{
    {plugin::ParamId{0}, 55},
    {plugin::ParamId{kUnpatchedFirstId}, 27},
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
/// @param id A param id.
/// @return The runtime slot, or nullopt.
[[nodiscard]] constexpr std::optional<RuntimeSlot> runtime_slot(plugin::ParamId id) {
	if (kParamBlocks[0].contains(id)) {
		return RuntimeSlot{RuntimeSet::PATCHED, static_cast<uint8_t>(id.value)};
	}
	if (kParamBlocks[1].contains(id)) {
		return RuntimeSlot{RuntimeSet::UNPATCHED, static_cast<uint8_t>(id.value - kUnpatchedFirstId)};
	}
	return std::nullopt;
}

/// @brief The id of the param the runtime keeps at @p slot.
/// @param slot A runtime slot.
/// @return The param's id.
[[nodiscard]] constexpr plugin::ParamId param_id(RuntimeSlot slot) {
	return plugin::ParamId{slot.set == RuntimeSet::PATCHED ? slot.index : kUnpatchedFirstId + slot.index};
}

static_assert(kParams.size() == kParamBlocks[0].count + kParamBlocks[1].count);

/// @brief The id of the MPE X axis (pitch bend) in the host's expression space, which numbers the axes X, Y, Z.
inline constexpr plugin::ParamId kExpressionPitchBend{0};

/// @brief The id of the MPE Y axis (slide, the mod wheel in mono expression) in the host's expression space.
inline constexpr plugin::ParamId kExpressionSlide{1};

/// @brief The id of the MPE Z axis (pressure) in the host's expression space.
inline constexpr plugin::ParamId kExpressionPressure{2};

/// @brief The first of the sound's global patched params: the per-voice ones have lower ids.
inline constexpr plugin::ParamId kFirstGlobalParam = param::kVolumePostFX;

/// @brief The index the runtime keeps the constant param @p id at within its set: the id for a patched param, the id
/// less @c kUnpatchedFirstId for an unpatched one. An id of no block does not compile.
/// @param id A param id of the instrument.
/// @return The index in its runtime set.
[[nodiscard]] consteval uint8_t runtime_index(plugin::ParamId id) {
	const std::optional<RuntimeSlot> slot = runtime_slot(id);
	if (!slot) {
		std::unreachable();
	}
	return slot->index;
}

/// @brief The id of the param at @p index of a @p kind collection, as the runtime numbers its collections: the
/// patched set from 0, an unpatched set (an unnamed one included) from @c kUnpatchedFirstId, and a host space (MPE
/// expression, MIDI CC) by its own index.
/// @param kind  The runtime collection kind.
/// @param index The param's index in its collection.
/// @return The id, or nullopt for a patch cable, whose index is a packed word and no id, and for an index at or above
/// @c plugin::kCableDepthFlag, which no param has (a negative index cast to unsigned is one).
[[nodiscard]] constexpr std::optional<plugin::ParamId> runtime_param_id(plugin::ParamKind kind, uint32_t index) {
	if (index >= plugin::kCableDepthFlag) {
		return std::nullopt;
	}
	switch (kind) {
	case plugin::ParamKind::NONE:
	case plugin::ParamKind::UNPATCHED_SOUND:
	case plugin::ParamKind::UNPATCHED_GLOBAL:
		return plugin::ParamId{kUnpatchedFirstId + index};
	case plugin::ParamKind::PATCH_CABLE:
		return std::nullopt;
	case plugin::ParamKind::PATCHED:
	case plugin::ParamKind::STATIC:
	case plugin::ParamKind::MIDI:
	case plugin::ParamKind::EXPRESSION:
	case plugin::ParamKind::ARP:
		break;
	}
	return plugin::ParamId{index};
}

/// @brief The table row of the param @p id in a @p kind collection: the instrument's rows for a sound's patched set
/// (looked up across the whole table) and its unpatched block, the host chain's rows for a global effectable's.
/// @param kind The runtime collection kind.
/// @param id   The param id, as @c runtime_param_id numbers it.
/// @return The row, or nullptr when the collection has no such param (a host space has no rows).
[[nodiscard]] constexpr const plugin::ParamInfo* param_row(plugin::ParamKind kind, plugin::ParamId id) {
	switch (kind) {
	case plugin::ParamKind::PATCHED:
		return plugin::find_param(kParams, id);
	case plugin::ParamKind::NONE:
	case plugin::ParamKind::UNPATCHED_SOUND:
		return kParamBlocks[1].contains(id) ? plugin::find_param(kParams, id) : nullptr;
	case plugin::ParamKind::UNPATCHED_GLOBAL:
		return host::chains::kParamBlock.contains(id) ? plugin::find_param(host::chains::kParams, id) : nullptr;
	case plugin::ParamKind::STATIC:
	case plugin::ParamKind::MIDI:
	case plugin::ParamKind::PATCH_CABLE:
	case plugin::ParamKind::EXPRESSION:
	case plugin::ParamKind::ARP:
		break;
	}
	return nullptr;
}

/// @brief The table row of the param at @p index of a @p kind collection. The tables keep each block's rows dense and
/// in id order, so the row is found at its own position without a scan; an index off that layout is looked up.
/// @param kind  The runtime collection kind.
/// @param index The param's index in its collection.
/// @return The row, or nullptr when the collection has no such param.
[[nodiscard]] constexpr const plugin::ParamInfo* param_row_at(plugin::ParamKind kind, uint32_t index) {
	const std::optional<plugin::ParamId> id = runtime_param_id(kind, index);
	if (!id) {
		return nullptr;
	}
	std::span<const plugin::ParamInfo> table{kParams};
	std::size_t at = index;
	switch (kind) {
	case plugin::ParamKind::PATCHED:
		break;
	case plugin::ParamKind::NONE:
	case plugin::ParamKind::UNPATCHED_SOUND:
		at += kParamBlocks[0].count;
		break;
	case plugin::ParamKind::UNPATCHED_GLOBAL:
		table = host::chains::kParams;
		break;
	case plugin::ParamKind::STATIC:
	case plugin::ParamKind::MIDI:
	case plugin::ParamKind::PATCH_CABLE:
	case plugin::ParamKind::EXPRESSION:
	case plugin::ParamKind::ARP:
		return nullptr;
	}
	if (at < table.size() && table[at].id == *id) {
		return &table[at];
	}
	return param_row(kind, *id);
}

/// @brief Is the param the arpeggiator's rhythm? It is a sound's unpatched param.
/// @param kind The runtime collection kind.
/// @param id   The param id, as @c runtime_param_id numbers it.
/// @return True for the sound's rhythm param.
[[nodiscard]] constexpr bool is_arp_rhythm(plugin::ParamKind kind, plugin::ParamId id) {
	return kind == plugin::ParamKind::UNPATCHED_SOUND && id == param::kRhythm;
}

/// @brief Is the param the stutter rate? A sound and a global effectable both have it, under their own kind.
/// @param kind The runtime collection kind.
/// @param id   The param id, as @c runtime_param_id numbers it.
/// @return True for the stutter rate of an unpatched set.
[[nodiscard]] constexpr bool is_stutter_rate(plugin::ParamKind kind, plugin::ParamId id) {
	return (kind == plugin::ParamKind::UNPATCHED_GLOBAL || kind == plugin::ParamKind::UNPATCHED_SOUND)
	       && id == host::chains::param::kStutterRate;
}

/// @brief Does the param's knob swing both ways around a middle value? Every patch cable's depth does, as does the
/// pitch-bend expression axis and any param whose row is flagged @c ParamFlag::CENTRED.
/// @param kind  The runtime collection kind.
/// @param index The param's index in its collection; a patch cable's packed word needs no id.
/// @return True for a bipolar param.
[[nodiscard]] constexpr bool is_bipolar_at(plugin::ParamKind kind, uint32_t index) {
	if (kind == plugin::ParamKind::PATCH_CABLE) {
		return true;
	}
	if (kind == plugin::ParamKind::EXPRESSION) {
		return runtime_param_id(kind, index) == kExpressionPitchBend;
	}
	const plugin::ParamInfo* row = param_row_at(kind, index);
	return row != nullptr && row->flags.has(plugin::ParamFlag::CENTRED);
}

/// @brief @c is_bipolar_at for the param @p id: the index @c runtime_param_id gives @p id in a @p kind collection.
/// @param kind The runtime collection kind.
/// @param id   The param id, as @c runtime_param_id numbers it.
/// @return True for a bipolar param.
[[nodiscard]] constexpr bool is_bipolar(plugin::ParamKind kind, plugin::ParamId id) {
	switch (kind) {
	case plugin::ParamKind::NONE:
	case plugin::ParamKind::UNPATCHED_SOUND:
	case plugin::ParamKind::UNPATCHED_GLOBAL:
		return id.value >= kUnpatchedFirstId && is_bipolar_at(kind, id.value - kUnpatchedFirstId);
	case plugin::ParamKind::PATCH_CABLE:
		return true;
	case plugin::ParamKind::PATCHED:
	case plugin::ParamKind::STATIC:
	case plugin::ParamKind::MIDI:
	case plugin::ParamKind::EXPRESSION:
	case plugin::ParamKind::ARP:
		break;
	}
	return is_bipolar_at(kind, id.value);
}

/// @brief @c is_arp_rhythm for the param at @p index of a @p kind collection.
/// @param kind  The runtime collection kind.
/// @param index The param's index in its collection.
/// @return True for the sound's rhythm param.
[[nodiscard]] constexpr bool is_arp_rhythm_at(plugin::ParamKind kind, uint32_t index) {
	const std::optional<plugin::ParamId> id = runtime_param_id(kind, index);
	return id && is_arp_rhythm(kind, *id);
}

/// @brief @c is_stutter_rate for the param at @p index of a @p kind collection.
/// @param kind  The runtime collection kind.
/// @param index The param's index in its collection.
/// @return True for the stutter rate of an unpatched set.
[[nodiscard]] constexpr bool is_stutter_rate_at(plugin::ParamKind kind, uint32_t index) {
	const std::optional<plugin::ParamId> id = runtime_param_id(kind, index);
	return id && is_stutter_rate(kind, *id);
}

/// @brief Whether a plugin's param table has the param @p id: the global effectable's host-chain table for it, the
/// instrument's table for any other plugin.
/// @param plugin The plugin whose params are asked.
/// @param id     The param id, in that plugin's numbering.
/// @return True when the table has the param.
[[nodiscard]] constexpr bool has_engine_param(plugin::PluginId plugin, plugin::ParamId id) {
	const std::span<const plugin::ParamInfo> table = plugin == plugin::kGlobalEffectable
	                                                     ? std::span<const plugin::ParamInfo>{host::chains::kParams}
	                                                     : std::span<const plugin::ParamInfo>{kParams};
	return plugin::find_param(table, id) != nullptr;
}

} // namespace deluge::instruments::classic
