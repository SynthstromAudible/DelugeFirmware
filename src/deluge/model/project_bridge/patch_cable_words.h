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
[[nodiscard]] inline uint32_t cable_word(PatchSource from, ParamDescriptor destination) noexcept {
	destination.addSource(from);
	return destination.data;
}

/// @brief The depth id of the cable from @p from to @p destination.
[[nodiscard]] inline deluge::engine::ParamId cable_depth(PatchSource from, ParamDescriptor destination) {
	return cableDepthIdOf(cable_word(from, destination));
}

[[nodiscard]] inline PatchSource source_of(const deluge::project::DocCable& cable) {
	return ParamDescriptor(static_cast<int32_t>(cableWordOf(cable.depth))).getBottomLevelSource();
}

[[nodiscard]] inline ParamDescriptor destination_of(const deluge::project::DocCable& cable) {
	return ParamDescriptor(static_cast<int32_t>(cableWordOf(cable.depth))).getDestination();
}

[[nodiscard]] constexpr Polarity polarity_of(const deluge::project::DocCable& cable) noexcept {
	return static_cast<Polarity>(cable.polarity);
}

/// @brief A model patch cable from the runtime's words and its amount.
[[nodiscard]] inline deluge::project::DocCable doc_cable(PatchSource from, ParamDescriptor destination,
                                                         Polarity polarity, deluge::project::DocAutoParam amount) {
	return {.depth = cable_depth(from, destination),
	        .polarity = util::to_underlying(polarity),
	        .amount = std::move(amount)};
}

} // namespace deluge::bridge
