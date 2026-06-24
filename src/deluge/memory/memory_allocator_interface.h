#pragma once

// this file exists to cross the arm/thumb boundary - allowing the arm code to see the GMA object leads to multi
// definitions
#include <cstdint>
void* allocMaxSpeed(uint32_t requiredSize);

void* allocLowSpeed(uint32_t requiredSize);

extern "C" {
void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam);
void delugeDealloc(void* address);
}
