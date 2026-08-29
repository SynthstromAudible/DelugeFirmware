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
/// @brief The four envelope pages' exported root span. Every leaf is a plain `ParamRef`, so this
///        subtree has no `get`/`set` hooks at all and the `Node` literals in `sound_env.h` are the
///        whole of it.

#include "scenes/menu/nodes/sound_env.h"

namespace deluge::scenes::menu::nodes {

namespace {
constexpr const Node* kRoots[] = {&env1, &env2, &env3, &env4};
} // namespace

const std::span<const Node* const> kSoundEnvChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
