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

} // namespace deluge::project
