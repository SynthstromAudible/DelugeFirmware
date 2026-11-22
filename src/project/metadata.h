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

#include <cstdint>
#include <string>

namespace deluge::project {

/// Sentinel for "no preset slot assigned" (the legacy slot numbering uses 0x7FFF).
constexpr uint16_t kNoSlot = 32767;
/// Sentinel for "no preset sub-slot assigned".
constexpr int8_t kNoSubSlot = -1;

/// @brief Song file identity: display name, containing directory, and the legacy preset-slot numbering.
/// A component of deluge::project::Song.
///
/// TODO: name/dir_path are std::string (heap); convert to a fixed-capacity (FAT-bounded) etl::string. It ripples
/// into function signatures (the std::string* passed to the sample recorder, the caselessEquals/append call sites).
struct Metadata {
	std::string name;
	std::string dir_path;
	/// The extension of the file the song was loaded from or last saved as (".json", or ".XML" for a song saved before
	/// songs saved as JSON); the host gives a new song the extension it saves songs with.
	std::string file_extension;
	uint16_t slot{kNoSlot};
	int8_t sub_slot{kNoSubSlot};
};

} // namespace deluge::project
