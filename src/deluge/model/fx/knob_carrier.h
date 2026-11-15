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

#include "deluge-project/mapping_table.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace deluge::model {

/// @brief The learned knobs an owner holds outside a song's table. A song's table holds an owner's knobs once the owner
/// joins it (a song's load, an arrival, a preset change's hand-over); until then they are here.
struct KnobCarrier {
	/// The knobs its file's `midiKnobs` read, in file order, each target's owner a placeholder. The table takes them
	/// when the owner joins a song, and they are emptied then; a save of an owner no song holds writes these.
	std::vector<project::Mapping> loaded{};
	/// The output whose learned knobs this one, a clone the render made, takes in the table, until the app loop copies
	/// them there (bridge::cloneOutputMappings). Should the render's held copy be lost, the drain that finds the loss
	/// copies them from it, before any held forget lands.
	std::optional<uint32_t> cloned_from{};
};

} // namespace deluge::model
