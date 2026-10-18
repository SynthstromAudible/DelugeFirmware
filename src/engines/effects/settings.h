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

#include "engine-api/types/rate_tables.h"
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

/// The delay's sync menu: one value over the level and its type row, every type offered.
inline constexpr engine::SettingDescriptorExtras kDelaySyncExtras{
    .mapping = {.kind = engine::DisplayMappingKind::COMBINED_SYNC, .partner = "delay/syncType"}};
/// The sidechain's sync menu: one value over the level and its type row; its user range offers off and the nine
/// levels, with no triplet or dotted.
inline constexpr engine::SettingDescriptorExtras kSidechainSyncExtras{
    .mapping = {.kind = engine::DisplayMappingKind::COMBINED_SYNC, .partner = "sidechain/syncType"}};
/// The sidechain sync menu's values: off and the nine levels, with no triplet or dotted.
/// The row's UserScale carries the COMBINED_SYNC menu's range, which from_menu honours.
inline constexpr engine::UserScale kSidechainSyncMenu{
    .user_min = 0, .user_max = engine::kMaxSyncLevel, .stored_min = 0, .stored_max = engine::kMaxSyncLevel};

/// The sidechain's attack and release menus: a step of the rate table, the stored rate the entry shifted.
inline constexpr engine::SettingDescriptorExtras kSidechainAttackExtras{
    .mapping = {.kind = engine::DisplayMappingKind::TABLE, .table = engine::kSidechainAttackRates, .table_shift = 2}};
inline constexpr engine::SettingDescriptorExtras kSidechainReleaseExtras{
    .mapping = {.kind = engine::DisplayMappingKind::TABLE, .table = engine::kSidechainReleaseRates, .table_shift = 3}};
/// A compressor knob's menu, 0..128: its top seven bits; the top step stores the 127th, as the menu clamps it.
inline constexpr engine::SettingDescriptorExtras kCompressorKnobExtras{
    .mapping = {.kind = engine::DisplayMappingKind::KNOB_POSITION, .clamp_to = 127 << 24}};
/// The compressor blend's menu: as a knob's, but its top step stores the whole blend.
inline constexpr engine::SettingDescriptorExtras kCompressorBlendExtras{
    .mapping = {.kind = engine::DisplayMappingKind::KNOB_POSITION, .clamp_to = kQ31Max}};

/// A new owner's effect settings: the song's defaults.
inline constexpr EffectStates kDefaults{};
/// The sidechain's rates and sync level are set by the owner at construction: the rates are its menus' steps 7 and
/// 28; the sync level is one below the delay's for the song's tick magnitude.
inline constexpr int32_t kDefaultSidechainAttack = engine::kSidechainAttackRates[7] << 2;
inline constexpr int32_t kDefaultSidechainRelease = engine::kSidechainReleaseRates[28] << 3;
inline constexpr int32_t kDefaultSidechainSyncLevel = engine::index_of(kDefaults.delay.sync_level) - 1;
} // namespace detail

/// @brief The effects' non-param settings, by their path in an owner's element. A sync level is the level itself
/// (0–9, 0 = off); a file writes it relative to the song's tick magnitude.
inline constexpr std::array<engine::SettingInfo, 23> kEffectSettings{{
    {.path = "modFXType", .kind = detail::CHOICE, .choices = detail::kModFxTypeNames},
    // 24dB, HPLadder: the filters' default circuits.
    {.path = "lpfMode", .kind = detail::CHOICE, .choices = detail::kLpfModeNames, .default_value = 1},
    {.path = "hpfMode", .kind = detail::CHOICE, .choices = detail::kHpfModeNames, .default_value = 2},
    {.path = "filterRoute", .kind = detail::CHOICE, .choices = detail::kFilterRouteNames},
    {.path = "clippingAmount", .min = 0, .max = 15},
    {.path = "delay/pingPong", .kind = detail::BOOL, .default_value = detail::kDefaults.delay.ping_pong ? 1 : 0},
    {.path = "delay/type", .kind = detail::CHOICE, .choices = detail::kDelayTypeNames},
    {.path = "delay/syncLevel",
     .min = 0,
     .max = engine::kMaxSyncLevel,
     .default_value = engine::index_of(detail::kDefaults.delay.sync_level),
     .units = engine::SettingUnits::SYNC_LEVEL,
     .extras = &detail::kDelaySyncExtras},
    {.path = "delay/syncType", .kind = detail::CHOICE, .choices = detail::kSyncTypeNames},
    {.path = "sidechain/attack",
     .min = 0,
     .max = detail::kQ31Max,
     .default_value = detail::kDefaultSidechainAttack,
     .extras = &detail::kSidechainAttackExtras},
    {.path = "sidechain/release",
     .min = 0,
     .max = detail::kQ31Max,
     .default_value = detail::kDefaultSidechainRelease,
     .extras = &detail::kSidechainReleaseExtras},
    {.path = "sidechain/syncLevel",
     .min = 0,
     .max = engine::kMaxSyncLevel,
     .user = detail::kSidechainSyncMenu,
     .default_value = detail::kDefaultSidechainSyncLevel,
     .units = engine::SettingUnits::SYNC_LEVEL,
     .extras = &detail::kSidechainSyncExtras},
    {.path = "sidechain/syncType", .kind = detail::CHOICE, .choices = detail::kSyncTypeNames},
    {.path = "audioCompressor/attack",
     .min = 0,
     .max = detail::kQ31Max,
     .default_value = detail::kDefaults.compressor.attack,
     .extras = &detail::kCompressorKnobExtras},
    {.path = "audioCompressor/release",
     .min = 0,
     .max = detail::kQ31Max,
     .default_value = detail::kDefaults.compressor.release,
     .extras = &detail::kCompressorKnobExtras},
    {.path = "audioCompressor/thresh",
     .min = 0,
     .max = detail::kQ31Max,
     .default_value = detail::kDefaults.compressor.threshold},
    {.path = "audioCompressor/ratio",
     .min = 0,
     .max = detail::kQ31Max,
     .default_value = detail::kDefaults.compressor.ratio,
     .extras = &detail::kCompressorKnobExtras},
    {.path = "audioCompressor/compHPF",
     .min = 0,
     .max = detail::kQ31Max,
     .default_value = detail::kDefaults.compressor.sidechain_hpf,
     .extras = &detail::kCompressorKnobExtras},
    {.path = "audioCompressor/compBlend",
     .min = 0,
     .max = detail::kQ31Max,
     .default_value = detail::kDefaults.compressor.blend,
     .extras = &detail::kCompressorBlendExtras},
    {.path = "stutter/quantized", .kind = detail::BOOL, .default_value = detail::kDefaults.stutter.quantized ? 1 : 0},
    {.path = "stutter/reverse", .kind = detail::BOOL},
    {.path = "stutter/pingPong", .kind = detail::BOOL},
    {.path = "stutter/useSongStutter",
     .kind = detail::BOOL,
     .default_value = detail::kDefaults.stutter.use_song_stutter ? 1 : 0},
}};
static_assert(engine::conditions_are_valid(kEffectSettings));

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
