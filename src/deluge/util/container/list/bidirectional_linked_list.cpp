/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "util/container/list/bidirectional_linked_list.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "storage/cluster/cluster.h"

BidirectionalLinkedList::BidirectionalLinkedList() {
	first = &endNode;
	endNode.prevPointer = &first;
}

void BidirectionalLinkedList::addToEnd(BidirectionalLinkedListNode* node) {
	node->prevPointer = endNode.prevPointer;
	*endNode.prevPointer = node;
	endNode.prevPointer = &node->next;
	node->next = &endNode;

	node->list = this;

#if ALPHA_OR_BETA_VERSION
	// Have deactivated this, because some of the lists will have up to 2000 elements in them on boot, and searching
	// through all of these causes voice culls
	// test();
#endif
}

void BidirectionalLinkedList::addToStart(BidirectionalLinkedListNode* node) {
	node->prevPointer = &first->next;
	node->next = first->next;
	first->next = node;

	node->list = this;

#if ALPHA_OR_BETA_VERSION
	// Have deactivated this, because some of the lists will have up to 2000 elements in them on boot, and searching
	// through all of these causes voice culls
	// test();
#endif
}

BidirectionalLinkedListNode* BidirectionalLinkedList::getFirst() {
	if (first == &endNode) {
		return NULL;
	}
	else {
		return first;
	}
}

int32_t BidirectionalLinkedList::getNum() {
	BidirectionalLinkedListNode* node = first;
	int32_t count = 0;
	while (node != &endNode) {
		node = node->next;
		count++;
	}
	return count;
}

BidirectionalLinkedListNode* BidirectionalLinkedList::getNext(BidirectionalLinkedListNode* node) {
	BidirectionalLinkedListNode* next = node->next;
	if (next == &endNode) {
		return NULL;
	}
	else {
		return next;
	}
}

void BidirectionalLinkedList::test() {

	if (first == NULL) {
		FREEZE_WITH_ERROR("E005");
	}

	int32_t count = 0;

	BidirectionalLinkedListNode* thisNode = first;
	BidirectionalLinkedListNode** prevPointer = &first;
	while (true) {
		if (thisNode->prevPointer != prevPointer) {
			FREEZE_WITH_ERROR("E006");
		}

		if (thisNode == &endNode) {
			break; // We got to the end
		}

		// Check the node references its list correctly
		if (thisNode->list != this) {
			FREEZE_WITH_ERROR("E007");
		}

		count++;

		// Check we're not spiralling around forever
		if (count > 2048) {
			FREEZE_WITH_ERROR("E008");
		}

		prevPointer = &thisNode->next;
		thisNode = thisNode->next;
	}

	D_PRINTLN("list size:  %d", count);
}

BidirectionalLinkedListNode::BidirectionalLinkedListNode() {
	list = NULL;
}

BidirectionalLinkedListNode::~BidirectionalLinkedListNode() {
	remove();
}

// It's intended that you may call this function even if you're not sure whether the node is in a list or not
void BidirectionalLinkedListNode::remove() {
	if (!list) {
		return;
	}
	*prevPointer = next;
	next->prevPointer = prevPointer;

	BidirectionalLinkedList* oldList = list;
	list = NULL;

#if ALPHA_OR_BETA_VERSION
	// Have deactivated this, because some of the lists will have up to 2000 elements in them on boot, and searching
	// through all of these causes voice culls
	// oldList->test();
#endif
}

void BidirectionalLinkedListNode::insertOtherNodeBefore(BidirectionalLinkedListNode* otherNode) {
	if constexpr (ALPHA_OR_BETA_VERSION || kCurrentFirmwareVersion <= FIRMWARE_4P0P0) {
		// If we're not already in a list, that means we also don't have a valid prevPointer, so everything's about to
		// break. This happened!
		if (!list) {
			FREEZE_WITH_ERROR("E443");
		}
	}
	otherNode->list = list;

	otherNode->next = this;
	otherNode->prevPointer = prevPointer;

	*prevPointer = otherNode;
	prevPointer = &otherNode->next;
}

// Ok this is a little bit dangerous - you'd better make damn sure list is set before calling this!
bool BidirectionalLinkedListNode::isLast() {
	if constexpr (ALPHA_OR_BETA_VERSION || kCurrentFirmwareVersion <= FIRMWARE_4P0P0) {
		if (!list) {
			FREEZE_WITH_ERROR("E444");
		}
	}
	return (next == &list->endNode);
}