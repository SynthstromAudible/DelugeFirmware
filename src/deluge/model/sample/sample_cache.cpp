/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/sample/sample_cache.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "util/misc.h"
#include <new>

#include "deluge_resource.h" // resource manager: cache clusters are a construct-only Asset

// === Resource-manager Source for a SampleCache's clusters =====================
// A cache cluster is written incrementally during playback (not read from disk), so the
// asset has NO materialize — only `construct` (init the Cluster object; the cache fills the
// data) and `on_evict` (drop + ~Cluster). cost=CPU; the asset is self_protect (unleased
// cache clusters mustn't be self-evicted mid-write) + evict_tail_first (clusterStolen
// discards higher-index siblings, so only the highest may be evicted).
static void sampleCacheConstruct(void* /*ctx*/, void* owner, uint32_t index, void* dest) {
	auto* sampleCache = static_cast<SampleCache*>(owner);
	auto* cluster = new (dest) Cluster();
	cluster->type = Cluster::Type::SAMPLE_CACHE;
	cluster->sampleCache = sampleCache;
	cluster->clusterIndex = index;
	cluster->resourceSlot = deluge_resource_slot_of(GeneralMemoryAllocator::get().resourceManager(), dest);
	// cache clusters are unleased (leaseCount() == 0) until the reader pins one.
}

static void sampleCacheEvict(void* /*ctx*/, void* owner, uint32_t index) {
	static_cast<SampleCache*>(owner)->onCacheEvict(index);
}

SampleCache::SampleCache(Sample* newSample, int32_t newNumClusters, int32_t newWaveformLengthBytes,
                         int32_t newPhaseIncrement, int32_t newTimeStretchRatio, int32_t newSkipSamplesAtStart,
                         bool newReversed) {
	sample = newSample;
	phaseIncrement = newPhaseIncrement;
	timeStretchRatio = newTimeStretchRatio;
	writeBytePos = 0;
#if ALPHA_OR_BETA_VERSION
	numClusters = newNumClusters;
#endif
	waveformLengthBytes = newWaveformLengthBytes;
	skipSamplesAtStart = newSkipSamplesAtStart;
	reversed = newReversed;
	/*
	for (int32_t i = 0; i < numClusters; i++) {
	    clusters[i] = NULL; // We don't actually have to initialize these, since writeBytePos tells us how many are
	"valid"
	}
	*/

	// Define this cache's resource-manager Asset (construct-only; the manager owns residency
	// + eviction for its clusters). The manager is the sole SDRAM evictor now, so a missing
	// manager / full asset table is fatal (no legacy fallback) — size the asset cap for it.
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	resourceAssetId =
	    (mgr != nullptr) ? deluge_resource_define_asset(mgr, this, nullptr /*materialize: never*/, sampleCacheEvict,
	                                                    nullptr, DELUGE_RESOURCE_COST_CPU, DELUGE_RESOURCE_BACKING_SLAB)
	                     : DELUGE_RESOURCE_NO_ASSET;
	if (resourceAssetId == DELUGE_RESOURCE_NO_ASSET) {
		FREEZE_WITH_ERROR("RSC1"); // resource asset table exhausted (raise kAssetCap)
	}
	deluge_resource_set_construct(mgr, resourceAssetId, sampleCacheConstruct);
	deluge_resource_set_self_protect(mgr, resourceAssetId, true);
	deluge_resource_set_evict_tail_first(mgr, resourceAssetId, true);
	// Inherit the source sample's project relevance: a current-song sample's caches should outlive
	// no-song data too (Sample::applyProjectReference toggles this asset thereafter).
	if (sample->isProjectReferenced()) {
		deluge_resource_reference(mgr, resourceAssetId);
	}
}

SampleCache::~SampleCache() {
	// Retire the Asset: the manager frees all our resident clusters highest-index-first
	// (via onCacheEvict, which nulls clusters[] as it goes).
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr != nullptr) {
		deluge_resource_release_asset(mgr, resourceAssetId);
	}
	resourceAssetId = DELUGE_RESOURCE_NO_ASSET;
}

void SampleCache::clusterStolen(int32_t clusterIndex) {

#if ALPHA_OR_BETA_VERSION
	if (clusterIndex < 0) {
		FREEZE_WITH_ERROR("E296");
	}
	else if (clusterIndex >= numClusters) {
		FREEZE_WITH_ERROR("E297");
	}
#endif

	D_PRINTLN("cache Cluster stolen");

	// There's now no point in having any further Clusters
	unlinkClusters(clusterIndex + 1, false); // Must do this before changing writeBytePos

	uint8_t bytesPerSample = sample->numChannels * kCacheByteDepth;

	// Make it a multiple of bytesPerSample - but round up.
	// If you try and simplify this, make sure it still works for 0 and doesn't go negative or anything!
	writeBytePos =
	    (uint32_t)((uint32_t)((clusterIndex << Cluster::size_magnitude) + bytesPerSample - 1) / bytesPerSample)
	    * bytesPerSample;

#if ALPHA_OR_BETA_VERSION
	if (writeBytePos < 0) {
		FREEZE_WITH_ERROR("E298");
	}
	else if (writeBytePos >= waveformLengthBytes) {
		FREEZE_WITH_ERROR("E299");
	}

	int32_t numExistentClusters = getNumExistentClusters(writeBytePos);

	if (numExistentClusters != clusterIndex) {
		FREEZE_WITH_ERROR("E295");
	}
	clusters[clusterIndex] = nullptr; // No need to remove this first Cluster from a queue or anything - that's already
	                                  // all done by the thing that's stealing it
#endif
}

// Resource-manager on_evict: the manager has chosen this cluster (always the highest-index,
// since the asset is evict_tail_first) and will free its backing slab slot right after we
// return. Destruct the Cluster object + drop our pointer, then run clusterStolen's truncation
// (no higher siblings to discard, so it doesn't cascade).
void SampleCache::onCacheEvict(int32_t clusterIndex) {
	Cluster* cluster = clusters[clusterIndex];
	clusters[clusterIndex] = nullptr;
	if (cluster != nullptr) {
		cluster->~Cluster(); // manager frees the slot
	}
	clusterStolen(clusterIndex); // truncate writeBytePos (clusters[clusterIndex] already null)
}

void SampleCache::unlinkClusters(int32_t startAtIndex, bool beingDestructed) {
	(void)beingDestructed;
	// And there's now no point in having any further Clusters
	int32_t numExistentClusters = getNumExistentClusters(writeBytePos);
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	for (int32_t i = startAtIndex; i < numExistentClusters; i++) {
		if (ALPHA_OR_BETA_VERSION && !clusters[i]) {
			FREEZE_WITH_ERROR("E167");
		}

		// Owner-driven drop (truncation): destruct the object and drop the chunk directly
		// (no on_evict — we're managing our own pointer here).
		Cluster* cluster = clusters[i];
		clusters[i] = nullptr;
		cluster->~Cluster();
		deluge_resource_evict_chunk(mgr, cluster);
	}
}

// You must be sure before calling this that newWriteBytePos is a multiple of (sample->numChannels * CACHE_BYTE_DEPTH)
void SampleCache::setWriteBytePos(int32_t newWriteBytePos) {

#if ALPHA_OR_BETA_VERSION
	if (newWriteBytePos < 0) {
		FREEZE_WITH_ERROR("E300");
	}
	if (newWriteBytePos > waveformLengthBytes) {
		FREEZE_WITH_ERROR("E301");
	}

	uint32_t bytesPerSample = sample->numChannels * kCacheByteDepth;
	if (newWriteBytePos != (uint32_t)newWriteBytePos / bytesPerSample * bytesPerSample) {
		FREEZE_WITH_ERROR("E302");
	}
#endif

	// When setting it earlier, we may have to discard some Clusters.
	// Remember, a cache cluster actually gets (bytesPerSample - 1) extra usable bytes after it.

	int32_t newNumExistentClusters = getNumExistentClusters(newWriteBytePos);
	unlinkClusters(newNumExistentClusters, false);

	writeBytePos = newWriteBytePos;

	if (ALPHA_OR_BETA_VERSION && getNumExistentClusters(writeBytePos) != newNumExistentClusters) {
		FREEZE_WITH_ERROR("E294");
	}
}

// Does not move the new Cluster to the appropriate "availability queue", because it's expected that the caller is just
// about to call getCluster(), to get it, which will call prioritizeNotStealingCluster(), and that'll do it
bool SampleCache::setupNewCluster(int32_t clusterIndex) {
	// D_PRINTLN("writing cache to new Cluster");

#if ALPHA_OR_BETA_VERSION
	if (clusterIndex >= numClusters) {
		FREEZE_WITH_ERROR("E126");
	}
	if (clusterIndex > getNumExistentClusters(writeBytePos)) {
		FREEZE_WITH_ERROR("E293");
	}
#endif

	// Manager-owned: request constructs the Cluster (sampleCacheConstruct sets type/cache/index)
	// + leases it; release immediately so it's resident-but-unleased (evictable from birth).
	// The cache writes into it; reads check the pointer.
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	void* p = deluge_resource_request(mgr, resourceAssetId, clusterIndex, sizeof(Cluster) + Cluster::size);
	if (p == nullptr) {
		D_PRINTLN("allocation fail");
		return false;
	}
	deluge_resource_release(mgr, p);
	clusters[clusterIndex] = reinterpret_cast<Cluster*>(p);
	return true;
}

Cluster* SampleCache::getCluster(int32_t clusterIndex) {
	// Manager-owned: recency (not a manual queue reorder) keeps later clusters first to evict;
	// tail-first ordering enforces the prefix dependency. Just mark it used.
	if (clusters[clusterIndex] != nullptr) {
		deluge_resource_touch(GeneralMemoryAllocator::get().resourceManager(), clusters[clusterIndex]);
	}
	return clusters[clusterIndex];
}

int32_t SampleCache::getNumExistentClusters(int32_t thisWriteBytePos) {
	int32_t bytesPerSample = sample->numChannels * kCacheByteDepth;

	// Remember, a cache Cluster actually gets (bytesPerSample - 1) extra usable bytes after it.
	int32_t numExistentClusters = (thisWriteBytePos + Cluster::size - bytesPerSample) >> Cluster::size_magnitude;

#if ALPHA_OR_BETA_VERSION
	if (numExistentClusters < 0) {
		FREEZE_WITH_ERROR("E303");
	}
	if (numExistentClusters > numClusters) {
		FREEZE_WITH_ERROR("E304");
	}
#endif

	return numExistentClusters;
}
