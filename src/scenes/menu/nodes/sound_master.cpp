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
/// @brief The master page's `get`/`set` hooks and its exported root span. The `Node` literals are
///        in `sound_master.h`.

#include "scenes/menu/nodes/sound_master.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "engines/deluge/words.h"
#include "model/edits/setting_edit.h"
#include "processing/sound/sound.h"
#include "serialization/value.h"

#include "processing/source.h"
#include <string>

namespace deluge::scenes::menu::nodes {

namespace {
constexpr const Node* kRoots[] = {&sound_master};
} // namespace

const std::span<const Node* const> kSoundMasterChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
