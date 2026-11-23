//! The resource manager: a value-scored, lease-based cache of reconstructable assets on the
//! `deluge_base_alloc` TLSF heap, and the typed functions the C ABI ([`crate::abi`]) forwards to.
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
use deluge_base_alloc::slab::{deluge_slab_acquire, deluge_slab_release, DelugeSlab};
use deluge_base_alloc::{deluge_alloc, deluge_free, deluge_heap_register_reclaim, DelugeHeap};

const NONE: u32 = u32::MAX;
/// Invalid chunk-slot index (the C ABI's `DELUGE_RESOURCE_NO_SLOT`), returned by `slot_of` for a
/// pointer that is not resident.
pub(crate) const NO_SLOT: u32 = u32::MAX;

/// Upper bound on the eviction heap's depth, which bounds every sift: a binary heap of up to
/// 2^32 slots is at most 32 levels deep.
const HEAP_MAX_DEPTH: u32 = 32;

/// Heap entries [`Manager::heap_pick`] can hold on its frontier while it walks past a protected
/// asset's chunks.
const PICK_FRONTIER: usize = 16;

/// Victims [`Manager::evict_lowest`] tries before giving up; another only after a pick fails to
/// re-validate.
const EVICT_ATTEMPTS: usize = 8;

/// The outcome of [`Manager::heap_pick`].
#[derive(Clone, Copy)]
enum Pick {
    /// The slot to evict.
    Found(usize),
    /// Nothing outside the protected asset is evictable.
    Empty,
    /// The frontier filled; see [`Manager::heap_pick_linear`].
    Overflow,
}

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
/// Chunks are uniform slots of the slab set by `resource_set_slab`; heap if none is set.
pub const BACKING_SLAB: u32 = 1;

/// How an asset's chunks are rebuilt, evicted and backed.
#[derive(Clone, Copy)]
pub struct Source {
    pub materialize: Option<MaterializeFn>,
    pub on_evict: Option<EvictFn>,
    /// Set by `resource_set_construct`; `None` means the asset cannot be `request`ed, only
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
    /// Chunks of this asset in the `(asset, index)` index, kept by [`Manager::index_insert`] and
    /// [`Manager::index_remove`], so a release finds them all without scanning the table.
    resident: u32,
    /// One past the highest index any chunk of this asset has been indexed at.
    index_end: u32,
    /// For an `evict_tail_first` asset, the highest index of its resident chunks, the only one
    /// that may be evicted; `NONE` when none is resident or the asset is not tail-first. Kept by
    /// [`Manager::index_insert`] and [`Manager::index_remove`].
    tail: u32,
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
        resident: 0,
        index_end: 0,
        tail: NONE,
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
    /// Reconstruction cost for the eviction rank: an adopted chunk's own, otherwise a copy of its
    /// asset's `source.cost`, refreshed by [`Manager::heap_sync`] so the heap compares slots
    /// without reading the asset table.
    cost: u32,
    /// The asset's `soft_refs > 0`, cached for the eviction rank like `cost`. False for adopted
    /// chunks.
    soft_ref: bool,
    // Adopted chunks (asset == NONE) carry their own evict callback; unused otherwise.
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
    /// Position in [`Manager::evict_heap`], or `NONE` while the chunk is not evictable.
    heap_pos: u32,
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
        soft_ref: false,
        adopt_evict: None,
        adopt_ctx: ptr::null_mut(),
        hash_next: NONE,
        ptr_next: NONE,
        queued_at: 0,
        heap_pos: NONE,
    };

    /// Queues this chunk at `priority` (lower = more urgent). While it is already queued the
    /// priority only rises, so a less urgent requester can't push back a chunk someone more urgent
    /// waits on. Once `loader_next` has popped it, the priority is set afresh.
    ///
    /// The queue wait is timed from when the chunk entered the queue, not from its latest request.
    fn queue_at(&mut self, priority: u32) {
        if self.queued {
            self.queue_priority = self.queue_priority.min(priority);
        } else {
            self.queue_priority = priority;
            // SAFETY: a plain counter read; no preconditions.
            self.queued_at = unsafe { deluge_debug_now_frames() };
        }
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
/// Mirrors `DelugeResourceStats` in `deluge_storage_resource.h`; read with `resource_stats`, zeroed
/// with `resource_stats_reset`.
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
    // masked read of a slot in SDRAM, so visits are the cost driver. For evictions, the slots
    // visited are eviction-heap steps: the pick, its sift out of the heap, and any tail walk.
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
    /// `queue_wait` and `fill` as log2 histograms of frames: bucket 0 holds 0, bucket `i` holds
    /// `[2^(i-1), 2^i)`, and the last also holds everything longer.
    pub queue_wait_buckets: [u32; LATENCY_BUCKETS],
    pub fill_buckets: [u32; LATENCY_BUCKETS],
}

/// Buckets in each of [`Stats`]' latency histograms.
pub const LATENCY_BUCKETS: usize = 16;

/// Adds one latency of `frames` to `buckets` (see [`Stats::queue_wait_buckets`]).
fn add_latency(buckets: &mut [u32; LATENCY_BUCKETS], frames: u64) {
    let bucket = ((u64::BITS - frames.leading_zeros()) as usize).min(LATENCY_BUCKETS - 1);
    buckets[bucket] = buckets[bucket].saturating_add(1);
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
    /// One bit per chunk slot, set in the same masked window that queues the slot, so the loader
    /// visits only queued slots instead of the whole table. A hint: a slot de-queued, freed or
    /// evicted keeps its bit until a scan finds it stale and clears it (see
    /// [`Manager::for_each_queued`]).
    queued_bits: &'static [Cell<u32>],
    /// Binary min-heap of the evictable chunks' slot indices, ordered by [`Manager::evicts_before`];
    /// `evict_heap[..heap_len]` is live and each member's `ChunkSlot::heap_pos` points back at its entry.
    ///
    /// A slot is a member exactly when [`Manager::is_evictable`] holds. Every write that can change
    /// that, or the slot's rank, calls [`Manager::heap_sync`] in the same masked window, so a
    /// preempting eviction always finds the heap and the table in agreement and its root is the
    /// chunk a full scan would pick.
    evict_heap: &'static [Cell<u32>],
    heap_len: Cell<u32>,
    /// Where the next free-slot search starts: just past the slot last handed out. Occupied slots
    /// pile up at the start of the table, so a search from slot 0 walked past all of them.
    free_hint: Cell<u32>,
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
    ///
    /// A chunk above a tail-first asset's tail becomes the tail, and the old tail leaves the
    /// eviction heap; the caller syncs slot `i` itself.
    fn index_insert(&self, i: usize) {
        let mut s = self.chunks[i].get();
        if s.asset == NONE {
            return;
        }
        let b = self.bucket_of(s.asset, s.index);
        s.hash_next = self.buckets[b].get();
        self.chunks[i].set(s);
        self.buckets[b].set(i as u32);
        if let Some(cell) = self.assets.get(s.asset as usize) {
            let mut a = cell.get();
            a.resident = a.resident.saturating_add(1);
            a.index_end = a.index_end.max(s.index.saturating_add(1));
            let old_tail = a.tail;
            let new_tail = a.evict_tail_first && (old_tail == NONE || s.index > old_tail);
            if new_tail {
                a.tail = s.index;
            }
            cell.set(a);
            // The old tail stops being a candidate; the new slot is synced by the caller.
            if new_tail && old_tail != NONE {
                if let Some(t) = self.find_indexed_unmasked(s.asset, old_tail) {
                    self.heap_sync(t);
                }
            }
        }
    }

    /// Counts one chunk of `asset` out of the index; see [`AssetSlot::resident`].
    fn note_unindexed(&self, asset: u32) {
        if let Some(cell) = self.assets.get(asset as usize) {
            let mut a = cell.get();
            a.resident = a.resident.saturating_sub(1);
            cell.set(a);
        }
    }

    /// Unlinks slot `i` from its bucket. Same masking contract as [`Manager::index_insert`]; must
    /// run before the slot is cleared, since it walks `hash_next`. Takes the key explicitly because
    /// the caller is overwriting the slot.
    ///
    /// When `i` is a tail-first asset's tail, the next-highest resident chunk becomes the tail and
    /// joins the eviction heap. Returns the heap steps taken.
    fn index_remove(&self, i: usize, asset: u32, index: u32) -> u32 {
        if asset == NONE {
            return 0; // never indexed — see index_insert
        }
        self.index_unlink(i, asset, index);
        let Some(cell) = self.assets.get(asset as usize) else {
            return 0;
        };
        let mut a = cell.get();
        if !a.evict_tail_first || a.tail != index {
            return 0;
        }
        // From `index` itself: a duplicate chunk at the same index (see `find_resident`) stays
        // the tail, as it would for a scan.
        a.tail = if a.resident == 0 {
            NONE
        } else {
            self.highest_indexed_below(asset, index.saturating_add(1))
        };
        cell.set(a);
        match self.find_indexed_unmasked(asset, a.tail) {
            Some(t) if a.tail != NONE => self.heap_sync(t),
            _ => 0,
        }
    }

    /// The bucket-chain half of [`Manager::index_remove`].
    fn index_unlink(&self, i: usize, asset: u32, index: u32) {
        let b = self.bucket_of(asset, index);
        let head = self.buckets[b].get();
        if head == i as u32 {
            self.buckets[b].set(self.chunks[i].get().hash_next);
            self.note_unindexed(asset);
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
                self.note_unindexed(asset);
                return;
            }
            cur = s.hash_next;
        }
    }

    /// [`Manager::find_resident`]'s indexed walk, unmasked and uncounted: for
    /// [`Manager::release_asset`], which already runs unmasked at its owner's teardown.
    fn find_indexed_unmasked(&self, asset: u32, index: u32) -> Option<usize> {
        let mut cur = self.buckets[self.bucket_of(asset, index)].get();
        let mut steps = 0;
        while cur != NONE && steps <= self.chunks.len() {
            let i = cur as usize;
            if i >= self.chunks.len() {
                return None; // corrupt link; see find_resident's walk
            }
            steps += 1;
            let s = self.chunks[i].get();
            if !s.backing.is_null() && s.asset == asset && s.index == index {
                return Some(i);
            }
            cur = s.hash_next;
        }
        None
    }

    /// Brute-force lookup: the ground truth the index tests compare `find_resident` against.
    #[cfg(test)]
    pub(crate) fn find_resident_linear(&self, asset: u32, index: u32) -> Option<usize> {
        (0..self.chunks.len()).find(|&i| {
            let s = m_get(&self.chunks[i]);
            !s.backing.is_null() && s.asset == asset && s.index == index
        })
    }

    /// The chunk a whole-table scan would evict with `protect` protected: the lowest
    /// [`evict_rank`], first slot on a tie, among unleased clean chunks not of `protect`, taking
    /// only the highest resident index of a tail-first asset. The ground truth the tests compare
    /// the eviction heap against.
    #[cfg(test)]
    pub(crate) fn evict_candidate_linear(&self, protect: u32) -> Option<usize> {
        let mut best: Option<usize> = None;
        let mut best_rank = (u8::MAX, u64::MAX, u64::MAX);
        for i in 0..self.chunks.len() {
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
                if a.evict_tail_first && !self.is_highest_resident(s.asset, s.index) {
                    continue;
                }
                (a.source.cost, a.soft_refs)
            } else {
                continue;
            };
            let rank = evict_rank(soft_refs, cost, s.size, s.recency);
            if rank < best_rank {
                best_rank = rank;
                best = Some(i);
            }
        }
        best
    }

    /// The chunk [`Manager::evict_lowest`] would evict with `protect` protected, and whether
    /// finding it overflowed the frontier.
    #[cfg(test)]
    pub(crate) fn evict_candidate_indexed(&self, protect: u32) -> (Option<usize>, bool) {
        let mut visited = 0;
        let pick = {
            let _m = Masked::enter();
            self.heap_pick(protect, &mut visited)
        };
        match pick {
            Pick::Found(i) => (Some(i), false),
            Pick::Empty => (None, false),
            Pick::Overflow => (self.heap_pick_linear(protect, &mut visited), true),
        }
    }

    /// Asserts the eviction heap's invariants: every entry points back at its position, every
    /// parent is evicted before its children, a slot is in the heap exactly when it is evictable
    /// (with the tail-first rule checked by a table scan), and members' cached rank inputs match
    /// their asset.
    #[cfg(test)]
    pub(crate) fn assert_evict_heap_consistent(&self) {
        let len = self.heap_len.get() as usize;
        assert!(len <= self.evict_heap.len(), "heap_len {len} past capacity");
        for pos in 0..len {
            let slot = self.evict_heap[pos].get() as usize;
            assert!(
                slot < self.chunks.len(),
                "heap[{pos}] = {slot} out of range"
            );
            assert_eq!(
                self.chunks[slot].get().heap_pos as usize,
                pos,
                "slot {slot} does not point back at heap[{pos}]"
            );
            if pos > 0 {
                let parent = self.evict_heap[(pos - 1) / 2].get() as usize;
                assert!(
                    self.evicts_before(parent, slot),
                    "heap order: parent slot {parent} ranks after child slot {slot}"
                );
            }
        }
        for (i, cell) in self.chunks.iter().enumerate() {
            let s = cell.get();
            let tail_ok = s.asset == NONE
                || !self.assets[s.asset as usize].get().evict_tail_first
                || self.is_highest_resident(s.asset, s.index);
            let evictable = !s.backing.is_null() && s.leases == 0 && !s.dirty && tail_ok;
            assert_eq!(
                s.heap_pos != NONE,
                evictable,
                "slot {i}: heap membership disagrees with the slot (asset {}, index {}, leases {}, \
                 dirty {})",
                s.asset,
                s.index,
                s.leases,
                s.dirty
            );
            if evictable && s.asset != NONE {
                let a = self.assets[s.asset as usize].get();
                assert_eq!(s.cost, a.source.cost, "slot {i}: stale cached cost");
                assert_eq!(
                    s.soft_ref,
                    a.soft_refs > 0,
                    "slot {i}: stale cached soft_ref"
                );
            }
        }
        for (ai, cell) in self.assets.iter().enumerate() {
            let a = cell.get();
            if !a.evict_tail_first {
                continue;
            }
            let highest = self
                .chunks
                .iter()
                .map(Cell::get)
                .filter(|s| !s.backing.is_null() && s.asset == ai as u32)
                .map(|s| s.index)
                .max()
                .unwrap_or(NONE);
            assert_eq!(a.tail, highest, "asset {ai}: stale tail");
        }
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
        let n = self.chunks.len();
        let start = m_get(&self.free_hint) as usize % n;
        let mut visited = 0u64;
        let found = (0..n).map(|k| (start + k) % n).find(|&i| {
            visited += 1;
            m_get(&self.chunks[i]).backing.is_null()
        });
        if let Some(i) = found {
            m_set(&self.free_hint, ((i + 1) % n) as u32);
        }
        self.stat(|s| {
            s.scan_free_calls += 1;
            s.scan_free_slots += visited;
        });
        found
    }

    /// `preferred` if it is still free, otherwise another free slot; for the masked window that
    /// fills a slot [`Manager::find_free_chunk`] found. Another context can take the found slot
    /// between the search and that window, while the backing is being allocated.
    ///
    /// Call with the mask held.
    fn still_free_or_another(&self, preferred: usize) -> Option<usize> {
        if self.chunks[preferred].get().backing.is_null() {
            return Some(preferred);
        }
        let n = self.chunks.len();
        let start = self.free_hint.get() as usize % n;
        (0..n)
            .map(|k| (start + k) % n)
            .find(|&i| self.chunks[i].get().backing.is_null())
    }

    /// Finds the slot backing `p`, then in one masked window re-validates `backing == p` (the
    /// slot may have been reused since the lookup), applies `f` and resyncs the slot's
    /// eviction-heap entry. Returns whether it mutated.
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
        self.heap_sync(i);
        true
    }

    /// Masked RMW on slot `i`, followed in the same window by [`Manager::heap_sync`].
    /// Re-validating the slot (it may have emptied since the lookup) is the closure's job.
    fn rmw_by_ptr_slot<R>(&self, i: usize, f: impl FnOnce(&mut ChunkSlot) -> R) -> R {
        let _m = Masked::enter();
        let mut s = self.chunks[i].get();
        let r = f(&mut s);
        self.chunks[i].set(s);
        self.heap_sync(i);
        r
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
        let _m = Masked::enter();
        let mut s = self.chunks[i].get();
        if s.backing.is_null() {
            return;
        }
        s.queue_at(priority);
        self.chunks[i].set(s);
        self.mark_queued(i);
    }

    /// Enqueues the chunk at `slot` and takes a queue-owned hard lease, so the entry survives the
    /// enqueuer dropping its own (see `ChunkSlot::queue_leased`).
    ///
    /// A re-enqueue raises the priority as `loader_enqueue` does, without a second lease. Every
    /// terminal path of the drain must call `loader_release_owned` (or `loader_remove`); a stranded
    /// queue lease pins the chunk forever. No-op if `slot` is out of range or free.
    pub(crate) fn loader_enqueue_owned(&self, slot: u32, priority: u32) {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return;
        }
        let _m = Masked::enter();
        let mut s = self.chunks[i].get();
        if s.backing.is_null() {
            return;
        }
        if !s.queue_leased {
            s.leases += 1;
            s.queue_leased = true;
        }
        s.queue_at(priority);
        self.chunks[i].set(s);
        self.heap_sync(i);
        self.mark_queued(i);
    }

    /// Sets slot `i`'s bit in `queued_bits`. Call inside the masked window that queues the slot,
    /// so a scan can never see the slot queued without its bit, nor clear the bit of a slot being
    /// queued.
    fn mark_queued(&self, i: usize) {
        let word = &self.queued_bits[i / 32];
        word.set(word.get() | 1 << (i % 32));
    }

    /// Calls `f` with each queued, resident slot and its state, until `f` returns `false`.
    ///
    /// Visits only the slots `queued_bits` marks. A marked slot that is no longer queued or resident
    /// has its bit cleared, in a masked window that re-reads the slot, so a slot queued meanwhile
    /// (which sets its bit in its own masked window) keeps it. Each slot is read in its own window,
    /// as the old whole-table scan read it; the caller re-validates whatever it acts on.
    fn for_each_queued(&self, mut f: impl FnMut(usize, ChunkSlot) -> bool) {
        for (w, word) in self.queued_bits.iter().enumerate() {
            let mut bits = m_get(word);
            while bits != 0 {
                let b = bits.trailing_zeros() as usize;
                bits &= bits - 1;
                let i = w * 32 + b;
                let state = {
                    let _m = Masked::enter();
                    let s = self.chunks[i].get();
                    if s.queued && !s.backing.is_null() {
                        Some(s)
                    } else {
                        word.set(word.get() & !(1 << b));
                        None
                    }
                };
                if let Some(s) = state {
                    if !f(i, s) {
                        return;
                    }
                }
            }
        }
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
        self.rmw_by_ptr_slot(i, |s| {
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
    /// `loader_release_owned` on every terminal path. Visits only queued slots.
    pub(crate) fn loader_next(&self) -> *mut u8 {
        // The pick is re-validated under the mask below.
        let mut best: Option<usize> = None;
        let mut best_pri = u32::MAX;
        let mut depth = 0u64; // queued+leased candidates this poll (Stats::loader_depth_*)
        self.for_each_queued(|i, s| {
            if s.leases > 0 {
                depth += 1;
                if best.is_none() || s.queue_priority < best_pri {
                    best = Some(i);
                    best_pri = s.queue_priority;
                }
            }
            true
        });
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
                add_latency(&mut st.queue_wait_buckets, waited);
            });
        }
        s.backing
    }

    /// Returns whether any queued, leased chunk sits at the least urgent priority (`u32::MAX`);
    /// `load_song_ui`'s "background load work remains" check.
    fn loader_has_lowest(&self) -> bool {
        let mut found = false;
        self.for_each_queued(|_, s| {
            found = s.leases > 0 && s.queue_priority == u32::MAX;
            !found
        });
        found
    }

    /// Returns whether `loader_next` would return a chunk, without popping anything. Backs
    /// `deluge_streaming_queue_drained`.
    fn loader_has_any(&self) -> bool {
        let mut found = false;
        self.for_each_queued(|_, s| {
            found = s.leases > 0;
            !found
        });
        found
    }

    /// Returns whether no resident chunk of `asset` has an index above `index`, by scanning the
    /// table: the ground truth for `AssetSlot::tail` in the tests.
    #[cfg(test)]
    fn is_highest_resident(&self, asset: u32, index: u32) -> bool {
        !(0..self.chunks.len()).any(|i| {
            let s = m_get(&self.chunks[i]);
            !s.backing.is_null() && s.asset == asset && s.index > index
        })
    }

    // The eviction heap (see `Manager::evict_heap`). Every function here expects the mask held, and
    // reads slots and heap entries with plain `get`.

    /// Whether `s` may be evicted now: occupied, unleased and clean, adopted or of an in-range
    /// asset, and, for an `evict_tail_first` asset, its highest resident index (`AssetSlot::tail`).
    fn is_evictable(&self, s: &ChunkSlot) -> bool {
        if s.backing.is_null() || s.leases != 0 || s.dirty {
            return false;
        }
        if s.asset == NONE {
            return true;
        }
        match self.assets.get(s.asset as usize) {
            Some(cell) => {
                let a = cell.get();
                !a.evict_tail_first || a.tail == s.index
            }
            None => false,
        }
    }

    /// Whether slot `i` is evicted before slot `j`: the lower [`evict_rank`], then the lower slot
    /// index, which is the choice a scan from slot 0 keeping the first minimum makes.
    fn evicts_before(&self, i: usize, j: usize) -> bool {
        let rank = |s: ChunkSlot| evict_rank(s.soft_ref as u32, s.cost, s.size, s.recency);
        (rank(self.chunks[i].get()), i) < (rank(self.chunks[j].get()), j)
    }

    /// The slot at heap position `pos`, or `None` past the end or for a corrupt entry.
    fn heap_entry(&self, pos: usize) -> Option<usize> {
        if pos >= self.heap_len.get() as usize {
            return None;
        }
        let slot = self.evict_heap.get(pos)?.get() as usize;
        (slot < self.chunks.len()).then_some(slot)
    }

    /// Stores `slot` at heap position `pos` and points the slot back at it.
    fn heap_place(&self, pos: usize, slot: usize) {
        self.evict_heap[pos].set(slot as u32);
        let mut s = self.chunks[slot].get();
        s.heap_pos = pos as u32;
        self.chunks[slot].set(s);
    }

    /// Moves the entry at `pos` up past every parent it is evicted before. Returns the levels
    /// moved; at most the heap depth, so a corrupt heap cannot loop.
    fn heap_sift_up(&self, mut pos: usize) -> u32 {
        let Some(slot) = self.heap_entry(pos) else {
            return 0;
        };
        let mut steps = 0;
        while pos > 0 && steps < HEAP_MAX_DEPTH {
            let parent = (pos - 1) / 2;
            let Some(p) = self.heap_entry(parent) else {
                break;
            };
            if !self.evicts_before(slot, p) {
                break;
            }
            self.heap_place(pos, p);
            pos = parent;
            steps += 1;
        }
        self.heap_place(pos, slot);
        steps
    }

    /// Moves the entry at `pos` down below every child evicted before it. Returns the levels
    /// moved, bounded like [`Manager::heap_sift_up`].
    fn heap_sift_down(&self, mut pos: usize) -> u32 {
        let Some(slot) = self.heap_entry(pos) else {
            return 0;
        };
        let mut steps = 0;
        while steps < HEAP_MAX_DEPTH {
            let Some(mut child) = self.heap_entry(2 * pos + 1) else {
                break;
            };
            let mut child_pos = 2 * pos + 1;
            if let Some(right) = self.heap_entry(2 * pos + 2) {
                if self.evicts_before(right, child) {
                    child = right;
                    child_pos = 2 * pos + 2;
                }
            }
            if !self.evicts_before(child, slot) {
                break;
            }
            self.heap_place(pos, child);
            pos = child_pos;
            steps += 1;
        }
        self.heap_place(pos, slot);
        steps
    }

    /// Restores heap order around `pos` after its entry's rank changed. Returns the levels moved.
    fn heap_fix(&self, pos: usize) -> u32 {
        match self.heap_sift_up(pos) {
            0 => self.heap_sift_down(pos),
            up => up,
        }
    }

    /// The heap position slot `i` claims, if the heap agrees.
    fn heap_pos_of(&self, i: usize) -> Option<usize> {
        let pos = self.chunks[i].get().heap_pos as usize;
        (self.heap_entry(pos) == Some(i)).then_some(pos)
    }

    /// Removes slot `i` from the heap, if it is there. Returns the heap steps taken.
    fn heap_remove(&self, i: usize) -> u32 {
        let mut steps = 0;
        if let Some(pos) = self.heap_pos_of(i) {
            let last = self.heap_len.get() as usize - 1;
            let moved = self.heap_entry(last);
            self.heap_len.set(last as u32);
            if let Some(moved) = moved.filter(|_| pos != last) {
                self.heap_place(pos, moved);
                steps = 1 + self.heap_fix(pos);
            }
        }
        let mut s = self.chunks[i].get();
        s.heap_pos = NONE;
        self.chunks[i].set(s);
        steps
    }

    /// Brings slot `i`'s heap membership and position in line with its state: inserts it once it
    /// becomes evictable, removes it once it stops, and re-sifts it when its rank may have changed.
    /// Refreshes the slot's cached `cost` and `soft_ref` from its asset first. Returns the heap
    /// steps taken.
    ///
    /// Call in the masked window of every write that can change [`Manager::is_evictable`] or the
    /// rank: `backing`, `leases`, `dirty`, `recency`, the asset's `soft_refs`, `evict_tail_first`
    /// and `tail`. The window then holds at most a sift: the heap depth, about 13 levels.
    fn heap_sync(&self, i: usize) -> u32 {
        let Some(cell) = self.chunks.get(i) else {
            return 0;
        };
        let mut s = cell.get();
        if !self.is_evictable(&s) {
            return if s.heap_pos == NONE {
                0
            } else {
                self.heap_remove(i)
            };
        }
        if s.asset != NONE {
            let a = self.assets[s.asset as usize].get();
            s.cost = a.source.cost;
            s.soft_ref = a.soft_refs > 0;
            cell.set(s);
        }
        if let Some(pos) = self.heap_pos_of(i) {
            return self.heap_fix(pos);
        }
        let len = self.heap_len.get() as usize;
        if len >= self.evict_heap.len() {
            return 0; // only reachable through a corrupt heap; the chunk just stays resident
        }
        self.heap_len.set(len as u32 + 1);
        self.heap_place(len, i);
        1 + self.heap_sift_up(len)
    }

    /// The eviction candidate: the heap root, or while `protect` names an asset, the lowest-ranked
    /// member of any other asset.
    ///
    /// With an asset protected, a best-first walk down from the root expands only that asset's
    /// entries: every unexpanded entry lies under the frontier, so the first frontier minimum not
    /// of `protect` is the answer. The frontier is a fixed array; `Pick::Overflow` means it filled
    /// before the walk ended and the caller must fall back to [`Manager::heap_pick_linear`].
    fn heap_pick(&self, protect: u32, visited: &mut u64) -> Pick {
        let Some(root) = self.heap_entry(0) else {
            return Pick::Empty;
        };
        *visited += 1;
        if protect == NONE || self.chunks[root].get().asset != protect {
            return Pick::Found(root);
        }
        let mut frontier = [0u32; PICK_FRONTIER];
        let mut n = 0;
        let expand = |pos: usize, frontier: &mut [u32; PICK_FRONTIER], n: &mut usize| {
            for child in [2 * pos + 1, 2 * pos + 2] {
                if self.heap_entry(child).is_some() {
                    if *n == PICK_FRONTIER {
                        return false;
                    }
                    frontier[*n] = child as u32;
                    *n += 1;
                }
            }
            true
        };
        if !expand(0, &mut frontier, &mut n) {
            return Pick::Overflow;
        }
        // Each round expands one heap entry, so the heap size bounds the walk.
        for _ in 0..self.heap_len.get() {
            if n == 0 {
                return Pick::Empty; // every member belongs to the protected asset
            }
            let mut best = 0;
            for k in 1..n {
                if let (Some(a), Some(b)) = (
                    self.heap_entry(frontier[k] as usize),
                    self.heap_entry(frontier[best] as usize),
                ) {
                    if self.evicts_before(a, b) {
                        best = k;
                    }
                }
            }
            let pos = frontier[best] as usize;
            *visited += 1;
            let Some(slot) = self.heap_entry(pos) else {
                return Pick::Empty;
            };
            if self.chunks[slot].get().asset != protect {
                return Pick::Found(slot);
            }
            n -= 1;
            frontier[best] = frontier[n];
            if !expand(pos, &mut frontier, &mut n) {
                return Pick::Overflow;
            }
        }
        Pick::Empty
    }

    /// [`Manager::heap_pick`]'s answer by a pass over every heap member, for when its frontier
    /// overflows: more than `PICK_FRONTIER` of the protected asset's chunks rank first. Each entry
    /// is read in its own masked window, so the caller re-validates the pick.
    fn heap_pick_linear(&self, protect: u32, visited: &mut u64) -> Option<usize> {
        let mut best: Option<usize> = None;
        for pos in 0..self.evict_heap.len() {
            let _m = Masked::enter();
            let Some(slot) = self.heap_entry(pos) else {
                break;
            };
            *visited += 1;
            if self.chunks[slot].get().asset == protect {
                continue;
            }
            if best.is_none_or(|b| self.evicts_before(slot, b)) {
                best = Some(slot);
            }
        }
        best
    }

    /// Re-runs [`Manager::heap_sync`] on every resident chunk of `asset`, after a change to the
    /// asset that moves their membership or rank. Walks the asset's indices downward through the
    /// `(asset, index)` index, stopping once it has seen as many chunks as the asset has resident.
    ///
    /// Call with the mask held; the window grows with the asset's `index_end`, so this is for
    /// changes made at asset setup.
    fn heap_sync_asset(&self, asset: u32) {
        let a = self.assets[asset as usize].get();
        let mut seen = 0;
        let mut index = a.index_end;
        while index > 0 && seen < a.resident {
            index -= 1;
            if let Some(i) = self.find_indexed_unmasked(asset, index) {
                self.heap_sync(i);
                seen += 1;
            }
        }
    }

    /// The highest index below `below` at which `asset` has a resident chunk, or `NONE`. Walks the
    /// `(asset, index)` index downward, so it is bounded by `below` and stops at the first hit.
    fn highest_indexed_below(&self, asset: u32, below: u32) -> u32 {
        let mut index = below;
        while index > 0 {
            index -= 1;
            if self.find_indexed_unmasked(asset, index).is_some() {
                return index;
            }
        }
        NONE
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

    /// Evicts the lowest-ranked evictable chunk: the [`Manager::evict_heap`] root, or, while an asset is
    /// protected, the lowest-ranked chunk of any other asset. Returns false if nothing is
    /// evictable. Serves as the heap reclaim hook and frees table slots.
    fn evict_lowest(&self) -> bool {
        let protect = m_get(&self.protect);
        // Heap steps, counted as the slots the old whole-table scan visited.
        let mut visited = 0u64;
        let mut result = false;
        // The common path picks and unlinks the victim in one masked window, so nothing can
        // intervene. Only the overflow fallback, or a heap entry that fails re-validation (which
        // `take_victim` resyncs out of the heap), comes round again.
        for _ in 0..EVICT_ATTEMPTS {
            let (pick, taken) = {
                let _m = Masked::enter();
                let pick = self.heap_pick(protect, &mut visited);
                let taken = match pick {
                    Pick::Found(i) => self.take_victim(i, &mut visited),
                    _ => None,
                };
                (pick, taken)
            };
            let taken = match (pick, taken) {
                (Pick::Empty, _) => break,
                (_, Some(s)) => Some(s),
                (Pick::Overflow, None) => match self.heap_pick_linear(protect, &mut visited) {
                    Some(i) => {
                        let _m = Masked::enter();
                        self.take_victim(i, &mut visited)
                    }
                    None => break,
                },
                (Pick::Found(_), None) => None,
            };
            if let Some(s) = taken {
                self.finish_eviction(s);
                result = true;
                break;
            }
        }
        self.stat(|s| {
            s.scan_evict_calls += 1;
            s.scan_evict_slots += visited;
        });
        result
    }

    /// Re-validates heap member `i` as evictable and clears it from the table and every index,
    /// returning its old state; or resyncs it (a stale entry leaves the heap) and returns `None`.
    ///
    /// Call with the mask held. The table is consistent before any callback runs, since
    /// [`Manager::finish_eviction`] runs them unmasked afterwards.
    fn take_victim(&self, i: usize, visited: &mut u64) -> Option<ChunkSlot> {
        let s = self.chunks.get(i)?.get();
        if s.heap_pos == NONE || !self.is_evictable(&s) {
            *visited += self.heap_sync(i) as u64;
            return None;
        }
        *visited += self.clear_slot(i) as u64;
        Some(s)
    }

    /// The unmasked half of an eviction: counts it, tells the owner, and frees the backing of the
    /// chunk [`Manager::take_victim`] cleared. Never hold the mask across a callback or a free.
    fn finish_eviction(&self, s: ChunkSlot) {
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
    }

    /// Unlinks occupied slot `i` from the key, pointer and eviction indexes and clears it.
    /// Returns the heap steps taken.
    ///
    /// Call with the mask held: the unlinks walk the slot's chain links, which the clear wipes.
    fn clear_slot(&self, i: usize) -> u32 {
        let s = self.chunks[i].get();
        let mut steps = self.index_remove(i, s.asset, s.index);
        self.ptr_index_remove(i, s.backing);
        steps += self.heap_remove(i);
        self.chunks[i].set(ChunkSlot::EMPTY);
        steps
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
        let idx = {
            let recency = self.bump();
            let generation = self.next_gen();
            let _m = Masked::enter();
            let Some(idx) = self.still_free_or_another(idx) else {
                drop(_m);
                self.free_backing(p, asset);
                return ptr::null_mut(); // every slot taken under preemption
            };
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
            self.heap_sync(idx);
            idx
        };
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
                self.clear_slot(idx);
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
            let Some(idx) = self.still_free_or_another(idx) else {
                drop(_m);
                self.free_backing(p, asset);
                return ptr::null_mut(); // every slot taken under preemption
            };
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
            self.heap_sync(idx);
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
        // One window over the flag, the tail and the asset's heap entries, which all change
        // together; the window walks the asset's indices, so set this before it has chunks.
        let _m = Masked::enter();
        let mut a = self.assets[ai].get();
        a.evict_tail_first = on;
        a.tail = if on && a.resident > 0 {
            self.highest_indexed_below(asset, a.index_end)
        } else {
            NONE
        };
        self.assets[ai].set(a);
        self.heap_sync_asset(asset);
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
            self.clear_slot(c);
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
            self.heap_sync(idx);
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
                add_latency(&mut st.fill_buckets, f as u64);
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
        self.rmw_by_ptr_slot(c, |s| {
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
            // Unlink, then clear the slot before the callback so a reentrant call sees a
            // consistent table. The asset slot is cleared last, since `free_backing` reads its
            // backing kind. Masked, as the eviction heap is shared with every other asset.
            let s = {
                let _m = Masked::enter();
                let s = self.chunks[i].get();
                self.clear_slot(i);
                s
            };
            if let Some(cb) = a.source.on_evict {
                // SAFETY: the callback, ctx and owner come from this asset.
                unsafe { cb(a.source.ctx, a.owner, s.index) };
            }
            self.free_backing(s.backing, asset);
        };
        // Highest index first, so a tail-first asset's `on_evict` never discards a chunk still
        // tracked here; any order would do for the rest. Each index is looked up in the
        // `(asset, index)` index rather than the table scanned for it, and the walk stops once
        // nothing of the asset is left: a whole-table scan per chunk took ~40 ms per sample freed.
        let mut index = a.index_end;
        while index > 0 && self.assets[ai].get().resident > 0 {
            index -= 1;
            if let Some(i) = self.find_indexed_unmasked(asset, index) {
                drop_chunk(i);
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
        let flipped = m_rmw(&self.assets[ai], |a| {
            let was = a.soft_refs > 0;
            if delta > 0 {
                a.soft_refs += delta as u32;
            } else {
                a.soft_refs = a.soft_refs.saturating_sub((-delta) as u32);
            }
            (a.soft_refs > 0) != was
        });
        if flipped {
            self.heap_resync_asset_windowed(asset);
        }
    }

    /// [`Manager::heap_sync_asset`] for an asset whose `soft_refs` just crossed zero, which
    /// changes the rank of every chunk of it: one masked window per index probed, since a
    /// sample's stream asset can span thousands of indices and this runs at song load.
    ///
    /// Between windows the heap is ordered and every member evictable, but chunks not yet
    /// reached keep their old `soft_ref` for the rank, so an eviction that preempts the walk may
    /// pick by the old reference state. A chunk written meanwhile is synced by its own write,
    /// from the new `soft_refs`.
    fn heap_resync_asset_windowed(&self, asset: u32) {
        let a = m_get(&self.assets[asset as usize]);
        let mut seen = 0;
        let mut index = a.index_end;
        while index > 0 && seen < a.resident {
            index -= 1;
            let _m = Masked::enter();
            if let Some(i) = self.find_indexed_unmasked(asset, index) {
                self.heap_sync(i);
                seen += 1;
            }
        }
    }
}

extern "C" fn resource_reclaim(ctx: *mut c_void, _bytes_needed: usize) -> bool {
    // SAFETY: `ctx` is the manager registered at create, which outlives the heap. A second
    // `&Manager` is sound: no `&mut` ever exists and mutation goes through `Cell`.
    let m = unsafe { &*(ctx as *mut Manager) };
    m.evict_lowest()
}

// The typed functions behind the C ABI (`deluge_storage_resource.h`, [`crate::abi`])

/// Opaque manager handle (`DelugeResource` in `deluge_storage_resource.h`).
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
/// `h` must be a non-null live handle from `resource_create`.
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
pub unsafe fn resource_create(
    heap: *mut DelugeHeap,
    asset_cap: usize,
    chunk_cap: usize,
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
    let queued_words = chunk_cap.div_ceil(32).max(1);
    let queued_bits_raw = deluge_alloc(
        heap,
        queued_words * core::mem::size_of::<Cell<u32>>(),
        core::mem::align_of::<Cell<u32>>().max(16),
    ) as *mut Cell<u32>;
    let heap_raw = deluge_alloc(
        heap,
        chunk_cap * core::mem::size_of::<Cell<u32>>(),
        core::mem::align_of::<Cell<u32>>().max(16),
    ) as *mut Cell<u32>;
    if m.is_null()
        || assets_raw.is_null()
        || chunks_raw.is_null()
        || buckets_raw.is_null()
        || ptr_buckets_raw.is_null()
        || queued_bits_raw.is_null()
        || heap_raw.is_null()
    {
        deluge_free(heap, m as *mut u8);
        deluge_free(heap, assets_raw as *mut u8);
        deluge_free(heap, chunks_raw as *mut u8);
        deluge_free(heap, buckets_raw as *mut u8);
        deluge_free(heap, ptr_buckets_raw as *mut u8);
        deluge_free(heap, queued_bits_raw as *mut u8);
        deluge_free(heap, heap_raw as *mut u8);
        return ptr::null_mut();
    }
    for i in 0..queued_words {
        queued_bits_raw.add(i).write(Cell::new(0));
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
        heap_raw.add(i).write(Cell::new(NONE));
    }
    // The tables are never freed, so 'static is sound.
    let assets: &'static [Cell<AssetSlot>] = &*ptr::slice_from_raw_parts(assets_raw, asset_cap);
    let chunks: &'static [Cell<ChunkSlot>] = &*ptr::slice_from_raw_parts(chunks_raw, chunk_cap);
    let buckets: &'static [Cell<u32>] = &*ptr::slice_from_raw_parts(buckets_raw, bucket_cap);
    let ptr_buckets: &'static [Cell<u32>] =
        &*ptr::slice_from_raw_parts(ptr_buckets_raw, bucket_cap);
    let queued_bits: &'static [Cell<u32>] =
        &*ptr::slice_from_raw_parts(queued_bits_raw, queued_words);
    let evict_heap: &'static [Cell<u32>] = &*ptr::slice_from_raw_parts(heap_raw, chunk_cap);
    ptr::write(
        m,
        Manager {
            heap,
            slab: Cell::new(ptr::null_mut()),
            assets,
            chunks,
            buckets,
            ptr_buckets,
            queued_bits,
            evict_heap,
            heap_len: Cell::new(0),
            free_hint: Cell::new(0),
            tick: Cell::new(0),
            protect: Cell::new(NONE),
            stats: Cell::new(Stats::default()),
            alloc_gen: Cell::new(0),
        },
    );
    deluge_heap_register_reclaim(heap, resource_reclaim, m as *mut c_void);
    m as *mut DelugeResource
}

/// Evicts the single lowest-ranked evictable chunk (the reclaim-hook body). Returns whether
/// something was freed.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
#[cfg(test)]
pub unsafe fn resource_try_evict(handle: *mut DelugeResource) -> bool {
    if handle.is_null() {
        return false;
    }
    mgr(handle).evict_lowest()
}

/// Sets the slab that backs [`BACKING_SLAB`] assets; until set, every asset uses the heap.
///
/// Create it with `deluge_slab_create_unmanaged` over the same heap, so eviction stays with this
/// manager.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`. `slab` must be null or a
/// live slab that outlives the manager.
pub unsafe fn resource_set_slab(handle: *mut DelugeResource, slab: *mut DelugeSlab) {
    if !handle.is_null() {
        mgr(handle).set_slab(slab);
    }
}

/// Defines an asset: an opaque `owner` token and its [`Source`]. Returns the asset id, or
/// `0xFFFFFFFF` if the asset table is full.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`. The callbacks and `ctx`
/// must stay valid until the asset is released.
pub unsafe fn resource_define_asset(
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
            construct: None, // set by resource_set_construct
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
/// `handle` must be null or a live handle from `resource_create`. Not safe against
/// concurrent access to the same manager.
pub unsafe fn resource_release_asset(handle: *mut DelugeResource, asset: u32) {
    if !handle.is_null() {
        mgr(handle).release_asset(asset);
    }
}

/// Sets (or, with `None`, clears) an asset's `construct` callback, making it requestable.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_set_construct(
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
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_set_evict_tail_first(handle: *mut DelugeResource, asset: u32, on: bool) {
    if !handle.is_null() {
        mgr(handle).set_evict_tail_first(asset, on);
    }
}

/// Marks an asset self-protecting: while it allocates a chunk, its own chunks are not eviction
/// candidates. For unleased caches whose `request(N+1)` must not evict the just-written `N`.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_set_self_protect(handle: *mut DelugeResource, asset: u32, on: bool) {
    if !handle.is_null() {
        mgr(handle).set_self_protect(asset, on);
    }
}

/// Returns chunk `index` of `asset` under a hard lease, constructing it without I/O if not
/// resident; the loader fills it and calls [`resource_mark_ready`]. Null on OOM or if the
/// asset has no `construct` callback.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_request(
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
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_acquire(
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
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_try_acquire(handle: *mut DelugeResource, asset: u32, index: u32) -> *mut u8 {
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
/// `handle` must be null or a live handle from `resource_create`.
#[cfg(test)]
pub unsafe fn resource_peek(handle: *mut DelugeResource, asset: u32, index: u32) -> *mut u8 {
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
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_mark_ready(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).mark_ready(ptr);
    }
}

/// Returns the slot index backing `ptr`, or `DELUGE_RESOURCE_NO_SLOT` if it is not resident.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_slot_of(handle: *mut DelugeResource, ptr: *mut u8) -> u32 {
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
/// `handle` must be null or a live handle from `resource_create`. Non-null `out_asset` and
/// `out_index` must be valid for a `u32` write.
pub unsafe fn resource_chunk_ident(
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
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_lease_count_by_slot(handle: *mut DelugeResource, slot: u32) -> u32 {
    if handle.is_null() {
        return 0;
    }
    mgr(handle).lease_count_by_slot(slot)
}

/// Returns the hard leases on the chunk at `slot` excluding the load queue's own (see
/// [`resource_loader_enqueue_owned`]): whether anyone besides the queue still wants it.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_external_lease_count_by_slot(handle: *mut DelugeResource, slot: u32) -> u32 {
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
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_loader_enqueue(handle: *mut DelugeResource, slot: u32, priority: u32) {
    if !handle.is_null() {
        mgr(handle).loader_enqueue(slot, priority);
    }
}

/// Enqueues the chunk at `slot` and takes a queue-owned hard lease, for a caller that cannot hold
/// its own lease until the load lands. A re-enqueue takes no second lease.
///
/// Every terminal path of the loader must call [`resource_loader_release_owned`]; a
/// stranded queue lease pins the chunk forever.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_loader_enqueue_owned(handle: *mut DelugeResource, slot: u32, priority: u32) {
    if !handle.is_null() {
        mgr(handle).loader_enqueue_owned(slot, priority);
    }
}

/// Releases the queue-owned lease from [`resource_loader_enqueue_owned`]. Returns whether
/// there was one. Idempotent; does not de-queue.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_loader_release_owned(handle: *mut DelugeResource, slot: u32) -> bool {
    !handle.is_null() && mgr(handle).loader_release_owned(slot)
}

/// Removes the chunk at `slot` from the load queue, dropping any queue-owned lease.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
#[cfg(test)]
pub unsafe fn resource_loader_remove(handle: *mut DelugeResource, slot: u32) {
    if !handle.is_null() {
        mgr(handle).loader_remove(slot);
    }
}

/// Pops the most urgent queued, still-leased chunk and returns its backing, or null. Unleased
/// queued chunks are skipped and left for normal eviction.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_loader_next(handle: *mut DelugeResource) -> *mut u8 {
    if handle.is_null() {
        return ptr::null_mut();
    }
    mgr(handle).loader_next()
}

/// Returns whether any queued, leased chunk sits at the least urgent priority (`u32::MAX`).
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_loader_has_lowest(handle: *mut DelugeResource) -> bool {
    !handle.is_null() && mgr(handle).loader_has_lowest()
}

/// Returns whether [`resource_loader_next`] would return a chunk, without popping it.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_loader_has_any(handle: *mut DelugeResource) -> bool {
    !handle.is_null() && mgr(handle).loader_has_any()
}

/// Copies the manager's [`Stats`] into `*out`. No-op if either pointer is null.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`. A non-null `out` must be
/// valid for a `Stats` write.
pub unsafe fn resource_stats(handle: *mut DelugeResource, out: *mut Stats) {
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
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_stats_reset(handle: *mut DelugeResource) {
    if !handle.is_null() {
        mgr(handle).stats_reset();
    }
}

/// Frees the resident chunk at `ptr` without calling `on_evict`, for an owner discarding a chunk
/// itself (e.g. a sample cache truncating its tail). No-op if `ptr` is not resident.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`. The caller must not use
/// `ptr` afterwards.
pub unsafe fn resource_evict_chunk(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).evict_chunk(ptr);
    }
}

/// Registers an owner-built heap block of `size` bytes as an unleased chunk the manager may evict.
/// On eviction it calls `on_evict(ctx, ptr)` and then frees `ptr`. Pin it with
/// [`resource_add_lease`].
///
/// Returns `ptr`, or null if the chunk table is full and nothing is evictable.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`. `ptr` must be a live block
/// allocated from the manager's heap; once adopted, the manager may free it. `on_evict` and `ctx`
/// must stay valid while the block is adopted.
pub unsafe fn resource_adopt(
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
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_add_lease(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).add_lease(ptr);
    }
}

/// Drops one hard lease on the chunk at `ptr`; it stays cached until evicted.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_release(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).release(ptr);
    }
}

/// Adds a soft reference to an asset: its chunks are evicted after unreferenced ones, but are not
/// pinned.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_reference(handle: *mut DelugeResource, asset: u32) {
    if !handle.is_null() {
        mgr(handle).reference(asset, 1);
    }
}
/// Drops a soft reference taken by [`resource_reference`].
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_unreference(handle: *mut DelugeResource, asset: u32) {
    if !handle.is_null() {
        mgr(handle).reference(asset, -1);
    }
}

/// Marks the chunk at `ptr` most recently used.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_touch(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).touch(ptr);
    }
}

/// Sets or clears the chunk at `ptr` as dirty; a dirty chunk is never evicted.
///
/// # Safety
///
/// `handle` must be null or a live handle from `resource_create`.
pub unsafe fn resource_mark_dirty(handle: *mut DelugeResource, ptr: *mut u8, dirty: bool) {
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
        let h = unsafe {
            deluge_base_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16)
        };
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { resource_create(h, 4, 1) }; // 1 chunk slot
        assert!(!handle.is_null());
        (handle, buf)
    }

    /// Defines an asset with a `construct` callback, so it can be `request`ed.
    fn define_requestable_test_asset(handle: *mut DelugeResource) -> u32 {
        // SAFETY: `handle` is live (from `test_manager`).
        let asset = unsafe {
            resource_define_asset(
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
        unsafe { resource_set_construct(handle, asset, Some(mock_construct)) };
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

    // The eviction heap against the whole-table scan it replaces.

    unsafe extern "C" fn mock_materialize(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        index: u32,
        _dest: *mut u8,
        _len: usize,
    ) -> bool {
        index % 7 != 6 // some indices cannot be rebuilt, exercising acquire's rollback
    }

    /// A deterministic xorshift generator, so a failing sequence replays from its seed.
    struct Rng(u32);
    impl Rng {
        fn next(&mut self) -> u32 {
            self.0 ^= self.0 << 13;
            self.0 ^= self.0 >> 17;
            self.0 ^= self.0 << 5;
            self.0
        }
        fn below(&mut self, n: u32) -> u32 {
            self.next() % n
        }
    }

    const DIFF_ASSETS: u32 = 5;

    /// Defines logical asset `n` (`0..DIFF_ASSETS`) in some free asset slot: assets 0 and 1 are
    /// tail-first and self-protecting, asset 2 is free to rebuild so its chunks crowd the top of
    /// the heap, and every asset is requestable.
    fn define_diff_asset(m: &Manager, n: u32) -> u32 {
        let a = m.define_asset(
            (n as usize + 1) as *mut c_void,
            Source {
                materialize: Some(mock_materialize),
                on_evict: None,
                construct: Some(mock_construct),
                ctx: ptr::null_mut(),
                cost: [3, 1, 0, 2, 4][n as usize],
                backing: BACKING_HEAP,
            },
        );
        assert_ne!(a, NONE);
        m.set_evict_tail_first(a, n < 2);
        m.set_self_protect(a, n < 2);
        a
    }

    /// A random occupied slot's backing, or null if it drew a free slot.
    fn some_resident(m: &Manager, rng: &mut Rng) -> *mut u8 {
        m.chunks[rng.below(m.chunks.len() as u32) as usize]
            .get()
            .backing
    }

    /// Asserts the heap invariants and that, for no protection and for each asset protected, the
    /// heap picks the chunk the table scan picks. Returns how many picks overflowed the frontier.
    fn check_against_scan(m: &Manager, assets: &[u32]) -> u32 {
        m.assert_evict_heap_consistent();
        let mut overflows = 0;
        for &protect in core::iter::once(&NONE).chain(assets) {
            let (indexed, overflowed) = m.evict_candidate_indexed(protect);
            overflows += overflowed as u32;
            assert_eq!(
                indexed,
                m.evict_candidate_linear(protect),
                "heap and scan disagree with asset {protect:#x} protected"
            );
        }
        overflows
    }

    /// Random operation sequences through the manager's API: after every step the heap must hold
    /// exactly the evictable chunks, in heap order, and pick the chunk the table scan picks.
    #[test]
    fn the_eviction_heap_picks_what_a_table_scan_picks() {
        const SEQUENCES: u32 = 200;
        const STEPS: u32 = 400;
        let mut overflows = 0;
        let mut protected_picks = 0;
        for seed in 1..=SEQUENCES {
            let mut rng = Rng(seed.wrapping_mul(0x9E37_79B9) | 1);
            // A small arena, so allocations run the reclaim hook as well as the table filling.
            let words = (128 * 1024usize).div_ceil(16);
            let mut buf = std::vec![0u128; words];
            // SAFETY: `buf` is a live, 16-aligned arena for the whole sequence.
            let h = unsafe {
                deluge_base_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16)
            };
            // SAFETY: `h` is the live heap just created.
            let handle = unsafe { resource_create(h, 8, 64) };
            assert!(!handle.is_null());
            // SAFETY: live for the sequence, with `buf`.
            let m = unsafe { mgr(handle) };
            let mut assets: std::vec::Vec<u32> =
                (0..DIFF_ASSETS).map(|n| define_diff_asset(m, n)).collect();
            // Leases this sequence took, by backing; released at random.
            let mut leases: std::vec::Vec<*mut u8> = std::vec::Vec::new();
            for _ in 0..STEPS {
                let n = rng.below(DIFF_ASSETS);
                let asset = assets[n as usize];
                // Asset 2 spans more indices than the pick frontier holds, to overflow it.
                let index = rng.below(if n == 2 { 48 } else { 12 });
                let size = 512 << rng.below(4);
                match rng.below(21) {
                    0..=2 => {
                        let p = m.acquire(asset, index, size);
                        if !p.is_null() {
                            leases.push(p);
                        }
                    }
                    3..=5 => {
                        let p = m.request(asset, index, size);
                        if !p.is_null() {
                            leases.push(p);
                        }
                    }
                    6..=8 => {
                        if !leases.is_empty() {
                            let p = leases.swap_remove(rng.below(leases.len() as u32) as usize);
                            m.release(p);
                        }
                    }
                    9 => {
                        let p = some_resident(m, &mut rng);
                        if !p.is_null() {
                            m.add_lease(p);
                            leases.push(p);
                        }
                    }
                    10 => {
                        // The slot-token pair, retain or release.
                        let slot = rng.below(m.chunks.len() as u32);
                        let gen = m.generation_of_slot(slot);
                        if rng.below(2) == 0 {
                            if m.retain_by_slot_gen(slot, gen) {
                                leases.push(m.chunks[slot as usize].get().backing);
                            }
                        } else if let Some(k) = leases
                            .iter()
                            .position(|&p| p == m.chunks[slot as usize].get().backing)
                        {
                            assert!(m.release_by_slot_gen(slot, gen));
                            leases.swap_remove(k);
                        }
                    }
                    11 => m.set_dirty(some_resident(m, &mut rng), rng.below(3) == 0),
                    12 => m.touch(some_resident(m, &mut rng)),
                    13 => m.reference(asset, if rng.below(2) == 0 { 1 } else { -1 }),
                    14 => {
                        let expected = m.evict_candidate_linear(NONE);
                        let before = expected.map(|i| m.chunks[i].get().backing);
                        assert_eq!(m.evict_lowest(), expected.is_some());
                        if let (Some(i), Some(b)) = (expected, before) {
                            assert_ne!(m.chunks[i].get().backing, b, "the scan's pick survived");
                        }
                    }
                    15 => {
                        let p = m.try_acquire(asset, index);
                        if !p.is_null() {
                            leases.push(p);
                        }
                        m.mark_ready(some_resident(m, &mut rng));
                    }
                    16 => {
                        let bytes = 256 << rng.below(4);
                        // SAFETY: `h` is live; the block is handed to (or freed past) the manager.
                        let raw = unsafe { deluge_base_alloc::deluge_alloc(h, bytes, 16) };
                        if !raw.is_null()
                            && m.adopt(raw, bytes, rng.below(5), ptr::null_mut(), None)
                                .is_null()
                        {
                            // SAFETY: not adopted, so still ours.
                            unsafe { deluge_base_alloc::deluge_free(h, raw) };
                        }
                    }
                    17 => {
                        let slot = rng.below(m.chunks.len() as u32);
                        match rng.below(3) {
                            0 => m.loader_enqueue_owned(slot, rng.below(4)),
                            1 => {
                                m.loader_release_owned(slot);
                            }
                            _ => m.loader_remove(slot),
                        }
                    }
                    18 => {
                        let p = some_resident(m, &mut rng);
                        if !p.is_null() {
                            leases.retain(|&q| q != p);
                            m.evict_chunk(p);
                        }
                    }
                    19 => {
                        // A burst of unleased asset-2 chunks, which rank first while unreferenced.
                        for _ in 0..24 {
                            let p = m.request(assets[2], rng.below(48), 512);
                            m.release(p);
                        }
                    }
                    _ => {
                        if rng.below(4) == 0 {
                            // Retire the asset and define it afresh, dropping the leases on it.
                            leases.retain(|&q| m.chunk_ident(q).map(|(a, _)| a) != Some(asset));
                            m.release_asset(asset);
                            assets[n as usize] = define_diff_asset(m, n);
                        } else {
                            // Flip tail-first with chunks resident.
                            let on = !m.assets[asset as usize].get().evict_tail_first;
                            m.set_evict_tail_first(asset, on);
                        }
                    }
                }
                overflows += check_against_scan(m, &assets);
                if m.evict_candidate_linear(assets[2]) != m.evict_candidate_linear(NONE) {
                    protected_picks += 1;
                }
            }
            drop(buf);
        }
        // The protected walk must have been exercised, past the root and past the frontier.
        assert!(
            protected_picks > 0,
            "no pick ever skipped a protected asset"
        );
        assert!(
            overflows > 0,
            "the frontier never overflowed, so its fallback went untested"
        );
        std::println!("{protected_picks} protected picks, {overflows} frontier overflows");
    }

    /// One eviction from a full 7,490-slot table visits a heap path, not the table.
    #[test]
    fn an_eviction_does_not_scan_the_chunk_table() {
        const SLOTS: usize = 7490;
        let words = (4 * 1024 * 1024usize).div_ceil(16);
        let mut buf = std::vec![0u128; words];
        // SAFETY: `buf` is a live, 16-aligned arena for the whole test.
        let h = unsafe {
            deluge_base_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16)
        };
        // SAFETY: `h` is the live heap just created.
        let handle = unsafe { resource_create(h, 4, SLOTS) };
        assert!(!handle.is_null());
        // SAFETY: live for the test, with `buf`.
        let m = unsafe { mgr(handle) };
        let asset = define_requestable_test_asset(handle);
        for index in 0..SLOTS as u32 {
            let p = m.request(asset, index, 64);
            assert!(!p.is_null(), "request {index} failed");
            m.release(p);
        }
        m.stats_reset();
        // SAFETY: `handle` is live.
        assert!(unsafe { resource_try_evict(handle) });
        let s = m.stats_snapshot();
        assert_eq!(s.scan_evict_calls, 1);
        std::println!(
            "one eviction over {SLOTS} evictable chunks visited {} slots",
            s.scan_evict_slots
        );
        assert!(
            s.scan_evict_slots < 64,
            "one eviction visited {} slots of {SLOTS}",
            s.scan_evict_slots
        );
        m.assert_evict_heap_consistent();
        drop(buf);
    }
}
