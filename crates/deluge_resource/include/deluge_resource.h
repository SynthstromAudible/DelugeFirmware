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

/// deluge_resource.h — the C ABI for the cached-asset / resource manager.
///
/// A value-scored, lease-based cache of *reconstructable* assets, layered on a
/// `DelugeHeap` (libdeluge/alloc.h). Replaces the C++ Stealable / CacheManager /
/// numReasonsToBeLoaded machinery. See docs/dev/resource_manager.md.
///
/// Model: an **Asset** is an opaque owner token + a reconstruction `Source`
/// (a `materialize` callback + cost). A **Chunk** is the resident, individually
/// leased/evicted unit (chunk 0..N of an asset). The manager keeps the most
/// valuable chunks resident and rebuilds the rest on demand; a chunk under a hard
/// lease (or marked dirty) is never evicted.
///
/// Owned by the `deluge_resource` crate; implemented in Rust, must stay in sync
/// with crates/deluge_resource/src/manager.rs. C-ABI: fixed-width types + function
/// pointers only (no C `enum`s across the boundary).
#ifndef DELUGE_RESOURCE_H
#define DELUGE_RESOURCE_H

#include "libdeluge/alloc.h" // DelugeHeap
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

/// Reconstruction cost classes (higher = dearer to rebuild = kept resident longer).
/// Plain ints, not a C enum, to keep the ABI fixed-width.
#define DELUGE_RESOURCE_COST_FREE 0u /* zero-fill / scratch */
#define DELUGE_RESOURCE_COST_IO 1u   /* re-read from storage (a sample cluster) */
#define DELUGE_RESOURCE_COST_CPU 2u  /* DSP recompute (a cache / wavetable band) */

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
/// May be NULL. (Distinct from the slab's `DelugeEvictFn` in alloc.h — this carries
/// the owner + chunk index.)
typedef void (*DelugeResourceEvictFn)(void* ctx, void* owner, uint32_t index);

/// Build a manager over `heap` with fixed-capacity asset/chunk tables (allocated
/// once from the heap) and register it as the heap's reclaim hook. `chunk_capacity`
/// must exceed the most chunks that can be resident at once (so the table is never
/// the limiter). Returns NULL on OOM.
DelugeResource* deluge_resource_create(DelugeHeap* heap, size_t asset_capacity, size_t chunk_capacity);

/// Configure the slab that backs `BACKING_SLAB` assets (uniform clusters). Until
/// set, every asset uses the heap. Create it with `deluge_slab_create_unmanaged`
/// (alloc.h) over the same heap, so eviction stays with this manager (the slab
/// self-evicts nothing — the manager's reclaim hook releases its slots).
void deluge_resource_set_slab(DelugeResource* mgr, DelugeSlab* slab);

/// Define an asset: an opaque `owner` token + its reconstruction `Source`
/// (`materialize` + optional `on_evict` + `ctx` + `cost` + `backing`). Returns the
/// asset id, or DELUGE_RESOURCE_NO_ASSET if the asset table is full. `backing` is
/// DELUGE_RESOURCE_BACKING_HEAP or _SLAB.
uint32_t deluge_resource_define_asset(DelugeResource* mgr, void* owner, DelugeResourceMaterializeFn materialize,
                                      DelugeResourceEvictFn on_evict, void* ctx, uint32_t cost, uint32_t backing);

/// Acquire chunk `index` of `asset` under a hard lease, materializing it if not
/// resident. Returns the backing pointer (16-byte aligned), or NULL on OOM /
/// reconstruction failure. A cache hit just adds a lease (pointer stable, no copy).
void* deluge_resource_acquire(DelugeResource* mgr, uint32_t asset, uint32_t index, size_t size);

/// Drop one hard lease on the chunk at `ptr` (it stays resident/cached until
/// evicted under pressure).
void deluge_resource_release(DelugeResource* mgr, void* ptr);

/// Soft-reference / un-reference an asset — project relevance: raises eviction
/// priority but does not pin (a soft-referenced chunk is still evictable + reloads).
void deluge_resource_reference(DelugeResource* mgr, uint32_t asset);
void deluge_resource_unreference(DelugeResource* mgr, uint32_t asset);

/// Mark the chunk at `ptr` most-recently-used (call on access so LRU reflects use).
void deluge_resource_touch(DelugeResource* mgr, void* ptr);

/// Mark/clear the chunk at `ptr` as dirty (unsaved, e.g. a recording ⇒ never
/// evicted until flushed).
void deluge_resource_mark_dirty(DelugeResource* mgr, void* ptr, bool dirty);

#ifdef __cplusplus
}
#endif

#endif // DELUGE_RESOURCE_H
