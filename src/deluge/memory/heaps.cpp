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
#include <cstdlib> // DELUGE_SIM_SDRAM_MB (sim/golden eviction testing)
#include <cstring>

// Linker-defined small-internal ("frunk") SRAM region — a reserved slack area that
// is NOT a deluge_memory_region, so its heap is built from these symbols here.
// (The Rust deluge_alloc crate stays linker-symbol-free; this app-layer file may
// reference them, as the GeneralMemoryAllocator historically did.)
// NOLINTBEGIN — addresses are only meaningful via &symbol
extern uint32_t __frunk_bss_end;
extern uint32_t __frunk_slack_end;
// NOLINTEND

namespace deluge::memory {

// Tiny internal allocations go to the small-internal ("frunk") heap (matches the
// old GeneralMemoryAllocator::kInternalSwitchSize).
constexpr std::size_t kInternalSwitchSize = 128;

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
			size_t sdram_size = r.size;
#ifdef DELUGE_DETERMINISTIC_ALLOC
			// Sim/golden only: DELUGE_SIM_SDRAM_MB clamps the usable SDRAM (the region array is
			// still full-size; we just build a smaller heap over its front) to force memory
			// pressure / eviction in headless renders — exercises the resource manager's
			// evict+recompute path. Done HERE, not in the BSP, so the binary layout is unchanged
			// (existing goldens stay valid; only the heap size differs). Firmware compiles this out.
			if (const char* mb = std::getenv("DELUGE_SIM_SDRAM_MB")) {
				long v = std::atol(mb);
				if (v > 0 && static_cast<size_t>(v) * 1024u * 1024u < sdram_size) {
					sdram_size = static_cast<size_t>(v) * 1024u * 1024u;
				}
			}
#endif
			g_sdram = deluge_heap_create(r.base, sdram_size);
			g_sdram_lo = reinterpret_cast<uintptr_t>(r.base);
			g_sdram_hi = g_sdram_lo + sdram_size;
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

namespace {
// Under DELUGE_DETERMINISTIC_ALLOC (the off-target sim / golden builds only) every block is zeroed before
// it's handed out, so any read-before-write resolves to a defined 0 — the offline golden render becomes
// independent of allocation layout. Firmware leaves it off; the no-op inlines away. (Mirrors the old
// GeneralMemoryAllocator::finalizeAlloc, now that these helpers are the primary allocation surface.)
[[gnu::always_inline]] inline void* finalize([[maybe_unused]] void* p, [[maybe_unused]] std::size_t size) {
#ifdef DELUGE_DETERMINISTIC_ALLOC
	if (p != nullptr) {
		std::memset(p, 0, size);
	}
#endif
	return p;
}
} // namespace

void* alloc_fast(std::size_t size, std::size_t align) {
	init_heaps();
	void* p = nullptr;
	if (size < kInternalSwitchSize) {
		p = deluge_alloc(g_frunk, size, align); // tiny: small-internal heap
	}
	if (p == nullptr) {
		p = deluge_alloc(g_sram, size, align); // fast internal SRAM
	}
	if (p == nullptr) {
		p = deluge_alloc(g_sdram, size, align); // fall back to SDRAM (== allocMaxSpeed)
	}
	return finalize(p, size);
}
void* alloc_sdram(std::size_t size, std::size_t align) {
	init_heaps();
	return finalize(deluge_alloc(g_sdram, size, align), size);
}
void* alloc_external(std::size_t size, std::size_t align) {
	init_heaps();
	return finalize(deluge_alloc(g_sdram, size, align), size); // external collapsed into the one SDRAM heap
}
void dealloc(void* ptr) {
	deluge_free(owning_heap(ptr), ptr); // deluge_free tolerates a null heap (no-op)
}
std::size_t usable_size(void* ptr) {
	return deluge_usable_size(owning_heap(ptr), ptr);
}
std::size_t shrink(void* ptr, std::size_t new_size) {
	DelugeHeap* h = owning_heap(ptr);
	deluge_realloc(h, ptr, new_size, 16); // in-place shrink, pointer stable
	return deluge_usable_size(h, ptr);
}
std::size_t shrink_left(void* /*ptr*/, std::size_t /*amount_to_shorten*/, std::size_t /*num_bytes_to_move_right*/) {
	return 0; // unsupported on the deluge_alloc heaps (no payload move-left)
}

} // namespace deluge::memory
