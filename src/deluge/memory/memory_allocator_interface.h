#pragma once

// this file exists to cross the arm/thumb boundary - allowing the arm code to see the GMA object leads to multi
// definitions
#include <cstdint>
void* allocMaxSpeed(size_t requiredSize, void* thingNotToStealFrom = nullptr);

void* allocLowSpeed(size_t requiredSize, void* thingNotToStealFrom = nullptr);

void* allocStealable(size_t requiredSize, void* thingNotToStealFrom = nullptr);

extern "C" {
void* delugeAlloc(size_t requiredSize, bool mayUseOnChipRam);
void delugeDealloc(void* address);
}
