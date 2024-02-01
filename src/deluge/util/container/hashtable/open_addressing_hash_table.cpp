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

#include "util/container/hashtable/open_addressing_hash_table.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "util/functions.h"
#include <string.h>

#define SECONDARY_MEMORY_FUNCTION_NONE 0
#define SECONDARY_MEMORY_FUNCTION_BEING_INITIALIZED 1
#define SECONDARY_MEMORY_FUNCTION_BEING_REHASHED_FROM 2

OpenAddressingHashTable::OpenAddressingHashTable() {
	memory = NULL;
	numBuckets = 0;
	numElements = 0;

	secondaryMemory = NULL;
	secondaryMemoryNumBuckets = 0;
	secondaryMemoryCurrentFunction = SECONDARY_MEMORY_FUNCTION_NONE;

	initialNumBuckets = 16;
}

OpenAddressingHashTable::~OpenAddressingHashTable() {
	empty(true);
}

void OpenAddressingHashTable::empty(bool destructing) {
	if (memory) {
		delugeDealloc(memory);
	}
	if (secondaryMemory) {
		delugeDealloc(secondaryMemory);
	}

	if (!destructing) {
		memory = NULL;
		numBuckets = 0;
		numElements = 0;

		secondaryMemory = NULL;
		secondaryMemoryNumBuckets = 0;
		secondaryMemoryCurrentFunction = SECONDARY_MEMORY_FUNCTION_NONE;
	}
}

// See these pages for good hash functions
// https://stackoverflow.com/questions/664014/what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
// http://www.azillionmonkeys.com/qed/hash.html
uint32_t hash(uint32_t x) {
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = (x >> 16) ^ x;
	return x;
}

int32_t OpenAddressingHashTable::getBucketIndex(uint32_t key) {
	return hash(key) & (numBuckets - 1);
}

void* OpenAddressingHashTable::getBucketAddress(int32_t b) {
	return (void*)((uint32_t)memory + b * elementSize);
}

void* OpenAddressingHashTable::secondaryMemoryGetBucketAddress(int32_t b) {
	return (void*)((uint32_t)secondaryMemory + b * elementSize);
}

void* OpenAddressingHashTable::insert(uint32_t key, bool* onlyIfNotAlreadyPresent) {

#if ALPHA_OR_BETA_VERSION
	if (doesKeyIndicateEmptyBucket(key)) {
		FREEZE_WITH_ERROR("E330");
	}
#endif

	// If no memory, get some
	if (!memory) {
		int32_t newNumBuckets = initialNumBuckets;
		memory = GeneralMemoryAllocator::get().allocMaxSpeed(newNumBuckets * elementSize);
		if (!memory) {
			return NULL;
		}

		numBuckets = newNumBuckets;
		numElements = 0; // Should already be...

		memset(memory, 0xFF, newNumBuckets * elementSize);
	}

	// Or if reached 75% full, try getting more
	else if (numElements >= numBuckets - (numBuckets >> 2)) {
		int32_t newNumBuckets = numBuckets << 1;

		secondaryMemory = GeneralMemoryAllocator::get().allocMaxSpeed(newNumBuckets * elementSize);
		if (secondaryMemory) {

			// Initialize
			secondaryMemoryNumBuckets = newNumBuckets;
			secondaryMemoryCurrentFunction = SECONDARY_MEMORY_FUNCTION_BEING_INITIALIZED;
			memset(secondaryMemory, 0xFF, secondaryMemoryNumBuckets * elementSize);

			// Swap the memories
			void* tempMemory = memory;
			memory = secondaryMemory;
			secondaryMemory = tempMemory;

			int32_t tempNumBuckets = numBuckets;
			numBuckets = secondaryMemoryNumBuckets;
			secondaryMemoryNumBuckets = tempNumBuckets;

			// Rehash
			secondaryMemoryCurrentFunction = SECONDARY_MEMORY_FUNCTION_BEING_REHASHED_FROM;
			secondaryMemoryFunctionCurrentIteration = 0;

			while (secondaryMemoryFunctionCurrentIteration < secondaryMemoryNumBuckets) {
				void* sourceBucketAddress = secondaryMemoryGetBucketAddress(secondaryMemoryFunctionCurrentIteration);
				uint32_t keyHere = getKeyFromAddress(sourceBucketAddress);

				// If there was something in that bucket, copy it
				if (!doesKeyIndicateEmptyBucket(keyHere)) {
					int32_t destBucketIndex = getBucketIndex(keyHere);

					void* destBucketAddress;
					while (true) {
						destBucketAddress = getBucketAddress(destBucketIndex);
						uint32_t destKey = getKeyFromAddress(destBucketAddress);
						if (doesKeyIndicateEmptyBucket(destKey)) {
							break;
						}
						destBucketIndex = (destBucketIndex + 1) & (numBuckets - 1);
					}

					// Ok, we've got an empty bucket!
					memcpy(destBucketAddress, sourceBucketAddress, elementSize);
				}

				secondaryMemoryFunctionCurrentIteration++;
			}

			// Discard old stuff
			secondaryMemoryCurrentFunction = SECONDARY_MEMORY_FUNCTION_NONE;
			delugeDealloc(secondaryMemory);
			secondaryMemory = NULL;
			secondaryMemoryNumBuckets = 0;
		}
	}

	// If still can't get new memory and table completely full...
	if (numElements == numBuckets) {
		return NULL;
	}

	int32_t b = getBucketIndex(key);
	void* bucketAddress;
	while (true) {
		bucketAddress = getBucketAddress(b);
		uint32_t destKey = getKeyFromAddress(bucketAddress);
		if (onlyIfNotAlreadyPresent && destKey == key) {
			*onlyIfNotAlreadyPresent = true;
			goto justReturnAddress;
		}
		if (doesKeyIndicateEmptyBucket(destKey)) {
			break;
		}
		b = (b + 1) & (numBuckets - 1);
	}

	// Ok, we've got an empty bucket!
	setKeyAtAddress(key, bucketAddress);

	numElements++;

justReturnAddress:
	return bucketAddress;
}

void* OpenAddressingHashTable::lookup(uint32_t key) {

#if ALPHA_OR_BETA_VERSION
	if (doesKeyIndicateEmptyBucket(key)) {
		FREEZE_WITH_ERROR("E331");
	}
#endif

	if (!memory) {
		return NULL;
	}

	int32_t bInitial = getBucketIndex(key);
	int32_t b = bInitial;
	while (true) {
		void* bucketAddress = getBucketAddress(b);

		uint32_t keyHere = getKeyFromAddress(bucketAddress);

		// If reached an empty bucket, there's nothing there
		if (doesKeyIndicateEmptyBucket(keyHere)) {
			break;
		}

		// Bucket's not empty. Does it hold our key?
		if (keyHere == key) {
			return bucketAddress;
		}

		b = (b + 1) & (numBuckets - 1);

		// If we've wrapped all the way around (which could only happen if table 100% full)
		if (b == bInitial) {
			break;
		}
	}

	return NULL;
}

// Returns whether it found the element
bool OpenAddressingHashTable::remove(uint32_t key) {

#if ALPHA_OR_BETA_VERSION
	if (doesKeyIndicateEmptyBucket(key)) {
		FREEZE_WITH_ERROR("E332");
	}
#endif

	if (!memory) {
		return false;
	}

	int32_t bInitial = getBucketIndex(key);
	int32_t b = bInitial;
	void* bucketAddress;
	while (true) {
		bucketAddress = getBucketAddress(b);

		uint32_t keyHere = getKeyFromAddress(bucketAddress);

		// If reached an empty bucket, can't find our element
		if (doesKeyIndicateEmptyBucket(keyHere)) {
			return false;
		}

		// Bucket's not empty. Does it hold our key?
		if (keyHere == key) {
			break;
		}

		b = (b + 1) & (numBuckets - 1);

		// If we've wrapped all the way around (which could only happen if table 100% full)
		if (b == bInitial) {
			return false;
		}
	}

	// We found the bucket with our element.
	numElements--;

	// If we've hit zero elements, and it's worth getting rid of the memory, just do that
	if (!numElements && numBuckets > initialNumBuckets) {
		delugeDealloc(memory);
		memory = NULL;
		numBuckets = 0;
	}

	else {
		int32_t lastBucketIndexLeftEmpty = b;
		bInitial = b;

		while (true) {
			b = (b + 1) & (numBuckets - 1);

			// If we've wrapped all the way around (which could only happen if table 100% full)
			if (b == bInitial) {
				break;
			}

			void* newBucketAddress = getBucketAddress(b);

			uint32_t keyHere = getKeyFromAddress(newBucketAddress);

			// If reached an empty bucket, we're done
			if (doesKeyIndicateEmptyBucket(keyHere)) {
				break;
			}

			// Bucket contains an element. What bucket did this element ideally want to be in?
			int32_t idealBucket = getBucketIndex(keyHere);
			if (idealBucket != b) {
				bool shouldMove;
				if (lastBucketIndexLeftEmpty < b) {
					shouldMove = (idealBucket <= lastBucketIndexLeftEmpty) || (idealBucket > b);
				}
				else {
					shouldMove = (idealBucket <= lastBucketIndexLeftEmpty) && (idealBucket > b);
				}

				if (shouldMove) {
					memcpy(bucketAddress, newBucketAddress, elementSize);
					lastBucketIndexLeftEmpty = b;
					bucketAddress = newBucketAddress;
				}
			}
		}

		// Mark the last one as empty
		setKeyAtAddress(0xFFFFFFFF, bucketAddress);
	}
	return true;
}

// 32-bit key
OpenAddressingHashTableWith32bitKey::OpenAddressingHashTableWith32bitKey() {
	elementSize = sizeof(uint32_t);
}

uint32_t OpenAddressingHashTableWith32bitKey::getKeyFromAddress(void* address) {
	return *(uint32_t*)address;
}

void OpenAddressingHashTableWith32bitKey::setKeyAtAddress(uint32_t key, void* address) {
	*(uint32_t*)address = key;
}

bool OpenAddressingHashTableWith32bitKey::doesKeyIndicateEmptyBucket(uint32_t key) {
	return (key == (uint32_t)0xFFFFFFFF);
}

// 16-bit key
OpenAddressingHashTableWith16bitKey::OpenAddressingHashTableWith16bitKey() {
	elementSize = sizeof(uint16_t);
}

uint32_t OpenAddressingHashTableWith16bitKey::getKeyFromAddress(void* address) {
	return *(uint16_t*)address;
}

void OpenAddressingHashTableWith16bitKey::setKeyAtAddress(uint32_t key, void* address) {
	*(uint16_t*)address = key;
}

bool OpenAddressingHashTableWith16bitKey::doesKeyIndicateEmptyBucket(uint32_t key) {
	return (key == (uint32_t)0xFFFF);
}

// 8-bit key
OpenAddressingHashTableWith8bitKey::OpenAddressingHashTableWith8bitKey() {
	elementSize = sizeof(uint8_t);
}

uint32_t OpenAddressingHashTableWith8bitKey::getKeyFromAddress(void* address) {
	return *(uint8_t*)address;
}

void OpenAddressingHashTableWith8bitKey::setKeyAtAddress(uint32_t key, void* address) {
	*(uint8_t*)address = key;
}

bool OpenAddressingHashTableWith8bitKey::doesKeyIndicateEmptyBucket(uint32_t key) {
	return (key == (uint32_t)0xFF);
}

#define NUM_ELEMENTS_TO_ADD 64
void OpenAddressingHashTable::test() {
	uint32_t elementsAdded[NUM_ELEMENTS_TO_ADD];

	uint32_t count = 0;

	while (true) {
		count++;
		if (!(count & ((1 << 13) - 1))) {
			D_PRINTLN("still going");
		}

		int32_t numElementsAdded = 0;

		// Add a bunch of elements
		while (numElementsAdded < NUM_ELEMENTS_TO_ADD) {
			do {
				elementsAdded[numElementsAdded] = getNoise() & 0xFF;
			} while (!elementsAdded[numElementsAdded]
			         || (uint8_t)elementsAdded[numElementsAdded]
			                == 0xFF); // Don't allow 0 - we'll use that for special test. Or 0xFF, cos that means empty

			bool result = insert(elementsAdded[numElementsAdded]);
			numElementsAdded++;

			if (!result) {
				D_PRINTLN("couldn't add element");
				while (1) {
					;
				}
			}
		}

		if (numElements != NUM_ELEMENTS_TO_ADD) {
			D_PRINTLN("wrong numElements");
			while (1) {
				;
			}
		}

		// See if it'll let us remove an element that doesn't exist
		bool result = remove(0);
		if (result) {
			D_PRINTLN("reported successful removal of nonexistent element");
			while (1) {
				;
			}
		}

		for (int32_t i = 0; i < NUM_ELEMENTS_TO_ADD; i++) {
			bool result = remove(elementsAdded[i]);
			if (!result) {
				D_PRINTLN("remove failed. i ==  %d numBuckets ==  %d numElements ==  %d key ==  %d", i, numBuckets,
				          numElements, elementsAdded[i]);
				while (1) {
					;
				}
			}
		}

		if (numElements != 0) {
			D_PRINTLN("numElements didn't return to 0");
			while (1) {
				;
			}
		}

		// See if it'll let us remove an element that doesn't exist
		result = remove(0);
		if (result) {
			D_PRINTLN("reported successful removal of element when there are no elements at all");
			while (1) {
				;
			}
		}
	}
}