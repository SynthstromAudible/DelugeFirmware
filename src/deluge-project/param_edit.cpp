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

#include "deluge-project/param_edit.h"
#include <algorithm>

namespace deluge::project {

int32_t setNodeAtPos(DocAutoParam& p, int32_t pos, int32_t value, bool interpolate) {
	auto& nodes = p.nodes;
	// nodes are kept sorted ascending by pos; find the first node at or after pos.
	auto it =
	    std::lower_bound(nodes.begin(), nodes.end(), pos, [](const ParamNode& n, int32_t key) { return n.pos < key; });
	auto i = static_cast<int32_t>(it - nodes.begin());
	if (it == nodes.end() || it->pos != pos) {
		it = nodes.insert(it, ParamNode{}); // no node exactly at pos — create one at its sorted position
	}
	it->pos = pos;
	it->value = value;
	it->interpolated = interpolate;
	return i;
}

} // namespace deluge::project
