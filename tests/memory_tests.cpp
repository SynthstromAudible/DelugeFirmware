#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "memory/general_memory_allocator.h"
#include "memory/memory_region.h"
#include "util/functions.h"
#include <iostream>
#define NUM_TEST_ALLOCATIONS 512
uint32_t vtableAddress; // will hold address of the stealable test vtable
MemoryRegion memreg;
class StealableTest : public Stealable {
public:
	void steal(char const* errorCode) {
		//Stealable::steal();

		memreg.numAllocations--;
	}
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
}

void testWritingMemory(void* address, uint32_t size) {
	uint8_t* __restrict__ writePos = (uint8_t*)address;
	uint8_t writeValue = getRandom255();
	for (int32_t j = 0; j < size; j++) {
		*writePos = writeValue;
		writeValue++;
		writePos++;
	}
}
bool testAllocationStructure(void* address, uint32_t size, uint32_t spaceType) {

	uint32_t* header = (uint32_t*)address - 1;
	uint32_t* footer = (uint32_t*)((int32_t)address + size);

	uint32_t shouldBe = size | spaceType;

	if (*header != shouldBe) {

		return false;
	}
	if (*footer != shouldBe) {

		return false;
	}
	if (spaceType == SPACE_HEADER_STEALABLE && *(header + 1) != vtableAddress) {
		std::cout << "vtable address corrupted" << std::endl;
		std::cout << std::hex << *(header + 1) << std::endl;
		std::cout << std::hex << vtableAddress << std::endl;
		return false;
	}
	return true;
}

TEST_GROUP(MemoryAllocation) {

	//this will hold the address of the stealable test vtable
	uint32_t empty_spaze_size = sizeof(EmptySpaceRecord) * 512;
	void* emptySpacesMemory = malloc(empty_spaze_size);
	int32_t mem_size = 0x02000000;
	void* raw_mem = malloc(mem_size);
	//this runs before each test to re intitialize the memory
	void setup() {
		memset(raw_mem, 0, mem_size);
		memset(emptySpacesMemory, 0, empty_spaze_size);
		memreg.setup(emptySpacesMemory, empty_spaze_size, (uint32_t)raw_mem, (uint32_t)raw_mem + mem_size);
	}
	void teardown() {
		//the empty spaces memory is freed when memreg is deconstructed
		free(raw_mem);
	}
};

TEST(MemoryAllocation, alloc1kb) {
	int32_t size = 1000;
	uint32_t actualSize;
	void* testalloc = memreg.alloc(size, &actualSize, false, NULL, false);
	CHECK(testalloc != NULL);
	CHECK(actualSize == size);
	CHECK(testAllocationStructure(testalloc, size, SPACE_HEADER_ALLOCATED));
}

TEST(MemoryAllocation, alloc100mb) {
	void* testalloc = memreg.alloc(0x04000000, NULL, false, NULL, false);
	CHECK(testalloc == NULL);
}

TEST(MemoryAllocation, allocstealable) {
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
}
//allocate 1000 1
TEST(MemoryAllocation, uniformAllocation) {

	void* testAllocations[NUM_TEST_ALLOCATIONS + 1];
	uint32_t size = 1000000;
	uint32_t actualSize;
	for (int i = 0; i < NUM_TEST_ALLOCATIONS + 10; i += 1) {
		void* testalloc = memreg.alloc(size, &actualSize, true, NULL, false);
		StealableTest* stealable = new (testalloc) StealableTest();
		memreg.cache_manager().QueueForReclamation(0, stealable);
		vtableAddress = *(uint32_t*)testalloc;

		CHECK(testAllocationStructure(testalloc, actualSize, SPACE_HEADER_STEALABLE));

		testAllocations[i] = testalloc;
	}
	std::cout << "passed" << std::endl;
}
