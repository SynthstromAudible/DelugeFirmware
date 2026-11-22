//! Slab cache pool: uniform slots of reclaimable memory, layered on the TLSF heap.
//!
//! A fixed-capacity table of uniform-size slots whose backing blocks are borrowed from, and
//! returned to, the heap. Uniform slots keep eviction a bounded scan and cannot fragment.
//!
//! - A *managed* pool ([`deluge_slab_create`]) registers itself as the heap's reclaim hook: when an
//!   allocation cannot be satisfied it evicts its coldest unpinned slot, notifying the owner, and
//!   the allocation retries.
//! - An *unmanaged* pool ([`deluge_slab_create_unmanaged`]) is backing only; an external
//!   coordinator owns the heap's reclaim hook and releases slots through [`deluge_slab_release`].
//!
//! Pinning protects a slot that is being read. Table edits are [`crate::section`] steps; scans run
//! outside any section and are re-checked inside one.

use crate::section::{Section, STEP_SLAB};
use crate::tlsf::ALIGN;
use crate::{deluge_alloc, deluge_free, deluge_heap_register_reclaim, DelugeHeap};
use core::ffi::c_void;
use core::ptr;
use core::sync::atomic::{AtomicPtr, Ordering};

/// Eviction notification: the owner's slot is being reclaimed, so it must drop its reference and
/// reload from backing store later.
pub type EvictFn = unsafe extern "C" fn(owner: *mut c_void);

/// A table entry.
///
/// `backing` is atomic because the table is scanned outside any section while another context may
/// claim or clear an entry inside one; every other field is accessed only inside a section.
struct Slot {
    backing: AtomicPtr<u8>, // null => free table entry
    owner: *mut c_void,
    pinned: u32,
    lru: u64,
}

/// The pool behind a [`DelugeSlab`] handle.
#[repr(C)]
pub struct SlabPool {
    heap: *mut DelugeHeap,
    slot_size: usize,
    capacity: usize,
    slots: *mut Slot, // `capacity` entries, allocated from the heap at creation
    tick: u64,
    on_evict: Option<EvictFn>,
    /// Managed (`true`): the pool is the heap's reclaim hook and, when its table is full, evicts
    /// its own coldest unpinned slot. Unmanaged (`false`): an external coordinator owns eviction;
    /// the pool never registers a hook and never self-evicts.
    self_reclaim: bool,
    /// Zero each slot on acquire; see [`deluge_slab_set_zero_on_acquire`].
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

    /// Returns the first free entry, scanned outside any section: a hint until re-checked inside
    /// one.
    unsafe fn find_free(this: *mut SlabPool) -> Option<usize> {
        (0..(*this).capacity).find(|&i| Self::backing(this, i).is_null())
    }

    /// Returns the entry holding `p`, scanned outside any section. A slot has one owner, so the
    /// entry cannot change under its owner; callers still re-check inside the section they act in.
    unsafe fn find_by_ptr(this: *mut SlabPool, p: *mut u8) -> Option<usize> {
        (0..(*this).capacity).find(|&i| Self::backing(this, i) == p)
    }

    /// Evicts the coldest occupied unpinned slot: clears its entry, notifies its owner, and returns
    /// its backing to the heap. Returns false if nothing is evictable.
    ///
    /// Serves as the heap's reclaim hook and makes room in a full table. The entry is re-checked
    /// and cleared inside a section (rescanning if another context changed it); the callback and
    /// the free run after it.
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
                // Clear the entry before the callbacks so a reentrant acquire or free sees a
                // consistent table.
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

    /// Fills entry `idx` for a freshly allocated backing block. Called inside a section.
    unsafe fn populate(this: *mut SlabPool, idx: usize, p: *mut u8, owner: *mut c_void) {
        let lru = Self::bump(this);
        let s = Self::slot(this, idx);
        (*s).owner = owner;
        (*s).pinned = 0;
        (*s).lru = lru;
        (*s).backing.store(p, Ordering::Relaxed);
    }

    /// Claims a free entry for `p`, re-checking inside a section and rescanning if another context
    /// took it. Returns false if the table is full.
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

    /// Allocates a slot's backing, zeroed if configured. The reclaim hook may evict other slots
    /// meanwhile.
    unsafe fn alloc_backing(this: *mut SlabPool) -> *mut u8 {
        let p = deluge_alloc((*this).heap, (*this).slot_size, ALIGN);
        if !p.is_null() && (*this).zero_on_acquire {
            ptr::write_bytes(p, 0, (*this).slot_size);
        }
        p
    }

    /// Acquires a slot for `owner`. Returns its payload, or null if the heap is exhausted and
    /// nothing can be evicted.
    unsafe fn acquire(this: *mut SlabPool, owner: *mut c_void) -> *mut u8 {
        if !(*this).self_reclaim {
            return Self::acquire_unmanaged(this, owner);
        }
        // Make sure a free entry exists, evicting the coldest if needed.
        if Self::find_free(this).is_none() && !Self::evict_coldest(this) {
            return ptr::null_mut(); // table full and all pinned
        }
        // The allocation may reentrantly evict other slots through the reclaim hook.
        let p = Self::alloc_backing(this);
        if p.is_null() {
            return ptr::null_mut();
        }
        // Claim afresh rather than reserve an index: another context may have taken it meanwhile.
        if !Self::claim(this, p, owner) {
            deluge_free((*this).heap, p);
            return ptr::null_mut();
        }
        p
    }

    /// Unmanaged acquire. The external reclaim hook frees both heap space and table entries
    /// (through [`deluge_slab_release`]), so allocate first — letting the hook recycle entries —
    /// and only then claim an entry, which a capacity of at least the slots that fit guarantees is
    /// free. Nothing is reserved across the allocation, so a reentrant release during it is
    /// harmless.
    unsafe fn acquire_unmanaged(this: *mut SlabPool, owner: *mut c_void) -> *mut u8 {
        let p = Self::alloc_backing(this);
        if p.is_null() {
            return ptr::null_mut();
        }
        if !Self::claim(this, p, owner) {
            // Unreachable with a correctly sized capacity; give the backing back regardless.
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

/// Builds a pool; `self_reclaim` selects managed (registered as the heap's reclaim hook) or
/// unmanaged.
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

/// Creates a managed pool of `capacity` slots of `slot_size` bytes over `heap`, registered as the
/// heap's reclaim hook.
///
/// `on_evict`, if set, is called with a slot's owner when that slot is reclaimed. Returns null on
/// OOM or a zero size or capacity.
///
/// # Safety
///
/// `heap` must be null or a live heap handle.
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_create(
    heap: *mut DelugeHeap,
    slot_size: usize,
    capacity: usize,
    on_evict: Option<EvictFn>,
) -> *mut DelugeSlab {
    create_pool(heap, slot_size, capacity, on_evict, true)
}

/// Creates an unmanaged pool: `slot_size`-byte slots over `heap`, with eviction owned by the
/// caller.
///
/// It registers no reclaim hook and never self-evicts; the caller's hook must free entries through
/// [`deluge_slab_release`]. `capacity` must be at least the most slots that fit in `heap`, so the
/// table is never the limit. Returns null on OOM. The app's sample-cluster pool is one.
///
/// # Safety
///
/// `heap` must be null or a live heap handle.
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_create_unmanaged(
    heap: *mut DelugeHeap,
    slot_size: usize,
    capacity: usize,
) -> *mut DelugeSlab {
    create_pool(heap, slot_size, capacity, None, false)
}

/// Sets whether each slot is zeroed on acquire (off by default).
///
/// Deterministic builds (`DELUGE_DETERMINISTIC_ALLOC`) turn it on so a read-before-write of slot
/// memory is a defined 0 rather than recycled content, keeping renders independent of heap layout.
/// Firmware leaves it off for speed.
///
/// # Safety
///
/// `slab` must be null or a live slab handle.
#[no_mangle]
pub unsafe extern "C" fn deluge_slab_set_zero_on_acquire(slab: *mut DelugeSlab, on: bool) {
    if !slab.is_null() {
        (*(slab as *mut SlabPool)).zero_on_acquire = on;
    }
}

/// Acquires a slot for `owner`, evicting if the pool is managed. Returns null on failure.
///
/// # Safety
///
/// `slab` must be null or a live slab handle.
pub unsafe fn deluge_slab_acquire(slab: *mut DelugeSlab, owner: *mut c_void) -> *mut u8 {
    if slab.is_null() {
        return ptr::null_mut();
    }
    SlabPool::acquire(slab as *mut SlabPool, owner)
}

/// Pins the slot at `p` against eviction; pins are counted. Unknown pointers are ignored.
///
/// # Safety
///
/// `slab` must be null or a live slab handle.
pub unsafe fn deluge_slab_pin(slab: *mut DelugeSlab, p: *mut u8) {
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

/// Removes one pin from the slot at `p`. Unknown or unpinned pointers are ignored.
///
/// # Safety
///
/// `slab` must be null or a live slab handle.
pub unsafe fn deluge_slab_unpin(slab: *mut DelugeSlab, p: *mut u8) {
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

/// Marks the slot at `p` most recently used; call on access so eviction order reflects use.
///
/// # Safety
///
/// `slab` must be null or a live slab handle.
pub unsafe fn deluge_slab_touch(slab: *mut DelugeSlab, p: *mut u8) {
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

/// Releases the slot at `p` back to the heap, without an eviction callback.
///
/// Returns false if `p` is not one of this pool's slots, so an external coordinator can try the
/// slab first and fall back to a heap free.
///
/// # Safety
///
/// `slab` must be null or a live slab handle; `p` must not be used after a true return.
pub unsafe fn deluge_slab_release(slab: *mut DelugeSlab, p: *mut u8) -> bool {
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
        // Capacity 4 forces table-full eviction independent of heap space.
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
        // A small heap the slots fill; a general allocation must then reclaim cold slots.
        let bytes = 256 * 1024;
        let mut buf = vec![0u8; bytes];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr(), bytes) };
        let slab = unsafe { deluge_slab_create(h, 16 * 1024, 64, Some(on_evict)) };
        // Acquire until an acquire has to recycle a slot: the cache then owns nearly all free
        // space.
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
        // Larger than the remaining free space: succeeds only by evicting slots, whose freed blocks
        // coalesce.
        let big = unsafe { deluge_alloc(h, 64 * 1024, 16) };
        assert!(!big.is_null(), "general alloc should reclaim slab space");
        assert!(evicts() >= 1, "reclaim should have evicted >= 1 slot");
        let _ = &buf;
    }

    // Unmanaged pool: eviction owned by an external coordinator.

    use core::cell::RefCell;
    thread_local! {
        // The external coordinator's view: the slab plus a FIFO of live slot payloads.
        static EXT_SLAB: Cell<*mut DelugeSlab> = const { Cell::new(ptr::null_mut()) };
        static EXT_LIVE: RefCell<Vec<*mut u8>> = const { RefCell::new(Vec::new()) };
    }

    // Stand-in for the app's reclaim hook: pops the oldest live slot and releases it.
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
        // More capacity than slots fit, so the table is never the limit.
        let slab = unsafe { deluge_slab_create_unmanaged(h, 16 * 1024, 64) };
        assert!(!slab.is_null());
        EXT_SLAB.with(|s| s.set(slab));
        EXT_LIVE.with(|v| v.borrow_mut().clear());
        // The coordinator owns the heap's reclaim hook.
        unsafe { deluge_heap_register_reclaim(h, ext_reclaim, ptr::null_mut()) };

        // Far more acquires than fit: each one that runs out of heap must drive the external hook.
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
        // With no coordinator, an unmanaged pool must not evict its own slots: once the heap is
        // full, acquire fails.
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
        // Everything pinned: a large general allocation cannot reclaim and must fail cleanly.
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
        // Table full: its backing must go back.
        let b = unsafe { deluge_slab_acquire(slab, ptr::null_mut()) };
        assert!(b.is_null());
        assert_eq!(fill(h), before);
        let _ = &buf;
    }

    #[test]
    fn two_contexts_keep_one_slab_consistent() {
        use crate::section::test_hooks::{self, Mode, MODE};
        test_hooks::install();
        let bytes = 4 << 20;
        let mut buf = vec![0u128; bytes / 16];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr() as *mut u8, bytes) };
        let capacity = 1024;
        let slab = unsafe { deluge_slab_create_unmanaged(h, 4096, capacity) };
        let hp = h as usize;
        let sp = slab as usize;

        // One context's churn: each held slot carries its own tag, checked before release, so a
        // slot handed to both contexts or an entry lost from the table fails. The render-shaped
        // context holds the host section for a whole block; the other masks per step.
        fn churn(sp: usize, seed: u32, tag: u64, rounds: usize, block: bool) {
            let slab = sp as *mut DelugeSlab;
            let mut x = seed;
            let mut next = move || {
                x ^= x << 13;
                x ^= x >> 17;
                x ^= x << 5;
                x
            };
            let mut held: Vec<(usize, u64)> = Vec::new();
            let mut seq = 0u64;
            for _ in 0..rounds {
                if block {
                    test_hooks::lock();
                }
                for _ in 0..16 {
                    if held.len() < 48 && (next() & 1 == 0 || held.is_empty()) {
                        let p = unsafe { deluge_slab_acquire(slab, ptr::null_mut()) };
                        if !p.is_null() {
                            seq += 1;
                            let v = (tag << 48) | seq;
                            unsafe { (p as *mut u64).write(v) };
                            held.push((p as usize, v));
                        }
                    } else {
                        let i = next() as usize % held.len();
                        let (p, v) = held.swap_remove(i);
                        let p = p as *mut u8;
                        assert_eq!(
                            unsafe { (p as *const u64).read() },
                            v,
                            "a slot held here was handed out again"
                        );
                        unsafe {
                            deluge_slab_touch(slab, p);
                            deluge_slab_pin(slab, p);
                            deluge_slab_unpin(slab, p);
                        }
                        assert!(
                            unsafe { deluge_slab_release(slab, p) },
                            "a held slot's table entry was lost"
                        );
                    }
                }
                if block {
                    test_hooks::unlock();
                }
            }
            if block {
                test_hooks::lock();
            }
            for (p, _) in held {
                assert!(unsafe { deluge_slab_release(slab, p as *mut u8) });
            }
            if block {
                test_hooks::unlock();
            }
        }

        let render = std::thread::spawn(move || {
            MODE.with(|m| m.set(Mode::StressRender));
            churn(sp, 0x9e37_79b9, 1, 3000, true);
        });
        let interaction = std::thread::spawn(move || {
            MODE.with(|m| m.set(Mode::StressInteraction));
            churn(sp, 0x85eb_ca6b, 2, 3000, false);
        });
        render.join().unwrap();
        interaction.join().unwrap();
        let this = sp as *mut SlabPool;
        assert!((0..capacity).all(|i| unsafe { SlabPool::backing(this, i) }.is_null()));
        assert!(unsafe { crate::deluge_heap_check(hp as *mut DelugeHeap) });
        let _ = &buf;
    }
}
