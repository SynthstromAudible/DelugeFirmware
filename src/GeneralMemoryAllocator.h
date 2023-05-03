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

#ifndef GENERALMEMORYALLOCATOR_H_
#define GENERALMEMORYALLOCATOR_H_

#include "MemoryRegion.h"

#define MEMORY_REGION_SDRAM 0
#define MEMORY_REGION_INTERNAL 1
#define NUM_MEMORY_REGIONS 2

class Stealable;

class GeneralMemoryAllocator {
public:
	GeneralMemoryAllocator();
	void* alloc(uint32_t requiredSize, uint32_t* getAllocatedSize = NULL, bool mayDeleteFirstUndoAction = false, bool mayUseOnChipRam = false, bool makeStealable = false, void* thingNotToStealFrom = NULL, bool getBiggestAllocationPossible = false);
	void dealloc(void* address);
	uint32_t shortenRight(void* address, uint32_t newSize);
	uint32_t shortenLeft(void* address, uint32_t amountToShorten, uint32_t numBytesToMoveRightIfSuccessful = 0);
	void extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend, uint32_t* getAmountExtendedLeft, uint32_t* getAmountExtendedRight, void* thingNotToStealFrom = NULL);
	uint32_t extendRightAsMuchAsEasilyPossible(void* address);
	void test();
	uint32_t getAllocatedSize(void* address);
	void checkStack(char const* caller);
	void testShorten(int i);
	int getRegion(void* address);
	void testMemoryDeallocated(void* address);

	void putStealableInQueue(Stealable* stealable, int q);
	void putStealableInAppropriateQueue(Stealable* stealable);

	MemoryRegion regions[NUM_MEMORY_REGIONS];

	bool lock;

private:
	void checkEverythingOk(char const* errorString);
};

extern GeneralMemoryAllocator generalMemoryAllocator;

#endif /* GENERALMEMORYALLOCATOR_H_ */
