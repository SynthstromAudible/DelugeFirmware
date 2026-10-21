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
/// @brief The randomizer page's `get`/`set` hooks and its exported root span. The `Node` literals
///        are in `sound_randomizer.h`.
///
/// Only the lock and the eleven `midi_cv` columns need hooks: the sound-side columns are unpatched
/// `ParamRef`s that `bindings::read`/`write` already serve.

#include "scenes/menu/nodes/sound_randomizer.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "model/drum/drum.h"
#include "model/edits/arp_setting_for.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "modulation/arpeggiator.h"

namespace deluge::scenes::menu::nodes {

namespace {
constexpr const Node* kRoots[] = {&randomizer};
} // namespace

const std::span<const Node* const> kSoundRandomizerChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
