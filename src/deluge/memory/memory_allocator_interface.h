#pragma once

// this file exists to cross the arm/thumb boundary - allowing the arm code to see the GMA object leads to multi
// definitions
#include <cstdint>
void* allocMaxSpeed(uint32_t requiredSize, void* thingNotToStealFrom = nullptr);

void* allocLowSpeed(uint32_t requiredSize, void* thingNotToStealFrom = nullptr);

void* allocStealable(uint32_t requiredSize, void* thingNotToStealFrom = nullptr);

/// Allocate directly from SDRAM (stealable region) without trying internal/external first.
/// Unlike allocStealable(), the memory is marked as ALLOCATED (not stealable), so it won't be stolen.
/// Use this for large temporary buffers that don't fit in the small external region.
void* allocSdram(uint32_t requiredSize, void* thingNotToStealFrom = nullptr);

extern "C" {
void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam);
void delugeDealloc(void* address);
}
