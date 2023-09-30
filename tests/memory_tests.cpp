#include "CppUTest/TestHarness.h"
#include "memory/general_memory_allocator.h"
#include "memory/memory_region.h"
TEST_GROUP(MemoryAllocation){};

TEST(MemoryAllocation, FirstTest) {
	FAIL("Fail me!");
}

TEST(MemoryAllocation, SecondTest) {
	STRCMP_EQUAL("hello", "world");
}
