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

#include "instruments/deluge/sound_state.h"
#include "plugin-api/types/setting.h"
#include "serialization/value.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace deluge::instrument {

/// @brief The Deluge instrument's non-param settings, by their path in a sound's element: the scalars a setting Edit
/// changes, and the structured ones (sample and wavetable ranges, the DX7 patch) only their own Edits change. Its
/// effect rows are kEffectSettings'. The context a host passes is plugin::SettingContext's host contract.
extern const std::span<const plugin::SettingInfo> kSettings;

/// @brief The Deluge instrument's clip-scoped settings: its arp's (kArpSettingRows). A clip holds their values in its
/// value layer, so they are apart from kSettings, whose every row is a value of the sound's slot.
extern const std::span<const plugin::SettingInfo> kClipScopedSettings;

/// @brief Whether @p value is one @p path's row allows: a non-structured row's choice, an integer in its range, or 0
/// or 1 for a bool. False for a structured row or a path the schema does not hold.
/// @param path  The setting's path.
/// @param value The candidate value.
/// @return True when the row allows the value.
[[nodiscard]] bool check_setting(std::string_view path, const serialization::Value& value);

/// @brief @p value as a file writes it at @p path (an integer without sign or leading zeros), or nothing where
/// check_setting refuses it.
/// @param path  The setting's path.
/// @param value The value to canonicalize.
/// @return The canonical value, or nullopt.
[[nodiscard]] std::optional<serialization::Value> canonical_setting(std::string_view path,
                                                                    const serialization::Value& value);

/// @brief setting_value() over a sound's record.
[[nodiscard]] std::optional<serialization::Value> setting_value(const SoundState& state, std::string_view path);

/// @brief held_setting() over a sound's record: kept fields answer too.
[[nodiscard]] std::optional<int32_t> held_setting(const SoundState& state, std::string_view path);

/// @brief allows_setting() over a sound's record.
[[nodiscard]] bool allows_setting(const SoundState& state, std::string_view path, const serialization::Value& value);

/// @brief takes_setting() over a sound's record.
[[nodiscard]] bool takes_setting(const SoundState& state, std::string_view path);

/// @brief write_setting() on a sound's record, through its parts as the file's layout names them: a field the sound
/// keeps for a switch back is written where it is kept.
[[nodiscard]] std::vector<std::string_view> write_setting(SoundState& state, std::string_view path,
                                                          const serialization::Value& value);

} // namespace deluge::instrument
