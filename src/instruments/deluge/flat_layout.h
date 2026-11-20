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

#include "instruments/deluge/cable_acceptance.h"
#include "instruments/deluge/codec.h"
#include "instruments/deluge/composed_state.h"
#include "instruments/deluge/flat_sound_state.h"
#include "instruments/deluge/settings.h"
#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/state_rules.h"

#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief A sound's settings in the file's flat layout, inside the library: the codec's parse form (a file's keys
/// arrive in any order, so a read lands here and is composed once) and the reference the record's codec, settings and
/// rules are checked against. The app and the scenes work on the record (sound_state.h) alone.
namespace deluge::instrument {

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

/// @brief The flat record @p flat as the state it plays and what it keeps for a switch back.
[[nodiscard]] std::pair<ComposedState, Recall> compose(const FlatSoundState& flat);

/// @brief The flat record a composed state and its recall are: `flatten(compose(f)) == f` for every record a file
/// decodes to or a rule makes.
[[nodiscard]] FlatSoundState flatten(const ComposedState& state, const Recall& recall);

/// @brief encode_pieces() over the file's flat layout: the reference the record's is checked against.
// The codec in the flat layout: its parse form and its reference
[[nodiscard]] SoundPieces encode_pieces(const FlatSoundState& state, const plugin::CodecContext& context,
                                        std::optional<int32_t> compressor_threshold = std::nullopt);

/// @brief encode() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] serialization::Value encode(const FlatSoundState& state, const plugin::CodecContext& context);

/// @brief with_state() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] serialization::Value with_state(const serialization::Value& element, const FlatSoundState& state,
                                              const plugin::CodecContext& context);

/// @brief encode_entry() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] std::optional<serialization::Value>
encode_entry(const FlatSoundState& state, std::string_view key, const plugin::CodecContext& context,
             std::optional<int32_t> compressor_threshold = std::nullopt);

/// @brief decode() over the file's flat layout: the reference the record's is checked against.
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

/// @brief decode_change() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] std::expected<FlatSlotChange, plugin::StateError> decode_change(const serialization::Value& element,
                                                                              const serialization::Value& previous,
                                                                              const FlatSoundState& current,
                                                                              const plugin::CodecContext& context);

/// @brief with_setting() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] std::expected<FlatSlotChange, plugin::StateError> with_setting(const FlatSoundState& state,
                                                                             std::string_view path,
                                                                             const serialization::Value& value,
                                                                             const plugin::CodecContext& context);

/// @brief differing_entries() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] std::vector<std::string_view> differing_entries(const FlatSoundState& a, const FlatSoundState& b,
                                                              const plugin::CodecContext& context);

/// @brief changed_entries() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] std::vector<std::string_view> changed_entries(const FlatSoundState& a, const FlatSoundState& b);

/// @brief change_to() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] std::expected<FlatSlotChange, plugin::StateError> change_to(const FlatSoundState& state,
                                                                          const FlatSoundState& next,
                                                                          std::span<const std::string_view> keys,
                                                                          const plugin::CodecContext& context);

/// @brief decode_onto() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] std::expected<FlatSoundState, plugin::StateError>
decode_onto(const serialization::Value& element, const FlatSoundState& current, const plugin::CodecContext& context);

/// @brief dump_state() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] serialization::Value dump_state(const FlatSoundState& state);

/// @brief setting_value() over the file's flat layout: the reference the record's is checked against.
// The settings rows over the flat layout (the reference the record's are checked against)
[[nodiscard]] std::optional<serialization::Value> setting_value(const FlatSoundState& state, std::string_view path);

/// @brief held_setting() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] std::optional<int32_t> held_setting(const FlatSoundState& state, std::string_view path);

/// @brief allows_setting() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] bool allows_setting(const FlatSoundState& state, std::string_view path,
                                  const serialization::Value& value);

/// @brief write_setting() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] std::vector<std::string_view> write_setting(FlatSoundState& state, std::string_view path,
                                                          const serialization::Value& value);

/// @brief takes_setting() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] bool takes_setting(const FlatSoundState& state, std::string_view path);

/// @brief with_mode() over the file's flat layout: the reference the record's is checked against.
// The rules over the flat layout (the reference the record's are checked against)
[[nodiscard]] FlatSoundState with_mode(FlatSoundState state, SynthMode mode);

/// @brief with_osc_type() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] FlatSoundState with_osc_type(FlatSoundState state, SourceId id, OscType type,
                                           std::span<const RangeState> held, bool held_are_wavetable);

/// @brief with_loop_mode() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] FlatSoundState with_loop_mode(FlatSoundState state, SourceId id, SampleRepeatMode mode);

/// @brief with_song_defaults() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] FlatSoundState with_song_defaults(FlatSoundState state, const effects::SongDefaults& defaults);

/// @brief as_sample_sound() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] FlatSoundState as_sample_sound(FlatSoundState state);

/// @brief as_default_synth() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] FlatSoundState as_default_synth(FlatSoundState state);

/// @brief as_blank_synth() over the file's flat layout: the reference the record's is checked against.
[[nodiscard]] FlatSoundState as_blank_synth(FlatSoundState state, bool dx);

/// @brief cable_allowed() over the file's flat layout: the reference the record's is checked against.
// Cable acceptance over the flat layout
[[nodiscard]] bool cable_allowed(PatchSource source, plugin::ParamId destination, const FlatSoundState& state,
                                 bool is_drum);

} // namespace deluge::instrument
