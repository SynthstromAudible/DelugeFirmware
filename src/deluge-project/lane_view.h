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

#include "deluge-project/param_automation.h"
#include "deluge-project/param_node.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::project {

/// @brief A lane, read-only: its base and its nodes, ascending by position. The render reads lanes through one, from a
///        published copy or, while lanes are written as they play, from the model's own.
struct LaneView {
	int32_t base{0};
	std::span<const ParamNode> nodes;

	[[nodiscard]] bool empty() const { return nodes.empty(); }
	[[nodiscard]] int32_t size() const { return static_cast<int32_t>(nodes.size()); }
	[[nodiscard]] const ParamNode& operator[](int32_t i) const { return nodes[static_cast<size_t>(i)]; }

	/// @brief Index of the first node at or after @p key, as OrderedPosVector::firstAtOrAfter.
	/// @param key         The position searched for.
	/// @param search_from The index to search from.
	/// @return That index, or size() if there is none.
	[[nodiscard]] int32_t first_at_or_after(int32_t key, int32_t search_from = 0) const {
		auto it = std::ranges::lower_bound(nodes.begin() + search_from, nodes.end(), key, {}, &ParamNode::pos);
		return static_cast<int32_t>(it - nodes.begin());
	}

	/// @brief The view of a model lane, valid while the lane is not changed.
	/// @param lane The lane.
	/// @return Its base and nodes.
	static LaneView of(const DocAutoParam& lane) {
		return {.base = lane.current_value, .nodes = std::span<const ParamNode>(lane.nodes.begin(), lane.nodes.end())};
	}
};

} // namespace deluge::project
