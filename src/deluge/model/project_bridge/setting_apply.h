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
#include "model/fx/effects_owner.h"
#include "serialization/value.h"

#include <optional>
#include <string_view>

class Song;
class Sound;

namespace deluge::bridge {

/// @brief A sound's pitch changed (its or a source's transpose or cents, or a source's ranges): its sounding voices'
/// phase increments follow.
/// @param sound The sound.
/// @param song  The song it plays in.
void retuneVoices(::Sound& sound, ::Song& song);

/// @brief A marker of source @p s's zone moved: the sound's sounding voices follow it, and a voice the zone no longer
/// holds ends.
/// @param sound  The sound.
/// @param s      The source index.
/// @param marker The marker that moved.
/// @param song   The song it plays in.
void zoneMoved(::Sound& sound, int32_t s, MarkerType marker, ::Song& song);

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
/// for an owner outside the song's slots and chains (a sound not bound, a preview), which holds its effects itself.
///
/// @param owner The effects' owner.
/// @param path  A path of the effects' schema.
/// @param value The new value.
/// @return Error::UNSPECIFIED when the schema does not allow @p value at @p path.
Error applyEffectSetting(deluge::model::EffectsOwner owner, std::string_view path,
                         const deluge::serialization::Value& value);

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

/// @brief Whether @p sound's runtime takes a setting at @p path in its current engine and oscillator types: not sample
/// playback where an oscillator plays no samples (or the sound is FM; live input takes its interpolation), not a sample
/// oscillator's own tuning outside FM (its ranges' tuning is its own), not a modulator outside FM.
[[nodiscard]] bool runtimeTakes(const ::Sound& sound, std::string_view path);

/// @brief Bring @p sound to @p after, which its record already holds, from @p before, what it held: the sound's own
/// fields and config follow the record, and each part that changed has the runtime effect its menu always had — voices
/// stop for a new engine, a type that plays files, input or a DX7 patch, a reversed sample or stretch coming or
/// going; live voices retune for a new tuning or unison detune; live sample parts follow a new unison count; the
/// params re-patch for a new engine, square coming or going, or an LFO's sync; the per-sound LFOs resync; the runtime
/// takes the ranges and DX7 patch it does not hold.
///
/// @param sound  The sound, bound to its record.
/// @param before The state the record held.
/// @param after  The state the record holds now.
/// @param song   The song the sound plays in.
/// @return The first refusal from taking the ranges or a DX7 patch, else Error::NONE.
Error applyChange(::Sound& sound, const deluge::instrument::SoundState& before,
                  const deluge::instrument::SoundState& after, ::Song& song);

} // namespace deluge::bridge
