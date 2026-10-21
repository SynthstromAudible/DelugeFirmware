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
#include <vector>

namespace deluge::instrument {

/// @brief The Deluge instrument's non-param settings, by their path in a sound's element: the scalars a setting Edit
/// changes, and the structured ones (sample and wavetable ranges, the DX7 patch) only their own Edits change. Its
/// effect rows are kEffectSettings'. The context a host passes is engine::SettingContext's host contract.
extern const std::span<const engine::SettingInfo> kSettings;

/// @brief The Deluge instrument's clip-scoped settings: its arp's (kArpSettingRows). A clip holds their values in its
/// value layer, so they are apart from kSettings, whose every row is a value of the sound's slot.
extern const std::span<const engine::SettingInfo> kClipScopedSettings;

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

/// @brief Path @p path's value in @p state in an Edit's form (an LFO's sync level as the level itself), or nothing
/// where the path does not apply to the state: sample playback off a sample source (interpolation off a sample or
/// live-input one) or in FM; an oscillator's type in FM; an oscillator's own transpose and cents on a sample source
/// outside FM; modulators outside FM; a structured path or one the schema does not hold; an unset drum note.
/// @param state The sound's state.
/// @param path  The setting's path.
/// @return The value, or nullopt where the path does not apply.
[[nodiscard]] std::optional<serialization::Value> setting_value(const SoundState& state, std::string_view path);

/// @brief The value @p state holds at @p path whether or not the path applies to the state (an oscillator's type in
/// FM included), as the row stores it: a CHOICE row's index, else the number. The read
/// path of a sound's SettingReader (see engine::SettingContext's host contract).
/// @param state The sound's state.
/// @param path  The setting's path.
/// @return The stored value, or nullopt for a structured path, one the schema does not hold, a CHOICE row whose held
///         word is none of its choices, or another row whose held text is not a whole number (both states the schema
///         cannot describe).
[[nodiscard]] std::optional<int32_t> held_setting(const SoundState& state, std::string_view path);
/// @brief Whether a sound in @p state may take @p value at @p path, beyond what the schema allows: ring mod rings only
/// the oscillators up to a wavetable, so it is not chosen while either oscillator is past one, nor such an oscillator
/// while it is chosen.
/// @param state The sound's state.
/// @param path  The setting's path.
/// @param value The candidate value.
/// @return True when the sound may take the value.
[[nodiscard]] bool allows_setting(const SoundState& state, std::string_view path, const serialization::Value& value);

/// @brief Write @p value (canonical, in an Edit's form: an LFO's sync level as the level itself) at @p path into
/// @p state, the inverse of setting_value: the field the path names, whether or not a file holds it in this state.
/// Not for a path whose menu reshapes more than its leaf (an oscillator's type or loop mode, the synth mode): those
/// write the field alone here.
/// @param state The sound's state.
/// @param path  The setting's path; a structured path or one the schema does not hold writes nothing.
/// @param value The value, as canonical_setting gives it.
/// @return The instrument entries the write changed: the path's own, or none when it held the value already.
[[nodiscard]] std::vector<std::string_view> write_setting(SoundState& state, std::string_view path,
                                                          const serialization::Value& value);

/// @brief Whether a sound in @p state takes a setting at @p path at all: where setting_value answers, or where the
/// value is unset (a sound drum's MIDI note before one is chosen).
/// @param state The sound's state.
/// @param path  The setting's path.
/// @return True when the sound takes a setting there.
[[nodiscard]] bool takes_setting(const SoundState& state, std::string_view path);

} // namespace deluge::instrument
