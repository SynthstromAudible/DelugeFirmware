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

// A model patch cable is named by its depth's id and stores its polarity raw; these read and write them as the
// runtime's words (its source, its destination descriptor, its polarity).

#include "deluge-project/param_automation.h"
#include "model/project_bridge/param_ids.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/patch/patch_cable.h"
#include "util/misc.h"

namespace deluge::bridge {

/// @brief The runtime's packed word of the cable from @p from to @p destination (its destination with its source).
///
/// @param from        The cable's source.
/// @param destination The cable's destination descriptor.
/// @return The packed cable word.
[[nodiscard]] inline uint32_t cable_word(PatchSource from, ParamDescriptor destination) noexcept {
	destination.addSource(from);
	return destination.data;
}

/// @brief The depth id of the cable from @p from to @p destination.
///
/// @param from        The cable's source.
/// @param destination The cable's destination descriptor.
/// @return The cable's depth id.
[[nodiscard]] inline deluge::plugin::ParamId cable_depth(PatchSource from, ParamDescriptor destination) {
	return cableDepthIdOf(cable_word(from, destination));
}

} // namespace deluge::bridge
