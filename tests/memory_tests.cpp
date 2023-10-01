#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "memory/general_memory_allocator.h"
#include "memory/memory_region.h"

// clang-format off
TEST_GROUP(MemoryAllocation)
{
	void setup() {
		MemoryRegion memreg;
		uint32_t empty_spaze_size = sizeof(EmptySpaceRecord) * 512;
		void* emptySpacesMemory = malloc(empty_spaze_size);
		constexpr int32_t mem_size = 0x02000000;
		void* raw_mem = malloc(mem_size);
		memreg.setup(emptySpacesMemory, empty_spaze_size, (uint32_t)raw_mem, (uint32_t)raw_mem + mem_size);
	}
};

TEST(MemoryAllocation, FirstTest) {
	FAIL("Fail me!");
}

TEST(MemoryAllocation, SecondTest) {
	STRCMP_EQUAL("hello", "world");
}
