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
#include "plugin/types/param.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

/// @brief The params a host chain has: the song master's, a kit's affect-entire's and an audio track's.
namespace deluge::host::chains {

/// @brief The ids, pinned forever by tests/unit/host/chains/params_spec.cpp.
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
inline constexpr ParamId kModFXRate{101};
inline constexpr ParamId kModFXDepth{102};
inline constexpr ParamId kDelayRate{103};
inline constexpr ParamId kDelayFeedback{104};
inline constexpr ParamId kPan{105};
inline constexpr ParamId kLpfFrequency{106};
inline constexpr ParamId kLpfResonance{107};
inline constexpr ParamId kLpfMorph{108};
inline constexpr ParamId kHpfFrequency{109};
inline constexpr ParamId kHpfResonance{110};
inline constexpr ParamId kHpfMorph{111};
inline constexpr ParamId kReverbAmount{112};
inline constexpr ParamId kVolume{113};
inline constexpr ParamId kSidechainCompressorVolume{114};
inline constexpr ParamId kPitchAdjust{115};
inline constexpr ParamId kTempo{116};
} // namespace param

/// @brief The first id; the runtime keeps the param with id @c kFirstId + i at index i of its unpatched set.
inline constexpr uint32_t kFirstId = 90;

namespace detail {
inline constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
inline constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
} // namespace detail

/// @brief Every param, in id order: an effect's row is its effect param (effects/params.h) at this table's id and
/// default; the arpeggiator's and the channel's rows are the table's own.
inline constexpr std::array<plugin::ParamInfo, 27> kParams{{
    effects::row(param::kStutterRate, effects::params::kStutterRate),
    effects::row(param::kBass, effects::params::kBass),
    effects::row(param::kTreble, effects::params::kTreble),
    effects::row(param::kBassFreq, effects::params::kBassFrequency),
    effects::row(param::kTrebleFreq, effects::params::kTrebleFrequency),
    effects::row(param::kSampleRateReduction, effects::params::kSampleRateReduction, {.default_value = detail::kMin}),
    effects::row(param::kBitcrushAmount, effects::params::kBitcrushAmount, {.default_value = detail::kMin}),
    effects::row(param::kModFXOffset, effects::params::kModFxOffset),
    effects::row(param::kModFXFeedback, effects::params::kModFxFeedback, {.default_value = detail::kMin}),
    effects::row(param::kCompressorShape, effects::params::kSidechainShape, {.default_value = -601295438}),
    effects::row(param::kCompressorThreshold, effects::params::kCompressorThreshold),
    effects::row(param::kModFXRate, effects::params::kModFxRate, {.default_value = -536870912}),
    effects::row(param::kModFXDepth, effects::params::kModFxDepth),
    effects::row(param::kDelayRate, effects::params::kDelayRate),
    effects::row(param::kDelayFeedback, effects::params::kDelayFeedback, {.default_value = detail::kMin}),
    {.id = param::kPan,
     .file_name = "pan",
     .mapping = plugin::MappingKind::PAN,
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    effects::row(param::kLpfFrequency, effects::params::kLpfFrequency, {.default_value = detail::kMax}),
    effects::row(param::kLpfResonance, effects::params::kLpfResonance, {.default_value = detail::kMin}),
    effects::row(param::kLpfMorph, effects::params::kLpfMorph, {.default_value = detail::kMin}),
    effects::row(param::kHpfFrequency, effects::params::kHpfFrequency, {.default_value = detail::kMin}),
    effects::row(param::kHpfResonance, effects::params::kHpfResonance, {.default_value = detail::kMin}),
    effects::row(param::kHpfMorph, effects::params::kHpfMorph, {.default_value = detail::kMin}),
    effects::row(param::kReverbAmount, effects::params::kReverbSend, {.default_value = detail::kMin}),
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

/// @brief The filters' ids, in the filter's param order (effects::params::kFilterParams).
inline constexpr std::array<plugin::ParamId, 6> kFilterIds{param::kLpfFrequency, param::kLpfResonance,
                                                           param::kLpfMorph,     param::kHpfFrequency,
                                                           param::kHpfResonance, param::kHpfMorph};
/// @brief The mod FX's ids, in its param order (effects::params::kModFxParams).
inline constexpr std::array<plugin::ParamId, 4> kModFxIds{param::kModFXRate, param::kModFXDepth, param::kModFXOffset,
                                                          param::kModFXFeedback};
/// @brief The EQ's ids, in its param order (effects::params::kEqualizerParams).
inline constexpr std::array<plugin::ParamId, 4> kEqIds{param::kBass, param::kTreble, param::kBassFreq,
                                                       param::kTrebleFreq};
/// @brief The bitcrush's ids, in its param order (effects::params::kBitcrushParams).
inline constexpr std::array<plugin::ParamId, 2> kBitcrushIds{param::kBitcrushAmount, param::kSampleRateReduction};

static_assert(effects::built_from(kParams, kFilterIds, effects::params::kFilterParams));
static_assert(effects::built_from(kParams, kModFxIds, effects::params::kModFxParams));
static_assert(effects::built_from(kParams, kEqIds, effects::params::kEqualizerParams));
static_assert(effects::built_from(kParams, kBitcrushIds, effects::params::kBitcrushParams));

/// @brief The rows a host chain's init leaves as the set holds them: the tempo is the song's, written by its tempo
/// edits (Song::setBPM).
inline constexpr std::array<plugin::ParamId, 1> kNotInitialised{param::kTempo};

/// @brief The one run of ids the params are stored in.
inline constexpr plugin::ParamBlock kParamBlock{plugin::ParamId{kFirstId}, 27};

/// @brief The runtime's index of the param @p id, or nullopt when there is no such param. For an id known only at run
/// time; a constant id takes @c runtime_index.
[[nodiscard]] constexpr std::optional<uint8_t> find_runtime_index(plugin::ParamId id) {
	if (!kParamBlock.contains(id)) {
		return std::nullopt;
	}
	return static_cast<uint8_t>(id.value - kFirstId);
}

/// @brief The runtime's index of the param @p id, for a constant id: an id of no param does not compile.
/// @param id A param id of the host chain.
/// @return The index in the runtime's unpatched set.
[[nodiscard]] consteval uint8_t runtime_index(plugin::ParamId id) {
	if (!kParamBlock.contains(id)) {
		std::unreachable();
	}
	return static_cast<uint8_t>(id.value - kFirstId);
}

/// @brief The id of the param the runtime keeps at @p index.
[[nodiscard]] constexpr plugin::ParamId param_id(uint8_t index) {
	return plugin::ParamId{kFirstId + index};
}

static_assert(kParams.size() == kParamBlock.count);

} // namespace deluge::host::chains
