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
#include <cmath>

namespace deluge::project {

int32_t setNodeAtPos(DocAutoParam& p, int32_t pos, int32_t value, bool interpolate) {
	auto& nodes = p.nodes;
	// nodes are kept sorted ascending by pos; find the first node at or after pos.
	auto it =
	    std::lower_bound(nodes.begin(), nodes.end(), pos, [](const ParamNode& n, int32_t key) { return n.pos < key; });
	auto i = static_cast<int32_t>(it - nodes.begin());
	if (it == nodes.end() || it->pos != pos) {
		(void)nodes.insertAt(i); // no node exactly at pos — create one at its sorted position
	}
	nodes[i].pos = pos;
	nodes[i].value = value;
	nodes[i].interpolated = interpolate;
	return i;
}

int32_t getValueAtPos(const DocAutoParam& p, uint32_t pos, int32_t loopLength, bool reversed) {
	const auto& nodes = p.nodes;
	if (nodes.empty()) {
		return p.current_value;
	}

	const auto size = static_cast<int32_t>(nodes.size());

	// firstAtOrAfter(pos + !reversed): index of the first node at or after the search key.
	const int32_t searchKey = static_cast<int32_t>(pos + static_cast<uint32_t>(!reversed));
	auto rightIt = std::lower_bound(nodes.begin(), nodes.end(), searchKey,
	                                [](const ParamNode& n, int32_t key) { return n.pos < key; });
	int32_t rightI = static_cast<int32_t>(rightIt - nodes.begin());
	if (rightI >= size) {
		rightI = 0; // wrap: past the last node, the "right" neighbour is the first (loop point)
	}
	const ParamNode& rightNode = nodes[rightI];

	int32_t leftI = rightI - 1;
	if (leftI < 0) {
		leftI += size; // wrap: before the first node, the "left" neighbour is the last (loop point)
	}
	const ParamNode& leftNode = nodes[leftI];
	if (!rightNode.interpolated) {
		return leftNode.value; // stepped node: hold the left value
	}

	int32_t ticksSinceLeftNode = static_cast<int32_t>(pos - static_cast<uint32_t>(leftNode.pos));
	if (ticksSinceLeftNode == 0) {
		return leftNode.value;
	}

	if (ticksSinceLeftNode < 0) { // pos is left of the leftmost node — wrap across the loop point
		if (loopLength == 2147483647) {
			// Infinite length, and we know we're interpolating: we'd have arrived at the next node's value.
			return rightNode.value;
		}
		ticksSinceLeftNode += loopLength;
	}

	int32_t ticksBetweenNodes = rightNode.pos - leftNode.pos;
	if (ticksBetweenNodes <= 0) { // right node wrapped to before the left node — span crosses the loop point
		if (loopLength == 2147483647) {
			return leftNode.value; // infinite length: still on the previous node's value
		}
		ticksBetweenNodes += loopLength;
	}

	const int64_t valueDistance = static_cast<int64_t>(rightNode.value) - static_cast<int64_t>(leftNode.value);
	return static_cast<int32_t>(leftNode.value + (valueDistance * ticksSinceLeftNode / ticksBetweenNodes));
}

int32_t getDistanceToNextNode(const DocAutoParam& p, int32_t pos, int32_t loopLength, bool reversed) {
	const auto& nodes = p.nodes;
	if (nodes.empty()) {
		return loopLength;
	}

	const auto size = static_cast<int32_t>(nodes.size());

	// firstAtOrAfter(pos + !reversed) - reversed: the node bounding `pos` in the playback direction.
	const int32_t searchKey = pos + static_cast<int32_t>(!reversed);
	auto it = std::lower_bound(nodes.begin(), nodes.end(), searchKey,
	                           [](const ParamNode& n, int32_t key) { return n.pos < key; });
	int32_t i = static_cast<int32_t>(it - nodes.begin()) - static_cast<int32_t>(reversed);
	if (i == -1) {
		i = size - 1; // wrap: before the first node, the previous node is the last (loop point)
	}
	else if (i == size) {
		i = 0; // wrap: past the last node, the next node is the first (loop point)
	}

	const ParamNode& node = nodes[i];
	int32_t distance = node.pos - pos;
	if (reversed) {
		distance = -distance;
	}
	if (distance <= 0) {
		distance += loopLength; // node is at/behind pos in our direction — the next hit is a loop away
	}
	return distance;
}

void insertTime(DocAutoParam& p, int32_t pos, int32_t lengthToInsert) {
	auto& nodes = p.nodes;
	auto it =
	    std::lower_bound(nodes.begin(), nodes.end(), pos, [](const ParamNode& n, int32_t key) { return n.pos < key; });
	for (; it != nodes.end(); ++it) {
		it->pos += lengthToInsert;
	}
}

void shiftHorizontally(DocAutoParam& p, int32_t amount, int32_t effectiveLength) {
	auto& nodes = p.nodes;
	if (nodes.empty()) {
		return;
	}

	// Wrap the amount to the length.
	if (amount >= effectiveLength) {
		amount = static_cast<int32_t>(static_cast<uint32_t>(amount) % static_cast<uint32_t>(effectiveLength));
	}
	else if (amount <= -effectiveLength) {
		amount = -static_cast<int32_t>(static_cast<uint32_t>(-amount) % static_cast<uint32_t>(effectiveLength));
	}
	if (amount == 0) {
		return;
	}

	int32_t cutoffPos = -amount;
	if (cutoffPos < 0) {
		cutoffPos += effectiveLength;
	}
	if (amount < 0) {
		amount += effectiveLength;
	}

	auto cutoffIt = std::lower_bound(nodes.begin(), nodes.end(), cutoffPos,
	                                 [](const ParamNode& n, int32_t key) { return n.pos < key; });
	const auto cutoffIndex = static_cast<int32_t>(cutoffIt - nodes.begin());

	for (int32_t i = 0; i < cutoffIndex; i++) {
		nodes[i].pos += amount;
	}
	for (int32_t i = cutoffIndex; i < static_cast<int32_t>(nodes.size()); i++) {
		nodes[i].pos += amount - effectiveLength;
	}

	std::rotate(nodes.begin(), nodes.begin() + cutoffIndex, nodes.end());
}

namespace {
// = engine lshiftAndSaturate<1>: clamp to the signed 31-bit range, then <<1 (a saturating doubling). Used by
// copyRegion/pasteRegion for the patch-cable node-range <-> parameter-range conversion.
int32_t lshiftAndSaturateBy1(int32_t val) {
	if (val > 1073741823) { // 2^30 - 1
		val = 1073741823;
	}
	else if (val < -1073741824) { // -2^30
		val = -1073741824;
	}
	return val << 1;
}

// Verbatim duplicate of the engine's dbIntervals table + shiftVolumeByDB (util/functions.cpp) so param_edit
// stays self-contained. Copied EXACTLY — including the integer-division quirk (howFarUpIntervalFloat is always
// 0) — so the dB-domain volume shift is bit-identical to the engine's.
const float dbIntervals[] = {
    24, // Not really real
    12.1, 7, 5, 3.9, 3.2, 2.6, 2.4, 2, 1.8, 1.7, 1.5, 1.4, 1.3, 1.2, 1.1,
};
int32_t shiftVolumeByDB(int32_t oldValue, float offset) {
	uint32_t oldValuePositive = (uint32_t)oldValue + 2147483648;
	uint32_t currentInterval = oldValuePositive >> 28;
	if (currentInterval >= 1) {
		uint32_t newValuePositive;
		int32_t howFarUpInterval = oldValuePositive & 268435455;
		float howFarUpIntervalFloat = howFarUpInterval / 268435456; // (engine int-division quirk: always 0)
doInterval:
		float dbThisInterval = dbIntervals[currentInterval];
		float dbLeftThisInterval = ((float)1 - howFarUpIntervalFloat) * dbThisInterval;
		if (dbLeftThisInterval > offset) {
			float amountOfRemainingDBWeWant = offset / dbLeftThisInterval;
			float newHowFarUpIntervalFloat = howFarUpIntervalFloat + amountOfRemainingDBWeWant;
			uint32_t newHowFarUpInterval = newHowFarUpIntervalFloat * 268435456;
			newValuePositive = (currentInterval << 28) + newHowFarUpInterval;
		}
		else {
			currentInterval++;
			if (currentInterval == 16) {
				newValuePositive = 4294967295u;
			}
			else {
				offset -= dbLeftThisInterval;
				howFarUpIntervalFloat = 0;
				goto doInterval;
			}
		}
		return newValuePositive - 2147483648u;
	}
	return oldValue;
}

// Saturating add matching AutoParam::shiftValues: clamp to the signed 32-bit range.
int32_t addSaturating(int32_t value, int32_t offset) {
	int64_t newValue = static_cast<int64_t>(value) + offset;
	if (newValue >= static_cast<int64_t>(2147483648u)) {
		return 2147483647;
	}
	if (newValue < static_cast<int64_t>(2147483648u) * -1) {
		return -2147483647 - 1;
	}
	return static_cast<int32_t>(newValue);
}

int32_t firstAtOrAfter(const ParamNodeVector& nodes, int32_t key) {
	auto it =
	    std::lower_bound(nodes.begin(), nodes.end(), key, [](const ParamNode& n, int32_t k) { return n.pos < k; });
	return static_cast<int32_t>(it - nodes.begin());
}

// Port of AutoParam::deleteNodesBeyondPos: drop every node at or after `pos`.
void deleteNodesBeyond(ParamNodeVector& nodes, int32_t pos) {
	nodes.erase(nodes.begin() + firstAtOrAfter(nodes, pos), nodes.end());
}

// Non-pingpong tiling — port of OrderedPosVector::generateRepeats. Tiles the contents (all before wrapPoint)
// out to endPos.
void tileRepeats(ParamNodeVector& nodes, int32_t wrapPoint, int32_t endPos) {
	if (nodes.empty()) {
		return;
	}
	int32_t numCompleteRepeats = static_cast<int32_t>(static_cast<uint32_t>(endPos) / static_cast<uint32_t>(wrapPoint));
	int32_t endPosWithinFirstRepeat = endPos - numCompleteRepeats * wrapPoint;
	int32_t iEndPosWithinFirstRepeat = firstAtOrAfter(nodes, endPosWithinFirstRepeat);

	// Take firstAtOrAfter(wrapPoint) rather than size() so any nodes >= wrapPoint are ignored/chopped.
	int32_t oldNum = firstAtOrAfter(nodes, wrapPoint);
	int32_t newNum = oldNum * numCompleteRepeats + iEndPosWithinFirstRepeat;

	nodes.resize(newNum);

	for (int32_t r = 1; r <= numCompleteRepeats; r++) {
		for (int32_t i = 0; i < oldNum; i++) {
			if (r == numCompleteRepeats && i >= iEndPosWithinFirstRepeat) {
				break;
			}
			nodes[i + oldNum * r] = nodes[i];
			nodes[i + oldNum * r].pos = nodes[i].pos + wrapPoint * r;
		}
	}
}
} // namespace

void shiftValues(DocAutoParam& p, int32_t offset) {
	p.current_value = addSaturating(p.current_value, offset);
	for (ParamNode& node : p.nodes) {
		node.value = addSaturating(node.value, offset);
	}
}

void generateRepeats(DocAutoParam& p, uint32_t oldLength, uint32_t newLength, bool shouldPingpong) {
	auto& nodes = p.nodes;
	if (nodes.empty()) {
		return;
	}

	// When recording session to arranger you can end up with nodes beyond the Clip's length; remove them now.
	deleteNodesBeyond(nodes, static_cast<int32_t>(oldLength) + nodes[0].pos);

	if (!shouldPingpong) {
		tileRepeats(nodes, static_cast<int32_t>(oldLength), static_cast<int32_t>(newLength));
		return;
	}

	// Pingponging: each odd repeat is mirrored, so we tile by hand.
	bool nothingAtZero = nodes[0].pos != 0;

	// A pingpong reverses direction at pos 0; if there's no node there, synthesise one carrying the value at 0
	// so the seam sounds continuous (always place it, matching the engine).
	if (nothingAtZero) {
		const ParamNode& nodeBeforeWrap = nodes[nodes.size() - 1];
		const ParamNode& nodeAfterWrap = nodes[0];
		bool nodeAfterWrapIsInterpolated = nodeAfterWrap.interpolated;

		int32_t valueAtZero;
		if (nodeAfterWrapIsInterpolated) {
			int64_t valueDistance =
			    static_cast<int64_t>(nodeAfterWrap.value) - static_cast<int64_t>(nodeBeforeWrap.value);
			int32_t ticksSinceLeftNode = static_cast<int32_t>(oldLength) - nodeBeforeWrap.pos;
			int32_t ticksBetweenNodes = ticksSinceLeftNode + nodeAfterWrap.pos;
			valueAtZero =
			    static_cast<int32_t>(nodeBeforeWrap.value + (valueDistance * ticksSinceLeftNode / ticksBetweenNodes));
		}
		else {
			valueAtZero = nodeBeforeWrap.value;
		}

		(void)nodes.insertAt(0); // invalidates the references above (no longer used)
		ParamNode& zeroNode = nodes[0];
		zeroNode.pos = 0;
		zeroNode.value = valueAtZero;
		zeroNode.interpolated = nodeAfterWrapIsInterpolated;

		nothingAtZero = false;
	}

	int32_t numRepeats =
	    static_cast<int32_t>((newLength - 1) / oldLength) + 1; // rounded up, incl. the existing first repeat

	int32_t numNodesBefore = static_cast<int32_t>(nodes.size());

	int32_t numToInsert = (numRepeats - 1) * numNodesBefore;
	if (numToInsert) {
		(void)nodes.insertAt(numNodesBefore, numToInsert);
	}

	int32_t highestNodeIndex = numNodesBefore - 1;

	for (int32_t r = 1; r < numRepeats; r++) {
		for (int32_t iNewWithinRepeat = 0; iNewWithinRepeat < numNodesBefore; iNewWithinRepeat++) {
			int32_t iOld = iNewWithinRepeat;

			if (r & 1) {
				iOld = -iOld - static_cast<int32_t>(nothingAtZero);
				if (iOld < 0) {
					iOld += numNodesBefore;
				}
			}

			const ParamNode& oldNode = nodes[iOld];
			int32_t newPos = oldNode.pos;

			if (r & 1) {
				newPos = -newPos;
				if (newPos < 0) {
					newPos += static_cast<int32_t>(oldLength);
				}
			}

			newPos += static_cast<int32_t>(oldLength) * r;
			if (newPos >= static_cast<int32_t>(newLength)) {
				break; // stop part-way through the final (partial) repeat
			}

			int32_t newValue = oldNode.value;
			bool newInterpolated = oldNode.interpolated;

			// If reversing, the node's characteristics change (value from its left neighbour, interp from its right).
			if (r & 1) {
				if (!oldNode.interpolated) {
					int32_t iOldToLeft = iOld - 1;
					if (iOldToLeft < 0) {
						iOldToLeft += numNodesBefore;
					}
					newValue = nodes[iOldToLeft].value;
				}

				int32_t iOldToRight = iOld + 1;
				if (iOldToRight >= numNodesBefore) {
					iOldToRight = 0;
				}
				newInterpolated = nodes[iOldToRight].interpolated;
			}

			int32_t iNew = iNewWithinRepeat + numNodesBefore * r;
			ParamNode& newNode = nodes[iNew];
			newNode.pos = newPos;
			newNode.value = newValue;
			newNode.interpolated = newInterpolated;

			highestNodeIndex = iNew;
		}
	}

	int32_t newNumNodes = highestNodeIndex + 1;
	if (static_cast<int32_t>(nodes.size()) > newNumNodes) {
		nodes.erase(nodes.begin() + newNumNodes, nodes.end());
	}
}

int32_t homogenizeRegion(DocAutoParam& p, int32_t startPos, int32_t length, int32_t startValue,
                         bool interpolateLeftNode, bool interpolateRightNode, int32_t effectiveLength, bool reversed,
                         bool recordingLinearly, int32_t posAtWhichClipWillCut) {
	auto& nodes = p.nodes;

	constexpr int kLeft = 0;  // REGION_EDGE_LEFT
	constexpr int kRight = 1; // REGION_EDGE_RIGHT

	int32_t edgePositions[2];
	bool anyWrap;

	if (!reversed) { // Playing forwards
		edgePositions[kLeft] = startPos;

		// First, limit the length if we're coming up to a cut-point.
		int32_t maxLength = posAtWhichClipWillCut - edgePositions[kLeft];
		if (length >= maxLength) {
			length = maxLength;
			interpolateRightNode = false;
			edgePositions[kRight] = posAtWhichClipWillCut;
			anyWrap = (edgePositions[kRight] >= effectiveLength);
			if (anyWrap) {
				edgePositions[kRight] = 0; // no node allowed right at the loop length point
			}
		}
		// Otherwise there could be a loop-point: wrap the region end around (don't limit length).
		else {
			edgePositions[kRight] = edgePositions[kLeft] + length;
			anyWrap = (edgePositions[kRight] >= effectiveLength);
			if (anyWrap) {
				// If linearly recording it's not really a loop (the Clip extends) — don't wrap past loopLength.
				if (recordingLinearly) {
					length = effectiveLength - edgePositions[kLeft];
					edgePositions[kRight] = 0;
				}
				else {
					edgePositions[kRight] -= effectiveLength;
				}
			}
		}
	}
	else { // Playing reversed
		edgePositions[kRight] = startPos;
		edgePositions[kLeft] = edgePositions[kRight] - length;

		// First, limit the length if we're coming up to a cut-point.
		if (edgePositions[kLeft] < posAtWhichClipWillCut) {
			edgePositions[kLeft] = posAtWhichClipWillCut;
			length = edgePositions[kRight] - edgePositions[kLeft];
			interpolateLeftNode = false;
			anyWrap = false;
		}
		// Otherwise there could be a loop-point: wrap the region end around.
		else {
			anyWrap = (edgePositions[kLeft] < 0);
			if (anyWrap) {
				edgePositions[kLeft] += effectiveLength;
			}
		}
	}

	if (anyWrap) { // temporarily order the edges ascending for the dual search
		std::swap(edgePositions[0], edgePositions[1]);
	}

	int32_t edgeIndexes[2];
	edgeIndexes[0] = firstAtOrAfter(nodes, edgePositions[0]);
	edgeIndexes[1] = firstAtOrAfter(nodes, edgePositions[1]); // == searchDual's second search from index[0]

	if (anyWrap) { // swap edge positions and indexes back into LEFT/RIGHT order
		std::swap(edgeIndexes[0], edgeIndexes[1]);
		std::swap(edgePositions[0], edgePositions[1]);
	}

	// Which edges already have a node exactly at their position (the engine's edgeNodes[] non-null test,
	// captured up-front). We keep indices rather than the engine's raw pointers: inserts below can reallocate
	// std::vector, so a pointer form would dangle — the engine only gets away with pointers because its
	// container rarely reallocates. Indices (which the engine already maintains for its re-fetches) are the
	// equivalent robust translation.
	bool hasEdge[2] = {false, false};
	for (int i = 0; i < 2; i++) {
		if (edgeIndexes[i] < static_cast<int32_t>(nodes.size()) && nodes[edgeIndexes[i]].pos == edgePositions[i]) {
			hasEdge[i] = true;
		}
	}

	const int lateEdge = static_cast<int>(!reversed); // the region edge reached last in playback order
	int32_t valueAtLateEdge;
	// If reversed and the late-edge node isn't interpolated we'd need the further-left node's value, so fall
	// through to the normal lookup.
	if (hasEdge[lateEdge] && !(reversed && !nodes[edgeIndexes[lateEdge]].interpolated)) {
		valueAtLateEdge = nodes[edgeIndexes[lateEdge]].value;
	}
	else {
		valueAtLateEdge = getValueAtPos(p, static_cast<uint32_t>(edgePositions[lateEdge]), effectiveLength, reversed);
	}

	// Sort out the rightmost (region-end) node.
	if (!hasEdge[kRight]) {
		// If there's a redundant further-left node we can grab and repurpose...
		if (edgeIndexes[kRight] && (anyWrap || edgeIndexes[kRight] > edgeIndexes[kLeft] + 1)) {
			edgeIndexes[kRight]--;
		}
		// Otherwise, insert one.
		else {
			(void)nodes.insertAt(edgeIndexes[kRight]);
			edgeIndexes[kLeft] += static_cast<int32_t>(anyWrap);
		}
		nodes[edgeIndexes[kRight]].pos = edgePositions[kRight];
	}
	nodes[edgeIndexes[kRight]].value = reversed ? startValue : valueAtLateEdge;
	nodes[edgeIndexes[kRight]].interpolated = interpolateRightNode;

	// And sort out the leftmost (region-start) node.
	if (!hasEdge[kLeft]) {
		// If there's a further-right node we can just update its pos (nothing to insert)...
		if (edgeIndexes[kRight] != edgeIndexes[kLeft] && edgeIndexes[kLeft] < static_cast<int32_t>(nodes.size())) {
			// Cool, we can just update the "pos" of that node (done below).
		}
		// Otherwise, insert one.
		else {
			(void)nodes.insertAt(edgeIndexes[kLeft]);
			edgeIndexes[kRight] += static_cast<int32_t>(!anyWrap);
		}
		nodes[edgeIndexes[kLeft]].pos = edgePositions[kLeft];
	}
	nodes[edgeIndexes[kLeft]].value = reversed ? valueAtLateEdge : startValue;
	nodes[edgeIndexes[kLeft]].interpolated = interpolateLeftNode;

	// Delete the redundant nodes strictly inside the region (all of them if no wrap; the before-wrap bit if wrap).
	int32_t indexToDeleteFrom = edgeIndexes[kLeft] + 1;
	int32_t indexToDeleteTo = anyWrap ? static_cast<int32_t>(nodes.size()) : edgeIndexes[kRight];
	if (indexToDeleteTo > indexToDeleteFrom) {
		nodes.erase(nodes.begin() + indexToDeleteFrom, nodes.begin() + indexToDeleteTo);
	}

	// And the nodes after the wrap point, if the region wrapped.
	if (anyWrap && edgeIndexes[kRight]) {
		nodes.erase(nodes.begin(), nodes.begin() + edgeIndexes[kRight]);
		edgeIndexes[kLeft] -= edgeIndexes[kRight];
	}

	return edgeIndexes[kLeft];
}

void deleteTime(DocAutoParam& p, int32_t startPos, int32_t lengthToDelete, int32_t loopLength) {
	auto& nodes = p.nodes;

	int32_t endPos = startPos + lengthToDelete;
	int32_t start = firstAtOrAfter(nodes, startPos);
	int32_t end = firstAtOrAfter(nodes, endPos);

	int32_t numToDelete = end - start;
	if (numToDelete > 0) {
		bool shouldAddNodeAtPos0 = false;
		int32_t oldValue = 0;

		// If we're chopping off the final node, we'll want a node at pos 0 (if none there) to hold its value.
		if (end >= static_cast<int32_t>(nodes.size())) {
			shouldAddNodeAtPos0 = nodes[0].pos != 0;
			if (shouldAddNodeAtPos0) {
				oldValue = getValueAtPos(p, 0, loopLength);
			}
		}
		// Otherwise, put a node at the cut-point (which becomes startPos) if the next node isn't already there —
		// repurpose the first node we were going to delete.
		else if (nodes[end].pos > endPos) {
			ParamNode& cutNode = nodes[start];
			cutNode.value = getValueAtPos(p, static_cast<uint32_t>(endPos), loopLength);
			cutNode.pos = startPos;
			cutNode.interpolated = false;
			numToDelete--; // that's one node we're keeping after all
			start++;
			// If numToDelete is now 0 the erase below is a no-op and shouldAddNodeAtPos0 is false, so falling
			// through is equivalent to the engine's `goto allDeleted`.
		}

		nodes.erase(nodes.begin() + start, nodes.begin() + start + numToDelete);

		if (shouldAddNodeAtPos0) {
			(void)nodes.insertAt(0);
			nodes[0].value = oldValue;
			nodes[0].pos = 0;
			nodes[0].interpolated = false;
			start++; // we shifted everything along by inserting at index 0
		}
	}

	for (int32_t i = start; i < static_cast<int32_t>(nodes.size()); i++) {
		nodes[i].pos -= lengthToDelete;
	}

	// A lone remaining node isn't meaningful automation — drop it (kept from the engine "safe" behaviour).
	if (nodes.size() == 1) {
		nodes.erase(nodes.begin());
	}
}

void moveRegionHorizontally(DocAutoParam& p, int32_t pos, int32_t length, int32_t offset, int32_t lengthBeforeLoop) {
	auto& nodes = p.nodes;

	if (nodes.empty()) {
		return;
	}

	if (length == lengthBeforeLoop) {
		shiftHorizontally(p, offset, lengthBeforeLoop);
		return;
	}

	int32_t endPos = pos + length;
	if (endPos > lengthBeforeLoop) { // Wrap
		endPos -= lengthBeforeLoop;
		int32_t resultingIndexes[2];
		resultingIndexes[0] = firstAtOrAfter(nodes, endPos);
		resultingIndexes[1] = firstAtOrAfter(nodes, pos);

		if (resultingIndexes[0] == resultingIndexes[1]) {
			shiftHorizontally(p, offset, lengthBeforeLoop);
			return;
		}

		if (offset == 1) { // Moving right
			// If anything after wrap, and the rightmost such node collides with endPos, delete it.
			if (resultingIndexes[0]) {
				int32_t rightMostIndex = resultingIndexes[0] - 1;
				if (nodes[rightMostIndex].pos >= endPos - 1) {
					nodes.erase(nodes.begin() + rightMostIndex);
					resultingIndexes[0]--;
					resultingIndexes[1]--;
				}
			}

			// And if the last before-wrap node needs to move right and wrap around...
			if (resultingIndexes[1] < static_cast<int32_t>(nodes.size())) {
				int32_t indexBeforeWrap = static_cast<int32_t>(nodes.size()) - 1;
				if (nodes[indexBeforeWrap].pos >= lengthBeforeLoop - 1) {
					ParamNode tempNode = nodes[indexBeforeWrap];
					nodes.erase(nodes.begin() + indexBeforeWrap);
					(void)nodes.insertAt(0);
					nodes[0] = tempNode;
					nodes[0].pos = -1; // it'll get incremented below
					resultingIndexes[0]++;
					resultingIndexes[1]++;
				}
			}

			for (int32_t i = 0; i < resultingIndexes[0]; i++) { // after wrap
				nodes[i].pos++;
			}
			for (int32_t i = resultingIndexes[1]; i < static_cast<int32_t>(nodes.size()); i++) { // before wrap
				nodes[i].pos++;
			}
		}
		else { // Moving left
			// If anything after the wrap sits at pos 0, wrap it around to the end.
			if (resultingIndexes[0]) {
				if (nodes[0].pos == 0) {
					ParamNode tempNode = nodes[0];
					int32_t rightMostIndex = static_cast<int32_t>(nodes.size()) - 1;
					nodes.erase(nodes.begin());
					(void)nodes.insertAt(rightMostIndex);
					nodes[rightMostIndex] = tempNode;
					nodes[rightMostIndex].pos = lengthBeforeLoop; // it'll get decremented below
					resultingIndexes[0]--;
					resultingIndexes[1]--;
				}
			}

			// And if our left edge is going to eat into the node just before it, delete that node.
			if (resultingIndexes[1] && resultingIndexes[1] < static_cast<int32_t>(nodes.size())) {
				int32_t prevNodeIndex = resultingIndexes[1] - 1;
				if (nodes[prevNodeIndex].pos >= pos - 1) {
					nodes.erase(nodes.begin() + prevNodeIndex);
					resultingIndexes[1]--;
				}
			}

			// The decrement loops live INSIDE the move-left branch (unlike the pre-fix engine, which left them
			// outside the offset if/else so they also cancelled the move-right increments — see the
			// moveRegionHorizontally bug fix). Move-right increments above; move-left decrements here.
			for (int32_t i = 0; i < resultingIndexes[0]; i++) { // after wrap
				nodes[i].pos--;
			}
			for (int32_t i = resultingIndexes[1]; i < static_cast<int32_t>(nodes.size()); i++) { // before wrap
				nodes[i].pos--;
			}
		}
	}
	else { // No wrap
		int32_t resultingIndexes[2];
		resultingIndexes[0] = firstAtOrAfter(nodes, pos);
		resultingIndexes[1] = firstAtOrAfter(nodes, endPos);

		if (!resultingIndexes[0] && resultingIndexes[1] == static_cast<int32_t>(nodes.size())) {
			shiftHorizontally(p, offset, lengthBeforeLoop);
			return;
		}

		if (resultingIndexes[1] != resultingIndexes[0]) {
			if (offset == 1) { // Moving right — nothing can wrap (it slams into the region's right wall)
				int32_t rightMostIndex = resultingIndexes[1] - 1;
				if (nodes[rightMostIndex].pos >= endPos - 1) {
					nodes.erase(nodes.begin() + rightMostIndex);
					resultingIndexes[1]--;
				}
				for (int32_t i = resultingIndexes[0]; i < resultingIndexes[1]; i++) {
					nodes[i].pos++;
				}
			}
			else { // Moving left
				// If there's a node just to the left we'll eat into, delete it.
				if (resultingIndexes[0] > 0) {
					int32_t prevNodeIndex = resultingIndexes[0] - 1;
					if (nodes[prevNodeIndex].pos >= pos - 1) {
						nodes.erase(nodes.begin() + prevNodeIndex);
						resultingIndexes[0]--;
						resultingIndexes[1]--;
					}
				}
				// Or if there's no space to the left, wrap the leftmost node around to the end.
				else {
					if (nodes[resultingIndexes[0]].pos == 0) {
						int32_t lastNodeIndex = static_cast<int32_t>(nodes.size()) - 1;
						if (nodes[lastNodeIndex].pos == lengthBeforeLoop - 1) {
							nodes[lastNodeIndex] = nodes[resultingIndexes[0]]; // reuse the node already there
						}
						else {
							ParamNode tempNode = nodes[resultingIndexes[0]];
							nodes.erase(nodes.begin());
							(void)nodes.insertAt(lastNodeIndex);
							nodes[lastNodeIndex] = tempNode;
						}
						nodes[lastNodeIndex].pos = lengthBeforeLoop - 1;
						resultingIndexes[1]--; // one less node to move below
					}
					// (else: leftmost node has room to go left — nothing to do)
				}

				for (int32_t i = resultingIndexes[0]; i < resultingIndexes[1]; i++) {
					nodes[i].pos--;
				}
			}
		}
	}
}

void deleteNodesWithinRegion(DocAutoParam& p, int32_t pos, int32_t length, int32_t effectiveLength) {
	auto& nodes = p.nodes;
	if (nodes.empty()) {
		return;
	}

	if (length >= effectiveLength) { // whole loop — clear all automation
		nodes.clear();
		p.current_value = 0;
		return;
	}

	int32_t t0 = pos;
	int32_t t1 = pos + length;
	bool wrapping = (t1 >= effectiveLength);
	if (wrapping) {
		t0 = t1 - effectiveLength;
		t1 = pos;
	}

	int32_t i0 = firstAtOrAfter(nodes, t0);
	int32_t i1 = firstAtOrAfter(nodes, t1);

	if (wrapping) {
		// Tail (before-wrap) first, then front (after-wrap): erasing the front shifts later indices, so a
		// front-first order would invalidate i1 (the engine's original bug). The ranges are disjoint.
		nodes.erase(nodes.begin() + i1, nodes.end());
		nodes.erase(nodes.begin(), nodes.begin() + i0);
	}
	else {
		nodes.erase(nodes.begin() + i0, nodes.begin() + i1);
	}

	if (nodes.empty()) {
		p.current_value = 0; // MPE safety — the engine zeroes currentValue when no automation remains
	}
}

void transposeCCValuesToChannelPressure(DocAutoParam& p) {
	for (ParamNode& node : p.nodes) {
		node.value = (node.value >> 1) + (1 << 30);
	}
	p.current_value = (p.current_value >> 1) + (1 << 30);
}

void flattenReverse(DocAutoParam& p, int32_t reverseDirectionWithLength) {
	auto& nodes = p.nodes;
	int32_t numNodes = static_cast<int32_t>(nodes.size());
	if (!reverseDirectionWithLength || !numNodes) {
		return;
	}

	// Move the owned nodes out, then rebuild them reversed into a fresh buffer.
	ParamNodeVector oldNodes = std::move(nodes);
	nodes.clear();
	(void)nodes.insertAt(0, numNodes);

	const ParamNode& rightmostNode = oldNodes[numNodes - 1];
	int32_t oldNodeToLeftValue = rightmostNode.value;

	const ParamNode& leftmostNode = oldNodes[0];
	bool anythingAtZero = !leftmostNode.pos;

	for (int32_t iOld = 0; iOld < numNodes; iOld++) {
		int32_t iNew = -iOld - static_cast<int32_t>(!anythingAtZero);
		if (iNew < 0) {
			iNew += numNodes;
		}

		const ParamNode& oldNode = oldNodes[iOld];
		ParamNode& newNode = nodes[iNew];

		int32_t iOldToRight = (iOld + 1 == numNodes) ? 0 : iOld + 1;
		const ParamNode& oldNodeToRight = oldNodes[iOldToRight];

		int32_t newPos = -oldNode.pos;
		if (newPos < 0) {
			newPos += reverseDirectionWithLength;
		}

		newNode.pos = newPos;
		newNode.value = oldNode.interpolated ? oldNode.value : oldNodeToLeftValue;
		newNode.interpolated = oldNodeToRight.interpolated;

		oldNodeToLeftValue = oldNode.value;
	}
	// oldNodes frees its deep copy on destruction.
}

void trimToLength(DocAutoParam& p, int32_t newLength, int32_t loopLength) {
	auto& nodes = p.nodes;

	// If no nodes, nothing to do.
	if (nodes.empty()) {
		return;
	}

	// If the final node is already within the new length, nothing to do.
	if (nodes[nodes.size() - 1].pos < newLength) {
		return;
	}

	// To ensure that the effective value at pos 0 remains the same even after later nodes are dropped (e.g.
	// under looping wraparound, where the now-deleted tail was contributing to that interpolation), we might
	// need to add a new, non-interpolating node there — but only if there isn't already one right at pos 0.
	bool needNewNodeAt0 = (nodes[0].pos != 0);
	int32_t oldValueAt0 = 0;
	if (needNewNodeAt0) {
		oldValueAt0 = getValueAtPos(p, 0, loopLength);
	}

	int32_t newNumNodes = firstAtOrAfter(nodes, newLength);

	// If at least 2 nodes would remain, do the basic trim (drop everything at/after newLength) and patch
	// pos 0 back in if that value would otherwise have shifted.
	if (newNumNodes >= 2) {
		deleteNodesBeyond(nodes, newLength);

		if (needNewNodeAt0) {
			(void)nodes.insertAt(0);
			nodes[0].pos = 0;
			nodes[0].value = oldValueAt0;
			nodes[0].interpolated = false;
		}
	}
	// Otherwise fewer than 2 nodes would remain — not meaningful automation, so drop it all.
	else {
		nodes.clear();
	}
}

void appendParam(DocAutoParam& dest, const DocAutoParam& src, int32_t oldLength, int32_t reverseThisRepeatWithLength,
                 bool pingpongingGenerally) {
	auto& destNodes = dest.nodes;
	const auto& srcNodes = src.nodes;

	int32_t numToInsert = static_cast<int32_t>(srcNodes.size());
	if (!numToInsert) {
		return;
	}

	// When recording session to arranger, dest may occasionally end up with nodes beyond the Clip's length.
	// These need to be removed now.
	const ParamNode& firstSrcNode = srcNodes[0];
	deleteNodesBeyond(destNodes, oldLength + firstSrcNode.pos);

	const ParamNode& nodeAfterWrap = srcNodes[0];
	bool nothingAtZero = nodeAfterWrap.pos != 0;

	// We may have to create a new node at pos 0 (of the new repeat) to represent the fact that a pingpong would
	// suddenly occur in the middle of an interpolating bit between nodes. Hopefully the note at actual pos 0 got
	// created back when generateRepeats got called initially for this, at the start of recording etc.
	if (pingpongingGenerally && nothingAtZero && nodeAfterWrap.interpolated) {

		// This block is a quick simple alternative to calling getValueAtPos(), which would also require timeline
		// context and check for a bunch of unnecessary stuff.
		const ParamNode& nodeBeforeWrap = srcNodes[numToInsert - 1];
		int64_t valueDistance = static_cast<int64_t>(nodeAfterWrap.value) - static_cast<int64_t>(nodeBeforeWrap.value);
		int32_t ticksSinceLeftNode = oldLength - nodeBeforeWrap.pos;
		int32_t ticksBetweenNodes = ticksSinceLeftNode + nodeAfterWrap.pos;
		int32_t valueAtZero =
		    static_cast<int32_t>(nodeBeforeWrap.value + (valueDistance * ticksSinceLeftNode / ticksBetweenNodes));

		int32_t newZeroNodeI = static_cast<int32_t>(destNodes.size());
		(void)destNodes.insertAt(newZeroNodeI);

		ParamNode& zeroNode = destNodes[newZeroNodeI];
		zeroNode.pos = oldLength;
		zeroNode.value = valueAtZero;
		zeroNode.interpolated = true;

		// nothingAtZero is left as-is: unlike generateRepeats, the node just added lives in dest's array, not
		// src's — it doesn't change how we read src below.
	}

	int32_t oldNumNodes = static_cast<int32_t>(destNodes.size());
	(void)destNodes.insertAt(oldNumNodes, numToInsert);

	if (reverseThisRepeatWithLength) {

		for (int32_t iNewWithinRepeat = 0; iNewWithinRepeat < numToInsert; iNewWithinRepeat++) {
			int32_t iOld = -iNewWithinRepeat - static_cast<int32_t>(nothingAtZero);
			if (iOld < 0) {
				iOld += numToInsert;
			}

			const ParamNode& oldNode = srcNodes[iOld];
			int32_t newPos = oldNode.pos;

			newPos = -newPos;
			if (newPos < 0) {
				newPos += reverseThisRepeatWithLength;
			}

			newPos += oldLength;

			int32_t newValue = oldNode.value;
			bool newInterpolated = oldNode.interpolated;

			if (!oldNode.interpolated) {
				int32_t iOldToLeft = iOld - 1;
				if (iOldToLeft < 0) {
					iOldToLeft += numToInsert;
				}
				const ParamNode& oldNodeToLeft = srcNodes[iOldToLeft];
				newValue = oldNodeToLeft.value;
			}

			int32_t iOldToRight = iOld + 1;
			if (iOldToRight >= numToInsert) {
				iOldToRight = 0;
			}
			const ParamNode& oldNodeToRight = srcNodes[iOldToRight];
			newInterpolated = oldNodeToRight.interpolated;

			int32_t iNew = iNewWithinRepeat + oldNumNodes;
			ParamNode& newNode = destNodes[iNew];

			newNode.pos = newPos;
			newNode.value = newValue;
			newNode.interpolated = newInterpolated;
		}
	}

	else {

		for (int32_t i = 0; i < numToInsert; i++) {
			const ParamNode& oldNode = srcNodes[i];
			ParamNode& newNode = destNodes[oldNumNodes + i];
			newNode.pos = oldNode.pos + oldLength;
			newNode.interpolated = oldNode.interpolated;
			newNode.value = oldNode.value;
		}
	}
}

void nudgeNonInterpolatingNodesAtPos(DocAutoParam& p, int32_t pos, int32_t offset, int32_t lengthBeforeLoop) {
	auto& nodes = p.nodes;

	int32_t nodeI = firstAtOrAfter(nodes, pos);
	if (nodeI >= static_cast<int32_t>(nodes.size()) || nodes[nodeI].pos != pos) {
		return; // no node exactly at pos
	}
	if (nodes[nodeI].interpolated) {
		return;
	}

	int32_t newNodePos = pos + offset;

	// Shared handling for both wrap directions (the engine's "doWrap:" goto target): the node at `eraseIndex`
	// has wrapped past a loop edge to `destPos` and may collide with whatever already sits at `neighborIndex`.
	auto wrapNode = [&nodes](int32_t eraseIndex, int32_t neighborIndex, int32_t destPos) {
		// There should never be just one node here — matches the engine's FREEZE_WITH_ERROR("E335") guard.
		// The kernel just leaves the array untouched instead of freezing (see risks).
		if (nodes.size() == 1) {
			return;
		}

		int32_t ourValue = nodes[eraseIndex].value; // grab this before deleting stuff

		nodes.erase(nodes.begin() + eraseIndex);

		// If the node we're nudging toward would get collided with, just take it over...
		if (nodes[neighborIndex].pos == destPos) {
			// ...unless that would leave only one node total — then there's nothing left to automate.
			if (nodes.size() == 1) {
				nodes.clear();
			}
			else {
				nodes[neighborIndex].value = ourValue;
				nodes[neighborIndex].interpolated = false;
			}
		}
		// Otherwise, create a new node at the destination.
		else {
			int32_t insertI = static_cast<int32_t>(nodes.size());
			(void)nodes.insertAt(insertI); // can't fail: we just erased one
			nodes[insertI].pos = destPos;
			nodes[insertI].value = ourValue;
			nodes[insertI].interpolated = false;
		}
	};

	if (newNodePos < 0) { // Wrapped left
		newNodePos += lengthBeforeLoop;
		// Subtract 2 instead of the normal 1: we're about to delete one element before using this index.
		wrapNode(nodeI, static_cast<int32_t>(nodes.size()) - 2, newNodePos);
	}
	else if (newNodePos >= lengthBeforeLoop) { // Wrapped right
		newNodePos -= lengthBeforeLoop;
		wrapNode(nodeI, 0, newNodePos);
	}
	else { // No wrap
		nodes[nodeI].pos = newNodePos;

		int32_t nextNodeI = nodeI + offset;
		if (nextNodeI >= 0 && nextNodeI < static_cast<int32_t>(nodes.size())) {
			if (nodes[nextNodeI].pos == newNodePos) {
				nodes.erase(nodes.begin() + nextNodeI);
			}
		}
	}
}

std::optional<bool> setValueForRegion(DocAutoParam& p, uint32_t pos, uint32_t length, int32_t value,
                                      int32_t effectiveLength, bool interpolateLeftNode, bool interpolateRightNode,
                                      bool recordingLinearly, bool clockActive, int32_t livePos,
                                      bool currentlyPlayingReversed) {
	auto& nodes = p.nodes;

	// The user can't edit beyond the length of the loop/Clip/TimelineCounter they're editing within.
	if (pos >= static_cast<uint32_t>(effectiveLength)) {
		return std::nullopt;
	}

	// Special case: the region covers the whole loop/Clip/TimelineCounter — collapse to a flat value.
	// (Faithful port of deleteAutomation's node mutation; Action snapshotting is engine wiring, not ported.)
	if (length == static_cast<uint32_t>(effectiveLength)) {
		nodes.clear();
		p.current_value = value;
		return false; // automationChanged stays false on this path, matching the engine
	}

	// Normal case: homogenize the region to a single flat/ramped segment.
	int32_t firstI = homogenizeRegion(p, static_cast<int32_t>(pos), static_cast<int32_t>(length), value,
	                                  interpolateLeftNode, interpolateRightNode, effectiveLength,
	                                  /*reversed=*/false, recordingLinearly);
	if (firstI == -1) { // OOM in the engine; unreachable with this container's homogenizeRegion, kept for parity
		return std::nullopt;
	}

	// If the transport isn't running right now, the edit takes effect on current_value immediately.
	if (!clockActive) {
		p.current_value = value;
		return true;
	}

	// Otherwise, it only takes effect immediately if the playhead is inside the edited region right now.
	// (resetInterpolationIncrement on that path is engine wiring — interpolation-cursor state, not ported.)
	int32_t mostRecentI = firstAtOrAfter(nodes, livePos + static_cast<int32_t>(!currentlyPlayingReversed)) - 1;
	if (mostRecentI == -1) {
		mostRecentI = static_cast<int32_t>(nodes.size()) - 1;
	}
	if (mostRecentI == firstI) {
		p.current_value = value;
	}
	return true;
}

void copyRegion(const DocAutoParam& p, int32_t startPos, int32_t endPos, int32_t loopLength, bool isPatchCable,
                CopiedAutomation& out) {
	const auto& nodes = p.nodes;

	int32_t startI = firstAtOrAfter(nodes, startPos);
	int32_t endI = firstAtOrAfter(nodes, endPos);

	out.width = endPos - startPos;
	out.nodes.clear();

	int32_t numNodes = endI - startI;

	bool insertingExtraNodeAtStart = false;
	if (numNodes > 0 && nodes[startI].pos != startPos) {
		insertingExtraNodeAtStart = true;
		numNodes++;
	}

	if (numNodes <= 0) {
		return;
	}

	out.nodes.reserve(numNodes);

	if (insertingExtraNodeAtStart) {
		ParamNode newNode;
		newNode.pos = 0;
		newNode.value = getValueAtPos(p, static_cast<uint32_t>(startPos), loopLength);
		newNode.interpolated = false;

		if (isPatchCable) {
			newNode.value = lshiftAndSaturateBy1(newNode.value);
		}

		out.nodes.push_back(newNode);
	}

	// Fill in all the Nodes' details.
	for (int32_t readingNodeI = startI; static_cast<int32_t>(out.nodes.size()) < numNodes; readingNodeI++) {
		ParamNode newNode = nodes[readingNodeI];
		newNode.pos -= startPos;

		if (isPatchCable) {
			newNode.value = lshiftAndSaturateBy1(newNode.value);
		}

		out.nodes.push_back(newNode);
	}
}

void pasteRegion(DocAutoParam& p, int32_t startPos, int32_t endPos, float scaleFactor, int32_t loopLength,
                 bool isPatchCable, const CopiedAutomation& copied) {
	if (copied.nodes.empty()) {
		return; // engine FREEZE_WITH_ERRORs on this precondition in debug builds; the kernel just no-ops.
	}

	auto& nodes = p.nodes;

	int32_t effectiveLength = loopLength;
	int32_t wrappedEndPos = endPos % effectiveLength;
	bool overwrittingEntireRegion = startPos == 0 && endPos >= effectiveLength;

	// Save the current value at the start and end of the region we're pasting in to, before we start messing
	// with it.
	int32_t startValue = getValueAtPos(p, static_cast<uint32_t>(startPos), effectiveLength);
	int32_t endValue = getValueAtPos(p, static_cast<uint32_t>(wrappedEndPos), effectiveLength);

	// Clear out any nodes that already exist in the region we're pasting in to.
	int32_t iDeleteBegin = firstAtOrAfter(nodes, startPos);
	int32_t iDeleteEnd = firstAtOrAfter(nodes, endPos);
	int32_t numToDelete = iDeleteEnd - iDeleteBegin;
	if (numToDelete > 0) {
		nodes.erase(nodes.begin() + iDeleteBegin, nodes.begin() + iDeleteBegin + numToDelete);
	}

	// Make sure that automation data before the paste region starts and after it ends aligns with what we're
	// about to put there.
	if (!overwrittingEntireRegion) {
		// The copied parameter automation always has a node at t=0; if that doesn't match the existing content
		// we need to insert a node right before it to preserve the automation before the paste.
		if (startValue != copied.nodes[0].value) {
			int32_t ticksBeforeStart = 1;
			int32_t resetPos = (ticksBeforeStart > startPos) ? (effectiveLength + startPos) - ticksBeforeStart
			                                                 : (startPos - ticksBeforeStart);
			int32_t resetI = firstAtOrAfter(nodes, resetPos);
			ParamNode* resetNode = nodes.tryGet(resetI);

			if (resetNode == nullptr || resetNode->pos != resetPos) {
				bool previousNodeInterpolated = resetNode != nullptr ? resetNode->interpolated : true;
				(void)nodes.insertAt(resetI);

				resetNode = &nodes[resetI];
				resetNode->pos = resetPos;
				resetNode->interpolated = previousNodeInterpolated;
			}

			resetNode->value = startValue;
		}

		// If the final node does not match the value at the end position, we need to insert a node.
		const ParamNode& finalNode = copied.nodes.back();
		if (endValue != finalNode.value) {
			int32_t resetPos = 0;
			// The copied automation has a node that will overlap, need to insert 1-past-the-end.
			if (finalNode.pos == endPos - startPos) {
				// wrappedEndPos+1 is always within the sequence length since a sequence length of 0 or 1 is
				// probably super broken in other places.
				resetPos = wrappedEndPos + 1;
			}
			else {
				resetPos = wrappedEndPos;
			}

			int32_t resetI = firstAtOrAfter(nodes, resetPos);
			ParamNode* resetNode = nodes.tryGet(resetI);

			if (!resetNode || resetNode->pos != resetPos) {
				(void)nodes.insertAt(resetI);

				resetNode = &nodes[resetI];
				resetNode->pos = resetPos;
				resetNode->interpolated = finalNode.interpolated;
			}

			resetNode->value = endValue;
		}
	}

	// Ok now paste the stuff.
	int32_t minPos = 0;
	int32_t maxPos = std::min(endPos, effectiveLength);

	for (const ParamNode& nodeSource : copied.nodes) {
		int32_t newPos = startPos + static_cast<int32_t>(roundf(static_cast<float>(nodeSource.pos) * scaleFactor));

		// Make sure that with dividing and rounding, we're not overlapping the previous node - or past the end
		// of the screen / Clip.
		if (newPos < minPos || newPos >= maxPos) {
			continue;
		}

		int32_t nodeDestI = nodes.insertSorted(newPos).value_or(-1);
		ParamNode* nodeDest = nodes.tryGet(nodeDestI);
		if (!nodeDest) {
			return;
		}

		nodeDest->value = nodeSource.value;
		nodeDest->interpolated = nodeSource.interpolated;

		if (isPatchCable) {
			nodeDest->value >>= 1;
		}

		minPos = newPos + 1;
	}

	nodes.testSequentiality("E440");
}

/* -------------------------------------------------------------------------------------------------------------
 * Supporting additions (NOT to be appended standalone — merge into the existing anonymous-namespace helper
 * block and includes; see "risks" for exact placement):
 *
 * 1. Add `#include <cmath>` to the top of param_edit.cpp (for roundf), alongside the existing <algorithm>.
 *
 * 2. Add this helper into the existing anonymous namespace (next to addSaturating/firstAtOrAfter):
 *
 * // Port of lshiftAndSaturate<1>(val): clamp val to the signed 31-bit range ([-2^30, 2^30-1]) then shift left 1
 * // — the patch-cable convention for converting a patch-cable-range node value into the general parameter range.
 * // param_edit.cpp is self-contained and can't include util/functions.h (which has no ARM-vs-host split anyway
 * // for this op — signed_saturate<31> is the same plain clamp on both), so this is a local re-derivation.
 * int32_t lshiftAndSaturateBy1(int32_t val) {
 * 	constexpr int32_t hi = 1073741823;  // 2^30 - 1
 * 	constexpr int32_t lo = -1073741824; // -2^30
 * 	int32_t clamped = val > hi ? hi : (val < lo ? lo : val);
 * 	return clamped << 1;
 * }
 * ------------------------------------------------------------------------------------------------------------- */

void stealNodes(DocAutoParam& p, int32_t pos, int32_t regionLength, int32_t loopLength, StolenNodes* outStolen) {
	auto& nodes = p.nodes;

	int32_t stopAt = pos + regionLength;
	int32_t durationAfterWrap = stopAt - loopLength;

	int32_t resultingIndex0 = firstAtOrAfter(nodes, pos);
	int32_t resultingIndex1 = firstAtOrAfter(nodes, stopAt); // == searchDual's second search from index0

	int32_t numNodesToStealBeforeWrap = resultingIndex1 - resultingIndex0;

	int32_t numNodesToStealAfterWrap = 0;
	if (durationAfterWrap > 0) {
		numNodesToStealAfterWrap = firstAtOrAfter(nodes, durationAfterWrap);
	}

	if (outStolen) {
		int32_t numNodesToStealTotal = numNodesToStealBeforeWrap + numNodesToStealAfterWrap;
		if (numNodesToStealTotal > 0) {
			outStolen->resize(numNodesToStealTotal);

			int32_t sourceI = resultingIndex0;
			int32_t stopAtI = resultingIndex1;
			int32_t destI = 0;
			for (;;) {
				while (sourceI < stopAtI) {
					ParamNode& dest = (*outStolen)[destI];
					dest = nodes[sourceI];
					dest.pos -= pos;
					if (dest.pos < 0) {
						dest.pos += loopLength;
					}
					sourceI++;
					destI++;
				}
				if (stopAtI == numNodesToStealAfterWrap) {
					break;
				}
				sourceI = 0;
				stopAtI = numNodesToStealAfterWrap;
			}
		}
	}

	// Now actually delete the source nodes. Before-wrap first: that erase only ever touches indices at/after
	// resultingIndex0, so it can't disturb the after-wrap front's indices (0..numNodesToStealAfterWrap), which
	// is why the engine's order here (unlike deleteNodesWithinRegion's) was already safe.
	if (numNodesToStealBeforeWrap > 0) {
		nodes.erase(nodes.begin() + resultingIndex0, nodes.begin() + resultingIndex0 + numNodesToStealBeforeWrap);
	}
	if (numNodesToStealAfterWrap > 0) {
		nodes.erase(nodes.begin(), nodes.begin() + numNodesToStealAfterWrap);
	}
}

void insertStolenNodes(DocAutoParam& p, int32_t pos, int32_t regionLength, int32_t loopLength,
                       const StolenNodes& stolenNodes) {
	// First, clear the destination area (an un-recorded steal, matching the engine's own call into itself).
	stealNodes(p, pos, regionLength, loopLength);

	auto& nodes = p.nodes;
	// This is really inefficient. (Carried over from the engine's own comment.)
	for (const ParamNode& stolenNode : stolenNodes) {
		if (stolenNode.pos >= regionLength) {
			break; // Our destination region is shorter than that of the stolen nodes.
		}
		int32_t destPos = stolenNode.pos + pos;
		if (destPos >= loopLength) {
			destPos -= loopLength;
		}

		int32_t destI = nodes.insertSorted(destPos).value_or(-1);
		if (destI == -1) {
			break;
		}
		nodes[destI] = stolenNode;
		nodes[destI].pos = destPos;
	}
}

void shiftParamVolumeByDB(DocAutoParam& p, float offset) {
	p.current_value = shiftVolumeByDB(p.current_value, offset);
	for (ParamNode& node : p.nodes) {
		node.value = shiftVolumeByDB(node.value, offset);
	}
}

} // namespace deluge::project
