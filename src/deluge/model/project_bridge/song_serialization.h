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

namespace deluge::bridge {

/// @brief Write the song through @p out, a JSON writer (a song is never written as XML); the model's parts are written
/// as their codecs encode them, at their place among the engine's.
///
/// @param song The song to write.
/// @param out  The JSON serializer to write through.
void saveSong(::Song& song, Serializer& out);

/// @brief Read a song from @p in, which must read a document tree (every song is opened as one): decode the model's
/// song-level records and arrangement from the tree, read the engine's parts through @p in, then resolve what the
/// model names by id or by position against the engine's clips, outputs and MIDI devices.
///
/// @param song The song to read into.
/// @param in   The deserializer, reading a document tree.
/// @return The first error met, else Error::NONE.
Error loadSong(::Song& song, Deserializer& in);

/// @brief Whether @p tagName is one of the song's tags the model decodes, which the engine's reader skips.
///
/// @param tagName The tag name.
/// @return true if the model decodes it.
[[nodiscard]] bool isDecodedSongTag(char const* tagName);

} // namespace deluge::bridge
