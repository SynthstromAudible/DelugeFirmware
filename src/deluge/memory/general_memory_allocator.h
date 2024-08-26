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

#pragma once

#include "definitions_cxx.hpp"
#include "memory/memory_region.h"

#define MEMORY_REGION_STEALABLE 0
#define MEMORY_REGION_INTERNAL 1
#define MEMORY_REGION_EXTERNAL 2
#define MEMORY_REGION_EXTERNAL_SMALL 3
#define MEMORY_REGION_INTERNAL_SMALL 4
#define NUM_MEMORY_REGIONS 5
constexpr uint32_t RESERVED_EXTERNAL_ALLOCATOR = 0x00200000;       // 2 MiB
constexpr uint32_t RESERVED_EXTERNAL_SMALL_ALLOCATOR = 0x00020000; // 200k
constexpr uint32_t RESERVED_INTERNAL_SMALL = 0x00010000;           // 200k
class Stealable;

/*
 * ======================= MEMORY ALLOCATION ========================
 *
 * The Deluge codebase uses a custom memory allocation system, largely necessitated by the fact that
 * the Deluge’s CPU has 3MB ram, plus the Deluge has an external 64MB SDRAM IC, and both of these
 * need to have dynamic memory allocation as part of the same system.
 *
 * The internal RAM on the CPU is a bit faster, so is allocated first when available.
 * But huge blocks of data like cached Clusters of audio data from the SD card are always
 * placed on the external RAM IC because they would overwhelm the internal RAM too quickly,
 * preventing potentially thousands of small objects which need to be accessed all the time
 * from being placed in that fast internal RAM.
 *
 * Various objects or pieces of data remain loaded (cached) in RAM even when they are no longer necessarily needed.
 * The main example of this is audio data in Clusters, discussed above. The base class for all such
 * objects is Stealable, and as the name suggests, their memory may usually be “stolen” when needed.
 *
 * Most Stealables store a “numReasonsToBeLoaded”, which counts how many “things” are requiring
 * that object to be retained in RAM. E.g. a Cluster of audio data would have a “reason” to
 * remain loaded in RAM if it is currently being played back. If that numReasons goes down to 0,
 * then that Stealable object is usually free to have its memory stolen.
 *
 * Stealables which in fact are eligible to be stolen at a given moment are stored in a queue which
 * prioritises stealing of the audio data which is less likely to be needed, e.g. if it belongs to a
 * Song that’s no longer loaded. But, to avoid overcomplication, this queue is not adhered to in the
 * case where a neighbouring region of memory is chosen for allocation (or itself being stolen) when
 * the allocation requires that the object in question have its memory stolen too in order to make
 * up a large enough allocation.
 */

class GeneralMemoryAllocator {
public:
	GeneralMemoryAllocator();
	[[gnu::always_inline]] void* allocMaxSpeed(uint32_t requiredSize, void* thingNotToStealFrom = NULL) {
		return alloc(requiredSize, true, false, thingNotToStealFrom);
	}

	[[gnu::always_inline]] void* allocLowSpeed(uint32_t requiredSize, void* thingNotToStealFrom = NULL) {
		return alloc(requiredSize, false, false, thingNotToStealFrom);
	}

	[[gnu::always_inline]] void* allocStealable(uint32_t requiredSize, void* thingNotToStealFrom = NULL) {
		return alloc(requiredSize, false, true, thingNotToStealFrom);
	}

	void* alloc(uint32_t requiredSize, bool mayUseOnChipRam, bool makeStealable, void* thingNotToStealFrom);
	void dealloc(void* address);
	void* allocExternal(uint32_t requiredSize);
	void* allocInternal(uint32_t requiredSize);
	void deallocExternal(void* address);
	uint32_t shortenRight(void* address, uint32_t newSize);
	uint32_t shortenLeft(void* address, uint32_t amountToShorten, uint32_t numBytesToMoveRightIfSuccessful = 0);
	void extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
	            uint32_t* getAmountExtendedLeft, uint32_t* getAmountExtendedRight, void* thingNotToStealFrom = NULL);
	uint32_t extendRightAsMuchAsEasilyPossible(void* address);
	void test();
	uint32_t getAllocatedSize(void* address);
	void checkStack(char const* caller);
	void testShorten(int32_t i);
	int32_t getRegion(void* address);
	void testMemoryDeallocated(void* address);

	void putStealableInQueue(Stealable* stealable, StealableQueue q);
	void putStealableInAppropriateQueue(Stealable* stealable);

	MemoryRegion regions[NUM_MEMORY_REGIONS];
	// only used for managing stealables (audio files that we could deallocate and re load from sd later if needed)
	CacheManager cacheManager;
	bool lock;

	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator generalMemoryAllocator;
		return generalMemoryAllocator;
	}

private:
	void checkEverythingOk(char const* errorString);
};

extern "C" {
void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam = true);
void delugeDealloc(void* address);
}
