#ifndef DELUGE_PARAMETER_TEST_MEMORY_H
#define DELUGE_PARAMETER_TEST_MEMORY_H
#include <cstddef>

namespace parameter_test {
void resetAllocator();
void failAllocationAfter(size_t successful_attempts);
size_t allocationAttempts();
size_t freeCount();
size_t liveAllocations();
bool ownsBlock(void const* address);
bool contains(void const* address, size_t size);
// Release leaked host memory after reporting its count, to isolate following tests.
size_t finishAllocatorTest();
} // namespace parameter_test

#endif // DELUGE_PARAMETER_TEST_MEMORY_H
