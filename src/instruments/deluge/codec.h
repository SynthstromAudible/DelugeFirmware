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

#include "instruments/deluge/sound_state.h"
#include "plugin-api/plugin.h"
#include "serialization/value.h"

#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace deluge::instrument {

/// @brief The most unison voices, the widest unison detune (cents) and the widest stereo spread a sound takes; a file's
/// values are clamped to them.
inline constexpr int32_t kMaxUnisonVoices = 8;
inline constexpr int32_t kMaxUnisonDetune = 50;
inline constexpr int32_t kMaxUnisonSpread = 50;

/// @brief The instrument's entries of a sound element, split where other owners' entries sit between them in a file:
/// a preset's path comes between effect_attributes and max_voices; params, the arpeggiator and the mod knobs between
/// body and midi_output; the learned MIDI knobs between delay and effect_tags.
struct SoundPieces {
	serialization::Value head;              ///< polyphonic, voicePriority, sideChainSend, mode, transpose, cents
	serialization::Value effect_attributes; ///< modFXType, lpfMode, hpfMode, filterRoute, clippingAmount
	serialization::Value max_voices;        ///< maxVoices
	serialization::Value body;              ///< osc1, osc2, lfo1–lfo4, modulator1–2 (FM only), unison
	serialization::Value midi_output;       ///< midiOutput
	serialization::Value delay;             ///< delay
	serialization::Value effect_tags;       ///< sidechain, audioCompressor, stutter
};

/// @brief encode_pieces() of a sound's record, each component writing its own keys.
[[nodiscard]] SoundPieces encode_pieces(const SoundState& state, const plugin::CodecContext& context,
                                        std::optional<int32_t> compressor_threshold = std::nullopt);

/// @brief encode() of a sound's record.
[[nodiscard]] serialization::Value encode(const SoundState& state, const plugin::CodecContext& context);

/// @brief Whether @p key, at the top of a sound element, is one the instrument writes.
/// @param key An entry name.
/// @return True when the instrument writes it.
[[nodiscard]] bool is_state_key(std::string_view key);

/// @brief with_state() with a sound's record's encoding.
[[nodiscard]] serialization::Value with_state(const serialization::Value& element, const SoundState& state,
                                              const plugin::CodecContext& context);

/// @brief encode_entry() of a sound's record: the component that owns @p key writes it.
[[nodiscard]] std::optional<serialization::Value>
encode_entry(const SoundState& state, std::string_view key, const plugin::CodecContext& context,
             std::optional<int32_t> compressor_threshold = std::nullopt);

/// @brief decode() onto a sound's record: decoded in the flat layout, then composed.
[[nodiscard]] std::expected<void, plugin::StateError> decode(const serialization::Value& element, SoundState& state,
                                                             const plugin::CodecContext& context);

/// @brief What a sound's slot says once it is changed: the record it decodes to, and its instrument entries the change
/// touched, in file order.
struct SlotChange {
	SoundState state;
	std::vector<std::string_view> changed;
};

/// @brief changed_entries() over two sounds' records, read through their readers.
[[nodiscard]] std::vector<std::string_view> changed_entries(const SoundState& a, const SoundState& b);

/// @brief dump_state() of a sound's record, in the file's layout.
[[nodiscard]] serialization::Value dump_state(const SoundState& state);

/// @brief decode_change() onto a sound's record: the change is read in the file's layout and composed.
[[nodiscard]] std::expected<SlotChange, plugin::StateError> decode_change(const serialization::Value& element,
                                                                          const serialization::Value& previous,
                                                                          const SoundState& current,
                                                                          const plugin::CodecContext& context);

/// @brief with_setting() on a sound's record.
[[nodiscard]] std::expected<SlotChange, plugin::StateError> with_setting(const SoundState& state, std::string_view path,
                                                                         const serialization::Value& value,
                                                                         const plugin::CodecContext& context);

/// @brief differing_entries() of two sounds' records, encoded per component.
[[nodiscard]] std::vector<std::string_view> differing_entries(const SoundState& a, const SoundState& b,
                                                              const plugin::CodecContext& context);

/// @brief change_to() between two sounds' records.
[[nodiscard]] std::expected<SlotChange, plugin::StateError> change_to(const SoundState& state, const SoundState& next,
                                                                      std::span<const std::string_view> keys,
                                                                      const plugin::CodecContext& context);

/// @brief decode_onto() onto a sound's record.
[[nodiscard]] std::expected<SoundState, plugin::StateError>
decode_onto(const serialization::Value& element, const SoundState& current, const plugin::CodecContext& context);

} // namespace deluge::instrument
