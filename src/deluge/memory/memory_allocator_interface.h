#include "general_memory_allocator.h"

void* allocMaxSpeed(uint32_t requiredSize, void* thingNotToStealFrom = NULL);

void* allocLowSpeed(uint32_t requiredSize, void* thingNotToStealFrom = NULL);

void* allocStealable(uint32_t requiredSize, void* thingNotToStealFrom = NULL);
