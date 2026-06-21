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
#include "libdeluge/alloc.h"
#include "libdeluge/memory.h"
#include <cstdint>

// Linker-defined small-internal ("frunk") SRAM region — a reserved slack area that
// is NOT a deluge_memory_region, so its heap is built from these symbols here.
// (The Rust deluge_alloc crate stays linker-symbol-free; this app-layer file may
// reference them, as the GeneralMemoryAllocator historically did.)
// NOLINTBEGIN — addresses are only meaningful via &symbol
extern uint32_t __frunk_bss_end;
extern uint32_t __frunk_slack_end;
// NOLINTEND

namespace deluge::memory {

namespace {
DelugeHeap* g_sram = nullptr;
DelugeHeap* g_sdram = nullptr;
DelugeHeap* g_frunk = nullptr;
uintptr_t g_sram_lo = 0, g_sram_hi = 0;
uintptr_t g_sdram_lo = 0, g_sdram_hi = 0;
uintptr_t g_frunk_lo = 0, g_frunk_hi = 0;
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
			g_sram_lo = reinterpret_cast<uintptr_t>(r.base);
			g_sram_hi = g_sram_lo + r.size;
		}
		// One unified SDRAM heap over the whole LARGE_EXTERNAL region (which on both
		// BSPs is exactly [__sdram_bss_end, external_end) — the post-BSS usable SDRAM
		// the GMA used to split into stealable/external/external_small). Collapsing
		// the partitions is the redesign's unified pool. The reclaim hook (cache
		// eviction) is registered by the GeneralMemoryAllocator.
		if (r.kind == DELUGE_MEM_LARGE_EXTERNAL && g_sdram == nullptr) {
			g_sdram = deluge_heap_create(r.base, r.size);
			g_sdram_lo = reinterpret_cast<uintptr_t>(r.base);
			g_sdram_hi = g_sdram_lo + r.size;
		}
	}
	// Small-internal ("frunk") heap from the linker symbols (empty on a board that
	// doesn't reserve the region → deluge_heap_create not called, g_frunk stays null).
	auto frunk_lo = reinterpret_cast<uintptr_t>(&__frunk_bss_end);
	auto frunk_hi = reinterpret_cast<uintptr_t>(&__frunk_slack_end);
	if (frunk_hi > frunk_lo) {
		g_frunk = deluge_heap_create(reinterpret_cast<void*>(frunk_lo), frunk_hi - frunk_lo);
		g_frunk_lo = frunk_lo;
		g_frunk_hi = frunk_hi;
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

DelugeHeap* frunk_heap() {
	init_heaps();
	return g_frunk;
}

DelugeHeap* owning_heap(void* ptr) {
	init_heaps();
	auto v = reinterpret_cast<uintptr_t>(ptr);
	if (v >= g_sram_lo && v < g_sram_hi) {
		return g_sram;
	}
	if (v >= g_frunk_lo && v < g_frunk_hi) {
		return g_frunk;
	}
	if (v >= g_sdram_lo && v < g_sdram_hi) {
		return g_sdram;
	}
	return nullptr;
}

std::size_t sram_size() {
	init_heaps();
	return static_cast<std::size_t>(g_sram_hi - g_sram_lo);
}

std::size_t sdram_size() {
	init_heaps();
	return static_cast<std::size_t>(g_sdram_hi - g_sdram_lo);
}

void* alloc_fast(std::size_t size, std::size_t align) {
	init_heaps();
	void* p = deluge_alloc(g_sram, size, align); // SRAM first
	if (p == nullptr) {
		p = deluge_alloc(g_sdram, size, align); // fall back to SDRAM (== allocMaxSpeed)
	}
	return p;
}
void* alloc_sdram(std::size_t size, std::size_t align) {
	init_heaps();
	return deluge_alloc(g_sdram, size, align);
}
void* alloc_external(std::size_t size, std::size_t align) {
	init_heaps();
	return deluge_alloc(g_sdram, size, align); // external collapsed into the one SDRAM heap
}
void dealloc(void* ptr) {
	deluge_free(owning_heap(ptr), ptr); // deluge_free tolerates a null heap (no-op)
}
std::size_t usable_size(void* ptr) {
	return deluge_usable_size(owning_heap(ptr), ptr);
}

} // namespace deluge::memory
