// Host adapters for firmware memory and fatal-error services. Parameter and
// container algorithms are compiled from src/deluge, not reproduced here.
#include "mocks/memory.h"

#include "CppUTest/TestHarness.h"
#include "memory/general_memory_allocator.h"
#include "util/functions.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace {
struct Allocation {
	// Dynamic byte storage models an allocator block, not a fixed-size array.
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
	std::unique_ptr<std::byte[]> memory = nullptr;
	uint32_t size = 0;
};
std::vector<Allocation> allocations;
size_t attempts = 0;
size_t frees = 0;
size_t fail_at = std::numeric_limits<size_t>::max();
uintptr_t addressValue(void const* address) {
	// Comparing integer addresses avoids ordering unrelated allocation pointers.
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	return reinterpret_cast<uintptr_t>(address);
}
auto findAllocation(void const* address) {
	return std::ranges::find_if(allocations,
	                            [address](auto const& allocation) { return allocation.memory.get() == address; });
}
} // namespace
namespace parameter_test {
void resetAllocator() {
	attempts = frees = 0;
	fail_at = std::numeric_limits<size_t>::max();
}
void failAllocationAfter(size_t successful_attempts) {
	fail_at = attempts + successful_attempts;
}
size_t allocationAttempts() {
	return attempts;
}
size_t freeCount() {
	return frees;
}
size_t liveAllocations() {
	return allocations.size();
}
bool ownsBlock(void const* address) {
	return findAllocation(address) != allocations.end();
}
bool contains(void const* address, size_t size) {
	auto value = addressValue(address);
	return std::ranges::any_of(allocations, [value, size](auto const& allocation) {
		auto start = addressValue(allocation.memory.get());
		return value >= start && value - start <= allocation.size && size <= allocation.size - (value - start);
	});
}
size_t finishAllocatorTest() {
	size_t leaked = allocations.size();
	allocations.clear();
	return leaked;
}
} // namespace parameter_test
MemoryRegion::MemoryRegion() : start(0), end(0), name(nullptr), cache_manager_(nullptr) {
}
GeneralMemoryAllocator::GeneralMemoryAllocator() : lock(false) {
}
// These definitions implement the production allocator interface, which is not static.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
void* GeneralMemoryAllocator::alloc(uint32_t requiredSize, bool /*mayUseOnChipRam*/, bool /*makeStealable*/,
                                    void* /*thingNotToStealFrom*/) {
	if (attempts++ == fail_at) {
		return nullptr;
	}
	try {
		// Byte-array new supplies storage aligned for the collection types, without
		// zeroing it and masking missing initialization in production constructors.
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
		auto memory = std::make_unique_for_overwrite<std::byte[]>(requiredSize);
		auto* address = memory.get();
		allocations.push_back({.memory = std::move(memory), .size = requiredSize});
		return address;
	} catch (std::bad_alloc const&) {
		return nullptr;
	}
}
void GeneralMemoryAllocator::dealloc(void* address) {
	if (address == nullptr) {
		return;
	}
	auto allocation = findAllocation(address);
	if (allocation == allocations.end()) {
		FAIL("Free of an unknown, interior, or already freed allocation");
		std::abort();
	}
	++frees;
	allocations.erase(allocation);
}
uint32_t GeneralMemoryAllocator::getAllocatedSize(void* address) {
	auto allocation = findAllocation(address);
	if (allocation == allocations.end()) {
		FAIL("Size requested for an unknown allocation");
		std::abort();
	}
	return allocation->size;
}
// Declining in-place growth/shrink is supported by the production allocator
// contract. This deliberately exercises the real container relocation path.
void GeneralMemoryAllocator::extend(void* /*address*/, uint32_t /*minAmountToExtend*/, uint32_t /*idealAmountToExtend*/,
                                    uint32_t* getAmountExtendedLeft, uint32_t* getAmountExtendedRight,
                                    void* /*thingNotToStealFrom*/) {
	*getAmountExtendedLeft = *getAmountExtendedRight = 0;
}
uint32_t GeneralMemoryAllocator::shortenLeft(void* /*address*/, uint32_t /*amountToShorten*/,
                                             uint32_t /*numBytesToMoveRightIfSuccessful*/) {
	return 0;
}
uint32_t GeneralMemoryAllocator::shortenRight(void* /*address*/, uint32_t /*newSize*/) {
	return 0;
}
// NOLINTEND(readability-convert-member-functions-to-static)
extern "C" void delugeDealloc(void* address) {
	GeneralMemoryAllocator::get().dealloc(address);
}
extern "C" void freezeWithError(char const* errmsg) {
	std::fputs("Firmware invariant failed: ", stderr);
	std::fputs(errmsg, stderr);
	std::fputc('\n', stderr);
	std::abort();
}
// Matches the production extern array declaration.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
alignas(CACHE_LINE_SIZE) char miscStringBuffer[kFilenameBufferSize];
