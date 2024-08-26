// Mock memory manager implementation
//
// This isn't in the mocks/ directory so it doesn't get picked up by the glob that feeds the memory manager tests --
// this file implements a stub for the actual memory manager instead.

#include "CppUTest/TestHarness.h"
#include "memory/general_memory_allocator.h"
#include <cstdlib>

// Fake allocator object
GeneralMemoryAllocator allocator;

class MockMemoryAllocator {
public:
	void* alloc(uint32_t requiredSize, bool mayUseOnChipRam, bool makeStealable, void* thingNotToStealFrom) {
		return malloc(requiredSize);
	}

	void dealloc(void* address) { free(address); }

	void* allocExternal(uint32_t requiredSize) { return malloc(requiredSize); }

	void deallocExternal(void* address) { free(address); }

	uint32_t shortenRight(void* address, uint32_t newSize) {
		/* noop on mock allocator */
		/// TODO: integrate some sort of sanity checking to make this actually check whether hte call is valid
		return 0;
	}

	uint32_t shortenLeft(void* address, uint32_t amountToShorten, uint32_t numBytesToMoveRightIfSuccessful = 0) {
		// TODO: implementing this requires actually tracking allocations so we know the size.
		CHECK(false);
		return -1;
	}

	void extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
	            uint32_t* getAmountExtendedLeft, uint32_t* getAmountExtendedRight, void* thingNotToStealFrom = NULL) {
		// TODO: implementing this requires actually tracking allocations so we know the starting size.
		CHECK(false);
	}

	uint32_t extendRightAsMuchAsEasilyPossible(void* address) {
		// TODO: implementing this requires actually tracking allocations so we know the starting size.
		CHECK(false);
		return -1;
	}

	uint32_t getAllocatedSize(void* address) {
		// TODO: implementing this requires actually tracking allocations so we know the starting size.
		CHECK(false);
		return -1;
	}

	void checkStack(char const* caller) { /* noop in tests */ }

	int32_t getRegion(void* address) {
		// TODO: we should expose an API to allow mocking this
		return 0;
	}
};

MockMemoryAllocator mockAllocator;

MemoryRegion::MemoryRegion() = default;
GeneralMemoryAllocator::GeneralMemoryAllocator() = default;

void* GeneralMemoryAllocator::alloc(uint32_t requiredSize, bool mayUseOnChipRam, bool makeStealable,
                                    void* thingNotToStealFrom) {
	return mockAllocator.alloc(requiredSize, mayUseOnChipRam, makeStealable, thingNotToStealFrom);
}

void GeneralMemoryAllocator::dealloc(void* address) {
	return mockAllocator.dealloc(address);
}

void* GeneralMemoryAllocator::allocExternal(uint32_t requiredSize) {
	return mockAllocator.allocExternal(requiredSize);
}

void GeneralMemoryAllocator::deallocExternal(void* address) {
	return mockAllocator.deallocExternal(address);
}

uint32_t GeneralMemoryAllocator::shortenRight(void* address, uint32_t newSize) {
	return mockAllocator.shortenRight(address, newSize);
}

uint32_t GeneralMemoryAllocator::shortenLeft(void* address, uint32_t amountToShorten,
                                             uint32_t numBytesToMoveRightIfSuccessful) {
	return mockAllocator.shortenLeft(address, amountToShorten, numBytesToMoveRightIfSuccessful);
}

void GeneralMemoryAllocator::extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
                                    uint32_t* getAmountExtendedLeft, uint32_t* getAmountExtendedRight,
                                    void* thingNotToStealFrom) {
	return mockAllocator.extend(address, minAmountToExtend, idealAmountToExtend, getAmountExtendedLeft,
	                            getAmountExtendedRight, thingNotToStealFrom);
}

uint32_t GeneralMemoryAllocator::extendRightAsMuchAsEasilyPossible(void* address) {
	return mockAllocator.extendRightAsMuchAsEasilyPossible(address);
}

uint32_t GeneralMemoryAllocator::getAllocatedSize(void* address) {
	return mockAllocator.getAllocatedSize(address);
}

void GeneralMemoryAllocator::checkStack(char const* caller) {
	return mockAllocator.checkStack(caller);
}

int32_t GeneralMemoryAllocator::getRegion(void* address) {
	return mockAllocator.getRegion(address);
}

extern "C" {

void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, mayUseOnChipRam, false, nullptr);
}

void delugeDealloc(void* address) {
	GeneralMemoryAllocator::get().dealloc(address);
}
}
