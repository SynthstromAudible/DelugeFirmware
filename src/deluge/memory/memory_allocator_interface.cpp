#include "general_memory_allocator.h"

void* allocMaxSpeed(size_t requiredSize, void* thingNotToStealFrom = NULL) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, true, false, thingNotToStealFrom);
}

void* allocLowSpeed(size_t requiredSize, void* thingNotToStealFrom = NULL) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, false, false, thingNotToStealFrom);
}

void* allocStealable(size_t requiredSize, void* thingNotToStealFrom = NULL) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, false, true, thingNotToStealFrom);
}
