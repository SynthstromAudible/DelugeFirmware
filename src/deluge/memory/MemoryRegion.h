/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#ifndef MEMORYREGION_H_
#define MEMORYREGION_H_

#include "OrderedResizeableArrayWithMultiWordKey.h"
#include "BidirectionalLinkedList.h"
#include "definitions.h"

struct EmptySpaceRecord {
	uint32_t length;
	uint32_t address;
};

struct NeighbouringMemoryGrabAttemptResult {
	uint32_t address; // 0 means didn't grab / not found.
	int amountsExtended[2];
	uint32_t longestRunFound; // Only valid if didn't return some space.
};

#define SPACE_HEADER_EMPTY 0
#define SPACE_HEADER_STEALABLE 0x40000000
#define SPACE_HEADER_ALLOCATED 0x80000000

#define SPACE_TYPE_MASK 0xC0000000
#define SPACE_SIZE_MASK 0x3FFFFFFF

class MemoryRegion {
public:
	MemoryRegion();
	void setup(void* emptySpacesMemory, int emptySpacesMemorySize, uint32_t regionBegin, uint32_t regionEnd);
	void* alloc(uint32_t requiredSize, uint32_t* getAllocatedSize, bool makeStealable, void* thingNotToStealFrom,
	            bool getBiggestAllocationPossible);
	uint32_t shortenRight(void* address, uint32_t newSize);
	uint32_t shortenLeft(void* address, uint32_t amountToShorten, uint32_t numBytesToMoveRightIfSuccessful = 0);
	void extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
	            uint32_t* getAmountExtendedLeft, uint32_t* getAmountExtendedRight, void* thingNotToStealFrom);
	uint32_t extendRightAsMuchAsEasilyPossible(void* spaceAddress);
	void dealloc(void* address);
	void verifyMemoryNotFree(void* address, uint32_t spaceSize);

	BidirectionalLinkedList stealableClusterQueues[NUM_STEALABLE_QUEUES];
	// Keeps track, semi-accurately, of biggest runs of memory that could be stolen. In a perfect world, we'd have a second
	// index on stealableClusterQueues[q], for run length. Although even that wouldn't automatically reflect changes to run
	// lengths as neighbouring memory is allocated.
	uint32_t stealableClusterQueueLongestRuns[NUM_STEALABLE_QUEUES];
	OrderedResizeableArrayWithMultiWordKey emptySpaces;
	int numAllocations;

#if ALPHA_OR_BETA_VERSION
	char const* name; // For debugging messages only.
#endif

private:
	uint32_t freeSomeStealableMemory(int totalSizeNeeded, void* thingNotToStealFrom, int* __restrict__ foundSpaceSize);
	void markSpaceAsEmpty(uint32_t spaceStart, uint32_t spaceSize, bool mayLookLeft = true, bool mayLookRight = true);
	NeighbouringMemoryGrabAttemptResult
	attemptToGrabNeighbouringMemory(void* originalSpaceAddress, int originalSpaceSize, int minAmountToExtend,
	                                int idealAmountToExtend, void* thingNotToStealFrom,
	                                uint32_t markWithTraversalNo = 0, bool originalSpaceNeedsStealing = false);
	void writeTempHeadersBeforeASteal(uint32_t newStartAddress, uint32_t newSize);
	void sanityCheck();
};

#endif /* MEMORYREGION_H_ */
