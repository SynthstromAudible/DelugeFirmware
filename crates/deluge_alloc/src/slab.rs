//! Slab cache pool — the "stealables" pager, layered on the TLSF heap.
//!
//! A fixed-capacity table of uniform-size slots whose backing blocks are borrowed
//! from, and returned to, the TLSF heap. The pool registers itself as the heap's
//! reclaim hook (tlsf::ReclaimFn): when a *live* allocation can't be satisfied,
//! the heap calls back here, the pool evicts its coldest **unpinned** slot
//! (notifying the owner — the analogue of `Stealable::steal()`), returns that
//! block to the heap, and the allocation retries. Uniform slots make eviction
//! O(1)-ish (a bounded scan) and unfragmentable — the redesign's
//! slab-backed-by-TLSF synthesis (docs/dev/allocator_redesign.md).
//!
//! Pinning (`numReasonsToBeLoaded` in C++) protects a slot a voice is mid-read of.

use crate::section::{Section, STEP_SLAB};
use crate::tlsf::ALIGN;
use crate::{deluge_alloc, deluge_free, deluge_heap_register_reclaim, DelugeHeap};
use core::ffi::c_void;
use core::ptr;
use core::sync::atomic::{AtomicPtr, Ordering};

/// Eviction notification: a slot the owner holds is being reclaimed (the owner
/// must drop its reference and reload from backing store later). Analogue of
/// `Stealable::steal()`.
pub type EvictFn = unsafe extern "C" fn(owner: *mut c_void);

/// A table entry. `backing` is atomic because the table is scanned outside any section while another context may
/// claim or clear an entry inside one (see crate::section); every other field is read and written only inside a
/// section.
struct Slot {
    backing: AtomicPtr<u8>, // null => free table entry
    owner: *mut c_void,
    pinned: u32,
    lru: u64,
}

#[repr(C)]
pub struct SlabPool {
    heap: *mut DelugeHeap,
    slot_size: usize,
    capacity: usize,
    slots: *mut Slot, // `capacity` entries, allocated from the heap at create
    tick: u64,
    on_evict: Option<EvictFn>,
    /// Managed (`true`): the pool owns eviction — it registers itself as the heap's
    /// reclaim hook and, when its table is full, evicts its own coldest unpinned slot
    /// (LRU). Unmanaged (`false`): an *external* coordinator owns eviction (e.g. the
    /// C++ CacheManager priority queues drive the heap's reclaim hook); the pool is
    /// backing-only — it never registers a hook and never self-evicts. The cluster
    /// pool is unmanaged (docs/dev/allocator_sdram_strangle.md, step 7).
    self_reclaim: bool,
    /// Zero each slot's bytes on acquire. Off by default (firmware: leave recycled content
    /// for speed). The sim/golden build turns it on (DELUGE_DETERMINISTIC_ALLOC) so a
    /// read-before-write of slot memory — e.g. a SampleCache reading interpolation-overhang
    /// bytes past its write position — resolves to a defined 0 instead of layout-dependent
    /// recycled content, keeping the offline render independent of heap layout.
    zero_on_acquire: bool,
}

impl SlabPool {
    #[inline]
    unsafe fn slot(this: *mut SlabPool, i: usize) -> *mut Slot {
        (*this).slots.add(i)
    }

    #[inline]
    fn bump(this: *mut SlabPool) -> u64 {
        unsafe {
            (*this).tick += 1;
            (*this).tick
        }
    }

    #[inline]
    unsafe fn backing(this: *mut SlabPool, i: usize) -> *mut u8 {
        (*Self::slot(this, i)).backing.load(Ordering::Relaxed)
    }

    /// First free entry, scanned outside any section: only a hint until re-checked inside one.
    unsafe fn find_free(this: *mut SlabPool) -> Option<usize> {
        (0..(*this).capacity).find(|&i| Self::backing(this, i).is_null())
    }

    /// The entry holding `p`, scanned outside any section. A slot has one owner, so while its owner is the caller the
    /// entry cannot change under it; callers still re-check inside the section they act in.
    unsafe fn find_by_ptr(this: *mut SlabPool, p: *mut u8) -> Option<usize> {
        (0..(*this).capacity).find(|&i| Self::backing(this, i) == p)
    }

    /// Evict the coldest occupied **unpinned** slot: notify its owner, return its backing to the heap, and free the
    /// table entry. Returns false if nothing is evictable (everything pinned/empty). Used both as the heap reclaim
    /// hook and to make room in a full table. The scan runs outside any section; the entry is re-checked and cleared
    /// inside one (rescanning if another context changed it), and the callback and the free run after it.
    unsafe fn evict_coldest(this: *mut SlabPool) -> bool {
        loop {
            let mut best: Option<(usize, *mut u8)> = None;
            let mut best_lru = u64::MAX;
            for i in 0..(*this).capacity {
                let s = Self::slot(this, i);
                let backing = (*s).backing.load(Ordering::Relaxed);
                if !backing.is_null() && (*s).pinned == 0 && (*s).lru < best_lru {
                    best_lru = (*s).lru;
                    best = Some((i, backing));
                }
            }
            let Some((i, backing)) = best else {
                return false;
            };
            let owner = {
                let _s = Section::enter(STEP_SLAB);
                let s = Self::slot(this, i);
                if (*s).backing.load(Ordering::Relaxed) != backing || (*s).pinned != 0 {
                    continue; // another context took or pinned it: rescan
                }
                // Clear the entry *before* the callbacks so a reentrant acquire/free sees a consistent table
                // (sequenced steal, redesign §2).
                let owner = (*s).owner;
                (*s).backing.store(ptr::null_mut(), Ordering::Relaxed);
                (*s).owner = ptr::null_mut();
                (*s).pinned = 0;
                owner
            };
            if let Some(cb) = (*this).on_evict {
                cb(owner);
            }
            deluge_free((*this).heap, backing);
            return true;
        }
    }

    /// Populate table entry `idx` for a freshly allocated backing block. Called inside a section.
    unsafe fn populate(this: *mut SlabPool, idx: usize, p: *mut u8, owner: *mut c_void) {
        let lru = Self::bump(this);
        let s = Self::slot(this, idx);
        (*s).owner = owner;
        (*s).pinned = 0;
        (*s).lru = lru;
        (*s).backing.store(p, Ordering::Relaxed);
    }

    /// Claim a free table entry for `p` in one step. Scans outside any section; claims inside one after re-checking
    /// the entry is still free, rescanning if another context took it. False if the table is full.
    unsafe fn claim(this: *mut SlabPool, p: *mut u8, owner: *mut c_void) -> bool {
        loop {
            let Some(idx) = Self::find_free(this) else {
                return false;
            };
            let _s = Section::enter(STEP_SLAB);
            if Self::backing(this, idx).is_null() {
                Self::populate(this, idx, p, owner);
                return true;
            }
        }
    }

    /// Allocate a slot's backing, zeroing it if asked. The heap's reclaim hook may evict *other* slots meanwhile.
    unsafe fn alloc_backing(this: *mut SlabPool) -> *mut u8 {
        let p = deluge_alloc((*this).heap, (*this).slot_size, ALIGN);
        if !p.is_null() && (*this).zero_on_acquire {
            ptr::write_bytes(p, 0, (*this).slot_size);
        }
        p
    }

    /// Acquire a slot for `owner`. Returns the backing payload, or null if the heap is exhausted and nothing can be
    /// evicted.
    unsafe fn acquire(this: *mut SlabPool, owner: *mut c_void) -> *mut u8 {
        if !(*this).self_reclaim {
            return Self::acquire_unmanaged(this, owner);
        }
        // 1. Make sure a free table entry exists (evicting the coldest to free one).
        if Self::find_free(this).is_none() && !Self::evict_coldest(this) {
            return ptr::null_mut(); // table full and all pinned
        }
        // 2. Allocate backing. This may reentrantly evict other slots via the heap reclaim hook.
        let p = Self::alloc_backing(this);
        if p.is_null() {
            return ptr::null_mut();
        }
        // 3. Claim an entry (not a reserved index: another context may have taken it meanwhile).
        if !Self::claim(this, p, owner) {
            deluge_free((*this).heap, p);
            return ptr::null_mut();
        }
        p
    }

    /// Unmanaged acquire: an external coordinator owns eviction via the heap's reclaim hook, which (for the cluster
    /// pool) frees *both* heap space and table entries through `deluge_slab_release`. So we allocate **first** --
    /// letting that hook fire and recycle entries -- and only then claim a table slot, which the generous capacity
    /// (>= max slots that can physically fit) guarantees is free. No table entry is reserved across the alloc, so a
    /// reentrant release during it is harmless (sequenced steal).
    unsafe fn acquire_unmanaged(this: *mut SlabPool, owner: *mut c_void) -> *mut u8 {
        let p = Self::alloc_backing(this);
        if p.is_null() {
            return ptr::null_mut();
        }
        if !Self::claim(this, p, owner) {
            // Capacity is sized so this can't happen (a successful alloc implies room for another slot); be safe.
            deluge_free((*this).heap, p);
            return ptr::null_mut();
        }
        p
    }
}

extern "C" fn slab_reclaim(ctx: *mut c_void, _bytes_needed: usize) -> bool {
    unsafe { SlabPool::evict_coldest(ctx as *mut SlabPool) }
}

/// Opaque slab handle.
#[repr(C)]
pub struct DelugeSlab {
    _opaque: [u8; 0],
}

/// Shared construction for both create variants. `self_reclaim` selects managed vs
/// backing-only; when set, the pool registers itself as the heap's reclaim hook.
unsafe fn create_pool(
    heap: *mut DelugeHeap,
    slot_size: usize,
    capacity: usize,
    on_evict: Option<EvictFn>,
    self_reclaim: bool,
) -> *mut DelugeSlab {
    if heap.is_null() || capacity == 0 || slot_size == 0 {
        return ptr::null_mut();
    }
    let ctrl = deluge_alloc(
        heap,
        core::mem::size_of::<SlabPool>(),
        core::mem::align_of::<SlabPool>().max(ALIGN),
    ) as *mut SlabPool;
    if ctrl.is_null() {
        return ptr::null_mut();
    }
    let slots = deluge_alloc(
        heap,
        capacity * core::mem::size_of::<Slot>(),
        core::mem::align_of::<Slot>().max(ALIGN),
    ) as *mut Slot;
    if slots.is_null() {
        deluge_free(heap, ctrl as *mut u8);
        return ptr::null_mut();
    }
    ptr::write(
        ctrl,
        SlabPool {
            heap,
            slot_size,
            capacity,
            slots,
            tick: 0,
            on_evict,
            self_reclaim,
            zero_on_acquire: false,
        },
    );
    for i in 0..capacity {
        ptr::write(
            slots.add(i),
            Slot {
                backing: AtomicPtr::new(ptr::null_mut()),
                owner: ptr::null_mut(),
                pinned: 0,
                lru: 0,
            },
        );
    }
    if self_reclaim {
        deluge_heap_register_reclaim(heap, slab_reclaim, ctrl as *mut c_void);
    }
    ctrl as *mut DelugeSlab
}

/// Create a *managed* slab pool of `capacity` uniform `slot_size`-byte slots over
/// `heap`, and register it as the heap's reclaim hook. `on_evict` (may be null) is
/// called with a slot's owner when that slot is reclaimed. Returns null on OOM.
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_create(
    heap: *mut DelugeHeap,
    slot_size: usize,
    capacity: usize,
    on_evict: Option<EvictFn>,
) -> *mut DelugeSlab {
    create_pool(heap, slot_size, capacity, on_evict, true)
}

/// Create a *backing-only* slab pool: uniform `slot_size`-byte slots over `heap`,
/// but with eviction owned by an external coordinator. It registers no reclaim hook
/// and never self-evicts — the heap's hook (set by the caller) must free table
/// entries via `deluge_slab_release`. `capacity` must be >= the most slots that can
/// physically fit in `heap` so the table is never the limiting factor. Returns null
/// on OOM. Used for the cluster pool, whose eviction policy lives in the C++
/// CacheManager queues (docs/dev/allocator_sdram_strangle.md, step 7).
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_create_unmanaged(
    heap: *mut DelugeHeap,
    slot_size: usize,
    capacity: usize,
) -> *mut DelugeSlab {
    create_pool(heap, slot_size, capacity, None, false)
}

/// Zero every slot's bytes on acquire (off by default). For the sim/golden build
/// (DELUGE_DETERMINISTIC_ALLOC) so a read-before-write of slot memory resolves to a
/// defined 0 instead of layout-dependent recycled heap content. Leave off in firmware.
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_set_zero_on_acquire(slab: *mut DelugeSlab, on: bool) {
    if !slab.is_null() {
        (*(slab as *mut SlabPool)).zero_on_acquire = on;
    }
}

#[no_mangle]
pub unsafe extern "C" fn deluge_slab_acquire(slab: *mut DelugeSlab, owner: *mut c_void) -> *mut u8 {
    if slab.is_null() {
        return ptr::null_mut();
    }
    SlabPool::acquire(slab as *mut SlabPool, owner)
}

/// Mark a slot's payload as in-use (un-evictable) — refcounted.
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_pin(slab: *mut DelugeSlab, p: *mut u8) {
    if slab.is_null() {
        return;
    }
    let this = slab as *mut SlabPool;
    if let Some(i) = SlabPool::find_by_ptr(this, p) {
        let _s = Section::enter(STEP_SLAB);
        let s = SlabPool::slot(this, i);
        if (*s).backing.load(Ordering::Relaxed) == p {
            (*s).pinned += 1;
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn deluge_slab_unpin(slab: *mut DelugeSlab, p: *mut u8) {
    if slab.is_null() {
        return;
    }
    let this = slab as *mut SlabPool;
    if let Some(i) = SlabPool::find_by_ptr(this, p) {
        let _s = Section::enter(STEP_SLAB);
        let s = SlabPool::slot(this, i);
        if (*s).backing.load(Ordering::Relaxed) == p && (*s).pinned > 0 {
            (*s).pinned -= 1;
        }
    }
}

/// Mark a slot most-recently-used (call on access so LRU reflects real usage).
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_touch(slab: *mut DelugeSlab, p: *mut u8) {
    if slab.is_null() {
        return;
    }
    let this = slab as *mut SlabPool;
    if let Some(i) = SlabPool::find_by_ptr(this, p) {
        let _s = Section::enter(STEP_SLAB);
        let s = SlabPool::slot(this, i);
        if (*s).backing.load(Ordering::Relaxed) == p {
            (*s).lru = SlabPool::bump(this);
        }
    }
}

/// Explicitly release a slot back to the heap (without an eviction callback).
/// Returns true if `p` was one of this pool's slots (and was freed), false if it
/// isn't owned here — letting an external reclaim coordinator do "slab-release else
/// heap-free" to route cluster victims to the slab and everything else to the heap.
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_release(slab: *mut DelugeSlab, p: *mut u8) -> bool {
    if slab.is_null() {
        return false;
    }
    let this = slab as *mut SlabPool;
    let Some(i) = SlabPool::find_by_ptr(this, p) else {
        return false;
    };
    let cleared = {
        let _s = Section::enter(STEP_SLAB);
        let s = SlabPool::slot(this, i);
        if (*s).backing.load(Ordering::Relaxed) == p {
            (*s).backing.store(ptr::null_mut(), Ordering::Relaxed);
            (*s).owner = ptr::null_mut();
            (*s).pinned = 0;
            true
        } else {
            false
        }
    };
    if cleared {
        deluge_free((*this).heap, p);
    }
    cleared
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{deluge_alloc, deluge_heap_create, deluge_usable_size};
    use core::cell::Cell;

    thread_local! {
        static EVICTS: Cell<usize> = const { Cell::new(0) };
        static LAST_EVICTED: Cell<usize> = const { Cell::new(0) };
    }
    extern "C" fn on_evict(owner: *mut c_void) {
        EVICTS.with(|c| c.set(c.get() + 1));
        LAST_EVICTED.with(|c| c.set(owner as usize));
    }
    fn evicts() -> usize {
        EVICTS.with(|c| c.get())
    }
    fn reset_evicts() {
        EVICTS.with(|c| c.set(0));
    }

    fn owner(n: usize) -> *mut c_void {
        n as *mut c_void
    }

    #[test]
    fn acquires_distinct_aligned_usable_slots() {
        let bytes = 1 << 20;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        let slab = unsafe { deluge_slab_create(h, 4096, 8, Some(on_evict)) };
        assert!(!slab.is_null());
        let mut ptrs = vec![];
        for n in 1..=8 {
            let p = unsafe { deluge_slab_acquire(slab, owner(n)) };
            assert!(!p.is_null(), "acquire {n}");
            assert_eq!(p as usize % 16, 0);
            assert!(unsafe { deluge_usable_size(h, p) } >= 4096);
            assert!(!ptrs.contains(&p), "duplicate slot {p:p}");
            unsafe { core::ptr::write_bytes(p, n as u8, 4096) };
            ptrs.push(p);
        }
        let _ = &buf;
    }

    #[test]
    fn evicts_coldest_unpinned_when_table_full() {
        let bytes = 1 << 20;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        // capacity 4 forces table-full eviction independent of heap space.
        let slab = unsafe { deluge_slab_create(h, 4096, 4, Some(on_evict)) };
        let p1 = unsafe { deluge_slab_acquire(slab, owner(1)) };
        let _p2 = unsafe { deluge_slab_acquire(slab, owner(2)) };
        let _p3 = unsafe { deluge_slab_acquire(slab, owner(3)) };
        let _p4 = unsafe { deluge_slab_acquire(slab, owner(4)) };
        // Pin the otherwise-coldest slot (owner 1); eviction must skip it.
        unsafe { deluge_slab_pin(slab, p1) };
        reset_evicts();
        let p5 = unsafe { deluge_slab_acquire(slab, owner(5)) };
        assert!(!p5.is_null());
        assert_eq!(evicts(), 1, "exactly one eviction");
        assert_eq!(
            LAST_EVICTED.with(|c| c.get()),
            2,
            "coldest *unpinned* (owner 2) evicted, not pinned owner 1"
        );
        let _ = &buf;
    }

    #[test]
    fn reclaim_yields_slab_space_to_general_alloc() {
        // Small heap so slab slots fill it; a general alloc then forces the heap
        // to reclaim cold slab slots (the unified-pool "caches yield on demand").
        let bytes = 256 * 1024;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        let slab = unsafe { deluge_slab_create(h, 16 * 1024, 64, Some(on_evict)) };
        // Acquire unpinned slots until the heap is full (an acquire has to recycle
        // a slot to fit) — that's the point where the cache owns ~all free space.
        let mut n = 0;
        loop {
            n += 1;
            assert!(n < 1000, "heap never filled");
            let before = evicts();
            let p = unsafe { deluge_slab_acquire(slab, owner(n)) };
            assert!(!p.is_null(), "acquire {n}");
            if evicts() > before {
                break;
            }
        }
        reset_evicts();
        // A general allocation larger than the remaining free space must succeed
        // by evicting cold slab slots (their freed blocks coalesce in the TLSF).
        let big = unsafe { deluge_alloc(h, 64 * 1024, 16) };
        assert!(!big.is_null(), "general alloc should reclaim slab space");
        assert!(evicts() >= 1, "reclaim should have evicted >= 1 slot");
        let _ = &buf;
    }

    // --- Unmanaged (backing-only) pool: eviction owned by an external coordinator ---

    use core::cell::RefCell;
    thread_local! {
        // The external coordinator's view: the unmanaged slab + a FIFO of live slot
        // payloads, mimicking the C++ CacheManager's reclamation queue.
        static EXT_SLAB: Cell<*mut DelugeSlab> = const { Cell::new(ptr::null_mut()) };
        static EXT_LIVE: RefCell<Vec<*mut u8>> = const { RefCell::new(Vec::new()) };
    }

    // Heap reclaim hook standing in for gmaSdramReclaim: pop the coldest live slot and
    // release it through the slab (slab-release-or-heap-free, here always slab-owned).
    extern "C" fn ext_reclaim(_ctx: *mut c_void, _bytes: usize) -> bool {
        let victim = EXT_LIVE.with(|v| {
            let mut v = v.borrow_mut();
            if v.is_empty() {
                None
            } else {
                Some(v.remove(0))
            }
        });
        match victim {
            Some(p) => unsafe { deluge_slab_release(EXT_SLAB.with(|s| s.get()), p) },
            None => false,
        }
    }

    #[test]
    fn unmanaged_pool_evicts_via_external_reclaim_hook() {
        let bytes = 256 * 1024;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        // Capacity generous enough that the table is never the limiter (more than fit).
        let slab = unsafe { deluge_slab_create_unmanaged(h, 16 * 1024, 64) };
        assert!(!slab.is_null());
        EXT_SLAB.with(|s| s.set(slab));
        EXT_LIVE.with(|v| v.borrow_mut().clear());
        // The coordinator owns the heap's reclaim hook (no self-eviction by the pool).
        unsafe { deluge_heap_register_reclaim(h, ext_reclaim, ptr::null_mut()) };

        // Acquire far more slots than fit at once: each acquire that runs out of heap
        // must drive the external hook to release the coldest, then succeed.
        let mut last = ptr::null_mut();
        for n in 1..=200usize {
            let p = unsafe { deluge_slab_acquire(slab, owner(n)) };
            assert!(
                !p.is_null(),
                "acquire {n} should succeed via external reclaim"
            );
            assert_eq!(p as usize % 16, 0);
            unsafe { core::ptr::write_bytes(p, n as u8, 16 * 1024) };
            EXT_LIVE.with(|v| v.borrow_mut().push(p));
            last = p;
        }
        // The most recent slot is intact and owned by the pool.
        assert!(
            unsafe { deluge_slab_release(slab, last) },
            "last slot is slab-owned"
        );
        let _ = &buf;
    }

    #[test]
    fn release_reports_ownership() {
        let bytes = 1 << 20;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        let slab = unsafe { deluge_slab_create_unmanaged(h, 4096, 16) };
        let p = unsafe { deluge_slab_acquire(slab, owner(1)) };
        assert!(!p.is_null());
        // A foreign pointer (a plain heap alloc) is not owned by the slab.
        let foreign = unsafe { deluge_alloc(h, 4096, 16) };
        assert!(
            !unsafe { deluge_slab_release(slab, foreign) },
            "foreign ptr not owned"
        );
        unsafe { deluge_free(h, foreign) };
        // The slab's own slot is owned; releasing twice reports false the second time.
        assert!(unsafe { deluge_slab_release(slab, p) }, "slab slot owned");
        assert!(!unsafe { deluge_slab_release(slab, p) }, "already released");
        let _ = &buf;
    }

    #[test]
    fn unmanaged_pool_does_not_self_evict() {
        // With no reclaim hook registered, an unmanaged pool must NOT evict its own
        // slots; once the heap is full, acquire simply fails (the coordinator, absent
        // here, is what would free space).
        let bytes = 128 * 1024;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        let slab = unsafe { deluge_slab_create_unmanaged(h, 16 * 1024, 64) };
        let mut acquired = 0;
        loop {
            let p = unsafe { deluge_slab_acquire(slab, owner(acquired + 1)) };
            if p.is_null() {
                break;
            }
            acquired += 1;
            assert!(
                acquired < 64,
                "should run out of heap, not self-evict forever"
            );
        }
        assert!(acquired >= 1, "should fit at least one slot");
        let _ = &buf;
    }

    #[test]
    fn all_pinned_means_graceful_oom() {
        let bytes = 256 * 1024;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        let slab = unsafe { deluge_slab_create(h, 16 * 1024, 64, Some(on_evict)) };
        let mut ptrs = vec![];
        for n in 1..=11 {
            let p = unsafe { deluge_slab_acquire(slab, owner(n)) };
            if p.is_null() {
                break;
            }
            unsafe { deluge_slab_pin(slab, p) };
            ptrs.push(p);
        }
        reset_evicts();
        // Everything pinned: a large general alloc can't reclaim and must fail
        // cleanly (no eviction of pinned slots, no crash).
        let big = unsafe { deluge_alloc(h, 128 * 1024, 16) };
        assert!(big.is_null(), "must not evict pinned slots");
        assert_eq!(evicts(), 0);
        let _ = &buf;
    }

    use crate::section::test_hooks;

    #[test]
    fn slab_slot_claims_and_clears_are_noted_inside_their_sections() {
        test_hooks::install();
        let bytes = 256 * 1024;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        let slab = unsafe { deluge_slab_create_unmanaged(h, 1024, 256) };
        test_hooks::take_events();
        let p = unsafe { deluge_slab_acquire(slab, ptr::null_mut()) };
        assert!(!p.is_null());
        unsafe { deluge_slab_touch(slab, p) };
        assert!(unsafe { deluge_slab_release(slab, p) });
        let events = test_hooks::take_events();
        assert_eq!(events.iter().filter(|e| **e == "slab").count(), 3); // claim, touch, clear
        assert_eq!(
            events.iter().filter(|e| **e == "enter").count(),
            events.iter().filter(|e| **e == "exit").count()
        );
        let _ = &buf;
    }

    #[test]
    fn unmanaged_acquire_on_a_full_table_frees_its_backing() {
        let bytes = 256 * 1024;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        // How many 1 KB blocks the heap can hand out right now (all freed again afterwards).
        let fill = |h: *mut DelugeHeap| -> usize {
            let mut held = Vec::new();
            loop {
                let p = unsafe { deluge_alloc(h, 1024, 16) };
                if p.is_null() {
                    break;
                }
                held.push(p);
            }
            let n = held.len();
            for p in held {
                unsafe { crate::deluge_free(h, p) };
            }
            n
        };
        let slab = unsafe { deluge_slab_create_unmanaged(h, 1024, 1) }; // one entry
        let a = unsafe { deluge_slab_acquire(slab, ptr::null_mut()) };
        assert!(!a.is_null());
        let before = fill(h);
        let b = unsafe { deluge_slab_acquire(slab, ptr::null_mut()) }; // table full: its backing must go back
        assert!(b.is_null());
        assert_eq!(fill(h), before);
        let _ = &buf;
    }
}
