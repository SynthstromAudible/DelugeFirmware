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
#include "engines/deluge/sound_state.h"
#include "serialization/value.h"

#include <optional>
#include <span>
#include <string_view>

namespace deluge::instrument {

/// @brief The Deluge instrument's non-param settings, by their path in a sound's element: the scalars a setting Edit
/// changes, and the structured ones (sample and wavetable ranges, the DX7 patch) only their own Edits change.
extern const std::span<const engine::SettingInfo> kSettings;

/// @brief Whether @p value is one @p path's row allows: a non-structured row's choice, an integer in its range, or 0
/// or 1 for a bool. False for a structured row or a path the schema does not hold.
[[nodiscard]] bool check_setting(std::string_view path, const serialization::Value& value);

/// @brief @p value as a file writes it at @p path (an integer without sign or leading zeros), or nothing where
/// check_setting refuses it.
[[nodiscard]] std::optional<serialization::Value> canonical_setting(std::string_view path,
                                                                    const serialization::Value& value);

/// @brief Path @p path's value in @p state in an Edit's form (an LFO's sync level as the level itself), or nothing
/// where the path does not apply to the state: sample playback off a sample source (interpolation off a sample or
/// live-input one) or in FM; an oscillator's type in FM; an oscillator's own transpose and cents on a sample source
/// outside FM; modulators outside FM; a structured path or one the schema does not hold; an unset drum note.
[[nodiscard]] std::optional<serialization::Value> setting_value(const SoundState& state, std::string_view path);

/// @brief Whether a sound in @p state takes a setting at @p path at all: where setting_value answers, or where the
/// value is unset (a sound drum's MIDI note before one is chosen).
[[nodiscard]] bool takes_setting(const SoundState& state, std::string_view path);

} // namespace deluge::instrument
