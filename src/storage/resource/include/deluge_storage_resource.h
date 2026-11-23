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

/// deluge_storage_resource.h — the C header of the `deluge_storage_resource` crate: the cached-asset / resource manager
/// C ABI.
///
/// A value-scored, lease-based cache of *reconstructable* assets, layered on a
/// `DelugeHeap` (deluge_base_alloc.h). Replaces the C++ Stealable / CacheManager /
/// numReasonsToBeLoaded machinery. See docs/dev/sram_residency_streaming_perf.md.
///
/// Model: an **Asset** is an opaque owner token + a reconstruction `Source`
/// (a `materialize` callback + cost). A **Chunk** is the resident, individually
/// leased/evicted unit (chunk 0..N of an asset). The manager keeps the most
/// valuable chunks resident and rebuilds the rest on demand; a chunk under a hard
/// lease (or marked dirty) is never evicted.
///
/// Owned by the `deluge_storage_resource` crate; implemented in Rust by the shims in
/// src/storage/resource/src/abi.rs, which `abi_pin!` keeps in sync with this header. C-ABI: fixed-width types +
/// function pointers only (no C `enum`s across the boundary).
#ifndef DELUGE_STORAGE_RESOURCE_H
#define DELUGE_STORAGE_RESOURCE_H

#include "deluge_base_alloc.h" // DelugeHeap
#include "libdeluge/abi.h"
#include <stddef.h>
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque manager handle.
typedef struct DelugeResource DelugeResource;

/// Invalid asset id (returned when the asset table is full).
#define DELUGE_RESOURCE_NO_ASSET 0xFFFFFFFFu

/// Invalid chunk-slot index — a C++ object's slot handle before its chunk is created, or
/// `deluge_resource_slot_of` on a non-resident pointer.
#define DELUGE_RESOURCE_NO_SLOT 0xFFFFFFFFu

/// Reconstruction-cost ladder (ascending = dearer to *rebuild*). Pure rebuild-expense — how long a
/// chunk stays resident is cost-per-byte (the value function divides by size), so memory-yield is NOT
/// baked in here. Plain ints, not a C enum, to keep the ABI fixed-width; compared only numerically.
#define DELUGE_RESOURCE_COST_FREE 0u         /* zero-fill / scratch (cheapest) */
#define DELUGE_RESOURCE_COST_IO 1u           /* native sample cluster — one storage read */
#define DELUGE_RESOURCE_COST_IO_CONVERTED 2u /* sample cluster needing format conversion (read + re-convert) */
#define DELUGE_RESOURCE_COST_CPU 3u          /* DSP recompute — repitch cache / wavetable band */
#define DELUGE_RESOURCE_COST_CPU_PERC 4u     /* perc cache — heavier time-stretch recompute */
#define DELUGE_RESOURCE_COST_OBJECT                                                                                    \
	2u /* AudioFile object — re-scan header + recreate descriptor (read+parse); tiny, so the value function's size   \
	      term keeps it resident, not an inflated cost */

/// Where a chunk's backing memory comes from. Uniform clusters use the slab; a
/// variable-size asset (e.g. a wavetable band) allocates from the TLSF heap.
#define DELUGE_RESOURCE_BACKING_HEAP 0u
#define DELUGE_RESOURCE_BACKING_SLAB 1u

/// Reconstruct chunk `index` of `owner` into `dest[0..len)`. Return false if it
/// cannot be rebuilt right now (e.g. the backing store vanished). This is the
/// public extension point — the manager treats asset kinds opaquely through it.
typedef bool (*DelugeResourceMaterializeFn)(void* ctx, void* owner, uint32_t index, void* dest, size_t len);

/// Notify the owner that a currently-unleased chunk it may reference is being
/// evicted — it must drop any cached pointer (the Stealable::steal() analogue).
/// May be NULL. (Distinct from the slab's `DelugeEvictFn` in deluge_base_alloc.h — this carries
/// the owner + chunk index.)
typedef void (*DelugeResourceEvictFn)(void* ctx, void* owner, uint32_t index);

/// Initialize a chunk's backing *without* the (slow) I/O to fill it — the async
/// counterpart to materialize, used by deluge_resource_request (prefetch). For a sample
/// cluster: placement-new the Cluster + set its fields, leaving the data for an external
/// loader to read. Cannot fail (no I/O).
typedef void (*DelugeResourceConstructFn)(void* ctx, void* owner, uint32_t index, void* dest);

/// Evict callback for an *adopted* (object-lifecycle) chunk — the manager chose this
/// externally-allocated block for eviction and frees it right after. Gets the block pointer
/// directly (no owner/index). The owner drops references + tears the object down. See
/// deluge_resource_adopt.
typedef void (*DelugeResourceAdoptEvictFn)(void* ctx, void* ptr);

/// Build a manager over `heap` with fixed-capacity asset/chunk tables (allocated
/// once from the heap) and register it as the heap's reclaim hook. `chunk_capacity`
/// must exceed the most chunks that can be resident at once (so the table is never
/// the limiter). Returns NULL on OOM. [task]
DelugeResource* deluge_resource_create(DelugeHeap* heap, size_t asset_capacity, size_t chunk_capacity) DELUGE_NOEXCEPT;

/// Configure the slab that backs `BACKING_SLAB` assets (uniform clusters). Until
/// set, every asset uses the heap. Create it with `deluge_slab_create_unmanaged`
/// (deluge_base_alloc.h) over the same heap, so eviction stays with this manager (the slab
/// self-evicts nothing — the manager's reclaim hook releases its slots). [task]
void deluge_resource_set_slab(DelugeResource* mgr, DelugeSlab* slab) DELUGE_NOEXCEPT;

/// Define an asset: an opaque `owner` token + its reconstruction `Source`
/// (`materialize` + optional `on_evict` + `ctx` + `cost` + `backing`). Returns the
/// asset id, or DELUGE_RESOURCE_NO_ASSET if the asset table is full. `backing` is
/// DELUGE_RESOURCE_BACKING_HEAP or _SLAB. [task] [audio]
uint32_t deluge_resource_define_asset(DelugeResource* mgr, void* owner, DelugeResourceMaterializeFn materialize,
                                      DelugeResourceEvictFn on_evict, void* ctx, uint32_t cost,
                                      uint32_t backing) DELUGE_NOEXCEPT;

/// Retire an asset: free any of its chunks still resident (notifying the owner via
/// the asset's `on_evict`, so it drops cached pointers), then free the asset slot for
/// reuse. Call when the owner is destroyed (e.g. a Sample unloads) so the
/// fixed-capacity asset table can't exhaust across song loads. No-op on an unused id. [task] [audio]
void deluge_resource_release_asset(DelugeResource* mgr, uint32_t asset) DELUGE_NOEXCEPT;

/// Attach (or clear, with NULL) the async `construct` callback on an asset, making it
/// requestable via deluge_resource_request. Call after deluge_resource_define_asset. [task] [audio]
void deluge_resource_set_construct(DelugeResource* mgr, uint32_t asset,
                                   DelugeResourceConstructFn construct) DELUGE_NOEXCEPT;

/// Mark an asset prefix-dependent: its `on_evict` discards all higher-index chunks (e.g. a
/// SampleCache), so the manager only ever evicts the asset's *highest-index* resident chunk
/// (so `on_evict` never discards a chunk the manager still tracks). Call after define_asset. [task] [audio]
void deluge_resource_set_evict_tail_first(DelugeResource* mgr, uint32_t asset, bool on) DELUGE_NOEXCEPT;

/// Mark an asset self-protecting: while it allocates a chunk (request/acquire), its own chunks
/// aren't eviction candidates (the dontStealFromThing port — for unleased caches whose
/// request(N+1) must not evict the just-written N). Call after define_asset. [task] [audio]
void deluge_resource_set_self_protect(DelugeResource* mgr, uint32_t asset, bool on) DELUGE_NOEXCEPT;

/// Reserve + construct chunk `index` of `asset` under a hard lease *without* loading it
/// (runs the asset's `construct` callback — no I/O), for prefetch: an external loader
/// fills the data afterwards. The async counterpart to acquire; a cache hit just leases.
/// Returns the backing pointer (16-byte aligned), or NULL on OOM / no construct callback. [task] [audio]
void* deluge_resource_request(DelugeResource* mgr, uint32_t asset, uint32_t index, size_t size) DELUGE_NOEXCEPT;

/// Acquire chunk `index` of `asset` under a hard lease, materializing it if not
/// resident. Returns the backing pointer (16-byte aligned), or NULL on OOM /
/// reconstruction failure. A cache hit just adds a lease (pointer stable, no copy). [task]
void* deluge_resource_acquire(DelugeResource* mgr, uint32_t asset, uint32_t index, size_t size) DELUGE_NOEXCEPT;

/// RT-safe acquire: take a hard lease + return chunk `index` of `asset` only if it is resident **and**
/// ready (loaded). Never allocates, materializes, or blocks — returns NULL on a miss (incl. a chunk
/// that is `request`ed but not yet `mark_ready`'d). [task]
void* deluge_resource_try_acquire(DelugeResource* mgr, uint32_t asset, uint32_t index) DELUGE_NOEXCEPT;

/// Mark a `request`ed (Loading) chunk ready — called when its read completes (the C++ loader after
/// readClusterData, or an embassy storage task after its async DMA). No-op if `ptr` isn't resident. [task]
void deluge_resource_mark_ready(DelugeResource* mgr, void* ptr) DELUGE_NOEXCEPT;

/// The chunk-table slot index backing `ptr`, or DELUGE_RESOURCE_NO_SLOT if `ptr` isn't resident. O(n);
/// callers cache the result at chunk creation so later lease reads go through lease_count_by_slot. [task] [audio]
uint32_t deluge_resource_slot_of(DelugeResource* mgr, void* ptr) DELUGE_NOEXCEPT;

/// Recover the `(asset, index)` of the resident chunk backing `ptr`: writes them to `*out_asset` and `*out_index` and
/// returns true, or returns false, leaving both untouched, if `ptr` isn't resident or an argument is NULL. The cluster
/// fill (`native_begin`/`native_finish` in `deluge_storage_sample_fill`) resolves a queued chunk's file and position
/// through it. [task]
bool deluge_resource_chunk_ident(DelugeResource* mgr, void* ptr, uint32_t* out_asset,
                                 uint32_t* out_index) DELUGE_NOEXCEPT;

/// O(1) hard-lease count of the chunk at `slot` — 0 if `slot` is NO_SLOT / out of range / free. The
/// single source of truth for a cluster's reason count, read via the C++ slot handle (no scan). [task] [audio]
uint32_t deluge_resource_lease_count_by_slot(DelugeResource* mgr, uint32_t slot) DELUGE_NOEXCEPT;

/// @brief Hard leases on the chunk at `slot` excluding the load queue's own. [task]
///
/// Answers "does any consumer besides the queue still want this chunk?", and equals
/// deluge_resource_lease_count_by_slot for a chunk the queue holds no lease on. The async drain's
/// abandonment check: with a queue-owned lease held, the raw count never reaches 0.
/// @see deluge_resource_loader_enqueue_owned
uint32_t deluge_resource_external_lease_count_by_slot(DelugeResource* mgr, uint32_t slot) DELUGE_NOEXCEPT;

/// Cluster load queue (per-slot state inside the manager; replaces the C++ ClusterPriorityQueue). The
/// priority is the caller's audio-domain rating — lower = more urgent.
/// enqueue the chunk at `slot` (a re-enqueue while still queued only raises the priority, so a less
/// urgent requester cannot push back a chunk someone more urgent waits on). [task]
void deluge_resource_loader_enqueue(DelugeResource* mgr, uint32_t slot, uint32_t priority) DELUGE_NOEXCEPT;

/// @brief Enqueue the chunk at `slot` AND take a queue-owned hard lease. [task]
///
/// deluge_resource_loader_next serves only leased chunks (filling an evictable one could write into a
/// recycled slot) and silently discards the rest, so a caller that cannot hold its own lease until the
/// load lands — a fire-and-forget "fill this eventually" — must enqueue through this instead, letting
/// the queue hold the lease the guard requires. Idempotent in the lease: re-enqueueing raises the
/// priority as deluge_resource_loader_enqueue does, without taking a second one.
///
/// @warning Every terminal path of the loader MUST pair this with
/// deluge_resource_loader_release_owned. A stranded queue lease pins the chunk against eviction for
/// the rest of the run.
void deluge_resource_loader_enqueue_owned(DelugeResource* mgr, uint32_t slot, uint32_t priority) DELUGE_NOEXCEPT;

/// @brief Release the queue-owned lease taken by deluge_resource_loader_enqueue_owned. [task]
/// @return Whether there was one to release — false for a chunk enqueued under the plain protocol,
///         whose enqueuer owns its own lease.
/// @note Idempotent, and does not de-queue.
bool deluge_resource_loader_release_owned(DelugeResource* mgr, uint32_t slot) DELUGE_NOEXCEPT;
/// Pop the most-urgent queued + still-leased chunk's backing ptr (clears its queued flag), else NULL. [task]
/// Queued-but-unleased chunks are skipped: they stay queued, and resident for normal eviction.
void* deluge_resource_loader_next(DelugeResource* mgr) DELUGE_NOEXCEPT;
/// Whether any queued + leased chunk sits at the lowest priority (0xFFFFFFFF) — the load-song yield gate. [task]
bool deluge_resource_loader_has_lowest(DelugeResource* mgr) DELUGE_NOEXCEPT;
/// @brief Whether any queued + leased chunk remains at all (any priority). [task]
///
/// The non-destructive loader-queue-non-empty predicate the offline async drain blocks on.
/// Answers "would deluge_resource_loader_next return non-null" without popping or mutating
/// anything.
/// @param mgr Resource manager instance.
/// @return True if the loader queue holds at least one queued + leased chunk.
bool deluge_resource_loader_has_any(DelugeResource* mgr) DELUGE_NOEXCEPT;

/// Number of cost classes `evictions_by_cost` buckets by (must match the Rust COST_BUCKETS).
#define DELUGE_RESOURCE_COST_BUCKETS 8

/// Buckets in each of DelugeResourceStats' latency histograms (must match the Rust LATENCY_BUCKETS).
#define DELUGE_RESOURCE_LATENCY_BUCKETS 16

/// Cumulative manager instrumentation counters — pure stats (never affect behaviour), for cache /
/// eviction analysis, profiling, on-device debug. Layout must match `Stats` in manager.rs.
typedef struct DelugeResourceStats {
	uint64_t acquires;       // acquire() calls
	uint64_t acquire_hits;   // ... that hit a resident chunk (no alloc/materialize)
	uint64_t requests;       // request() (prefetch-construct) calls
	uint64_t materializes;   // asset materialize() invocations (sync storage reloads)
	uint64_t evictions;      // chunks evicted under pressure / to free a slot
	uint64_t alloc_failures; // alloc_backing returned null (pool exhausted after reclaim)
	uint64_t adopts;         // objects adopted
	uint64_t evictions_by_cost[DELUGE_RESOURCE_COST_BUCKETS]; // evicted-chunk breakdown by cost class
	// Scan-cost instrumentation (THROWAWAY: added 2026-08-16 to size the manager's O(table) linear
	// scans on device; remove once the indexing decision is made). `*_calls` counts invocations,
	// `*_slots` counts slots actually visited -- the cost driver, since each visit is a masked
	// (interrupt-disabling) read of a chunk slot in SDRAM. Measured on hardware: a full 6144-slot
	// scan costs ~10.5 ms, i.e. ~1.7 us per slot visited.
	uint64_t scan_resident_calls;
	uint64_t scan_resident_slots;
	uint64_t scan_ptr_calls;
	uint64_t scan_ptr_slots;
	uint64_t scan_free_calls;
	uint64_t scan_free_slots;
	uint64_t scan_evict_calls;
	uint64_t scan_evict_slots;
	// Loader queue depth sampled inside loader_next's existing scan: how many candidates it chose
	// between. Measured mean 1.19 / max 37 on 2026-08-16 — about one at a time, so there is nothing
	// to order. A mean well above 1 is when ordering would start to matter.
	uint64_t loader_depth_polls;
	uint64_t loader_depth_total;
	uint64_t loader_depth_max;
	// Loader service latency in output frames, split: queue_wait = enqueue -> loader_next (the fill
	// task's scheduling delay); fill = loader_next -> mark_ready (the card). Measured 2026-08-16:
	// queue_wait mean 35 ms vs fill mean 5.6 ms, i.e. 86% of a chunk's latency is scheduling. 35 ms
	// is the number the storage-tier scheduling work has to beat.
	uint64_t queue_wait_samples;
	uint64_t queue_wait_total;
	uint64_t queue_wait_max;
	uint64_t fill_samples;
	uint64_t fill_total;
	uint64_t fill_max;
	// queue_wait and fill as log2 histograms of frames: bucket 0 holds 0, bucket i holds [2^(i-1), 2^i), and the last
	// also holds everything longer.
	uint32_t queue_wait_buckets[DELUGE_RESOURCE_LATENCY_BUCKETS];
	uint32_t fill_buckets[DELUGE_RESOURCE_LATENCY_BUCKETS];
} DelugeResourceStats;

/// Copy the manager's cumulative counters into `*out`. No-op if either pointer is null. [task] [audio]
void deluge_resource_stats(DelugeResource* mgr, DelugeResourceStats* out) DELUGE_NOEXCEPT;
/// Zero the counters (e.g. to measure one render in isolation). [task] [audio]
void deluge_resource_stats_reset(DelugeResource* mgr) DELUGE_NOEXCEPT;

/// Adopt an externally-allocated heap block as a manager-evictable (object-lifecycle) chunk:
/// the owner allocated + built it; the manager owns only its eviction (value-scored by cost-per-byte
/// + recency, so pass the block's `size` in bytes). On eviction it calls `on_evict(ctx, ptr)` then
/// frees `ptr`. Registered unleased (pin via deluge_resource_add_lease). Returns `ptr`, or NULL if the
/// chunk table is full and nothing is evictable. The adopt counterpart to define_asset+acquire. [task] [audio]
void* deluge_resource_adopt(DelugeResource* mgr, void* ptr, size_t size, uint32_t cost, void* ctx,
                            DelugeResourceAdoptEvictFn on_evict) DELUGE_NOEXCEPT;

/// Add a hard lease to an already-resident chunk by its backing pointer (no
/// re-materialize) — the pointer-keyed counterpart to a cache-hit acquire, for a caller
/// that already holds the chunk and just wants to pin it harder. No-op if `ptr` is not a
/// resident chunk. [task] [audio]
void deluge_resource_add_lease(DelugeResource* mgr, void* ptr) DELUGE_NOEXCEPT;

/// Drop a specific resident chunk by its backing pointer (clear slot + free backing),
/// *without* calling its on_evict — for an owner deliberately discarding a chunk it
/// manages (e.g. a SampleCache truncating its tail). No-op if `ptr` isn't resident. [task] [audio]
void deluge_resource_evict_chunk(DelugeResource* mgr, void* ptr) DELUGE_NOEXCEPT;

/// Drop one hard lease on the chunk at `ptr` (it stays resident/cached until
/// evicted under pressure). [task] [audio]
void deluge_resource_release(DelugeResource* mgr, void* ptr) DELUGE_NOEXCEPT;

/// Soft-reference / un-reference an asset — project relevance: raises eviction
/// priority but does not pin (a soft-referenced chunk is still evictable + reloads). [task] [audio]
void deluge_resource_reference(DelugeResource* mgr, uint32_t asset) DELUGE_NOEXCEPT;
void deluge_resource_unreference(DelugeResource* mgr, uint32_t asset) DELUGE_NOEXCEPT; ///< [task] [audio]

/// Mark the chunk at `ptr` most-recently-used (call on access so LRU reflects use). [task] [audio]
void deluge_resource_touch(DelugeResource* mgr, void* ptr) DELUGE_NOEXCEPT;

/// Mark/clear the chunk at `ptr` as dirty (unsaved, e.g. a recording ⇒ never
/// evicted until flushed). [task] [audio]
void deluge_resource_mark_dirty(DelugeResource* mgr, void* ptr, bool dirty) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // DELUGE_STORAGE_RESOURCE_H
