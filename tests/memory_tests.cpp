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
	void* testalloc = memreg.alloc(size, false, NULL);
	CHECK(testalloc != NULL);
	uint32_t actualSize = GeneralMemoryAllocator::get().getAllocatedSize(testalloc);
	CHECK(actualSize == size);
	CHECK(testAllocationStructure(testalloc, size, SPACE_HEADER_ALLOCATED));
};

TEST(MemoryAllocation, alloc100mb) {
	void* testalloc = memreg.alloc(0x04000000,false, NULL);
	CHECK(testalloc == NULL);
};

TEST(MemoryAllocation, allocstealable) {
	//mock().expectOneCall("steal");
	int32_t size = 1000;
	void* testalloc = memreg.alloc(size, true, NULL);
	StealableTest* stealable = new (testalloc) StealableTest();
	stealable->testIndex = 0;

	memreg.cache_manager().QueueForReclamation(0, stealable);
	vtableAddress = *(uint32_t*)testalloc;
	CHECK(testalloc != NULL);
	uint32_t actualSize = GeneralMemoryAllocator::get().getAllocatedSize(testalloc);
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
	for (int i = 0; i < NUM_TEST_ALLOCATIONS; i += 1) {
		void* testalloc = memreg.alloc(size, true, NULL);
		uint32_t actualSize = GeneralMemoryAllocator::get().getAllocatedSize(testalloc);
		StealableTest* stealable = new (testalloc) StealableTest();
		memreg.cache_manager().QueueForReclamation(0, stealable);
		vtableAddress = *(uint32_t*)testalloc;

		CHECK(testAllocationStructure(testalloc, actualSize, SPACE_HEADER_STEALABLE));

		testAllocations[i] = testalloc;
	}
	mock().checkExpectations();
};

TEST(MemoryAllocation, allocationStructure) {
	//this is technically random
	int expectedAllocations = 1000;
	void* testAllocations[expectedAllocations] = {0};
	uint32_t testSizes[expectedAllocations] = {0};
	uint32_t totalSize = 0;

	for (int i = 0; i < expectedAllocations; i++) {
		//this is to make a log distribution - probably the worst case for packing efficiency
		int magnitude = rand() % 16;
		int size = (rand() % 10) << magnitude;
		void* testalloc = memreg.alloc(size, false, NULL);
		if (testalloc) {
			uint32_t actualSize = GeneralMemoryAllocator::get().getAllocatedSize(testalloc);
			totalSize += actualSize;
			testWritingMemory(testalloc, actualSize);
			CHECK(testAllocationStructure(testalloc, actualSize, SPACE_HEADER_ALLOCATED));
			testAllocations[i] = testalloc;
			testSizes[i] = actualSize;
			if (i > 1) {
				CHECK(testAllocationStructure(testAllocations[i - 1], testSizes[i - 1], SPACE_HEADER_ALLOCATED));
			}
			if (i < expectedAllocations - 1) {
				CHECK(testAllocationStructure(testAllocations[i + 1], testSizes[i + 1], SPACE_HEADER_ALLOCATED));
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
		if (i < expectedAllocations - 1) {
			CHECK(testAllocationStructure(testAllocations[i + 1], testSizes[i + 1], SPACE_HEADER_ALLOCATED));
		}
	}
};

TEST(MemoryAllocation, allocationSizes) {
	srand(1);
	int expectedAllocations = 700;
	int numRepeats = 1000;
	void* testAllocations[expectedAllocations] = {0};
	uint32_t testSizes[expectedAllocations] = {0};
	uint32_t totalSize;
	float average_packing_factor = 0;
	for (int j = 0; j < numRepeats; j++) {
		totalSize = 0;
		for (int i = 0; i < expectedAllocations; i++) {
			if (!testAllocations[i]) {
				//this is to make a log distribution - probably the worst case for packing efficiency
				int magnitude = rand() % 16;
				int size = (rand() % 10) << magnitude;
				void* testalloc = memreg.alloc(size, false, NULL);
				if (testalloc) {
					uint32_t actualSize = GeneralMemoryAllocator::get().getAllocatedSize(testalloc);
					totalSize += actualSize;
					testAllocations[i] = testalloc;
					testSizes[i] = actualSize;
				}
			}
		}
		for (int i = 0; i < expectedAllocations; i++) {
			if (testAllocations[i]) {
				memreg.dealloc(testAllocations[i]);
				testAllocations[i] = nullptr;
				testSizes[i] = 0;
			}
		}
		//check that fragmentation wasn't terrible
		CHECK(totalSize > 0.95 * mem_size);
		average_packing_factor += (float(totalSize) / float(mem_size));

		//we should have one empty space left, and it should be the size of the memory minus headers
		CHECK(memreg.emptySpaces.getNumElements() == 1);
		CHECK(memreg.emptySpaces.getKeyAtIndex(0) == mem_size - 16);
	}
	//un modified GMA gets .999311
	CHECK(average_packing_factor / numRepeats > 0.999);
};

TEST(MemoryAllocation, RandomAllocFragmentation) {
	srand(1);
	int expectedAllocations = 600;
	int numRepeats = 1000;
	void* testAllocations[expectedAllocations] = {0};
	uint32_t testSizes[expectedAllocations] = {0};
	uint32_t totalSize = 0;
	float averageSize = 0;
	uint32_t allocations = 0;
	//we need to pre alloc a bunch to setup the test
	for (int i = 0; i < expectedAllocations; i++) {
		if (i % 4 != 0) {
			int magnitude = rand() % 18;
			int size = (rand() % 10) << magnitude;
			void* testalloc = memreg.alloc(size, false, NULL);
			if (testalloc) {
				uint32_t actualSize = GeneralMemoryAllocator::get().getAllocatedSize(testalloc);
				allocations += 1;
				totalSize += actualSize;
				testAllocations[allocations] = testalloc;
				testSizes[allocations] = actualSize;
			}
		}
	}

	for (int j = 0; j < numRepeats; j++) {
		totalSize = 0;
		for (int i = 0; i < allocations; i++) {
			if (!testAllocations[i]) {
				//this is to make a log distribution - probably the worst case for packing efficiency
				int magnitude = rand() % 18;
				int size = (rand() % 10) << magnitude;
				void* testalloc = memreg.alloc(size, false, NULL);
				if (testalloc) {
					uint32_t actualSize = GeneralMemoryAllocator::get().getAllocatedSize(testalloc);
					totalSize += actualSize;
					testAllocations[i] = testalloc;
					testSizes[i] = actualSize;
				}
			}
			else {
				if (rand() % 4 == 0) {
					memreg.dealloc(testAllocations[i]);
					testAllocations[i] = nullptr;
					testSizes[i] = 0;
				}
				else {
					totalSize += testSizes[i];
				}
			}
		}
		//std::cout << float(totalSize) / float(mem_size) << std::endl;
		averageSize += totalSize;
	};
	//for regression - unmodified GMA scores 0.648
	//a perfect allocator with no fragmentation would tend towards 0.75
	//std::cout << (float(averageSize / numRepeats) / float(mem_size)) << std::endl;
	CHECK(averageSize / numRepeats > 0.64 * mem_size);
};
} // namespace
