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

#include <cstddef>

extern "C" {

/// @brief One scenes spec file: its id (its path under sim/scenes_spec without the extension) and its entry.
struct DelugeSceneSpecEntry {
	const char* id;          ///< e.g. "menu_scene_spec", "selftest/pass_spec".
	int (*run)(int, char**); ///< The file's CppSpec entry: 0 when every example passed.
};

/// @brief The entry for @p id, or null when no ported spec file has that id.
/// @param id A spec file's id.
/// @return The entry, or null.
const DelugeSceneSpecEntry* deluge_scenes_spec_find(const char* id);

/// @brief How many spec files the table holds.
/// @return The count.
size_t deluge_scenes_spec_count(void);

/// @brief The id of the @p i th spec file.
/// @param i An index below deluge_scenes_spec_count().
/// @return Its id.
const char* deluge_scenes_spec_id(size_t i);

} // extern "C"
