//! Slab cache pool — the "stealables" pager, layered on the TLSF heap.
//!
//! A fixed-capacity table of uniform-size slots whose backing blocks are borrowed
//! from, and returned to, the TLSF heap. The pool registers itself as the heap's
//! reclaim hook (tlsf::ReclaimFn): when a *live* allocation can't be satisfied,
//! the heap calls back here, the pool evicts its coldest **unpinned** slot
//! (notifying the owner — the analogue of `Stealable::steal()`), returns that
//! block to the heap, and the allocation retries. Uniform slots make eviction
//! O(1)-ish (a bounded scan) and unfragmentable — the redesign's
//! slab-backed-by-TLSF synthesis (docs/dev/allocator_redesign.md), M2.
//!
//! Pinning (`numReasonsToBeLoaded` in C++) protects a slot a voice is mid-read of.

use crate::tlsf::ALIGN;
use crate::{deluge_alloc, deluge_free, deluge_heap_register_reclaim, DelugeHeap};
use core::ffi::c_void;
use core::ptr;

/// Eviction notification: a slot the owner holds is being reclaimed (the owner
/// must drop its reference and reload from backing store later). Analogue of
/// `Stealable::steal()`.
pub type EvictFn = unsafe extern "C" fn(owner: *mut c_void);

struct Slot {
    backing: *mut u8, // null => free table entry
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

    unsafe fn find_free(this: *mut SlabPool) -> Option<usize> {
        (0..(*this).capacity).find(|&i| (*Self::slot(this, i)).backing.is_null())
    }

    unsafe fn find_by_ptr(this: *mut SlabPool, p: *mut u8) -> Option<usize> {
        (0..(*this).capacity).find(|&i| (*Self::slot(this, i)).backing == p)
    }

    /// Evict the coldest occupied **unpinned** slot: notify the owner, return its
    /// backing to the heap, and free the table entry. Returns false if nothing is
    /// evictable (everything pinned/empty). Used both as the heap reclaim hook and
    /// to make room in a full table.
    unsafe fn evict_coldest(this: *mut SlabPool) -> bool {
        let mut best: Option<usize> = None;
        let mut best_lru = u64::MAX;
        for i in 0..(*this).capacity {
            let s = Self::slot(this, i);
            if !(*s).backing.is_null() && (*s).pinned == 0 && (*s).lru < best_lru {
                best_lru = (*s).lru;
                best = Some(i);
            }
        }
        let Some(i) = best else { return false };
        let s = Self::slot(this, i);
        let backing = (*s).backing;
        let owner = (*s).owner;
        // Clear the entry *before* the callbacks so a reentrant acquire/free sees
        // a consistent table (sequenced steal, redesign §2).
        (*s).backing = ptr::null_mut();
        (*s).owner = ptr::null_mut();
        (*s).pinned = 0;
        if let Some(cb) = (*this).on_evict {
            cb(owner);
        }
        deluge_free((*this).heap, backing);
        true
    }

    /// Acquire a slot for `owner`. Returns the backing payload, or null if the
    /// heap is exhausted and nothing can be evicted.
    unsafe fn acquire(this: *mut SlabPool, owner: *mut c_void) -> *mut u8 {
        // 1. Ensure a free table entry (evicting the coldest to reuse one).
        let idx = match Self::find_free(this) {
            Some(i) => i,
            None => {
                if !Self::evict_coldest(this) {
                    return ptr::null_mut(); // table full and all pinned
                }
                Self::find_free(this).expect("entry freed by eviction")
            }
        };
        // 2. Allocate backing. This may reentrantly evict *other* slots via the
        //    heap reclaim hook to free space; entry `idx` is empty so it is safe.
        let p = deluge_alloc((*this).heap, (*this).slot_size, ALIGN);
        if p.is_null() {
            return ptr::null_mut();
        }
        // 3. Populate the reserved entry.
        let lru = Self::bump(this);
        let s = Self::slot(this, idx);
        (*s).backing = p;
        (*s).owner = owner;
        (*s).pinned = 0;
        (*s).lru = lru;
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

/// Create a slab pool of `capacity` uniform `slot_size`-byte slots over `heap`,
/// and register it as the heap's reclaim hook. `on_evict` (may be null) is called
/// with a slot's owner when that slot is reclaimed. Returns null on OOM.
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_create(
    heap: *mut DelugeHeap,
    slot_size: usize,
    capacity: usize,
    on_evict: Option<EvictFn>,
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
        },
    );
    for i in 0..capacity {
        ptr::write(
            slots.add(i),
            Slot {
                backing: ptr::null_mut(),
                owner: ptr::null_mut(),
                pinned: 0,
                lru: 0,
            },
        );
    }
    deluge_heap_register_reclaim(heap, slab_reclaim, ctrl as *mut c_void);
    ctrl as *mut DelugeSlab
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
        (*SlabPool::slot(this, i)).pinned += 1;
    }
}

#[no_mangle]
pub unsafe extern "C" fn deluge_slab_unpin(slab: *mut DelugeSlab, p: *mut u8) {
    if slab.is_null() {
        return;
    }
    let this = slab as *mut SlabPool;
    if let Some(i) = SlabPool::find_by_ptr(this, p) {
        let s = SlabPool::slot(this, i);
        if (*s).pinned > 0 {
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
        let lru = SlabPool::bump(this);
        (*SlabPool::slot(this, i)).lru = lru;
    }
}

/// Explicitly release a slot back to the heap (without an eviction callback).
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_release(slab: *mut DelugeSlab, p: *mut u8) {
    if slab.is_null() {
        return;
    }
    let this = slab as *mut SlabPool;
    if let Some(i) = SlabPool::find_by_ptr(this, p) {
        let s = SlabPool::slot(this, i);
        (*s).backing = ptr::null_mut();
        (*s).owner = ptr::null_mut();
        (*s).pinned = 0;
        deluge_free((*this).heap, p);
    }
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
}
