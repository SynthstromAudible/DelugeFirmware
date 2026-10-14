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

#include "engine-api/types/setting.h"
#include "engine-api/types/sync.h"
#include "engines/effects/state.h"
#include "serialization/value.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace deluge::effects {

namespace detail {
using engine::SettingInfo;
using enum engine::SettingKind;

/// A delay's types, as a file names them.
inline constexpr std::array<std::string_view, 3> kDelayTypeNames{"off", "digital", "analog"};
inline constexpr std::array<std::string_view, 8> kModFxTypeNames{"none",         "flanger",    "chorus",    "phaser",
                                                                 "StereoChorus", "TapeWarble", "dimension", "grainFX"};
/// The low-pass filter's circuits and off, as a file names them.
inline constexpr std::array<std::string_view, 6> kLpfModeNames{"12dB",     "24dB",      "24dBDrive",
                                                               "SVF_Band", "SVF_Notch", "Off"};
/// The high-pass filter's circuits and off, as a file names them.
inline constexpr std::array<std::string_view, 4> kHpfModeNames{"SVF_Band", "SVF_Notch", "HPLadder", "Off"};
inline constexpr std::array<std::string_view, 3> kFilterRouteNames{"H2L", "L2H", "PARA"};
/// A synced rate's type, as its stored number (even, triplet, dotted).
inline constexpr std::array<std::string_view, 3> kSyncTypeNames{"0", "10", "19"};
/// A Q31 knob position or rate: 0 to its greatest value.
inline constexpr int32_t kQ31Max = std::numeric_limits<int32_t>::max();
} // namespace detail

/// @brief The effects' non-param settings, by their path in an owner's element. A sync level is the level itself
/// (0–9, 0 = off); a file writes it relative to the song's tick magnitude.
inline constexpr std::array<engine::SettingInfo, 23> kEffectSettings{{
    {.path = "modFXType", .kind = detail::CHOICE, .choices = detail::kModFxTypeNames},
    {.path = "lpfMode", .kind = detail::CHOICE, .choices = detail::kLpfModeNames},
    {.path = "hpfMode", .kind = detail::CHOICE, .choices = detail::kHpfModeNames},
    {.path = "filterRoute", .kind = detail::CHOICE, .choices = detail::kFilterRouteNames},
    {.path = "clippingAmount", .min = 0, .max = 15},
    {.path = "delay/pingPong", .kind = detail::BOOL},
    {.path = "delay/type", .kind = detail::CHOICE, .choices = detail::kDelayTypeNames},
    {.path = "delay/syncLevel", .min = 0, .max = engine::kMaxSyncLevel},
    {.path = "delay/syncType", .kind = detail::CHOICE, .choices = detail::kSyncTypeNames},
    {.path = "sidechain/attack", .min = 0, .max = detail::kQ31Max},
    {.path = "sidechain/release", .min = 0, .max = detail::kQ31Max},
    {.path = "sidechain/syncLevel", .min = 0, .max = engine::kMaxSyncLevel},
    {.path = "sidechain/syncType", .kind = detail::CHOICE, .choices = detail::kSyncTypeNames},
    {.path = "audioCompressor/attack", .min = 0, .max = detail::kQ31Max},
    {.path = "audioCompressor/release", .min = 0, .max = detail::kQ31Max},
    {.path = "audioCompressor/thresh", .min = 0, .max = detail::kQ31Max},
    {.path = "audioCompressor/ratio", .min = 0, .max = detail::kQ31Max},
    {.path = "audioCompressor/compHPF", .min = 0, .max = detail::kQ31Max},
    {.path = "audioCompressor/compBlend", .min = 0, .max = detail::kQ31Max},
    {.path = "stutter/quantized", .kind = detail::BOOL},
    {.path = "stutter/reverse", .kind = detail::BOOL},
    {.path = "stutter/pingPong", .kind = detail::BOOL},
    {.path = "stutter/useSongStutter", .kind = detail::BOOL},
}};

/// @brief Whether @p value is one kEffectSettings allows at @p path.
/// @param path  The setting's path in an owner's element.
/// @param value The value to check.
/// @return True if kEffectSettings holds @p path and allows @p value there.
[[nodiscard]] bool check_effect_setting(std::string_view path, const serialization::Value& value);

/// @brief Path @p path's value in @p states in an Edit's form (a sync level as the level itself), or nothing for a path
/// kEffectSettings does not hold.
/// @param states The effect settings to read.
/// @param path   The setting's path in an owner's element.
/// @return The value, or nullopt for a path kEffectSettings does not hold.
[[nodiscard]] std::optional<serialization::Value> effect_setting_value(const EffectStates& states,
                                                                       std::string_view path);

} // namespace deluge::effects
