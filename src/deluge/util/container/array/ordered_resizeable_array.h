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

#include "util/container/array/resizeable_array.h"

class OrderedResizeableArray : public ResizeableArray {
public:
	OrderedResizeableArray(int32_t newElementSize, int32_t keyNumBits, int32_t newKeyOffset = 0,
	                       int32_t newMaxNumEmptySpacesToKeep = 16, int32_t newNumExtraSpacesToAllocate = 15);
	int32_t search(int32_t key, int32_t comparison, int32_t rangeBegin, int32_t rangeEnd);
	inline int32_t search(int32_t key, int32_t comparison, int32_t rangeBegin = 0) {
		return search(key, comparison, rangeBegin, numElements);
	}

	int32_t searchExact(int32_t key);
	int32_t insertAtKey(int32_t key, bool isDefinitelyLast = false);
	void deleteAtKey(int32_t key);

#if TEST_VECTOR
	// TODO: move to unit tests
	void test();
#endif
#if TEST_VECTOR_DUPLICATES
	// TODO: move to unit tests
	void testDuplicates();
#endif
	/// \brief test that the keys in this array are sorted in ascending order.
	///
	/// This is used in assert-like fashion, so not for unit tests.
	void testSequentiality(char const* errorCode);

	inline int32_t getKeyAtIndex(int32_t i) { return getKeyAtMemoryLocation(getElementAddress(i)); }

	inline void setKeyAtIndex(int32_t key, int32_t i) { setKeyAtMemoryLocation(key, getElementAddress(i)); }

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
	const int32_t keyOffset;
	const int32_t keyShiftAmount;
};

// The purpose of this is not so much that special functionality is required for 32-bit keys, but that some further
// child classes inherit from this, which require that the key be 32-bit.
class OrderedResizeableArrayWith32bitKey : public OrderedResizeableArray {
public:
	explicit OrderedResizeableArrayWith32bitKey(int32_t newElementSize, int32_t newMaxNumEmptySpacesToKeep = 16,
	                                            int32_t newNumExtraSpacesToAllocate = 15);
	void shiftHorizontal(int32_t amount, int32_t effectiveLength);
	void searchDual(int32_t const* __restrict__ searchTerms, int32_t* __restrict__ resultingIndexes);
	void searchMultiple(int32_t* __restrict__ searchTerms, int32_t numSearchTerms, int32_t rangeEnd = -1);
	bool generateRepeats(int32_t wrapPoint, int32_t endPos);
#if TEST_VECTOR_SEARCH_MULTIPLE
	// TODO: move to unit tests
	void testSearchMultiple();
#endif

	inline int32_t getKeyAtIndex(int32_t i) { return getKeyAtMemoryLocation(getElementAddress(i)); }

	inline void setKeyAtIndex(int32_t key, int32_t i) { setKeyAtMemoryLocation(key, getElementAddress(i)); }

protected:
	// These shadow - they don't override. Might give a tiny bit of efficiency
	inline int32_t getKeyAtMemoryLocation(void* address) { return *(int32_t*)address; }

	// Shadows - doesn't override
	inline void setKeyAtMemoryLocation(int32_t key, void* address) { *(int32_t*)address = key; }
};
