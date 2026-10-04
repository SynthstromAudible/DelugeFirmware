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

} // namespace deluge::instrument
