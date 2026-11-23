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

struct DelugeSlab;     // deluge_base_alloc.h — opaque here; full C ABI included in the .cpp
struct DelugeResource; // deluge_storage_resource.h — the resource manager handle (opaque here)

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
 * the Rust resource manager (deluge_storage_resource.h), layered on the TLSF heap + cluster slab: it is the
 * SDRAM heap's sole reclaim hook. The GMA is now just thin forwarding to deluge::memory (heaps.h)
 * plus boot's creation of the cluster slab + resource manager.
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
	// own). Created at boot alongside the manager, once Cluster::size is set to the session
	// maximum (the firmware never raises cluster size after boot). nullptr until then.
	DelugeSlab* clusterSlab_{nullptr};

	// The Rust resource manager: the sole SDRAM reclaim coordinator. Owns cluster residency (SAMPLE
	// / cache / perc) + adopted AudioFile objects + wavetable bands, value-scoring eviction. Created
	// at boot with the cluster slab (hooked as the heap's reclaim callback). nullptr until then.
	DelugeResource* resourceManager_{nullptr};

	/// @brief Creates the cluster slab and the resource manager. Boot calls it once, right after
	/// AudioFileManager::init() has set Cluster::size; a second call does nothing. Both share the
	/// uniform Cluster slot size, which this commits for the session.
	void initClusterSystem();

	/// @brief The resource manager that owns SDRAM eviction. A Sample / SampleCache defines its Asset
	/// against this; the manager allocates slab slots and drives eviction.
	/// @return The manager, or `nullptr` before boot's initClusterSystem() or if it ran out of memory.
	DelugeResource* resourceManager() const { return resourceManager_; }

	/// @brief The cluster slab.
	/// @return The slab, or `nullptr` before boot's initClusterSystem() or if it ran out of memory.
	DelugeSlab* clusterSlab() const { return clusterSlab_; }

	// Free an SDRAM block: if it's a cluster slot, release it through the slab
	// (clearing the table entry too); otherwise free it from the heap directly.
	void freeSdram(void* address);

	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator generalMemoryAllocator;
		return generalMemoryAllocator;
	}

	// MEM_GUARD periodic heap-integrity walk (called from AudioEngine::routine()). Validates every heap's
	// physical block chain via deluge_heap_check() across the deluge_base_alloc.h boundary (the Rust
	// deluge_base_alloc replacement for the legacy MemoryRegion boundary-tag walk); freezes on corruption.
	void checkEverythingOk(char const* errorString);
};

extern "C" {
void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam = true);
void delugeDealloc(void* address);
}
