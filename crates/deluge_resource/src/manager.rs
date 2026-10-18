//! The resource manager: a value-scored, lease-based cache of reconstructable assets on the
//! `deluge_alloc` TLSF heap, and its C ABI.
//!
//! An **asset** (an owner token, a [`Source`] recipe, soft references) is split into **chunks**,
//! the unit that is individually leased and evicted. Both live in fixed-capacity tables allocated
//! once from the heap.
//!
//! The tables are `&'static [Cell<_>]` of `Copy` PODs, so all bookkeeping is safe Rust. No method
//! takes `&mut self`, which makes the manager reentrant: when an allocation runs the heap's
//! reclaim hook, the hook takes another `&Manager` and mutates through `Cell`. Every `Cell` access
//! goes through the [`sync`](crate::sync) helpers, one slot at a time: a table scan releases the
//! mask between slots, so any slot a scan found must be re-validated under the mask before use.
//!
//! The manager never dereferences a chunk's backing; `unsafe` is confined to the FFI entry points,
//! the one-time table setup, the callback calls, and the alloc/free calls.

use crate::sync::{m_get, m_rmw, m_set, Masked};
use crate::value::evict_rank;
use core::cell::Cell;
use core::ffi::c_void;
use core::ptr;
use deluge_alloc::slab::{deluge_slab_acquire, deluge_slab_release, DelugeSlab};
use deluge_alloc::{deluge_alloc, deluge_free, deluge_heap_register_reclaim, DelugeHeap};

const NONE: u32 = u32::MAX;
/// Invalid chunk-slot index (the C ABI's `DELUGE_RESOURCE_NO_SLOT`), returned by `slot_of` for a
/// pointer that is not resident.
pub(crate) const NO_SLOT: u32 = u32::MAX;

/// Reconstructs chunk `index` of `owner` into `dest[..len]`; returns false if it cannot be rebuilt
/// now (e.g. the backing file vanished).
pub type MaterializeFn = unsafe extern "C" fn(
    ctx: *mut c_void,
    owner: *mut c_void,
    index: u32,
    dest: *mut u8,
    len: usize,
) -> bool;

/// Tells the owner that an unleased chunk is being evicted; it must drop any cached pointer to it.
pub type EvictFn = unsafe extern "C" fn(ctx: *mut c_void, owner: *mut c_void, index: u32);

/// Initializes a chunk's backing without the I/O to fill it; the data is read later by an external
/// loader. The asynchronous counterpart to [`MaterializeFn`], used by `request`. Cannot fail.
pub type ConstructFn =
    unsafe extern "C" fn(ctx: *mut c_void, owner: *mut c_void, index: u32, dest: *mut u8);

/// Eviction callback for an adopted block: the owner drops its references and tears the object
/// down; the manager frees the block right after. Receives the block pointer, so one callback can
/// serve many objects.
pub type AdoptEvictFn = unsafe extern "C" fn(ctx: *mut c_void, ptr: *mut u8);

// Plain `u32`s rather than a C enum, to cross the C ABI.

/// Chunks are allocated from the heap at their requested size.
pub const BACKING_HEAP: u32 = 0;
/// Chunks are uniform slots of the slab set by `deluge_resource_set_slab`; heap if none is set.
pub const BACKING_SLAB: u32 = 1;

/// How an asset's chunks are rebuilt, evicted and backed.
#[derive(Clone, Copy)]
pub struct Source {
    pub materialize: Option<MaterializeFn>,
    pub on_evict: Option<EvictFn>,
    /// Set by `deluge_resource_set_construct`; `None` means the asset cannot be `request`ed, only
    /// `acquire`d.
    pub construct: Option<ConstructFn>,
    /// Passed as the first argument to every callback.
    pub ctx: *mut c_void,
    /// One of the `COST_*` constants.
    pub cost: u32,
    /// [`BACKING_HEAP`] or [`BACKING_SLAB`].
    pub backing: u32,
}

#[derive(Clone, Copy)]
struct AssetSlot {
    in_use: bool,
    owner: *mut c_void,
    source: Source,
    soft_refs: u32,
    /// Prefix-dependent asset (e.g. a sample cache, whose `on_evict` discards every higher-index
    /// chunk): only its highest-index resident chunk may be evicted, so `on_evict` never discards
    /// a sibling the manager still tracks.
    evict_tail_first: bool,
    /// While this asset is in `request`/`acquire`, its own chunks are not eviction candidates.
    /// For unleased caches, whose `request(N+1)` would otherwise evict the just-written `N`;
    /// leased assets (sample clusters) must stay able to evict their own old chunks.
    self_protect: bool,
}
impl AssetSlot {
    const EMPTY: AssetSlot = AssetSlot {
        in_use: false,
        owner: ptr::null_mut(),
        source: Source {
            materialize: None,
            on_evict: None,
            construct: None,
            ctx: ptr::null_mut(),
            cost: 0,
            backing: BACKING_HEAP,
        },
        soft_refs: 0,
        evict_tail_first: false,
        self_protect: false,
    };
}

#[derive(Clone, Copy)]
struct ChunkSlot {
    backing: *mut u8, // null => free slot
    asset: u32,       // index into the asset table; NONE => an adopted chunk
    index: u32,       // chunk index within the asset (unused for adopted chunks)
    leases: u32,      // hard leases; > 0 ⇒ never evicted
    dirty: bool,      // unsaved (e.g. a recording) ⇒ never evicted
    // False between `request` (constructed, no data yet) and `mark_ready`; `acquire` and `adopt`
    // set it. `try_acquire` returns only ready chunks, so the RT path never reads half-loaded data.
    ready: bool,
    recency: u64,
    /// Backing size in bytes: the memory evicting this chunk reclaims, for the cost-per-byte rank.
    size: u32,
    /// Stamped from [`Manager::next_gen`] each time the slot takes a fresh backing (not on a
    /// cache-hit lease). With the slot index it forms a `{slot, generation}` pin token, so a
    /// retain/release through a token that outlived its chunk is a checked no-op.
    generation: u32,
    // The load queue is per-slot state: `queued` means waiting for the loader, ordered by
    // `queue_priority` (lower = more urgent). Clearing the slot on eviction also de-queues it.
    queued: bool,
    queue_priority: u32,
    /// One of `leases` belongs to the load queue, taken by `loader_enqueue_owned` and released by
    /// `loader_release_owned`.
    ///
    /// `loader_next` serves only leased chunks, since filling an evictable one could write into a
    /// recycled slot; an enqueuer that drops its own lease would otherwise lose the entry. The flag
    /// keeps the release idempotent and lets `external_lease_count_by_slot` exclude the queue's
    /// lease. Callers that hold their own lease across the load use plain `loader_enqueue` and
    /// leave it false.
    queue_leased: bool,
    // Adopted chunks (asset == NONE) carry their own cost and evict callback; unused otherwise.
    cost: u32,
    adopt_evict: Option<AdoptEvictFn>,
    adopt_ctx: *mut c_void,
    /// Next slot in this chunk's `(asset, index)` bucket chain, or `NONE`. Meaningful only for
    /// occupied, non-adopted slots (see [`Manager::index_insert`]).
    hash_next: u32,
    /// `deluge_debug_now_frames()` at enqueue, re-stamped by `loader_next`, so the service latency
    /// splits into queue wait and fill. 0 means never enqueued.
    queued_at: u32,
    /// Next slot in this chunk's backing-pointer bucket chain, or `NONE`. Every occupied slot is in
    /// this index, adopted ones included, because backing pointers are unique.
    ptr_next: u32,
}
impl ChunkSlot {
    const EMPTY: ChunkSlot = ChunkSlot {
        backing: ptr::null_mut(),
        asset: NONE,
        index: 0,
        leases: 0,
        dirty: false,
        ready: false,
        recency: 0,
        size: 0,
        generation: 0,
        queued: false,
        queue_priority: 0,
        queue_leased: false,
        cost: 0,
        adopt_evict: None,
        adopt_ctx: ptr::null_mut(),
        hash_next: NONE,
        ptr_next: NONE,
        queued_at: 0,
    };

    /// Queues this chunk at `priority` (lower = more urgent). While it is already queued the
    /// priority only rises, so a less urgent requester can't push back a chunk someone more urgent
    /// waits on. Once `loader_next` has popped it, the priority is set afresh.
    fn queue_at(&mut self, priority: u32) {
        self.queue_priority = if self.queued {
            self.queue_priority.min(priority)
        } else {
            priority
        };
        self.queued = true;
    }
}

// The audio sample timer, provided by the C++ side, for the loader-latency fields of `Stats`.
unsafe extern "C" {
    pub fn deluge_debug_now_frames() -> u32;
}

/// Number of cost classes in [`Stats::evictions_by_cost`]; higher costs share the last bucket.
pub const COST_BUCKETS: usize = 8;

/// Cumulative manager counters, for instrumentation only.
///
/// Mirrors `DelugeResourceStats` in `deluge_resource.h`; read with `deluge_resource_stats`, zeroed
/// with `deluge_resource_stats_reset`.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Stats {
    pub acquires: u64,                          // acquire() calls
    pub acquire_hits: u64, // ... that hit a resident chunk (no alloc/materialize)
    pub requests: u64,     // request() calls
    pub materializes: u64, // materialize() invocations
    pub evictions: u64,    // chunks evicted under pressure / to free a slot
    pub alloc_failures: u64, // alloc_backing returned null (pool exhausted after reclaim)
    pub adopts: u64,       // objects adopted
    pub evictions_by_cost: [u64; COST_BUCKETS], // evictions by cost class
    // Table-scan cost: `*_calls` counts lookups, `*_slots` the slots visited. Each visit is a
    // masked read of a slot in SDRAM, so visits are the cost driver.
    pub scan_resident_calls: u64,
    pub scan_resident_slots: u64,
    pub scan_ptr_calls: u64,
    pub scan_ptr_slots: u64,
    pub scan_free_calls: u64,
    pub scan_free_slots: u64,
    pub scan_evict_calls: u64,
    pub scan_evict_slots: u64,
    /// Load-queue depth: the queued, leased candidates each `loader_next` chose between. Queue
    /// ordering only matters when the mean is well above 1.
    pub loader_depth_polls: u64,
    pub loader_depth_total: u64,
    pub loader_depth_max: u64,
    /// Loader service latency in output frames, in two halves: `queue_wait` is enqueue to
    /// `loader_next` (the fill task's scheduling delay), `fill` is `loader_next` to `mark_ready`
    /// (the read and convert).
    pub queue_wait_samples: u64,
    pub queue_wait_total: u64,
    pub queue_wait_max: u64,
    pub fill_samples: u64,
    pub fill_total: u64,
    pub fill_max: u64,
}

/// The manager state behind a [`DelugeResource`] handle.
pub struct Manager {
    // Only passed to the alloc/slab calls, never dereferenced here. A null slab means every asset
    // uses the heap.
    heap: *mut DelugeHeap,
    slab: Cell<*mut DelugeSlab>,
    assets: &'static [Cell<AssetSlot>],
    chunks: &'static [Cell<ChunkSlot>],
    /// Hash index over `(asset, index)`: each bucket heads an intrusive chain through
    /// `ChunkSlot::hash_next`, or is `NONE`. Power-of-two length `>= chunks.len()`, so bucket
    /// selection is a mask.
    ///
    /// Chained rather than open-addressed because every eviction deletes: tombstones would
    /// accumulate over hours of churn, while a chain needs no rehash and its links live in the
    /// chunk slots. Without it, every lookup miss would scan the whole table.
    buckets: &'static [Cell<u32>],
    /// Hash index over `backing`, chained through `ChunkSlot::ptr_next`; same shape as `buckets`.
    /// Backs every pointer-keyed entry point (`find_by_ptr`). Holds every occupied slot, adopted
    /// chunks included.
    ptr_buckets: &'static [Cell<u32>],
    tick: Cell<u64>,
    /// The self-protecting asset currently allocating in `request`/`acquire`, whose chunks are not
    /// eviction candidates; `NONE` otherwise. Set through [`ProtectGuard`].
    protect: Cell<u32>,
    stats: Cell<Stats>,
    /// Counter feeding `ChunkSlot::generation` (see [`Manager::next_gen`]).
    alloc_gen: Cell<u32>,
}

/// Restores `Manager::protect` on drop, so every early return from `request`/`acquire` clears
/// the self-protection.
struct ProtectGuard<'a> {
    mgr: &'a Manager,
    prev: u32,
}
impl Drop for ProtectGuard<'_> {
    fn drop(&mut self) {
        m_set(&self.mgr.protect, self.prev);
    }
}

impl Manager {
    #[inline]
    fn bump(&self) -> u64 {
        m_rmw(&self.tick, |t| {
            *t += 1;
            *t
        })
    }

    /// Updates the counters in one masked window. Events are per chunk, so copying the whole
    /// struct is cheap.
    fn stat(&self, f: impl FnOnce(&mut Stats)) {
        m_rmw(&self.stats, |s| f(s));
    }

    /// Returns the next allocation generation, stamped into a slot each time it takes a fresh
    /// backing (see `ChunkSlot::generation`). Wraps after 2^32 allocations.
    #[inline]
    fn next_gen(&self) -> u32 {
        m_rmw(&self.alloc_gen, |g| {
            *g = g.wrapping_add(1);
            *g
        })
    }

    /// Masked snapshot of the counters; audio writes them, so an unmasked copy could tear.
    fn stats_snapshot(&self) -> Stats {
        m_get(&self.stats)
    }
    fn stats_reset(&self) {
        m_set(&self.stats, Stats::default());
    }

    fn find_resident(&self, asset: u32, index: u32) -> Option<usize> {
        let mut visited = 0u64;
        let found = if asset == NONE {
            // Adopted chunks are not indexed (see index_insert), so this key needs the full
            // scan. No production caller passes NONE.
            (0..self.chunks.len()).find(|&i| {
                visited += 1;
                let s = m_get(&self.chunks[i]);
                !s.backing.is_null() && s.asset == asset && s.index == index
            })
        } else {
            // The chain is read unmasked between slots, so a slot can be evicted and reused
            // under us; the masked per-slot read plus this identity check is what makes it safe.
            let mut cur = self.buckets[self.bucket_of(asset, index)].get();
            let mut hit = None;
            // Bounded walk: only a corrupt index can be cyclic, but on the audio path a hang is
            // a dead device while a wrong answer is a duplicate chunk. Take the survivable failure.
            let mut steps = 0;
            while cur != NONE && steps <= self.chunks.len() {
                let i = cur as usize;
                if i >= self.chunks.len() {
                    break; // corrupt link; treat as a miss rather than panicking on the audio path
                }
                visited += 1;
                steps += 1;
                let s = m_get(&self.chunks[i]);
                if !s.backing.is_null() && s.asset == asset && s.index == index {
                    hit = Some(i);
                    break;
                }
                cur = s.hash_next;
            }
            hit
        };
        // One stat update per lookup: `stat` is a masked RMW of the whole struct, so a per-slot
        // update would cost more than the scan it measures.
        self.stat(|s| {
            s.scan_resident_calls += 1;
            s.scan_resident_slots += visited;
        });
        found
    }
    /// Bucket for `(asset, index)`. Both halves are small and dense, so both are mixed (with the
    /// golden-ratio and xxHash primes); a plain concatenation would cluster every asset's chunk 0.
    #[inline]
    fn bucket_of(&self, asset: u32, index: u32) -> usize {
        let h = asset.wrapping_mul(0x9E37_79B1).rotate_left(15) ^ index.wrapping_mul(0x85EB_CA6B);
        (h as usize) & (self.buckets.len() - 1)
    }

    /// Bucket for a backing pointer. Backings are 16-byte aligned, so the low four bits are shifted
    /// out before mixing.
    #[inline]
    fn ptr_bucket_of(&self, p: *mut u8) -> usize {
        let h = ((p as usize) >> 4) as u32;
        (h.wrapping_mul(0x9E37_79B1).rotate_left(15) as usize) & (self.ptr_buckets.len() - 1)
    }

    /// Links slot `i` into the pointer index. Same masking contract as [`Manager::index_insert`].
    fn ptr_index_insert(&self, i: usize) {
        let mut s = self.chunks[i].get();
        if s.backing.is_null() {
            return;
        }
        let b = self.ptr_bucket_of(s.backing);
        s.ptr_next = self.ptr_buckets[b].get();
        self.chunks[i].set(s);
        self.ptr_buckets[b].set(i as u32);
    }

    /// Unlinks slot `i` from the pointer index. Same masking contract as
    /// [`Manager::index_insert`]; takes `backing` explicitly because the caller is clearing the
    /// slot.
    fn ptr_index_remove(&self, i: usize, backing: *mut u8) {
        if backing.is_null() {
            return;
        }
        let b = self.ptr_bucket_of(backing);
        let head = self.ptr_buckets[b].get();
        if head == i as u32 {
            self.ptr_buckets[b].set(self.chunks[i].get().ptr_next);
            return;
        }
        let mut cur = head;
        let mut steps = 0;
        while cur != NONE && steps <= self.chunks.len() {
            let c = cur as usize;
            if c >= self.chunks.len() {
                return; // corrupt link; see find_resident's walk
            }
            steps += 1;
            let s = self.chunks[c].get();
            if s.ptr_next == i as u32 {
                let mut sc = s;
                sc.ptr_next = self.chunks[i].get().ptr_next;
                self.chunks[c].set(sc);
                return;
            }
            cur = s.ptr_next;
        }
    }

    /// Links slot `i` into its `(asset, index)` bucket.
    ///
    /// The caller must hold one masked window covering both this and the slot write, so the slot
    /// and the index never disagree: a preempting lookup that saw the slot unlinked would report a
    /// miss, and its caller would allocate a second chunk for the same key.
    ///
    /// Adopted chunks (`asset == NONE`) are not indexed: they all share the key `(NONE, 0)`, so
    /// they would form one long chain whose unlink is a linear scan.
    fn index_insert(&self, i: usize) {
        let mut s = self.chunks[i].get();
        if s.asset == NONE {
            return;
        }
        let b = self.bucket_of(s.asset, s.index);
        s.hash_next = self.buckets[b].get();
        self.chunks[i].set(s);
        self.buckets[b].set(i as u32);
    }

    /// Unlinks slot `i` from its bucket. Same masking contract as [`Manager::index_insert`]; must
    /// run before the slot is cleared, since it walks `hash_next`. Takes the key explicitly because
    /// the caller is overwriting the slot.
    fn index_remove(&self, i: usize, asset: u32, index: u32) {
        if asset == NONE {
            return; // never indexed — see index_insert
        }
        let b = self.bucket_of(asset, index);
        let head = self.buckets[b].get();
        if head == i as u32 {
            self.buckets[b].set(self.chunks[i].get().hash_next);
            return;
        }
        // Walk to the predecessor; chains average under one entry since buckets >= slots.
        let mut cur = head;
        let mut steps = 0;
        while cur != NONE && steps <= self.chunks.len() {
            let c = cur as usize;
            if c >= self.chunks.len() {
                return; // corrupt link; see find_resident's walk
            }
            steps += 1;
            let s = self.chunks[c].get();
            if s.hash_next == i as u32 {
                let mut sc = s;
                sc.hash_next = self.chunks[i].get().hash_next;
                self.chunks[c].set(sc);
                return;
            }
            cur = s.hash_next;
        }
    }

    /// Brute-force lookup: the ground truth the index tests compare `find_resident` against.
    #[cfg(test)]
    pub(crate) fn find_resident_linear(&self, asset: u32, index: u32) -> Option<usize> {
        (0..self.chunks.len()).find(|&i| {
            let s = m_get(&self.chunks[i]);
            !s.backing.is_null() && s.asset == asset && s.index == index
        })
    }

    fn find_by_ptr(&self, p: *mut u8) -> Option<usize> {
        let mut visited = 0u64;
        // Free slots hold null, so null must never match.
        let found = if p.is_null() {
            None
        } else {
            let mut cur = self.ptr_buckets[self.ptr_bucket_of(p)].get();
            let mut hit = None;
            let mut steps = 0;
            while cur != NONE && steps <= self.chunks.len() {
                let i = cur as usize;
                if i >= self.chunks.len() {
                    break; // corrupt link; see find_resident's walk
                }
                visited += 1;
                steps += 1;
                let s = m_get(&self.chunks[i]);
                if s.backing == p {
                    hit = Some(i);
                    break;
                }
                cur = s.ptr_next;
            }
            hit
        };
        self.stat(|s| {
            s.scan_ptr_calls += 1;
            s.scan_ptr_slots += visited;
        });
        found
    }
    fn find_free_chunk(&self) -> Option<usize> {
        let mut visited = 0u64;
        let found = (0..self.chunks.len()).find(|&i| {
            visited += 1;
            m_get(&self.chunks[i]).backing.is_null()
        });
        self.stat(|s| {
            s.scan_free_calls += 1;
            s.scan_free_slots += visited;
        });
        found
    }

    /// Finds the slot backing `p`, then in one masked window re-validates `backing == p` (the
    /// slot may have been reused since the lookup) and applies `f`. Returns whether it mutated.
    fn rmw_by_ptr(&self, p: *mut u8, f: impl FnOnce(&mut ChunkSlot)) -> bool {
        let Some(i) = self.find_by_ptr(p) else {
            return false;
        };
        let _m = Masked::enter();
        let mut s = self.chunks[i].get();
        if s.backing != p {
            return false; // slot changed under us — bail
        }
        f(&mut s);
        self.chunks[i].set(s);
        true
    }

    /// Masked RMW on slot `i`. Re-validating the slot (it may have emptied since the lookup) is
    /// the closure's job.
    fn rmw_by_ptr_slot<R>(&self, i: usize, f: impl FnOnce(&mut ChunkSlot) -> R) -> R {
        m_rmw(&self.chunks[i], f)
    }

    /// Returns the slot index backing `p`, or [`NO_SLOT`] if `p` is not resident.
    pub(crate) fn slot_of(&self, p: *mut u8) -> u32 {
        match self.find_by_ptr(p) {
            Some(i) => i as u32,
            None => NO_SLOT,
        }
    }

    /// Returns the hard-lease count of the chunk at `slot`; 0 if `slot` is out of range or free.
    pub(crate) fn lease_count_by_slot(&self, slot: u32) -> u32 {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return 0;
        }
        let s = m_get(&self.chunks[i]);
        if s.backing.is_null() {
            return 0;
        }
        s.leases
    }

    /// Returns the hard leases on the chunk at `slot` excluding the load queue's own
    /// (`ChunkSlot::queue_leased`).
    ///
    /// The drain's abandonment check needs this: with a queue lease held the raw count never
    /// reaches 0, so an unwanted chunk would be retried forever.
    pub(crate) fn external_lease_count_by_slot(&self, slot: u32) -> u32 {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return 0;
        }
        let s = m_get(&self.chunks[i]);
        if s.backing.is_null() {
            return 0;
        }
        s.leases.saturating_sub(s.queue_leased as u32)
    }

    /// Returns the `(asset, index)` of the resident chunk backing `p`, or `None` if `p` is not
    /// resident. For an adopted chunk `asset` is `u32::MAX` and `index` is meaningless.
    pub(crate) fn chunk_ident(&self, p: *mut u8) -> Option<(u32, u32)> {
        let i = self.find_by_ptr(p)?;
        let s = m_get(&self.chunks[i]);
        (s.backing == p).then_some((s.asset, s.index))
    }

    /// Returns the generation of the chunk at `slot`; 0 if out of range or free.
    pub(crate) fn generation_of_slot(&self, slot: u32) -> u32 {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return 0;
        }
        let s = m_get(&self.chunks[i]);
        if s.backing.is_null() {
            return 0;
        }
        s.generation
    }

    /// Takes a hard lease on the chunk at `slot` if it is occupied and its generation is still
    /// `gen`. Returns whether it leased.
    pub(crate) fn retain_by_slot_gen(&self, slot: u32, gen: u32) -> bool {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return false;
        }
        let r = self.bump();
        self.rmw_by_ptr_slot(i, |s| {
            if s.backing.is_null() || s.generation != gen {
                return false;
            }
            s.leases += 1;
            s.recency = r;
            true
        })
    }

    /// Drops a hard lease on the chunk at `slot` if its generation is still `gen`; the checked
    /// counterpart of `release`. Saturating, so a double release cannot underflow. Returns whether
    /// the slot matched.
    pub(crate) fn release_by_slot_gen(&self, slot: u32, gen: u32) -> bool {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return false;
        }
        self.rmw_by_ptr_slot(i, |s| {
            if s.backing.is_null() || s.generation != gen {
                return false;
            }
            s.leases = s.leases.saturating_sub(1);
            true
        })
    }

    // Load queue (per-slot state; see `ChunkSlot::queued`)

    /// Enqueues the chunk at `slot` at `priority` (lower = more urgent). A re-enqueue while still
    /// queued only raises the priority (see `ChunkSlot::queue_at`). No-op if `slot` is out of range
    /// or free.
    pub(crate) fn loader_enqueue(&self, slot: u32, priority: u32) {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return;
        }
        m_rmw(&self.chunks[i], |s| {
            if s.backing.is_null() {
                return;
            }
            s.queue_at(priority);
            // SAFETY: a plain counter read; no preconditions.
            s.queued_at = unsafe { deluge_debug_now_frames() };
        });
    }

    /// Enqueues the chunk at `slot` and takes a queue-owned hard lease, so the entry survives the
    /// enqueuer dropping its own (see `ChunkSlot::queue_leased`).
    ///
    /// A re-enqueue raises the priority as `loader_enqueue` does, without a second lease. Every terminal path of the drain
    /// must call `loader_release_owned` (or `loader_remove`); a stranded queue lease pins the chunk
    /// forever. No-op if `slot` is out of range or free.
    pub(crate) fn loader_enqueue_owned(&self, slot: u32, priority: u32) {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return;
        }
        m_rmw(&self.chunks[i], |s| {
            if s.backing.is_null() {
                return;
            }
            if !s.queue_leased {
                s.leases += 1;
                s.queue_leased = true;
            }
            s.queue_at(priority);
        });
    }

    /// Releases the queue-owned lease from `loader_enqueue_owned`. Returns whether there was one,
    /// i.e. whether the chunk was queue-owned rather than enqueued with plain `loader_enqueue`.
    ///
    /// Idempotent, and does not de-queue: the drain calls it on chunks `loader_next` has already
    /// popped.
    pub(crate) fn loader_release_owned(&self, slot: u32) -> bool {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return false;
        }
        self.rmw_by_ptr_slot(i, |s| {
            if s.backing.is_null() || !s.queue_leased {
                return false;
            }
            s.leases = s.leases.saturating_sub(1);
            s.queue_leased = false;
            true
        })
    }

    /// Removes the chunk at `slot` from the load queue and drops any queue-owned lease, which
    /// would otherwise pin it permanently.
    pub(crate) fn loader_remove(&self, slot: u32) {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return;
        }
        m_rmw(&self.chunks[i], |s| {
            if s.queued {
                s.queued = false;
            }
            if s.queue_leased {
                s.leases = s.leases.saturating_sub(1);
                s.queue_leased = false;
            }
        });
    }

    /// Pops the most urgent queued chunk that is still leased: clears its `queued` flag and
    /// returns its backing, or null if there is none.
    ///
    /// Unleased queued chunks are skipped and stay resident for normal eviction. The lease is a
    /// correctness guard: an unleased chunk is evictable, so filling it could write into a recycled
    /// slot. A popped chunk may hold a queue-owned lease, which the caller must release with
    /// `loader_release_owned` on every terminal path. O(n) scan.
    pub(crate) fn loader_next(&self) -> *mut u8 {
        // Per-slot masked scan; the pick is re-validated under the mask below.
        let mut best: Option<usize> = None;
        let mut best_pri = u32::MAX;
        let mut depth = 0u64; // queued+leased candidates this poll (Stats::loader_depth_*)
        for (i, slot) in self.chunks.iter().enumerate() {
            let s = m_get(slot);
            if !s.queued || s.backing.is_null() || s.leases == 0 {
                continue;
            }
            depth += 1;
            if best.is_none() || s.queue_priority < best_pri {
                best = Some(i);
                best_pri = s.queue_priority;
            }
        }
        self.stat(|st| {
            st.loader_depth_polls += 1;
            st.loader_depth_total += depth;
            if depth > st.loader_depth_max {
                st.loader_depth_max = depth;
            }
        });
        let Some(i) = best else {
            return ptr::null_mut();
        };
        let mut waited_for_stats: Option<u64> = None;
        // Commit in one masked window: it may have been released or evicted since the scan.
        let _m = Masked::enter();
        let mut s = self.chunks[i].get();
        if !s.queued || s.backing.is_null() || s.leases == 0 {
            return ptr::null_mut(); // changed under us — caller retries next poll
        }
        // Queue wait ends here; re-stamp so `mark_ready` measures the fill through the same field.
        if s.queued_at != 0 {
            // SAFETY: a plain counter read; no preconditions.
            let now = unsafe { deluge_debug_now_frames() };
            waited_for_stats = Some(now.wrapping_sub(s.queued_at) as u64);
            s.queued_at = now;
        }
        s.queued = false;
        self.chunks[i].set(s);
        drop(_m); // keep the window short; `stat` masks on its own
        if let Some(waited) = waited_for_stats {
            self.stat(|st| {
                st.queue_wait_samples += 1;
                st.queue_wait_total += waited;
                if waited > st.queue_wait_max {
                    st.queue_wait_max = waited;
                }
            });
        }
        s.backing
    }

    /// Returns whether any queued, leased chunk sits at the least urgent priority (`u32::MAX`);
    /// `load_song_ui`'s "background load work remains" check.
    fn loader_has_lowest(&self) -> bool {
        (0..self.chunks.len()).any(|i| {
            let s = m_get(&self.chunks[i]);
            s.queued && !s.backing.is_null() && s.leases > 0 && s.queue_priority == u32::MAX
        })
    }

    /// Returns whether `loader_next` would return a chunk, without popping anything. Backs
    /// `deluge_streaming_queue_drained`.
    fn loader_has_any(&self) -> bool {
        (0..self.chunks.len()).any(|i| {
            let s = m_get(&self.chunks[i]);
            s.queued && !s.backing.is_null() && s.leases > 0
        })
    }

    /// Returns whether no resident chunk of `asset` has an index above `index`; gates tail-first
    /// eviction. O(n).
    fn is_highest_resident(&self, asset: u32, index: u32) -> bool {
        !(0..self.chunks.len()).any(|i| {
            let s = m_get(&self.chunks[i]);
            !s.backing.is_null() && s.asset == asset && s.index > index
        })
    }

    /// Protects `asset`'s chunks from eviction until the returned guard drops.
    fn protect_asset(&self, asset: u32) -> ProtectGuard<'_> {
        let prev = m_get(&self.protect);
        m_set(&self.protect, asset);
        ProtectGuard { mgr: self, prev }
    }

    /// Returns the slab backing `asset`'s chunks, or null for heap backing. Adopted chunks
    /// (`NONE`) are always heap-backed.
    #[inline]
    fn slab_for(&self, asset: u32) -> *mut DelugeSlab {
        let slab = m_get(&self.slab);
        if asset != NONE
            && !slab.is_null()
            && m_get(&self.assets[asset as usize]).source.backing == BACKING_SLAB
        {
            slab
        } else {
            ptr::null_mut()
        }
    }

    /// Frees a chunk's backing to the slab or the heap, whichever it came from.
    fn free_backing(&self, backing: *mut u8, asset: u32) {
        let slab = self.slab_for(asset);
        if !slab.is_null() {
            // SAFETY: `slab` is live; release returns false for a block it does not own, which
            // then came from the heap.
            if unsafe { deluge_slab_release(slab, backing) } {
                return;
            }
        }
        // SAFETY: `backing` was returned by deluge_alloc on this heap and is live.
        unsafe { deluge_free(self.heap, backing) };
    }

    /// Allocates a chunk's backing: a slab slot for slab-backed assets, else `size` heap bytes
    /// (16-byte aligned).
    fn alloc_backing(&self, size: usize, asset: u32) -> *mut u8 {
        let slab = self.slab_for(asset);
        let p = if !slab.is_null() {
            // SAFETY: `slab` is a live unmanaged slab over the same heap; slots are uniform, and
            // the owner is unused because the slab never self-evicts.
            unsafe { deluge_slab_acquire(slab, ptr::null_mut()) }
        } else {
            // SAFETY: `self.heap` is a live heap handle (checked at create).
            unsafe { deluge_alloc(self.heap, size, 16) }
        };
        if p.is_null() {
            self.stat(|s| s.alloc_failures += 1);
        }
        p
    }

    /// Evicts the lowest-ranked unleased, clean chunk. Returns false if nothing is evictable.
    /// Serves as the heap reclaim hook and frees table slots.
    fn evict_lowest(&self) -> bool {
        let protect = m_get(&self.protect);
        // The commit bails if the victim was leased or dirtied since the scan; then retry
        // without it. Bounded by the table size.
        let mut skip: Option<usize> = None;
        // A floor: the nested `is_highest_resident` scans for tail-first assets are not counted.
        let mut visited = 0u64;
        let mut result = false;
        for _ in 0..self.chunks.len() {
            let mut best: Option<usize> = None;
            let mut best_rank = (u8::MAX, u64::MAX, u64::MAX);
            for i in 0..self.chunks.len() {
                if Some(i) == skip {
                    continue;
                }
                visited += 1;
                let s = m_get(&self.chunks[i]);
                if s.backing.is_null() || s.leases != 0 || s.dirty {
                    continue;
                }
                // NONE means no protection (and also marks adopted chunks, so test it first).
                if protect != NONE && s.asset == protect {
                    continue;
                }
                let (cost, soft_refs) = if s.asset == NONE {
                    (s.cost, 0)
                } else if (s.asset as usize) < self.assets.len() {
                    let a = m_get(&self.assets[s.asset as usize]);
                    // Tail-first assets: only the highest-index chunk is a candidate.
                    if a.evict_tail_first && !self.is_highest_resident(s.asset, s.index) {
                        continue;
                    }
                    (a.source.cost, a.soft_refs)
                } else {
                    continue; // out-of-range asset index
                };
                let rank = evict_rank(soft_refs, cost, s.size, s.recency);
                if rank < best_rank {
                    best_rank = rank;
                    best = Some(i);
                }
            }
            let Some(i) = best else { break };
            if self.evict_slot(i) {
                result = true;
                break;
            }
            skip = Some(i); // commit bailed; re-scan without it
        }
        self.stat(|s| {
            s.scan_evict_calls += 1;
            s.scan_evict_slots += visited;
        });
        result
    }

    /// Evicts the chunk at slot `i`, returning false if it is no longer evictable.
    ///
    /// The slot is re-validated and cleared in one masked window before any callback runs, so the
    /// table is consistent during `on_evict`; the callback and the free run unmasked.
    fn evict_slot(&self, i: usize) -> bool {
        // Masked commit: re-read, validate, capture, clear.
        let s = {
            let _m = Masked::enter();
            let s = self.chunks[i].get();
            if s.backing.is_null() || s.leases != 0 || s.dirty {
                return false; // changed under us since the scan — do not evict
            }
            // Unlink before clearing (EMPTY wipes the chain links), in the same window.
            self.index_remove(i, s.asset, s.index);
            self.ptr_index_remove(i, s.backing);
            self.chunks[i].set(ChunkSlot::EMPTY);
            s
        };
        // Never hold the mask across a callback or a free.
        let cost: u32 = if s.asset == NONE {
            s.cost
        } else if (s.asset as usize) < self.assets.len() {
            m_get(&self.assets[s.asset as usize]).source.cost
        } else {
            0
        };
        let bucket = (cost as usize).min(COST_BUCKETS - 1);
        self.stat(|st| {
            st.evictions += 1;
            st.evictions_by_cost[bucket] += 1;
        });
        if s.asset == NONE {
            if let Some(cb) = s.adopt_evict {
                // SAFETY: the callback and ctx were supplied with this block at adopt.
                unsafe { cb(s.adopt_ctx, s.backing) };
            }
        } else if (s.asset as usize) < self.assets.len() {
            let a = m_get(&self.assets[s.asset as usize]);
            if let Some(cb) = a.source.on_evict {
                // SAFETY: the callback, ctx and owner come from the asset that owns this chunk.
                unsafe { cb(a.source.ctx, a.owner, s.index) };
            }
        }
        self.free_backing(s.backing, s.asset);
        true
    }

    /// Returns chunk `index` of `asset` under a hard lease, materializing it if not resident; null
    /// on OOM or reconstruction failure. A cache hit just adds a lease.
    fn acquire(&self, asset: u32, index: u32, size: usize) -> *mut u8 {
        let ai = asset as usize;
        if ai >= self.assets.len() || !m_get(&self.assets[ai]).in_use {
            return ptr::null_mut();
        }
        self.stat(|s| s.acquires += 1);
        // Self-protecting assets keep their chunks through this call, including the reentrant
        // reclaim hook during alloc_backing.
        let prot = if m_get(&self.assets[ai]).self_protect {
            asset
        } else {
            NONE
        };
        let _g = self.protect_asset(prot);
        // Cache hit.
        if let Some(c) = self.find_resident(asset, index) {
            let r = self.bump();
            let hit = self.rmw_by_ptr_slot(c, |s| {
                if s.backing.is_null() || s.asset != asset || s.index != index {
                    return ptr::null_mut();
                }
                s.leases += 1;
                s.recency = r;
                s.backing
            });
            if !hit.is_null() {
                self.stat(|s| s.acquire_hits += 1);
                return hit;
            }
        }
        // Reserve a free slot (evicting the lowest-value chunk if the table is full).
        let idx = match self.find_free_chunk() {
            Some(i) => i,
            None => {
                if !self.evict_lowest() {
                    return ptr::null_mut(); // table full and nothing evictable
                }
                match self.find_free_chunk() {
                    Some(i) => i,
                    None => return ptr::null_mut(), // freed slot consumed under preemption
                }
            }
        };
        // May reentrantly evict other chunks through the reclaim hook; `idx` is still empty, so
        // it is not a candidate.
        let p = self.alloc_backing(size, asset);
        if p.is_null() {
            return ptr::null_mut();
        }
        // Lease before materializing, so a reentrant eviction cannot take this chunk. One masked
        // window covers the slot write and its index links (see index_insert).
        {
            let recency = self.bump();
            let generation = self.next_gen();
            let _m = Masked::enter();
            self.chunks[idx].set(ChunkSlot {
                backing: p,
                asset,
                index,
                leases: 1,
                dirty: false,
                ready: true, // acquire materializes synchronously below, before anyone else can run
                recency,
                size: size as u32,
                generation,
                ..ChunkSlot::EMPTY
            });
            self.index_insert(idx);
            self.ptr_index_insert(idx);
        }
        let a = m_get(&self.assets[ai]);
        let ok = match a.source.materialize {
            Some(f) => {
                self.stat(|s| s.materializes += 1);
                // SAFETY: ctx/owner come from this asset; `p` is the `size`-byte backing just
                // allocated.
                unsafe { f(a.source.ctx, a.owner, index, p, size) }
            }
            None => true,
        };
        if !ok {
            // Roll back, unlinking before the clear.
            {
                let _m = Masked::enter();
                self.index_remove(idx, asset, index);
                self.ptr_index_remove(idx, p);
                self.chunks[idx].set(ChunkSlot::EMPTY);
            }
            self.free_backing(p, asset);
            return ptr::null_mut();
        }
        p
    }

    /// Adds a hard lease to the resident chunk backing `p` and bumps its recency. No-op if `p` is
    /// not resident.
    pub(crate) fn add_lease(&self, p: *mut u8) {
        let r = self.bump();
        self.rmw_by_ptr(p, |s| {
            s.leases += 1;
            s.recency = r;
        });
    }

    /// Returns chunk `index` of `asset` under a hard lease, allocating and constructing it (no
    /// I/O) if not resident; the loader fills the data later and calls `mark_ready`.
    ///
    /// A cache hit just leases. Returns null on OOM, a full table with nothing evictable, or an
    /// asset without a `construct` callback.
    pub(crate) fn request(&self, asset: u32, index: u32, size: usize) -> *mut u8 {
        let ai = asset as usize;
        if ai >= self.assets.len() || !m_get(&self.assets[ai]).in_use {
            return ptr::null_mut();
        }
        if m_get(&self.assets[ai]).source.construct.is_none() {
            return ptr::null_mut(); // not a requestable asset
        }
        self.stat(|s| s.requests += 1);
        // Self-protecting assets keep their chunks through this call (see `acquire`).
        let prot = if m_get(&self.assets[ai]).self_protect {
            asset
        } else {
            NONE
        };
        let _g = self.protect_asset(prot);
        // Cache hit (constructed, maybe loaded): just lease.
        if let Some(c) = self.find_resident(asset, index) {
            let r = self.bump();
            let hit = self.rmw_by_ptr_slot(c, |s| {
                if s.backing.is_null() || s.asset != asset || s.index != index {
                    return ptr::null_mut();
                }
                s.leases += 1;
                s.recency = r;
                s.backing
            });
            if !hit.is_null() {
                return hit;
            }
        }
        let idx = match self.find_free_chunk() {
            Some(i) => i,
            None => {
                if !self.evict_lowest() {
                    return ptr::null_mut();
                }
                match self.find_free_chunk() {
                    Some(i) => i,
                    None => return ptr::null_mut(), // freed slot consumed under preemption
                }
            }
        };
        let p = self.alloc_backing(size, asset);
        if p.is_null() {
            return ptr::null_mut();
        }
        // Lease before constructing, in one window with the index links (see `acquire`).
        {
            let recency = self.bump();
            let generation = self.next_gen();
            let _m = Masked::enter();
            self.chunks[idx].set(ChunkSlot {
                backing: p,
                asset,
                index,
                leases: 1,
                dirty: false,
                recency,
                size: size as u32,
                generation,
                ..ChunkSlot::EMPTY
            });
            self.index_insert(idx);
            self.ptr_index_insert(idx);
        }
        let a = m_get(&self.assets[ai]);
        // SAFETY: ctx/owner come from this asset and `p` is the backing just allocated;
        // `construct` was checked non-None above.
        unsafe { (a.source.construct.unwrap())(a.source.ctx, a.owner, index, p) };
        p
    }

    fn set_construct(&self, asset: u32, construct: Option<ConstructFn>) {
        let ai = asset as usize;
        if ai >= self.assets.len() {
            return;
        }
        m_rmw(&self.assets[ai], |a| a.source.construct = construct);
    }

    fn set_evict_tail_first(&self, asset: u32, on: bool) {
        let ai = asset as usize;
        if ai >= self.assets.len() {
            return;
        }
        m_rmw(&self.assets[ai], |a| a.evict_tail_first = on);
    }

    fn set_self_protect(&self, asset: u32, on: bool) {
        let ai = asset as usize;
        if ai >= self.assets.len() {
            return;
        }
        m_rmw(&self.assets[ai], |a| a.self_protect = on);
    }

    /// Frees the resident chunk backing `p` without calling `on_evict`, for an owner discarding a
    /// chunk itself (e.g. a sample cache truncating its tail). No-op if `p` is not resident.
    fn evict_chunk(&self, p: *mut u8) {
        let Some(c) = self.find_by_ptr(p) else {
            return;
        };
        let s = {
            let _m = Masked::enter();
            let s = self.chunks[c].get();
            if s.backing != p {
                return; // changed under us
            }
            self.index_remove(c, s.asset, s.index); // before the clear, which wipes the links
            self.ptr_index_remove(c, s.backing);
            self.chunks[c].set(ChunkSlot::EMPTY);
            s
        };
        self.free_backing(s.backing, s.asset); // unmasked
    }

    /// Drops one hard lease on the chunk backing `p`; it stays cached until evicted. Safe from any
    /// context, including an ISR.
    pub(crate) fn release(&self, p: *mut u8) {
        self.rmw_by_ptr(p, |s| {
            if s.leases > 0 {
                s.leases -= 1;
            }
        });
    }

    /// Registers an owner-built heap block as an unleased chunk the manager may evict (ranked by
    /// `cost`, `size` and recency). On eviction it calls `on_evict(ctx, ptr)` and then frees `ptr`.
    ///
    /// Returns `ptr`, or null if the table is full and nothing is evictable.
    fn adopt(
        &self,
        ptr: *mut u8,
        size: usize,
        cost: u32,
        ctx: *mut c_void,
        on_evict: Option<AdoptEvictFn>,
    ) -> *mut u8 {
        if ptr.is_null() {
            return ptr::null_mut();
        }
        let idx = match self.find_free_chunk() {
            Some(i) => i,
            None => {
                if !self.evict_lowest() {
                    return ptr::null_mut();
                }
                match self.find_free_chunk() {
                    Some(i) => i,
                    None => return ptr::null_mut(), // freed slot consumed under preemption
                }
            }
        };
        // One window over the slot write and its pointer-index link (see index_insert). Adopted
        // chunks are only in the pointer index.
        {
            let recency = self.bump();
            let generation = self.next_gen();
            let _m = Masked::enter();
            self.chunks[idx].set(ChunkSlot {
                backing: ptr,
                asset: NONE,
                index: 0,
                leases: 0,
                dirty: false,
                ready: true, // owner-built object, usable immediately
                recency,
                size: size as u32,
                generation,
                cost,
                adopt_evict: on_evict,
                adopt_ctx: ctx,
                ..ChunkSlot::EMPTY
            });
            self.ptr_index_insert(idx);
        }
        self.stat(|s| s.adopts += 1);
        ptr
    }

    fn touch(&self, p: *mut u8) {
        let r = self.bump();
        self.rmw_by_ptr(p, |s| s.recency = r);
    }

    fn set_dirty(&self, p: *mut u8, dirty: bool) {
        self.rmw_by_ptr(p, |s| s.dirty = dirty);
    }

    /// Marks the `request`ed chunk backing `p` as loaded. No-op if `p` is not resident.
    pub(crate) fn mark_ready(&self, p: *mut u8) {
        let mut filled: Option<u32> = None;
        self.rmw_by_ptr(p, |s| {
            s.ready = true;
            if s.queued_at != 0 {
                // SAFETY: a plain counter read; no preconditions.
                let now = unsafe { deluge_debug_now_frames() };
                filled = Some(now.wrapping_sub(s.queued_at));
                s.queued_at = 0;
            }
        });
        if let Some(f) = filled {
            self.stat(|st| {
                st.fill_samples += 1;
                st.fill_total += f as u64;
                if (f as u64) > st.fill_max {
                    st.fill_max = f as u64;
                }
            });
        }
    }

    /// Returns whether the chunk backing `p` is resident and ready.
    pub(crate) fn is_ready_by_ptr(&self, p: *mut u8) -> bool {
        let Some(i) = self.find_by_ptr(p) else {
            return false;
        };
        let _m = Masked::enter();
        let s = self.chunks[i].get();
        s.backing == p && s.ready
    }

    /// Returns chunk `index` of `asset` under a hard lease if it is resident and ready, else null.
    ///
    /// RT-safe: never allocates, materializes or blocks. Bumps recency on a hit.
    pub(crate) fn try_acquire(&self, asset: u32, index: u32) -> *mut u8 {
        let Some(c) = self.find_resident(asset, index) else {
            return ptr::null_mut();
        };
        let r = self.bump();
        m_rmw(&self.chunks[c], |s| {
            if s.backing.is_null() || !s.ready || s.asset != asset || s.index != index {
                return ptr::null_mut();
            }
            s.leases += 1;
            s.recency = r;
            s.backing
        })
    }

    /// Returns the backing of chunk `index` of `asset` if resident, ready or not, else null.
    ///
    /// Takes no lease and leaves recency alone, so it does not perturb eviction order.
    pub(crate) fn peek(&self, asset: u32, index: u32) -> *mut u8 {
        let Some(c) = self.find_resident(asset, index) else {
            return ptr::null_mut();
        };
        // The slot may have been reused since the lookup: re-validate identity.
        let s = m_get(&self.chunks[c]);
        if s.backing.is_null() || s.asset != asset || s.index != index {
            return ptr::null_mut();
        }
        s.backing
    }

    fn define_asset(&self, owner: *mut c_void, source: Source) -> u32 {
        for (i, slot) in self.assets.iter().enumerate() {
            if m_get(slot).in_use {
                continue;
            }
            let claimed = m_rmw(slot, |a| {
                if a.in_use {
                    return false; // lost the race for this slot
                }
                a.in_use = true;
                a.owner = owner;
                a.source = source;
                a.soft_refs = 0;
                true
            });
            if claimed {
                return i as u32;
            }
        }
        NONE
    }

    /// Retires an asset at owner teardown: frees every resident chunk (leased ones too), calling
    /// `on_evict` for each, then frees the asset slot.
    fn release_asset(&self, asset: u32) {
        // Unmasked: this runs at owner teardown and is not safe against concurrent access to
        // the same manager.
        let ai = asset as usize;
        if ai >= self.assets.len() || !self.assets[ai].get().in_use {
            return;
        }
        let a = self.assets[ai].get();
        let drop_chunk = |i: usize| {
            let s = self.chunks[i].get();
            // Unlink, then clear the slot before the callback so a reentrant call sees a
            // consistent table. The asset slot is cleared last, since `free_backing` reads its
            // backing kind.
            self.index_remove(i, s.asset, s.index);
            self.ptr_index_remove(i, s.backing);
            self.chunks[i].set(ChunkSlot::EMPTY);
            if let Some(cb) = a.source.on_evict {
                // SAFETY: the callback, ctx and owner come from this asset.
                unsafe { cb(a.source.ctx, a.owner, s.index) };
            }
            self.free_backing(s.backing, asset);
        };
        if a.evict_tail_first {
            // Highest index first, so `on_evict` never discards a chunk still tracked here.
            loop {
                let mut hi: Option<usize> = None;
                let mut hidx = 0u32;
                for (i, c) in self.chunks.iter().enumerate() {
                    let s = c.get();
                    if !s.backing.is_null() && s.asset == asset && (hi.is_none() || s.index >= hidx)
                    {
                        hi = Some(i);
                        hidx = s.index;
                    }
                }
                let Some(i) = hi else { break };
                drop_chunk(i);
            }
        } else {
            for (i, slot) in self.chunks.iter().enumerate() {
                let s = slot.get();
                if !s.backing.is_null() && s.asset == asset {
                    drop_chunk(i);
                }
            }
        }
        self.assets[ai].set(AssetSlot::EMPTY);
    }

    fn set_slab(&self, slab: *mut DelugeSlab) {
        m_set(&self.slab, slab);
    }

    fn reference(&self, asset: u32, delta: i32) {
        let ai = asset as usize;
        if ai >= self.assets.len() {
            return;
        }
        m_rmw(&self.assets[ai], |a| {
            if delta > 0 {
                a.soft_refs += delta as u32;
            } else {
                a.soft_refs = a.soft_refs.saturating_sub((-delta) as u32);
            }
        });
    }
}

extern "C" fn resource_reclaim(ctx: *mut c_void, _bytes_needed: usize) -> bool {
    // SAFETY: `ctx` is the manager registered at create, which outlives the heap. A second
    // `&Manager` is sound: no `&mut` ever exists and mutation goes through `Cell`.
    let m = unsafe { &*(ctx as *mut Manager) };
    m.evict_lowest()
}

// C ABI (`deluge_resource.h`)

/// Opaque manager handle (`DelugeResource` in `deluge_resource.h`).
#[repr(C)]
pub struct DelugeResource {
    _opaque: [u8; 0],
}

/// Asserts the hash indexes and the chunk table describe the same set of chunks.
///
/// Checks both directions: every occupied slot is reachable from its own bucket exactly once (a
/// missed insert makes a resident chunk invisible, so its caller allocates a duplicate), and every
/// chain entry is an occupied slot that hashes to that bucket (a missed remove leaves a dangling
/// link into a recycled slot).
///
/// # Safety
///
/// `h` must be a live manager handle.
#[cfg(test)]
pub(crate) unsafe fn debug_assert_index_consistent(h: *mut DelugeResource) {
    // SAFETY: caller contract — `h` is a live handle.
    let m = unsafe { mgr(h) };
    for b in 0..m.buckets.len() {
        let mut cur = m.buckets[b].get();
        let mut guard = 0;
        while cur != NONE {
            let i = cur as usize;
            assert!(
                i < m.chunks.len(),
                "bucket {b} links to out-of-range slot {i}"
            );
            let s = m.chunks[i].get();
            assert!(!s.backing.is_null(), "bucket {b} links to free slot {i}");
            assert_ne!(s.asset, NONE, "adopted slot {i} must not be indexed");
            assert_eq!(
                m.bucket_of(s.asset, s.index),
                b,
                "slot {i} is in the wrong bucket"
            );
            cur = s.hash_next;
            guard += 1;
            assert!(guard <= m.chunks.len(), "bucket {b} chain is cyclic");
        }
    }
    for i in 0..m.chunks.len() {
        let s = m.chunks[i].get();
        if s.backing.is_null() || s.asset == NONE {
            continue;
        }
        let mut cur = m.buckets[m.bucket_of(s.asset, s.index)].get();
        let mut seen = 0;
        while cur != NONE {
            if cur as usize == i {
                seen += 1;
            }
            cur = m.chunks[cur as usize].get().hash_next;
        }
        assert_eq!(
            seen, 1,
            "occupied slot {i} appears {seen} times in its bucket"
        );
    }

    // The same for the pointer index, which holds every occupied slot, adopted ones included.
    for b in 0..m.ptr_buckets.len() {
        let mut cur = m.ptr_buckets[b].get();
        let mut guard = 0;
        while cur != NONE {
            let i = cur as usize;
            assert!(
                i < m.chunks.len(),
                "ptr bucket {b} links to out-of-range slot {i}"
            );
            let s = m.chunks[i].get();
            assert!(
                !s.backing.is_null(),
                "ptr bucket {b} links to free slot {i}"
            );
            assert_eq!(
                m.ptr_bucket_of(s.backing),
                b,
                "slot {i} is in the wrong ptr bucket"
            );
            cur = s.ptr_next;
            guard += 1;
            assert!(guard <= m.chunks.len(), "ptr bucket {b} chain is cyclic");
        }
    }
    for i in 0..m.chunks.len() {
        let s = m.chunks[i].get();
        if s.backing.is_null() {
            continue;
        }
        let mut cur = m.ptr_buckets[m.ptr_bucket_of(s.backing)].get();
        let mut seen = 0;
        while cur != NONE {
            if cur as usize == i {
                seen += 1;
            }
            cur = m.chunks[cur as usize].get().ptr_next;
        }
        assert_eq!(
            seen, 1,
            "occupied slot {i} appears {seen} times in its ptr bucket"
        );
    }
}

/// Returns `(indexed, linear)` lookups of `(asset, index)`, so a test can compare the index with
/// the brute-force scan.
///
/// # Safety
///
/// `h` must be a live manager handle.
#[cfg(test)]
pub(crate) unsafe fn debug_find_resident_both(
    h: *mut DelugeResource,
    asset: u32,
    index: u32,
) -> (Option<usize>, Option<usize>) {
    // SAFETY: caller contract — `h` is a live handle.
    let m = unsafe { mgr(h) };
    (
        m.find_resident(asset, index),
        m.find_resident_linear(asset, index),
    )
}

/// Returns the manager behind a handle. Taking a shared reference is sound while others exist,
/// since no `&mut Manager` is ever formed.
///
/// # Safety
///
/// `h` must be a non-null live handle from `deluge_resource_create`.
#[inline]
pub(crate) unsafe fn mgr<'a>(h: *mut DelugeResource) -> &'a Manager {
    &*(h as *mut Manager)
}

/// Creates a manager over `heap` with fixed-capacity asset and chunk tables, and registers it as
/// the heap's reclaim hook. Returns null on OOM or a zero capacity.
///
/// `chunk_cap` should exceed the most chunks that can be resident at once, so the table is never
/// the limiter. The manager and its tables are never freed.
///
/// # Safety
///
/// `heap` must be null or a live heap that outlives every use of the returned handle.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_create(
    heap: *mut DelugeHeap,
    asset_cap: usize,
    chunk_cap: usize,
) -> *mut DelugeResource {
    create_inner(heap, asset_cap, chunk_cap, true)
}

/// Like [`deluge_resource_create`] but without registering the reclaim hook, for a caller whose
/// own reclaim hook drives eviction through [`deluge_resource_try_evict`].
///
/// # Safety
///
/// As for [`deluge_resource_create`].
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_create_unhooked(
    heap: *mut DelugeHeap,
    asset_cap: usize,
    chunk_cap: usize,
) -> *mut DelugeResource {
    create_inner(heap, asset_cap, chunk_cap, false)
}

/// Evicts the single lowest-ranked evictable chunk (the reclaim-hook body). Returns whether
/// something was freed.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_try_evict(handle: *mut DelugeResource) -> bool {
    if handle.is_null() {
        return false;
    }
    mgr(handle).evict_lowest()
}

unsafe fn create_inner(
    heap: *mut DelugeHeap,
    asset_cap: usize,
    chunk_cap: usize,
    register_hook: bool,
) -> *mut DelugeResource {
    if heap.is_null() || asset_cap == 0 || chunk_cap == 0 {
        return ptr::null_mut();
    }
    let m = deluge_alloc(
        heap,
        core::mem::size_of::<Manager>(),
        core::mem::align_of::<Manager>().max(16),
    ) as *mut Manager;
    let assets_raw = deluge_alloc(
        heap,
        asset_cap * core::mem::size_of::<Cell<AssetSlot>>(),
        core::mem::align_of::<Cell<AssetSlot>>().max(16),
    ) as *mut Cell<AssetSlot>;
    let chunks_raw = deluge_alloc(
        heap,
        chunk_cap * core::mem::size_of::<Cell<ChunkSlot>>(),
        core::mem::align_of::<Cell<ChunkSlot>>().max(16),
    ) as *mut Cell<ChunkSlot>;
    // Power of two >= chunk_cap: bucket selection is a mask and the load factor stays below 1.
    let bucket_cap = chunk_cap.next_power_of_two();
    let buckets_raw = deluge_alloc(
        heap,
        bucket_cap * core::mem::size_of::<Cell<u32>>(),
        core::mem::align_of::<Cell<u32>>().max(16),
    ) as *mut Cell<u32>;
    let ptr_buckets_raw = deluge_alloc(
        heap,
        bucket_cap * core::mem::size_of::<Cell<u32>>(),
        core::mem::align_of::<Cell<u32>>().max(16),
    ) as *mut Cell<u32>;
    if m.is_null()
        || assets_raw.is_null()
        || chunks_raw.is_null()
        || buckets_raw.is_null()
        || ptr_buckets_raw.is_null()
    {
        deluge_free(heap, m as *mut u8);
        deluge_free(heap, assets_raw as *mut u8);
        deluge_free(heap, chunks_raw as *mut u8);
        deluge_free(heap, buckets_raw as *mut u8);
        deluge_free(heap, ptr_buckets_raw as *mut u8);
        return ptr::null_mut();
    }
    for i in 0..bucket_cap {
        buckets_raw.add(i).write(Cell::new(NONE));
        ptr_buckets_raw.add(i).write(Cell::new(NONE));
    }
    for i in 0..asset_cap {
        assets_raw.add(i).write(Cell::new(AssetSlot::EMPTY));
    }
    for i in 0..chunk_cap {
        chunks_raw.add(i).write(Cell::new(ChunkSlot::EMPTY));
    }
    // The tables are never freed, so 'static is sound.
    let assets: &'static [Cell<AssetSlot>] = &*ptr::slice_from_raw_parts(assets_raw, asset_cap);
    let chunks: &'static [Cell<ChunkSlot>] = &*ptr::slice_from_raw_parts(chunks_raw, chunk_cap);
    let buckets: &'static [Cell<u32>] = &*ptr::slice_from_raw_parts(buckets_raw, bucket_cap);
    let ptr_buckets: &'static [Cell<u32>] =
        &*ptr::slice_from_raw_parts(ptr_buckets_raw, bucket_cap);
    ptr::write(
        m,
        Manager {
            heap,
            slab: Cell::new(ptr::null_mut()),
            assets,
            chunks,
            buckets,
            ptr_buckets,
            tick: Cell::new(0),
            protect: Cell::new(NONE),
            stats: Cell::new(Stats::default()),
            alloc_gen: Cell::new(0),
        },
    );
    if register_hook {
        deluge_heap_register_reclaim(heap, resource_reclaim, m as *mut c_void);
    }
    m as *mut DelugeResource
}

/// Sets the slab that backs [`BACKING_SLAB`] assets; until set, every asset uses the heap.
///
/// Create it with `deluge_slab_create_unmanaged` over the same heap, so eviction stays with this
/// manager.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`. `slab` must be null or a
/// live slab that outlives the manager.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_set_slab(
    handle: *mut DelugeResource,
    slab: *mut DelugeSlab,
) {
    if !handle.is_null() {
        mgr(handle).set_slab(slab);
    }
}

/// Defines an asset: an opaque `owner` token and its [`Source`]. Returns the asset id, or
/// `0xFFFFFFFF` if the asset table is full.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`. The callbacks and `ctx`
/// must stay valid until the asset is released.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_define_asset(
    handle: *mut DelugeResource,
    owner: *mut c_void,
    materialize: Option<MaterializeFn>,
    on_evict: Option<EvictFn>,
    ctx: *mut c_void,
    cost: u32,
    backing: u32,
) -> u32 {
    if handle.is_null() {
        return NONE;
    }
    mgr(handle).define_asset(
        owner,
        Source {
            materialize,
            on_evict,
            construct: None, // set by deluge_resource_set_construct
            ctx,
            cost,
            backing,
        },
    )
}

/// Retires an asset: frees its resident chunks (calling `on_evict` for each) and its slot. Call
/// when the owner is destroyed, or the fixed-size asset table fills over a session.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`. Not safe against
/// concurrent access to the same manager.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_release_asset(handle: *mut DelugeResource, asset: u32) {
    if !handle.is_null() {
        mgr(handle).release_asset(asset);
    }
}

/// Sets (or, with `None`, clears) an asset's `construct` callback, making it requestable.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_set_construct(
    handle: *mut DelugeResource,
    asset: u32,
    construct: Option<ConstructFn>,
) {
    if !handle.is_null() {
        mgr(handle).set_construct(asset, construct);
    }
}

/// Marks an asset prefix-dependent: its `on_evict` discards every higher-index chunk, so only
/// its highest-index resident chunk is ever evicted.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_set_evict_tail_first(
    handle: *mut DelugeResource,
    asset: u32,
    on: bool,
) {
    if !handle.is_null() {
        mgr(handle).set_evict_tail_first(asset, on);
    }
}

/// Marks an asset self-protecting: while it allocates a chunk, its own chunks are not eviction
/// candidates. For unleased caches whose `request(N+1)` must not evict the just-written `N`.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_set_self_protect(
    handle: *mut DelugeResource,
    asset: u32,
    on: bool,
) {
    if !handle.is_null() {
        mgr(handle).set_self_protect(asset, on);
    }
}

/// Returns chunk `index` of `asset` under a hard lease, constructing it without I/O if not
/// resident; the loader fills it and calls [`deluge_resource_mark_ready`]. Null on OOM or if the
/// asset has no `construct` callback.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_request(
    handle: *mut DelugeResource,
    asset: u32,
    index: u32,
    size: usize,
) -> *mut u8 {
    if handle.is_null() {
        return ptr::null_mut();
    }
    mgr(handle).request(asset, index, size)
}

/// Returns chunk `index` of `asset` under a hard lease, materializing it if needed; null on OOM
/// or reconstruction failure. The backing is 16-byte aligned.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_acquire(
    handle: *mut DelugeResource,
    asset: u32,
    index: u32,
    size: usize,
) -> *mut u8 {
    if handle.is_null() {
        return ptr::null_mut();
    }
    mgr(handle).acquire(asset, index, size)
}

/// Returns chunk `index` of `asset` under a hard lease only if it is resident and ready; null
/// otherwise. RT-safe: never allocates, materializes or blocks.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_try_acquire(
    handle: *mut DelugeResource,
    asset: u32,
    index: u32,
) -> *mut u8 {
    if handle.is_null() {
        return ptr::null_mut();
    }
    mgr(handle).try_acquire(asset, index)
}

/// Returns the backing of chunk `index` of `asset` if resident, ready or not, else null. Takes no
/// lease and leaves recency alone.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_peek(
    handle: *mut DelugeResource,
    asset: u32,
    index: u32,
) -> *mut u8 {
    if handle.is_null() {
        return ptr::null_mut();
    }
    mgr(handle).peek(asset, index)
}

/// Marks the `request`ed chunk at `ptr` as loaded, once its read completes. No-op if `ptr` is not
/// resident.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_mark_ready(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).mark_ready(ptr);
    }
}

/// Returns the slot index backing `ptr`, or `DELUGE_RESOURCE_NO_SLOT` if it is not resident.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_slot_of(handle: *mut DelugeResource, ptr: *mut u8) -> u32 {
    if handle.is_null() {
        return NO_SLOT;
    }
    mgr(handle).slot_of(ptr)
}

/// Writes the `(asset, index)` of the resident chunk backing `ptr` to `*out_asset` and
/// `*out_index` and returns true; returns false, leaving them untouched, if `ptr` is not resident
/// or an argument is null.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`. Non-null `out_asset` and
/// `out_index` must be valid for a `u32` write.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_chunk_ident(
    handle: *mut DelugeResource,
    ptr: *mut u8,
    out_asset: *mut u32,
    out_index: *mut u32,
) -> bool {
    if handle.is_null() || out_asset.is_null() || out_index.is_null() {
        return false;
    }
    let Some((asset, index)) = mgr(handle).chunk_ident(ptr) else {
        return false;
    };
    // SAFETY: non-null (checked above) and writable per the caller contract.
    unsafe {
        *out_asset = asset;
        *out_index = index;
    }
    true
}

/// Returns the hard-lease count of the chunk at `slot`; 0 if `slot` is `NO_SLOT`, out of range or
/// free. O(1).
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_lease_count_by_slot(
    handle: *mut DelugeResource,
    slot: u32,
) -> u32 {
    if handle.is_null() {
        return 0;
    }
    mgr(handle).lease_count_by_slot(slot)
}

/// Returns the hard leases on the chunk at `slot` excluding the load queue's own (see
/// [`deluge_resource_loader_enqueue_owned`]): whether anyone besides the queue still wants it.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_external_lease_count_by_slot(
    handle: *mut DelugeResource,
    slot: u32,
) -> u32 {
    if handle.is_null() {
        return 0;
    }
    mgr(handle).external_lease_count_by_slot(slot)
}

// Load queue

/// Enqueues the chunk at `slot` at `priority` (lower = more urgent); a re-enqueue while still
/// queued only raises the priority. The caller must keep its own lease until the load lands.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_enqueue(
    handle: *mut DelugeResource,
    slot: u32,
    priority: u32,
) {
    if !handle.is_null() {
        mgr(handle).loader_enqueue(slot, priority);
    }
}

/// Enqueues the chunk at `slot` and takes a queue-owned hard lease, for a caller that cannot hold
/// its own lease until the load lands. A re-enqueue takes no second lease.
///
/// Every terminal path of the loader must call [`deluge_resource_loader_release_owned`]; a
/// stranded queue lease pins the chunk forever.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_enqueue_owned(
    handle: *mut DelugeResource,
    slot: u32,
    priority: u32,
) {
    if !handle.is_null() {
        mgr(handle).loader_enqueue_owned(slot, priority);
    }
}

/// Releases the queue-owned lease from [`deluge_resource_loader_enqueue_owned`]. Returns whether
/// there was one. Idempotent; does not de-queue.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_release_owned(
    handle: *mut DelugeResource,
    slot: u32,
) -> bool {
    !handle.is_null() && mgr(handle).loader_release_owned(slot)
}

/// Removes the chunk at `slot` from the load queue, dropping any queue-owned lease.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_remove(handle: *mut DelugeResource, slot: u32) {
    if !handle.is_null() {
        mgr(handle).loader_remove(slot);
    }
}

/// Pops the most urgent queued, still-leased chunk and returns its backing, or null. Unleased
/// queued chunks are skipped and left for normal eviction.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_next(handle: *mut DelugeResource) -> *mut u8 {
    if handle.is_null() {
        return ptr::null_mut();
    }
    mgr(handle).loader_next()
}

/// Returns whether any queued, leased chunk sits at the least urgent priority (`u32::MAX`).
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_has_lowest(handle: *mut DelugeResource) -> bool {
    !handle.is_null() && mgr(handle).loader_has_lowest()
}

/// Returns whether [`deluge_resource_loader_next`] would return a chunk, without popping it.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_has_any(handle: *mut DelugeResource) -> bool {
    !handle.is_null() && mgr(handle).loader_has_any()
}

/// Copies the manager's [`Stats`] into `*out`. No-op if either pointer is null.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`. A non-null `out` must be
/// valid for a `Stats` write.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_stats(handle: *mut DelugeResource, out: *mut Stats) {
    if handle.is_null() || out.is_null() {
        return;
    }
    // SAFETY: non-null and writable per the caller contract; `DelugeResourceStats` mirrors `Stats`.
    unsafe { *out = mgr(handle).stats_snapshot() };
}

/// Zeroes the manager's [`Stats`].
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_stats_reset(handle: *mut DelugeResource) {
    if !handle.is_null() {
        mgr(handle).stats_reset();
    }
}

/// Frees the resident chunk at `ptr` without calling `on_evict`, for an owner discarding a chunk
/// itself (e.g. a sample cache truncating its tail). No-op if `ptr` is not resident.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`. The caller must not use
/// `ptr` afterwards.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_evict_chunk(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).evict_chunk(ptr);
    }
}

/// Registers an owner-built heap block of `size` bytes as an unleased chunk the manager may evict.
/// On eviction it calls `on_evict(ctx, ptr)` and then frees `ptr`. Pin it with
/// [`deluge_resource_add_lease`].
///
/// Returns `ptr`, or null if the chunk table is full and nothing is evictable.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`. `ptr` must be a live block
/// allocated from the manager's heap; once adopted, the manager may free it. `on_evict` and `ctx`
/// must stay valid while the block is adopted.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_adopt(
    handle: *mut DelugeResource,
    ptr: *mut u8,
    size: usize,
    cost: u32,
    ctx: *mut c_void,
    on_evict: Option<AdoptEvictFn>,
) -> *mut u8 {
    if handle.is_null() {
        return ptr::null_mut();
    }
    mgr(handle).adopt(ptr, size, cost, ctx, on_evict)
}

/// Adds a hard lease to the resident chunk at `ptr`. No-op if `ptr` is not resident.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_add_lease(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).add_lease(ptr);
    }
}

/// Drops one hard lease on the chunk at `ptr`; it stays cached until evicted.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_release(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).release(ptr);
    }
}

/// Adds a soft reference to an asset: its chunks are evicted after unreferenced ones, but are not
/// pinned.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_reference(handle: *mut DelugeResource, asset: u32) {
    if !handle.is_null() {
        mgr(handle).reference(asset, 1);
    }
}
/// Drops a soft reference taken by [`deluge_resource_reference`].
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_unreference(handle: *mut DelugeResource, asset: u32) {
    if !handle.is_null() {
        mgr(handle).reference(asset, -1);
    }
}

/// Marks the chunk at `ptr` most recently used.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_touch(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).touch(ptr);
    }
}

/// Sets or clears the chunk at `ptr` as dirty; a dirty chunk is never evicted.
///
/// # Safety
///
/// `handle` must be null or a live handle from `deluge_resource_create`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_mark_dirty(
    handle: *mut DelugeResource,
    ptr: *mut u8,
    dirty: bool,
) {
    if !handle.is_null() {
        mgr(handle).set_dirty(ptr, dirty);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    unsafe extern "C" fn mock_construct(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        _index: u32,
        dest: *mut u8,
    ) {
        // SAFETY: `dest` comes from the manager's just-allocated backing.
        unsafe { *dest = 0xC0 };
    }

    /// A manager with a single chunk slot, so a second distinct `request` must evict and reuse
    /// it: a generation bump without depending on eviction order.
    fn test_manager() -> (*mut DelugeResource, std::vec::Vec<u128>) {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf = std::vec![0u128; words];
        // SAFETY: `buf` is a live, 16-aligned, `words * 16`-byte arena for as long as
        // the returned tuple (and thus `buf`) is alive.
        let h =
            unsafe { deluge_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { deluge_resource_create(h, 4, 1) }; // 1 chunk slot
        assert!(!handle.is_null());
        (handle, buf)
    }

    /// Defines an asset with a `construct` callback, so it can be `request`ed.
    fn define_requestable_test_asset(handle: *mut DelugeResource) -> u32 {
        // SAFETY: `handle` is live (from `test_manager`).
        let asset = unsafe {
            deluge_resource_define_asset(
                handle,
                ptr::null_mut(),
                None,
                None,
                ptr::null_mut(),
                1,
                BACKING_HEAP,
            )
        };
        // SAFETY: handle and asset are live and valid for this call.
        unsafe { deluge_resource_set_construct(handle, asset, Some(mock_construct)) };
        asset
    }

    #[test]
    fn generation_bumps_on_slot_reuse_and_gates_stale_retain() {
        let (handle, _buf) = test_manager();
        let asset = define_requestable_test_asset(handle);
        // SAFETY: `handle` is live for the whole test (via `_buf`).
        let m = unsafe { mgr(handle) };

        // Occupy the (only) slot, capture its {slot, generation}.
        let p0 = m.request(asset, 0, 64);
        assert!(!p0.is_null());
        let slot0 = m.slot_of(p0);
        let gen0 = m.generation_of_slot(slot0);
        assert!(gen0 >= 1, "a live slot has a nonzero generation");

        // With one slot, a distinct-index request must evict slot0 and reuse it.
        m.release(p0); // leases -> 0 (evictable)
        let p1 = m.request(asset, 1, 64);
        assert!(!p1.is_null());
        let slot1 = m.slot_of(p1);
        assert_eq!(
            slot1, slot0,
            "single-slot table: reuse must be the same slot"
        );
        let gen1 = m.generation_of_slot(slot1);
        assert!(
            gen1 > gen0,
            "reused slot must have a strictly greater generation"
        );

        // A retain through the stale (slot0, gen0) token is a checked no-op.
        let before = m.lease_count_by_slot(slot1);
        assert!(
            !m.retain_by_slot_gen(slot0, gen0),
            "stale generation must not lease"
        );
        assert_eq!(
            m.lease_count_by_slot(slot1),
            before,
            "no lease taken on stale handle"
        );

        // A retain through the current token succeeds and balances with release.
        assert!(m.retain_by_slot_gen(slot1, gen1));
        assert_eq!(m.lease_count_by_slot(slot1), before + 1);
        assert!(m.release_by_slot_gen(slot1, gen1));
        assert_eq!(m.lease_count_by_slot(slot1), before);
    }
}
