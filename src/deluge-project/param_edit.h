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

// DOM edit operations on automation node data. These operate on the DOM's DocAutoParam directly
// (OrderedPosVector<ParamNode>), take timeline context as parameters (never engine globals), and mirror the
// corresponding engine AutoParam ops, so they can be unit-tested in isolation and are the single authority for
// automation editing.

#include "deluge-project/param_automation.h" // DocAutoParam / ParamNode
#include <cstdint>

namespace deluge::project {

/// @brief Ensure the DocAutoParam has a node at @p pos carrying @p value, creating one (keeping `nodes` sorted by
/// pos) or modifying the existing node at @p pos.
/// @param p           The automation.
/// @param pos         Node position, in ticks.
/// @param value       The value the node carries.
/// @param interpolate Sets ParamNode::interpolated on the touched node.
/// @return The index of the touched node.
int32_t setNodeAtPos(DocAutoParam& p, int32_t pos, int32_t value, bool interpolate);

/// @brief Value of the parameter at timeline position @p pos, interpolating between nodes as AutoParam::getValueAtPos
/// does.
/// @param p          The automation.
/// @param pos        Timeline position, in ticks.
/// @param loopLength The timeline loop length; pass 2147483647 (INT32_MAX) for an infinite/non-looping timeline.
/// @param reversed   Selects the playback direction.
/// @return The value at @p pos; `current_value` when there are no nodes.
int32_t getValueAtPos(const DocAutoParam& p, uint32_t pos, int32_t loopLength, bool reversed = false);

/// @brief Number of ticks from @p pos to the next node in the playback direction, wrapping across the loop point.
/// @param p          The automation.
/// @param pos        Timeline position, in ticks.
/// @param loopLength The timeline loop length; with no nodes the whole loop is the distance.
/// @param reversed   Selects the playback direction.
/// @return The distance, in ticks.
int32_t getDistanceToNextNode(const DocAutoParam& p, int32_t pos, int32_t loopLength, bool reversed);

/// @brief Open a gap of @p lengthToInsert ticks at @p pos: every node at or after @p pos moves right by that much
/// (nodes before @p pos are untouched).
/// @param p              The automation.
/// @param pos            Where the gap opens.
/// @param lengthToInsert Gap length, in ticks.
void insertTime(DocAutoParam& p, int32_t pos, int32_t lengthToInsert);

/// @brief Rotate all node positions by @p amount ticks within a loop of @p effectiveLength, wrapping around the loop
/// point and keeping `nodes` sorted (see OrderedPosVector::shiftHorizontal).
/// @param p               The automation.
/// @param amount          Shift in ticks; may be positive or negative and is reduced modulo the length.
/// @param effectiveLength Loop length, in ticks.
void shiftHorizontally(DocAutoParam& p, int32_t amount, int32_t effectiveLength);

/// @brief Add @p offset to `current_value` and to every node value, saturating at the int32 range.
/// @param p      The automation.
/// @param offset The value offset.
void shiftValues(DocAutoParam& p, int32_t offset);

/// @brief Tile the first repeat's automation (nodes in [0, oldLength)) out to @p newLength.
///
/// When @p shouldPingpong, odd repeats are mirrored (a synthesised node at pos 0 keeps the seam continuous). The
/// non-pingpong path is OrderedPosVector::generateRepeats.
/// @param p              The automation.
/// @param oldLength      Length of the first repeat, in ticks.
/// @param newLength      Length to tile out to, in ticks.
/// @param shouldPingpong Mirror odd repeats.
void generateRepeats(DocAutoParam& p, uint32_t oldLength, uint32_t newLength, bool shouldPingpong);

/// @brief Collapse the automation inside the region [startPos, startPos+length) to a single flat/ramped segment,
/// leaving automation outside the region unchanged.
///
/// Places/repurposes an edge node at each end (the far edge carries the value the automation had just past the
/// region, so downstream playback is preserved) and deletes the nodes between them. Handles loop wrap, reversed
/// playback, and clip-cut limiting.
/// @param p                   The automation.
/// @param startPos            Region start, in ticks.
/// @param length              Region length, in ticks.
/// @param startValue          The value the region starts at.
/// @param interpolateLeftNode Whether the left edge node interpolates.
/// @param interpolateRightNode Whether the right edge node interpolates.
/// @param effectiveLength     The loop length.
/// @param reversed            Playback direction.
/// @param recordingLinearly   The clip's getCurrentlyRecordingLinearly(); only consulted on a forward wrap with no
///                            cut.
/// @param posAtWhichClipWillCut Bounds the region; 2147483647 = no cut.
/// @return The index of the region's leftmost node, or -1 for a region that can't be homogenized: a non-positive
///         length, a negative start or first node, or a region the cut point leaves empty or puts before the start.
int32_t homogenizeRegion(DocAutoParam& p, int32_t startPos, int32_t length, int32_t startValue,
                         bool interpolateLeftNode, bool interpolateRightNode, int32_t effectiveLength, bool reversed,
                         bool recordingLinearly, int32_t posAtWhichClipWillCut = 2147483647);

/// @brief Remove the time span [startPos, startPos+lengthToDelete) from the timeline: nodes inside it are deleted and
/// everything after shifts left by that much, capping the cut point with a node so the pre-cut value is preserved.
/// @param p              The automation.
/// @param startPos       Start of the span, in ticks.
/// @param lengthToDelete Span length, in ticks.
/// @param loopLength     The timeline loop length (used for the value lookup at the cut).
void deleteTime(DocAutoParam& p, int32_t startPos, int32_t lengthToDelete, int32_t loopLength);

/// @brief Nudge the automation nodes in a region horizontally by @p offset within a loop of @p lengthBeforeLoop,
/// wrapping/colliding at the region and loop edges.
///
/// This is the pure node-mutation kernel; undo snapshots are the caller's concern.
/// @param p                The automation.
/// @param pos              Region start, in ticks.
/// @param length           Region length, in ticks.
/// @param offset           Must be +1 or -1.
/// @param lengthBeforeLoop Loop length, in ticks.
void moveRegionHorizontally(DocAutoParam& p, int32_t pos, int32_t length, int32_t offset, int32_t lengthBeforeLoop);

/// @brief Delete all nodes within the region [pos, pos+length) of a loop of @p effectiveLength, wrapping at the loop
/// point; if the region covers the whole loop, clears all automation.
///
/// `current_value` is zeroed if no nodes remain (the MPE-safety reset). This is the node kernel only; action
/// recording and notification are the caller's. The before-wrap tail is erased before the after-wrap front.
/// @param p               The automation.
/// @param pos             Region start, in ticks.
/// @param length          Region length, in ticks.
/// @param effectiveLength Loop length, in ticks.
void deleteNodesWithinRegion(DocAutoParam& p, int32_t pos, int32_t length, int32_t effectiveLength);

/// @brief Remap CC-range values to channel-pressure-range: halve and bias into the top half (`v>>1 + 2^30`), applied
/// to `current_value` and every node value.
/// @param p The automation.
void transposeCCValuesToChannelPressure(DocAutoParam& p);

/// @brief Reverse the automation's node positions within a loop of @p reverseDirectionWithLength ticks.
///
/// A no-op if that's 0, or if there are no nodes. `current_value` is left untouched — only the node array is rebuilt,
/// mirroring positions around the loop and re-deriving each node's value/interpolated flag from its old neighbours.
/// @param p                          The automation.
/// @param reverseDirectionWithLength Loop length to reverse within, in ticks.
void flattenReverse(DocAutoParam& p, int32_t reverseDirectionWithLength);

/// @brief Trim the timeline to @p newLength: nodes at or beyond @p newLength are dropped.
///
/// If dropping the tail would change the effective value at pos 0 (e.g. under looping wraparound, when no node
/// already sits there), a new non-interpolating node is inserted at pos 0 carrying the pre-trim value so playback
/// continuity at the loop point is preserved. If fewer than 2 nodes would remain afterwards, all automation is
/// cleared instead.
/// @param p          The automation.
/// @param newLength  The new length, in ticks.
/// @param loopLength The timeline loop length (for the value lookup); pass 2147483647 (INT32_MAX) for an
///                   infinite/non-looping timeline.
void trimToLength(DocAutoParam& p, int32_t newLength, int32_t loopLength);

/// @brief Concatenate/tile @p src's automation onto the end of @p dest, at the seam @p oldLength (the length of the
/// repeat already in @p dest).
///
/// Straight-copies @p src's nodes offset by @p oldLength when @p reverseThisRepeatWithLength is 0, or mirrors them
/// (as the pingpong reversal does elsewhere) when it is nonzero. When @p pingpongingGenerally is set and @p src has
/// no node at pos 0 but its first node is interpolated, synthesises a seam node in @p dest at @p oldLength carrying
/// the interpolated value, so a pingpong direction-flip mid-segment doesn't audibly jump. Also trims any of
/// @p dest's own nodes beyond `oldLength + src.nodes[0].pos` first (stray overflow left over from e.g. arranger
/// recording). `current_value` is untouched.
/// @param dest                       The automation appended to.
/// @param src                        The automation appended.
/// @param oldLength                  Length of the repeat already in @p dest, in ticks.
/// @param reverseThisRepeatWithLength 0 to copy straight, else the length to mirror within.
/// @param pingpongingGenerally       Whether a pingpong direction-flip applies.
void appendParam(DocAutoParam& dest, const DocAutoParam& src, int32_t oldLength, int32_t reverseThisRepeatWithLength,
                 bool pingpongingGenerally);

/// @brief Nudge the single non-interpolating node at @p pos by @p offset within a loop of @p lengthBeforeLoop.
///
/// Moves it to `pos + offset`, wrapping at the loop edges, and merges into (or is erased by) whatever node already
/// sits at the destination. A node that doesn't exist at @p pos, or that is interpolated, is left untouched. This is
/// the pure node-mutation kernel; undo snapshots and interpolation-cursor resets are the caller's.
/// @param p                The automation.
/// @param pos              The node's position, in ticks.
/// @param offset           Must be +1 or -1.
/// @param lengthBeforeLoop Loop length, in ticks.
void nudgeNonInterpolatingNodesAtPos(DocAutoParam& p, int32_t pos, int32_t offset, int32_t lengthBeforeLoop);

/// @brief Which value-branch the post-mutation tail of setValueForRegion takes.
///
/// Named so the engine wiring seam can reproduce the interpolation-cursor / UI-notify step the kernel itself does not
/// do. `currentValue = doc->current_value` is uniform across all three (value-taking branches wrote it; `NotifyOnly`
/// left it unchanged).
enum class RegionCurrentValueEffect : uint8_t {
	Set,               ///< current_value written; NO interpolation reset (whole-region, or normal + transport stopped)
	SetAndResetInterp, ///< current_value written AND the interpolation cursor should reset (clock-active, playhead in
	                   ///< region)
	NotifyOnly, ///< current_value UNCHANGED; the engine notifies the UI of automation (clock-active, playhead outside)
};

/// @brief Result of setValueForRegion: the `automationChanged` the wiring seam feeds notifyParamModifiedInSomeWay,
/// plus the value-branch the tail must reproduce.
struct RegionWriteResult {
	bool automationChanged;
	RegionCurrentValueEffect currentValueEffect;
};

/// @brief Write @p value across the region [pos, pos+length).
///
/// Normally homogenizes [pos, pos+length) to a single flat/ramped segment (see homogenizeRegion); when the region
/// covers the *entire* loop/Clip/TimelineCounter it instead clears all automation and writes `current_value`
/// directly. A caller with `length == 0` is expected to take the point-write path itself, e.g. via setNodeAtPos.
/// Action snapshotting, the notify call itself, and interpolation-cursor state are the caller's.
///
/// The region itself is always homogenized forward; @p currentlyPlayingReversed is the unrelated live transport
/// direction, used solely for the live-pos check that decides whether the new value takes effect on
/// `current_value` immediately (the playhead is inside the edited region right now) versus being left for the next
/// node lookup to pick up during playback.
/// @param p                      The automation.
/// @param pos                    Region start, in ticks.
/// @param length                 Region length, in ticks.
/// @param value                  The value to write.
/// @param effectiveLength        Loop length (2147483647 for infinite/non-looping).
/// @param interpolateLeftNode    The automation view's interpolation-before setting (false when not on the automation
///                               view), already resolved by the caller.
/// @param interpolateRightNode   The automation view's interpolation-after setting (false when not on the automation
///                               view), already resolved by the caller.
/// @param recordingLinearly      The Clip's getCurrentlyRecordingLinearly(); only consulted, inside homogenizeRegion,
///                               on an unlimited forward loop-wrap.
/// @param clockActive            playbackHandler.isEitherClockActive().
/// @param livePos                The transport's current position.
/// @param currentlyPlayingReversed The transport's current direction.
/// @return std::nullopt when nothing was touched at all (@p pos at/past @p effectiveLength); otherwise
/// `automationChanged` (false on the whole-region collapse path, true on the normal homogenize path) and which
/// value-branch the post-mutation tail took (see RegionCurrentValueEffect).
std::optional<RegionWriteResult> setValueForRegion(DocAutoParam& p, uint32_t pos, uint32_t length, int32_t value,
                                                   int32_t effectiveLength, bool interpolateLeftNode,
                                                   bool interpolateRightNode, bool recordingLinearly, bool clockActive,
                                                   int32_t livePos, bool currentlyPlayingReversed);

/// @brief Value carrier for copyRegion/pasteRegion: automation nodes on their own little timeline, always starting
/// with a node at pos 0 (`width` is the number of ticks it spans).
///
/// An owned vector, so the kernel needs no engine SDRAM allocation. An empty `nodes` means nothing is copied.
struct CopiedAutomation {
	int32_t width{0};
	std::vector<ParamNode> nodes;
};

/// @brief Extract the automation within [startPos, endPos) into @p out, relative to @p startPos (so
/// `out.nodes[0].pos` is always 0 when non-empty, and `out.width` is endPos-startPos).
///
/// If there is no node exactly at @p startPos, a leading node is synthesised carrying the value the parameter had
/// there (via getValueAtPos), so the copy is self-contained.
/// @param p            The automation.
/// @param startPos     Region start, in ticks.
/// @param endPos       Region end, in ticks.
/// @param loopLength   The timeline loop length (for the value lookup); pass 2147483647 (INT32_MAX) for
///                     infinite/non-looping.
/// @param isPatchCable When true, copied values are left-shifted-and-saturated by 1, converting from the patch-cable
///                     node range into the general parameter range.
/// @param out          Receives the copied automation.
void copyRegion(const DocAutoParam& p, int32_t startPos, int32_t endPos, int32_t loopLength, bool isPatchCable,
                CopiedAutomation& out);

/// @brief Paste @p copied (as produced by copyRegion) into [startPos, endPos), time-scaling each node's position
/// relative to @p startPos by @p scaleFactor.
///
/// A pasted node landing outside [startPos, min(endPos, loopLength)) or colliding with an already-placed node is
/// dropped. Nodes already inside the destination region are cleared first; unless the region covers the whole loop,
/// an edge node is synthesised/repurposed at each end (holding the value the destination had just outside the
/// region) so automation outside the pasted region is preserved. Does nothing if @p copied is empty.
/// @param p            The automation.
/// @param startPos     Destination start, in ticks.
/// @param endPos       Destination end, in ticks.
/// @param scaleFactor  Time-scale applied to each node's position relative to @p startPos.
/// @param loopLength   The timeline loop length.
/// @param isPatchCable When true, pasted values are halved, converting from the general parameter range back down to
///                     the patch-cable node range.
/// @param copied       The automation to paste.
void pasteRegion(DocAutoParam& p, int32_t startPos, int32_t endPos, float scaleFactor, int32_t loopLength,
                 bool isPatchCable, const CopiedAutomation& copied);

/// @brief The nodes captured by stealNodes(): a plain value-typed carrier (std::vector<ParamNode>).
///
/// Each node's `pos` is rebased to be relative to the stolen region's start (0..regionLength, wrapped at the loop
/// point). A value vector rather than the engine's StolenParamNodes: param_edit.cpp is self-contained and cannot call
/// the engine's alloc_fast.
using StolenNodes = std::vector<ParamNode>;

/// @brief Cut the nodes inside the region [pos, pos+regionLength) out of @p p (wrapping at @p loopLength);
/// automation outside the region is untouched.
///
/// If @p outStolen is non-null and the region actually contained nodes, the cut nodes are copied into it (each
/// rebased so its `pos` is relative to the region's start); @p outStolen is left untouched when nothing was cut.
/// `current_value` is never touched.
/// @param p            The automation.
/// @param pos          Region start, in ticks.
/// @param regionLength Region length, in ticks.
/// @param loopLength   Loop length, in ticks.
/// @param outStolen    Receives the cut nodes, or null to discard them.
void stealNodes(DocAutoParam& p, int32_t pos, int32_t regionLength, int32_t loopLength,
                StolenNodes* outStolen = nullptr);

/// @brief Clear the region [pos, pos+regionLength) (as stealNodes does, discarding whatever automation was there) and
/// splice @p stolenNodes back in starting at @p pos, wrapping at @p loopLength.
///
/// A stolen node whose relative pos is >= @p regionLength is dropped (the destination region is shorter than the
/// source one). Action snapshotting and notification are the caller's.
/// @param p            The automation.
/// @param pos          Destination region start, in ticks.
/// @param regionLength Destination region length, in ticks.
/// @param loopLength   Loop length, in ticks.
/// @param stolenNodes  Nodes as produced by stealNodes — positions relative to the *source* region's start.
void insertStolenNodes(DocAutoParam& p, int32_t pos, int32_t regionLength, int32_t loopLength,
                       const StolenNodes& stolenNodes);

/// @brief Insert extra interpolated nodes along a run so a coarsely-quantized interpolation approximates a smooth
/// ramp.
///
/// For each interpolated node whose "small" (quantized-down) value differs from its predecessor's by 2+ steps,
/// synthesises intermediate nodes tracing a straight-line gradient.
/// @param p                   The automation.
/// @param clipLength          Resolves the wrap-around distance to the previous node.
/// @param quantizationRShift  How coarse a "small" value is (22 for pitch bend, 25 for other MIDI params) and how
///                            synthesised values scale back up.
/// @return false only on allocation failure (a partial completion).
bool makeInterpolationGoodAgain(DocAutoParam& p, int32_t clipLength, int32_t quantizationRShift);

/// @brief Record a live value at the current playback position: the node mutation of the engine's live-recording
/// branch (the setNodeAtPos/homogenizeRegion "clear ~0.2s ahead" call, plus the reversed further-right-node
/// interpolated fixup).
///
/// Sets `current_value` to @p value unconditionally.
///
/// Not done here (the caller's): whether to record at all, the "clip too short" early-out
/// (ticksToClear >= effectiveLength — a trivial nodes.clear() the caller does directly), the interpolation cursor,
/// Action snapshotting, notification, and post-write pruning of redundant nodes in a linear run.
/// @param p                         The automation.
/// @param livePos                   The live playback position, in ticks.
/// @param value                     The live value.
/// @param ticksToClear              Region length ahead of @p livePos to homogenize (the engine's "0.2 seconds ahead",
///                                  already converted from real time by the caller).
/// @param doMPEMode                 Selects the single-node MPE shortcut over homogenizing a region (as does
///                                  @p ticksToClear == 0).
/// @param interpolateLeft           The caller's already-decided `reversed || shouldInterpolateRegionStart` flag.
/// @param effectiveLength           The loop length.
/// @param reversed                  The playback direction.
/// @param recordingLinearly         The clip's getCurrentlyRecordingLinearly() (consulted only by homogenizeRegion on
///                                  a forward wrap with no cut).
/// @param posAtWhichPlaybackWillCut Bounds a non-looping clip (2147483647 = no cut).
/// @return The leftmost affected node index, or -1 if homogenizeRegion() rejected the region (then nothing is
///         written, current_value included).
int32_t recordLiveValue(DocAutoParam& p, int32_t livePos, int32_t value, int32_t ticksToClear, bool doMPEMode,
                        bool interpolateLeft, int32_t effectiveLength, bool reversed, bool recordingLinearly,
                        int32_t posAtWhichPlaybackWillCut = 2147483647);

} // namespace deluge::project
