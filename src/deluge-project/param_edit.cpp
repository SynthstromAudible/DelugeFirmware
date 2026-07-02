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

int32_t firstAtOrAfter(const std::vector<ParamNode>& nodes, int32_t key) {
	auto it =
	    std::lower_bound(nodes.begin(), nodes.end(), key, [](const ParamNode& n, int32_t k) { return n.pos < k; });
	return static_cast<int32_t>(it - nodes.begin());
}

// Port of AutoParam::deleteNodesBeyondPos: drop every node at or after `pos`.
void deleteNodesBeyond(std::vector<ParamNode>& nodes, int32_t pos) {
	nodes.erase(nodes.begin() + firstAtOrAfter(nodes, pos), nodes.end());
}

// Non-pingpong tiling — port of OrderedPosVector::generateRepeats. Tiles the contents (all before wrapPoint)
// out to endPos.
void tileRepeats(std::vector<ParamNode>& nodes, int32_t wrapPoint, int32_t endPos) {
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

		nodes.insert(nodes.begin(), ParamNode{}); // invalidates the references above (no longer used)
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
		nodes.insert(nodes.begin() + numNodesBefore, numToInsert, ParamNode{});
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
			nodes.insert(nodes.begin() + edgeIndexes[kRight], ParamNode{});
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
			nodes.insert(nodes.begin() + edgeIndexes[kLeft], ParamNode{});
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
			nodes.insert(nodes.begin(), ParamNode{});
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
					nodes.insert(nodes.begin(), ParamNode{});
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
					nodes.insert(nodes.begin() + rightMostIndex, ParamNode{});
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
							nodes.insert(nodes.begin() + lastNodeIndex, ParamNode{});
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

} // namespace deluge::project
