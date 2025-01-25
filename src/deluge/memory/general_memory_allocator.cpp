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

#include "memory/general_memory_allocator.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "memory/stealable.h"
#include "processing/engines/audio_engine.h"

// TODO: Check if these have the right size
char emptySpacesMemory[sizeof(EmptySpaceRecord) * 512];
char emptySpacesMemoryInternal[sizeof(EmptySpaceRecord) * 1024];
char emptySpacesMemoryInternalSmall[sizeof(EmptySpaceRecord) * 256];
char emptySpacesMemoryGeneral[sizeof(EmptySpaceRecord) * 256];
char emptySpacesMemoryGeneralSmall[sizeof(EmptySpaceRecord) * 256];
extern uint32_t __sdram_bss_start;
extern uint32_t __sdram_bss_end;
extern uint32_t __heap_start;
extern uint32_t __heap_end;
extern uint32_t program_stack_start;
extern uint32_t program_stack_end;

GeneralMemoryAllocator::GeneralMemoryAllocator() {
	uint32_t stealableStart = (uint32_t)&__sdram_bss_end;
	uint32_t stealableEnd = EXTERNAL_MEMORY_END - RESERVED_EXTERNAL_ALLOCATOR;
	uint32_t externalStart = stealableEnd;
	uint32_t externalEnd = EXTERNAL_MEMORY_END - RESERVED_EXTERNAL_SMALL_ALLOCATOR;
	uint32_t externalSmallStart = externalEnd;
	uint32_t externalSmallEnd = EXTERNAL_MEMORY_END;

	uint32_t internalStart = (uint32_t)&__heap_start;
	uint32_t internalEnd = (uint32_t)&program_stack_start - RESERVED_INTERNAL_SMALL;
	uint32_t internalSmallStart = internalEnd;
	uint32_t internalSmallEnd = (uint32_t)&program_stack_start;
	lock = false;

	regions[MEMORY_REGION_STEALABLE].setup(emptySpacesMemory, sizeof(emptySpacesMemory), stealableStart, stealableEnd,
	                                       &cacheManager);
	regions[MEMORY_REGION_EXTERNAL].setup(emptySpacesMemoryGeneral, sizeof(emptySpacesMemoryGeneral), externalStart,
	                                      externalEnd, nullptr);
	regions[MEMORY_REGION_EXTERNAL_SMALL].setup(emptySpacesMemoryGeneralSmall, sizeof(emptySpacesMemoryGeneralSmall),
	                                            externalSmallStart, externalSmallEnd, nullptr);
	regions[MEMORY_REGION_INTERNAL].minAlign_ = 24;
	regions[MEMORY_REGION_INTERNAL].maxAlign_ = 64;
	regions[MEMORY_REGION_INTERNAL].pivot_ = 64;
	regions[MEMORY_REGION_INTERNAL].setup(emptySpacesMemoryInternal, sizeof(emptySpacesMemoryInternal), internalStart,
	                                      internalEnd, nullptr);
	regions[MEMORY_REGION_INTERNAL_SMALL].setup(emptySpacesMemoryInternalSmall, sizeof(emptySpacesMemoryInternalSmall),
	                                            internalStart, internalEnd, nullptr);

#if ALPHA_OR_BETA_VERSION
	regions[MEMORY_REGION_STEALABLE].name = "stealable";
	regions[MEMORY_REGION_INTERNAL].name = "internal";
	regions[MEMORY_REGION_EXTERNAL].name = "external";
	regions[MEMORY_REGION_EXTERNAL_SMALL].name = "small external";
	regions[MEMORY_REGION_INTERNAL_SMALL].name = "small internal";
#endif
}

int32_t closestDistance = 2147483647;

void GeneralMemoryAllocator::checkStack(char const* caller) {
#if ALPHA_OR_BETA_VERSION

	char a;

	int32_t distance = (int32_t)&a - (uint32_t)&program_stack_start;
	if (distance < closestDistance) {
		closestDistance = distance;

		D_PRINTLN("%d bytes in stack %d free bytes in stack at %s", (uint32_t)&program_stack_end - (int32_t)&a,
		          distance, caller);
		if (distance < 200) {
			FREEZE_WITH_ERROR("E338");
			D_PRINTLN("COLLISION");
		}
	}
#endif
}

#if TEST_GENERAL_MEMORY_ALLOCATION
uint32_t totalMallocTime = 0;
int32_t numMallocTimes = 0;
#endif
extern "C" void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, mayUseOnChipRam, false, nullptr);
}
extern "C" void delugeDealloc(void* address) {
#ifdef IN_UNIT_TESTS
	free(address);
#else
	GeneralMemoryAllocator::get().dealloc(address);
#endif
}
void* GeneralMemoryAllocator::allocExternal(uint32_t requiredSize) {

	if (lock) {
		return nullptr; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they
		                // could extend the stack an unspecified amount
	}

	lock = true;
	void* address = nullptr;
	if (requiredSize < 128) {
		address = regions[MEMORY_REGION_EXTERNAL_SMALL].alloc(requiredSize, false, NULL);
	}
	// if it's a large object or the small object allocator was full stick it in the big one
	if (address == nullptr) {
		address = regions[MEMORY_REGION_EXTERNAL].alloc(requiredSize, false, NULL);
	}
	lock = false;
	if (!address) {
		// FREEZE_WITH_ERROR("M998");
		return nullptr;
	}
	return address;
}

void* GeneralMemoryAllocator::allocInternal(uint32_t requiredSize) {

	if (lock) {
		return nullptr; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they
		                // could extend the stack an unspecified amount
	}

	lock = true;
	void* address = nullptr;
	if (requiredSize < 128) {
		address = regions[MEMORY_REGION_INTERNAL_SMALL].alloc(requiredSize, false, NULL);
	}
	// if it's a large object or the small object allocator was full stick it in the big one
	if (address == nullptr) {
		address = regions[MEMORY_REGION_INTERNAL].alloc(requiredSize, false, NULL);
	}
	lock = false;
	if (!address) {
		// FREEZE_WITH_ERROR("M998");
		return nullptr;
	}
	return address;
}
void GeneralMemoryAllocator::deallocExternal(void* address) {
	return regions[getRegion(address)].dealloc(address);
}

// Watch the heck out - in the older V3.1 branch, this had one less argument - makeStealable was missing - so in code
// from there, thingNotToStealFrom could be interpreted as makeStealable! requiredSize 0 means get biggest allocation
// available.
void* GeneralMemoryAllocator::alloc(uint32_t requiredSize, bool mayUseOnChipRam, bool makeStealable,
                                    void* thingNotToStealFrom) {

	if (lock) {
		return nullptr; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they
		                // could extend the stack an unspecified amount
	}

	void* address = nullptr;

	// Only allow allocating stealables in stelable region
	if (!makeStealable) {
		// If internal is allowed, try that first
		if (mayUseOnChipRam) {
			lock = true;
			address = regions[MEMORY_REGION_INTERNAL].alloc(requiredSize, makeStealable, thingNotToStealFrom);
			lock = false;

			if (address) {
				return address;
			}

			AudioEngine::logAction("internal allocation failed");
		}

		// Second try external region
		address = allocExternal(requiredSize);

		if (address) {
			return address;
		}

		AudioEngine::logAction("external allocation failed");

		D_PRINTLN("Dire memory, resorting to stealable area");
	}

#if TEST_GENERAL_MEMORY_ALLOCATION
	if (requiredSize < 1) {
		D_PRINTLN("alloc too little a bit");
		while (1) {}
	}
#endif

	lock = true;
	address = regions[MEMORY_REGION_STEALABLE].alloc(requiredSize, makeStealable, thingNotToStealFrom);
	lock = false;
	return address;
}

uint32_t GeneralMemoryAllocator::getAllocatedSize(void* address) {
	uint32_t* header = (uint32_t*)((uint32_t)address - 4);
	return (*header & SPACE_SIZE_MASK);
}

int32_t GeneralMemoryAllocator::getRegion(void* address) {
	uint32_t value = (uint32_t)address;
	if (value >= regions[MEMORY_REGION_INTERNAL].start && value < regions[MEMORY_REGION_INTERNAL].end) {
		return MEMORY_REGION_INTERNAL;
	}
	else if (value >= regions[MEMORY_REGION_STEALABLE].start && value < regions[MEMORY_REGION_STEALABLE].end) {
		return MEMORY_REGION_STEALABLE;
	}
	else if (value >= regions[MEMORY_REGION_EXTERNAL].start && value < regions[MEMORY_REGION_EXTERNAL].end) {
		return MEMORY_REGION_EXTERNAL;
	}
	else if (value >= regions[MEMORY_REGION_EXTERNAL_SMALL].start
	         && value < regions[MEMORY_REGION_EXTERNAL_SMALL].end) {
		return MEMORY_REGION_EXTERNAL_SMALL;
	}

	FREEZE_WITH_ERROR("E339");
	return 0;
}

// Returns new size
uint32_t GeneralMemoryAllocator::shortenRight(void* address, uint32_t newSize) {
	return regions[getRegion(address)].shortenRight(address, newSize);
}

// Returns how much it was shortened by
uint32_t GeneralMemoryAllocator::shortenLeft(void* address, uint32_t amountToShorten,
                                             uint32_t numBytesToMoveRightIfSuccessful) {
	return regions[getRegion(address)].shortenLeft(address, amountToShorten, numBytesToMoveRightIfSuccessful);
}

void GeneralMemoryAllocator::extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
                                    uint32_t* __restrict__ getAmountExtendedLeft,
                                    uint32_t* __restrict__ getAmountExtendedRight, void* thingNotToStealFrom) {

	*getAmountExtendedLeft = 0;
	*getAmountExtendedRight = 0;

	if (lock) {
		return;
	}

	lock = true;
	regions[getRegion(address)].extend(address, minAmountToExtend, idealAmountToExtend, getAmountExtendedLeft,
	                                   getAmountExtendedRight, thingNotToStealFrom);
	lock = false;
}

uint32_t GeneralMemoryAllocator::extendRightAsMuchAsEasilyPossible(void* address) {
	return regions[getRegion(address)].extendRightAsMuchAsEasilyPossible(address);
}

void GeneralMemoryAllocator::dealloc(void* address) {
	if (address == nullptr) [[unlikely]] {
		return;
	}
	regions[getRegion(address)].dealloc(address);
}

void GeneralMemoryAllocator::putStealableInQueue(Stealable* stealable, StealableQueue q) {
	MemoryRegion& region = regions[getRegion(stealable)];
	region.cache_manager().QueueForReclamation(q, stealable);
}

void GeneralMemoryAllocator::putStealableInAppropriateQueue(Stealable* stealable) {
	StealableQueue q = stealable->getAppropriateQueue();
	putStealableInQueue(stealable, q);
}
