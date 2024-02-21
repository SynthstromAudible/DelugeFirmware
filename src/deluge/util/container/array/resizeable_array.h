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

#include "definitions_cxx.hpp"
#include <cstdint>

#define GREATER_OR_EQUAL 0
#define LESS (-1)

#define RESIZEABLE_ARRAY_DO_LOCKS (ALPHA_OR_BETA_VERSION)

class ResizeableArray {
public:
	ResizeableArray(int32_t newElementSize, int32_t newMaxNumEmptySpacesToKeep = 16,
	                int32_t newNumExtrarSpacesToAllocate = 15);
	~ResizeableArray();
	void init();
	bool cloneFrom(ResizeableArray* other);
	void empty();
	void swapStateWith(ResizeableArray* other);
	void deleteAtIndex(int32_t i, int32_t numToDelete = 1, bool mayShortenMemoryAfter = true);
	bool ensureEnoughSpaceAllocated(int32_t numAdditionalElementsNeeded);
	Error insertAtIndex(int32_t i, int32_t numToInsert = 1, void* thingNotToStealFrom = NULL);
	void swapElements(int32_t i1, int32_t i2);
	void repositionElement(int32_t iFrom, int32_t iTo);
	Error beenCloned();
	void setMemory(void* newMemory, int32_t newMemorySize);
	void setStaticMemory(void* newMemory, int32_t newMemorySize);

	void moveElementsLeft(int32_t oldStartIndex, int32_t oldStopIndex, int32_t distance);
	void moveElementsRight(int32_t oldStartIndex, int32_t oldStopIndex, int32_t distance);

	[[gnu::always_inline]] inline void* getElementAddress(int32_t index) {
		int32_t absoluteIndex = index + memoryStart;
		if (absoluteIndex >= memorySize)
			absoluteIndex -= memorySize;
		return (char* __restrict__)memory + (absoluteIndex * elementSize);
	}

	[[gnu::always_inline]] inline int32_t getNumElements() { return numElements; }

	uint32_t elementSize;
	bool emptyingShouldFreeMemory;
	uint32_t staticMemoryAllocationSize;

protected:
	void* memory;
	int32_t numElements;
	int32_t memorySize; // In elements, not bytes
	int32_t memoryStart;
#if TEST_VECTOR
	int32_t moveCount;
#endif

#if RESIZEABLE_ARRAY_DO_LOCKS
	bool lock;
	void freezeOnLock();
	void exitLock();
#endif

private:
	void attemptMemoryShorten();
	bool attemptMemoryExpansion(int32_t minNumToExtend, int32_t idealNumToExtend, bool mayExtendAllocation,
	                            void* thingNotToStealFrom);
	void copyToNewMemory(void* newMemory, uint32_t destinationIndex, void* source, uint32_t numElementsToCopy,
	                     uint32_t newMemorySize, uint32_t newMemoryStartIndex);
	Error copyElementsFromOldMemory(void* otherMemory, int32_t otherMemorySize, int32_t otherMemoryStart);

	void moveElementsRightNoWrap(int32_t oldStartIndex, int32_t oldStopIndex, int32_t distance);
	void moveElementsLeftNoWrap(int32_t oldStartIndex, int32_t oldStopIndex, int32_t distance);

	void* memoryAllocationStart; // This might be slightly to the left of "memory"

	const int32_t maxNumEmptySpacesToKeep;  // Can go down to 0
	const int32_t numExtraSpacesToAllocate; // Can go down to 0
};
