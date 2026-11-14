/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The allocator seam the live input's pitch shifter reaches (memory/memory_allocator_interface.h), from the host heap,
// for specs that link the synth library. Included by one spec per executable.

#pragma once

#include <cstdint>
#include <cstdlib>

void* allocMaxSpeed(uint32_t requiredSize) {
	return std::malloc(requiredSize);
}
extern "C" void delugeDealloc(void* address) {
	std::free(address);
}
