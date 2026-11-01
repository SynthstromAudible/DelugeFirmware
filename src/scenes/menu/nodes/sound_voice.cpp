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
/// @brief The voice page's `get`/`set` hooks and its exported root span. The `Node` literals are in
///        `sound_voice.h`.

#include "scenes/menu/nodes/sound_voice.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "instruments/deluge/words.h"
#include "serialization/value.h"

namespace deluge::scenes::menu::nodes {

using deluge::l10n::String;

namespace {
constexpr const Node* kRoots[] = {&voice};
} // namespace

const std::span<const Node* const> kSoundVoiceChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
