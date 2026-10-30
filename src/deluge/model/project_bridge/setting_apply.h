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

#include "definitions_cxx.hpp" // Error
#include "instruments/deluge/sound_state.h"
#include "serialization/value.h"

#include <optional>
#include <string_view>

class ModControllableAudio;
class Song;
class Sound;

namespace deluge::bridge {

/// @brief Change @p sound's setting at @p path (a path of the Deluge instrument's settings schema) to @p value, as the
/// sound editor's menu for it does, side effects included: the runtime's own setter for the path.
///
/// @param sound The sound to change.
/// @param path  A path of the Deluge instrument's settings schema.
/// @param value The new value.
/// @param song  The song the sound belongs to.
/// @return Error::UNSPECIFIED when the schema does not allow @p value at @p path, or the path has no setter here.
Error applySetting(::Sound& sound, std::string_view path, const deluge::serialization::Value& value, ::Song& song);

/// @brief Change @p owner's effect setting at @p path (a path of the effects' schema) to @p value, as its menu does:
/// for a kit, an audio track or the song master, whose effects have no slot yet.
///
/// @param owner The effects' owner.
/// @param path  A path of the effects' schema.
/// @param value The new value.
/// @return Error::UNSPECIFIED when the schema does not allow @p value at @p path.
Error applyEffectSetting(ModControllableAudio& owner, std::string_view path, const deluge::serialization::Value& value);

/// @brief The state @p sound reaches when its setting at @p path becomes @p value, where the setting's menu reshapes
/// more than the one value: an oscillator's type (its ranges keep only their top notes when they become the other
/// kind, sample or wavetable, and there is at least one; a DX7 has a patch) and its loop mode (stretching to note
/// length makes pitch and speed independent, and leaving it gives that up), and the synth mode (into FM the filters go
/// off; out of it, a filter that is off comes back on). Nothing for any other setting.
///
/// @param sound The sound the setting belongs to.
/// @param path  The setting's path.
/// @param value The new value.
/// @return The reshaped state, or nullopt for any other setting.
[[nodiscard]] std::optional<deluge::instrument::SoundState>
stateWithSetting(const ::Sound& sound, std::string_view path, const deluge::serialization::Value& value);

} // namespace deluge::bridge
