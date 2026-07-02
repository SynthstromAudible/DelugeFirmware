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

/// Tile the first repeat's automation (nodes in [0, oldLength)) out to `newLength`. When `shouldPingpong`,
/// odd repeats are mirrored (a synthesised node at pos 0 keeps the seam continuous). Faithful port of
/// AutoParam::generateRepeats (→ OrderedPosVector::generateRepeats for the non-pingpong path).
void generateRepeats(DocAutoParam& p, uint32_t oldLength, uint32_t newLength, bool shouldPingpong);

/// Collapse the automation inside the region [startPos, startPos+length) to a single flat/ramped segment,
/// while leaving automation outside the region unchanged: places/repurposes an edge node at each end (the
/// far edge carries the value the automation had just past the region, so downstream playback is preserved)
/// and deletes the nodes between them. Handles loop wrap, reversed playback, and clip-cut limiting. Returns
/// the index of the region's leftmost node (or -1 on failure). Faithful port of AutoParam::homogenizeRegion.
///
/// Timeline context is passed explicitly (the engine reads it from the ModelStack): `effectiveLength` is the
/// loop length, `recordingLinearly` is the clip's getCurrentlyRecordingLinearly() (only consulted on a
/// forward wrap with no cut), and `posAtWhichClipWillCut` bounds the region (2147483647 = no cut).
int32_t homogenizeRegion(DocAutoParam& p, int32_t startPos, int32_t length, int32_t startValue,
                         bool interpolateLeftNode, bool interpolateRightNode, int32_t effectiveLength, bool reversed,
                         bool recordingLinearly, int32_t posAtWhichClipWillCut = 2147483647);

/// Remove the time span [startPos, startPos+lengthToDelete) from the timeline: nodes inside it are deleted and
/// everything after shifts left by that much, capping the cut point with a node so the pre-cut value is
/// preserved. `loopLength` is the timeline loop length (the engine reads it from the ModelStack via
/// getValueAtPos). Faithful port of AutoParam::deleteTime.
void deleteTime(DocAutoParam& p, int32_t startPos, int32_t lengthToDelete, int32_t loopLength);

/// Nudge the automation nodes in a region horizontally by `offset` (must be +1 or -1) within a loop of
/// `lengthBeforeLoop`, wrapping/colliding at the region and loop edges. Faithful port of
/// AutoParam::moveRegionHorizontally (the pure node-mutation kernel; the engine's undo snapshot is a separate
/// concern handled at the wiring seam).
void moveRegionHorizontally(DocAutoParam& p, int32_t pos, int32_t length, int32_t offset, int32_t lengthBeforeLoop);

} // namespace deluge::project
