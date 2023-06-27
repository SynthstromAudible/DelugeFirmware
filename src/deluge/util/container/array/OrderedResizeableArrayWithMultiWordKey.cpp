/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include <OrderedResizeableArrayWithMultiWordKey.h>
#include "uart.h"

OrderedResizeableArrayWithMultiWordKey::OrderedResizeableArrayWithMultiWordKey(int newElementSize, int newNumWordsInKey)
    : OrderedResizeableArrayWith32bitKey(newElementSize, 16, 15), numWordsInKey(newNumWordsInKey) {
}

int OrderedResizeableArrayWithMultiWordKey::searchMultiWord(uint32_t* __restrict__ keyWords, int comparison,
                                                            int rangeBegin, int rangeEnd) {

	if (rangeEnd == -1) rangeEnd = numElements;

	const uint32_t* const finalKeyWord = keyWords + (numWordsInKey - 1);

	while (rangeBegin != rangeEnd) {
		int proposedIndex = (rangeBegin + rangeEnd) >> 1;

		uint32_t* __restrict__ wordsHere = (uint32_t*)getElementAddress(proposedIndex);
		uint32_t* __restrict__ keyWord = keyWords;

		while (true) {
			int difference = *wordsHere - *keyWord;
			if (difference > 0) break;
			if (difference < 0) goto searchFurtherRight;
			if (keyWord == finalKeyWord) break;
			keyWord++;
			wordsHere++;
		}

		// If we exited or got to the end of the loop, search further left
		rangeEnd = proposedIndex;
		continue;

searchFurtherRight:
		rangeBegin = proposedIndex + 1;
	}

	return rangeBegin + comparison;
}

// Returns -1 if not found
int OrderedResizeableArrayWithMultiWordKey::searchMultiWordExact(uint32_t* __restrict__ keyWords,
                                                                 int* getIndexToInsertAt, int rangeBegin) {
	int i = searchMultiWord(keyWords, GREATER_OR_EQUAL, rangeBegin);
	if (i < numElements) {
		uint32_t* __restrict__ wordsHere = (uint32_t*)getElementAddress(i);
		for (int w = 0; w < numWordsInKey; w++) {
			if (wordsHere[w] != keyWords[w]) goto notFound;
		}

		return i;
	}

notFound:
	if (getIndexToInsertAt) *getIndexToInsertAt = i;
	return -1;
}

// Returns index created, or -1 if error
int OrderedResizeableArrayWithMultiWordKey::insertAtKeyMultiWord(uint32_t* __restrict__ keyWords, int rangeBegin,
                                                                 int rangeEnd) {
	int i = searchMultiWord(keyWords, GREATER_OR_EQUAL, 0, rangeEnd);

	int error = insertAtIndex(i);
	if (error) return -1;

	uint32_t* __restrict__ wordsHere = (uint32_t*)getElementAddress(i);

	for (int w = 0; w < numWordsInKey; w++) {
		wordsHere[w] = keyWords[w];
	}

	return i;
}

// Returns whether it did actually do a delete
bool OrderedResizeableArrayWithMultiWordKey::deleteAtKeyMultiWord(uint32_t* __restrict__ keyWords) {
	int i = searchMultiWordExact(keyWords);
	if (i != -1) {
		deleteAtIndex(i);
		return true;
	}
	else {
		Uart::println("couldn't find key to delete");
		Uart::println(keyWords[0]);
		Uart::println(keyWords[1]);
		return false;
	}
}
