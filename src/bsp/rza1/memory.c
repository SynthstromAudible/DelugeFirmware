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

uintptr_t deluge_memory_external_end(void) {
	return EXTERNAL_MEMORY_END;
}

uintptr_t deluge_memory_internal_begin(void) {
	return INTERNAL_MEMORY_BEGIN;
}

void* deluge_memory_scratch(void) {
	return (void*)UNUSED_MEMORY_SPACE_ADDRESS;
}
