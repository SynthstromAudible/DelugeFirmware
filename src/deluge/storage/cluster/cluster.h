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
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

class Sample;
class SampleCache;

/// @brief Shared FAT-geometry configuration for the cluster payload types.
///
/// Not itself instantiated as a payload — ComputedChunk (below) is the real C++ chunk struct (the
/// streamed SAMPLE role's chunk lives in Rust, `deluge_sample_fill::chunk`). Cluster carries
/// the session-wide cluster size and the Type enum the computed kinds reference.
///
/// @note A chunk's backing comes from the resource manager's uniform cluster slab; the manager owns
///       its residency and eviction — leased SAMPLE (streamed) / PERC clusters via the owner's
///       Asset, unleased SAMPLE_CACHE clusters via the cache's Asset. Chunks are constructed via
///       the asset materialize/construct callbacks (placement `new` into a slab slot), never via a
///       self-allocating factory.
class Cluster final {
public:
	constexpr static size_t kSizeFAT16Max = 32768;

	/// @brief The computed-chunk kinds (ComputedChunk::type).
	///
	/// The streamed SAMPLE role carries no type — its role is fixed by its (Rust) type.
	enum class Type {
		SAMPLE_CACHE,
		PERC_CACHE_FORWARDS,
		PERC_CACHE_REVERSED,
	};

	static size_t size;
	static size_t size_magnitude;
	static void set_size(size_t size);
};

// The slab-release safety net shared by both chunk structs: chunks are allocated from the backing
// slab, so their memory MUST be released through the slab (GMA::freeSdram -> slab_release) to clear
// the slab's table entry. The normal recycle path is deluge::cluster::free_chunk(); this operator
// delete is a safety net so a stray `delete someChunk` routes to freeSdram too instead of the global
// operator delete (which frees the heap block but leaks the slab slot — exhausting the slab table
// and breaking later allocations).

/// @brief Edge-slack contract shared by both chunk structs: the application's boundary-straddle
///        reach past the payload edges (a multi-byte sample frame that spans a cluster boundary).
///
/// Distinct from, and covered by, the DMA cache-line guard documented on the structs below.
inline constexpr size_t kFrontSlackBytes = 4;    ///< The `-4 + byte_depth` frame-cursor front-underread.
inline constexpr size_t kTrailingSlackBytes = 7; ///< The stitch `Cluster::size + 7` / recorder `+5` over-reads.

/// @brief Placeholder `size` argument for a slab-backed chunk asset request.
///
/// deluge_resource_request/acquire take a `size` argument that BACKING_SLAB assets ignore — the
/// slab always hands back its fixed slot_size (computed once in general_memory_allocator.cpp),
/// never the caller's value. Every streamed-chunk/ComputedChunk asset is slab-backed, so call sites
/// requesting one pass this rather than a `sizeof(chunk) + Cluster::size` figure that reads as
/// load-bearing but isn't.
inline constexpr size_t kSlabBackedSizeIgnored = 0;

/// @brief A computed/cached chunk — repitch SampleCache or perc-cache scratch.
///
/// One of Cluster::Type::SAMPLE_CACHE / PERC_CACHE_FORWARDS / PERC_CACHE_REVERSED. Its backing comes
/// from the resource-manager cluster slab (placement-new'd into a slot); the actual cluster data
/// lives after this header in the same slot, reached via `payload_`.
///
/// @warning Only construct via placement `new` into a slab slot (asset callbacks).
///
/// @note The streamed SAMPLE role's per-cluster state lives in Rust
///       (`deluge_sample_fill::chunk::StreamedChunk`); this is the last C++ chunk struct.
struct ComputedChunk final {
	Cluster::Type type; ///< One of SAMPLE_CACHE / PERC_CACHE_FORWARDS / PERC_CACHE_REVERSED.
	uint32_t cluster_index = 0;

	/// Handle to this cluster's chunk slot in the resource manager (set at creation via
	/// deluge_resource_slot_of).
	/// @note `0xFFFFFFFF` == DELUGE_RESOURCE_NO_SLOT (literal here so this widely-included header
	///       needn't pull in deluge_resource.h).
	uint32_t resource_slot = 0xFFFFFFFF;
	Sample* sample = nullptr;
	SampleCache* sampleCache = nullptr; ///< Written by sampleCacheConstruct; currently no reads.

	/// @brief Base of this chunk's audio payload.
	///
	/// Set at construction from the slab-slot base (`dest`) plus kChunkPayloadOffset, giving it
	/// whole-slot provenance.
	/// @note Every construction path is a placement-`new` in an asset construct callback that sets
	///       this.
	std::byte* payload_ = nullptr;

	ComputedChunk() = default;

	/// @brief The resource-manager Asset that owns this chunk's residency for the *leased*
	///        (reason-tracked) perc kinds — the sample's per-direction perc asset.
	///
	/// SAMPLE_CACHE chunks are unleased (never reasoned) and managed via the cache's own Asset.
	/// @return The owning Asset id, or DELUGE_RESOURCE_NO_ASSET for unleased (SAMPLE_CACHE) chunks.
	uint32_t resource_lease_asset_id() const;

	static void operator delete(void* ptr); // slab-release safety net (see above)

	/// @brief The cluster's audio payload — Cluster::size bytes, living in the slab slot after this
	///        header. The backing region is over-allocated on both edges (see the guard note below).
	[[nodiscard]] std::span<std::byte> payload() { return {payload_, Cluster::size}; }
	/// @copydoc payload()
	[[nodiscard]] std::span<const std::byte> payload() const { return {payload_, Cluster::size}; }

	/// @brief Byte pointer positioned so a 32-bit word read yields the `byte_depth`-byte little-endian
	///        sample frame at `pos`, left-justified per the Deluge fixed-point convention.
	/// @note For the first frame of a cluster (`pos == 0`) this points up to kFrontSlackBytes BEFORE
	///       the payload, into the leading guard — those bytes are don't-care (masked out of the
	///       read). Called at cluster-boundary cadence, not per sample.
	[[nodiscard]] std::byte* frame_read_origin(uint32_t pos, uint8_t byte_depth) {
		return payload().data() + pos - 4 + byte_depth;
	}

	/// @brief The payload plus its trailing edge-slack — for the boundary stitch + recorder overshoot
	///        that read/write up to kTrailingSlackBytes past the nominal payload end (a frame
	///        straddling into the next cluster). The slack is guard bytes, guaranteed by the trailing
	///        guard.
	[[nodiscard]] std::span<std::byte> payload_with_trailing_slack() {
		return {payload().data(), Cluster::size + kTrailingSlackBytes};
	}
};

/// @brief Byte offset of the payload from a ComputedChunk slot base.
///
/// Defined here — after the struct definition, so `sizeof` sees the pure-header size. A slab slot is
/// laid out as `[chunk header][front guard][Cluster::size payload][trailing guard]`; the payload sits
/// at kChunkPayloadOffset from the slot base, and the ComputedChunk construct callbacks set
/// `payload_ = dest + kChunkPayloadOffset` (whole-slot provenance).
///
/// @note This is ComputedChunk-only: the streamed SAMPLE role's chunk lives in Rust
///       (`deluge_sample_fill::chunk`) and reports its OWN payload offset over the C-ABI
///       (`deluge_streamed_chunk_payload_offset()`); the shared slab slot takes the max of the two so
///       it fits either role — see general_memory_allocator.cpp.
/// @note kChunkPayloadOffset is the header size plus one cache line, so the front guard
///       (kChunkPayloadOffset - sizeof(ComputedChunk)) is >= CACHE_LINE_SIZE BY CONSTRUCTION —
///       covering the DMA cache-maintenance range-rounding and the application's front-underread. It
///       is expressed in `sizeof`, so it auto-fits any ABI. It also stays >= 4-byte aligned
///       (sizeof(ComputedChunk) is >= 4-aligned, CACHE_LINE_SIZE == 32, and `dest` is >= 16-byte
///       aligned), so the ALPHA-only `payload().data() & 0b11` misalignment check still passes.
inline constexpr size_t kChunkPayloadOffset = sizeof(ComputedChunk) + CACHE_LINE_SIZE;

/// @brief Bytes of trailing guard past the payload.
///
/// Covers the DMA end-of-range rounding and the application's trailing over-read.
inline constexpr size_t kChunkTrailingGuard = CACHE_LINE_SIZE;

/// @brief Lease bookkeeping shared by the streamed chunk (Rust) and ComputedChunk.
///
/// Free functions rather than members, since neither role needs a common base for this.
/// add_lease/release_lease take the chunk's own backing pointer (the resource manager identifies a
/// lease by the chunk address); lease_count is queried by resource_slot instead, since callers that
/// only need the count (freeze-checks, eviction gates) commonly don't have the chunk pointer handy —
/// e.g. right after a slot lookup. This matches the underlying C ABI's two lookup shapes.
namespace deluge::cluster {
/// @brief Take a lease on the resident chunk at `chunk` (its own backing pointer).
/// @param chunk The chunk's own backing pointer.
/// @note No-op if the resource manager isn't up yet.
void add_lease(void* chunk);

/// @brief Drop one lease on the resident chunk at `chunk` (its own backing pointer).
///
/// Most callers want remove_reason() instead, which also does the ALPHA/BETA zero-lease
/// freeze-check.
/// @param chunk The chunk's own backing pointer.
/// @note No-op if the resource manager isn't up yet.
void release_lease(void* chunk);

/// @brief Hard-lease ("reason") count for the chunk resident in slot `resource_slot`.
///
/// The single source of truth lives in the resource manager's chunk slot.
/// @param resource_slot Handle to the chunk's slot in the resource manager.
/// @return The lease count, or 0 if the resource manager isn't up yet.
[[nodiscard]] uint32_t lease_count(uint32_t resource_slot);

/// @brief Release a chunk (streamed or computed) back to the backing slab, clearing its slab-table
///        entry.
///
/// Both chunk roles are trivially destructible, so this is a role-agnostic free.
/// @param chunk The chunk's own backing pointer.
void free_chunk(void* chunk);

/// @brief Drop one lease ("reason") on `chunk`, freezing (ALPHA/BETA only) if it already had zero
///        leases — i.e. a reason removed that was never held.
///
/// The single reason-drop entry point for the computed/cached chunk role; forwards to
/// release_lease().
/// @param chunk The resident ComputedChunk to drop a lease on.
/// @param error_code FREEZE_WITH_ERROR code reported (ALPHA/BETA only) if `chunk` had zero leases.
void remove_reason(ComputedChunk& chunk, char const* error_code);
} // namespace deluge::cluster
