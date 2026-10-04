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

/// @brief Read a whole sound element into @p state, which holds the state of the sound being read into (a new sound's,
/// for a file). Each entry is applied in the element's order as the sound's reader has always applied it, older
/// formats included; anything the element lacks keeps its value. Params, patch cables, mod knobs and the effects are
/// not the instrument's to read here.
/// @return Nothing, or why the element cannot be read (two ranges of a source with the same top note).
[[nodiscard]] std::expected<void, engine::StateError> decode(const serialization::Value& element, SoundState& state,
                                                             const engine::CodecContext& context);

/// @brief The state a new synth starts with, before anything is read into it: what a file's reading starts from.
[[nodiscard]] SoundState new_synth_state();

/// @brief Every field of @p state the codec reads and writes, by name, ranges' millisecond zones included; mod knobs
/// and the effects are left out. For comparing two states and for recording what a reader produced.
[[nodiscard]] serialization::Value dump_state(const SoundState& state);

} // namespace deluge::instrument
