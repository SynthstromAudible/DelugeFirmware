/*
 * Copyright © 2024 Synthstrom Audible Limited
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

class Song;
class Serializer;
class Deserializer;

namespace deluge::project {
struct Groove;
struct Arrangement;
struct Transport;
struct ScaleSettings;
} // namespace deluge::project

namespace deluge::bridge {

using deluge::project::Arrangement;
using deluge::project::Groove;
using deluge::project::ScaleSettings;
using deluge::project::Transport;

/// @brief Write the song body (`<song>…</song>`) through @p out; the model's parts are written by the writers below at
/// their place among the engine's.
void saveSong(::Song& song, Serializer& out);

/// @brief Read a song from @p in, which must read a document tree (every song is opened as one): decode the model's
/// song-level records and arrangement from the tree, read the engine's parts through @p in, then resolve what the
/// model names by id or by position against the engine's clips, outputs and MIDI devices.
Error loadSong(::Song& song, Deserializer& in);

/// @brief Whether @p tagName is one of the song's tags the model decodes, which the engine's reader skips.
[[nodiscard]] bool isDecodedSongTag(char const* tagName);

/// @brief The swing (`swingAmount`, `swingInterval`), the interval written relative to the song's tick magnitude.
void serialize(const Groove& groove, Serializer& out, ::Song* song);

/// @brief The `sections` block: every section, its repeats and its MIDI launch command.
void serializeSections(const Arrangement& arrangement, Serializer& out);

/// @brief The `sessionMacros` block, up to the last macro that does something; clips and outputs by id.
void serializeMacros(const Arrangement& arrangement, Serializer& out);

/// @brief The tempo, as the tick length's high and low halves, and the input tick magnitude.
void serialize(const Transport& transport, Serializer& out);

/// @brief The key's root note (written among the song's attributes).
void serializeScaleKey(const ScaleSettings& scale, Serializer& out);
/// @brief The key's mode notes and the `scales` block (written among the song's elements).
void serializeScaleNotes(const ScaleSettings& scale, Serializer& out);

} // namespace deluge::bridge
