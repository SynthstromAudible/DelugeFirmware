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

#include <string>

namespace deluge::scenes::menu {

/// @brief The whole declarative menu tree, every root of `nodes::kAllRoots`, as JSON: the shape the website's
///        menu documentation reads (`website/src/data/generated/menu-tree.json`).
///
/// Names and titles resolve through the built-in English table, set before the walk, so the output does not depend
/// on the language a run was left in.
/// @param out Replaced with the JSON, ending in one newline.
/// @return false, with a message on stderr, when the node ids could not be assigned.
bool dump_menu_tree(std::string& out);

} // namespace deluge::scenes::menu
