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

#include "plugin-api/types/param.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

/// @brief The params the song master, a kit's affect-entire and an audio track share. Provisional: it goes when each
/// effect declares its own table.
namespace deluge::global_effectable {

/// @brief The ids, pinned forever by tests/effects/global_effectable/params_spec.cpp.
namespace param {
using plugin::ParamId;
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
inline constexpr ParamId kModFXRate{116};
inline constexpr ParamId kModFXDepth{117};
inline constexpr ParamId kDelayRate{118};
inline constexpr ParamId kDelayFeedback{119};
inline constexpr ParamId kArpRate{120};
inline constexpr ParamId kPan{121};
inline constexpr ParamId kLpfFrequency{122};
inline constexpr ParamId kLpfResonance{123};
inline constexpr ParamId kLpfMorph{124};
inline constexpr ParamId kHpfFrequency{125};
inline constexpr ParamId kHpfResonance{126};
inline constexpr ParamId kHpfMorph{127};
inline constexpr ParamId kReverbAmount{128};
inline constexpr ParamId kVolume{129};
inline constexpr ParamId kSidechainCompressorVolume{130};
inline constexpr ParamId kPitchAdjust{131};
inline constexpr ParamId kTempo{132};
} // namespace param

/// @brief The first id; the runtime keeps the param with id @c kFirstId + i at index i of its unpatched set.
inline constexpr uint32_t kFirstId = 90;

/// @brief Every param, in id order.
inline constexpr std::array<plugin::ParamInfo, 43> kParams{{
    {.id = param::kStutterRate, .file_name = "stutterRate", .flags = plugin::ParamFlag::NOT_AUTOMATABLE},
    {.id = param::kBass, .file_name = "bass", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kTreble, .file_name = "treble", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kBassFreq, .file_name = "bassFreq"},
    {.id = param::kTrebleFreq, .file_name = "trebleFreq"},
    {.id = param::kSampleRateReduction,
     .file_name = "sampleRateReduction",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kBitcrushAmount, .file_name = "bitcrushAmount", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModFXOffset, .file_name = "modFXOffset"},
    {.id = param::kModFXFeedback, .file_name = "modFXFeedback", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kCompressorShape, .file_name = "compressorShape", .default_value = -601295438},
    {.id = param::kCompressorThreshold, .file_name = "compressorThreshold", .mapping = plugin::MappingKind::UNIPOLAR},
    {.id = param::kArpGate, .file_name = "arpGate", .default_value = std::numeric_limits<int32_t>::max()},
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
    {.id = param::kModFXRate, .file_name = "modFXRate", .default_value = -536870912},
    {.id = param::kModFXDepth, .file_name = "modFXDepth"},
    {.id = param::kDelayRate, .file_name = "delayRate", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kDelayFeedback,
     .file_name = "delayFeedback",
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kArpRate, .file_name = "arpRate"},
    {.id = param::kPan,
     .file_name = "pan",
     .mapping = plugin::MappingKind::PAN,
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kLpfFrequency, .file_name = "lpfFrequency", .default_value = std::numeric_limits<int32_t>::max()},
    {.id = param::kLpfResonance, .file_name = "lpfResonance", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kLpfMorph, .file_name = "lpfMorph", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kHpfFrequency, .file_name = "hpfFrequency", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kHpfResonance, .file_name = "hpfResonance", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kHpfMorph, .file_name = "hpfMorph", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kReverbAmount, .file_name = "reverbAmount", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kVolume, .file_name = "volume"},
    {.id = param::kSidechainCompressorVolume,
     .file_name = "sidechainCompressorVolume",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kPitchAdjust,
     .file_name = "pitchAdjust",
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kTempo,
     .file_name = "tempo",
     .flags = plugin::ParamFlag::NO_UNDO | plugin::ParamFlag::FLOAT_INTERPOLATION},
}};

/// @brief The one run of ids the params are stored in.
inline constexpr plugin::ParamBlock kParamBlock{plugin::ParamId{kFirstId}, 43};

/// @brief The runtime's index of the param @p id, or nullopt when there is no such param.
[[nodiscard]] constexpr std::optional<uint8_t> runtime_index(plugin::ParamId id) {
	if (!kParamBlock.contains(id)) {
		return std::nullopt;
	}
	return static_cast<uint8_t>(id.value - kFirstId);
}

/// @brief The id of the param the runtime keeps at @p index.
[[nodiscard]] constexpr plugin::ParamId param_id(uint8_t index) {
	return plugin::ParamId{kFirstId + index};
}

static_assert(kParams.size() == kParamBlock.count);

} // namespace deluge::global_effectable
