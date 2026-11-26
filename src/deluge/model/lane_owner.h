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

/// @file
/// @brief The object a set of params belongs to, typed: what an engine stack's owner slot holds, and what a lane
/// target names.

#include <optional>
#include <variant>

class AudioOutput;
class Drum;
class Kit;
class NonAudioDrum;
class NonAudioInstrument;
class Output;
class Sound;

namespace deluge::model {

class MasterChannel;
struct EffectsOwner;

/// @brief A synth or a sound drum (its Sound), a kit's own effects, an audio track, a MIDI or CV track, a MIDI or gate
/// drum, or the song's master channel. Empty for none.
using LaneOwner = std::variant<std::monostate, ::Sound*, ::Kit*, ::AudioOutput*, ::NonAudioInstrument*, ::NonAudioDrum*,
                               MasterChannel*>;

/// @brief The typed owner of @p output's params: its Sound for a synth, the kit, the audio output, the MIDI or CV
/// instrument.
/// @param output The output.
/// @return The owner; empty for an output of no type.
[[nodiscard]] LaneOwner owner_of(::Output& output);

/// @brief The typed owner of @p drum's params: its Sound for a sound drum, else the MIDI or gate drum.
/// @param drum The drum.
/// @return The owner.
[[nodiscard]] LaneOwner owner_of(::Drum& drum);

/// @brief The sound @p owner is, where the code holds one: patched params and cables exist only on sounds.
/// @param owner The owner; it must hold a Sound.
/// @return The sound.
[[nodiscard]] inline ::Sound& sound_of(const LaneOwner& owner) {
	return *std::get<::Sound*>(owner);
}

/// @brief Whether @p owner names no object.
/// @param owner The owner.
/// @return True for none.
[[nodiscard]] constexpr bool is_none(const LaneOwner& owner) {
	return std::holds_alternative<std::monostate>(owner);
}

/// @brief The effects @p owner answers for: a sound's, a kit's, an audio track's or the master channel's.
/// @param owner The owner.
/// @return Its effects; none for a MIDI or CV instrument, a MIDI or gate drum, or no owner.
[[nodiscard]] std::optional<EffectsOwner> effects_owner_of(const LaneOwner& owner);

} // namespace deluge::model
