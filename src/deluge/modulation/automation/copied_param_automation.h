/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include <cstdint>

class ParamNode;

/// Copied automation nodes. Nodes here are on their own timeline with the length determined by "width" and node
/// positions relative to node 0 starting at time 0.
///
/// numNodes is 0 and nodes is null when there is no copied automation.
class CopiedParamAutomation {
public:
	CopiedParamAutomation() = default;
	~CopiedParamAutomation() = default;

	/// Width in sequencer ticks
	int32_t width;

	/// Copied nodes.
	///
	/// Must be sorted by nodes[i]->pos, and the first node must have pos == 0.
	ParamNode* nodes = nullptr;

	/// Number of nodes allocated at `nodes`.
	int32_t numNodes = 0;
};
