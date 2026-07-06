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
// DOM's DocAutoParam directly (OrderedPosVector<ParamNode>), take timeline context as parameters (never engine
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

/// Delete all nodes within the region [pos, pos+length) of a loop of `effectiveLength`, wrapping at the loop
/// point; if the region covers the whole loop, clears all automation. `current_value` is zeroed if no nodes
/// remain (matching the engine's MPE-safety reset). Port of AutoParam::deleteNodesWithinRegion's node kernel
/// (the engine's action/notify are handled at the wiring seam). NB: erases the before-wrap tail before the
/// after-wrap front — the engine did the reverse, a bug that left before-wrap nodes undeleted (now fixed).
void deleteNodesWithinRegion(DocAutoParam& p, int32_t pos, int32_t length, int32_t effectiveLength);

/// Remap CC-range values to channel-pressure-range: halve and bias into the top half (`v>>1 + 2^30`), applied to
/// `current_value` and every node value. Faithful port of AutoParam::transposeCCValuesToChannelPressureValues.
void transposeCCValuesToChannelPressure(DocAutoParam& p);

/// Reverse the automation's node positions within a loop of `reverseDirectionWithLength` ticks (a no-op if
/// that's 0, or if there are no nodes). `current_value` is left untouched — only the node array is rebuilt,
/// mirroring positions around the loop and re-deriving each node's value/interpolated flag from its old
/// neighbours. Faithful port of AutoParam::flattenReverse; the reversed-position math itself is covered by
/// param_clone_test.
void flattenReverse(DocAutoParam& p, int32_t reverseDirectionWithLength);

/// Trim the timeline to `newLength`: nodes at or beyond `newLength` are dropped. If dropping the tail would
/// change the effective value at pos 0 (e.g. under looping wraparound, when no node already sits there), a
/// new non-interpolating node is inserted at pos 0 carrying the pre-trim value so playback continuity at the
/// loop point is preserved. If fewer than 2 nodes would remain afterwards, all automation is cleared instead.
/// `loopLength` is the timeline loop length (the engine reads it from the ModelStack via getValueAtPos) —
/// pass 2147483647 (INT32_MAX) for an infinite/non-looping timeline. Faithful port of AutoParam::trimToLength's
/// node-mutation kernel (the "basic trim" path — the engine's undo-snapshot branch is wiring around this same
/// final mutation and is not part of this kernel; deleteNodesBeyondPos is folded in via the existing
/// deleteNodesBeyond helper).
void trimToLength(DocAutoParam& p, int32_t newLength, int32_t loopLength);

/// Concatenate/tile `src`'s automation onto the end of `dest`, at the seam `oldLength` (the length of the
/// repeat already in `dest`): straight-copies `src`'s nodes offset by `oldLength` when `reverseThisRepeatWithLength`
/// is 0, or mirrors them (matching the pingpong reversal math used elsewhere) when it is nonzero. When
/// `pingpongingGenerally` is set and `src` has no node at pos 0 but its first node is interpolated, synthesises a
/// seam node in `dest` at `oldLength` carrying the interpolated value, so a pingpong direction-flip mid-segment
/// doesn't audibly jump. Also trims any of `dest`'s own nodes beyond `oldLength + src.nodes[0].pos` first (stray
/// overflow left over from e.g. arranger recording). `current_value` is untouched. Faithful port of
/// AutoParam::appendParam (the pure node-mutation kernel; the engine's Action/ModelStack plumbing is not part of
/// this op at all — appendParam takes neither).
void appendParam(DocAutoParam& dest, const DocAutoParam& src, int32_t oldLength, int32_t reverseThisRepeatWithLength,
                 bool pingpongingGenerally);

/// Nudge the single non-interpolating node at `pos` by `offset` (must be +1 or -1, matching every engine call
/// site) within a loop of `lengthBeforeLoop`: moves it to `pos + offset`, wrapping at the loop edges, and
/// merges into (or is erased by) whatever node already sits at the destination. A node that doesn't exist at
/// `pos`, or that is interpolated, is left untouched. Faithful port of AutoParam::nudgeNonInterpolatingNodesAtPos
/// (the pure node-mutation kernel; the engine's undo-action snapshot and interpolation-cursor reset are
/// wiring-seam concerns and are not ported here).
void nudgeNonInterpolatingNodesAtPos(DocAutoParam& p, int32_t pos, int32_t offset, int32_t lengthBeforeLoop);

/// Write `value` across the region [pos, pos+length): the region-write half of AutoParam::setValueForRegion
/// (AutoParam::setValuePossiblyForRegion's dispatcher, and the point-write path via
/// setCurrentValueInResponseToUserInput, are engine wiring/RT-recording concerns and are not ported here — a
/// caller with `length == 0` is expected to take that branch itself, e.g. via setNodeAtPos).
///
/// Normally homogenizes [pos, pos+length) to a single flat/ramped segment (see homogenizeRegion); when the
/// region covers the *entire* loop/Clip/TimelineCounter it instead clears all automation and writes
/// `current_value` directly (the node-mutation half of AutoParam::deleteAutomation — Action snapshotting is
/// engine wiring and isn't ported; net on `nodes`/`current_value` is identical whichever way the engine gets
/// there).
///
/// Timeline/playback context the engine reads off the ModelStack becomes explicit: `effectiveLength` is the
/// loop length (2147483647 for infinite/non-looping); `interpolateLeftNode`/`interpolateRightNode` are the
/// automation-view's interpolation-before/-after settings, already resolved by the caller (the engine decides
/// these from `getRootUI() == &automationView`, matching homogenizeRegion's own parameter list);
/// `recordingLinearly` is the Clip's getCurrentlyRecordingLinearly() (only consulted, inside homogenizeRegion,
/// on an unlimited forward loop-wrap); `clockActive` is playbackHandler.isEitherClockActive(); `livePos` /
/// `currentlyPlayingReversed` are the transport's current position/direction
/// (ModelStackWithAutoParam::getLivePos() / isCurrentlyPlayingReversed()), consulted only to decide whether the
/// new value takes effect on `current_value` immediately (the playhead is inside the edited region right now)
/// versus being left for the next node lookup to pick up during playback. Note the *region* itself is always
/// homogenized forward (the engine's only call site passes `reversed=false` to homogenizeRegion) —
/// `currentlyPlayingReversed` is the unrelated live transport direction, used solely for that live-pos check.
///
/// Returns std::nullopt when nothing was touched at all — `pos` at/past `effectiveLength` (matching the
/// engine's early return, before which it never even creates an Action or calls notify). Otherwise returns the
/// engine's local `automationChanged` (false on the whole-region collapse path, true on the normal homogenize
/// path): the value the wiring seam needs for its notifyParamModifiedInSomeWay(..., automationChanged, ...)
/// call, which the engine otherwise makes unconditionally whenever this doesn't return nullopt. Action
/// snapshotting, the notify call itself, and interpolation-cursor state (resetInterpolationIncrement) are
/// engine wiring and are not ported. Faithful port of AutoParam::setValueForRegion's node/value mutation.
std::optional<bool> setValueForRegion(DocAutoParam& p, uint32_t pos, uint32_t length, int32_t value,
                                      int32_t effectiveLength, bool interpolateLeftNode, bool interpolateRightNode,
                                      bool recordingLinearly, bool clockActive, int32_t livePos,
                                      bool currentlyPlayingReversed);

/// Value carrier for copyRegion/pasteRegion: automation nodes lifted onto their own little timeline, always
/// starting with a node at pos 0 (`width` is the number of ticks it spans). Mirrors the engine's
/// CopiedParamAutomation (a plain heap node buffer used as the automation clipboard) but as an owned vector,
/// so the kernel needs no engine SDRAM allocation. An empty `nodes` means nothing is copied.
struct CopiedAutomation {
	int32_t width{0};
	std::vector<ParamNode> nodes;
};

/// Extract the automation within [startPos, endPos) into `out`, relative to `startPos` (so `out.nodes[0].pos`
/// is always 0 when non-empty, and `out.width` is endPos-startPos). If there is no node exactly at `startPos`,
/// a leading node is synthesised carrying the value the parameter had there (via getValueAtPos), so the copy
/// is self-contained. When `isPatchCable`, copied values are left-shifted-and-saturated by 1, converting from
/// the patch-cable node range into the general parameter range (the convention used when a patch cable's
/// automation is lifted onto the clipboard). `loopLength` is the timeline loop length (the engine reads it
/// from the ModelStack via getValueAtPos) — pass 2147483647 (INT32_MAX) for infinite/non-looping. Faithful
/// port of AutoParam::copy.
void copyRegion(const DocAutoParam& p, int32_t startPos, int32_t endPos, int32_t loopLength, bool isPatchCable,
                CopiedAutomation& out);

/// Paste `copied` (as produced by copyRegion) into [startPos, endPos), time-scaling each node's position
/// relative to `startPos` by `scaleFactor`; a pasted node landing outside [startPos, min(endPos, loopLength))
/// or colliding with an already-placed node is dropped. Nodes already inside the destination region are
/// cleared first; unless the region covers the whole loop, an edge node is synthesised/repurposed at each end
/// (holding the value the destination had just outside the region) so automation outside the pasted region is
/// preserved. When `isPatchCable`, pasted values are halved, converting from the general parameter range back
/// down to the patch-cable node range. `loopLength` is the timeline loop length (the engine reads it from the
/// ModelStack). Does nothing if `copied` is empty (the engine FREEZE_WITH_ERRORs on that precondition in debug
/// builds; the kernel just no-ops). Faithful port of AutoParam::paste.
void pasteRegion(DocAutoParam& p, int32_t startPos, int32_t endPos, float scaleFactor, int32_t loopLength,
                 bool isPatchCable, const CopiedAutomation& copied);

/// The nodes captured by stealNodes(): a plain value-typed carrier (std::vector<ParamNode>) standing in for
/// the engine's `StolenParamNodes`, which points into memory obtained from `deluge::memory::alloc_fast` — an
/// engine allocator param_edit.cpp (self-contained) cannot call. Each node's `pos` is rebased to be relative
/// to the stolen region's start (0..regionLength, wrapped at the loop point), exactly as the engine's stolen
/// node positions are.
using StolenNodes = std::vector<ParamNode>;

/// Cut the nodes inside the region [pos, pos+regionLength) out of `p` (wrapping at `loopLength`); automation
/// outside the region is untouched. If `outStolen` is non-null and the region actually contained nodes, the
/// cut nodes are copied into it (each rebased so its `pos` is relative to the region's start) — `outStolen` is
/// left untouched when nothing was cut, mirroring the engine's `if (numNodesToSteal)` guard around populating
/// `stolenNodeRecord`. `current_value` is never touched (nor is the engine's). Faithful port of
/// AutoParam::stealNodes's node kernel (the engine's `Action*` undo-snapshot is wiring, dropped here).
void stealNodes(DocAutoParam& p, int32_t pos, int32_t regionLength, int32_t loopLength,
                StolenNodes* outStolen = nullptr);

/// Clear the region [pos, pos+regionLength) (as stealNodes does, discarding whatever automation was there) and
/// splice `stolenNodes` (as produced by stealNodes — positions relative to the *source* region's start) back
/// in starting at `pos`, wrapping at `loopLength`; a stolen node whose relative pos is >= regionLength is
/// dropped (the destination region is shorter than the source one's `regionLength`). Faithful port of
/// AutoParam::insertStolenNodes's node kernel (the engine's Action snapshot and notifyParamModifiedInSomeWay
/// are wiring, dropped here).
void insertStolenNodes(DocAutoParam& p, int32_t pos, int32_t regionLength, int32_t loopLength,
                       const StolenNodes& stolenNodes);

} // namespace deluge::project
