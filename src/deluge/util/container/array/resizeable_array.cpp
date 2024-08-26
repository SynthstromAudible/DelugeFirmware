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

#include "util/container/array/resizeable_array.h"
#include "definitions_cxx.hpp"
#include "processing/engines/audio_engine.h"
// #include <algorithm>
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "util/functions.h"
#include <string.h>

#if RESIZEABLE_ARRAY_DO_LOCKS
#define LOCK_ENTRY freezeOnLock();
// Bay_Mud got this error around V4.0.1 (must have been a beta), and thinks a FlashAir card might have been a catalyst.
// It still "shouldn't" be able to happen though.
#define LOCK_EXIT exitLock();
void ResizeableArray::freezeOnLock() {
	if (lock) {
		FREEZE_WITH_ERROR("i008");
	}
	lock = true;
}
void ResizeableArray::exitLock() {
	if (!lock) {
		FREEZE_WITH_ERROR("i008");
	}
	lock = false;
}
#else
#define LOCK_ENTRY                                                                                                     \
	{}
#define LOCK_EXIT                                                                                                      \
	{}
#endif

ResizeableArray::ResizeableArray(int32_t newElementSize, int32_t newMaxNumEmptySpacesToKeep,
                                 int32_t newNumExtrarSpacesToAllocate)
    : elementSize(newElementSize), maxNumEmptySpacesToKeep(newMaxNumEmptySpacesToKeep),
      numExtraSpacesToAllocate(newNumExtrarSpacesToAllocate) {
	emptyingShouldFreeMemory = true;
	staticMemoryAllocationSize = 0;

#if RESIZEABLE_ARRAY_DO_LOCKS
	lock = false;
#endif

	init();
}

ResizeableArray::~ResizeableArray() {
	LOCK_ENTRY

	if (memory) {
		delugeDealloc(memoryAllocationStart);
	}
	// Don't call empty() - this does some other writing, which is a waste of time

	LOCK_EXIT
}

void ResizeableArray::init() {

	LOCK_ENTRY

	numElements = 0;
	memory = NULL;
	memoryAllocationStart = NULL;
	memorySize = 0;
	memoryStart = 0;

	LOCK_EXIT
}

void ResizeableArray::empty() {

	LOCK_ENTRY

	numElements = 0;
	memoryStart = 0;

	if (!staticMemoryAllocationSize && emptyingShouldFreeMemory) {
		if (memory) {
			delugeDealloc(memoryAllocationStart);
		}

		memory = NULL;
		memoryAllocationStart = NULL;
		memorySize = 0;
	}

	LOCK_EXIT
}

// Returns error
Error ResizeableArray::beenCloned() {

	LOCK_ENTRY

	int32_t otherMemorySize = memorySize;
	int32_t otherMemoryStart = memoryStart;
	void* __restrict__ oldMemory = memory;

	Error error = copyElementsFromOldMemory(oldMemory, otherMemorySize, otherMemoryStart);

	LOCK_EXIT

	return error;
}

bool ResizeableArray::cloneFrom(ResizeableArray const* other) {

	LOCK_ENTRY

	numElements = other->numElements;
	Error error = copyElementsFromOldMemory(other->memory, other->memorySize, other->memoryStart);

	LOCK_EXIT

	return error == Error::NONE;
}

// Returns error
Error ResizeableArray::copyElementsFromOldMemory(void* __restrict__ otherMemory, int32_t otherMemorySize,
                                                 int32_t otherMemoryStart) {

	memoryStart = 0;

	if (!numElements) {
		memoryAllocationStart = NULL;
		memory = NULL;
		memorySize = 0;
	}

	else {
		int32_t newSize = numElements + 1;
		uint32_t allocatedSize = newSize * elementSize;
		memory = GeneralMemoryAllocator::get().allocMaxSpeed(allocatedSize);

		if (!memory) {
			numElements = 0;
			memorySize = 0;
			return Error::INSUFFICIENT_RAM;
		}

		memorySize = allocatedSize / elementSize;
		memoryAllocationStart = memory;

		int32_t elementsBeforeWrap = otherMemorySize - otherMemoryStart;

		// TODO: ideally, we're wrap the memory in the clone

		// Copy everything before other's wrap
		int32_t numBytesBefore = std::min(elementsBeforeWrap, numElements) * elementSize;
		memcpy((char* __restrict__)memory, (char* __restrict__)otherMemory + (otherMemoryStart * elementSize),
		       numBytesBefore);

		// And everything after other's wrap, if there was some
		int32_t elementsAfterWrap = numElements - elementsBeforeWrap;
		if (elementsAfterWrap > 0) {
			memcpy((char* __restrict__)memory + (elementsBeforeWrap * elementSize), otherMemory,
			       elementsAfterWrap * elementSize);
		}
	}

	return Error::NONE;
}

void ResizeableArray::swapStateWith(ResizeableArray* other) {

	LOCK_ENTRY

	void* __restrict__ memoryTemp = memory;
	void* __restrict__ memoryAllocationStartTemp = memoryAllocationStart;
	int32_t numElementsTemp = numElements;
	int32_t memorySizeTemp = memorySize;
	int32_t memoryStartTemp = memoryStart;

	memory = other->memory;
	memoryAllocationStart = other->memoryAllocationStart;
	numElements = other->numElements;
	memorySize = other->memorySize;
	memoryStart = other->memoryStart;

	other->memory = memoryTemp;
	other->memoryAllocationStart = memoryAllocationStartTemp;
	other->numElements = numElementsTemp;
	other->memorySize = memorySizeTemp;
	other->memoryStart = memoryStartTemp;

	LOCK_EXIT
}

void ResizeableArray::attemptMemoryShorten() {
	if (staticMemoryAllocationSize) {
		return;
	}
	if ((uint32_t)memoryAllocationStart >= (uint32_t)INTERNAL_MEMORY_BEGIN) {
		return;
	}

	uint32_t allocatedSize = GeneralMemoryAllocator::get().getAllocatedSize(memoryAllocationStart);

	if (allocatedSize > (memorySize + maxNumEmptySpacesToKeep) * elementSize) {
		int32_t extraSpaceLeft = (uint32_t)memory - (uint32_t)memoryAllocationStart;

		int32_t extraSpaceRight = allocatedSize - extraSpaceLeft - memorySize * elementSize;

		if (extraSpaceLeft > extraSpaceRight) {
			uint32_t amountShortened = GeneralMemoryAllocator::get().shortenLeft(memoryAllocationStart, extraSpaceLeft);
			memoryAllocationStart = (char* __restrict__)memoryAllocationStart + amountShortened;
		}

		else {
			GeneralMemoryAllocator::get().shortenRight(memoryAllocationStart,
			                                           extraSpaceLeft + memorySize * elementSize);
		}
	}
}

void ResizeableArray::deleteAtIndex(int32_t i, int32_t numToDelete, bool mayShortenMemoryAfter) {
	LOCK_ENTRY

	int32_t newNum = numElements - numToDelete;

	// If that takes us down to 0 elements, easy!
	if (newNum <= 0) {
		LOCK_EXIT
		empty();
		return;
	}

	int32_t elementsBeforeWrap = memorySize - memoryStart;
	int32_t elementsAfterWrap = numElements - elementsBeforeWrap;

	// If no wrap...
	if (elementsAfterWrap < 0) {

mostBasicDelete:
		// If deleting in first half...
		if ((i + (numToDelete >> 1)) < (numElements >> 1)) {
			// if (i) moveElementsRightNoWrap(0, i, numToDelete); // No - not sure why, but this gave insane occasional
			// crashes.
			moveElementsRight(0, i, numToDelete);
			memoryStart += numToDelete;
			if (memoryStart >= memorySize) {
				memoryStart -= memorySize;
			}
		}

		// If deleting in second half...
		else {
			/* No - not sure why, but this gave insane occasional crashes.
			int32_t oldStartIndex = i + numToDelete;
			if (oldStartIndex != numElements)
			    moveElementsLeftNoWrap(oldStartIndex, numElements, numToDelete);
			    */
			moveElementsLeft(i + numToDelete, numElements, numToDelete);
		}
	}

	// Or if yes wrap...
	else {

		// If our delete borders (or overlaps) the wrap point, our job is really easy
		if (i <= elementsBeforeWrap && i + numToDelete >= elementsBeforeWrap) {

			int32_t elementsToDeleteBeforeWrap = elementsBeforeWrap - i;
			int32_t elementsToDeleteAfterWrap = numToDelete - elementsToDeleteBeforeWrap;

			memory = (char*)memory + elementsToDeleteAfterWrap * elementSize;
			memorySize -= numToDelete;
			memoryStart -= elementsToDeleteAfterWrap;
			if (memoryStart < 0) {
				memoryStart += memorySize; // Could this happen?
			}
		}

		else {

			int32_t distanceFromEndPoint = std::min(i, numElements - i - numToDelete);

			int32_t distanceFromWrapPoint;
			if (i >= elementsBeforeWrap) {
				distanceFromWrapPoint = i - elementsBeforeWrap;
			}
			else if (i + numToDelete <= elementsBeforeWrap) {
				distanceFromWrapPoint = elementsBeforeWrap - (i + numToDelete);
			}
			else {
				distanceFromWrapPoint = 0;
			}

			// If closer to either start or end than wrap-point AND there isn't too much wasted empty space between
			// these...
			if (distanceFromEndPoint <= distanceFromWrapPoint) {

				// If we have a fixed memory allocation, then we don't need to worry about wasted empty space, and we
				// can just do the most basic delete. Careful with this - it seemed to cause a crash when first
				// introduced in commit "Tidied up some stuff in ResizeableArray", though that seems to have been
				// because of the bugs fixed in commit "Fixed horrendous problem with ResizeableArray[...]".
				if (staticMemoryAllocationSize || !mayShortenMemoryAfter) {
					goto mostBasicDelete;
				}

				int32_t freeMemory = memorySize - newNum; // After deletion

				// If we're not going to end up with more free memory than we're allowed, then the best option is to do
				// the most basic delete
				if (freeMemory < maxNumEmptySpacesToKeep) {
					goto mostBasicDelete;
				}

				// Ok, if we're still here, we're looking to reduce memory usage

				// If easier to move bit between between deletion point and wrap point...
				if (distanceFromWrapPoint < memorySize - distanceFromWrapPoint - freeMemory) {
					goto moveBitBetweenWrapPointAndDeletionPoint;
				}

				// Or if easier to move everything else...
				else {

					int32_t contractMemoryBy = freeMemory - (maxNumEmptySpacesToKeep >> 1);

					// If deleting before wrap point...
					if (i < elementsBeforeWrap) {

						// Move bit left of deletion point right
						if (i != 0) {
							memmove((char* __restrict__)memory + (memoryStart + numToDelete) * elementSize,
							        (char* __restrict__)memory + (memoryStart)*elementSize, i * elementSize);
#if TEST_VECTOR
							moveCount += i;
#endif
						}

						memoryStart += numToDelete;
						if (memoryStart >= memorySize) {
							memoryStart -= memorySize;
						}

						// And move bit right of wrap point right too
						memmove((char* __restrict__)memory + contractMemoryBy * elementSize, memory,
						        elementsAfterWrap * elementSize);
#if TEST_VECTOR
						moveCount += elementsAfterWrap;
#endif
						memory = (char* __restrict__)memory + contractMemoryBy * elementSize;
					}

					// Or if deleting after wrap point...
					else {

						// Move bit right of deletion point left
						if (i != numElements - numToDelete) {
							memmove((char* __restrict__)memory + (memoryStart + i - memorySize) * elementSize,
							        (char* __restrict__)memory
							            + (memoryStart + i + numToDelete - memorySize) * elementSize,
							        (numElements - i - numToDelete) * elementSize);
#if TEST_VECTOR
							moveCount += (numElements - i - numToDelete);
#endif
						}

						// Move bit left of wrap point left too
						memmove((char* __restrict__)memory + (memoryStart - contractMemoryBy) * elementSize,
						        (char* __restrict__)memory + (memoryStart)*elementSize,
						        elementsBeforeWrap * elementSize);
#if TEST_VECTOR
						moveCount += elementsBeforeWrap;
#endif
					}

					memorySize -= contractMemoryBy;
					memoryStart -= contractMemoryBy;
					if (memoryStart < 0) {
						memoryStart += memorySize;
					}
				}
			}

			// Or if closer to wrap point...
			else {

moveBitBetweenWrapPointAndDeletionPoint:

				// If deleting before wrap point...
				if (i < elementsBeforeWrap) {

					// Move elements left
					if (i != elementsBeforeWrap - numToDelete) {
						memmove((char* __restrict__)memory + (memoryStart + i) * elementSize,
						        (char* __restrict__)memory + (memoryStart + i + numToDelete) * elementSize,
						        (elementsBeforeWrap - i - numToDelete) * elementSize);
#if TEST_VECTOR
						moveCount += (elementsBeforeWrap - i - numToDelete);
#endif
					}

					memorySize -= numToDelete;
					if (memoryStart >= memorySize) {
						memoryStart -= memorySize;
					}
				}

				// Or if deleting after wrap point...
				else {

					// Move elements right
					if (i != elementsBeforeWrap) {
						memmove((char* __restrict__)memory + numToDelete * elementSize, (char* __restrict__)memory,
						        (i - elementsBeforeWrap) * elementSize);
#if TEST_VECTOR
						moveCount += (i - elementsBeforeWrap);
#endif
					}

					memorySize -= numToDelete;
					memoryStart -= numToDelete;
					if (memoryStart < 0) {
						memoryStart += memorySize;
					}
					memory = (char* __restrict__)memory + numToDelete * elementSize;
				}
			}
		}
	}

	numElements = newNum;

	if (!staticMemoryAllocationSize && mayShortenMemoryAfter) {
		elementsBeforeWrap = memorySize - memoryStart; // Updates this

		// If no wrap...
		if (numElements <= elementsBeforeWrap) {
			memorySize = numElements;
			memory = (char* __restrict__)memory + memoryStart * elementSize;
			memoryStart = 0;
		}

		attemptMemoryShorten();
	}

	LOCK_EXIT
}

// Currently this doesn't really support having a static memory allocation, so don't call this
bool ResizeableArray::ensureEnoughSpaceAllocated(int32_t numAdditionalElementsNeeded) {

	LOCK_ENTRY

	// If nothing allocated yet, easy
	if (!memory) {
		if (staticMemoryAllocationSize) {
			LOCK_EXIT
			return false;
		}

		uint32_t allocatedMemorySize = numAdditionalElementsNeeded * elementSize;

		void* newMemory = GeneralMemoryAllocator::get().allocMaxSpeed(allocatedMemorySize);
		if (!newMemory) {
			LOCK_EXIT
			return false;
		}

		setMemory(newMemory, allocatedMemorySize);
		LOCK_EXIT
		return true;
	}

	int32_t elementsBeforeWrap = memorySize - memoryStart;
	int32_t elementsAfterWrap = numElements - elementsBeforeWrap;

	int32_t newNum = numElements + numAdditionalElementsNeeded;

	void* __restrict__ oldMemory = memory;
	void* __restrict__ oldMemoryAllocationStart = memoryAllocationStart;
	int32_t oldMemoryStart = memoryStart;
	int32_t oldMemorySize = memorySize;

tryAgain:
	uint32_t allocatedSize = GeneralMemoryAllocator::get().getAllocatedSize(memoryAllocationStart);

	// Try expanding left into existing memory
	uint32_t extraSpaceLeft = (uint32_t)memory - (uint32_t)memoryAllocationStart;
	uint32_t extraElementsLeft = extraSpaceLeft / elementSize;
	memory = (char* __restrict__)memory - extraElementsLeft * elementSize;
	memoryStart += extraElementsLeft;
	memorySize += extraElementsLeft;

	// Try expanding right into existing memory
	extraSpaceLeft = (uint32_t)memory - (uint32_t)memoryAllocationStart; // Updates it
	memorySize = (uint32_t)(allocatedSize - extraSpaceLeft) / elementSize;

	int32_t memoryIncreasedBy = memorySize - oldMemorySize;

	// If that wasn't enough memory (very likely on first try)
	if (memorySize < newNum) {

		if (staticMemoryAllocationSize) {
			LOCK_EXIT
			return false;
		}

		// Try extending memory
		uint32_t amountExtendedLeft, amountExtendedRight;

#ifdef TEST_VECTOR
		if (getRandom255() < 10)
			goto getBrandNewMemory;
#endif
		GeneralMemoryAllocator::get().extend(memoryAllocationStart, (newNum)*elementSize - allocatedSize,
		                                     (newNum + numExtraSpacesToAllocate) * elementSize - allocatedSize,
		                                     &amountExtendedLeft, &amountExtendedRight);

		// If successfully extended...
		if (amountExtendedLeft || amountExtendedRight) {
			memoryAllocationStart = (char* __restrict__)memoryAllocationStart - amountExtendedLeft;
			goto tryAgain;
		}

getBrandNewMemory:

		// If couldn't extend, try allocating brand new space instead

		void* __restrict__ newMemory;

#ifdef TEST_VECTOR
		if (getRandom255() < 50) {
			D_PRINTLN("allocation fail for test purpose");
			goto allocationFail;
		}
#endif

		uint32_t newMemoryAllocationSize = (newNum + numExtraSpacesToAllocate) * elementSize;
		newMemory = GeneralMemoryAllocator::get().allocMaxSpeed(newMemoryAllocationSize);
		if (!newMemory) {
			newMemoryAllocationSize = newNum * elementSize;
			newMemory = GeneralMemoryAllocator::get().allocMaxSpeed(newMemoryAllocationSize);
		}

		// If that didn't work...
		if (!newMemory) {

allocationFail:

			// Put everything back how it was and get out
			memory = oldMemory;
			memoryAllocationStart = oldMemoryAllocationStart;
			memoryStart = oldMemoryStart;
			memorySize = oldMemorySize;

			LOCK_EXIT
			return false;
		}

		uint32_t newMemorySize = newMemoryAllocationSize / elementSize;
		uint32_t newMemoryStartIndex = 0;

		if (memoryIncreasedBy) {
			D_PRINTLN("new memory, already increased");
		}

		// Or if we're here, we got our new memory. Copy the stuff over. Before wrap point...
		int32_t upTo = std::min(elementsBeforeWrap, numElements);
		copyToNewMemory(newMemory, 0, getElementAddress(0), upTo, newMemorySize, newMemoryStartIndex);

		// And after wrap point...
		if (elementsAfterWrap > 0) {
			copyToNewMemory(newMemory, elementsBeforeWrap, getElementAddress(elementsBeforeWrap + memoryIncreasedBy),
			                elementsAfterWrap, newMemorySize, newMemoryStartIndex);
		}

		delugeDealloc(memoryAllocationStart);
		memory = newMemory;
		memoryAllocationStart = newMemory;
		memorySize = newMemorySize;
		memoryStart = newMemoryStartIndex;
	}

	// Or if we did have enough memory...
	else {

		// If memory wrapped...
		if (elementsAfterWrap > 0) {

			// If less elements before wrap...
			if (elementsBeforeWrap < elementsAfterWrap) {

				// Move elements right
				for (int32_t i = (elementsBeforeWrap - 1); i >= 0; i--) {
					memcpy(getElementAddress(i + memoryIncreasedBy), getElementAddress(i), elementSize);
				}

				memoryStart += memoryIncreasedBy;
				if (memoryStart >= memorySize) {
					memoryStart -= memorySize;
				}
			}

			// Or if less elements after wrap
			else {

				// Move elements left
				for (int32_t i = elementsBeforeWrap; i < numElements; i++) {
					memcpy(getElementAddress(i), getElementAddress(i + memoryIncreasedBy), elementSize);
				}
			}
		}
	}

	LOCK_EXIT
	return true;
}

bool ResizeableArray::attemptMemoryExpansion(int32_t minNumToExtend, int32_t idealNumToExtendIfExtendingAllocation,
                                             bool mayExtendAllocation, void* thingNotToStealFrom) {

	void* __restrict__ oldMemory = memory;
	int32_t oldMemorySize = memorySize;
	int32_t oldMemoryStart = memoryStart;

startAgain:
	// If we actually had a bit more already, left...
	uint32_t extraBytesLeft = (uint32_t)memory - (uint32_t)memoryAllocationStart;
	if (extraBytesLeft >= elementSize) {
		int32_t extraElementsLeft =
		    extraBytesLeft / elementSize; // See how many extra elements there are space for on the left
		if (extraElementsLeft > minNumToExtend) {
			extraElementsLeft = minNumToExtend; // We might not want all the extra space
		}

		memory = (char* __restrict__)memory - (elementSize * extraElementsLeft);
		memorySize += extraElementsLeft;
		memoryStart += extraElementsLeft;

		minNumToExtend -= extraElementsLeft;
		if (minNumToExtend <= 0) {
			return true;
		}
		idealNumToExtendIfExtendingAllocation -= extraElementsLeft;
		extraBytesLeft -= elementSize * extraElementsLeft;
	}

	// If we actually had a bit more already, right...
	uint32_t potentialMemorySize = staticMemoryAllocationSize;
	if (!potentialMemorySize) {
		potentialMemorySize = GeneralMemoryAllocator::get().getAllocatedSize(memoryAllocationStart) - extraBytesLeft;
	}
	uint32_t extraBytesRight = potentialMemorySize - (memorySize * elementSize);
	if (extraBytesRight >= elementSize) {
		int32_t extraElementsRight =
		    extraBytesRight / elementSize; // See how many extra elements there are space for on the right
		if (extraElementsRight > minNumToExtend) {
			extraElementsRight = minNumToExtend; // We might not want all the extra space
		}

		memorySize += extraElementsRight;

		minNumToExtend -= extraElementsRight;
		if (minNumToExtend <= 0) {
			return true;
		}
		idealNumToExtendIfExtendingAllocation -= extraElementsRight;
		extraBytesRight -= elementSize * extraElementsRight;
	}

	if (mayExtendAllocation) {

		/*
#ifdef TEST_VECTOR
		if (getRandom255() < 2) goto fail;
#endif
*/
		// Try extending memory.
		// TODO: in a perfect world, we'd be able to specify a minimum amount as well as a step-size for left and right,
		// or something like that
		uint32_t amountExtendedLeft, amountExtendedRight;
		GeneralMemoryAllocator::get().extend(memoryAllocationStart, minNumToExtend * elementSize,
		                                     idealNumToExtendIfExtendingAllocation * elementSize, &amountExtendedLeft,
		                                     &amountExtendedRight, thingNotToStealFrom);

		if (amountExtendedLeft || amountExtendedRight) {
			memoryAllocationStart = (char* __restrict__)memoryAllocationStart - amountExtendedLeft;
			goto startAgain;
		}
	}

	memory = oldMemory;
	memoryStart = oldMemoryStart;
	memorySize = oldMemorySize;

	return false;
}

void ResizeableArray::copyToNewMemory(void* __restrict__ newMemory, uint32_t destinationIndex,
                                      void* __restrict__ source, uint32_t numElementsToCopy, uint32_t newMemorySize,
                                      uint32_t newMemoryStartIndex) {

	destinationIndex += newMemoryStartIndex;
#if TEST_VECTOR
	moveCount += numElementsToCopy;
#endif
	// If ends after wrap point...
	if (destinationIndex + numElementsToCopy > newMemorySize) {

		// If also begins after wrap point...
		if (destinationIndex >= newMemorySize) {
			destinationIndex -= newMemorySize;
		}

		// Or if begins before wrap point...
		else {
			int32_t elementsBeforeWrap = newMemorySize - destinationIndex;
			memcpy((char* __restrict__)newMemory + destinationIndex * elementSize, source,
			       elementsBeforeWrap * elementSize);
			memcpy((char* __restrict__)newMemory, (char* __restrict__)source + elementsBeforeWrap * elementSize,
			       (numElementsToCopy - elementsBeforeWrap) * elementSize);
			return;
		}
	}

	memcpy((char* __restrict__)newMemory + destinationIndex * elementSize, source, numElementsToCopy * elementSize);
}

// Ensure oldStartIndex != oldStopIndex before calling
void ResizeableArray::moveElementsLeftNoWrap(int32_t oldStartIndex, int32_t oldStopIndex, int32_t distance) {

	void* oldStartAddress = getElementAddress(oldStartIndex);
	void* newStartAddress = (char*)oldStartAddress - (elementSize * distance);
	int32_t numBytes = (oldStopIndex - oldStartIndex) * elementSize;
	memmove(newStartAddress, oldStartAddress, numBytes);
#if TEST_VECTOR
	moveCount += (oldStopIndex - oldStartIndex);
#endif
}

void ResizeableArray::moveElementsLeft(int32_t oldStartIndex, int32_t oldStopIndex, int32_t distance) {

	if (oldStartIndex == oldStopIndex) {
		return;
	}

	int32_t elementsBeforeWrap = memorySize - memoryStart;

	int32_t newStartIndex = oldStartIndex - distance;
	int32_t oldLastIndex = oldStopIndex - 1;

	bool newStartBeforeWrap = (newStartIndex < elementsBeforeWrap);
	bool oldLastIndexBeforeWrap = (oldLastIndex < elementsBeforeWrap);

	// If area to be moved doesn't overlap wrap, simple...
	if (newStartBeforeWrap == oldLastIndexBeforeWrap) {
		moveElementsLeftNoWrap(oldStartIndex, oldStopIndex, distance);
	}

	// Otherwise, complicated
	else {

		// Do everything left of the wrap point
		int32_t numElementsLeft = elementsBeforeWrap - oldStartIndex;
		if (numElementsLeft > 0) {
			void* newStartAddress = getElementAddress(newStartIndex);
			void* oldStartAddress = (char*)newStartAddress + (elementSize * distance);
			memmove(newStartAddress, oldStartAddress, numElementsLeft * elementSize);
#if TEST_VECTOR
			moveCount += numElementsLeft;
#endif
		}

		// Move the elements that need to move past the wrap point
		// These two new ints are relative to memory, not to memoryStart
		int32_t startPastWrapPoint = std::max(oldStartIndex - elementsBeforeWrap, 0_i32);
		int32_t stopPastWrapPoint = std::min(oldStopIndex - elementsBeforeWrap, distance);

		int32_t numToMovePastWrapPoint = stopPastWrapPoint - startPastWrapPoint;
		memcpy((char*)memory + (memorySize - distance + startPastWrapPoint) * elementSize,
		       (char*)memory + startPastWrapPoint * elementSize, elementSize * numToMovePastWrapPoint);
#if TEST_VECTOR
		moveCount += numToMovePastWrapPoint;
#endif

		// Do everything right of the wrap point
		int32_t numElementsRight = oldStopIndex - elementsBeforeWrap - distance;
		if (numElementsRight > 0) {
			memmove(memory, (char*)memory + (elementSize * distance), numElementsRight * elementSize);
#if TEST_VECTOR
			moveCount += numElementsRight;
#endif
		}
	}
}

// Ensure oldStartIndex != oldStopIndex before calling
void ResizeableArray::moveElementsRightNoWrap(int32_t oldStartIndex, int32_t oldStopIndex, int32_t distance) {

	void* oldStartAddress = getElementAddress(oldStartIndex);
	void* newStartAddress = (char*)oldStartAddress + (elementSize * distance);
	int32_t numBytes = (oldStopIndex - oldStartIndex) * elementSize;
	memmove(newStartAddress, oldStartAddress, numBytes);
#if TEST_VECTOR
	moveCount += (oldStopIndex - oldStartIndex);
#endif
}

void ResizeableArray::moveElementsRight(int32_t oldStartIndex, int32_t oldStopIndex, int32_t distance) {
	if (oldStartIndex == oldStopIndex) {
		return;
	}

	int32_t elementsBeforeWrap = memorySize - memoryStart;

	int32_t newLastIndex = oldStopIndex - 1 + distance;

	bool oldStartBeforeWrap = (oldStartIndex < elementsBeforeWrap);
	bool newLastBeforeWrap = (newLastIndex < elementsBeforeWrap);

	// If area to be moved doesn't overlap wrap, simple...
	if (oldStartBeforeWrap == newLastBeforeWrap) {

		moveElementsRightNoWrap(oldStartIndex, oldStopIndex, distance);
	}

	// Otherwise, complicated
	else {

		// Do everything right of the wrap point
		int32_t numElementsRight = oldStopIndex - elementsBeforeWrap;
		if (numElementsRight > 0) {
			memmove((char*)memory + (elementSize * distance), memory, numElementsRight * elementSize);
#if TEST_VECTOR
			moveCount += numElementsRight;
#endif
		}

		// Move the elements that need to move past the wrap point. TODO: this isn't quite ideal...
		int32_t startPastWrapPoint = std::max(oldStartIndex - elementsBeforeWrap + distance, 0_i32);
		int32_t stopPastWrapPoint = std::min(oldStopIndex - elementsBeforeWrap + distance, distance);
		int32_t numToMovePastWrapPoint = stopPastWrapPoint - startPastWrapPoint;
		memcpy((char*)memory + startPastWrapPoint * elementSize,
		       (char*)memory + (memorySize - distance + startPastWrapPoint) * elementSize,
		       elementSize * numToMovePastWrapPoint);
#if TEST_VECTOR
		moveCount += numToMovePastWrapPoint;
#endif
		// Do everything left of the wrap point
		int32_t numElementsLeft = elementsBeforeWrap - (oldStartIndex + distance);
		if (numElementsLeft > 0) {
			void* oldStartAddress = getElementAddress(oldStartIndex);
			void* newStartAddress = (char*)oldStartAddress + (elementSize * distance);
			memmove(newStartAddress, oldStartAddress, numElementsLeft * elementSize);
#if TEST_VECTOR
			moveCount += numElementsLeft;
#endif
		}
	}
}

// You can only call this if there definitely isn't already any memory set
void ResizeableArray::setMemory(void* newMemory, int32_t newMemorySize) {

	memory = newMemory;
	memoryAllocationStart = newMemory;
	memorySize = newMemorySize / elementSize;
	memoryStart = 0;
#if TEST_VECTOR
	moveCount = 0;
#endif
}

// You can only call this if there definitely isn't already any memory set
void ResizeableArray::setStaticMemory(void* newMemory, int32_t newMemorySize) {

	staticMemoryAllocationSize = newMemorySize;
	setMemory(newMemory, newMemorySize);
}

// Returns error code
Error ResizeableArray::insertAtIndex(int32_t i, int32_t numToInsert, void* thingNotToStealFrom) {

	if (ALPHA_OR_BETA_VERSION && (i < 0 || i > numElements || numToInsert < 1)) {
		FREEZE_WITH_ERROR("E280");
	}

	LOCK_ENTRY

	int32_t newNum = numElements + numToInsert;

	// If no memory yet...
	if (!memory) {

		if (staticMemoryAllocationSize) {
			LOCK_EXIT
			return Error::INSUFFICIENT_RAM;
		}

		int32_t newMemorySize = (numExtraSpacesToAllocate >> 1)
		                        + numToInsert; // The >>1 is arbirtary - we just don't wanna be allocating lots

		uint32_t allocatedMemorySize = newMemorySize * elementSize;

		void* newMemory = GeneralMemoryAllocator::get().allocMaxSpeed(allocatedMemorySize, thingNotToStealFrom);
		if (!newMemory) {
			LOCK_EXIT
			return Error::INSUFFICIENT_RAM;
		}

		if (ALPHA_OR_BETA_VERSION && allocatedMemorySize < newMemorySize * elementSize) {
			FREEZE_WITH_ERROR("FFFF");
		}

		setMemory(newMemory, allocatedMemorySize);
	}

	// Otherwise, if some memory...
	else {

		int32_t elementsBeforeWrap = memorySize - memoryStart;
		int32_t elementsAfterWrap = numElements - elementsBeforeWrap;

		// If no wrap
		if (elementsAfterWrap <= 0) {

			// If not enough memory...
			if (newNum > memorySize) {
				bool success = attemptMemoryExpansion(numToInsert, numToInsert + numExtraSpacesToAllocate,
				                                      !staticMemoryAllocationSize, thingNotToStealFrom);
				if (!success) {
					goto getBrandNewMemory;
				}
			}

workNormally:
			// If inserting in first half...
			if ((i << 1) < numElements) {
				memoryStart -= numToInsert;
				if (memoryStart < 0) {
					memoryStart += memorySize;
				}
				moveElementsLeft(numToInsert, i + numToInsert,
				                 numToInsert); // There might be a wrap after moving them...
			}

			// If inserting in second half...
			else {
				moveElementsRight(i, numElements, numToInsert); // There might be a wrap after moving them...
			}
		}

		// Or, if wrap...
		else {

			int32_t distanceFromEndPoint = std::min(i, numElements - i);
			int32_t distanceFromWrapPoint = i - elementsBeforeWrap;
			if (distanceFromWrapPoint < 0) {
				distanceFromWrapPoint = -distanceFromWrapPoint;
			}

			// If closer to either start or end than wrap-point...
			if (distanceFromEndPoint <= distanceFromWrapPoint) {

				// If we have enough memory, then great - doing the "normal" thing is the most efficient thing we can
				// do. Sadly, if we've been inserting lots, there won't be any extra memory within memorySize here - if
				// there's any, it'll be out to the sides (at the wrap point). It'd be better if we had some in both
				// places, like happens after a get-brand-new-memory. Maybe I should make it so doing an extend puts
				// some of the empty space in the middle there...
				if (newNum <= memorySize) {
					goto workNormally;

					// Or if we didn't have enough memory...
				}
				else {

					// ... and we can't extend memory...
					if (!attemptMemoryExpansion(numToInsert, numToInsert + numExtraSpacesToAllocate,
					                            !staticMemoryAllocationSize, thingNotToStealFrom)) {

						// Only choice is to get brand new memory
						goto getBrandNewMemory;
					}
				}
			}

			// Or if closer to wrap-point...
			else {

				// If can't or won't grab some extra memory at the wrap-point...
				if (!attemptMemoryExpansion(numToInsert, numToInsert + numExtraSpacesToAllocate,
				                            !staticMemoryAllocationSize, thingNotToStealFrom)) {

					// If we do actually have enough memory, working "normally" is still an option, and it's now the
					// best option
					if (newNum <= memorySize) {
						goto workNormally;

						// Otherwise, we need brand new memory
					}
					else {
						goto getBrandNewMemory;
					}
				}
			}

			// If we're here, memorySize has just been expanded by numToInsert

			int32_t numNewElementsWhichAreBeforeWrap = memorySize - memoryStart - elementsBeforeWrap;
			int32_t numNewElementsWhichAreAfterWrap = numToInsert - numNewElementsWhichAreBeforeWrap;

			// If inserting before wrap point...
			if (i < elementsBeforeWrap) {

				int32_t elementsBetweenInsertionAndWrap = elementsBeforeWrap - i;
				int32_t otherOption = i + elementsAfterWrap; // The other option is to separately move the elements
				                                             // before the insertion, and the ones after the wrap
				if (elementsBetweenInsertionAndWrap <= otherOption) {

					// Move elements between insertion point and wrap point right
					moveElementsRight(i, elementsBeforeWrap,
					                  numToInsert); // No-wrap seems to work here, but I can't quite see why
				}

				else {

					// Move elements after wrap point left
					moveElementsLeft(elementsBeforeWrap + numToInsert, numElements + numToInsert, numToInsert);

					// Move elements before insertion point left. We call moveElementsLeft() because that deals with if
					// it passes over the wrap point, which can definitely happen.
					memoryStart -= numToInsert;
					if (memoryStart < 0) {
						memoryStart += memorySize; // Could this actually happen? Don't think so?
					}
					moveElementsLeft(numToInsert, numToInsert + i, numToInsert);
				}
			}

			// Or if inserting after wrap point...
			else if (i > elementsBeforeWrap) {

				int32_t elementsBetweenWrapAndInsertion = i - elementsBeforeWrap;
				int32_t otherOption = elementsBeforeWrap + (numElements - i);
				if (elementsBetweenWrapAndInsertion <= otherOption) {

					// Move elements between wrap point and insertion point left
					moveElementsLeft(elementsBeforeWrap + numToInsert, i + numToInsert, numToInsert);
				}

				else {

					// Move elements before wrap right
					moveElementsRight(0, elementsBeforeWrap, numToInsert);
					memoryStart += numToInsert; // Gets wrapped below
					if (memoryStart >= memorySize) {
						memoryStart -= memorySize;
					}

					// Move elements after insertion point (which is after wrap) right
					// REMEMBER - memoryStart has already incremented, just above
					moveElementsRight(i, numElements, numToInsert);
				}
			}
		}

		if (false) {
getBrandNewMemory:

			if (staticMemoryAllocationSize) {
				LOCK_EXIT
				return Error::INSUFFICIENT_RAM;
			}

			// D_PRINTLN("getting new memory");

			// Otherwise, manually get some brand new memory and do a more complex copying process
			uint32_t desiredSize = (newNum + numExtraSpacesToAllocate) * elementSize;

getBrandNewMemoryAgain:
			uint32_t allocatedSize = desiredSize;
			void* __restrict__ newMemory =
			    GeneralMemoryAllocator::get().allocMaxSpeed(allocatedSize, thingNotToStealFrom);

			// If that didn't work...
			if (!newMemory) {

				// If our expectations weren't already as low as they can go, then lower our expectations and try
				// again...
				if (desiredSize > newNum * elementSize) {
					desiredSize = newNum * elementSize;
					goto getBrandNewMemoryAgain;
				}

				// Otherwise, we're screwed
				else {
					LOCK_EXIT
					return Error::INSUFFICIENT_RAM;
				}
			}

			uint32_t newAllocatedSize = allocatedSize / elementSize;
			uint32_t surplusElements = newAllocatedSize - newNum;

			// Set up the new memory so the wrap point is right in the middle. This makes everything run faster,
			// particularly if this brand-new-memory routine is called often
			uint32_t newMemorySize = newNum + (surplusElements >> 1);
			uint32_t newMemoryStartIndex = newMemorySize - (newNum >> 1);

			// Copy until we get up to either the wrap point or the insertion point
			int32_t firstElements = std::min(i, elementsBeforeWrap);
			if (firstElements > 0) {
				copyToNewMemory(newMemory, 0, getElementAddress(0), firstElements, newMemorySize, newMemoryStartIndex);
			}

			// If we didn't reach the wrap point because we reached the insertion point, do some more, up until the wrap
			// point
			if (firstElements < elementsBeforeWrap) {
				int32_t secondElementsTotal = std::min(elementsBeforeWrap, numElements);
				if (secondElementsTotal > 0) {
					copyToNewMemory(newMemory, firstElements + numToInsert, getElementAddress(firstElements),
					                secondElementsTotal - firstElements, newMemorySize, newMemoryStartIndex);
				}
			}

			// We've now copied up until the wrap point, or to the end of the elements if there weren't that many.
			// See if we need to do any more before the insertion point...
			int32_t elementsAfterWrapBeforeInsertion = i - elementsBeforeWrap;
			if (elementsAfterWrapBeforeInsertion > 0) {
				copyToNewMemory(newMemory, elementsBeforeWrap, memory, elementsAfterWrapBeforeInsertion, newMemorySize,
				                newMemoryStartIndex);
			}

			// Ok, we've now passed the wrap point *and* the insertion point. See what's left
			int32_t upTo = std::max(elementsBeforeWrap, i);
			int32_t elementsLeft = numElements - upTo;
			if (elementsLeft > 0) {
				copyToNewMemory(newMemory, upTo + numToInsert, getElementAddress(upTo), elementsLeft, newMemorySize,
				                newMemoryStartIndex);
			}

			// That copy may have taken ages, particularly in the case where they're recording / resampling a sample and
			// the array of clusters has grown.
			AudioEngine::bypassCulling = true;

			delugeDealloc(memoryAllocationStart);
			memory = newMemory;
			memoryAllocationStart = newMemory;
			memorySize = newMemorySize;
			memoryStart = newMemoryStartIndex;
		}
	}

	numElements = newNum;

	LOCK_EXIT
	return Error::NONE;
}
// swapElements and repositionElements are fine - GCC doesn't like that workingMemory
// would be unbounded if elementSize is unbounded, but it is so it's all ok
#pragma GCC push
#pragma GCC diagnostic ignored "-Wstack-usage="

void ResizeableArray::swapElements(int32_t i1, int32_t i2) {
	LOCK_ENTRY

	char workingMemory[elementSize];

	memcpy(workingMemory, getElementAddress(i1), elementSize);
	memcpy(getElementAddress(i1), getElementAddress(i2), elementSize);
	memcpy(getElementAddress(i2), workingMemory, elementSize);

	LOCK_EXIT
}

void ResizeableArray::repositionElement(int32_t iFrom, int32_t iTo) {
	LOCK_ENTRY

	char workingMemory[elementSize];

	memcpy(workingMemory, getElementAddress(iFrom), elementSize);

	if (iFrom < iTo) {
		moveElementsLeft(iFrom + 1, iTo + 1, 1);
	}
	else {
		moveElementsRight(iTo, iFrom, 1);
	}
	memcpy(getElementAddress(iTo), workingMemory, elementSize);

	LOCK_EXIT
}
#pragma GCC pop
