#include "CppUTest/TestHarness.h"
#include "memory/memory_region.h"
TEST_GROUP(FirstTestGroup){};

TEST(FirstTestGroup, FirstTest) {
	FAIL("Fail me!");
}

TEST(FirstTestGroup, SecondTest) {
	STRCMP_EQUAL("hello", "world");
}
