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

use crate::value::evict_rank;
use core::cell::Cell;
use core::ffi::c_void;
use core::ptr;
use deluge_alloc::slab::{deluge_slab_acquire, deluge_slab_release, DelugeSlab};
use deluge_alloc::{deluge_alloc, deluge_free, deluge_heap_register_reclaim, DelugeHeap};

const NONE: u32 = u32::MAX;
/// Invalid chunk-slot index (the C ABI's `DELUGE_RESOURCE_NO_SLOT`): a C++ object's slot handle before
/// its chunk is created, or `slot_of` on a non-resident pointer.
const NO_SLOT: u32 = u32::MAX;

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
    // Adopt mode (asset == NONE): the chunk carries its own cost + evict callback, because
    // an adopted block is an externally-allocated object, not a chunk of a Source asset.
    // The owner allocated it; the manager only owns its eviction. (Unused when asset != NONE.)
    cost: u32,
    adopt_evict: Option<AdoptEvictFn>,
    adopt_ctx: *mut c_void,
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
        cost: 0,
        adopt_evict: None,
        adopt_ctx: ptr::null_mut(),
    };
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
    tick: Cell<u64>,
    /// Transient self-protection (the `dontStealFromThing` port): while an asset is
    /// allocating a chunk (`request`/`acquire`), its own chunks are not eviction
    /// candidates — so allocating cluster N+1 can't steal the just-written N. `NONE`
    /// outside such a call. Set via the `ProtectGuard` RAII helper.
    protect: Cell<u32>,
}

/// Restores `Manager::protect` on drop — so every early return from `request`/`acquire`
/// clears the self-protection.
struct ProtectGuard<'a> {
    mgr: &'a Manager,
    prev: u32,
}
impl Drop for ProtectGuard<'_> {
    fn drop(&mut self) {
        self.mgr.protect.set(self.prev);
    }
}

impl Manager {
    #[inline]
    fn bump(&self) -> u64 {
        let t = self.tick.get() + 1;
        self.tick.set(t);
        t
    }

    fn find_resident(&self, asset: u32, index: u32) -> Option<usize> {
        self.chunks.iter().position(|c| {
            let s = c.get();
            !s.backing.is_null() && s.asset == asset && s.index == index
        })
    }
    fn find_by_ptr(&self, p: *mut u8) -> Option<usize> {
        self.chunks.iter().position(|c| c.get().backing == p)
    }
    fn find_free_chunk(&self) -> Option<usize> {
        self.chunks.iter().position(|c| c.get().backing.is_null())
    }

    /// The chunk-table slot index backing `p`, or `NO_SLOT` if `p` isn't resident. O(n); the C++ side
    /// caches the result at chunk creation so subsequent lease reads go through `lease_count_by_slot`.
    fn slot_of(&self, p: *mut u8) -> u32 {
        match self.find_by_ptr(p) {
            Some(i) => i as u32,
            None => NO_SLOT,
        }
    }

    /// O(1) hard-lease count of the chunk at `slot` — 0 if `slot` is out of range or the slot is free.
    /// The C++ object holds its slot index (a handle), so the loading queue / invariant checks read the
    /// lease count without an O(n) `find_by_ptr` scan.
    fn lease_count_by_slot(&self, slot: u32) -> u32 {
        let i = slot as usize;
        if i >= self.chunks.len() {
            return 0;
        }
        let s = self.chunks[i].get();
        if s.backing.is_null() {
            return 0;
        }
        s.leases
    }

    /// Is `index` the highest-index resident chunk of `asset`? (No resident chunk of the
    /// asset has a greater index.) Used to gate tail-first eviction.
    fn is_highest_resident(&self, asset: u32, index: u32) -> bool {
        !self.chunks.iter().any(|c| {
            let s = c.get();
            !s.backing.is_null() && s.asset == asset && s.index > index
        })
    }

    /// Set the transient self-protection to `asset`, restoring the previous value on drop.
    fn protect_asset(&self, asset: u32) -> ProtectGuard<'_> {
        let prev = self.protect.get();
        self.protect.set(asset);
        ProtectGuard { mgr: self, prev }
    }

    /// Does this asset want slab backing, and is a slab configured? `NONE` (an adopted
    /// chunk) is always heap-backed (the owner allocated it from the heap).
    #[inline]
    fn slab_for(&self, asset: u32) -> *mut DelugeSlab {
        let slab = self.slab.get();
        if asset != NONE
            && !slab.is_null()
            && self.assets[asset as usize].get().source.backing == BACKING_SLAB
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
        if !slab.is_null() {
            // SAFETY: `slab` is a live unmanaged slab over the same heap; uniform slot
            // size, so `size` is implicit. owner is unused (no slab self-eviction).
            return unsafe { deluge_slab_acquire(slab, ptr::null_mut()) };
        }
        // SAFETY: `self.heap` is a live heap handle (checked at create).
        unsafe { deluge_alloc(self.heap, size, 16) }
    }

    /// Evict the lowest-value evictable (unleased, non-dirty) chunk: notify its
    /// owner, return its backing to the heap, free the slot. Returns false if
    /// nothing is evictable. Used both as the heap reclaim hook and to free a slot.
    fn evict_lowest(&self) -> bool {
        let mut best: Option<usize> = None;
        let mut best_rank = (u8::MAX, u32::MAX, u64::MAX);
        let protect = self.protect.get();
        for (i, c) in self.chunks.iter().enumerate() {
            let s = c.get();
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
            } else {
                let a = self.assets[s.asset as usize].get();
                // Prefix-dependent assets: only the highest-index resident chunk is a candidate,
                // so `on_evict` never discards a sibling chunk the manager still tracks.
                if a.evict_tail_first && !self.is_highest_resident(s.asset, s.index) {
                    continue;
                }
                (a.source.cost, a.soft_refs)
            };
            let rank = evict_rank(soft_refs, cost, s.recency);
            if rank < best_rank {
                best_rank = rank;
                best = Some(i);
            }
        }
        let Some(i) = best else { return false };
        let s = self.chunks[i].get();
        // Clear the slot *before* the callbacks so a reentrant acquire/evict sees a
        // consistent table (sequenced steal).
        self.chunks[i].set(ChunkSlot::EMPTY);
        if s.asset == NONE {
            // Adopted chunk: its own evict callback, given the block pointer directly.
            if let Some(cb) = s.adopt_evict {
                // SAFETY: ctx/ptr were supplied at adopt; valid for the manager's lifetime.
                unsafe { cb(s.adopt_ctx, s.backing) };
            }
        } else {
            let a = self.assets[s.asset as usize].get();
            if let Some(cb) = a.source.on_evict {
                // SAFETY: owner/ctx come from the asset that owns this chunk; the C side
                // promises the callback is valid for the manager's lifetime.
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
        if ai >= self.assets.len() || !self.assets[ai].get().in_use {
            return ptr::null_mut();
        }
        // Protect this asset's existing chunks from eviction for the duration (incl. the
        // reentrant reclaim hook during alloc_backing) — the dontStealFromThing port. Only
        // for opted-in (cache) assets; leased assets must stay able to evict their own old.
        let prot = if self.assets[ai].get().self_protect {
            asset
        } else {
            NONE
        };
        let _g = self.protect_asset(prot);
        // Cache hit.
        if let Some(c) = self.find_resident(asset, index) {
            let mut s = self.chunks[c].get();
            s.leases += 1;
            s.recency = self.bump();
            self.chunks[c].set(s);
            return s.backing;
        }
        // Reserve a free slot (evicting the lowest-value chunk if the table is full).
        let idx = match self.find_free_chunk() {
            Some(i) => i,
            None => {
                if !self.evict_lowest() {
                    return ptr::null_mut(); // table full and nothing evictable
                }
                self.find_free_chunk().expect("slot freed by eviction")
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
        self.chunks[idx].set(ChunkSlot {
            backing: p,
            asset,
            index,
            leases: 1,
            dirty: false,
            ready: true, // acquire materializes synchronously below, before anyone else can run
            recency: self.bump(),
            ..ChunkSlot::EMPTY
        });
        let a = self.assets[ai].get();
        let ok = match a.source.materialize {
            // SAFETY: owner/ctx/dest come from this asset + the slot we just allocated.
            Some(f) => unsafe { f(a.source.ctx, a.owner, index, p, size) },
            None => true,
        };
        if !ok {
            // Reconstruction failed (e.g. source vanished) — roll back the slot.
            self.chunks[idx].set(ChunkSlot::EMPTY);
            self.free_backing(p, asset);
            return ptr::null_mut();
        }
        p
    }

    /// Add a hard lease to an already-resident chunk by its backing pointer (no
    /// materialize). The pointer-keyed counterpart to a cache-hit `acquire`, for callers
    /// that already hold the chunk and just want to pin it harder (C++ `Cluster::addReason`).
    /// No-op if the pointer isn't a resident chunk.
    fn add_lease(&self, p: *mut u8) {
        if let Some(c) = self.find_by_ptr(p) {
            let mut s = self.chunks[c].get();
            s.leases += 1;
            s.recency = self.bump();
            self.chunks[c].set(s);
        }
    }

    /// Reserve + construct (but do NOT load) chunk `index` of `asset` under a hard
    /// lease: allocate backing and run the `construct` callback (init the object, no
    /// I/O), leaving the data for an external loader to fill. The async counterpart to
    /// `acquire` — for prefetch, so the audio thread never blocks on I/O. A cache hit
    /// just leases (like `acquire`). Returns the backing pointer, or null on OOM, a
    /// full table with nothing evictable, or no `construct` callback on the asset.
    fn request(&self, asset: u32, index: u32, size: usize) -> *mut u8 {
        let ai = asset as usize;
        if ai >= self.assets.len() || !self.assets[ai].get().in_use {
            return ptr::null_mut();
        }
        if self.assets[ai].get().source.construct.is_none() {
            return ptr::null_mut(); // not a requestable asset
        }
        // Protect this asset's existing chunks from eviction while we allocate (the
        // dontStealFromThing port) — a cache writing cluster N+1 mustn't evict cluster N.
        // Only for opted-in (cache) assets.
        let prot = if self.assets[ai].get().self_protect {
            asset
        } else {
            NONE
        };
        let _g = self.protect_asset(prot);
        // Cache hit (already resident — constructed, maybe also loaded): just lease.
        if let Some(c) = self.find_resident(asset, index) {
            let mut s = self.chunks[c].get();
            s.leases += 1;
            s.recency = self.bump();
            self.chunks[c].set(s);
            return s.backing;
        }
        let idx = match self.find_free_chunk() {
            Some(i) => i,
            None => {
                if !self.evict_lowest() {
                    return ptr::null_mut();
                }
                self.find_free_chunk().expect("slot freed by eviction")
            }
        };
        let p = self.alloc_backing(size, asset);
        if p.is_null() {
            return ptr::null_mut();
        }
        // Lease before constructing (mirrors acquire: a reentrant eviction can't steal it).
        self.chunks[idx].set(ChunkSlot {
            backing: p,
            asset,
            index,
            leases: 1,
            dirty: false,
            recency: self.bump(),
            ..ChunkSlot::EMPTY
        });
        let a = self.assets[ai].get();
        // SAFETY: owner/ctx/dest come from this asset + the slot we just allocated.
        // construct does no I/O and cannot fail (checked non-None above).
        unsafe { (a.source.construct.unwrap())(a.source.ctx, a.owner, index, p) };
        p
    }

    fn set_construct(&self, asset: u32, construct: Option<ConstructFn>) {
        let ai = asset as usize;
        if ai >= self.assets.len() {
            return;
        }
        let mut a = self.assets[ai].get();
        a.source.construct = construct;
        self.assets[ai].set(a);
    }

    fn set_evict_tail_first(&self, asset: u32, on: bool) {
        let ai = asset as usize;
        if ai >= self.assets.len() {
            return;
        }
        let mut a = self.assets[ai].get();
        a.evict_tail_first = on;
        self.assets[ai].set(a);
    }

    fn set_self_protect(&self, asset: u32, on: bool) {
        let ai = asset as usize;
        if ai >= self.assets.len() {
            return;
        }
        let mut a = self.assets[ai].get();
        a.self_protect = on;
        self.assets[ai].set(a);
    }

    /// Drop a specific resident chunk by its backing pointer: clear its slot and free the
    /// backing, *without* calling `on_evict` (the owner is deliberately discarding it — e.g.
    /// a SampleCache truncating its tail — so it manages its own pointer/state). No-op if
    /// `p` isn't resident. Distinct from `release` (which only drops a lease).
    fn evict_chunk(&self, p: *mut u8) {
        if let Some(c) = self.find_by_ptr(p) {
            let s = self.chunks[c].get();
            self.chunks[c].set(ChunkSlot::EMPTY);
            self.free_backing(s.backing, s.asset);
        }
    }

    fn release(&self, p: *mut u8) {
        if let Some(c) = self.find_by_ptr(p) {
            let mut s = self.chunks[c].get();
            if s.leases > 0 {
                s.leases -= 1;
                self.chunks[c].set(s);
            }
        }
    }

    /// Adopt an externally-allocated heap block `ptr` as a resident, **unleased** chunk the
    /// manager may evict (value-scored by `cost` + recency). On eviction it calls
    /// `on_evict(ctx, ptr)` then frees `ptr`. For object-lifecycle blocks (AudioFile objects,
    /// GrainBuffer) the owner builds — `asset = NONE`, the chunk carries its own cost/evict.
    /// Returns `ptr` on success, or null if the chunk table is full and nothing is evictable.
    fn adopt(&self, ptr: *mut u8, cost: u32, ctx: *mut c_void, on_evict: Option<AdoptEvictFn>) -> *mut u8 {
        if ptr.is_null() {
            return ptr::null_mut();
        }
        let idx = match self.find_free_chunk() {
            Some(i) => i,
            None => {
                if !self.evict_lowest() {
                    return ptr::null_mut();
                }
                self.find_free_chunk().expect("slot freed by eviction")
            }
        };
        self.chunks[idx].set(ChunkSlot {
            backing: ptr,
            asset: NONE,
            index: 0,
            leases: 0,
            dirty: false,
            ready: true, // owner-built object, usable immediately
            recency: self.bump(),
            cost,
            adopt_evict: on_evict,
            adopt_ctx: ctx,
        });
        ptr
    }

    fn touch(&self, p: *mut u8) {
        if let Some(c) = self.find_by_ptr(p) {
            let mut s = self.chunks[c].get();
            s.recency = self.bump();
            self.chunks[c].set(s);
        }
    }

    fn set_dirty(&self, p: *mut u8, dirty: bool) {
        if let Some(c) = self.find_by_ptr(p) {
            let mut s = self.chunks[c].get();
            s.dirty = dirty;
            self.chunks[c].set(s);
        }
    }

    /// Mark a `request`ed (Loading) chunk ready — the loader / embassy storage task signals the read
    /// completed. No-op if `p` isn't a resident chunk.
    fn mark_ready(&self, p: *mut u8) {
        if let Some(c) = self.find_by_ptr(p) {
            let mut s = self.chunks[c].get();
            s.ready = true;
            self.chunks[c].set(s);
        }
    }

    /// RT-safe acquire: take a hard lease + return the backing only if the chunk is resident **and**
    /// ready (never allocates, never materializes, never blocks). Returns null otherwise — the caller
    /// (RT render / embassy path) must cope with a miss. Touches recency on a hit.
    fn try_acquire(&self, asset: u32, index: u32) -> *mut u8 {
        match self.find_resident(asset, index) {
            Some(c) => {
                let mut s = self.chunks[c].get();
                if !s.ready {
                    return ptr::null_mut();
                }
                s.leases += 1;
                s.recency = self.bump();
                self.chunks[c].set(s);
                s.backing
            }
            None => ptr::null_mut(),
        }
    }

    fn define_asset(&self, owner: *mut c_void, source: Source) -> u32 {
        for (i, cell) in self.assets.iter().enumerate() {
            let mut a = cell.get();
            if !a.in_use {
                a.in_use = true;
                a.owner = owner;
                a.source = source;
                a.soft_refs = 0;
                cell.set(a);
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
            // the asset's backing kind to route slab-vs-heap).
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
        self.slab.set(slab);
    }

    fn reference(&self, asset: u32, delta: i32) {
        let ai = asset as usize;
        if ai >= self.assets.len() {
            return;
        }
        let mut a = self.assets[ai].get();
        if delta > 0 {
            a.soft_refs += delta as u32;
        } else {
            a.soft_refs = a.soft_refs.saturating_sub((-delta) as u32);
        }
        self.assets[ai].set(a);
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
#[inline]
unsafe fn mgr<'a>(h: *mut DelugeResource) -> &'a Manager {
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
    if m.is_null() || assets_raw.is_null() || chunks_raw.is_null() {
        deluge_free(heap, m as *mut u8);
        deluge_free(heap, assets_raw as *mut u8);
        deluge_free(heap, chunks_raw as *mut u8);
        return ptr::null_mut();
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
    ptr::write(
        m,
        Manager {
            heap,
            slab: Cell::new(ptr::null_mut()),
            assets,
            chunks,
            tick: Cell::new(0),
            protect: Cell::new(NONE),
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

/// O(1) hard-lease count of the chunk at `slot` — 0 if `slot` is `NO_SLOT` / out of range / free. The
/// single source of truth for "how many reasons does this cluster have", read via the C++ slot handle.
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_lease_count_by_slot(handle: *mut DelugeResource, slot: u32) -> u32 {
    if handle.is_null() {
        return 0;
    }
    mgr(handle).lease_count_by_slot(slot)
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
/// the owner allocated + built it; the manager owns only its eviction (value-scored by `cost`
/// + recency). On eviction it calls `on_evict(ctx, ptr)` then frees `ptr`. Registered unleased
/// (pin via `deluge_resource_add_lease`). Returns `ptr`, or null if the chunk table is full and
/// nothing is evictable. The adopt counterpart to define_asset+acquire (Source mode).
#[no_mangle]
pub unsafe extern "C" fn deluge_resource_adopt(
    handle: *mut DelugeResource,
    ptr: *mut u8,
    cost: u32,
    ctx: *mut c_void,
    on_evict: Option<AdoptEvictFn>,
) -> *mut u8 {
    if handle.is_null() {
        return ptr::null_mut();
    }
    mgr(handle).adopt(ptr, cost, ctx, on_evict)
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
