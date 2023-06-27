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

#ifndef RESIZEABLEARRAY_H_
#define RESIZEABLEARRAY_H_

#include "r_typedefs.h"
#include "definitions.h"

#define GREATER_OR_EQUAL 0
#define LESS (-1)

#define RESIZEABLE_ARRAY_DO_LOCKS (ALPHA_OR_BETA_VERSION)

class ResizeableArray {
public:
	ResizeableArray(int newElementSize, int newMaxNumEmptySpacesToKeep = 16, int newNumExtrarSpacesToAllocate = 15);
	~ResizeableArray();
	void init();
	bool cloneFrom(ResizeableArray* other);
	void empty();
	void swapStateWith(ResizeableArray* other);
	void deleteAtIndex(int i, int numToDelete = 1, bool mayShortenMemoryAfter = true);
	bool ensureEnoughSpaceAllocated(int numAdditionalElementsNeeded);
	int insertAtIndex(int i, int numToInsert = 1, void* thingNotToStealFrom = NULL);
	void swapElements(int i1, int i2);
	void repositionElement(int iFrom, int iTo);
	int beenCloned();
	void setMemory(void* newMemory, int newMemorySize);
	void setStaticMemory(void* newMemory, int newMemorySize);

	void moveElementsLeft(int oldStartIndex, int oldStopIndex, int distance);
	void moveElementsRight(int oldStartIndex, int oldStopIndex, int distance);

	inline void* getElementAddress(int index) {
		int absoluteIndex = index + memoryStart;
		if (absoluteIndex >= memorySize) absoluteIndex -= memorySize;
		return (char* __restrict__)memory + (absoluteIndex * elementSize);
	}

	inline int getNumElements() { return numElements; }

	unsigned int elementSize;
	bool emptyingShouldFreeMemory;
	uint32_t staticMemoryAllocationSize;

protected:
	void* memory;
	int numElements;
	int memorySize; // In elements, not bytes
	int memoryStart;
#if TEST_VECTOR
	int moveCount;
#endif

#if RESIZEABLE_ARRAY_DO_LOCKS
	bool lock;
#endif

private:
	void attemptMemoryShorten();
	bool attemptMemoryExpansion(int minNumToExtend, int idealNumToExtend, bool mayExtendAllocation,
	                            void* thingNotToStealFrom);
	void copyToNewMemory(void* newMemory, uint32_t destinationIndex, void* source, uint32_t numElementsToCopy,
	                     uint32_t newMemorySize, uint32_t newMemoryStartIndex);
	int copyElementsFromOldMemory(void* otherMemory, int otherMemorySize, int otherMemoryStart);

	void moveElementsRightNoWrap(int oldStartIndex, int oldStopIndex, int distance);
	void moveElementsLeftNoWrap(int oldStartIndex, int oldStopIndex, int distance);

	void* memoryAllocationStart; // This might be slightly to the left of "memory"

	const int maxNumEmptySpacesToKeep;  // Can go down to 0
	const int numExtraSpacesToAllocate; // Can go down to 0
};

#endif /* RESIZEABLEARRAY_H_ */
