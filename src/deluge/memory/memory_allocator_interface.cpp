#include "general_memory_allocator.h"

void* allocMaxSpeed(uint32_t requiredSize) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, true);
}

void* allocLowSpeed(uint32_t requiredSize) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, false);
}
