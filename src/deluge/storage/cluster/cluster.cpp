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

#include "storage/cluster/cluster.h"
#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "model/sample/sample_cache.h"
#include "storage/audio/audio_file_manager.h"
#include "util/misc.h"

#include "deluge_resource.h" // resource manager: manager-owned clusters lease instead of queueing
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>

// ComputedChunk must stay standard-layout + non-polymorphic: it's placement-new'd into a raw slab
// slot (as a pure metadata header) and its payload lives, via payload_, elsewhere in the same slot.
// (The streamed chunk's equivalent guarantee is the Rust struct's own Drop-free POD contract.)
static_assert(std::is_standard_layout_v<ComputedChunk> && !std::is_polymorphic_v<ComputedChunk>);

// The slot-geometry guard proof (see cluster.h). The payload sits at kChunkPayloadOffset from the slot
// base, so the front guard is (kChunkPayloadOffset - sizeof(header)). Each guard must be >= CACHE_LINE_SIZE
// to keep the SD-read cache-maintenance range-rounding off live neighbour data (the header below the
// front guard, the next slot above the trailing guard), AND >= the application edge-slack reach
// (kFrontSlackBytes/kTrailingSlackBytes). These use sizeof, so they hold on both the ARM32 firmware and
// the x86-64 sim despite differing header sizes (kChunkPayloadOffset auto-fits via std::max(sizeof)).
static_assert(kChunkPayloadOffset - sizeof(ComputedChunk) >= CACHE_LINE_SIZE); // front guard >= a cache line (DMA)
static_assert(kChunkPayloadOffset - sizeof(ComputedChunk) >= kFrontSlackBytes);
static_assert(kChunkTrailingGuard >= CACHE_LINE_SIZE && kChunkTrailingGuard >= kTrailingSlackBytes); // trailing guard

// The universal size of all clusters
size_t Cluster::size = 32768;
size_t Cluster::size_magnitude = 15;

void Cluster::set_size(size_t size) {
	Cluster::size = size;

	// Find the highest bit set
	Cluster::size_magnitude = 32 - __builtin_clz(size) - 1;
}

// Safety net (see the header): release through the slab so the table entry is cleared.
// freeSdram() falls back to a plain heap free for any non-slab pointer.
void ComputedChunk::operator delete(void* ptr) {
	GeneralMemoryAllocator::get().freeSdram(ptr);
}

// The resource-manager Asset that owns this chunk's residency for the *leased* (reason-tracked) perc
// kinds (the sample's per-direction perc asset). SAMPLE_CACHE chunks are unleased (never reasoned),
// so they return NO_ASSET here and are managed via their cache's own Asset instead.
uint32_t ComputedChunk::resource_lease_asset_id() const {
	switch (type) {
	case Cluster::Type::PERC_CACHE_FORWARDS:
		return (sample != nullptr) ? sample->percCacheAssetId[0] : DELUGE_RESOURCE_NO_ASSET;
	case Cluster::Type::PERC_CACHE_REVERSED:
		return (sample != nullptr) ? sample->percCacheAssetId[1] : DELUGE_RESOURCE_NO_ASSET;
	default:
		return DELUGE_RESOURCE_NO_ASSET; // SAMPLE_CACHE is unleased
	}
}

namespace deluge::cluster {

void free_chunk(void* chunk) {
	// Release back to the slab so its table entry is cleared (a bare deluge_free / delugeDealloc
	// would leave a dangling slot pointing at freed memory). Both chunk structs are trivially
	// destructible (POD / char-array members), so no explicit destructor call is needed.
	GeneralMemoryAllocator::get().freeSdram(chunk);
}

void add_lease(void* chunk) {
	// Manager-owned leased clusters (SAMPLE / PERC) are pinned by a resource-manager lease (they're
	// never on a stealable queue). Take a lease so the manager won't evict a chunk the caller still
	// holds. The lease count lives in the manager's chunk slot (read via deluge::cluster::lease_count()).
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr != nullptr) {
		deluge_resource_add_lease(mgr, chunk); // hit-only lease bump on this resident chunk
	}
}

void release_lease(void* chunk) {
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr != nullptr) {
		deluge_resource_release(mgr, chunk); // unlease (backing ptr == the chunk's own address)
	}
}

uint32_t lease_count(uint32_t resource_slot) {
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	return (mgr != nullptr) ? deluge_resource_lease_count_by_slot(mgr, resource_slot) : 0;
}

// Shared reason-drop body for either chunk role. Every cluster is a manager-owned chunk — SAMPLE /
// PERC leased via the owner's Asset, SAMPLE_CACHE resident via the cache's Asset (and leased by the
// low-level reader while it streams it). A removed reason is just a manager lease drop: the cluster
// stays resident (cached, evictable under pressure), never enqueued/destroyed here. Only the chunk's
// own address + resource_slot are needed, so this is role-agnostic (the two chunk types share no base).
static void remove_reason_impl(void* chunk, uint32_t resource_slot, [[maybe_unused]] char const* error_code) {
	if (ALPHA_OR_BETA_VERSION && lease_count(resource_slot) == 0) {
		FREEZE_WITH_ERROR(error_code); // removing a reason that was never there
	}
	release_lease(chunk); // unlease (backing ptr == the chunk's own address)
}

void remove_reason(ComputedChunk& chunk, char const* error_code) {
	remove_reason_impl(&chunk, chunk.resource_slot, error_code);
}

} // namespace deluge::cluster
