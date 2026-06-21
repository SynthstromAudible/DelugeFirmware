/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// RZ/A1L implementation of <libdeluge/memory.h> (region bounds).
///
/// Part of bsp-rza1-legacy; depends only on the HAL's memory map, so it lives in
/// the clean bsp_rza1 library. The region-descriptor and cache-maintenance
/// entry points declared in the header are added when their consumers migrate.

#include "libdeluge/memory.h"

#include "RZA1/cpu_specific.h" // EXTERNAL_MEMORY_END, INTERNAL_MEMORY_BEGIN, UNUSED_MEMORY_SPACE_ADDRESS

// Linker boundary symbols: the allocatable external region runs from the end of
// the SDRAM .bss to EXTERNAL_MEMORY_END; the internal heap from the end of the
// image (__heap_start) to the base of the program stack. (program_stack_start
// stays the program stack base in this single-stack layout, so it doubles as the
// internal heap top.)
extern uint32_t __sdram_bss_end;
extern uint32_t __heap_start;
extern uint32_t program_stack_start;

uintptr_t deluge_memory_external_end(void) {
	return EXTERNAL_MEMORY_END;
}

uint8_t deluge_memory_region_count(void) {
	return 2;
}

DelugeStatus deluge_memory_region(uint8_t index, DelugeMemoryRegion* out) {
	if (index == 0) {
		out->base = &__sdram_bss_end;
		out->size = EXTERNAL_MEMORY_END - (uintptr_t)&__sdram_bss_end;
		out->kind = DELUGE_MEM_LARGE_EXTERNAL;
		return DELUGE_OK;
	}
	if (index == 1) {
		out->base = &__heap_start;
		out->size = (uintptr_t)&program_stack_start - (uintptr_t)&__heap_start;
		out->kind = DELUGE_MEM_FAST_INTERNAL;
		return DELUGE_OK;
	}
	return DELUGE_ERR_PARAM;
}

uintptr_t deluge_memory_internal_begin(void) {
	return INTERNAL_MEMORY_BEGIN;
}

void* deluge_memory_scratch(void) {
	return (void*)UNUSED_MEMORY_SPACE_ADDRESS;
}
