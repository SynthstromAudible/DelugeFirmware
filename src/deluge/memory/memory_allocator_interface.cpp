#include "general_memory_allocator.h"

void* allocMaxSpeed(uint32_t requiredSize, void* thingNotToStealFrom = NULL) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, true, false, thingNotToStealFrom);
}

void* allocLowSpeed(uint32_t requiredSize, void* thingNotToStealFrom = NULL) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, false, false, thingNotToStealFrom);
}

void* allocStealable(uint32_t requiredSize, void* thingNotToStealFrom = NULL) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, false, true, thingNotToStealFrom);
}
