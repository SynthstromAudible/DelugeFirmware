#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "memory/general_memory_allocator.h"
#include "memory/memory_region.h"

class MemoryRegionMock : public MemoryRegion {
public:
	virtual void dbgprint(const char* string) { printf(string); }
	virtual void freeze(const char* string) { printf(string); }
};

TEST_GROUP(MemoryAllocation){void setup(){MemoryRegionMock memreg;
}
}
;

TEST(MemoryAllocation, FirstTest) {
	FAIL("Fail me!");
}

TEST(MemoryAllocation, SecondTest) {
	STRCMP_EQUAL("hello", "world");
}
