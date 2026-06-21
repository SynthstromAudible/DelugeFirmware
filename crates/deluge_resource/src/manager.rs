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

/// Where a chunk's backing comes from — uniform clusters use the slab, variable
/// assets use the heap directly. Plain `u32` to cross the C ABI (no C enum).
pub const BACKING_HEAP: u32 = 0;
pub const BACKING_SLAB: u32 = 1;

#[derive(Clone, Copy)]
pub struct Source {
    pub materialize: Option<MaterializeFn>,
    pub on_evict: Option<EvictFn>,
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
}
impl AssetSlot {
    const EMPTY: AssetSlot = AssetSlot {
        in_use: false,
        owner: ptr::null_mut(),
        source: Source {
            materialize: None,
            on_evict: None,
            ctx: ptr::null_mut(),
            cost: 0,
            backing: BACKING_HEAP,
        },
        soft_refs: 0,
    };
}

#[derive(Clone, Copy)]
struct ChunkSlot {
    backing: *mut u8, // null => free slot
    asset: u32,       // index into the asset table
    index: u32,       // chunk index within the asset
    size: usize,
    leases: u32, // hard leases; > 0 ⇒ never evicted
    dirty: bool, // unsaved (e.g. a recording) ⇒ never evicted
    recency: u64,
}
impl ChunkSlot {
    const EMPTY: ChunkSlot = ChunkSlot {
        backing: ptr::null_mut(),
        asset: NONE,
        index: 0,
        size: 0,
        leases: 0,
        dirty: false,
        recency: 0,
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

    /// Does this asset want slab backing, and is a slab configured?
    #[inline]
    fn slab_for(&self, asset: u32) -> *mut DelugeSlab {
        let slab = self.slab.get();
        if !slab.is_null() && self.assets[asset as usize].get().source.backing == BACKING_SLAB {
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
        for (i, c) in self.chunks.iter().enumerate() {
            let s = c.get();
            if s.backing.is_null() || s.leases != 0 || s.dirty {
                continue;
            }
            let a = self.assets[s.asset as usize].get();
            let rank = evict_rank(a.soft_refs, a.source.cost, s.recency);
            if rank < best_rank {
                best_rank = rank;
                best = Some(i);
            }
        }
        let Some(i) = best else { return false };
        let s = self.chunks[i].get();
        let a = self.assets[s.asset as usize].get();
        // Clear the slot *before* the callbacks so a reentrant acquire/evict sees a
        // consistent table (sequenced steal).
        self.chunks[i].set(ChunkSlot::EMPTY);
        if let Some(cb) = a.source.on_evict {
            // SAFETY: owner/ctx come from the asset that owns this chunk; the C side
            // promises the callback is valid for the manager's lifetime.
            unsafe { cb(a.source.ctx, a.owner, s.index) };
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
            size,
            leases: 1,
            dirty: false,
            recency: self.bump(),
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

    fn release(&self, p: *mut u8) {
        if let Some(c) = self.find_by_ptr(p) {
            let mut s = self.chunks[c].get();
            if s.leases > 0 {
                s.leases -= 1;
                self.chunks[c].set(s);
            }
        }
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
        },
    );
    deluge_heap_register_reclaim(heap, resource_reclaim, m as *mut c_void);
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
            ctx,
            cost,
            backing,
        },
    )
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
