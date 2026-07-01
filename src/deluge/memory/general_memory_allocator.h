/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#pragma once

#include "definitions_cxx.hpp"
#include "memory/heaps.h" // deluge::memory heaps (+ opaque DelugeHeap)

struct DelugeSlab;     // libdeluge/alloc.h — opaque here; full C ABI included in the .cpp
struct DelugeResource; // deluge_resource.h — the resource manager handle (opaque here)

/*
 * ======================= MEMORY ALLOCATION ========================
 *
 * The Deluge codebase uses a custom memory allocation system, largely necessitated by the fact that
 * the Deluge’s CPU has 3MB ram, plus the Deluge has an external 64MB SDRAM IC, and both of these
 * need to have dynamic memory allocation as part of the same system.
 *
 * The internal RAM on the CPU is a bit faster, so is allocated first when available.
 * But huge blocks of data like cached Clusters of audio data from the SD card are always
 * placed on the external RAM IC because they would overwhelm the internal RAM too quickly,
 * preventing potentially thousands of small objects which need to be accessed all the time
 * from being placed in that fast internal RAM.
 *
 * Various objects or pieces of data remain loaded (cached) in RAM even when they are no longer
 * strictly needed (the main example being audio Clusters). Eviction of these is owned entirely by
 * the Rust resource manager (deluge_resource.h), layered on the TLSF heap + cluster slab: it is the
 * SDRAM heap's sole reclaim hook. The GMA is now just thin forwarding to deluge::memory (heaps.h)
 * plus lazy creation of the cluster slab + resource manager.
 */

class GeneralMemoryAllocator {
public:
	GeneralMemoryAllocator();

	void* alloc(uint32_t requiredSize, bool mayUseOnChipRam = true);
	void dealloc(void* address);
	void* allocExternal(uint32_t requiredSize);
	void* allocInternal(uint32_t requiredSize);
	void test();
	void testShorten(int32_t i);
	void testMemoryDeallocated(void* address);

	bool lock;

	// Backing-only slab for the uniform Cluster blocks (slab-izing isolates the high-churn cluster
	// traffic from mixed general SDRAM allocs so it can't fragment the unified pool). The resource
	// manager allocates its slots and drives eviction (the slab registers no reclaim hook of its
	// own). Created lazily alongside the manager when Cluster::size is finalized to the session
	// maximum (the firmware never raises cluster size after boot). nullptr until then.
	DelugeSlab* clusterSlab_{nullptr};

	// The Rust resource manager: the sole SDRAM reclaim coordinator. Owns cluster residency (SAMPLE
	// / cache / perc) + adopted AudioFile objects + wavetable bands, value-scoring eviction. Created
	// lazily with the cluster slab (hooked as the heap's reclaim callback). nullptr until then.
	DelugeResource* resourceManager_{nullptr};

	// The resource manager that owns SDRAM eviction (creating the cluster slab + manager on first
	// use). A Sample / SampleCache defines its Asset against this; the manager allocates slab slots
	// and drives eviction. Returns nullptr only if the slab/manager couldn't be created (OOM).
	DelugeResource* resourceManager();

	// Free an SDRAM block: if it's a cluster slot, release it through the slab
	// (clearing the table entry too); otherwise free it from the heap directly.
	void freeSdram(void* address);

	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator generalMemoryAllocator;
		return generalMemoryAllocator;
	}

	// MEM_GUARD periodic heap-integrity walk (called from AudioEngine::routine()). Validates every heap's
	// physical block chain via deluge_heap_check() across the libdeluge/alloc.h boundary (the Rust
	// deluge_alloc replacement for the legacy MemoryRegion boundary-tag walk); freezes on corruption.
	void checkEverythingOk(char const* errorString);

private:
	// Lazily stand up the cluster slab + resource manager (idempotent). Both share the
	// uniform Cluster slot size, finalized to the session maximum by boot. Returns true
	// if the slab exists afterwards.
	bool ensureClusterSystem();
};

extern "C" {
void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam = true);
void delugeDealloc(void* address);
}
