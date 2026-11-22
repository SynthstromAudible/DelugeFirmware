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

#include "effects/state.h"
#include "effects/words.h"
#include "plugin/types/param.h"
#include "plugin/types/param_mapping.h"
#include "serialization/value.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace deluge::effects {

/// @brief The first setting id, clear of both plugins' param tables (each table's header checks its ids stay below it).
inline constexpr uint32_t kFirstSettingId = 1024;

/// @brief The ids of the settings a gold-knob press changes, from kFirstSettingId.
namespace setting {
inline constexpr plugin::ParamId kDelayPingPong{kFirstSettingId};
inline constexpr plugin::ParamId kDelayType{1025};
inline constexpr plugin::ParamId kDelaySyncType{1026};
inline constexpr plugin::ParamId kDelaySyncLevel{1027};
inline constexpr plugin::ParamId kLpfMode{1028};
inline constexpr plugin::ParamId kHpfMode{1029};
inline constexpr plugin::ParamId kSidechainSyncLevel{1030};
inline constexpr plugin::ParamId kModFXType{1031};
inline constexpr plugin::ParamId kCompThreshold{1032};
inline constexpr plugin::ParamId kCompRatio{1033};
inline constexpr plugin::ParamId kCompAttack{1034};
inline constexpr plugin::ParamId kCompRelease{1035};
inline constexpr plugin::ParamId kCompHpf{1036};
inline constexpr plugin::ParamId kCompBlend{1037};
} // namespace setting

/// @brief The setting params. Each row's file name is the path SetEffectSetting takes. The first eight step through
/// their choices; the compressor's six are continuous and have no stepper.
inline constexpr std::array<plugin::ParamInfo, 14> kSettingParams{{
    {.id = setting::kDelayPingPong,
     .file_name = "delay/pingPong",
     .flags = plugin::ParamFlag::STEPPED | plugin::ParamFlag::ENUM | plugin::ParamFlag::SETTING},
    {.id = setting::kDelayType,
     .file_name = "delay/type",
     .flags = plugin::ParamFlag::STEPPED | plugin::ParamFlag::ENUM | plugin::ParamFlag::SETTING},
    {.id = setting::kDelaySyncType,
     .file_name = "delay/syncType",
     .flags = plugin::ParamFlag::STEPPED | plugin::ParamFlag::ENUM | plugin::ParamFlag::SETTING},
    {.id = setting::kDelaySyncLevel,
     .file_name = "delay/syncLevel",
     .flags = plugin::ParamFlag::STEPPED | plugin::ParamFlag::ENUM | plugin::ParamFlag::SETTING},
    {.id = setting::kLpfMode,
     .file_name = "lpfMode",
     .flags = plugin::ParamFlag::STEPPED | plugin::ParamFlag::ENUM | plugin::ParamFlag::SETTING},
    {.id = setting::kHpfMode,
     .file_name = "hpfMode",
     .flags = plugin::ParamFlag::STEPPED | plugin::ParamFlag::ENUM | plugin::ParamFlag::SETTING},
    {.id = setting::kSidechainSyncLevel,
     .file_name = "sidechain/syncLevel",
     .flags = plugin::ParamFlag::STEPPED | plugin::ParamFlag::ENUM | plugin::ParamFlag::SETTING},
    {.id = setting::kModFXType,
     .file_name = "modFXType",
     .flags = plugin::ParamFlag::STEPPED | plugin::ParamFlag::ENUM | plugin::ParamFlag::SETTING},
    {.id = setting::kCompThreshold, .file_name = "audioCompressor/thresh", .flags = plugin::ParamFlag::SETTING},
    {.id = setting::kCompRatio, .file_name = "audioCompressor/ratio", .flags = plugin::ParamFlag::SETTING},
    {.id = setting::kCompAttack, .file_name = "audioCompressor/attack", .flags = plugin::ParamFlag::SETTING},
    {.id = setting::kCompRelease, .file_name = "audioCompressor/release", .flags = plugin::ParamFlag::SETTING},
    {.id = setting::kCompHpf, .file_name = "audioCompressor/compHPF", .flags = plugin::ParamFlag::SETTING},
    {.id = setting::kCompBlend, .file_name = "audioCompressor/compBlend", .flags = plugin::ParamFlag::SETTING},
}};

/// @brief What a step needs beyond the effects' state.
struct StepContext {
	/// The song's input tick magnitude, which shifts the sidechain's slow and fast levels.
	int32_t tick_magnitude{1};
};

/// @brief A setting after one step: the effects' state with it applied, and the value SetEffectSetting takes.
struct Stepped {
	EffectStates after;
	serialization::Value value;
};

/// @brief The next choice of setting @p id, from @p fx.
/// @param id  A setting param's id.
/// @param fx  The effects' state before the step.
/// @param ctx What the step needs beyond @p fx.
/// @return The state after the step and the value to write, or nullopt for a continuous or unknown setting.
[[nodiscard]] std::optional<Stepped> step_setting(plugin::ParamId id, const EffectStates& fx, const StepContext& ctx);

/// @brief The last setting id: a cable depth's flag must stay clear of it.
inline constexpr uint32_t kLastSettingId = setting::kCompBlend.value;
static_assert(std::ranges::all_of(kSettingParams,
                                  [](const plugin::ParamInfo& p) {
	                                  return p.id.value >= kFirstSettingId && p.id.value <= kLastSettingId;
                                  }),
              "every setting id lies between the first and the last");

/// @brief The setting param with id @p id.
/// @param id A param id.
/// @return Its row, or nullptr if @p id is not a setting.
[[nodiscard]] inline const plugin::ParamInfo* find_setting(plugin::ParamId id) {
	return plugin::find_param(kSettingParams, id);
}

} // namespace deluge::effects
