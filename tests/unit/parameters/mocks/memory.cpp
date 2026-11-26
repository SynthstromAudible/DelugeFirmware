// Host adapters for firmware memory and fatal-error services. Parameter and
// container algorithms are compiled from src/deluge, not reproduced here.
#include "CppUTest/TestHarness.h"
#include "memory/general_memory_allocator.h"
#include "util/functions.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace {
std::unordered_map<void*, uint32_t> allocations;
}
MemoryRegion::MemoryRegion() = default;
GeneralMemoryAllocator::GeneralMemoryAllocator() = default;
void* GeneralMemoryAllocator::alloc(uint32_t size, bool, bool, void*) {
	void* p = std::malloc(size);
	if (p)
		allocations.emplace(p, size);
	return p;
}
void GeneralMemoryAllocator::dealloc(void* p) {
	if (!p)
		return;
	CHECK_EQUAL(1, allocations.erase(p));
	std::free(p);
}
uint32_t GeneralMemoryAllocator::getAllocatedSize(void* p) {
	return allocations.at(p);
}
// Declining in-place growth/shrink is supported by the production allocator
// contract. This deliberately exercises the real container relocation path.
void GeneralMemoryAllocator::extend(void*, uint32_t, uint32_t, uint32_t* left, uint32_t* right, void*) {
	*left = *right = 0;
}
uint32_t GeneralMemoryAllocator::shortenLeft(void*, uint32_t, uint32_t) {
	return 0;
}
uint32_t GeneralMemoryAllocator::shortenRight(void*, uint32_t) {
	return 0;
}
extern "C" void delugeDealloc(void* p) {
	GeneralMemoryAllocator::get().dealloc(p);
}
extern "C" void freezeWithError(char const* message) {
	std::fprintf(stderr, "Firmware invariant failed: %s\n", message);
	std::abort();
}

// Host storage for globals used by the production containers.
namespace AudioEngine {
bool bypassCulling = false;
}
alignas(CACHE_LINE_SIZE) char miscStringBuffer[kFilenameBufferSize];
