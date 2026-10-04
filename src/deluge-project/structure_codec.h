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

#include "deluge-project/codec.h"
#include "deluge-project/doc_audio_clip.h"
#include "deluge-project/doc_clip.h"
#include "deluge-project/doc_drum.h"
#include "deluge-project/doc_note_row.h"
#include "deluge-project/doc_output.h"
#include "deluge-project/instrument_slot.h"
#include "serialization/value.h"

#include <cstdint>
#include <expected>
#include <string_view>

namespace deluge::project {

// The structure records: the model's keys within the element that carries them (a clip, a note row, an output, a
// drum), beside keys that are the engine's. Each encode returns a Map of the record's keys only; each decode reads
// them from the element and keeps any value the element lacks.

/// The MIDI channel value that sends a MIDI output's notes to the song's transpose instead of a channel.
inline constexpr int32_t kMidiChannelTranspose = 55;
/// The MIDI channel values naming the lower and upper MPE zones.
inline constexpr int32_t kMidiChannelLowerZone = 16;
inline constexpr int32_t kMidiChannelUpperZone = 17;

/// @brief A clip's `length` (legacy `trackLength`; at least 1), `colourOffset`, `section` (at most 23 as read;
/// 255, arranger-only, is not written) and `launchStyle`.
[[nodiscard]] serialization::Value encode(const DocClip& clip);
[[nodiscard]] std::expected<void, LoadError> decode(const serialization::Value& clipElement, DocClip& clip);

/// @brief A note row's `y` (not in kits, whose rows are placed by their drums), `muted`, `colourOffset` (kits
/// only), `length` and `sequenceDirection`.
[[nodiscard]] serialization::Value encode(const DocNoteRow& row, bool in_kit);
/// @param min_y The lowest y this row may take, raised past it on return: a clip's rows rise strictly in file order.
[[nodiscard]] std::expected<void, LoadError> decode(const serialization::Value& rowElement, DocNoteRow& row,
                                                    int32_t& min_y);

/// @brief An audio clip's sample, its positions (as the int32 the file holds), pitch, playback and priority.
[[nodiscard]] serialization::Value encode(const DocAudioClip& clip);
[[nodiscard]] std::expected<void, LoadError> decode(const serialization::Value& clipElement, DocAudioClip& clip);

/// The CC a MIDI output sends MPE Y on until the user picks another (CC 74, MPE's timbre controller).
inline constexpr uint8_t kMidiOutputMpeYDefault = 74;

/// @brief A new output record of the type @p element names, holding the values the engine gives that type before
/// reading a file: what a key the file lacks stays as.
[[nodiscard]] DocOutput new_output(std::string_view element);

/// @brief An output's identity within its @p element (`sound`, `kit`, `audioTrack`, `midi`, `midiChannel`,
/// `mpeZone` or `cvChannel`): arrangement state, colour, name, folder, velocity and the type's own channel words.
/// A default velocity of 0 or 128 and above keeps the record's value.
[[nodiscard]] serialization::Value encode_identity(const DocOutput& output, std::string_view element);
[[nodiscard]] std::expected<void, LoadError> decode_identity(const serialization::Value& outputElement,
                                                             std::string_view element, DocOutput& output);

/// @brief A drum's identity within its @p element (`sound`, `sample`, `synth`, `midiOutput` or `gateOutput`,
/// which gives its type): its name, and the channel and note of a MIDI or gate drum.
[[nodiscard]] serialization::Value encode_identity(const DocDrum& drum);
[[nodiscard]] std::expected<void, LoadError> decode_identity(const serialization::Value& drumElement,
                                                             std::string_view element, DocDrum& drum);

/// @brief The instrument of a synth track's `sound` element: the element's entries the model does not own (its id and
/// identity keys), in order, for the Deluge instrument.
[[nodiscard]] InstrumentSlot slot_of_output(const serialization::Value& element);

/// @brief The instrument of a sound kit row's element: its entries other than its id and name, in order.
[[nodiscard]] InstrumentSlot slot_of_drum(const serialization::Value& element);

/// @brief The element a drum of @p type is written as.
[[nodiscard]] std::string_view drum_element(DrumType type);

} // namespace deluge::project
