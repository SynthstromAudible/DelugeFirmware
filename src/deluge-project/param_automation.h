/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "modulation/params/param_node.h"
#include "modulation/params/param_node_vector.h"
#include <cstdint>
#include <vector>

class AutoParam;

namespace deluge::project {

/// Value-typed document form of one `AutoParam` — the automation leaf of the param/automation DOM
/// (docs/dev/param_automation_dom.md). `ParamNode` is already a `{pos, value, interpolated}` POD and is
/// reused verbatim; an empty `nodes` means the parameter is unautomated and `current_value` applies.
/// Patch-cable AutoParams store their node values in the patch-cable range — the projection copies the
/// stored representation faithfully (no reinterpretation).
struct DocAutoParam {
	int32_t current_value{0};
	std::vector<ParamNode> nodes;
};

/// The projection core operates on the raw pieces (an AutoParam's `currentValue` + its node vector) so
/// it is testable without the whole-app-coupled `AutoParam`; the `AutoParam` overloads below are thin
/// wrappers over these.
inline DocAutoParam toDocAutoParam(int32_t current_value, const ParamNodeVector& nodes) {
	DocAutoParam doc;
	doc.current_value = current_value;
	doc.nodes.reserve(nodes.size());
	for (const ParamNode& node : nodes) {
		doc.nodes.push_back(node);
	}
	return doc;
}

inline void fromDocAutoParam(const DocAutoParam& doc, int32_t& current_value, ParamNodeVector& nodes) {
	current_value = doc.current_value;
	nodes.clear();
	(void)nodes.reserveExtra(static_cast<int32_t>(doc.nodes.size()));
	for (const ParamNode& node : doc.nodes) {
		// doc.nodes are ascending by pos (copied from an ordered source), so each is the new last node.
		int32_t i = nodes.insertSorted(node.pos, /*isDefinitelyLast=*/true).value_or(-1);
		if (i < 0) {
			break; // allocation failed — leave what we have, matching AutoParam::readFromFile on OOM.
		}
		nodes[i] = node;
	}
}

/// Project to/from a live `AutoParam` (defined in param_automation.cpp; reads/writes its public
/// `currentValue` + `nodes` via the core above).
DocAutoParam toDocAutoParam(const AutoParam& param);
void fromDocAutoParam(const DocAutoParam& doc, AutoParam& param);

} // namespace deluge::project
