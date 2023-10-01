#include "CppUTest/TestHarness.h"

TEST_GROUP(MemoryAllocation){};

TEST(MemoryAllocation, FirstTest) {
	FAIL("Fail me!");
}

TEST(MemoryAllocation, SecondTest) {
	STRCMP_EQUAL("hello", "world");
}
