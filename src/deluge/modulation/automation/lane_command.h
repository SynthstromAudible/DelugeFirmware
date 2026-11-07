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

#include "deluge-project/param_edit.h"
#include "model/snapshot/param_snapshot.h"
#include <cstdint>

namespace deluge::modulation {

/// @brief An Edit made to a lane being recorded, which the render makes to its working copy in order with the values
/// recorded. Held by the interaction tier until the commit after the render has made it.
struct LaneCommand {
	enum class Kind : uint8_t {
		BASE,   ///< `value` becomes the base
		REGION, ///< `value` over [pos, pos + length), as project::setValueForRegion with `region`
		CLEAR,  ///< every node deleted
		PASTE,  ///< `pasted` over [pos, endPos), scaled by `scale`, as project::pasteRegion over `loopLength`
	};
	Kind kind{Kind::BASE};
	int32_t value{0};
	int32_t pos{0};
	int32_t length{0};
	int32_t endPos{0};
	int32_t loopLength{0};
	float scale{1.0F};
	bool isPatchCable{false};
	snapshot::RegionContext region{};
	project::CopiedAutomation pasted;
};

} // namespace deluge::modulation
