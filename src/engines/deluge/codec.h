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

#include "engine-api/engine.h"
#include "engines/deluge/sound_state.h"
#include "serialization/value.h"

#include <expected>
#include <string_view>
#include <vector>

namespace deluge::instrument {

/// @brief The most unison voices, the widest unison detune (cents) and the widest stereo spread a sound takes; a file's
/// values are clamped to them.
inline constexpr int32_t kMaxUnisonVoices = 8;
inline constexpr int32_t kMaxUnisonDetune = 50;
inline constexpr int32_t kMaxUnisonSpread = 50;

/// @brief The instrument's entries of a sound element, split where other owners' entries sit between them in a file:
/// the effects' attributes and a preset's path come between head and max_voices; params, the arpeggiator and the mod
/// knobs between body and midi_output; the effects' elements after midi_output.
struct SoundPieces {
	serialization::Value head;        ///< polyphonic, voicePriority, sideChainSend, mode, transpose, cents
	serialization::Value max_voices;  ///< maxVoices
	serialization::Value body;        ///< osc1, osc2, lfo1–lfo4, modulator1–2 (FM only), unison
	serialization::Value midi_output; ///< midiOutput
};

/// @brief The instrument's entries of a sound element for @p state, in file order, as pieces (see SoundPieces).
/// Numbers are written as the file's 32-bit signed integers (a free-running phase, 0xFFFFFFFF, as -1); sync levels
/// relative to @p context's tick magnitude.
[[nodiscard]] SoundPieces encode_pieces(const SoundState& state, const engine::CodecContext& context);

/// @brief The pieces of encode_pieces as one Map, in file order: what the instrument owns of a sound element.
[[nodiscard]] serialization::Value encode(const SoundState& state, const engine::CodecContext& context);

/// @brief Whether @p key, at the top of a sound element, is one the instrument writes.
[[nodiscard]] bool is_state_key(std::string_view key);

/// @brief @p element (a sound's slot) with the Deluge instrument's entries replaced by @p state's encoding: each such
/// entry takes the encoding's value at its old position, one the encoding no longer writes is dropped, and one the
/// encoding writes that the element lacked is appended in the encoding's order. An older file's instrument entries
/// (oscillatorA, ringMod, a top-level fileName…) are dropped: the encoding holds what they said. Every other entry
/// (params, effects, cables…) is kept, in order.
[[nodiscard]] serialization::Value with_state(const serialization::Value& element, const SoundState& state,
                                              const engine::CodecContext& context);

/// @brief Read a whole sound element into @p state, which holds the state of the sound being read into (a new sound's,
/// for a file). Each entry is applied in the element's order as the sound's reader has always applied it, older
/// formats included; anything the element lacks keeps its value. Params, patch cables, mod knobs and the effects are
/// not the instrument's to read here.
/// @return Nothing, or why the element cannot be read (two ranges of a source with the same top note).
[[nodiscard]] std::expected<void, engine::StateError> decode(const serialization::Value& element, SoundState& state,
                                                             const engine::CodecContext& context);

/// @brief The fields of @p from that @p state's encoding does not write, copied into @p state: an FM sound's sources'
/// types, sample playback, ranges and DX patches; a non-FM sound's modulators; a sample source's own transpose and
/// cents and its retrigger phase (and for source 2 the oscillator sync); a non-sample source's sample playback, its
/// ranges unless it is a wavetable, and its DX patch unless it is a DX7; and the effects, which are not the
/// instrument's.
void keep_unwritten(SoundState& state, const SoundState& from);

/// @brief What a sound's slot says once it is changed: the state it decodes to, and its instrument entries the change
/// touched, in file order.
struct SlotChange {
	SoundState state;
	std::vector<std::string_view> changed;
};

/// @brief @p element (a sound's changed slot) decoded onto @p current, whose slot was @p previous: a fresh read of it,
/// as a file is read but without the ring-mod clamp only a file read applies, with every field the encoding does not
/// write kept from @p current, and every entry the element holds as @p previous did kept from @p current too. An entry
/// the change touches is read as a file holds it; what it did not touch, and what no file can hold, keeps its value.
[[nodiscard]] std::expected<SlotChange, engine::StateError> decode_change(const serialization::Value& element,
                                                                          const serialization::Value& previous,
                                                                          const SoundState& current,
                                                                          const engine::CodecContext& context);

/// @brief decode_change of @p element onto @p current, whose slot is taken to be what @p current encodes to.
[[nodiscard]] std::expected<SoundState, engine::StateError>
decode_onto(const serialization::Value& element, const SoundState& current, const engine::CodecContext& context);

/// @brief The state a new synth starts with, before anything is read into it: what a file's reading starts from.
[[nodiscard]] SoundState new_synth_state();

/// @brief Every field of @p state the codec reads and writes, by name, ranges' millisecond zones included; mod knobs
/// and the effects are left out. For comparing two states and for recording what a reader produced.
[[nodiscard]] serialization::Value dump_state(const SoundState& state);

} // namespace deluge::instrument
