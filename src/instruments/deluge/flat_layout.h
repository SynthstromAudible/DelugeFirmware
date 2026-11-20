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
/// @brief The memory of a playing slot whose kind does not hold @p kept (the slot's other fields in the file's layout,
/// the kind's own at their defaults) and @p retrigger (a sample's).
[[nodiscard]] SlotMemory remember(const SourceState& kept, const synth::Retrigger& retrigger);

/// @brief The kept fields and retrigger @p memory holds for a playing slot, in the file's layout.
[[nodiscard]] SourceState recalled(const SlotMemory& memory, synth::Retrigger& retrigger);

/// @brief The memory of a slot parked while FM plays: @p kept is its whole source but for its pitch (the carrier's).
[[nodiscard]] SlotMemory remember_parked(const SourceState& kept);

/// @brief The whole source @p memory parks while FM plays, its pitch left at zero (the carrier's).
[[nodiscard]] SourceState recalled_parked(const SlotMemory& memory);

/// @brief The kind state slot source @p full plays, its phase word @p phase; what the kind does not hold goes to
/// @p kept and @p kept_retrigger (the fields the kind holds are left at their defaults there).
[[nodiscard]] synth::SlotState pack_slot(const SourceState& full, uint32_t phase, SourceState& kept,
                                         synth::Retrigger& kept_retrigger);

/// @brief The slot source a kind state and what it keeps are, its phase word into @p phase.
[[nodiscard]] SourceState unpack_slot(const synth::SlotState& slot, const SourceState& kept,
                                      const synth::Retrigger& kept_retrigger, uint32_t& phase);

/// @brief The flat record @p flat as the state it plays and what it keeps for a switch back.
[[nodiscard]] std::pair<ComposedState, Recall> compose(const FlatSoundState& flat);

/// @brief The flat record a composed state and its recall are: `flatten(compose(f)) == f` for every record a file
/// decodes to or a rule makes.
[[nodiscard]] FlatSoundState flatten(const ComposedState& state, const Recall& recall);

// The codec in the flat layout: its parse form and its reference
/// @brief The instrument's entries of a sound element for @p state, in file order, as pieces (see SoundPieces).
/// Numbers are written as the file's 32-bit signed integers (a free-running phase, 0xFFFFFFFF, as -1); sync levels
/// relative to @p context's tick magnitude.
/// @param state                The sound's settings.
/// @param context              The file context (tick magnitude).
/// @param compressor_threshold The compressor threshold to write in place of the state's: a sound's is its param, as
/// the render last applied it.
/// @return The pieces, in file order.
[[nodiscard]] SoundPieces encode_pieces(const FlatSoundState& state, const plugin::CodecContext& context,
                                        std::optional<int32_t> compressor_threshold = std::nullopt);

/// @brief The pieces of encode_pieces as one Map, in file order: what the instrument owns of a sound element.
/// @param state   The sound's settings.
/// @param context The file context.
/// @return The Map of the instrument's entries.
[[nodiscard]] serialization::Value encode(const FlatSoundState& state, const plugin::CodecContext& context);

/// @brief @p element (a sound's slot) with the Deluge instrument's entries replaced by @p state's encoding: each such
/// entry takes the encoding's value at its old position, one the encoding no longer writes is dropped, and one the
/// encoding writes that the element lacked is appended in the encoding's order. The sidechain's older tag
/// ("compressor") is dropped: the encoding holds what it said. Every other entry (params, cables, an older file's
/// entries the instrument no longer writes…) is kept, in order; the cables are the cable codec's (cable_codec.h).
/// @param element The sound's slot.
/// @param state   The state whose encoding replaces the instrument's entries.
/// @param context The file context.
/// @return The updated element.
[[nodiscard]] serialization::Value with_state(const serialization::Value& element, const FlatSoundState& state,
                                              const plugin::CodecContext& context);

/// @brief The instrument's encoding of top-level entry @p key, as encode writes it.
/// @param state                The sound's settings.
/// @param key                  A sound element's top-level key.
/// @param context              The file context.
/// @param compressor_threshold As encode_pieces takes it.
/// @return The entry, or nullopt where @p state writes none there (a transpose of 0, a modulator off FM) or @p key is
///         not the instrument's.
[[nodiscard]] std::optional<serialization::Value>
encode_entry(const FlatSoundState& state, std::string_view key, const plugin::CodecContext& context,
             std::optional<int32_t> compressor_threshold = std::nullopt);

/// @brief Read a whole sound element into @p state, which holds the state of the sound being read into (a new sound's,
/// for a file). Each entry is applied in the element's order, older
/// formats included; anything the element lacks keeps its value. Params, mod knobs and the effects are not the
/// instrument's to read here, and its cables are read and written by the cable codec (decode_cables, encode_cables).
/// @param element The sound element.
/// @param state   The state read into; entries the element lacks keep their value.
/// @param context The file context.
/// @return Nothing, or why the element cannot be read (two ranges of a source with the same top note).
[[nodiscard]] std::expected<void, plugin::StateError> decode(const serialization::Value& element, FlatSoundState& state,
                                                             const plugin::CodecContext& context);

/// @brief The fields of @p from that @p state's encoding does not write, copied into @p state: an FM sound's sources'
/// types, sample playback, ranges and DX patches; a non-FM sound's modulators; a sample source's own transpose and
/// cents and its retrigger phase (and for source 2 the oscillator sync); a non-sample source's sample playback, its
/// ranges unless it is a wavetable, and its DX patch unless it is a DX7; and whether its stutter uses the song's.
/// @param state The state to copy into.
/// @param from  The state the fields are copied from.
void keep_unwritten(FlatSoundState& state, const FlatSoundState& from);

/// @brief A SlotChange in the file's flat layout: what the codec's parse form decodes a change to.
struct FlatSlotChange {
	FlatSoundState state;
	std::vector<std::string_view> changed;
};

/// @brief @p element (a sound's changed slot) decoded onto @p current, whose slot was @p previous: a fresh read of it,
/// as a file is read but without the ring-mod clamp only a file read applies, with every field the encoding does not
/// write kept from @p current, and every entry the element holds as @p previous did kept from @p current too. An entry
/// the change touches is read as a file holds it; what it did not touch, and what no file can hold, keeps its value.
/// @param element  The sound's changed slot.
/// @param previous The slot before the change.
/// @param current  The state the slot decoded to before the change.
/// @param context  The file context.
/// @return The new state and the instrument entries the change touched, or why the element cannot be read.
[[nodiscard]] std::expected<FlatSlotChange, plugin::StateError> decode_change(const serialization::Value& element,
                                                                              const serialization::Value& previous,
                                                                              const FlatSoundState& current,
                                                                              const plugin::CodecContext& context);

/// @brief The state @p state becomes when its setting at @p path takes @p value, as decode_change of its slot with that
/// leaf changed gives it, with the instrument entries the write touched. Only the path's own entry, and "mode" (which a
/// source's and a modulator's reading depends on), are encoded and read.
/// @param state   The sound's state.
/// @param path    The setting's path: not one whose menu reshapes more than its leaf (an oscillator's type or loop
/// mode,
///                or the synth mode).
/// @param value   The value in its Edit form (canonical; a sync level as the level itself).
/// @param context The file context: the slot's tick magnitude.
/// @return The new state and the entries touched, or why the entry cannot be read.
[[nodiscard]] std::expected<FlatSlotChange, plugin::StateError> with_setting(const FlatSoundState& state,
                                                                             std::string_view path,
                                                                             const serialization::Value& value,
                                                                             const plugin::CodecContext& context);

/// @brief The instrument's top-level keys whose encoding differs between @p a and @p b (present in one alone included),
/// in file order.
/// @param a       One state.
/// @param b       The other.
/// @param context The file context.
/// @return The keys.
[[nodiscard]] std::vector<std::string_view> differing_entries(const FlatSoundState& a, const FlatSoundState& b,
                                                              const plugin::CodecContext& context);

/// @brief The instrument's top-level keys whose typed fields differ between @p a and @p b, in file order: what
/// differing_entries tells without encoding, a field no file holds in its state included.
/// @param a One state.
/// @param b The other.
/// @return The keys.
[[nodiscard]] std::vector<std::string_view> changed_entries(const FlatSoundState& a, const FlatSoundState& b);

/// @brief decode_change of @p next's encoding of @p keys onto @p state, whose encoding of them is the slot before: what
/// a slot changed to say @p next there reads as, the entries read as a file holds them (and refused as a file's would
/// be). Only @p keys and "mode" are encoded and read.
/// @param state   The sound's state.
/// @param next    The state the change asks for; it differs from @p state only in @p keys' entries.
/// @param keys    The instrument's top-level keys the change reaches.
/// @param context The file context.
/// @return The new state and the entries touched, or why an entry cannot be read.
[[nodiscard]] std::expected<FlatSlotChange, plugin::StateError> change_to(const FlatSoundState& state,
                                                                          const FlatSoundState& next,
                                                                          std::span<const std::string_view> keys,
                                                                          const plugin::CodecContext& context);

/// @brief decode_change of @p element onto @p current, whose slot is taken to be what @p current encodes to.
/// @param element The sound's changed slot.
/// @param current The state to decode onto.
/// @param context The file context.
/// @return The new state, or why the element cannot be read.
[[nodiscard]] std::expected<FlatSoundState, plugin::StateError>
decode_onto(const serialization::Value& element, const FlatSoundState& current, const plugin::CodecContext& context);

/// @brief Every field of @p state the codec reads and writes, by name, ranges' millisecond zones included; mod knobs
/// and the effects are left out. For comparing two states and for recording what a reader produced.
/// @param state The state to dump.
/// @return A Map of the fields by name.
[[nodiscard]] serialization::Value dump_state(const FlatSoundState& state);

// The settings rows over the flat layout (the reference the record's are checked against)
/// @brief Path @p path's value in @p state in an Edit's form (an LFO's sync level as the level itself), or nothing
/// where the path does not apply to the state: sample playback off a sample source (interpolation off a sample or
/// live-input one) or in FM; an oscillator's type in FM; an oscillator's own transpose and cents on a sample source
/// outside FM; modulators outside FM; a structured path or one the schema does not hold; an unset drum note.
/// @param state The sound's state.
/// @param path  The setting's path.
/// @return The value, or nullopt where the path does not apply.
[[nodiscard]] std::optional<serialization::Value> setting_value(const FlatSoundState& state, std::string_view path);

/// @brief The value @p state holds at @p path whether or not the path applies to the state (an oscillator's type in
/// FM included), as the row stores it: a CHOICE row's index, else the number. The read
/// path of a sound's SettingReader (see plugin::SettingContext's host contract).
/// @param state The sound's state.
/// @param path  The setting's path.
/// @return The stored value, or nullopt for a structured path, one the schema does not hold, a CHOICE row whose held
///         word is none of its choices, or another row whose held text is not a whole number (both states the schema
///         cannot describe).
[[nodiscard]] std::optional<int32_t> held_setting(const FlatSoundState& state, std::string_view path);
/// @brief Whether a sound in @p state may take @p value at @p path, beyond what the schema allows: ring mod rings only
/// the oscillators up to a wavetable, so it is not chosen while either oscillator is past one, nor such an oscillator
/// while it is chosen.
/// @param state The sound's state.
/// @param path  The setting's path.
/// @param value The candidate value.
/// @return True when the sound may take the value.
[[nodiscard]] bool allows_setting(const FlatSoundState& state, std::string_view path,
                                  const serialization::Value& value);

/// @brief Write @p value (canonical, in an Edit's form: an LFO's sync level as the level itself) at @p path into
/// @p state, the inverse of setting_value: the field the path names, whether or not a file holds it in this state.
/// Not for a path whose menu reshapes more than its leaf (an oscillator's type or loop mode, the synth mode): those
/// write the field alone here.
/// @param state The sound's state.
/// @param path  The setting's path; a structured path or one the schema does not hold writes nothing.
/// @param value The value, as canonical_setting gives it.
/// @return The instrument entries the write changed: the path's own, or none when it held the value already.
[[nodiscard]] std::vector<std::string_view> write_setting(FlatSoundState& state, std::string_view path,
                                                          const serialization::Value& value);

/// @brief Whether a sound in @p state takes a setting at @p path at all: where setting_value answers, or where the
/// value is unset (a sound drum's MIDI note before one is chosen).
/// @param state The sound's state.
/// @param path  The setting's path.
/// @return True when the sound takes a setting there.
[[nodiscard]] bool takes_setting(const FlatSoundState& state, std::string_view path);

// The rules over the flat layout (the reference the record's are checked against)
/// @brief @p state with its engine set to @p mode. Into FM both filters go off; out of it a filter that is off comes
/// back on (the low-pass as the 24 dB transistor ladder, the high-pass as the ladder).
[[nodiscard]] FlatSoundState with_mode(FlatSoundState state, SynthMode mode);

/// @brief @p state with slot @p id's oscillator set to @p type. When the ranges change kind (sample to wavetable or
/// back) the record's ranges keep only their top notes, and a slot left with none gains one open range; otherwise the
/// slot takes the ranges it holds now. A DX7 slot without a patch gains the init voice.
/// @param held The ranges the slot plays now (the runtime holds them until it holds no ranges of its own).
/// @param held_are_wavetable Whether those ranges are a wavetable's.
[[nodiscard]] FlatSoundState with_osc_type(FlatSoundState state, SourceId id, OscType type,
                                           std::span<const RangeState> held, bool held_are_wavetable);

/// @brief @p state with slot @p id's loop mode set: stretching to note length makes pitch and speed independent, and
/// leaving it gives that up.
[[nodiscard]] FlatSoundState with_loop_mode(FlatSoundState state, SourceId id, SampleRepeatMode mode);

/// @brief @p state with the effect settings that follow the song set from @p defaults: a new sound is
/// `with_song_defaults(FlatSoundState{}, defaults)`.
[[nodiscard]] FlatSoundState with_song_defaults(FlatSoundState state, const effects::SongDefaults& defaults);

/// @brief @p state set up to play samples (a new kit row, the sample browser's preview): polyphony automatic, the 24dB
/// ladder low-pass, both oscillators sample ones.
[[nodiscard]] FlatSoundState as_sample_sound(FlatSoundState state);

/// @brief @p state set up as the default new synth: a saw an octave down over a square, four detuned unison parts,
/// the 24dB ladder low-pass.
[[nodiscard]] FlatSoundState as_default_synth(FlatSoundState state);

/// @brief @p state set up as a blank new synth, a DX7 one when @p dx (its first oscillator the init voice).
[[nodiscard]] FlatSoundState as_blank_synth(FlatSoundState state, bool dx);

// Cable acceptance over the flat layout
/// @brief Whether a cable from @p source to @p destination may exist on a sound in this state: the sound's settings
/// alone decide it (its synth mode, its filters' modes, its LFOs' sync, whether it is a kit's drum), never its params'
/// values.
/// @param source      The cable's source.
/// @param destination A patched param's id, or kAnyDestination.
/// @param state       The sound's typed state, its own effects' settings with it.
/// @param is_drum     Whether the sound is a kit's drum.
/// @return Whether the cable is allowed.
[[nodiscard]] bool cable_allowed(PatchSource source, plugin::ParamId destination, const FlatSoundState& state,
                                 bool is_drum);

} // namespace deluge::instrument
