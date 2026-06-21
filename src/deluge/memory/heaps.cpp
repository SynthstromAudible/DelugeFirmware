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
#include <cstdint>
#else
#include "memory/general_memory_allocator.h"
#endif

namespace deluge::memory {

#if DELUGE_USE_RUST_ALLOC

namespace {
DelugeHeap* g_sram = nullptr;
DelugeHeap* g_sdram = nullptr;
uintptr_t g_sram_lo = 0, g_sram_hi = 0;
uintptr_t g_sdram_lo = 0, g_sdram_hi = 0;
bool g_built = false;

// The heap a (typed-allocator) pointer belongs to, by address range.
DelugeHeap* heap_for(void* p) {
	auto v = reinterpret_cast<uintptr_t>(p);
	if (v >= g_sram_lo && v < g_sram_hi) {
		return g_sram;
	}
	if (v >= g_sdram_lo && v < g_sdram_hi) {
		return g_sdram;
	}
	return nullptr;
}
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
}

DelugeHeap* sram_heap() {
	init_heaps();
	return g_sram;
}

DelugeHeap* sdram_heap() {
	init_heaps();
	return g_sdram;
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
	deluge_free(heap_for(ptr), ptr); // deluge_free tolerates a null heap (no-op)
}
std::size_t usable_size(void* ptr) {
	return deluge_usable_size(heap_for(ptr), ptr);
}

#else // !DELUGE_USE_RUST_ALLOC — legacy GeneralMemoryAllocator/MemoryRegion path.

void init_heaps() {
}
DelugeHeap* sram_heap() {
	return nullptr;
}
DelugeHeap* sdram_heap() {
	return nullptr;
}
void* alloc_fast(std::size_t size, std::size_t /*align*/) {
	return GeneralMemoryAllocator::get().allocMaxSpeed(size);
}
void* alloc_sdram(std::size_t size, std::size_t /*align*/) {
	return GeneralMemoryAllocator::get().allocLowSpeed(size);
}
void* alloc_external(std::size_t size, std::size_t /*align*/) {
	return GeneralMemoryAllocator::get().allocExternal(size);
}
void dealloc(void* ptr) {
	GeneralMemoryAllocator::get().dealloc(ptr);
}
std::size_t usable_size(void* ptr) {
	return GeneralMemoryAllocator::get().getAllocatedSize(ptr);
}

#endif

} // namespace deluge::memory
