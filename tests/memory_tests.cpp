#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "memory/general_memory_allocator.h"
#include "memory/memory_region.h"
#include "util/functions.h"
#include <iostream>
#include <stdlib.h>
#define NUM_TEST_ALLOCATIONS 512
#define MEM_SIZE 10000000
namespace {
uint32_t vtableAddress; // will hold address of the stealable test vtable

class StealableTest : public Stealable {
public:
	void steal(char const* errorCode) { mock().actualCall("steal"); }
	bool mayBeStolen(void* thingNotToStealFrom) { return true; }
	int32_t getAppropriateQueue() { return 0; }
	int32_t testIndex;
};

bool testReadingMemory(void* address, uint32_t size) {
	uint8_t* __restrict__ readPos = (uint8_t*)address;
	uint8_t readValue = *readPos;
	for (int32_t j = 0; j < size; j++) {
		if (*readPos != readValue) {
			return false;
		}
		readPos++;
		readValue++;
	}
	return true;
};

void testWritingMemory(void* address, uint32_t size) {
	uint8_t* __restrict__ writePos = (uint8_t*)address;
	uint8_t writeValue = getRandom255();
	for (int32_t j = 0; j < size; j++) {
		*writePos = writeValue;
		writeValue++;
		writePos++;
	}
};

bool testAllocationStructure(void* address, uint32_t size, uint32_t spaceType) {
	if (!address) {
		//for convenience
		return true;
	}
	uint32_t* header = (uint32_t*)address - 1;
	uint32_t* footer = (uint32_t*)((int32_t)address + size);

	uint32_t shouldBe = size | spaceType;

	if (*header != shouldBe) {
		std::cout << "header corrupted" << std::endl;
		return false;
	}
	if (*footer != shouldBe) {
		std::cout << "footer corrupted" << std::endl;
		return false;
	}
	if (spaceType == SPACE_HEADER_STEALABLE && *(header + 1) != vtableAddress) {
		std::cout << "vtable address corrupted" << std::endl;
		std::cout << std::hex << *(header + 1) << std::endl;
		std::cout << std::hex << vtableAddress << std::endl;
		return false;
	}
	return true;
};

TEST_GROUP(MemoryAllocation) {
	MemoryRegion memreg;
	//this will hold the address of the stealable test vtable
	uint32_t empty_spaze_size = sizeof(EmptySpaceRecord) * 512;
	void* emptySpacesMemory = malloc(empty_spaze_size);
	int32_t mem_size = MEM_SIZE;
	void* raw_mem = malloc(mem_size);
	//this runs before each test to re intitialize the memory
	void setup() {

		memset(raw_mem, 0, mem_size);
		memset(emptySpacesMemory, 0, empty_spaze_size);
		memreg.setup(emptySpacesMemory, empty_spaze_size, (uint32_t)raw_mem, (uint32_t)raw_mem + mem_size);
	}
};

TEST(MemoryAllocation, alloc1kb) {
	int32_t size = 1000;
	uint32_t actualSize;
	void* testalloc = memreg.alloc(size, &actualSize, false, NULL, false);
	CHECK(testalloc != NULL);
	CHECK(actualSize == size);
	CHECK(testAllocationStructure(testalloc, size, SPACE_HEADER_ALLOCATED));
};

TEST(MemoryAllocation, alloc100mb) {
	void* testalloc = memreg.alloc(0x04000000, NULL, false, NULL, false);
	CHECK(testalloc == NULL);
};

TEST(MemoryAllocation, allocstealable) {
	//mock().expectOneCall("steal");
	int32_t size = 1000;
	uint32_t actualSize;
	void* testalloc = memreg.alloc(size, &actualSize, true, NULL, false);
	StealableTest* stealable = new (testalloc) StealableTest();
	stealable->testIndex = 0;

	memreg.cache_manager().QueueForReclamation(0, stealable);
	vtableAddress = *(uint32_t*)testalloc;
	CHECK(testalloc != NULL);
	CHECK(actualSize == size);
	CHECK(testAllocationStructure(testalloc, size, SPACE_HEADER_STEALABLE));
};

// allocate 512 1m stealables
TEST(MemoryAllocation, uniformAllocation) {
	uint32_t size = 1000000;
	//this is the number of steals we expect to occur
	//it's plus 8 for the header and footer
	int ncalls = NUM_TEST_ALLOCATIONS - mem_size / (size + 8);
	mock().expectNCalls(ncalls, "steal");

	void* testAllocations[NUM_TEST_ALLOCATIONS];
	uint32_t actualSize;
	for (int i = 0; i < NUM_TEST_ALLOCATIONS; i += 1) {
		void* testalloc = memreg.alloc(size, &actualSize, true, NULL, false);
		StealableTest* stealable = new (testalloc) StealableTest();
		memreg.cache_manager().QueueForReclamation(0, stealable);
		vtableAddress = *(uint32_t*)testalloc;

		CHECK(testAllocationStructure(testalloc, actualSize, SPACE_HEADER_STEALABLE));

		testAllocations[i] = testalloc;
	}
	mock().checkExpectations();
};

TEST(MemoryAllocation, randomAllocations) {
	//this is technically random
	int expectedAllocations = 1000;
	void* testAllocations[expectedAllocations] = {0};
	uint32_t testSizes[expectedAllocations] = {0};
	uint32_t totalSize = 0;
	uint32_t actualSize;

	for (int i = 0; i < expectedAllocations; i++) {
		//this is to make a log distribution - probably the worst case for packing efficiency
		int magnitude = rand() % 16;
		int size = (rand() % 10) << magnitude;
		void* testalloc = memreg.alloc(size, &actualSize, false, NULL, false);
		if (testalloc) {
			totalSize += actualSize;
			testWritingMemory(testalloc, actualSize);
			CHECK(testAllocationStructure(testalloc, actualSize, SPACE_HEADER_ALLOCATED));
			testAllocations[i] = testalloc;
			testSizes[i] = actualSize;
			if (i > 2) {
				CHECK(testAllocationStructure(testAllocations[i - 1], testSizes[i - 1], SPACE_HEADER_ALLOCATED));
			}
		}
		else {
			//filled the memory
			break;
		}
	}
	for (int i = 0; i < expectedAllocations; i++) {
		if (testAllocations[i]) {
			CHECK(testReadingMemory(testAllocations[i], testSizes[i]));
			memreg.dealloc(testAllocations[i]);
		}
	}
};

TEST(MemoryAllocation, randomAllocDeAlloc) {
	//this is technically random, should fill memory in ~5-600 allocations
	int expectedAllocations = 1000;
	int numRepeats = 25;
	void* testAllocations[expectedAllocations] = {0};
	uint32_t testSizes[expectedAllocations] = {0};
	uint32_t totalSize = 0;
	uint32_t actualSize;
	for (int j = 0; j < numRepeats; j++) {
		for (int i = 0; i < expectedAllocations; i++) {
			if (!testAllocations[i]) {
				//this is to make a log distribution - probably the worst case for packing efficiency
				int magnitude = rand() % 16;
				int size = (rand() % 10) << magnitude;
				void* testalloc = memreg.alloc(size, &actualSize, false, NULL, false);
				if (testalloc) {
					totalSize += actualSize;
					testWritingMemory(testalloc, actualSize);
					CHECK(testAllocationStructure(testalloc, actualSize, SPACE_HEADER_ALLOCATED));
					testAllocations[i] = testalloc;
					testSizes[i] = actualSize;
					if (i > 1) {
						CHECK(
						    testAllocationStructure(testAllocations[i - 1], testSizes[i - 1], SPACE_HEADER_ALLOCATED));
					}
					if (i < expectedAllocations - 1) {
						CHECK(
						    testAllocationStructure(testAllocations[i + 1], testSizes[i + 1], SPACE_HEADER_ALLOCATED));
					}
				}
				else {
					break;
				}
			}
		}
		for (int i = 0; i < expectedAllocations; i++) {
			if (testAllocations[i]) {
				CHECK(testReadingMemory(testAllocations[i], testSizes[i]));
				memreg.dealloc(testAllocations[i]);
				testAllocations[i] = nullptr;
				testSizes[i] = 0;
			}
			if (i < expectedAllocations - 1) {
				CHECK(testAllocationStructure(testAllocations[i + 1], testSizes[i + 1], SPACE_HEADER_ALLOCATED));
			}
		}
	}
};
} // namespace
