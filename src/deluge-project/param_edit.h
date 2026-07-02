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

// DOM edit operations on automation node data (param/automation DOM Phase 1 of the DOM-live-authoritative
// split — docs/superpowers/specs/2026-07-02-dom-live-authoritative-automation-design.md). These operate on the
// DOM's DocAutoParam directly (std::vector<ParamNode>), take timeline context as parameters (never engine
// globals), and are faithful ports of the corresponding engine AutoParam ops, so they can be unit-tested in
// isolation and become the single authority for automation editing.

#include "deluge-project/param_automation.h" // DocAutoParam / ParamNode
#include <cstdint>

namespace deluge::project {

/// Ensure the DocAutoParam has a node at `pos` carrying `value`, creating one (keeping `nodes` sorted by pos)
/// or modifying the existing node at `pos`. `interpolate` sets ParamNode::interpolated on the touched node.
/// Returns the index of the touched node. Faithful port of AutoParam::setNodeAtPos.
int32_t setNodeAtPos(DocAutoParam& p, int32_t pos, int32_t value, bool interpolate);

/// Value of the parameter at timeline position `pos`, interpolating between nodes exactly as
/// AutoParam::getValueAtPos does. `loopLength` is the timeline loop length (the engine reads it from the
/// ModelStack; here it is an explicit parameter) — pass 2147483647 (INT32_MAX) for an infinite/non-looping
/// timeline. `reversed` selects the playback direction. Returns `current_value` when there are no nodes.
/// Faithful port of AutoParam::getValueAtPos.
int32_t getValueAtPos(const DocAutoParam& p, uint32_t pos, int32_t loopLength, bool reversed = false);

/// Number of ticks from `pos` to the next node in the playback direction, wrapping across the loop point.
/// `loopLength` is the timeline loop length (the engine reads it from the ModelStack as `effectiveLength`);
/// with no nodes the whole loop is the distance. `reversed` selects the playback direction. Faithful port of
/// AutoParam::getDistanceToNextNode.
int32_t getDistanceToNextNode(const DocAutoParam& p, int32_t pos, int32_t loopLength, bool reversed);

/// Open a gap of `lengthToInsert` ticks at `pos`: every node at or after `pos` moves right by that much
/// (nodes before `pos` are untouched). Faithful port of AutoParam::insertTime.
void insertTime(DocAutoParam& p, int32_t pos, int32_t lengthToInsert);

/// Rotate all node positions by `amount` ticks within a loop of `effectiveLength`, wrapping around the loop
/// point and keeping `nodes` sorted. `amount` may be positive or negative and is reduced modulo the length.
/// Faithful port of AutoParam::shiftHorizontally (→ OrderedPosVector::shiftHorizontal).
void shiftHorizontally(DocAutoParam& p, int32_t amount, int32_t effectiveLength);

/// Add `offset` to `current_value` and to every node value, saturating at the int32 range. Faithful port of
/// AutoParam::shiftValues.
void shiftValues(DocAutoParam& p, int32_t offset);

} // namespace deluge::project
