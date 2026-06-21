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

#include "memory/heaps.h"
#if DELUGE_USE_RUST_ALLOC
#include "libdeluge/alloc.h"
#include "libdeluge/memory.h"
#endif

namespace deluge::memory {

#if DELUGE_USE_RUST_ALLOC

namespace {
DelugeHeap* g_sram = nullptr;
DelugeHeap* g_sdram = nullptr;
bool g_built = false;
} // namespace

void init_heaps() {
	if (g_built) {
		return;
	}
	g_built = true;
	for (uint8_t i = 0, n = deluge_memory_region_count(); i < n; ++i) {
		DelugeMemoryRegion r;
		if (deluge_memory_region(i, &r) != DELUGE_OK) {
			continue;
		}
		if (r.kind == DELUGE_MEM_FAST_INTERNAL && g_sram == nullptr) {
			g_sram = deluge_heap_create(r.base, r.size);
		}
		// One unified SDRAM heap over the whole LARGE_EXTERNAL region (which on both
		// BSPs is exactly [__sdram_bss_end, external_end) — the post-BSS usable SDRAM
		// the GMA used to split into stealable/external/external_small). Collapsing
		// the partitions is the redesign's unified pool. The reclaim hook (cache
		// eviction) is registered by the GeneralMemoryAllocator.
		if (r.kind == DELUGE_MEM_LARGE_EXTERNAL && g_sdram == nullptr) {
			g_sdram = deluge_heap_create(r.base, r.size);
		}
	}
}

DelugeHeap* sram_heap() {
	init_heaps();
	return g_sram;
}

DelugeHeap* sdram_heap() {
	init_heaps();
	return g_sdram;
}

#else // !DELUGE_USE_RUST_ALLOC — legacy MemoryRegion path; no Rust heaps.

void init_heaps() {
}
DelugeHeap* sram_heap() {
	return nullptr;
}
DelugeHeap* sdram_heap() {
	return nullptr;
}

#endif

} // namespace deluge::memory
