/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "definitions.h"
#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include <array>
#include <cstdint>

class Sample;
class SampleCluster;
class SampleCache;

/// Header data for a cluster. The actual cluster data is expected to be in the same allocation, after this class
/// member. To correctly allocate an instance of this class, you must also allocate Cluster::size bytes with enough
/// padding to safely absorb an offset of at least CACHE_LINE_SIZE.
// A Cluster's backing comes from the resource manager's uniform cluster slab; the manager owns its
// residency + eviction (leased SAMPLE / PERC clusters via the owner's Asset; unleased SAMPLE_CACHE
// clusters via the cache's Asset). Constructed via the asset materialize/construct callbacks (placement
// `new` into a slab slot), not a self-allocating factory.
class Cluster final {
public:
	constexpr static size_t kSizeFAT16Max = 32768;
	enum class Type {
		EMPTY,
		SAMPLE,
		GENERAL_MEMORY,
		SAMPLE_CACHE,
		PERC_CACHE_FORWARDS,
		PERC_CACHE_REVERSED,
		OTHER,
	};

	void destroy();

	// Clusters are allocated from the backing slab, so their memory MUST be released through the
	// slab (GMA::freeSdram -> slab_release) to clear the slab's table entry. The normal recycle
	// path is destroy(); this class operator delete is a safety net so a stray `delete someCluster`
	// routes to freeSdram too instead of the global operator delete (which frees the heap block but
	// leaks the slab slot — exhausting the slab table and breaking later allocations).
	static void operator delete(void* ptr);

	static size_t size;
	static size_t size_magnitude;
	static void setSize(size_t size);

	/// Warning! do not call this constructor directly! It must be called via placement `new`
	/// after allocating a region with the General Memory Allocator!
	Cluster() = default;
	void convertDataIfNecessary();
	void addReason();

	// The resource-manager Asset that owns this cluster's residency for the *leased* (reason-
	// tracked) kinds — SAMPLE (the sample's asset) and PERC_CACHE_* (the sample's per-direction
	// perc asset) — or DELUGE_RESOURCE_NO_ASSET otherwise. Used by addReason /
	// removeReasonFromCluster to route a reason to a manager lease. SAMPLE_CACHE clusters are
	// unleased (never reasoned), so they're excluded here and managed via the cache's Asset.
	uint32_t resourceLeaseAssetId() const;

	// Hard-lease ("reason") count for this cluster — the single source of truth lives in the resource
	// manager's chunk slot, read O(1) via `resourceSlot` (the slot handle, set at creation). Replaces
	// the old `numReasonsToBeLoaded` mirror field. 0 if the cluster isn't a manager chunk yet.
	[[nodiscard]] uint32_t leaseCount() const;

	Cluster::Type type;
	uint32_t clusterIndex = 0;

	// Handle to this cluster's chunk slot in the resource manager (set at creation via
	// deluge_resource_slot_of). 0xFFFFFFFF == DELUGE_RESOURCE_NO_SLOT (literal here so this widely-
	// included header needn't pull in deluge_resource.h).
	uint32_t resourceSlot = 0xFFFFFFFF;
	int8_t numReasonsHeldBySampleRecorder = 0;
	bool unloadable = false;
	bool extraBytesAtStartConverted = false;
	bool extraBytesAtEndConverted = false;

	Sample* sample = nullptr;
	SampleCache* sampleCache = nullptr;

	char firstThreeBytesPreDataConversion[3]{};
	bool loaded = false;

	// MUST BE THE LAST TWO MEMBERS
	alignas(4) char dummy[CACHE_LINE_SIZE]{};
	alignas(4) char data[CACHE_LINE_SIZE]{};
};
