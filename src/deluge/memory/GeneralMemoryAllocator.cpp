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

#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <Cluster.h>
#include "GeneralMemoryAllocator.h"
#include <new>
#include "uart.h"
#include "functions.h"
#include <string.h>
#include "ActionLogger.h"
#include "numericdriver.h"
#include "Stealable.h"
#include "mtu_all_cpus.h"
#include "definitions.h"

char emptySpacesMemory[sizeof(EmptySpaceRecord) * 512];
char emptySpacesMemoryInternal[sizeof(EmptySpaceRecord) * 1024];

extern uint32_t __heap_start;
extern uint32_t __heap_end;

GeneralMemoryAllocator generalMemoryAllocator{};

GeneralMemoryAllocator::GeneralMemoryAllocator() {
	lock = false;
	regions[MEMORY_REGION_SDRAM].setup(emptySpacesMemory, sizeof(emptySpacesMemory), EXTERNAL_MEMORY_BEGIN,
	                                   EXTERNAL_MEMORY_END);
	regions[MEMORY_REGION_INTERNAL].setup(emptySpacesMemoryInternal, sizeof(emptySpacesMemoryInternal),
	                                      (uint32_t)&__heap_start, INTERNAL_MEMORY_END - 8192);

#if ALPHA_OR_BETA_VERSION
	regions[MEMORY_REGION_SDRAM].name = "external";
	regions[MEMORY_REGION_INTERNAL].name = "internal";
#endif
}

int closestDistance = 2147483647;

void GeneralMemoryAllocator::checkStack(char const* caller) {
#if ALPHA_OR_BETA_VERSION

	char a;

	int distance = (int)&a - (INTERNAL_MEMORY_END - PROGRAM_STACK_MAX_SIZE);
	if (distance < closestDistance) {
		closestDistance = distance;

		Uart::print(distance);
		Uart::print(" free bytes in stack at ");
		Uart::println(caller);

		if (distance < 200) {
			Uart::println("COLLISION");
			numericDriver.freezeWithError("E338");
		}
	}
#endif
}

#if TEST_GENERAL_MEMORY_ALLOCATION
uint32_t totalMallocTime = 0;
int numMallocTimes = 0;
#endif

extern "C" void* delugeAlloc(int requiredSize) {
	return generalMemoryAllocator.alloc(requiredSize, NULL, false, true);
}

// Watch the heck out - in the older V3.1 branch, this had one less argument - makeStealable was missing - so in code from there, thingNotToStealFrom could be interpreted as makeStealable!
// requiredSize 0 means get biggest allocation available.
void* GeneralMemoryAllocator::alloc(uint32_t requiredSize, uint32_t* getAllocatedSize, bool mayDeleteFirstUndoAction,
                                    bool mayUseOnChipRam, bool makeStealable, void* thingNotToStealFrom,
                                    bool getBiggestAllocationPossible) {

	if (lock)
		return NULL; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they could extend the stack an unspecified amount

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

			Uart::print("average malloc time: ");
			Uart::println(totalMallocTime / numMallocTimes);

			//Uart::print("total: ");
			//Uart::println(totalMallocTime);
			*/
			return address;
		}
	}

#if TEST_GENERAL_MEMORY_ALLOCATION
	if (requiredSize < 1) {
		Uart::println("alloc too little a bit");
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

int GeneralMemoryAllocator::getRegion(void* address) {
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

	if (lock) return;

	lock = true;
	regions[getRegion(address)].extend(address, minAmountToExtend, idealAmountToExtend, getAmountExtendedLeft,
	                                   getAmountExtendedRight, thingNotToStealFrom);
	lock = false;
}

uint32_t GeneralMemoryAllocator::extendRightAsMuchAsEasilyPossible(void* address) {
	return regions[getRegion(address)].extendRightAsMuchAsEasilyPossible(address);
}

extern "C" void delugeDealloc(void* address) {
	generalMemoryAllocator.dealloc(address);
}

void GeneralMemoryAllocator::dealloc(void* address) {
	return regions[getRegion(address)].dealloc(address);
}

void GeneralMemoryAllocator::putStealableInQueue(Stealable* stealable, int q) {
	MemoryRegion* region = &regions[getRegion(stealable)];
	region->stealableClusterQueues[q].addToEnd(stealable);
	region->stealableClusterQueueLongestRuns[q] = 0xFFFFFFFF; // TODO: actually investigate neighbouring memory "run".
}

void GeneralMemoryAllocator::putStealableInAppropriateQueue(Stealable* stealable) {
	int q = stealable->getAppropriateQueue();
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
		generalMemoryAllocator.regions[MEMORY_REGION_SDRAM].numAllocations--;

		// The steal() function is allowed to deallocate or shorten some other allocations, too
		int i = getRandom255() % NUM_TEST_ALLOCATIONS;
		if (testAllocations[i]) {
			int a = getRandom255();

			// Dealloc
			if (spaceTypes[i] == SPACE_HEADER_STEALABLE || a < 128) {

				if (spaceTypes[i] == SPACE_HEADER_STEALABLE) {
					((Stealable*)testAllocations[i])->~Stealable();
				}
				generalMemoryAllocator.dealloc(testAllocations[i]);
				testAllocations[i] = NULL;
			}

			else {
				// Shorten
				generalMemoryAllocator.testShorten(i);
			}
		}
	}
	bool mayBeStolen(void* thingNotToStealFrom) { return true; }
	int getAppropriateQueue() { return 0; }
	int testIndex;
};

void testReadingMemory(int i) {
	uint8_t* __restrict__ readPos = (uint8_t*)testAllocations[i];
	uint8_t readValue = *readPos;
	for (int j = 0; j < sizes[i]; j++) {
		if (*readPos != readValue) {
			Uart::println("data corrupted!");
			Uart::println((int)readPos);
			Uart::print("allocation total size: ");
			Uart::println(sizes[i]);
			Uart::print("num bytes in: ");
			Uart::println((int)readPos - (int)testAllocations[i]);
			while (1) {}
		}
		readPos++;
		readValue++;
	}
}

void testWritingMemory(int i) {
	uint8_t* __restrict__ writePos = (uint8_t*)testAllocations[i];
	uint8_t writeValue = getRandom255();
	for (int j = 0; j < sizes[i]; j++) {
		*writePos = writeValue;
		writeValue++;
		writePos++;
	}
}

bool skipConsistencyCheck =
    false; // Sometimes we want to make sure this check isn't happen, while things temporarily are not in an inspectable state

void GeneralMemoryAllocator::checkEverythingOk(char const* errorString) {
	if (skipConsistencyCheck) return;

	for (int i = 0; i < NUM_TEST_ALLOCATIONS; i++) {
		if (testAllocations[i]) {
			uint32_t* header = (uint32_t*)testAllocations[i] - 1;
			uint32_t* footer = (uint32_t*)((int)testAllocations[i] + sizes[i]);

			uint32_t shouldBe = sizes[i] | spaceTypes[i];

			if (*header != shouldBe) {
				Uart::println("allocation header wrong");
				Uart::println(errorString);
				Uart::println(*header);
				Uart::println(shouldBe);
				while (1) {}
			}
			if (*footer != shouldBe) {
				Uart::println("allocation footer wrong");
				Uart::println(errorString);
				while (1) {}
			}
			if (spaceTypes[i] == SPACE_HEADER_STEALABLE && *(header + 1) != vtableAddress) {
				Uart::println("vtable address corrupted");
				Uart::println(errorString);
				while (1) {}
			}
		}
	}

	for (int i = 0; i < regions[MEMORY_REGION_SDRAM].emptySpaces.getNumElements(); i++) {
		EmptySpaceRecord* record = (EmptySpaceRecord*)regions[MEMORY_REGION_SDRAM].emptySpaces.getElementAddress(i);

		uint32_t* header = (uint32_t*)record->address - 1;
		uint32_t* footer = (uint32_t*)(record->address + record->length);

		uint32_t shouldBe = record->length | SPACE_HEADER_EMPTY;

		if (*header != shouldBe) {
			Uart::println("empty space header wrong");
			Uart::println(errorString);
			while (1) {}
		}
		if (*footer != shouldBe) {
			Uart::println("empty space footer wrong");
			Uart::println(errorString);
			while (1) {}
		}
	}
}

void GeneralMemoryAllocator::testMemoryDeallocated(void* address) {
	for (int i = 0; i < NUM_TEST_ALLOCATIONS; i++) {
		if (testAllocations[i] == address) {
			testAllocations[i] = NULL;
		}
	}
}

void GeneralMemoryAllocator::testShorten(int i) {
	int a = getRandom255();

	if (a < 128) {

		if (!getRandom255()) Uart::println("shortening left");
		int newSize =
		    ((uint32_t)getRandom255() << 17) | ((uint32_t)getRandom255() << 9) | ((uint32_t)getRandom255() << 1);
		while (newSize > sizes[i])
			newSize >>= 1;
		int amountShortened = shortenLeft(testAllocations[i], sizes[i] - newSize);

		sizes[i] -= amountShortened;
		testAllocations[i] = (char*)testAllocations[i] + amountShortened;

		checkEverythingOk("after shortening left");
	}

	else {

		if (!getRandom255()) Uart::println("shortening right");
		int newSize =
		    ((uint32_t)getRandom255() << 17) | ((uint32_t)getRandom255() << 9) | ((uint32_t)getRandom255() << 1);
		while (newSize > sizes[i])
			newSize >>= 1;
		sizes[i] = shortenRight(testAllocations[i], newSize);

		checkEverythingOk("after shortening right");
	}
}

void GeneralMemoryAllocator::test() {

	Uart::println("GeneralMemoryAllocator::test()");

	// Corrupt the crap out of these two so we know they can take it!
	sampleManager.clusterSize = 0;
	sampleManager.clusterSizeMagnitude = 0;

	memset(testAllocations, 0, sizeof(testAllocations));

	int count = 0;

	bool goingUp = true;

	while (1) {
		//if (!(count & 15)) Uart::println("...");
		count++;

		for (int i = 0; i < NUM_TEST_ALLOCATIONS; i++) {

			if (testAllocations[i]) {

				// Check data is still there
				if (spaceTypes[i] != SPACE_HEADER_STEALABLE) testReadingMemory(i);

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
						if (!getRandom255()) Uart::println("extending");
						uint32_t amountExtendedLeft, amountExtendedRight;

						uint32_t idealAmountToExtend = ((uint32_t)getRandom255() << 17)
						                               | ((uint32_t)getRandom255() << 9)
						                               | ((uint32_t)getRandom255() << 1);
						int magnitudeReduction = getRandom255() % 25;
						idealAmountToExtend >>= magnitudeReduction;

						idealAmountToExtend = getMax(idealAmountToExtend, (uint32_t)4);
						int magnitudeReduction2 = getRandom255() % 25;
						uint32_t minAmountToExtend = idealAmountToExtend >> magnitudeReduction2;
						minAmountToExtend = getMax(minAmountToExtend, (uint32_t)4);

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
								Uart::println("extended too little!");
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
				Uart::print("\nfree spaces: ");
				Uart::println(regions[MEMORY_REGION_SDRAM].emptySpaces.getNumElements());
				Uart::print("allocations: ");
				Uart::println(regions[MEMORY_REGION_SDRAM].numAllocations);

				if (regions[MEMORY_REGION_SDRAM].emptySpaces.getNumElements() == 1) {
					EmptySpaceRecord* firstRecord =
					    (EmptySpaceRecord*)regions[MEMORY_REGION_SDRAM].emptySpaces.getElementAddress(0);
					Uart::print("free space size: ");
					Uart::println(firstRecord->length);
					Uart::print("free space address: ");
					Uart::println(firstRecord->address);
				}
				delayMS(200);
			}

			int desiredSize =
			    ((uint32_t)getRandom255() << 9) | ((uint32_t)getRandom255() << 1); // (uint32_t)getRandom255() << 17) |

			int magnitudeReduction = getRandom255() % 25;
			desiredSize >>= magnitudeReduction;

			if (desiredSize < 1) desiredSize = 1;

			uint32_t actualSize;
			if (!testAllocations[i]) {

				bool makeStealable = false;
				if (desiredSize >= sizeof(StealableTest)) makeStealable = getRandom255() & 1;

				testAllocations[i] = alloc(desiredSize, &actualSize, false, true, makeStealable);
				if (testAllocations[i]) {

					//if ((uint32_t)testAllocations[i] >= (uint32_t)INTERNAL_MEMORY_BEGIN) actualSize = desiredSize; // If on-chip memory

					if (actualSize < desiredSize) {
						Uart::println("got too little!!");
						Uart::println(desiredSize - actualSize);
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
