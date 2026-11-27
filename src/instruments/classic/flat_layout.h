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

#include "instruments/classic/codec.h"
#include "instruments/classic/composed_state.h"
#include "instruments/classic/flat_sound_state.h"
#include "instruments/classic/sound_state.h"

#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief A sound's settings in the file's flat layout, inside the library: the codec's parse form (a file's keys
/// arrive in any order, so a read lands here and is composed once). Everything else, in the library and out of it,
/// works on the record (sound_state.h).
namespace deluge::instruments::classic {

// The record and the flat layout: one slot at a time, and the whole sound
/// @brief A slot source split into what its kind holds and what the slot remembers of the rest.
struct PackedSlot {
	synth::SlotState kind;
	SlotMemory memory;
};

/// @brief A slot as the file's layout holds it: its whole source and its retrigger phase word.
struct UnpackedSlot {
	SourceState source;
	uint32_t phase{0};
};

/// @brief Slot source @p source, whose retrigger phase word is @p phase, as its kind and the slot's memory.
[[nodiscard]] PackedSlot pack_slot(const SourceState& source, uint32_t phase);

/// @brief The slot source a kind and the slot's memory make, with its retrigger phase word.
[[nodiscard]] UnpackedSlot unpack_slot(const synth::SlotState& kind, const SlotMemory& memory);

/// @brief The memory of a slot parked while FM plays: its kind and parts, its pitch left to the carrier.
[[nodiscard]] SlotMemory park_slot(SourceState source);

/// @brief The slot source a slot parked while FM plays makes, its pitch at zero (the carrier holds it).
[[nodiscard]] SourceState unpark_slot(const SlotMemory& memory);

/// @brief The memory of a slot parked while the DX7 plays: its kind and parts with its own pitch, whose retrigger
/// phase word is @p phase.
[[nodiscard]] SlotMemory park_whole_slot(const SourceState& source, uint32_t phase);

/// @brief The slot source and phase word a slot parked while the DX7 plays makes (a new synth's square when it parks
/// none).
[[nodiscard]] UnpackedSlot unpark_whole_slot(const SlotMemory& memory);

/// @brief The flat record @p flat as the state it plays and what it keeps for a switch back.
[[nodiscard]] std::pair<ComposedState, Recall> compose(const FlatSoundState& flat);

/// @brief The flat record a composed state and its recall are: `flatten(compose(f)) == f` for every record a file
/// decodes to or a rule makes.
[[nodiscard]] FlatSoundState flatten(const ComposedState& state, const Recall& recall);

/// @brief The record a flat state composes to: `to_flat(to_record(f)) == f` for every state a file decodes to.
[[nodiscard]] SoundState to_record(const FlatSoundState& flat);

/// @brief The record in the file's flat layout.
[[nodiscard]] FlatSoundState to_flat(const SoundState& state);

/// @brief decode() into the file's flat layout: the parse the record's decode composes.
[[nodiscard]] std::expected<void, plugin::StateError> decode(const serialization::Value& element, FlatSoundState& state,
                                                             const plugin::CodecContext& context);

/// @brief The fields of @p from that @p state's encoding does not write, copied into @p state: an FM sound's sources'
/// types, sample playback, ranges and DX patches; a non-FM sound's modulators; a sample source's own transpose and
/// cents and its retrigger phase (and for source 2 the oscillator sync); a non-sample source's sample playback, its
/// ranges unless it is a wavetable, and its DX patch unless it is a DX7; and whether its stutter uses the song's.
/// @param state The state to copy into.
/// @param from  The state the fields are copied from.
void keep_unwritten(FlatSoundState& state, const FlatSoundState& from);

/// @brief What a slot change decodes to in the file's flat layout (the codec's parse form); SlotChange is its record.
struct FlatSlotChange {
	FlatSoundState state;
	std::vector<std::string_view> changed;
};

/// @brief decode_change() into the file's flat layout: the parse the record's decode_change composes.
[[nodiscard]] std::expected<FlatSlotChange, plugin::StateError> decode_change(const serialization::Value& element,
                                                                              const serialization::Value& previous,
                                                                              const FlatSoundState& current,
                                                                              const plugin::CodecContext& context);

} // namespace deluge::instruments::classic
