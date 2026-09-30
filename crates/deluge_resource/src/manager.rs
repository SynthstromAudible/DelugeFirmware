//! The resource manager core — a value-scored, lease-based cache of reconstructable
//! assets, layered on the `deluge_alloc` TLSF heap. See docs/dev/resource_manager.md.
//!
//! Model: **Asset** (identity + a `Source` recipe + soft references) → **Chunk**
//! (the resident, individually-leased/evicted unit). Both live in fixed-capacity
//! tables allocated once from the heap.
//!
//! **Safe core, thin `unsafe` shell.** The tables are `&'static [Cell<Slot>]` of
//! `Copy` PODs, accessed by index — so all the bookkeeping (lease accounting, the
//! value function, eviction selection) is *safe* Rust. `Cell` gives interior
//! mutability with no borrow to invalidate, which is exactly what makes the manager
//! reentrancy-tolerant: when an allocation drives the reclaim hook, that hook takes
//! another shared `&Manager` and mutates through `Cell` — sound, because no method
//! ever takes `&mut self`. The manager never dereferences a chunk's backing memory
//! (it only stores it and passes it to `materialize` / `deluge_free`), so the only
//! `unsafe` is at the edges: the FFI entry points, the one-time table setup over the
//! heap bytes, the `materialize`/`on_evict` fn-pointer calls, and the alloc/free calls.

use crate::sync::{m_get, m_rmw, m_set, Masked};
use crate::value::evict_rank;
use core::cell::Cell;
use core::ffi::c_void;
use core::ptr;
use deluge_alloc::slab::{deluge_slab_acquire, deluge_slab_release, DelugeSlab};
use deluge_alloc::{deluge_alloc, deluge_free, deluge_heap_register_reclaim, DelugeHeap};

const NONE: u32 = u32::MAX;
/// Invalid chunk-slot index (the C ABI's `DELUGE_RESOURCE_NO_SLOT`): a C++ object's slot handle before
/// its chunk is created, or `slot_of` on a non-resident pointer. `pub(crate)` so `facade::Resource`
/// can recognize the same sentinel `slot_of` returns.
pub(crate) const NO_SLOT: u32 = u32::MAX;

/// Reconstruct chunk `index` of `owner` into `dest[..len]`. Returns false if it
/// can't be rebuilt right now (e.g. the backing store vanished). The public
/// extension point: the manager treats asset kinds opaquely through this.
pub type MaterializeFn = unsafe extern "C" fn(
    ctx: *mut c_void,
    owner: *mut c_void,
    index: u32,
    dest: *mut u8,
    len: usize,
) -> bool;

/// Notify the owner that a (currently unleased) chunk it may reference is being
/// evicted — it must drop any cached pointer (the `Stealable::steal()` analogue).
pub type EvictFn = unsafe extern "C" fn(ctx: *mut c_void, owner: *mut c_void, index: u32);

/// Initialize a chunk's backing *without* doing the (slow) I/O to fill it — the
/// async counterpart to `materialize`, used by `request` (prefetch). For a sample
/// cluster: placement-new the `Cluster` object and set its fields, leaving the data
/// to be read later by an external loader. Cannot fail (no I/O).
pub type ConstructFn =
    unsafe extern "C" fn(ctx: *mut c_void, owner: *mut c_void, index: u32, dest: *mut u8);

/// Evict callback for an **adopted** (object-lifecycle) chunk: the manager has chosen this
/// externally-allocated block for eviction and will free it right after. The owner drops any
/// reference + runs the object's teardown (e.g. erase + destruct). Gets the block pointer
/// directly (no owner/index), so a shared evictor can identify the object. See `adopt`.
pub type AdoptEvictFn = unsafe extern "C" fn(ctx: *mut c_void, ptr: *mut u8);

/// Where a chunk's backing comes from — uniform clusters use the slab, variable
/// assets use the heap directly. Plain `u32` to cross the C ABI (no C enum).
pub const BACKING_HEAP: u32 = 0;
pub const BACKING_SLAB: u32 = 1;

#[derive(Clone, Copy)]
pub struct Source {
    pub materialize: Option<MaterializeFn>,
    pub on_evict: Option<EvictFn>,
    /// Optional async path: init a chunk without I/O (for `request`/prefetch). Set via
    /// `deluge_resource_set_construct` after `define_asset`; None ⇒ the asset isn't
    /// requestable (only `acquire`, which materializes synchronously).
    pub construct: Option<ConstructFn>,
    pub ctx: *mut c_void,
    pub cost: u32,
    pub backing: u32, // BACKING_HEAP | BACKING_SLAB
}

#[derive(Clone, Copy)]
struct AssetSlot {
    in_use: bool,
    owner: *mut c_void,
    source: Source,
    soft_refs: u32,
    /// Prefix-dependent asset (e.g. a SampleCache, whose `on_evict` discards all
    /// higher-index chunks): only the highest-index resident chunk may be evicted, so
    /// `on_evict` never cascades into a sibling the manager still tracks. See `evict_lowest`.
    evict_tail_first: bool,
    /// Self-protect during own allocation (the `dontStealFromThing` port): while this asset
    /// is in `request`/`acquire`, its own chunks are not eviction candidates. For unleased
    /// caches (which would otherwise let `request(N+1)` evict the just-written `N`); leased
    /// assets (sample clusters) don't need it and must stay able to evict their own old chunks.
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
    asset: u32,       // index into the asset table; NONE => an adopted (object-lifecycle) chunk
    index: u32,       // chunk index within the asset (unused for adopted chunks)
    leases: u32,      // hard leases; > 0 ⇒ never evicted
    dirty: bool,      // unsaved (e.g. a recording) ⇒ never evicted
    // Loaded/ready: false between `request` (slot reserved + constructed, no data yet — the `Loading`
    // state) and `mark_ready` (the loader/embassy task signalled the read complete). `acquire` (which
    // materializes synchronously) and `adopt` (owner-built) leave it true. `try_acquire` only returns a
    // chunk that is `ready`, so the RT/async path never reads half-loaded data.
    ready: bool,
    recency: u64,
    /// Backing size in bytes — the memory reclaimed by evicting this chunk. Feeds the value
    /// function's cost-per-byte term (a big cheap chunk is preferred over a tiny dear one). Set at
    /// alloc: the requested `size` for `acquire`/`request`, the owner-supplied block size for `adopt`.
    size: u32,
    /// Stamped from `Manager::next_gen()` each time this slot takes a fresh backing
    /// (free -> occupied, i.e. NOT on a cache-hit lease). Pairs with the slot index into
    /// a `{slot, generation}` independent-pin token (see `facade::Resource::pin_token`):
    /// a token minted while this slot held one chunk no longer matches after the slot is
    /// evicted and reused for another, so a stale retain/release is a checked no-op.
    generation: u32,
    // The cluster load queue lives *as per-slot state* (no separate heap): `queued` ⇒ this chunk is
    // waiting to be read by the loader, ordered by `queue_priority` (lower = more urgent, the C++
    // Voice::getPriorityRating). `loader_next` picks the lowest-priority queued+leased slot. Eviction
    // resets the slot to EMPTY, which auto-de-queues — no dangling queue entry. See the loader_* fns.
    queued: bool,
    queue_priority: u32,
    /// One of this slot's `leases` belongs to the load queue itself, taken by
    /// `loader_enqueue_owned` and released by `loader_release_owned`.
    ///
    /// `loader_next` only serves a chunk with `leases > 0`, because filling an unleased (therefore
    /// evictable) chunk could write into a slot that has since been recycled. But nothing made the
    /// *enqueuer* keep a lease alive: a caller that enqueued and then dropped its own lease left an
    /// entry `loader_next` silently discards. That is a lease the queue needs, so the queue takes
    /// it — this flag records that it did, which keeps the release idempotent and makes
    /// `external_lease_count_by_slot` ("does anyone BESIDES the queue still want this?") answerable.
    ///
    /// Both protocols coexist per chunk: a caller that holds its own lease across the whole load
    /// (the passive-lookahead prefetch, whose lease lives in the reader's reservation window) still
    /// uses the plain `loader_enqueue`, leaves this false, and is unaffected.
    queue_leased: bool,
    // Adopt mode (asset == NONE): the chunk carries its own cost + evict callback, because
    // an adopted block is an externally-allocated object, not a chunk of a Source asset.
    // The owner allocated it; the manager only owns its eviction. (Unused when asset != NONE.)
    cost: u32,
    adopt_evict: Option<AdoptEvictFn>,
    adopt_ctx: *mut c_void,
    /// Next slot in this chunk's `(asset, index)` hash bucket, or `NONE` at the end of the chain.
    /// Only meaningful while this slot is indexed — i.e. occupied with `asset != NONE`; see
    /// [`Manager::index_insert`] for why adopted chunks are excluded.
    hash_next: u32,
    /// `deluge_debug_now_frames()` when this chunk was enqueued for loading, so the service latency
    /// can be split at `loader_next` and measured again at `mark_ready`. 0 == never enqueued (the
    /// synchronous `acquire`/`adopt` paths).
    queued_at: u32,
    /// Next slot in this chunk's `backing`-pointer hash bucket, or `NONE` at the end of the chain.
    /// Unlike `hash_next`, EVERY occupied slot is in this index: backing pointers are unique by
    /// construction, so adopted chunks need no exclusion rule here.
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
}

// The audio sample-timer, for measuring loader service latency (how long a chunk waits between
// being enqueued and its fill completing). Provided by the C++ side; the manager has no clock of its
// own. See `Stats`'s queue_wait_*/fill_* fields.
unsafe extern "C" {
    pub fn deluge_debug_now_frames() -> u32;
}

/// Number of cost classes the eviction stats bucket by (`evictions_by_cost[min(cost, COST_BUCKETS-1)]`).
pub const COST_BUCKETS: usize = 8;

/// Cumulative manager counters — pure instrumentation (never affects behaviour), for cache/eviction
/// analysis, profiling, and on-device debug. C-ABI POD; read via `deluge_resource_stats`, zero via
/// `deluge_resource_stats_reset`. All events are chunk-granular (off the per-sample audio path).
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Stats {
    pub acquires: u64,                          // acquire() calls
    pub acquire_hits: u64, // ... that hit a resident chunk (no alloc/materialize)
    pub requests: u64,     // request() (prefetch-construct) calls
    pub materializes: u64, // asset materialize() invocations (sync storage reloads)
    pub evictions: u64,    // chunks evicted under pressure / to free a slot
    pub alloc_failures: u64, // alloc_backing returned null (pool exhausted after reclaim)
    pub adopts: u64,       // objects adopted
    pub evictions_by_cost: [u64; COST_BUCKETS], // evicted-chunk breakdown by cost class
    // ── Scan-cost instrumentation (THROWAWAY: added 2026-08-16 to size the manager's O(table)
    // linear scans on device; remove once the indexing decision is made). `*_calls` counts
    // invocations, `*_slots` counts slots actually visited -- the cost driver, since each visit is a
    // masked (interrupt-disabling) read of a Cell<ChunkSlot> in SDRAM. Measured conversion on
    // hardware: a full 6144-slot scan costs ~10.5 ms, i.e. ~1.7 us per slot visited.
    pub scan_resident_calls: u64,
    pub scan_resident_slots: u64,
    pub scan_ptr_calls: u64,
    pub scan_ptr_slots: u64,
    pub scan_free_calls: u64,
    pub scan_free_slots: u64,
    pub scan_evict_calls: u64,
    pub scan_evict_slots: u64,
    /// Loader queue DEPTH sampled inside `loader_next`'s existing scan (so it costs nothing extra):
    /// how many queued+leased candidates it had to choose between. Measured at **mean 1.19, max 37**
    /// on 2026-08-16 — with about one candidate at a time there is nothing to order, which is why
    /// deadline-ordering the queue changed nothing. A mean rising well above 1 is the condition
    /// under which ordering would start to matter again.
    pub loader_depth_polls: u64,
    pub loader_depth_total: u64,
    pub loader_depth_max: u64,
    /// Loader service latency in output frames, SPLIT into its two halves.
    ///
    /// `queue_wait` is enqueue -> `loader_next` picking it up: the fill task's scheduling delay.
    /// `fill` is `loader_next` -> `mark_ready`: the read plus convert, i.e. the card.
    ///
    /// Measured 2026-08-16: **queue_wait mean 35 ms, fill mean 5.6 ms** — 86% of a streaming chunk's
    /// latency is waiting to be scheduled, not doing I/O, against the ~186 ms a 32 KB cluster buys.
    /// This is the acceptance criterion for the storage-tier scheduling work: 35 ms mean is the
    /// number to beat. See that design's Appendix C.
    pub queue_wait_samples: u64,
    pub queue_wait_total: u64,
    pub queue_wait_max: u64,
    pub fill_samples: u64,
    pub fill_total: u64,
    pub fill_max: u64,
}

pub struct Manager {
    // Raw, but only ever *passed* to the (unsafe) deluge_alloc/slab calls — never
    // dereferenced by the manager itself. The cluster slab (if set via
    // deluge_resource_set_slab) backs uniform BACKING_SLAB assets; null ⇒ all assets
    // use the heap.
    heap: *mut DelugeHeap,
    slab: Cell<*mut DelugeSlab>,
    assets: &'static [Cell<AssetSlot>],
    chunks: &'static [Cell<ChunkSlot>],
    /// Hash index over `(asset, index)` -> chunk slot: each entry is the head of an intrusive
    /// chain linked through `ChunkSlot::hash_next`, or `NONE` when empty. Power-of-two length
    /// (`>= chunks.len()`), so bucket selection is a mask.
    ///
    /// Chaining rather than open addressing because deletion is frequent here (every eviction):
    /// open addressing needs tombstones, and on a device that runs for hours of churn tombstones
    /// accumulate until something rehashes. A chain has no tombstones and no rehash, and its
    /// links live in the chunk slots that already exist.
    ///
    /// It replaces what was an unconditional linear scan of the whole table on every lookup MISS.
    /// Measured on device before this existed: ~5827 slots visited per miss at ~0.92 us each, so
    /// a cold sample preview spent 21 ms here, and ordinary playback spent 5-27% of CPU in it --
    /// all with interrupts masked, since each visit is a masked read.
    buckets: &'static [Cell<u32>],
    /// Hash index over `backing` -> chunk slot, chained through `ChunkSlot::ptr_next`. Same
    /// structure and length as `buckets`, and it exists for the same reason: `find_by_ptr` backs
    /// seven entry points (retain/release via `rmw_by_ptr`, `slot_of`, `chunk_ident`,
    /// `is_ready_by_ptr`, `mark_ready`, `evict_chunk`), and every one of them was a full masked
    /// scan of the table.
    ///
    /// Measured on device once the `(asset, index)` index had removed the larger cost: 25441 calls
    /// visiting 7.9M slots over ~80 s of playback — 312 slots per call, 12-18% of CPU. Unlike the
    /// key index this one holds EVERY occupied slot, adopted chunks included, because backing
    /// pointers are unique.
    ptr_buckets: &'static [Cell<u32>],
    tick: Cell<u64>,
    /// Transient self-protection (the `dontStealFromThing` port): while an asset is
    /// allocating a chunk (`request`/`acquire`), its own chunks are not eviction
    /// candidates — so allocating cluster N+1 can't steal the just-written N. `NONE`
    /// outside such a call. Set via the `ProtectGuard` RAII helper.
    protect: Cell<u32>,
    /// Cumulative instrumentation counters (see `Stats`).
    stats: Cell<Stats>,
    /// Monotonic per-allocation counter feeding `ChunkSlot::generation` (see `next_gen`).
    alloc_gen: Cell<u32>,
}

/// Restores `Manager::protect` on drop — so every early return from `request`/`acquire`
/// clears the self-protection.
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

    /// Mutate the instrumentation counters (masked get/modify/set). Pure bookkeeping,
    /// never gates behaviour; events are chunk-granular so the whole-struct copy is negligible.
    fn stat(&self, f: impl FnOnce(&mut Stats)) {
        m_rmw(&self.stats, |s| f(s));
    }

    /// Monotonic per-allocation generation. Stamped into a slot each time it takes a
    /// fresh backing (free -> occupied), so a `{slot, generation}` token minted while
    /// a chunk was resident no longer matches after that slot is evicted and reused —
    /// making a stale independent-pin retain/release a checked no-op. Wraps after 2^32
    /// allocations (astronomically beyond any session).
    #[inline]
    fn next_gen(&self) -> u32 {
        m_rmw(&self.alloc_gen, |g| {
            *g = g.wrapping_add(1);
            *g
        })
    }

    /// Masked snapshot of the instrumentation counters (the FFI read path — the audio
    /// thread writes these via `stat`, so the reader must mask to avoid a torn copy).
    fn stats_snapshot(&self) -> Stats {
        m_get(&self.stats)
    }
    /// Masked reset of the instrumentation counters.
    fn stats_reset(&self) {
        m_set(&self.stats, Stats::default());
    }

    fn find_resident(&self, asset: u32, index: u32) -> Option<usize> {
        let mut visited = 0u64;
        let found = if asset == NONE {
            // Adopted chunks are not indexed (see index_insert); keep the original scan so this
            // key's semantics are unchanged. No production caller passes NONE here.
            (0..self.chunks.len()).find(|&i| {
                visited += 1;
                let s = m_get(&self.chunks[i]);
                !s.backing.is_null() && s.asset == asset && s.index == index
            })
        } else {
            // Walk the bucket chain, re-validating each candidate against exactly the predicate
            // the linear scan used. The re-validation is not belt-and-braces: the chain is read
            // unmasked between slots (as every scan here is), so a slot can be evicted and reused
            // under us — the masked per-slot read plus this check is what makes that safe.
            let mut cur = self.buckets[self.bucket_of(asset, index)].get();
            let mut hit = None;
            // Bounded walk. A chain can only be cyclic if the index is corrupt, but this runs on
            // the audio path, where spinning forever is a dead device and a wrong answer is a
            // duplicate chunk — degraded, alive, and visible in the stats. Take the survivable
            // failure. (A dropped `index_remove` produced exactly this cycle in testing.)
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
        // One stat update per scan, never per slot: `stat` is itself a masked RMW of the whole
        // Stats struct, so a per-slot update would cost more than the scan being measured.
        self.stat(|s| {
            s.scan_resident_calls += 1;
            s.scan_resident_slots += visited;
        });
        found
    }
    /// Bucket for `(asset, index)`. Mixes BOTH halves of the key: cluster indices are small and
    /// dense and asset ids are small and dense, so any hash that merely concatenates them piles
    /// every asset's cluster 0 into neighbouring buckets. The two odd constants are the usual
    /// 32-bit mixing primes (golden-ratio and xxHash's).
    #[inline]
    fn bucket_of(&self, asset: u32, index: u32) -> usize {
        let h = asset.wrapping_mul(0x9E37_79B1).rotate_left(15) ^ index.wrapping_mul(0x85EB_CA6B);
        (h as usize) & (self.buckets.len() - 1)
    }

    /// Bucket for a backing pointer. Backings are 16-byte aligned (see `alloc_backing`), so the
    /// low four bits carry no information and are shifted out before mixing — hashing them in
    /// would leave a quarter of the buckets unreachable.
    #[inline]
    fn ptr_bucket_of(&self, p: *mut u8) -> usize {
        let h = ((p as usize) >> 4) as u32;
        (h.wrapping_mul(0x9E37_79B1).rotate_left(15) as usize) & (self.ptr_buckets.len() - 1)
    }

    /// Link slot `i` into the pointer index. Same masking contract as [`Manager::index_insert`]:
    /// the caller holds the window that also writes the slot.
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

    /// Unlink slot `i` from the pointer index. Takes `backing` explicitly for the same reason
    /// [`Manager::index_remove`] takes the key: the caller is clearing the slot around this call.
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
                return; // corrupt link — survivable failure, as in find_resident's walk
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

    /// Link slot `i` into its bucket. Caller MUST already hold a masked window covering the write
    /// of the slot itself, so the slot and the index never disagree even for an instant: a
    /// preempting lookup that saw the slot but not the link would report "not resident" for a
    /// chunk that is, and its caller would allocate a SECOND chunk for the same key.
    ///
    /// Adopted chunks (`asset == NONE`) are deliberately NOT indexed. They all share the key
    /// `(NONE, 0)` (see `adopt`), so they would form one chain thousands long, and unlinking from
    /// it would be exactly the linear scan this index exists to remove. `find_resident` keeps the
    /// scan for that key instead.
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

    /// Unlink slot `i` from its bucket. Same masking contract as [`Manager::index_insert`] — the
    /// caller holds the window that also clears the slot.
    ///
    /// Takes the key explicitly because the caller has usually already overwritten (or is about
    /// to overwrite) the slot, so the key can no longer be read back from it.
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
        // Walk to the predecessor. Bounded by the chain, which is ~1 entry: live keyed chunks are
        // capped by slab capacity and the bucket count is >= the chunk table's length.
        let mut cur = head;
        let mut steps = 0;
        while cur != NONE && steps <= self.chunks.len() {
            let c = cur as usize;
            if c >= self.chunks.len() {
                return; // corrupt link — same survivable-failure argument as find_resident's walk
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

    /// Ground truth for the index tests: the unconditional linear scan `find_resident` used to
    /// be. Kept test-only so the agreement test compares the index against the definition of
    /// correctness rather than against the test's own model of what should be resident (which
    /// eviction, lease drops, and preemption all falsify).
    #[cfg(test)]
    pub(crate) fn find_resident_linear(&self, asset: u32, index: u32) -> Option<usize> {
        (0..self.chunks.len()).find(|&i| {
            let s = m_get(&self.chunks[i]);
            !s.backing.is_null() && s.asset == asset && s.index == index
        })
    }

    fn find_by_ptr(&self, p: *mut u8) -> Option<usize> {
        let mut visited = 0u64;
        // A null pointer is never resident, and every free slot holds null — without this guard
        // the walk below would be asked for the bucket of null and match the first free slot it
        // found, reporting a "resident" chunk that is not one.
        let found = if p.is_null() {
            None
        } else {
            let mut cur = self.ptr_buckets[self.ptr_bucket_of(p)].get();
            let mut hit = None;
            let mut steps = 0;
            while cur != NONE && steps <= self.chunks.len() {
                let i = cur as usize;
                if i >= self.chunks.len() {
                    break; // corrupt link; treat as a miss (see find_resident's walk)
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

    /// Fiber-safe pointer-keyed RMW: locate the slot whose `backing == p` (heuristic
    /// scan, mask released between slots), then under ONE masked window re-validate
    /// `backing == p` (audio may have evicted+reused the slot since the scan) and apply
    /// `f`. Returns true if it mutated. No-op (false) if `p` isn't resident / changed.
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

    /// Masked, re-validating RMW on a slot by index (the index came from a prior scan).
    /// The closure returns a value; a slot that emptied under us is the closure's concern.
    fn rmw_by_ptr_slot<R>(&self, i: usize, f: impl FnOnce(&mut ChunkSlot) -> R) -> R {
        m_rmw(&self.chunks[i], f)
    }

    /// The chunk-table slot index backing `p`, or `NO_SLOT` if `p` isn't resident. O(n); the C++ side
    /// caches the result at chunk creation so subsequent lease reads go through `lease_count_by_slot`.
    pub(crate) fn slot_of(&self, p: *mut u8) -> u32 {
        match self.find_by_ptr(p) {
            Some(i) => i as u32,
            None => NO_SLOT,
        }
    }

    /// O(1) hard-lease count of the chunk at `slot` — 0 if `slot` is out of range or the slot is free.
    /// The C++ object holds its slot index (a handle), so the loading queue / invariant checks read the
    /// lease count without an O(n) `find_by_ptr` scan. `pub(crate)` so the safe `facade` module (and its
    /// tests) can observe lease-balance without an O(n) scan.
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

    /// Hard leases on the chunk at `slot` EXCLUDING the load queue's own (see
    /// `ChunkSlot::queue_leased`) — "does any consumer besides the loader queue still want this
    /// chunk?". Identical to `lease_count_by_slot` for a chunk the queue does not hold a lease on.
    ///
    /// This is the count the drain's abandonment check needs: with a queue-owned lease held, the
    /// raw count never reaches 0, so a chunk nobody wants any more would be retried forever instead
    /// of dropped.
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

    /// The `(asset, index)` identity of the resident chunk backing `p` (the manager's `ChunkSlot`
    /// holds both), or `None` if `p` isn't resident. `asset == NONE` (`u32::MAX`) for an adopted
    /// (object-lifecycle) chunk — mirrors `ChunkSlot::asset`'s own sentinel; `index` is meaningless
    /// for those (see `ChunkSlot`'s field doc). O(n) (`find_by_ptr`), re-validated under one masked
    /// window after the scan (same "heuristic scan, then re-check" shape as `rmw_by_ptr`) — the scan
    /// itself releases the mask between slots, so the slot `p` was found at could have been
    /// evicted+reused by the time this reads it. `pub(crate)` so the safe `facade` module can expose
    /// it as `Resource::chunk_ident`.
    pub(crate) fn chunk_ident(&self, p: *mut u8) -> Option<(u32, u32)> {
        let i = self.find_by_ptr(p)?;
        let s = m_get(&self.chunks[i]);
        (s.backing == p).then_some((s.asset, s.index))
    }

    /// The generation stamped on the chunk at `slot` — 0 if out of range or free.
    /// Pairs with a `{slot, generation}` independent-pin token (see the facade).
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

    /// Generation-checked +1 hard lease on the chunk at `slot`. Masked, re-validating:
    /// takes the lease only if the slot is in range, occupied, AND its generation still
    /// equals `gen` (the slot has not been evicted+reused since the token was minted).
    /// Returns whether it leased. The independent-pin retain path.
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

    /// Generation-checked -1 hard lease (the independent-pin release path). Symmetric
    /// to `retain_by_slot_gen`: a no-op on a stale/out-of-range/free slot. Saturating,
    /// so a contract-violating double-release cannot underflow. Mirrors `release`
    /// (which also only decrements the lease count — see its doc) so the two paths stay
    /// behaviourally identical modulo the generation check.
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

    // ---- cluster load queue (per-slot state; see the `queued`/`queue_priority` fields) ----------

    /// Enqueue the chunk at `slot` for loading at `priority` (lower = more urgent). Re-enqueue just
    /// updates the priority. No-op if `slot` is out of range / free.
    pub(crate) fn loader_enqueue(&self, slot: u32, priority: u32) {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return;
        }
        m_rmw(&self.chunks[i], |s| {
            if s.backing.is_null() {
                return;
            }
            s.queued = true;
            s.queue_priority = priority;
            // SAFETY: a plain counter read provided by the C++ side; no invariants.
            s.queued_at = unsafe { deluge_debug_now_frames() };
        });
    }

    /// Enqueue the chunk at `slot` at `priority` AND take a queue-owned hard lease, so the entry
    /// survives the enqueuer dropping its own lease. For a caller that cannot hold a lease until the
    /// load lands — a fire-and-forget "fill this eventually" — because `loader_next` serves only
    /// leased chunks and silently discards the rest. See `ChunkSlot::queue_leased`.
    ///
    /// Idempotent in the lease: re-enqueueing an already-queue-leased chunk updates the priority
    /// without taking a second lease. Every terminal path in the drain must pair this with
    /// `loader_release_owned`, and `loader_remove` releases it too — a stranded queue lease pins the
    /// chunk against eviction forever.
    ///
    /// No-op if `slot` is out of range / free.
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
            s.queued = true;
            s.queue_priority = priority;
        });
    }

    /// Release the queue-owned lease taken by `loader_enqueue_owned`. Returns whether there was one
    /// to release, so a caller can distinguish "this chunk was queue-owned" from "the enqueuer owns
    /// its own lease" (the plain `loader_enqueue` protocol). Idempotent; a no-op on an
    /// out-of-range/free slot or a chunk without a queue lease.
    ///
    /// Does NOT de-queue: the drain's read-failure path releases nothing and re-queues, while its
    /// terminal paths release a chunk `loader_next` has already de-queued.
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

    /// Remove the chunk at `slot` from the load queue (the C++ `erase`). No-op if not queued.
    /// Also drops any queue-owned lease (`loader_enqueue_owned`): erasing the entry ends the
    /// queue's interest in the chunk, and a lease left behind here would pin it permanently.
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

    /// Pop the most-urgent (lowest `queue_priority`) queued chunk that is still leased; clear its
    /// `queued` and return its backing. Queued-but-unleased chunks (abandoned prefetch) are silently
    /// de-queued and left resident (the manager evicts them normally — they are NOT destroyed here, so
    /// the owner pointer is only ever nulled via the proper on_evict path). Returns null if none.
    ///
    /// The lease requirement is a correctness guard, not a policy: an unleased chunk is evictable, so
    /// filling one could write into a slot already recycled for another chunk. A caller that cannot
    /// hold its own lease across the load must therefore enqueue via `loader_enqueue_owned`, which
    /// gives the queue a lease of its own — a chunk popped here may hold one, and the caller of this
    /// function owns releasing it (`loader_release_owned`) on every terminal path.
    /// O(n) scan, consistent with `evict_lowest`.
    pub(crate) fn loader_next(&self) -> *mut u8 {
        // Scan unmasked (coherent per-slot m_get, mask released between slots) for the
        // most-urgent queued + still-leased chunk. The pick is a heuristic; the commit
        // re-validates under the mask.
        let mut best: Option<usize> = None;
        let mut best_pri = u32::MAX;
        let mut depth = 0u64; // queued+leased candidates this poll — see Stats::loader_depth_*
        for i in 0..self.chunks.len() {
            let s = m_get(&self.chunks[i]);
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
        // Winner-commit under one masked window: re-check it is still queued+leased
        // (audio may have released/evicted it since the scan), clear queued, return backing.
        let _m = Masked::enter();
        let mut s = self.chunks[i].get();
        if !s.queued || s.backing.is_null() || s.leases == 0 {
            return ptr::null_mut(); // changed under us — caller retries next poll
        }
        // Split the service latency here: everything before this point was queue wait, everything
        // after is the fill. Re-stamping rather than clearing lets `mark_ready` measure the second
        // half through the same field.
        if s.queued_at != 0 {
            // SAFETY: a plain counter read provided by the C++ side; no invariants.
            let now = unsafe { deluge_debug_now_frames() };
            waited_for_stats = Some(now.wrapping_sub(s.queued_at) as u64);
            s.queued_at = now;
        }
        s.queued = false;
        self.chunks[i].set(s);
        drop(_m); // release the mask before the stats RMW (it re-enters; keep windows short)
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

    /// Any queued + leased chunk at the lowest priority value (u32::MAX) — the `load_song_ui` yield
    /// predicate ("is there still lowest-priority background load work").
    fn loader_has_lowest(&self) -> bool {
        (0..self.chunks.len()).any(|i| {
            let s = m_get(&self.chunks[i]);
            s.queued && !s.backing.is_null() && s.leases > 0 && s.queue_priority == u32::MAX
        })
    }

    /// Any queued + leased chunk at all, regardless of priority — the non-destructive
    /// "is the loader queue non-empty" predicate the non-blocking
    /// `deluge_streaming_queue_drained` asks (an offline render waits on it, pass by pass). Neither pops nor mutates
    /// queue state (unlike `loader_next`), and does not filter on priority (unlike
    /// `loader_has_lowest`): it answers exactly "would `loader_next` return non-null", i.e. is there
    /// any drain work left.
    fn loader_has_any(&self) -> bool {
        (0..self.chunks.len()).any(|i| {
            let s = m_get(&self.chunks[i]);
            s.queued && !s.backing.is_null() && s.leases > 0
        })
    }

    /// Is `index` the highest-index resident chunk of `asset`? (No resident chunk of the
    /// asset has a greater index.) Used to gate tail-first eviction.
    fn is_highest_resident(&self, asset: u32, index: u32) -> bool {
        !(0..self.chunks.len()).any(|i| {
            let s = m_get(&self.chunks[i]);
            !s.backing.is_null() && s.asset == asset && s.index > index
        })
    }

    /// Set the transient self-protection to `asset`, restoring the previous value on drop.
    fn protect_asset(&self, asset: u32) -> ProtectGuard<'_> {
        let prev = m_get(&self.protect);
        m_set(&self.protect, asset);
        ProtectGuard { mgr: self, prev }
    }

    /// Does this asset want slab backing, and is a slab configured? `NONE` (an adopted
    /// chunk) is always heap-backed (the owner allocated it from the heap).
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

    /// Free a chunk's backing — slab-release for slab-backed assets, else heap-free.
    fn free_backing(&self, backing: *mut u8, asset: u32) {
        let slab = self.slab_for(asset);
        if !slab.is_null() {
            // SAFETY: `backing` is a slot of this slab. Returns false only if it's not
            // owned here, in which case fall through to a plain heap free.
            if unsafe { deluge_slab_release(slab, backing) } {
                return;
            }
        }
        // SAFETY: `backing` was returned by deluge_alloc on this heap and is live.
        unsafe { deluge_free(self.heap, backing) };
    }

    /// Allocate a chunk's backing — a uniform slot from the slab for slab-backed
    /// assets, else `size` bytes from the heap.
    fn alloc_backing(&self, size: usize, asset: u32) -> *mut u8 {
        let slab = self.slab_for(asset);
        let p = if !slab.is_null() {
            // SAFETY: `slab` is a live unmanaged slab over the same heap; uniform slot
            // size, so `size` is implicit. owner is unused (no slab self-eviction).
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

    /// Evict the lowest-value evictable (unleased, non-dirty) chunk: notify its
    /// owner, return its backing to the heap, free the slot. Returns false if
    /// nothing is evictable. Used both as the heap reclaim hook and to free a slot.
    fn evict_lowest(&self) -> bool {
        let protect = m_get(&self.protect);
        // Retry loop: a masked commit can bail if the victim got leased/dirtied under us
        // since the unmasked scan; try the next-best candidate. Bounded by table size.
        let mut skip: Option<usize> = None;
        // Counts only THIS function's own slot visits. `is_highest_resident` (called per candidate
        // below) runs its own nested O(n) scan for evict_tail_first assets, so the true cost of a
        // single evict_lowest can exceed what this records -- read it as a floor, not a total.
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
                // Self-protection: don't evict the asset currently allocating a chunk. Only when a
                // real asset is protected (NONE = no protection; NONE is also the adopted marker).
                if protect != NONE && s.asset == protect {
                    continue;
                }
                // Cost + soft-refs come from the asset (Source chunks) or the chunk itself (adopted).
                let (cost, soft_refs) = if s.asset == NONE {
                    (s.cost, 0)
                } else if (s.asset as usize) < self.assets.len() {
                    let a = m_get(&self.assets[s.asset as usize]);
                    // Prefix-dependent assets: only the highest-index resident chunk is a candidate,
                    // so `on_evict` never discards a sibling chunk the manager still tracks.
                    if a.evict_tail_first && !self.is_highest_resident(s.asset, s.index) {
                        continue;
                    }
                    (a.source.cost, a.soft_refs)
                } else {
                    continue; // defensively skip an out-of-range asset index
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
            skip = Some(i); // commit bailed — exclude and re-scan
        }
        self.stat(|s| {
            s.scan_evict_calls += 1;
            s.scan_evict_slots += visited;
        });
        result
    }

    /// Evict the chunk at slot `i`: under a short masked window re-read + re-validate it is
    /// still evictable (unleased, non-dirty, same backing), clear it to EMPTY (before any
    /// callback — the sequenced-steal invariant), then run `on_evict` and free the backing
    /// UNMASKED. Returns false (no-op) if the slot re-validated as non-evictable under us.
    fn evict_slot(&self, i: usize) -> bool {
        // Masked commit: re-read, validate, capture, clear.
        let s = {
            let _m = Masked::enter();
            let s = self.chunks[i].get();
            if s.backing.is_null() || s.leases != 0 || s.dirty {
                return false; // changed under us since the scan — do not evict
            }
            // Unlink BEFORE clearing: index_remove walks the chain through `hash_next`, which
            // ChunkSlot::EMPTY would have wiped. Same masked window as the clear, so the slot and
            // the index are never separately visible (see index_insert).
            self.index_remove(i, s.asset, s.index);
            self.ptr_index_remove(i, s.backing);
            self.chunks[i].set(ChunkSlot::EMPTY);
            s
        };
        // Everything below runs UNMASKED (no mask across a callback or a free).
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
                // SAFETY: ctx/ptr were supplied at adopt; valid for the manager's lifetime.
                unsafe { cb(s.adopt_ctx, s.backing) };
            }
        } else if (s.asset as usize) < self.assets.len() {
            let a = m_get(&self.assets[s.asset as usize]);
            if let Some(cb) = a.source.on_evict {
                // SAFETY: owner/ctx come from the asset that owns this chunk.
                unsafe { cb(a.source.ctx, a.owner, s.index) };
            }
        }
        self.free_backing(s.backing, s.asset);
        true
    }

    /// Acquire chunk `index` of `asset` under a hard lease, materializing it if not
    /// resident. Returns the backing pointer, or null on OOM / reconstruction
    /// failure. A cache hit just adds a lease (no realloc, pointer stable).
    fn acquire(&self, asset: u32, index: u32, size: usize) -> *mut u8 {
        let ai = asset as usize;
        if ai >= self.assets.len() || !m_get(&self.assets[ai]).in_use {
            return ptr::null_mut();
        }
        self.stat(|s| s.acquires += 1);
        // Protect this asset's existing chunks from eviction for the duration (incl. the
        // reentrant reclaim hook during alloc_backing) — the dontStealFromThing port. Only
        // for opted-in (cache) assets; leased assets must stay able to evict their own old.
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
        // Allocate backing. May reentrantly evict *other* chunks via the heap reclaim
        // hook; `idx` is still empty (backing null) so it is not an eviction candidate.
        let p = self.alloc_backing(size, asset);
        if p.is_null() {
            return ptr::null_mut();
        }
        // Lease *before* materialize so a reentrant eviction (if materialize itself
        // allocates) can't steal this just-populated chunk.
        // ONE masked window over the slot write AND its index link: see index_insert for why
        // they must not be separately visible.
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
                // SAFETY: owner/ctx/dest come from this asset + the slot we just allocated.
                unsafe { f(a.source.ctx, a.owner, index, p, size) }
            }
            None => true,
        };
        if !ok {
            // Reconstruction failed (e.g. source vanished) — roll back the slot, unlinking it
            // first (index_remove reads `hash_next`, which EMPTY wipes).
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

    /// Add a hard lease to an already-resident chunk by its backing pointer (no
    /// materialize). The pointer-keyed counterpart to a cache-hit `acquire`, for callers
    /// that already hold the chunk and just want to pin it harder (C++ `Cluster::addReason`).
    /// No-op if the pointer isn't a resident chunk. `pub(crate)` so the safe `facade::Lease`
    /// RAII guard can take its one lease through the same path the C ABI wrapper uses.
    pub(crate) fn add_lease(&self, p: *mut u8) {
        let r = self.bump();
        self.rmw_by_ptr(p, |s| {
            s.leases += 1;
            s.recency = r;
        });
    }

    /// Reserve + construct (but do NOT load) chunk `index` of `asset` under a hard
    /// lease: allocate backing and run the `construct` callback (init the object, no
    /// I/O), leaving the data for an external loader to fill. The async counterpart to
    /// `acquire` — for prefetch, so the audio thread never blocks on I/O. A cache hit
    /// just leases (like `acquire`). Returns the backing pointer, or null on OOM, a
    /// full table with nothing evictable, or no `construct` callback on the asset.
    pub(crate) fn request(&self, asset: u32, index: u32, size: usize) -> *mut u8 {
        let ai = asset as usize;
        if ai >= self.assets.len() || !m_get(&self.assets[ai]).in_use {
            return ptr::null_mut();
        }
        if m_get(&self.assets[ai]).source.construct.is_none() {
            return ptr::null_mut(); // not a requestable asset
        }
        self.stat(|s| s.requests += 1);
        // Protect this asset's existing chunks from eviction while we allocate (the
        // dontStealFromThing port) — a cache writing cluster N+1 mustn't evict cluster N.
        // Only for opted-in (cache) assets.
        let prot = if m_get(&self.assets[ai]).self_protect {
            asset
        } else {
            NONE
        };
        let _g = self.protect_asset(prot);
        // Cache hit (already resident — constructed, maybe also loaded): just lease.
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
        // Lease before constructing (mirrors acquire: a reentrant eviction can't steal it).
        // ONE masked window over the slot write AND its index link (see index_insert).
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
        // SAFETY: owner/ctx/dest come from this asset + the slot we just allocated;
        // construct does no I/O and cannot fail (checked non-None above). Unmasked.
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

    /// Drop a specific resident chunk by its backing pointer: clear its slot and free the
    /// backing, *without* calling `on_evict` (the owner is deliberately discarding it — e.g.
    /// a SampleCache truncating its tail — so it manages its own pointer/state). No-op if
    /// `p` isn't resident. Distinct from `release` (which only drops a lease).
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
            self.index_remove(c, s.asset, s.index); // before the clear — EMPTY wipes hash_next
            self.ptr_index_remove(c, s.backing);
            self.chunks[c].set(ChunkSlot::EMPTY);
            s
        };
        self.free_backing(s.backing, s.asset); // unmasked (no mask across free)
    }

    /// Drop one hard lease on the chunk at `p` (it stays resident/cached until evicted).
    /// `pub(crate)` so `facade::Lease::drop` can release exactly the lease it took — already
    /// masked-safe (via `rmw_by_ptr`'s `Masked::enter`), so it may run from an ISR or the
    /// main thread with no extra locking.
    pub(crate) fn release(&self, p: *mut u8) {
        self.rmw_by_ptr(p, |s| {
            if s.leases > 0 {
                s.leases -= 1;
            }
        });
    }

    /// Adopt an externally-allocated heap block `ptr` as a resident, **unleased** chunk the
    /// manager may evict (value-scored by `cost` + recency). On eviction it calls
    /// `on_evict(ctx, ptr)` then frees `ptr`. For object-lifecycle blocks (AudioFile objects,
    /// GrainBuffer) the owner builds — `asset = NONE`, the chunk carries its own cost/evict.
    /// Returns `ptr` on success, or null if the chunk table is full and nothing is evictable.
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
        // ONE masked window over the slot write AND its pointer-index link (see index_insert).
        // Adopted chunks are absent from the KEY index (they all share `(NONE, 0)`) but present
        // in the pointer index — `evict_chunk` and `release` reach them by backing pointer.
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

    /// Mark a `request`ed (Loading) chunk ready — the loader / embassy storage task signals the read
    /// completed. No-op if `p` isn't a resident chunk.
    pub(crate) fn mark_ready(&self, p: *mut u8) {
        let mut filled: Option<u32> = None;
        self.rmw_by_ptr(p, |s| {
            s.ready = true;
            if s.queued_at != 0 {
                // SAFETY: a plain counter read provided by the C++ side; no invariants.
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

    /// Live readiness of the chunk at `p` — masked read of its `ready` flag,
    /// re-validated by pointer. `false` if `p` is no longer resident (evicted/never
    /// resident). The facade's `Resource::is_ready` query.
    pub(crate) fn is_ready_by_ptr(&self, p: *mut u8) -> bool {
        let Some(i) = self.find_by_ptr(p) else {
            return false;
        };
        let _m = Masked::enter();
        let s = self.chunks[i].get();
        s.backing == p && s.ready
    }

    /// RT-safe acquire: take a hard lease + return the backing only if the chunk is resident **and**
    /// ready (never allocates, never materializes, never blocks). Returns null otherwise — the caller
    /// (RT render / embassy path) must cope with a miss. Touches recency on a hit.
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

    /// Non-leasing residency peek: the backing pointer for `(asset, index)` if RESIDENT (backing
    /// non-null), regardless of `ready`/loaded state, else null. Unlike `try_acquire` it takes NO
    /// lease and does NOT bump recency — a peek must not perturb eviction ordering. Not ready-gated:
    /// a constructed-but-not-yet-loaded chunk is resident; callers check `->loaded` themselves.
    pub(crate) fn peek(&self, asset: u32, index: u32) -> *mut u8 {
        let Some(c) = self.find_resident(asset, index) else {
            return ptr::null_mut();
        };
        // Masked re-read: the slot could have been evicted/reused between find_resident and here;
        // re-validate identity (NOT ready) and return null on a race — never a stale pointer.
        let s = m_get(&self.chunks[c]);
        if s.backing.is_null() || s.asset != asset || s.index != index {
            return ptr::null_mut();
        }
        s.backing
    }

    fn define_asset(&self, owner: *mut c_void, source: Source) -> u32 {
        for i in 0..self.assets.len() {
            if m_get(&self.assets[i]).in_use {
                continue;
            }
            let claimed = m_rmw(&self.assets[i], |a| {
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

    /// Retire an asset: free any chunks of it still resident (notifying the owner via
    /// `on_evict`, as if each were evicted — so the owner drops its cached pointers),
    /// then mark the asset slot free for reuse. Called when the owner is destroyed
    /// (e.g. a `Sample` unloads). Leased chunks are freed too — at owner teardown there
    /// should be none, but we must not leak the backing or the slot.
    fn release_asset(&self, asset: u32) {
        // NOTE (design §4): release_asset is OWNER-TEARDOWN, not steady-state RT, and is
        // deliberately left UNGUARDED for now — the same masked pattern applies if wanted.
        let ai = asset as usize;
        if ai >= self.assets.len() || !self.assets[ai].get().in_use {
            return;
        }
        let a = self.assets[ai].get();
        // Helper: clear slot, fire on_evict, free backing for chunk at table-index `i`.
        let drop_chunk = |i: usize| {
            let s = self.chunks[i].get();
            // Clear the slot before the callback (consistent table for any reentrancy),
            // and free the backing *before* clearing the asset slot (free_backing reads
            // the asset's backing kind to route slab-vs-heap). Unlink first: index_remove
            // reads `hash_next`, which EMPTY wipes.
            self.index_remove(i, s.asset, s.index);
            self.ptr_index_remove(i, s.backing);
            self.chunks[i].set(ChunkSlot::EMPTY);
            if let Some(cb) = a.source.on_evict {
                // SAFETY: owner/ctx come from this asset; valid for the manager lifetime.
                unsafe { cb(a.source.ctx, a.owner, s.index) };
            }
            self.free_backing(s.backing, asset);
        };
        if a.evict_tail_first {
            // Prefix-dependent: free highest-index first so a cascading on_evict (which
            // discards higher-index siblings) never hits a chunk we still track.
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
            for i in 0..self.chunks.len() {
                let s = self.chunks[i].get();
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
    // SAFETY: `ctx` is the manager pointer we registered at create; it outlives the
    // heap. Taking a shared `&Manager` is sound even while another `&Manager` is live
    // (no `&mut` ever exists; mutation goes through `Cell`).
    let m = unsafe { &*(ctx as *mut Manager) };
    m.evict_lowest()
}

// ---- C ABI surface (the only FFI boundary; consumed by C++ / plugins) --------

/// Opaque manager handle.
#[repr(C)]
pub struct DelugeResource {
    _opaque: [u8; 0],
}

/// SAFETY: turn a non-null handle into a shared `&Manager` (see resource_reclaim).
/// `pub(crate)` so the safe `facade` module can build a `Resource` over the same
/// opaque handle the C ABI wrappers below use — same cast, same safety contract.
#[inline]
/// Test-only: assert the hash index and the chunk table describe the same set.
///
/// Two directions, because each catches a different bug: every occupied keyed slot must be
/// reachable from its own bucket exactly once (a missed insert or a lost link makes a resident
/// chunk invisible, so its caller allocates a duplicate), and every chain entry must point at an
/// occupied slot that really hashes to that bucket (a missed remove leaves a dangling link into a
/// recycled slot).
///
/// # Safety
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

    // Same two directions for the pointer index. Membership differs: EVERY occupied slot belongs
    // to it, adopted chunks included, because backing pointers are unique.
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

/// Test-only: does the (indexed) `find_resident` agree with the brute-force scan for this key?
/// Returns `(indexed, linear)` so a failing assertion can report both.
///
/// # Safety
/// `h` must be a live manager handle.
#[cfg(test)]
pub(crate) unsafe fn debug_find_resident_both(
    h: *mut DelugeResource,
    asset: u32,
    index: u32,
) -> (Option<usize>, Option<usize>) {
    // SAFETY: caller contract — `h` is a live handle, same as every other entry point here.
    let m = unsafe { mgr(h) };
    (
        m.find_resident(asset, index),
        m.find_resident_linear(asset, index),
    )
}

pub(crate) unsafe fn mgr<'a>(h: *mut DelugeResource) -> &'a Manager {
    &*(h as *mut Manager)
}

/// Build a manager over `heap` with fixed-capacity asset/chunk tables (allocated
/// once from the heap) and register it as the heap's reclaim hook. Returns null on
/// OOM. `chunk_cap` must exceed the most chunks that can be resident at once so the
/// table is never the limiter (like the slab).
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_create(
    heap: *mut DelugeHeap,
    asset_cap: usize,
    chunk_cap: usize,
) -> *mut DelugeResource {
    create_inner(heap, asset_cap, chunk_cap, true)
}

/// Like `deluge_resource_create` but does NOT register the heap reclaim hook — for
/// coexistence with another reclaim coordinator (the C++ CacheManager during the
/// raw-cluster migration), where the caller's own hook drives eviction by calling
/// `deluge_resource_try_evict` and then the other coordinator.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_create_unhooked(
    heap: *mut DelugeHeap,
    asset_cap: usize,
    chunk_cap: usize,
) -> *mut DelugeResource {
    create_inner(heap, asset_cap, chunk_cap, false)
}

/// Evict the single lowest-value evictable chunk (the reclaim-hook body, exposed so
/// an external coordinator can drive it). Returns true if something was freed.
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
    // Hash-index buckets: power of two >= chunk_cap, so bucket selection is a mask and the load
    // factor stays below 1 even with every slot occupied.
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
    // The tables live in the heap for the whole program (never freed), so 'static is
    // sound. From here on the slices are accessed only through safe code.
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

/// Configure the slab that backs BACKING_SLAB assets (uniform clusters). Until set,
/// all assets use the heap. Create it with `deluge_slab_create_unmanaged` over the
/// same heap so eviction stays with this manager (the slab self-evicts nothing).
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_set_slab(
    handle: *mut DelugeResource,
    slab: *mut DelugeSlab,
) {
    if !handle.is_null() {
        mgr(handle).set_slab(slab);
    }
}

/// Define an asset: an opaque `owner` token + its reconstruction `Source`. Returns
/// the asset id, or `0xFFFFFFFF` if the asset table is full. `backing` is
/// BACKING_HEAP (variable, from the TLSF heap) or BACKING_SLAB (uniform clusters).
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
            construct: None, // attach later via deluge_resource_set_construct if requestable
            ctx,
            cost,
            backing,
        },
    )
}

/// Retire an asset (free its resident chunks via `on_evict`, then free the slot for
/// reuse). Call when the owner is destroyed so the fixed-capacity asset table can't
/// exhaust over a long session. No-op on a null handle or an unused asset id.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_release_asset(handle: *mut DelugeResource, asset: u32) {
    if !handle.is_null() {
        mgr(handle).release_asset(asset);
    }
}

/// Attach (or clear) the async `construct` callback on an asset, making it requestable.
/// Call after `deluge_resource_define_asset`. Pass NULL to clear.
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

/// Mark an asset as prefix-dependent: its `on_evict` discards all higher-index chunks
/// (e.g. a SampleCache), so the manager only ever evicts the asset's *highest-index*
/// resident chunk — `on_evict` then never discards a chunk the manager still tracks.
/// Call after `define_asset`.
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

/// Mark an asset self-protecting: while it is allocating a chunk (`request`/`acquire`), its
/// own chunks are not eviction candidates (the `dontStealFromThing` port). For unleased
/// caches whose `request(N+1)` must not evict the just-written `N`. Call after define_asset.
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

/// Reserve + construct chunk `index` of `asset` under a hard lease *without* loading it
/// (runs the asset's `construct` callback, no I/O). The async/prefetch counterpart to
/// `acquire`: an external loader fills the data afterwards. A cache hit just leases.
/// Returns the backing pointer, or null on OOM / no `construct` callback.
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

/// Acquire chunk `index` of `asset` under a hard lease (materializing if needed).
/// Returns the backing pointer, or null on OOM / reconstruction failure.
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

/// RT-safe acquire: take a hard lease + return chunk `index` of `asset` only if it is resident **and**
/// ready (loaded). Never allocates, materializes, or blocks; returns null on a miss. The seam for the
/// RT render / embassy storage path — a `request`ed-but-not-yet-`mark_ready`'d chunk returns null.
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

/// Non-leasing residency peek: the backing pointer for `(asset, index)` if resident (ready OR not),
/// else null. Does NOT take a lease and does NOT bump recency — a peek must not perturb eviction
/// ordering. Callers that need loaded data check the chunk's own ready/loaded flag.
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

/// Mark a `request`ed (Loading) chunk ready — called when the read completes (the C++ loader after
/// `readClusterData`, or an embassy storage task after its DMA `.await`). No-op if `ptr` isn't resident.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_mark_ready(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).mark_ready(ptr);
    }
}

/// The chunk-table slot index backing `ptr`, or `DELUGE_RESOURCE_NO_SLOT` if `ptr` isn't resident.
/// O(n); the caller caches the result at chunk creation so later lease reads use `lease_count_by_slot`.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_slot_of(handle: *mut DelugeResource, ptr: *mut u8) -> u32 {
    if handle.is_null() {
        return NO_SLOT;
    }
    mgr(handle).slot_of(ptr)
}

/// The `(asset, index)` identity of the resident chunk backing `ptr`, written to `*out_asset`/
/// `*out_index` — `true` on a hit, `false` (leaving the out-params untouched) if `ptr` isn't resident
/// or either pointer is null. C-ABI mirror of `slot_of` just above (same `find_by_ptr` lookup), out-
/// param shaped like `deluge_resource_stats` (a Rust `(u32, u32)` has no direct C-ABI return shape).
/// Exposes the facade's `Resource::chunk_ident` (`facade.rs`) — used by the native fill task
/// (`streaming_loader.rs`), which recovers a loader-queue chunk's `(asset, index)` to
/// look up its per-asset fill-context (`fill_context_for`).
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
    // SAFETY: `out_asset`/`out_index` are caller-provided non-null `u32` out-params (checked above).
    unsafe {
        *out_asset = asset;
        *out_index = index;
    }
    true
}

/// O(1) hard-lease count of the chunk at `slot` — 0 if `slot` is `NO_SLOT` / out of range / free. The
/// single source of truth for "how many reasons does this cluster have", read via the C++ slot handle.
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

/// Hard leases on the chunk at `slot` excluding the load queue's own (see
/// `deluge_resource_loader_enqueue_owned`) — "does any consumer besides the queue still want this?".
/// Same value as `deluge_resource_lease_count_by_slot` for a chunk the queue holds no lease on. The
/// async drain's abandonment check: with a queue-owned lease held, the raw count never reaches 0.
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

// ---- cluster load queue (per-slot; the C++ ClusterPriorityQueue moved into the manager) -----------

/// Enqueue the chunk at `slot` for loading at `priority` (lower = more urgent; re-enqueue updates it).
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

/// Enqueue the chunk at `slot` at `priority` AND take a queue-owned hard lease, so the entry
/// survives the enqueuer dropping its own lease — for a caller that cannot hold a lease until the
/// load lands (`deluge_resource_loader_next` serves only leased chunks and silently discards the
/// rest). Idempotent in the lease. Every terminal path of the loader MUST pair this with
/// `deluge_resource_loader_release_owned`; a stranded queue lease pins the chunk forever.
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

/// Release the queue-owned lease taken by `deluge_resource_loader_enqueue_owned`. Returns whether
/// there was one to release (false for a chunk enqueued under the plain protocol, whose enqueuer
/// owns its own lease). Idempotent; does not de-queue.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_release_owned(
    handle: *mut DelugeResource,
    slot: u32,
) -> bool {
    !handle.is_null() && mgr(handle).loader_release_owned(slot)
}

/// Remove the chunk at `slot` from the load queue (the C++ `erase`). Also drops any queue-owned
/// lease, so an erased entry cannot leave the chunk pinned.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_remove(handle: *mut DelugeResource, slot: u32) {
    if !handle.is_null() {
        mgr(handle).loader_remove(slot);
    }
}

/// Pop the most-urgent queued + still-leased chunk's backing ptr (clearing its queued flag), or null.
/// Queued-but-unleased chunks are de-queued + left for normal eviction (not destroyed here).
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_next(handle: *mut DelugeResource) -> *mut u8 {
    if handle.is_null() {
        return ptr::null_mut();
    }
    mgr(handle).loader_next()
}

/// Whether any queued + leased chunk sits at the lowest priority (u32::MAX) — the load-song yield gate.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_has_lowest(handle: *mut DelugeResource) -> bool {
    !handle.is_null() && mgr(handle).loader_has_lowest()
}

/// Whether any queued + leased chunk remains at all (any priority) — the non-destructive
/// loader-queue-non-empty predicate the offline async drain blocks on. Answers "would
/// `deluge_resource_loader_next` return non-null" without popping or mutating anything.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_loader_has_any(handle: *mut DelugeResource) -> bool {
    !handle.is_null() && mgr(handle).loader_has_any()
}

/// Copy the manager's cumulative instrumentation counters into `*out` (see `Stats` /
/// `DelugeResourceStats`). No-op if either pointer is null.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_stats(handle: *mut DelugeResource, out: *mut Stats) {
    if handle.is_null() || out.is_null() {
        return;
    }
    // SAFETY: `out` is a caller-provided DelugeResourceStats (layout matches Stats).
    unsafe { *out = mgr(handle).stats_snapshot() };
}

/// Zero the manager's instrumentation counters (e.g. to measure a single render in isolation).
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_stats_reset(handle: *mut DelugeResource) {
    if !handle.is_null() {
        mgr(handle).stats_reset();
    }
}

/// Drop a specific resident chunk by its backing pointer (clear slot + free backing),
/// *without* calling its `on_evict` — for an owner deliberately discarding a chunk it
/// manages (e.g. a SampleCache truncating its tail). No-op if `ptr` isn't resident.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_evict_chunk(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).evict_chunk(ptr);
    }
}

/// Adopt an externally-allocated heap block as a manager-evictable (object-lifecycle) chunk:
/// the owner allocated + built it; the manager owns only its eviction (value-scored by cost-per-byte
/// and recency — so pass the block's `size` in bytes). On eviction it calls `on_evict(ctx, ptr)` then
/// frees `ptr`. Registered unleased (pin via `deluge_resource_add_lease`). Returns `ptr`, or null if
/// the chunk table is full and nothing is evictable. The adopt counterpart to define_asset+acquire.
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

/// Add a hard lease to an already-resident chunk by its backing pointer (no
/// re-materialize) — the pointer-keyed counterpart to a cache-hit `acquire`, for a
/// caller that already holds the chunk and wants to pin it harder. No-op if `ptr` is
/// not a resident chunk.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_add_lease(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).add_lease(ptr);
    }
}

/// Drop one hard lease on the chunk at `ptr` (it stays resident/cached until evicted).
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_release(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).release(ptr);
    }
}

/// Soft-reference / un-reference an asset (project relevance — raises eviction
/// priority; does not pin).
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_reference(handle: *mut DelugeResource, asset: u32) {
    if !handle.is_null() {
        mgr(handle).reference(asset, 1);
    }
}
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_unreference(handle: *mut DelugeResource, asset: u32) {
    if !handle.is_null() {
        mgr(handle).reference(asset, -1);
    }
}

/// Mark the chunk at `ptr` most-recently-used.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_touch(handle: *mut DelugeResource, ptr: *mut u8) {
    if !handle.is_null() {
        mgr(handle).touch(ptr);
    }
}

/// Mark/clear the chunk at `ptr` as dirty (unsaved ⇒ never evicted until flushed).
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

    /// A manager over a throwaway 16-aligned test heap (mirrors the `arena()` harness
    /// in `lib.rs`'s own `mod tests` / `facade.rs`'s `test_resource()`), sized with a
    /// **single** chunk slot so a second distinct `request` MUST evict + reuse slot 0
    /// — the only way to deterministically exercise a generation bump without relying
    /// on the value function's eviction order across a bigger table.
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

    /// Define a requestable (has a `construct` callback, no I/O) test asset — the
    /// `request`/prefetch path this task's token is minted from.
    fn define_requestable_test_asset(handle: *mut DelugeResource) -> u32 {
        // SAFETY: `handle` is a live handle from `test_manager` for the duration of
        // this call.
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

        // Release + force reuse: with a 1-slot chunk table, a distinct-index request
        // MUST evict slot0 (now unleased) and reuse the same slot for a new chunk.
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

        // A retain keyed on the STALE (slot0, gen0) must be a checked no-op.
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

        // A retain on the CURRENT handle succeeds and balances with release.
        assert!(m.retain_by_slot_gen(slot1, gen1));
        assert_eq!(m.lease_count_by_slot(slot1), before + 1);
        assert!(m.release_by_slot_gen(slot1, gen1));
        assert_eq!(m.lease_count_by_slot(slot1), before);
    }
}
