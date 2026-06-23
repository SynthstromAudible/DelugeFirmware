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

#include "memory/general_memory_allocator.h"
#include "libdeluge/alloc.h"
#include "libdeluge/memory.h"

#include "definitions_cxx.hpp"
#include "deluge_resource.h" // resource manager (coexistence: raw-cluster residency)
#include "io/debug/log.h"
#include "memory/stealable.h"
#include "processing/engines/audio_engine.h"
#include "storage/cluster/cluster.h" // sizeof(Cluster) + Cluster::size for the slab slot
#include <cstring>

namespace {
// Under DELUGE_DETERMINISTIC_ALLOC (the off-target sim / golden builds only) every block is zeroed before it's
// handed out, so any read-before-write resolves to a defined 0 instead of recycled-block content. This makes the
// offline golden render independent of the GMA's allocation *layout*: a refactor that shifts what lands where can
// no longer flip a fixture through a latent uninitialised read. Firmware builds leave this off — the no-op inlines
// away, preserving their exact behaviour and allocation-time performance.
[[gnu::always_inline]] inline void* finalizeAlloc([[maybe_unused]] void* address,
                                                  [[maybe_unused]] uint32_t requiredSize) {
#ifdef DELUGE_DETERMINISTIC_ALLOC
	if (address != nullptr) {
		memset(address, 0, requiredSize);
	}
#endif
	return address;
}
} // namespace

// Reclaim hook for the SDRAM heap: when a live allocation can't fit, evict the
// coldest unpinned stealable (CacheManager priority policy) and return its block
// to the heap, so deluge_alloc can retry. `ctx` is the GeneralMemoryAllocator.
extern "C" bool gmaSdramReclaim(void* ctx, size_t bytesNeeded) {
	(void)bytesNeeded; // free one victim at a time; the alloc retry loop drives us
	auto* gma = static_cast<GeneralMemoryAllocator*>(ctx);
	// Coexistence (raw-cluster migration): the resource manager owns raw sample-cluster
	// residency; the CacheManager still owns everything else (caches, grain, wavetable).
	// Try the manager first, then fall back to the legacy stealable queues. (Until raw
	// clusters are routed through it, the manager is empty and try_evict returns false,
	// so this is behaviour-identical to the CacheManager-only path.)
	if (gma->resourceManager_ != nullptr && deluge_resource_try_evict(gma->resourceManager_)) {
		return true;
	}
	void* victim = gma->cacheManager.reclaimOne(gma->currentDontStealFrom_);
	if (victim == nullptr) {
		return false;
	}
	// Clusters live in the slab (release clears their table entry too); everything
	// else (GrainBuffer, WaveTableBandData, ...) is a plain heap block.
	gma->freeSdram(victim);
	return true;
}

bool GeneralMemoryAllocator::ensureClusterSystem() {
	if (clusterSlab_ != nullptr) {
		return true;
	}
	// Lazily create on the first cluster use, when Cluster::size has been finalized
	// to the session maximum (AudioFileManager reads the card before any cluster is
	// made, and the firmware never raises cluster size after boot — see
	// cardReinserted). Uniform slots of that size accommodate every cluster for the
	// session; a smaller reinserted card simply under-fills its slots. Backing-only:
	// the slab registers no reclaim hook, so gmaSdramReclaim (already registered) stays
	// the SDRAM heap's hook and drives eviction (manager first, then CacheManager).
	size_t slot = sizeof(Cluster) + Cluster::size;
	size_t capacity = (deluge::memory::sdram_size() / slot) + 1; // table never the limiter
	clusterSlab_ = deluge_slab_create_unmanaged(deluge::memory::sdram_heap(), slot, capacity);
	if (clusterSlab_ == nullptr) {
		return false;
	}
#ifdef DELUGE_DETERMINISTIC_ALLOC
	// Sim/golden only: zero each cluster slot on acquire so a read-before-write of slot
	// memory (e.g. a SampleCache reading interpolation-overhang bytes past its write
	// position) is a defined 0, not layout-dependent recycled content — keeps the offline
	// render independent of heap layout. Firmware leaves it off (speed; behaviour unchanged).
	deluge_slab_set_zero_on_acquire(clusterSlab_, true);
#endif
	// Stand up the resource manager alongside (coexistence): it shares the cluster
	// slab and owns raw SAMPLE-cluster residency. Created *unhooked* — gmaSdramReclaim
	// stays the heap's reclaim hook and drives both coordinators.
	constexpr size_t kAssetCap = 512; // max distinct samples resident at once
	resourceManager_ = deluge_resource_create_unhooked(deluge::memory::sdram_heap(), kAssetCap, capacity);
	if (resourceManager_ != nullptr) {
		deluge_resource_set_slab(resourceManager_, clusterSlab_);
	}
	return true;
}

void* GeneralMemoryAllocator::acquireCluster(void* dontStealFromThing) {
	if (!ensureClusterSystem()) {
		return nullptr;
	}
	currentDontStealFrom_ = dontStealFromThing;
	void* p = deluge_slab_acquire(clusterSlab_, nullptr); // owner unused (no on_evict)
	currentDontStealFrom_ = nullptr;
	return p;
}

DelugeResource* GeneralMemoryAllocator::resourceManager() {
	ensureClusterSystem(); // creates the slab + manager if needed; leaves resourceManager_ set (or null on OOM)
	return resourceManager_;
}

void GeneralMemoryAllocator::freeSdram(void* address) {
	// A cluster pointer must be released through the slab so its table entry is cleared
	// (a bare deluge_free would leave a dangling slot). deluge_slab_release returns false
	// for non-slab pointers, so fall through to a direct heap free for those.
	if (clusterSlab_ != nullptr && deluge_slab_release(clusterSlab_, address)) {
		return;
	}
	deluge_free(deluge::memory::sdram_heap(), address);
}

GeneralMemoryAllocator::GeneralMemoryAllocator() : lock(false) {
	// Heaps (fast SRAM, unified SDRAM, small-internal frunk) are owned by
	// deluge::memory and built from the BSP region descriptors + linker symbols.
	// The GMA is just the stealable/reclaim coordinator now: it registers the SDRAM
	// heap's reclaim hook, which evicts the coldest unpinned stealable (CacheManager
	// priority policy) when a live allocation can't fit — the unified pool over TLSF.
	deluge::memory::init_heaps();
	deluge_heap_register_reclaim(deluge::memory::sdram_heap(), gmaSdramReclaim, this);
}
#if TEST_GENERAL_MEMORY_ALLOCATION
uint32_t totalMallocTime = 0;
int32_t numMallocTimes = 0;
#endif
constexpr size_t kInternalSwitchSize = 128; // tiny internal allocs go to the frunk heap (allocInternal)

extern "C" void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, mayUseOnChipRam, false, nullptr);
}
extern "C" void delugeDealloc(void* address) {
#ifdef IN_UNIT_TESTS
	free(address);
#else
	GeneralMemoryAllocator::get().dealloc(address);
#endif
}
void* GeneralMemoryAllocator::allocExternal(uint32_t requiredSize) {
	// General SDRAM allocation: any cold stealable may yield (no protection). The
	// heap's reclaim hook (gmaSdramReclaim) handles eviction + reentrancy (depth-1).
	currentDontStealFrom_ = nullptr;
	return deluge_alloc(deluge::memory::sdram_heap(), requiredSize, 16);
}

void* GeneralMemoryAllocator::allocInternal(uint32_t requiredSize) {
	// Tiny internal allocations go to the small-internal ("frunk") heap so they can't
	// fragment / starve the main SRAM heap; larger ones (or if frunk is full/absent)
	// fall through to the main SRAM heap.
	void* address = nullptr;
	if (requiredSize < kInternalSwitchSize) {
		address = deluge_alloc(deluge::memory::frunk_heap(), requiredSize, 16);
	}
	if (address == nullptr) {
		address = deluge_alloc(deluge::memory::sram_heap(), requiredSize, 16);
	}
	return address;
}

// Watch the heck out - in the older V3.1 branch, this had one less argument - makeStealable was missing - so in code
// from there, thingNotToStealFrom could be interpreted as makeStealable! requiredSize 0 means get biggest allocation
// available.
void* GeneralMemoryAllocator::alloc(uint32_t requiredSize, bool mayUseOnChipRam, bool makeStealable,
                                    void* thingNotToStealFrom) {

	if (lock) {
		return nullptr; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they
		                // could extend the stack an unspecified amount
	}

	void* address = nullptr;

	// Only allow allocating stealables in stelable region
	if (!makeStealable) {
		// If internal is allowed, try that first
		if (mayUseOnChipRam) {
			address = allocInternal(requiredSize);

			if (address != nullptr) {
				return finalizeAlloc(address, requiredSize);
			}

			AudioEngine::logAction("internal allocation failed");
		}

		// Second try external region
		address = allocExternal(requiredSize);

		if (address) {
			return finalizeAlloc(address, requiredSize);
		}

		AudioEngine::logAction("external allocation failed");

		D_PRINTLN("Dire memory, resorting to stealable area");
	}

#if TEST_GENERAL_MEMORY_ALLOCATION
	if (requiredSize < 1) {
		D_PRINTLN("alloc too little a bit");
		while (1) {}
	}
#endif

	// Stealable allocation = a normal SDRAM block; its "stealable" nature is its
	// CacheManager queue membership (the owner queues it when reasons hit 0, as
	// before). thingNotToStealFrom protects the in-flight cluster from the reclaim
	// hook that may fire during this very alloc.
	currentDontStealFrom_ = thingNotToStealFrom;
	address = deluge_alloc(deluge::memory::sdram_heap(), requiredSize, 16);
	currentDontStealFrom_ = nullptr;
	return finalizeAlloc(address, requiredSize);
}

void GeneralMemoryAllocator::dealloc(void* address) {
	if (address == nullptr) [[unlikely]] {
		return;
	}
	deluge::memory::dealloc(address);
}

void GeneralMemoryAllocator::putStealableInQueue(Stealable* stealable, StealableQueue q) {
	// Stealables only ever live in the stealable region, which is paired with this
	// single CacheManager — go straight to the resource layer instead of reaching
	// through the allocator region (the allocator no longer exposes it).
	cacheManager.QueueForReclamation(q, stealable);
}

void GeneralMemoryAllocator::putStealableInAppropriateQueue(Stealable* stealable) {
	StealableQueue q = stealable->getAppropriateQueue();
	putStealableInQueue(stealable, q);
}
