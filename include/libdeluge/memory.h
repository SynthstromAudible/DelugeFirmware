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

/// libdeluge/memory.h — memory regions and cache maintenance.
///
/// The application's allocator asks the BSP where the usable RAM regions are
/// instead of hard-coding `EXTERNAL_MEMORY_END` etc. The BSP also exposes the
/// cache clean/invalidate operations needed for DMA coherency.
#ifndef LIBDELUGE_MEMORY_H
#define LIBDELUGE_MEMORY_H

#include "abi.h"
#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Kind of memory region a DelugeMemoryRegion describes.
typedef enum DelugeMemoryKind : uint8_t {
	DELUGE_MEM_FAST_INTERNAL = 0, ///< small, low-latency on-chip SRAM
	DELUGE_MEM_LARGE_EXTERNAL = 1 ///< large external SDRAM
} DelugeMemoryKind;

typedef struct DelugeMemoryRegion {
	void* base;
	uint32_t size; ///< bytes
	DelugeMemoryKind kind;
} DelugeMemoryRegion;

/// Number of allocatable memory regions the board provides. [task]
uint8_t deluge_memory_region_count(void) DELUGE_NOEXCEPT;

/// Describe region `index` (0..count-1). [task]
DelugeStatus deluge_memory_region(uint8_t index, DelugeMemoryRegion* out) DELUGE_NOEXCEPT;

/// A writable scratch address whose contents are never read (a "bit bucket"),
/// for discarding streamed writes. [task] [audio] [isr]
void* deluge_memory_scratch(void) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_MEMORY_H
