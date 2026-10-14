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

/// @file
/// @brief The sound-editor root's exported span. Everything else about it is `constexpr` and lives
///        in `sound_root.h`; this translation unit exists so the header has a home for the
///        `extern`-declared span the other `nodes/*.cpp` files all provide, and so the root is
///        linked in even when nothing else in the library refers to it.

#include "scenes/menu/nodes/sound_root.h"

namespace deluge::scenes::menu::nodes {

const std::span<const Node* const> kSoundRootChildrenSpan{kSoundRootChildren};

} // namespace deluge::scenes::menu::nodes
