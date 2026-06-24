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
#include "deluge_resource.h" // resource manager: the sole SDRAM reclaim coordinator
#include "io/debug/log.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h" // setCardRead() — commit the cluster size before slab sizing
#include "storage/cluster/cluster.h"          // sizeof(Cluster) + Cluster::size for the slab slot
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

bool GeneralMemoryAllocator::ensureClusterSystem() {
	if (clusterSlab_ != nullptr) {
		return true;
	}
	// Lazily create on the first cluster / asset use, when Cluster::size has been finalized to the
	// session maximum (AudioFileManager reads the card before any cluster is made, and the firmware
	// never raises cluster size after boot — see cardReinserted). setCardRead() commits the size
	// before we size the slab. Uniform slots of that size accommodate every cluster for the session;
	// a smaller reinserted card simply under-fills its slots.
	audioFileManager.setCardRead();
	size_t slot = sizeof(Cluster) + Cluster::size;
	size_t slabCapacity = (deluge::memory::sdram_size() / slot) + 1; // table never the limiter
	clusterSlab_ = deluge_slab_create_unmanaged(deluge::memory::sdram_heap(), slot, slabCapacity);
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
	// Stand up the resource manager alongside the slab and register it as the SDRAM heap's reclaim
	// hook (it is the sole reclaim coordinator). It owns cluster residency + adopted objects.
	//   asset_cap: one Asset per Sample + per SampleCache + 2 per perc cache — sized generously, as
	//              exhaustion is fatal (no legacy fallback).
	//   chunk_cap: every slab slot (clusters) PLUS one chunk per asset (adopted AudioFile objects),
	//              so the chunk table is never the limiter.
	constexpr size_t kAssetCap = 4096;
	size_t chunkCapacity = slabCapacity + kAssetCap;
	resourceManager_ = deluge_resource_create(deluge::memory::sdram_heap(), kAssetCap, chunkCapacity);
	if (resourceManager_ != nullptr) {
		deluge_resource_set_slab(resourceManager_, clusterSlab_);
	}
	return true;
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
	// Heaps (fast SRAM, unified SDRAM, small-internal frunk) are owned by deluge::memory and built
	// from the BSP region descriptors + linker symbols. The GMA no longer registers a reclaim hook:
	// the resource manager is the SDRAM heap's sole reclaim coordinator and registers itself when it
	// is created (lazily, on first cluster / asset use — see ensureClusterSystem). Nothing evictable
	// exists before then, so the heap needs no hook in the interim.
	deluge::memory::init_heaps();
}
#if TEST_GENERAL_MEMORY_ALLOCATION
uint32_t totalMallocTime = 0;
int32_t numMallocTimes = 0;
#endif
constexpr size_t kInternalSwitchSize = 128; // tiny internal allocs go to the frunk heap (allocInternal)

extern "C" void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, mayUseOnChipRam);
}
extern "C" void delugeDealloc(void* address) {
#ifdef IN_UNIT_TESTS
	free(address);
#else
	GeneralMemoryAllocator::get().dealloc(address);
#endif
}
void* GeneralMemoryAllocator::allocExternal(uint32_t requiredSize) {
	// General SDRAM allocation. The heap's reclaim hook (the resource manager) frees the
	// lowest-value evictable chunk + handles reentrancy if the pool is full.
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

// requiredSize 0 means get biggest allocation available.
void* GeneralMemoryAllocator::alloc(uint32_t requiredSize, bool mayUseOnChipRam) {

	if (lock) {
		return nullptr; // Prevent any weird loops in the reclaim hook, which mostly would only be bad cos they
		                // could extend the stack an unspecified amount
	}

	void* address = nullptr;

	// If internal is allowed, try that first
	if (mayUseOnChipRam) {
		address = allocInternal(requiredSize);

		if (address != nullptr) {
			return finalizeAlloc(address, requiredSize);
		}

		AudioEngine::logAction("internal allocation failed");
	}

	// Otherwise the external SDRAM pool (the resource manager evicts under pressure via the heap's
	// reclaim hook).
	address = allocExternal(requiredSize);
	if (address == nullptr) {
		AudioEngine::logAction("external allocation failed");
	}
	return finalizeAlloc(address, requiredSize);
}

void GeneralMemoryAllocator::dealloc(void* address) {
	if (address == nullptr) [[unlikely]] {
		return;
	}
	deluge::memory::dealloc(address);
}
