#include "general_memory_allocator.h"

void* allocMaxSpeed(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, true, false, thingNotToStealFrom);
}

void* allocLowSpeed(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, false, false, thingNotToStealFrom);
}

void* allocStealable(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, false, true, thingNotToStealFrom);
}

void* allocSdram(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
	// Allocate directly from SDRAM (stealable region) but mark as ALLOCATED (not stealable).
	// This skips the external region which is too small for large buffers.
	return GeneralMemoryAllocator::get().regions[MEMORY_REGION_STEALABLE].alloc(requiredSize, false,
	                                                                            thingNotToStealFrom);
}
