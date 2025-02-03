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

#include "util/container/array/ordered_resizeable_array_with_multi_word_key.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
OrderedResizeableArrayWithMultiWordKey::OrderedResizeableArrayWithMultiWordKey(int32_t newElementSize,
                                                                               int32_t newNumWordsInKey)
    : OrderedResizeableArrayWith32bitKey(newElementSize, 16, 15), numWordsInKey(newNumWordsInKey) {
}

OrderedResizeableArrayWithMultiWordKey::OrderedResizeableArrayWithMultiWordKey(int32_t newElementSize,
                                                                               int32_t newNumWordsInKey,
                                                                               int32_t newMaxNumEmptySpacesToKeep,
                                                                               int32_t newNumExtraSpacesToAllocate)
    : OrderedResizeableArrayWith32bitKey(newElementSize, newMaxNumEmptySpacesToKeep, newNumExtraSpacesToAllocate),
      numWordsInKey(newNumWordsInKey) {
}

int32_t OrderedResizeableArrayWithMultiWordKey::searchMultiWord(uint32_t* __restrict__ keyWords, int32_t comparison,
                                                                int32_t rangeBegin, int32_t rangeEnd) {

	if (rangeEnd == -1) {
		rangeEnd = numElements;
	}

	const uint32_t* const finalKeyWord = keyWords + (numWordsInKey - 1);

	while (rangeBegin != rangeEnd) {
		int32_t proposedIndex = (rangeBegin + rangeEnd) >> 1;

		uint32_t* __restrict__ wordsHere = (uint32_t*)getElementAddress(proposedIndex);
		uint32_t* __restrict__ keyWord = keyWords;

		while (true) {
			int32_t difference = *wordsHere - *keyWord;
			if (difference > 0) {
				break;
			}
			if (difference < 0) {
				goto searchFurtherRight;
			}
			if (keyWord == finalKeyWord) {
				break;
			}
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
int32_t OrderedResizeableArrayWithMultiWordKey::searchMultiWordExact(uint32_t* __restrict__ keyWords,
                                                                     int32_t* getIndexToInsertAt, int32_t rangeBegin) {
	int32_t i = searchMultiWord(keyWords, GREATER_OR_EQUAL, rangeBegin);
	if (i < numElements) {
		uint32_t* __restrict__ wordsHere = (uint32_t*)getElementAddress(i);
		for (int32_t w = 0; w < numWordsInKey; w++) {
			if (wordsHere[w] != keyWords[w]) {
				goto notFound;
			}
		}

		return i;
	}

notFound:
	if (getIndexToInsertAt) {
		*getIndexToInsertAt = i;
	}
	return -1;
}

// Returns index created, or -1 if error
int32_t OrderedResizeableArrayWithMultiWordKey::insertAtKeyMultiWord(uint32_t* __restrict__ keyWords,
                                                                     int32_t rangeBegin, int32_t rangeEnd) {
	int32_t i = searchMultiWord(keyWords, GREATER_OR_EQUAL, 0, rangeEnd);

	Error error = insertAtIndex(i);
	if (error != Error::NONE) {
		return -1;
	}

	uint32_t* __restrict__ wordsHere = (uint32_t*)getElementAddress(i);

	for (int32_t w = 0; w < numWordsInKey; w++) {
		wordsHere[w] = keyWords[w];
	}

	return i;
}

// Returns whether it did actually do a delete
bool OrderedResizeableArrayWithMultiWordKey::deleteAtKeyMultiWord(uint32_t* __restrict__ keyWords) {
	int32_t i = searchMultiWordExact(keyWords);
	if (i != -1) {
		deleteAtIndex(i);
		return true;
	}
	else {
		D_PRINTLN("couldn't find key to delete %s / %s", keyWords[0], keyWords[1]);
		return false;
	}
}

void OrderedResizeableArrayWithMultiWordKey::testSequentiality(char const* errorCode) {
#if ENABLE_SEQUENTIALITY_TESTS
	for (int32_t i = 1; i < getNumElements(); i++) {
		uint32_t* __restrict__ wordsHere = (uint32_t*)getElementAddress(i);
		uint32_t* __restrict__ lastWords = (uint32_t*)getElementAddress(i - 1);
		for (int j = 0; j < numWordsInKey; j++) {
			int32_t difference = wordsHere[j] - lastWords[j];
			if (difference < 0) {
				FREEZE_WITH_ERROR(errorCode);
			}
			if (difference == 0) {
				if (j == numWordsInKey - 1) {
					// if we got here it's a duplicate key
					FREEZE_WITH_ERROR(errorCode);
				}
			}
			else {
				break;
			}
		}
	}
#endif
}
