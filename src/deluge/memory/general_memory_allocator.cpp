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
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/stealable.h"
#include "processing/engines/audio_engine.h"
#include "storage/cluster/cluster.h"
#include "util/functions.h"

// TODO: Check if these have the right size
char emptySpacesMemory[sizeof(EmptySpaceRecord) * 512];
char emptySpacesMemoryInternal[sizeof(EmptySpaceRecord) * 1024];
char emptySpacesMemoryGeneral[sizeof(EmptySpaceRecord) * 256];
extern uint32_t __sdram_bss_start;
extern uint32_t __sdram_bss_end;
extern uint32_t __heap_start;
extern uint32_t __heap_end;
extern uint32_t program_stack_start;
extern uint32_t program_stack_end;
GeneralMemoryAllocator::GeneralMemoryAllocator() {
	lock = false;

	regions[MEMORY_REGION_STEALABLE].setup(emptySpacesMemory, sizeof(emptySpacesMemory), (uint32_t)&__sdram_bss_end,
	                                       EXTERNAL_MEMORY_END - RESERVED_EXTERNAL_ALLOCATOR);
	regions[MEMORY_REGION_EXTERNAL].setup(emptySpacesMemoryGeneral, sizeof(emptySpacesMemoryGeneral),
	                                      EXTERNAL_MEMORY_END - RESERVED_EXTERNAL_ALLOCATOR, EXTERNAL_MEMORY_END);
	regions[MEMORY_REGION_INTERNAL].setup(emptySpacesMemoryInternal, sizeof(emptySpacesMemoryInternal),
	                                      (uint32_t)&__heap_start, (uint32_t)&program_stack_start);

#if ALPHA_OR_BETA_VERSION
	regions[MEMORY_REGION_STEALABLE].name = "stealable";
	regions[MEMORY_REGION_INTERNAL].name = "internal";
	regions[MEMORY_REGION_EXTERNAL].name = "external";
#endif
}

int32_t closestDistance = 2147483647;

void GeneralMemoryAllocator::checkStack(char const* caller) {
#if ALPHA_OR_BETA_VERSION

	char a;

	int32_t distance = (int32_t)&a - (uint32_t)&program_stack_start;
	if (distance < closestDistance) {
		closestDistance = distance;
		D_PRINT("%d bytes in stack %d free bytes in stack at %x", (uint32_t)&program_stack_end - (int32_t)&a, distance,
		        caller);

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
		return NULL; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they
		             // could extend the stack an unspecified amount
	}

	lock = true;
	void* address = regions[MEMORY_REGION_EXTERNAL].alloc(requiredSize, false, NULL);
	lock = false;
	if (!address) {
		// FREEZE_WITH_ERROR("M998");
		return nullptr;
	}
	return address;
}
void GeneralMemoryAllocator::deallocExternal(void* address) {
	return regions[MEMORY_REGION_EXTERNAL].dealloc(address);
}

// Watch the heck out - in the older V3.1 branch, this had one less argument - makeStealable was missing - so in code
// from there, thingNotToStealFrom could be interpreted as makeStealable! requiredSize 0 means get biggest allocation
// available.
void* GeneralMemoryAllocator::alloc(uint32_t requiredSize, bool mayUseOnChipRam, bool makeStealable,
                                    void* thingNotToStealFrom) {

	if (lock) {
		return NULL; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they
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
		lock = true;
		address = regions[MEMORY_REGION_EXTERNAL].alloc(requiredSize, makeStealable, thingNotToStealFrom);
		lock = false;

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
	return regions[getRegion(address)].dealloc(address);
}

void GeneralMemoryAllocator::putStealableInQueue(Stealable* stealable, int32_t q) {
	MemoryRegion& region = regions[getRegion(stealable)];
	region.cache_manager().QueueForReclamation(q, stealable);
}

void GeneralMemoryAllocator::putStealableInAppropriateQueue(Stealable* stealable) {
	int32_t q = stealable->getAppropriateQueue();
	putStealableInQueue(stealable, q);
}

#if TEST_GENERAL_MEMORY_ALLOCATION

#define NUM_TEST_ALLOCATIONS 512
void* testAllocations[NUM_TEST_ALLOCATIONS];
uint32_t sizes[NUM_TEST_ALLOCATIONS];
uint32_t spaceTypes[NUM_TEST_ALLOCATIONS];
uint32_t vtableAddress;

class StealableTest : public Stealable {
public:
	void steal(char const* errorCode) {
		// Stealable::steal();
		testAllocations[testIndex] = 0;
		GeneralMemoryAllocator::get().regions[GeneralMemoryAllocator::get().getRegion(this)].numAllocations--;

		// The steal() function is allowed to deallocate or shorten some other allocations, too
		int32_t i = getRandom255() % NUM_TEST_ALLOCATIONS;
		if (testAllocations[i]) {
			int32_t a = getRandom255();

			// Dealloc
			if (spaceTypes[i] == SPACE_HEADER_STEALABLE || a < 128) {

				if (spaceTypes[i] == SPACE_HEADER_STEALABLE) {
					((Stealable*)testAllocations[i])->~Stealable();
				}
				delugeDealloc(testAllocations[i]);
				testAllocations[i] = NULL;
			}

			else {
				// Shorten
				GeneralMemoryAllocator::get().testShorten(i);
			}
		}
	}
	bool mayBeStolen(void* thingNotToStealFrom) { return true; }
	int32_t getAppropriateQueue() { return 0; }
	int32_t testIndex;
};

void testReadingMemory(int32_t i) {
	uint8_t* __restrict__ readPos = (uint8_t*)testAllocations[i];
	uint8_t readValue = *readPos;
	for (int32_t j = 0; j < sizes[i]; j++) {
		if (*readPos != readValue) {
			D_PRINTLN("data corrupted! readPos %d", (int32_t)readPos);
			D_PRINTLN("allocation total size:  %d num bytes in:  %d", sizes[i],
			          (int32_t)readPos - (int32_t)testAllocations[i]);
			while (1) {}
		}
		readPos++;
		readValue++;
	}
}

void testWritingMemory(int32_t i) {
	uint8_t* __restrict__ writePos = (uint8_t*)testAllocations[i];
	uint8_t writeValue = getRandom255();
	for (int32_t j = 0; j < sizes[i]; j++) {
		*writePos = writeValue;
		writeValue++;
		writePos++;
	}
}

bool skipConsistencyCheck = false; // Sometimes we want to make sure this check isn't happen, while things temporarily
                                   // are not in an inspectable state

void GeneralMemoryAllocator::checkEverythingOk(char const* errorString) {
	if (skipConsistencyCheck)
		return;

	for (int32_t i = 0; i < NUM_TEST_ALLOCATIONS; i++) {
		if (testAllocations[i]) {
			uint32_t* header = (uint32_t*)testAllocations[i] - 1;
			uint32_t* footer = (uint32_t*)((int32_t)testAllocations[i] + sizes[i]);

			uint32_t shouldBe = sizes[i] | spaceTypes[i];

			if (*header != shouldBe) {
				D_PRINTLN("allocation header wrong");
				D_PRINTLN(errorString);
				D_PRINTLN(*header);
				D_PRINTLN(shouldBe);
				while (1) {}
			}
			if (*footer != shouldBe) {
				D_PRINTLN("allocation footer wrong");
				D_PRINTLN(errorString);
				while (1) {}
			}
			if (spaceTypes[i] == SPACE_HEADER_STEALABLE && *(header + 1) != vtableAddress) {
				D_PRINT("vtable address corrupted: %s", errorString);
				while (1) {}
			}
		}
	}

	for (int32_t i = 0; i < regions[MEMORY_REGION_STEALABLE].emptySpaces.getNumElements(); i++) {
		EmptySpaceRecord* record = (EmptySpaceRecord*)regions[MEMORY_REGION_STEALABLE].emptySpaces.getElementAddress(i);

		uint32_t* header = (uint32_t*)record->address - 1;
		uint32_t* footer = (uint32_t*)(record->address + record->length);

		uint32_t shouldBe = record->length | SPACE_HEADER_EMPTY;

		if (*header != shouldBe) {
			D_PRINTLN("empty space header wrong: %s", errorString);
			while (1) {}
		}
		if (*footer != shouldBe) {
			D_PRINTLN("empty space footer wrong: %s", errorString);
			while (1) {}
		}
	}
}

void GeneralMemoryAllocator::testMemoryDeallocated(void* address) {
	for (int32_t i = 0; i < NUM_TEST_ALLOCATIONS; i++) {
		if (testAllocations[i] == address) {
			testAllocations[i] = NULL;
		}
	}
}

void GeneralMemoryAllocator::testShorten(int32_t i) {
	int32_t a = getRandom255();

	if (a < 128) {

		if (!getRandom255())
			D_PRINTLN("shortening left");
		int32_t newSize =
		    ((uint32_t)getRandom255() << 17) | ((uint32_t)getRandom255() << 9) | ((uint32_t)getRandom255() << 1);
		while (newSize > sizes[i])
			newSize >>= 1;
		int32_t amountShortened = shortenLeft(testAllocations[i], sizes[i] - newSize);

		sizes[i] -= amountShortened;
		testAllocations[i] = (char*)testAllocations[i] + amountShortened;

		checkEverythingOk("after shortening left");
	}

	else {

		if (!getRandom255())
			D_PRINTLN("shortening right");
		int32_t newSize =
		    ((uint32_t)getRandom255() << 17) | ((uint32_t)getRandom255() << 9) | ((uint32_t)getRandom255() << 1);
		while (newSize > sizes[i])
			newSize >>= 1;
		sizes[i] = shortenRight(testAllocations[i], newSize);

		checkEverythingOk("after shortening right");
	}
}

void GeneralMemoryAllocator::test() {

	D_PRINTLN("GeneralMemoryAllocator::test()");

	memset(testAllocations, 0, sizeof(testAllocations));

	int32_t count = 0;

	bool goingUp = true;

	while (1) {
		// if (!(count & 15)) D_PRINTLN("...");
		count++;

		for (int32_t i = 0; i < NUM_TEST_ALLOCATIONS; i++) {

			if (testAllocations[i]) {

				// Check data is still there
				if (spaceTypes[i] != SPACE_HEADER_STEALABLE)
					testReadingMemory(i);

				if (spaceTypes[i] == SPACE_HEADER_STEALABLE
				    //|| (uint32_t)testAllocations[i] >= (uint32_t)INTERNAL_MEMORY_BEGIN // If on-chip memory, this is
				    // the only option
				    || getRandom255() < 128) {

					if (spaceTypes[i] == SPACE_HEADER_STEALABLE) {
						((Stealable*)testAllocations[i])->~Stealable();
					}
					dealloc(testAllocations[i]);
					testAllocations[i] = NULL;

					checkEverythingOk("after deallocating");
				}

				else {

					uint8_t a = getRandom255();

					if (a < 192) {
						testShorten(i);
					}

					else {
						if (!getRandom255())
							D_PRINTLN("extending");
						uint32_t amountExtendedLeft, amountExtendedRight;

						uint32_t idealAmountToExtend = ((uint32_t)getRandom255() << 17)
						                               | ((uint32_t)getRandom255() << 9)
						                               | ((uint32_t)getRandom255() << 1);
						int32_t magnitudeReduction = getRandom255() % 25;
						idealAmountToExtend >>= magnitudeReduction;

						idealAmountToExtend = std::max(idealAmountToExtend, (uint32_t)4);
						int32_t magnitudeReduction2 = getRandom255() % 25;
						uint32_t minAmountToExtend = idealAmountToExtend >> magnitudeReduction2;
						minAmountToExtend = std::max(minAmountToExtend, (uint32_t)4);

						checkEverythingOk("before extending");

						void* allocationAddress = testAllocations[i];
						testAllocations[i] = NULL; // Set this to NULL temporarily so we don't do deallocate or shorten
						                           // it during a steal()

						extend(allocationAddress, minAmountToExtend, idealAmountToExtend, &amountExtendedLeft,
						       &amountExtendedRight);

						testAllocations[i] = allocationAddress;

						uint32_t amountExtended = amountExtendedLeft + amountExtendedRight;

						// Check the old memory is still intact
						testReadingMemory(i);

						if (amountExtended > 0) {
							if (amountExtended < minAmountToExtend) {
								D_PRINTLN("extended too little!");
								while (1) {}
							}
						}

						testAllocations[i] -= amountExtendedLeft;
						sizes[i] += amountExtended;

						checkEverythingOk("after extending");

						// Write to new, expanded memory
						testWritingMemory(i);
					}
				}
			}

			if (getRandom255() < 2) {
				D_PRINTLN("\nfree spaces:  %d allocations:  %d",
				          regions[MEMORY_REGION_STEALABLE].emptySpaces.getNumElements(),
				          regions[MEMORY_REGION_STEALABLE].numAllocations);

				if (regions[MEMORY_REGION_STEALABLE].emptySpaces.getNumElements() == 1) {
					EmptySpaceRecord* firstRecord =
					    (EmptySpaceRecord*)regions[MEMORY_REGION_STEALABLE].emptySpaces.getElementAddress(0);
					D_PRINTLN("free space size:  %d free space address:  %d", firstRecord->length,
					          firstRecord->address);
				}
				delayMS(200);
			}

			int32_t desiredSize =
			    ((uint32_t)getRandom255() << 9) | ((uint32_t)getRandom255() << 1); // (uint32_t)getRandom255() << 17) |

			int32_t magnitudeReduction = getRandom255() % 10;
			desiredSize >>= magnitudeReduction;

			if (desiredSize < 1)
				desiredSize = 1;

			uint32_t actualSize;
			if (!testAllocations[i]) {

				bool makeStealable = false;
				if (desiredSize >= sizeof(StealableTest))
					makeStealable = getRandom255() & 1;

				testAllocations[i] = alloc(desiredSize, &actualSize, false, true, makeStealable);
				if (testAllocations[i]) {

					// if ((uint32_t)testAllocations[i] >= (uint32_t)INTERNAL_MEMORY_BEGIN) actualSize = desiredSize; //
					// If on-chip memory

					if (actualSize < desiredSize) {
						D_PRINTLN("got too little!!");
						D_PRINTLN(desiredSize - actualSize);
						while (1) {}
					}

					sizes[i] = actualSize;

					if (makeStealable) {
						spaceTypes[i] = SPACE_HEADER_STEALABLE;
						StealableTest* stealable = new (testAllocations[i]) StealableTest();
						stealable->testIndex = i;

						// regions[getRegion(stealable)].stealableClusterQueues[0].addToEnd(stealable);
						putStealableInQueue(stealable, 0);
						vtableAddress = *(uint32_t*)testAllocations[i];
					}
					else {
						spaceTypes[i] = SPACE_HEADER_ALLOCATED;
						testWritingMemory(i);
					}
				}

				checkEverythingOk("after allocating");
			}
		}

		goingUp = !goingUp;
	}
}

#endif
