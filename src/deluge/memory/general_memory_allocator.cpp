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
#include "drivers/mtu/mtu.h"
#include "hid/display/numeric_driver.h"
#include "io/debug/print.h"
#include "memory/stealable.h"
#include "model/action/action_logger.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "util/functions.h"
#include <cstring>
#include <new>

char emptySpacesMemory[sizeof(EmptySpaceRecord) * 512];
char emptySpacesMemoryInternal[sizeof(EmptySpaceRecord) * 1024];

extern uint32_t __heap_start;
extern uint32_t __heap_end;

GeneralMemoryAllocator::GeneralMemoryAllocator() {
	lock = false;
	regions[MEMORY_REGION_SDRAM].setup(emptySpacesMemory, sizeof(emptySpacesMemory), EXTERNAL_MEMORY_BEGIN,
	                                   EXTERNAL_MEMORY_END);
	regions[MEMORY_REGION_INTERNAL].setup(emptySpacesMemoryInternal, sizeof(emptySpacesMemoryInternal),
	                                      (uint32_t)&__heap_start, kInternalMemoryEnd - 8192);

#if ALPHA_OR_BETA_VERSION
	regions[MEMORY_REGION_SDRAM].name = "external";
	regions[MEMORY_REGION_INTERNAL].name = "internal";
#endif
}

int32_t closestDistance = 2147483647;

void GeneralMemoryAllocator::checkStack(char const* caller) {
#if ALPHA_OR_BETA_VERSION

	char a;

	int32_t distance = (int32_t)&a - (kInternalMemoryEnd - kProgramStackMaxSize);
	if (distance < closestDistance) {
		closestDistance = distance;

		Debug::print(distance);
		Debug::print(" free bytes in stack at ");
		Debug::println(caller);

		if (distance < 200) {
			Debug::println("COLLISION");
			numericDriver.freezeWithError("E338");
		}
	}
#endif
}

#if TEST_GENERAL_MEMORY_ALLOCATION
uint32_t totalMallocTime = 0;
int32_t numMallocTimes = 0;
#endif

extern "C" void* delugeAlloc(unsigned int requiredSize) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, NULL, false, true);
}

// Watch the heck out - in the older V3.1 branch, this had one less argument - makeStealable was missing - so in code from there, thingNotToStealFrom could be interpreted as makeStealable!
// requiredSize 0 means get biggest allocation available.
void* GeneralMemoryAllocator::alloc(uint32_t requiredSize, uint32_t* getAllocatedSize, bool mayDeleteFirstUndoAction,
                                    bool mayUseOnChipRam, bool makeStealable, void* thingNotToStealFrom,
                                    bool getBiggestAllocationPossible) {

	if (lock) {
		return NULL; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they could extend the stack an unspecified amount
	}

	if (mayUseOnChipRam
#if TEST_GENERAL_MEMORY_ALLOCATION
	    && getRandom255() < 128
#endif
	) {
		lock = true;
		//uint16_t startTime = *TCNT[TIMER_SYSTEM_FAST];
		void* address = regions[MEMORY_REGION_INTERNAL].alloc(requiredSize, getAllocatedSize, makeStealable,
		                                                      thingNotToStealFrom, getBiggestAllocationPossible);
		//uint16_t endTime = *TCNT[TIMER_SYSTEM_FAST];
		lock = false;
		if (address) {

			/*
			uint16_t timeTaken = endTime - startTime;
			totalMallocTime += timeTaken;
			numMallocTimes++;

			Debug::print("average malloc time: ");
			Debug::println(totalMallocTime / numMallocTimes);

			//Debug::print("total: ");
			//Debug::println(totalMallocTime);
			*/
			return address;
		}
	}

#if TEST_GENERAL_MEMORY_ALLOCATION
	if (requiredSize < 1) {
		Debug::println("alloc too little a bit");
		while (1) {}
	}
#endif

	lock = true;
	void* address = regions[MEMORY_REGION_SDRAM].alloc(requiredSize, getAllocatedSize, makeStealable,
	                                                   thingNotToStealFrom, getBiggestAllocationPossible);
	lock = false;
	return address;
}

uint32_t GeneralMemoryAllocator::getAllocatedSize(void* address) {
	uint32_t* header = (uint32_t*)((uint32_t)address - 4);
	return (*header & SPACE_SIZE_MASK);
}

int32_t GeneralMemoryAllocator::getRegion(void* address) {
	return ((uint32_t)address >= (uint32_t)INTERNAL_MEMORY_BEGIN) ? MEMORY_REGION_INTERNAL : MEMORY_REGION_SDRAM;
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

extern "C" void delugeDealloc(void* address) {
	GeneralMemoryAllocator::get().dealloc(address);
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

#define NUM_TEST_ALLOCATIONS 64
void* testAllocations[NUM_TEST_ALLOCATIONS];
uint32_t sizes[NUM_TEST_ALLOCATIONS];
uint32_t spaceTypes[NUM_TEST_ALLOCATIONS];
uint32_t vtableAddress;

class StealableTest : public Stealable {
public:
	void steal() {
		//Stealable::steal();
		testAllocations[testIndex] = 0;
		GeneralMemoryAllocator::get().regions[MEMORY_REGION_SDRAM].numAllocations--;

		// The steal() function is allowed to deallocate or shorten some other allocations, too
		int32_t i = getRandom255() % NUM_TEST_ALLOCATIONS;
		if (testAllocations[i]) {
			int32_t a = getRandom255();

			// Dealloc
			if (spaceTypes[i] == SPACE_HEADER_STEALABLE || a < 128) {

				if (spaceTypes[i] == SPACE_HEADER_STEALABLE) {
					((Stealable*)testAllocations[i])->~Stealable();
				}
				GeneralMemoryAllocator::get().dealloc(testAllocations[i]);
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
			Debug::println("data corrupted!");
			Debug::println((int32_t)readPos);
			Debug::print("allocation total size: ");
			Debug::println(sizes[i]);
			Debug::print("num bytes in: ");
			Debug::println((int32_t)readPos - (int32_t)testAllocations[i]);
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

bool skipConsistencyCheck =
    false; // Sometimes we want to make sure this check isn't happen, while things temporarily are not in an inspectable state

void GeneralMemoryAllocator::checkEverythingOk(char const* errorString) {
	if (skipConsistencyCheck)
		return;

	for (int32_t i = 0; i < NUM_TEST_ALLOCATIONS; i++) {
		if (testAllocations[i]) {
			uint32_t* header = (uint32_t*)testAllocations[i] - 1;
			uint32_t* footer = (uint32_t*)((int32_t)testAllocations[i] + sizes[i]);

			uint32_t shouldBe = sizes[i] | spaceTypes[i];

			if (*header != shouldBe) {
				Debug::println("allocation header wrong");
				Debug::println(errorString);
				Debug::println(*header);
				Debug::println(shouldBe);
				while (1) {}
			}
			if (*footer != shouldBe) {
				Debug::println("allocation footer wrong");
				Debug::println(errorString);
				while (1) {}
			}
			if (spaceTypes[i] == SPACE_HEADER_STEALABLE && *(header + 1) != vtableAddress) {
				Debug::println("vtable address corrupted");
				Debug::println(errorString);
				while (1) {}
			}
		}
	}

	for (int32_t i = 0; i < regions[MEMORY_REGION_SDRAM].emptySpaces.getNumElements(); i++) {
		EmptySpaceRecord* record = (EmptySpaceRecord*)regions[MEMORY_REGION_SDRAM].emptySpaces.getElementAddress(i);

		uint32_t* header = (uint32_t*)record->address - 1;
		uint32_t* footer = (uint32_t*)(record->address + record->length);

		uint32_t shouldBe = record->length | SPACE_HEADER_EMPTY;

		if (*header != shouldBe) {
			Debug::println("empty space header wrong");
			Debug::println(errorString);
			while (1) {}
		}
		if (*footer != shouldBe) {
			Debug::println("empty space footer wrong");
			Debug::println(errorString);
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
			Debug::println("shortening left");
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
			Debug::println("shortening right");
		int32_t newSize =
		    ((uint32_t)getRandom255() << 17) | ((uint32_t)getRandom255() << 9) | ((uint32_t)getRandom255() << 1);
		while (newSize > sizes[i])
			newSize >>= 1;
		sizes[i] = shortenRight(testAllocations[i], newSize);

		checkEverythingOk("after shortening right");
	}
}

void GeneralMemoryAllocator::test() {

	Debug::println("GeneralMemoryAllocator::test()");

	// Corrupt the crap out of these two so we know they can take it!
	sampleManager.clusterSize = 0;
	sampleManager.clusterSizeMagnitude = 0;

	memset(testAllocations, 0, sizeof(testAllocations));

	int32_t count = 0;

	bool goingUp = true;

	while (1) {
		//if (!(count & 15)) Debug::println("...");
		count++;

		for (int32_t i = 0; i < NUM_TEST_ALLOCATIONS; i++) {

			if (testAllocations[i]) {

				// Check data is still there
				if (spaceTypes[i] != SPACE_HEADER_STEALABLE)
					testReadingMemory(i);

				if (spaceTypes[i] == SPACE_HEADER_STEALABLE
				    //|| (uint32_t)testAllocations[i] >= (uint32_t)INTERNAL_MEMORY_BEGIN // If on-chip memory, this is the only option
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
							Debug::println("extending");
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
						testAllocations[i] =
						    NULL; // Set this to NULL temporarily so we don't do deallocate or shorten it during a steal()

						extend(allocationAddress, minAmountToExtend, idealAmountToExtend, &amountExtendedLeft,
						       &amountExtendedRight);

						testAllocations[i] = allocationAddress;

						uint32_t amountExtended = amountExtendedLeft + amountExtendedRight;

						// Check the old memory is still intact
						testReadingMemory(i);

						if (amountExtended > 0) {
							if (amountExtended < minAmountToExtend) {
								Debug::println("extended too little!");
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
				Debug::print("\nfree spaces: ");
				Debug::println(regions[MEMORY_REGION_SDRAM].emptySpaces.getNumElements());
				Debug::print("allocations: ");
				Debug::println(regions[MEMORY_REGION_SDRAM].numAllocations);

				if (regions[MEMORY_REGION_SDRAM].emptySpaces.getNumElements() == 1) {
					EmptySpaceRecord* firstRecord =
					    (EmptySpaceRecord*)regions[MEMORY_REGION_SDRAM].emptySpaces.getElementAddress(0);
					Debug::print("free space size: ");
					Debug::println(firstRecord->length);
					Debug::print("free space address: ");
					Debug::println(firstRecord->address);
				}
				delayMS(200);
			}

			int32_t desiredSize =
			    ((uint32_t)getRandom255() << 9) | ((uint32_t)getRandom255() << 1); // (uint32_t)getRandom255() << 17) |

			int32_t magnitudeReduction = getRandom255() % 25;
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

					//if ((uint32_t)testAllocations[i] >= (uint32_t)INTERNAL_MEMORY_BEGIN) actualSize = desiredSize; // If on-chip memory

					if (actualSize < desiredSize) {
						Debug::println("got too little!!");
						Debug::println(desiredSize - actualSize);
						while (1) {}
					}

					sizes[i] = actualSize;

					if (makeStealable) {
						spaceTypes[i] = SPACE_HEADER_STEALABLE;
						StealableTest* stealable = new (testAllocations[i]) StealableTest();
						stealable->testIndex = i;

						regions[getRegion(stealable)].stealableClusterQueues[0].addToEnd(stealable);

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
