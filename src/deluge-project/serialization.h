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

/// The single boundary through which a whole song is (de)serialized — the seam the project-model
/// document model (DOM) is being built behind (docs/dev/project_model_level3.md). Today it delegates
/// to the legacy per-class self-serialization (`Song::writeToFile`/`readFromFile`); it is the
/// insertion point where DOM-based (and later Rust) serialization will replace that body, leaving the
/// save/load orchestration and the golden round-trip harness unchanged.
///
/// The caller chooses the concrete format stream (XML/JSON) and passes it in; the seam itself touches
/// no serialization globals.
void saveSong(::Song& song, Serializer& out);   ///< writes the <song>…</song> body via `out`
Error loadSong(::Song& song, Deserializer& in); ///< reads the song body from `in`

} // namespace deluge::project
