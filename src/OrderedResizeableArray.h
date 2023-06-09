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

#ifndef ORDEREDRESIZEABLEARRAY_H_
#define ORDEREDRESIZEABLEARRAY_H_

#include "ResizeableArray.h"

class OrderedResizeableArray : public ResizeableArray {
public:
	OrderedResizeableArray(int newElementSize, int keyNumBits, int newKeyOffset = 0,
	                       int newMaxNumEmptySpacesToKeep = 16, int newNumExtraSpacesToAllocate = 15);
	int search(int32_t key, int comparison, int rangeBegin, int rangeEnd);
	inline int search(int32_t key, int comparison, int rangeBegin = 0) {
		return search(key, comparison, rangeBegin, numElements);
	}

	int searchExact(int32_t key);
	int insertAtKey(int32_t key, bool isDefinitelyLast = false);
	void deleteAtKey(int32_t key);

	void test();
	void testSequentiality(char const* errorCode);
	void testDuplicates();

	inline int32_t getKeyAtIndex(int i) { return getKeyAtMemoryLocation(getElementAddress(i)); }

	inline void setKeyAtIndex(int32_t key, int i) { setKeyAtMemoryLocation(key, getElementAddress(i)); }

protected:
	inline int32_t getKeyAtMemoryLocation(void* address) {
		int32_t keyBig = *(uint32_t*)((uint32_t)address + keyOffset) << keyShiftAmount;
		return keyBig >> keyShiftAmount; // We use shifting instead of a mask so negative numbers get treated directly.
	}

	inline void setKeyAtMemoryLocation(int32_t key, void* address) {
		uint32_t offsetAddress = (uint32_t)address + keyOffset;
		uint32_t prevContents = *(uint32_t*)offsetAddress;
		*(uint32_t*)offsetAddress = (key & keyMask) | (prevContents & ~keyMask);
	}

private:
	const uint32_t keyMask;
	const int keyOffset;
	const int keyShiftAmount;
};

// The purpose of this is not so much that special functionality is required for 32-bit keys, but that some further child classes inherit from this, which require that the key be 32-bit.
class OrderedResizeableArrayWith32bitKey : public OrderedResizeableArray {
public:
	OrderedResizeableArrayWith32bitKey(int newElementSize, int newMaxNumEmptySpacesToKeep = 16,
	                                   int newNumExtraSpacesToAllocate = 15);
	void shiftHorizontal(int32_t amount, int32_t effectiveLength);
	void searchDual(int32_t const* __restrict__ searchTerms, int* __restrict__ resultingIndexes);
	void searchMultiple(int32_t* __restrict__ searchTerms, int numSearchTerms, int rangeEnd = -1);
	bool generateRepeats(int32_t wrapPoint, int32_t endPos);
	void testSearchMultiple();

	inline int32_t getKeyAtIndex(int i) { return getKeyAtMemoryLocation(getElementAddress(i)); }

	inline void setKeyAtIndex(int32_t key, int i) { setKeyAtMemoryLocation(key, getElementAddress(i)); }

protected:
	// These shadow - they don't override. Might give a tiny bit of efficiency
	inline int32_t getKeyAtMemoryLocation(void* address) { return *(int32_t*)address; }

	// Shadows - doesn't override
	inline void setKeyAtMemoryLocation(int32_t key, void* address) { *(int32_t*)address = key; }
};

#endif /* ORDEREDRESIZEABLEARRAY_H_ */
