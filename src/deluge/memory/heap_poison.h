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

// heap_poison.h — teach a sanitizer about the custom allocator's block boundaries.
//
// GeneralMemoryAllocator hands out slices of a few large static regions (on the host-sim, the BSP's
// host_sdram[] etc.). To AddressSanitizer/Valgrind those regions are just big buffers, so an object that is
// freed (or stolen) and then used again is invisible - the access still lands inside the region. These hooks
// mark a block's body as poisoned ("no access") when it is returned to the allocator and unpoisoned when it
// is handed back out, so use-after-free / use-after-steal / out-of-bounds reads within a region become real
// sanitizer findings on the host-sim, the same way they would for libc malloc/free.
//
// On the device (no sanitizer) these compile to nothing, and MEM_GUARD provides the equivalent coverage via
// its own poison pattern + periodic heap walk. The two are mutually exclusive: MEM_GUARD writes the freed
// body (its poison fill / verify-on-realloc), which would itself trip ASan, so definitions.h disables
// MEM_GUARD whenever a sanitizer is active and lets these hooks own corruption detection instead.
#ifndef DELUGE_MEMORY_HEAP_POISON_H
#define DELUGE_MEMORY_HEAP_POISON_H

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define DELUGE_HEAP_POISON(addr, size) __asan_poison_memory_region((addr), (size))
#define DELUGE_HEAP_UNPOISON(addr, size) __asan_unpoison_memory_region((addr), (size))
#define DELUGE_HEAP_POISON_ACTIVE 1

#elif defined(DELUGE_VALGRIND)
#include <valgrind/memcheck.h>
#define DELUGE_HEAP_POISON(addr, size) VALGRIND_MAKE_MEM_NOACCESS((addr), (size))
#define DELUGE_HEAP_UNPOISON(addr, size) VALGRIND_MAKE_MEM_UNDEFINED((addr), (size))
#define DELUGE_HEAP_POISON_ACTIVE 1

#else
#define DELUGE_HEAP_POISON(addr, size) ((void)0)
#define DELUGE_HEAP_UNPOISON(addr, size) ((void)0)
#define DELUGE_HEAP_POISON_ACTIVE 0
#endif

#endif // DELUGE_MEMORY_HEAP_POISON_H
