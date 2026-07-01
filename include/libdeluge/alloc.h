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

/// libdeluge/alloc.h — the C ABI for different allocator cores
///
/// A `DelugeHeap` is built over a caller-provided memory region (in firmware,
/// a BSP region from libdeluge/memory.h; in tests, a malloc'd arena). Placement
/// (fast SRAM vs slow SDRAM) is a property of *which heap* — there is no
/// allocMaxSpeed/allocLowSpeed split. Hand-written for now; cbindgen-generated
/// later. Must stay in sync with crates/deluge_alloc/src/lib.rs.
#ifndef LIBDELUGE_ALLOC_H
#define LIBDELUGE_ALLOC_H

#include <stddef.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque heap handle. The control block lives at the front of the region passed
/// to deluge_heap_create (no separate metadata storage, no allocator-in-allocator).
typedef struct DelugeHeap DelugeHeap;

/// Build a heap managing `[base, base+size)`. Returns NULL if the region is too
/// small to hold the control block. The region's memory is owned by the caller.
DelugeHeap* deluge_heap_create(void* base, size_t size);

/// Invalidate a heap handle. The backing region is the caller's to free.
void deluge_heap_destroy(DelugeHeap* heap);

/// Allocate `size` bytes aligned to at least `align` (a power of two; values < 16
/// are treated as 16 — the over-aligned-SIMD floor). Returns NULL on OOM.
void* deluge_alloc(DelugeHeap* heap, size_t size, size_t align);

/// Free a pointer previously returned by deluge_alloc / deluge_realloc.
void deluge_free(DelugeHeap* heap, void* ptr);

/// Resize, preserving the leading min(old_usable, new_size) bytes. Returns NULL
/// (leaving the original block intact) on failure.
void* deluge_realloc(DelugeHeap* heap, void* ptr, size_t new_size, size_t align);

/// Bytes guaranteed usable at `ptr` (the block's payload size, >= the request).
size_t deluge_usable_size(DelugeHeap* heap, void* ptr);

/// Read-only heap-integrity walk for the MEM_GUARD periodic check. Returns true if
/// the physical block chain's boundary tags are intact. Never allocates; reads only
/// block headers. A NULL heap is vacuously ok.
bool deluge_heap_check(DelugeHeap* heap);

// ---------------------------------------------------------------------------
// Slab cache pool — the "stealables" pager, layered on a heap (M2).
//
// A fixed-capacity table of uniform-size slots whose backing is borrowed from /
// returned to the heap. The pool registers itself as the heap's reclaim hook:
// when a live allocation can't be satisfied, the heap evicts the pool's coldest
// unpinned slot (notifying its owner) and retries. Replaces the C++
// Stealable/CacheManager machinery (numReasonsToBeLoaded -> pin count;
// Stealable::steal() -> DelugeEvictFn). See docs/dev/allocator_redesign.md.
// ---------------------------------------------------------------------------

/// Reclaim hook: free at least `bytes_needed`; return true if it freed something
/// (the alloc is retried). One hook per heap.
typedef bool (*DelugeReclaimFn)(void* ctx, size_t bytes_needed);
void deluge_heap_register_reclaim(DelugeHeap* heap, DelugeReclaimFn cb, void* ctx);

/// Opaque slab handle.
typedef struct DelugeSlab DelugeSlab;

/// Called when a slot is reclaimed, with that slot's owner (analogue of
/// Stealable::steal() — the owner must drop its reference). May be NULL.
typedef void (*DelugeEvictFn)(void* owner);

/// Create a *managed* pool of `capacity` uniform `slot_size`-byte slots over `heap`
/// and register it as the heap's reclaim hook (the pool self-evicts its coldest
/// unpinned slot). Returns NULL on OOM.
DelugeSlab* deluge_slab_create(DelugeHeap* heap, size_t slot_size, size_t capacity, DelugeEvictFn on_evict);

/// Create a *backing-only* pool: uniform `slot_size`-byte slots over `heap`, but
/// with eviction owned by an external coordinator. It registers no reclaim hook and
/// never self-evicts — the heap's hook (registered by the caller) must free slots via
/// deluge_slab_release. `capacity` must be >= the most slots that can fit in `heap`,
/// so the table is never the limiting factor. Used for the cluster pool, whose
/// eviction policy lives in the C++ CacheManager. Returns NULL on OOM.
DelugeSlab* deluge_slab_create_unmanaged(DelugeHeap* heap, size_t slot_size, size_t capacity);

/// Zero every slot's bytes on acquire (off by default). For the sim/golden build
/// (DELUGE_DETERMINISTIC_ALLOC) so a read-before-write of slot memory resolves to a
/// defined 0 instead of layout-dependent recycled heap content. Leave off in firmware.
void deluge_slab_set_zero_on_acquire(DelugeSlab* slab, bool on);

/// Acquire a slot for `owner` (may evict a cold slot). Returns the slot payload,
/// or NULL if exhausted and nothing is evictable.
void* deluge_slab_acquire(DelugeSlab* slab, void* owner);

/// Pin / unpin a slot (refcounted) — a pinned slot is never evicted. Maps to the
/// C++ Cluster::numReasonsToBeLoaded.
void deluge_slab_pin(DelugeSlab* slab, void* slot);
void deluge_slab_unpin(DelugeSlab* slab, void* slot);

/// Mark a slot most-recently-used (call on access so LRU reflects real usage).
void deluge_slab_touch(DelugeSlab* slab, void* slot);

/// Explicitly release a slot back to the heap (no eviction callback). Returns true
/// if `slot` was one of this pool's slots (and was freed), false if not owned here —
/// letting an external reclaim coordinator do "slab-release else heap-free".
bool deluge_slab_release(DelugeSlab* slab, void* slot);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_ALLOC_H
