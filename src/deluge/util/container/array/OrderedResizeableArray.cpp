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

#include <OrderedResizeableArray.h>
#include "definitions.h"
#include "functions.h"
#include "sio_char.h"
#include "numericdriver.h"
#include "GeneralMemoryAllocator.h"
#include "mtu_all_cpus.h"
#include "uart.h"

extern "C" {
#include "uart_all_cpus.h"
}

OrderedResizeableArray::OrderedResizeableArray(int newElementSize, int keyNumBits, int newKeyOffset,
                                               int newMaxNumEmptySpacesToKeep, int newNumExtraSpacesToAllocate)
    : ResizeableArray(newElementSize, newMaxNumEmptySpacesToKeep, newNumExtraSpacesToAllocate),
      keyMask(0xFFFFFFFF >> (32 - keyNumBits)), keyOffset(newKeyOffset), keyShiftAmount(32 - keyNumBits) {
}

// With duplicate keys, this will work correctly, returning the leftmost matching (or greater) one if doing GREATER_OR_EQUAL, or the rightmost less one if doing LESS.
// This behaviour for duplicates can be tested with TEST_VECTOR_DUPLICATES / testDuplicates().
int OrderedResizeableArray::search(int32_t searchKey, int comparison, int rangeBegin, int rangeEnd) {

	while (rangeBegin != rangeEnd) {
		int proposedIndex = (rangeBegin + rangeEnd) >> 1;

		int32_t keyHere = getKeyAtIndex(proposedIndex);

		if (keyHere < searchKey) rangeBegin = proposedIndex + 1;
		else rangeEnd = proposedIndex;
	}

	return rangeBegin + comparison;
}

// Returns -1 if not found
int OrderedResizeableArray::searchExact(int32_t key) {
	int i = search(key, GREATER_OR_EQUAL);
	if (i < numElements) {
		int32_t keyHere = getKeyAtIndex(i);
		if (keyHere == key) return i;
	}
	return -1;
}

struct SearchRecord {
	int defaultRangeEnd;
	int lastsUntilSearchTerm;
};

// Like searchMultiple(), but much less complex as we know it's only doing 2 search terms.
void OrderedResizeableArrayWith32bitKey::searchDual(int32_t const* __restrict__ searchTerms,
                                                    int* __restrict__ resultingIndexes) {

	int rangeBegin = 0;
	int rangeEnd = numElements;
	int rangeEndForSecondTerm = numElements;

	while (rangeBegin != rangeEnd) {
		int proposedIndex = (rangeBegin + rangeEnd) >> 1;

		int32_t keyHere = getKeyAtIndex(proposedIndex);

		if (keyHere < searchTerms[0]) rangeBegin = proposedIndex + 1;
		else {
			rangeEnd = proposedIndex;

			// And we can also narrow down our next search, if that's going to be left of our proposedIndex too.
			if (keyHere >= searchTerms[1]) rangeEndForSecondTerm = proposedIndex;
		}
	}

	resultingIndexes[0] = rangeBegin;
	resultingIndexes[1] = search(searchTerms[1], GREATER_OR_EQUAL, rangeBegin, rangeEndForSecondTerm);
}

// Returns results as if GREATER_OR_EQUAL had been supplied to search. To turn this into LESS, subtract 1
// Results are written back into the searchTerms array
void OrderedResizeableArrayWith32bitKey::searchMultiple(int32_t* __restrict__ searchTerms, int numSearchTerms,
                                                        int rangeEnd) {

	if (rangeEnd == -1) rangeEnd = numElements;

	int const maxNumSearchRecords = FILENAME_BUFFER_SIZE / sizeof(SearchRecord);
	SearchRecord* const __restrict__ searchRecords = (SearchRecord*)miscStringBuffer;

	int rangeBegin = 0;

	searchRecords[0].defaultRangeEnd = rangeEnd;
	searchRecords[0].lastsUntilSearchTerm = numSearchTerms;

	//int maxSearchRecord = 0;

	int currentSearchRecord = 0;

	// Go through each search term. rangeBegin carries over between search terms
	for (int t = 0; t < numSearchTerms; t++) {

		if (t >= searchRecords[currentSearchRecord].lastsUntilSearchTerm) currentSearchRecord--;

		int rangeEnd = searchRecords[currentSearchRecord].defaultRangeEnd;
		int searchTermsRangeEnd = searchRecords[currentSearchRecord].lastsUntilSearchTerm;

		// Solve for this search term, putting aside valuable data as we narrow down the range of items we're investigating.
		// And we're also already making use of any data we previously put aside - our rangeBegin and rangeEnd are probably already fairly tight.
		while (rangeBegin != rangeEnd) {
			int rangeSize = rangeEnd - rangeBegin;
			int proposedIndex = rangeBegin + (rangeSize >> 1);

			int examiningElementPos = getKeyAtIndex(proposedIndex);

			// If element pos greater than search term, tighten rangeEnd...
			if (examiningElementPos >= searchTerms[t]) {

				rangeEnd = proposedIndex;

				// But we also want to make a note for any further search terms for which their "defaultRangeEnd" could also be tightened to the same point.
				// How many more search terms to the right can we do this for? We'll figure that out quickly by (ironically) searching through the search terms
				// to find the first one >= the element pos we're currently looking at.
				// Thankfully, we've already tightened the "searchTermsRangeEnd" for this too!
				int searchTermsRangeBegin = t + 1;
				while (searchTermsRangeBegin != searchTermsRangeEnd) {
					int searchTermsRangeSize = searchTermsRangeEnd - searchTermsRangeBegin;
					int proposedSearchTerm = searchTermsRangeBegin + (searchTermsRangeSize >> 1);
					if (searchTerms[proposedSearchTerm] >= examiningElementPos) {
						searchTermsRangeEnd = proposedSearchTerm;
					}
					else {
						searchTermsRangeBegin = proposedSearchTerm + 1;
					}
				}

				// If this tightened defaultRangeEnd is going to apply beyond just this search term here, make a note by putting it on the stack.
				if (searchTermsRangeEnd > t + 1) {

					// Well, we only need a new stack entry if this new run ends before the previous one did. Otherwise, we'll just overwrite that, so no need to
					if (searchTermsRangeEnd < searchRecords[currentSearchRecord].lastsUntilSearchTerm) {

						// But, if the stack was going to overflow, the beauty is, we can just not put our new item on the stack at all. Everything will still work fine,
						// though not quite as efficiently as it could have
						if (currentSearchRecord == maxNumSearchRecords - 1) continue;

						currentSearchRecord++;
						//if (currentSearchRecord > maxSearchRecord) maxSearchRecord = currentSearchRecord;
					}

					// Update that (probably brand new) stack record
					searchRecords[currentSearchRecord].defaultRangeEnd = proposedIndex;
					searchRecords[currentSearchRecord].lastsUntilSearchTerm = searchTermsRangeEnd;
				}
			}

			// Otherwise, tighten rangeBegin
			else {
				rangeBegin = proposedIndex + 1;
			}
		}

		// Cool, we've now solved for this search term
		searchTerms[t] = rangeEnd;
	}

	//Uart::print("maxSearchRecord: ");
	//Uart::println(maxSearchRecord);
}

bool OrderedResizeableArrayWith32bitKey::generateRepeats(int32_t wrapPoint, int32_t endPos) {
	if (!memory) return true;

	int numCompleteRepeats = (uint32_t)endPos / (uint32_t)wrapPoint;

	int32_t endPosWithinFirstRepeat = endPos - numCompleteRepeats * wrapPoint;
	int iEndPosWithinFirstRepeat = search(endPosWithinFirstRepeat, GREATER_OR_EQUAL);

	int oldNum = search(
	    wrapPoint,
	    GREATER_OR_EQUAL); // Do this rather than just copying numElements - this is better because it ensures we ignore / chop off any elements >= wrapPoint
	int newNum = oldNum * numCompleteRepeats + iEndPosWithinFirstRepeat;

	if (!ensureEnoughSpaceAllocated(newNum - numElements)) return false;

	numElements = newNum;

	for (int r = 1; r <= numCompleteRepeats; r++) { // For each repeat
		for (int i = 0; i < oldNum; i++) {
			if (r == numCompleteRepeats && i >= iEndPosWithinFirstRepeat) break;
			memcpy(getElementAddress(i + oldNum * r), getElementAddress(i), elementSize);

			int32_t newPos = getKeyAtIndex(i) + wrapPoint * r;

			setKeyAtIndex(newPos, i + oldNum * r);
		}
	}

	return true;
}

// Returns index created, or -1 if error
int OrderedResizeableArray::insertAtKey(int32_t key, bool isDefinitelyLast) {

	int i;

	if (isDefinitelyLast) i = numElements;
	else i = search(key, GREATER_OR_EQUAL);

	int error = insertAtIndex(i);
	if (error) return -1;

	setKeyAtIndex(key, i);

	return i;
}

void OrderedResizeableArray::deleteAtKey(int32_t key) {
	int i = searchExact(key);
	if (i != -1) {
		deleteAtIndex(i);
	}
}

void OrderedResizeableArray::testSequentiality(char const* errorCode) {
	if (!ALPHA_OR_BETA_VERSION) return;

	int32_t lastKey = -2147483648;
	for (int i = 0; i < getNumElements(); i++) {
		int32_t key = getKeyAtIndex(i);
		if (key <= lastKey) numericDriver.freezeWithError(errorCode);

		lastKey = key;
	}
}

#define TEST_SEARCH_MULTIPLE_NUM_ITEMS 50000
#define TEST_SEARCH_MULTIPLE_NUM_SEARCH_TERMS 10000

void OrderedResizeableArrayWith32bitKey::testSearchMultiple() {
	insertAtIndex(0, TEST_SEARCH_MULTIPLE_NUM_ITEMS);

	int32_t* searchPos = (int32_t*)generalMemoryAllocator.alloc(TEST_SEARCH_MULTIPLE_NUM_SEARCH_TERMS * sizeof(int32_t),
	                                                            NULL, false, false);
	int32_t* resultingIndexes = (int32_t*)generalMemoryAllocator.alloc(
	    TEST_SEARCH_MULTIPLE_NUM_SEARCH_TERMS * sizeof(int32_t), NULL, false, false);

	while (true) {

		int valueHere = 0;
		for (int i = 0; i < TEST_SEARCH_MULTIPLE_NUM_ITEMS; i++) {
			setKeyAtIndex(valueHere, i);
			valueHere += (int)getRandom255() + 1;
		}

		for (int t = 0; t < TEST_SEARCH_MULTIPLE_NUM_SEARCH_TERMS; t++) {
			searchPos[t] = valueHere / TEST_SEARCH_MULTIPLE_NUM_SEARCH_TERMS * t;
			resultingIndexes[t] = searchPos[t];
		}

		uint16_t startTime = *TCNT[TIMER_SYSTEM_FAST];
		searchMultiple(resultingIndexes, TEST_SEARCH_MULTIPLE_NUM_SEARCH_TERMS);
		uint16_t endTime = *TCNT[TIMER_SYSTEM_FAST];

		uint16_t timeTaken = endTime - startTime;

		// Verify it
		int i = 0;
		for (int t = 0; t < TEST_SEARCH_MULTIPLE_NUM_SEARCH_TERMS; t++) {
			while (getKeyAtIndex(i) < searchPos[t]) {
				if (i >= resultingIndexes[t]) {
					//numericDriver.freezeWithError("FAIL");
					Uart::println("fail");
					goto thatsDone;
				}
				i++;
			}
		}

		Uart::print("search-multiple success. time taken: ");
thatsDone:
		Uart::println(timeTaken);
	}
}

#if TEST_VECTOR
#define NUM_TEST_INSERTIONS 10000
int32_t values[NUM_TEST_INSERTIONS];

char staticMemory[10000 * 50];

void OrderedResizeableArray::test() {

	//setStaticMemory(staticMemory, sizeof(staticMemory));

	while (true) {

		Uart::print("up ");

		// Insert tons of stuff
		for (int v = 0; v < NUM_TEST_INSERTIONS;) {

			if (!staticMemoryAllocationSize && getRandom255() < 3) {
				ensureEnoughSpaceAllocated(getRandom255());
			}

startAgain:
			int32_t value =
			    ((uint32_t)getRandom255() << 16) | ((uint32_t)getRandom255() << 8) | (uint32_t)getRandom255();

			// Check that value doesn't already exist
			int i = search(value, GREATER_OR_EQUAL);
			if (i < numElements) {
				if (getKeyAtIndex(i) == value) goto startAgain;
			}

			int numToInsert = 1;

			// Maybe we'll insert multiple
			if (true || getRandom255() < 16) {
				int desiredNumToInsert = getRandom255() & 15;

				if (desiredNumToInsert < 1) desiredNumToInsert = 1;
				int32_t valueNow = value;

				while (numToInsert < desiredNumToInsert) {
					valueNow++;
					if (valueNow < value) break;
					if (i < numElements && getKeyAtIndex(i) == valueNow) break;

					numToInsert++;
				}
			}

			// Make sure we don't shoot past the end of values[NUM_TEST_INSERTIONS]
			if (numToInsert > NUM_TEST_INSERTIONS - v) numToInsert = NUM_TEST_INSERTIONS - v;

			//if (numToInsert == 15) Uart::println("inserting 15");

			int error = insertAtIndex(i, numToInsert);

			if (error) {
				Uart::println("insert failed");
				while (1) {}
			}

			for (int j = 0; j < numToInsert; j++) {
				setKeyAtIndex(value + j, i + j);
				values[v] = value + j;
				v++;
			}

			//insertAtPos(value);
			//values[v] = value;
		}

		if (numElements != NUM_TEST_INSERTIONS) {
			Uart::println("wrong size");
			while (1) {}
			//empty();
		}

		Uart::println(moveCount);

		Uart::print("down ");

		moveCount = 0;

		// Delete the stuff
		for (int v = 0; v < NUM_TEST_INSERTIONS;) {

			int i = search(values[v], GREATER_OR_EQUAL);
			if (i >= numElements) {
				Uart::println("value no longer there, end");
				while (1) {}
			}
			if (getKeyAtIndex(i) != values[v]) {
				Uart::println("value no longer there, mid");
				while (1) {}
			}

			int w = v;
			int32_t value = values[v];
			int numToDelete = 1;
			int j = i;

			while (true) {
				w++;
				if (w >= NUM_TEST_INSERTIONS) break;

				value++;
				if (values[w] != value) break;

				j++;
				if (j >= numElements) {
					Uart::println("multi value no longer there, end");
					while (1) {}
				}

				if (getKeyAtIndex(j) != value) {
					Uart::println("multi value no longer there, mid");
					while (1) {}
				}

				numToDelete++;
			}

			//if (numToDelete == 15) Uart::println("deleting 15");

			deleteAtIndex(i, numToDelete);

			v += numToDelete;
		}

		if (numElements) {
			Uart::println("some elements left");
			while (1) {}
			//empty();
		}

		Uart::println(moveCount);
	}
}
#endif

#if TEST_VECTOR_DUPLICATES
#define NUM_DUPLICATES_TO_TEST 1000
void OrderedResizeableArray::testDuplicates() {
	int count = 0;
	while (1) {
		if (!(count & 31)) {
			Uart::println("testing duplicate search...");
		}
		count++;

		for (int i = 0; i < NUM_DUPLICATES_TO_TEST; i++) {
			int key = (getNoise() >> 16) & 1023;
			int num = getRandom255() % 7;
			for (int n = 0; n < num; n++) {
				insertAtKey(key);
			}
		}

		for (int j = 0; j < 1000; j++) {
			int searchKey = (getNoise() >> 16) & 1023;
			int i = search(searchKey, GREATER_OR_EQUAL);

			int keyAtSearchResult = 2147483647;

			// Ensure that the key returned looks valid
			if (i < numElements) {
				keyAtSearchResult = getKeyAtIndex(i);
				if (keyAtSearchResult < searchKey) {
					Uart::println("key too low");
					while (1)
						;
				}
				else if (keyAtSearchResult == searchKey) {
					// Key matches exactly
					//continue;
				}
			}

			// If here, we got a key higher than our search key, so check the next key to the left is lower
			if (i) {
				if (getKeyAtIndex(i - 1) >= searchKey) {
					Uart::println("invalid");
					while (1)
						;
				}
			}
		}

		empty();
	}
}
#endif

// 32-bit key
OrderedResizeableArrayWith32bitKey::OrderedResizeableArrayWith32bitKey(int newElementSize,
                                                                       int newMaxNumEmptySpacesToKeep,
                                                                       int newNumExtraSpacesToAllocate)
    : OrderedResizeableArray(newElementSize, 32, 0, newMaxNumEmptySpacesToKeep, newNumExtraSpacesToAllocate) {
}

void OrderedResizeableArrayWith32bitKey::shiftHorizontal(int32_t shiftAmount, int32_t effectiveLength) {

	if (!numElements) return;

	// Wrap the amount to the length.
	if (shiftAmount >= effectiveLength) {
		shiftAmount = (uint32_t)shiftAmount % (uint32_t)effectiveLength;
	}
	else if (shiftAmount <= -effectiveLength) {
		shiftAmount = -((uint32_t)(-shiftAmount) % (uint32_t)effectiveLength);
	}

	if (!shiftAmount) return;

	int32_t cutoffPos = -shiftAmount;
	if (cutoffPos < 0) cutoffPos += effectiveLength;

	if (shiftAmount < 0) shiftAmount += effectiveLength;

	int cutoffIndex = search(
	    cutoffPos,
	    GREATER_OR_EQUAL); // This relates to the key/position cutoff - nothing to do with the memory location wrap point!

	// Update the elements' keys (positions) - starting with those left of the cutoffIndex.
	int i = 0;
	int stopAt = cutoffIndex;
updateKeys:
	for (; i < stopAt; i++) {
		void* address = getElementAddress(i);
		int32_t oldKey = getKeyAtMemoryLocation(address);
		int32_t newKey = oldKey + shiftAmount;
		setKeyAtMemoryLocation(newKey, address);
	}

	// If that was us just doing the ones left of the cutoffIndex, go back and do the ones to the right
	if (stopAt < numElements) {
		stopAt = numElements;
		shiftAmount -= effectiveLength;
		goto updateKeys;
	}

	// If the leftmost element (in terms of key/position, *not* physical memory location!) has actually changed...
	if (cutoffIndex && cutoffIndex < numElements) {

		// If ends aren't touching...
		int memoryTooBigBy = memorySize - numElements;
		if (memoryTooBigBy) {

			// If wrap, then do the smallest amount of memory moving possible to make the ends touch
			int numElementsBeforeWrap = memorySize - memoryStart;
			if (numElementsBeforeWrap < numElements) {

				// If number of elements after wrap is less, move them right
				if ((numElementsBeforeWrap << 1) >= numElements) {
					int numElementsAfterWrap = numElements - numElementsBeforeWrap;
					void* newMemory = (char*)memory + memoryTooBigBy * elementSize;
					memmove(newMemory, memory, numElementsAfterWrap * elementSize);
					memory = newMemory;
					memoryStart -= memoryTooBigBy;
				}

				// Or, vice versa
				else {
					int newMemoryStart = memoryStart - memoryTooBigBy;
					memmove((char*)memory + newMemoryStart * elementSize, (char*)memory + memoryStart * elementSize,
					        numElementsBeforeWrap * elementSize);
					memoryStart = newMemoryStart;
				}
			}

			// Or if no wrap, just alter some parameters. (Remember, we will have introduced a wrap, which we'll set up below.)
			else {
				memory = (char*)memory + memoryStart * elementSize;
				memoryStart = 0;
			}
			memorySize = numElements;
		}

		// Finally, actually move the first-element-index along to reflect the new leftmost element
		memoryStart += cutoffIndex;
		if (memoryStart >= memorySize) { // Wrap if needed
			memoryStart -= memorySize;
		}
	}

#if ALPHA_OR_BETA_VERSION
	testSequentiality("E378");
#endif
}
