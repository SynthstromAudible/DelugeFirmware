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

#pragma once

#include <cstdint>

class BidirectionalLinkedList;

class BidirectionalLinkedListNode {
public:
	BidirectionalLinkedListNode();
	virtual ~BidirectionalLinkedListNode();

	void remove();
	void insertOtherNodeBefore(BidirectionalLinkedListNode* otherNode);
	bool isLast();

	BidirectionalLinkedListNode* next;         // Only valid if list is not NULL, contains jibberish otherwise.
	BidirectionalLinkedListNode** prevPointer; // Only valid if list is not NULL, contains jibberish otherwise.
	BidirectionalLinkedList* list;
};

class BidirectionalLinkedList {
public:
	BidirectionalLinkedList();
	void addToEnd(BidirectionalLinkedListNode* node);
	BidirectionalLinkedListNode* getFirst();
	BidirectionalLinkedListNode* getNext(BidirectionalLinkedListNode* node);
	void test();
	int32_t getNum();

	BidirectionalLinkedListNode endNode;
	BidirectionalLinkedListNode* first;
	void addToStart(BidirectionalLinkedListNode* node);
};
