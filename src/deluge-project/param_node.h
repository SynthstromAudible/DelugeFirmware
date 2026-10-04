/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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
#include "deluge-project/ordered_pos_vector.h"

#include <cstdint>

namespace deluge::project {

/// @brief One automation point: a value at a position, optionally ramped to from the previous point.
struct ParamNode {
	/// Position, in ticks from the start of the automated span.
	int32_t pos{0};
	/// The value at this node.
	///
	/// For patch cables, this is stored in the range [-2^30, 2^30-1] while for all other parameter types the full range
	/// is used (even for unipolar parameters!). This means when converting automation from patch cables to non-patch
	/// cables, lshiftAndSaturate<1>() must be used while conversion in the opposite direction (non-patch-cable to
	/// patch-cable) a right shift by 1 is required.
	int32_t value{0};
	/// Whether the value should be interpolated from the previous node to this one.
	///
	/// When false, the value should change only when this node is actually reached.
	bool interpolated{false};

	bool operator==(const ParamNode&) const = default;
};

/// @brief A parameter's automation: its points in ascending position.
using ParamNodeVector = OrderedPosVector<ParamNode>;

} // namespace deluge::project
