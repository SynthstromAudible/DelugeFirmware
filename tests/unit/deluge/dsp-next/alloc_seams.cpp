/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The allocator seam the synth library reaches (the live input's pitch shifter and its histories), from the host
// heap: one definition for the dsp_specs driver.

#include "memory/memory_allocator_interface.h"

#include <cstdint>
#include <cstdlib>

void* allocMaxSpeed(uint32_t requiredSize) {
	return std::malloc(requiredSize);
}

extern "C" void delugeDealloc(void* address) {
	std::free(address);
}
