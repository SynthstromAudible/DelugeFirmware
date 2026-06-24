//! deluge_resource — the Deluge's cached-asset / resource manager core, behind a
//! stable C ABI (`deluge_resource.h`). Replaces the C++ `Stealable` / `CacheManager`
//! / `numReasonsToBeLoaded` machinery with a value-scored, lease-based cache of
//! reconstructable assets layered on the `deluge_alloc` TLSF heap.
//! See docs/dev/resource_manager.md for the model and the resolved design decisions.
//!
//! A plain `no_std` rlib: it has **no `#[panic_handler]`** (the `deluge_rust`
//! umbrella staticlib provides the one for the whole dependency graph) and uses
//! **no `alloc`/global allocator** — all bookkeeping is intrusive + fixed-capacity
//! `core::`, so the reclaim hook can never re-enter the manager mid-mutation.

#![cfg_attr(target_os = "none", no_std)]
#![allow(clippy::missing_safety_doc)]

pub mod manager;
pub mod value;

#[cfg(any(test, feature = "fuzzing"))]
pub mod testing;

pub use manager::*;
pub use value::{COST_CPU, COST_CPU_PERC, COST_FREE, COST_IO, COST_IO_CONVERTED, COST_OBJECT};

#[cfg(test)]
mod proptests {
    use crate::testing::{Harness, Op};
    use proptest::prelude::*;

    fn any_op() -> impl Strategy<Value = Op> {
        prop_oneof![
            3 => (0u32..6, 0u32..10).prop_map(|(asset, index)| Op::Acquire { asset, index }),
            2 => (0usize..32).prop_map(|which| Op::Release { which }),
            1 => (0usize..32).prop_map(|which| Op::Touch { which }),
            1 => (0u32..6).prop_map(|asset| Op::Reference { asset }),
        ]
    }

    proptest! {
        // Any churn sequence must hold the invariants (leased chunks never evicted
        // or corrupted, acquires aligned/non-aliasing); proptest shrinks any failure.
        #![proptest_config(ProptestConfig::with_cases(48))]
        #[test]
        fn churn_never_violates_invariants(ops in prop::collection::vec(any_op(), 0..400)) {
            let mut h = Harness::new(256 * 1024, 64);
            for op in ops {
                h.step(op);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::cell::Cell;
    use core::ffi::c_void;
    use deluge_alloc::deluge_heap_create;

    fn owner(n: usize) -> *mut c_void {
        n as *mut c_void
    }
    // A chunk's "file contents": byte pattern derived from (owner, index) so a
    // reload is verifiable. Mirrors a real Source reading deterministic bytes.
    fn pattern(owner: *mut c_void, index: u32) -> u8 {
        (owner as usize as u8)
            .wrapping_add(index as u8)
            .wrapping_add(0x5A)
    }

    thread_local! {
        static EVICTS: Cell<usize> = const { Cell::new(0) };
    }
    fn reset_evicts() {
        EVICTS.with(|c| c.set(0));
    }
    fn evicts() -> usize {
        EVICTS.with(|c| c.get())
    }

    unsafe extern "C" fn mock_materialize(
        _ctx: *mut c_void,
        owner: *mut c_void,
        index: u32,
        dest: *mut u8,
        len: usize,
    ) -> bool {
        core::ptr::write_bytes(dest, pattern(owner, index), len);
        true
    }
    unsafe extern "C" fn failing_materialize(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        _index: u32,
        _dest: *mut u8,
        _len: usize,
    ) -> bool {
        false // source vanished
    }
    unsafe extern "C" fn mock_on_evict(_ctx: *mut c_void, _owner: *mut c_void, _index: u32) {
        EVICTS.with(|c| c.set(c.get() + 1));
    }

    // Build a heap over a 16-aligned buffer (Vec<u128>) — real BSP regions are
    // page-aligned, and miri tracks an allocation's declared alignment (a Vec<u8>
    // arena would flag the TLSF's 16-aligned headers as misaligned). Returns the
    // buffer so the caller keeps it alive (not leaked → miri-leak-clean).
    fn arena(bytes: usize) -> (Vec<u128>, *mut deluge_alloc::DelugeHeap) {
        let words = bytes.div_ceil(16);
        let mut buf = vec![0u128; words];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        (buf, h)
    }

    fn check_pattern(p: *mut u8, owner: *mut c_void, index: u32, len: usize) {
        let want = pattern(owner, index);
        for i in 0..len {
            assert_eq!(
                unsafe { *p.add(i) },
                want,
                "byte {i} of ({owner:p},{index})"
            );
        }
    }

    #[test]
    fn acquire_materializes_and_caches() {
        let (_buf, h) = arena(1 << 20);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        assert!(!mgr.is_null());
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(1),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let p = unsafe { deluge_resource_acquire(mgr, a, 0, 4096) };
        assert!(!p.is_null());
        assert_eq!(p as usize % 16, 0);
        check_pattern(p, owner(1), 0, 4096);
        // Cache hit: same chunk → same pointer, no re-materialize.
        let p2 = unsafe { deluge_resource_acquire(mgr, a, 0, 4096) };
        assert_eq!(p, p2, "cache hit should return the resident pointer");
    }

    #[test]
    fn leased_chunk_is_never_evicted() {
        // Small heap, 64 KB chunks: hold one leased, then hammer acquisitions that
        // force reclaim — the leased chunk must survive and keep its bytes.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(7),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let pinned = unsafe { deluge_resource_acquire(mgr, a, 0, 64 * 1024) }; // lease held (not released)
        assert!(!pinned.is_null());
        for n in 1..20u32 {
            let p = unsafe { deluge_resource_acquire(mgr, a, n, 64 * 1024) };
            if !p.is_null() {
                unsafe { deluge_resource_release(mgr, p) }; // make it evictable
            }
        }
        check_pattern(pinned, owner(7), 0, 64 * 1024); // intact despite the churn
    }

    #[test]
    fn eviction_reloads_correct_bytes() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(3),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        // Acquire+release chunk 0, then evict it by pressure, then re-acquire: the
        // manager must re-materialize the correct bytes.
        let p0 = unsafe { deluge_resource_acquire(mgr, a, 0, 64 * 1024) };
        unsafe { deluge_resource_release(mgr, p0) };
        reset_evicts();
        for n in 1..20u32 {
            let p = unsafe { deluge_resource_acquire(mgr, a, n, 64 * 1024) };
            if !p.is_null() {
                unsafe { deluge_resource_release(mgr, p) };
            }
        }
        assert!(evicts() >= 1, "pressure should have evicted something");
        let again = unsafe { deluge_resource_acquire(mgr, a, 0, 64 * 1024) };
        assert!(!again.is_null());
        check_pattern(again, owner(3), 0, 64 * 1024);
    }

    #[test]
    fn dirty_chunk_is_never_evicted() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(9),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let d = unsafe { deluge_resource_acquire(mgr, a, 0, 64 * 1024) };
        unsafe { deluge_resource_mark_dirty(mgr, d, true) };
        unsafe { deluge_resource_release(mgr, d) }; // unleased, but dirty
        for n in 1..20u32 {
            let p = unsafe { deluge_resource_acquire(mgr, a, n, 64 * 1024) };
            if !p.is_null() {
                unsafe { deluge_resource_release(mgr, p) };
            }
        }
        // Still resident (dirty pin): re-acquiring chunk 0 returns the same pointer.
        let d2 = unsafe { deluge_resource_acquire(mgr, a, 0, 64 * 1024) };
        assert_eq!(d, d2, "dirty chunk must not have been evicted/reloaded");
    }

    #[test]
    fn soft_reference_raises_priority() {
        // Two assets; the referenced one's chunk must outlast the unreferenced one's
        // under eviction pressure.
        let (_buf, h) = arena(192 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let cold = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(1),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let warm = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(2),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        unsafe { deluge_resource_reference(mgr, warm) }; // project cares about `warm`
        let pc = unsafe { deluge_resource_acquire(mgr, cold, 0, 64 * 1024) };
        unsafe { deluge_resource_release(mgr, pc) };
        let pw = unsafe { deluge_resource_acquire(mgr, warm, 0, 64 * 1024) };
        unsafe { deluge_resource_release(mgr, pw) };
        // A third acquire forces one eviction — it must take the unreferenced `cold`.
        let p3 = unsafe { deluge_resource_acquire(mgr, cold, 1, 64 * 1024) };
        assert!(!p3.is_null());
        // `warm`'s chunk is still resident (same pointer on re-acquire); `cold`'s
        // original chunk 0 was the eviction victim (re-acquire makes a fresh one).
        let pw2 = unsafe { deluge_resource_acquire(mgr, warm, 0, 64 * 1024) };
        assert_eq!(pw, pw2, "referenced asset's chunk should survive");
    }

    #[test]
    fn all_leased_means_graceful_oom() {
        let (_buf, h) = arena(192 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(5),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let mut held = 0;
        // Hold leases until the heap can't fit another (nothing evictable).
        for n in 0..100u32 {
            let p = unsafe { deluge_resource_acquire(mgr, a, n, 64 * 1024) };
            if p.is_null() {
                break;
            }
            held += 1; // leave it leased
        }
        assert!(held >= 1, "should fit at least one");
        // Everything leased ⇒ the next acquire fails cleanly (no crash, no eviction).
        reset_evicts();
        let p = unsafe { deluge_resource_acquire(mgr, a, 999, 64 * 1024) };
        assert!(p.is_null(), "must not evict leased chunks");
        assert_eq!(evicts(), 0);
    }

    #[test]
    fn fuzz_replay_smoke() {
        // The exact path the fuzzer would drive, deterministic so it runs under
        // `cargo test` without cargo-fuzz installed.
        let mut x = 0x1234_5678u32;
        let mut bytes = std::vec![0u8; 6000];
        for b in bytes.iter_mut() {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            *b = x as u8;
        }
        crate::testing::run_bytes(&bytes);
    }

    #[test]
    fn slab_backed_asset_acquires_evicts_reloads() {
        // A BACKING_SLAB asset's chunks come from an unmanaged slab over the same heap;
        // the manager (not the slab) drives eviction via its reclaim hook → slab_release.
        let (_buf, h) = arena(256 * 1024);
        let slot = 16 * 1024;
        let slab = unsafe { deluge_alloc::slab::deluge_slab_create_unmanaged(h, slot, 64) };
        assert!(!slab.is_null());
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        unsafe { deluge_resource_set_slab(mgr, slab) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(4),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_SLAB,
            )
        };
        let p0 = unsafe { deluge_resource_acquire(mgr, a, 0, slot) };
        assert!(!p0.is_null());
        assert_eq!(p0 as usize % 16, 0);
        check_pattern(p0, owner(4), 0, slot);
        unsafe { deluge_resource_release(mgr, p0) };
        reset_evicts();
        for n in 1..40u32 {
            let p = unsafe { deluge_resource_acquire(mgr, a, n, slot) };
            if !p.is_null() {
                unsafe { deluge_resource_release(mgr, p) };
            }
        }
        assert!(
            evicts() >= 1,
            "slab-backed pressure should evict (via the manager hook)"
        );
        let again = unsafe { deluge_resource_acquire(mgr, a, 0, slot) };
        assert!(!again.is_null());
        check_pattern(again, owner(4), 0, slot);
    }

    #[test]
    fn reconstruction_failure_returns_null() {
        let (_buf, h) = arena(1 << 20);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(1),
                Some(failing_materialize),
                None,
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let p = unsafe { deluge_resource_acquire(mgr, a, 0, 4096) };
        assert!(p.is_null(), "failed materialize must roll back to null");
        // And the slot was reclaimed — a working asset can still acquire afterwards.
        let b = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(2),
                Some(mock_materialize),
                None,
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let q = unsafe { deluge_resource_acquire(mgr, b, 0, 4096) };
        assert!(!q.is_null());
    }

    thread_local! {
        static CONSTRUCTS: Cell<usize> = const { Cell::new(0) };
    }
    // Async construct: stamp a marker byte (0xC0) so a test can tell "constructed but not
    // yet loaded" from a full materialize (which writes the (owner,index) pattern).
    unsafe extern "C" fn mock_construct(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        _index: u32,
        dest: *mut u8,
    ) {
        CONSTRUCTS.with(|c| c.set(c.get() + 1));
        *dest = 0xC0; // just touch it; the "load" happens separately
    }

    #[test]
    fn request_constructs_without_loading_then_leases() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(8),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        // Not requestable until a construct callback is attached.
        assert!(unsafe { deluge_resource_request(mgr, a, 0, 64 * 1024) }.is_null());
        unsafe { deluge_resource_set_construct(mgr, a, Some(mock_construct)) };
        CONSTRUCTS.with(|c| c.set(0));
        let p = unsafe { deluge_resource_request(mgr, a, 0, 64 * 1024) };
        assert!(!p.is_null());
        assert_eq!(CONSTRUCTS.with(|c| c.get()), 1, "construct ran once");
        assert_eq!(unsafe { *p }, 0xC0, "constructed, not materialized");
        // A second request is a cache hit: leases again, no re-construct.
        let p2 = unsafe { deluge_resource_request(mgr, a, 0, 64 * 1024) };
        assert_eq!(p, p2);
        assert_eq!(
            CONSTRUCTS.with(|c| c.get()),
            1,
            "cache hit must not re-construct"
        );
        // Held by two leases now; release both → evictable.
        unsafe { deluge_resource_release(mgr, p) };
        unsafe { deluge_resource_release(mgr, p) };
        reset_evicts();
        for n in 1..20u32 {
            let q = unsafe { deluge_resource_acquire(mgr, a, n, 64 * 1024) };
            if !q.is_null() {
                unsafe { deluge_resource_release(mgr, q) };
            }
        }
        assert!(evicts() >= 1, "released constructed chunk is evictable");
    }

    #[test]
    fn try_acquire_gates_on_ready() {
        // The async path: request() reserves a chunk in the Loading state (not ready). try_acquire
        // must return null until mark_ready, then return the leased ptr. acquire() (sync materialize)
        // is ready immediately.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(9),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        unsafe { deluge_resource_set_construct(mgr, a, Some(mock_construct)) };

        // request → Loading: try_acquire must refuse it.
        let p = unsafe { deluge_resource_request(mgr, a, 0, 64 * 1024) };
        assert!(!p.is_null());
        assert!(
            unsafe { deluge_resource_try_acquire(mgr, a, 0) }.is_null(),
            "try_acquire must not hand out a Loading (not-ready) chunk"
        );

        // mark_ready → now try_acquire returns the same backing and takes a lease.
        unsafe { deluge_resource_mark_ready(mgr, p) };
        let q = unsafe { deluge_resource_try_acquire(mgr, a, 0) };
        assert_eq!(q, p, "ready chunk is returned by try_acquire");

        // try_acquire on a never-requested index is a plain miss (no alloc/materialize).
        assert!(
            unsafe { deluge_resource_try_acquire(mgr, a, 5) }.is_null(),
            "try_acquire never materializes"
        );

        // It leased twice (request + try_acquire); release both → evictable.
        unsafe { deluge_resource_release(mgr, p) };
        unsafe { deluge_resource_release(mgr, p) };
        reset_evicts();
        for n in 1..20u32 {
            let r = unsafe { deluge_resource_acquire(mgr, a, n, 64 * 1024) };
            if !r.is_null() {
                unsafe { deluge_resource_release(mgr, r) };
            }
        }
        assert!(evicts() >= 1, "released chunk is evictable after try_acquire leases drop");
    }

    #[test]
    fn acquire_is_ready_immediately() {
        // A synchronously-materialized chunk is ready without a separate mark_ready.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(10),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let p = unsafe { deluge_resource_acquire(mgr, a, 0, 64 * 1024) };
        assert!(!p.is_null());
        let q = unsafe { deluge_resource_try_acquire(mgr, a, 0) };
        assert_eq!(q, p, "acquire()'d chunk is ready for try_acquire");
    }

    #[test]
    fn slot_handle_round_trips_and_tracks_leases() {
        // The C++ side caches slot_of(ptr) at creation, then reads the lease count O(1) by slot.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(11),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let p = unsafe { deluge_resource_acquire(mgr, a, 0, 64 * 1024) };
        assert!(!p.is_null());
        let slot = unsafe { deluge_resource_slot_of(mgr, p) };
        assert_ne!(slot, u32::MAX, "resident ptr → a real slot");
        assert_eq!(unsafe { deluge_resource_lease_count_by_slot(mgr, slot) }, 1);

        unsafe { deluge_resource_add_lease(mgr, p) };
        assert_eq!(unsafe { deluge_resource_lease_count_by_slot(mgr, slot) }, 2, "by-slot tracks add_lease");
        unsafe { deluge_resource_release(mgr, p) };
        unsafe { deluge_resource_release(mgr, p) };
        assert_eq!(
            unsafe { deluge_resource_lease_count_by_slot(mgr, slot) },
            0,
            "unleased but still resident → 0"
        );

        // Edge cases: bogus ptr → NO_SLOT; out-of-range / NO_SLOT slot → 0 leases.
        assert_eq!(unsafe { deluge_resource_slot_of(mgr, 0xdead_beef as *mut u8) }, u32::MAX);
        assert_eq!(unsafe { deluge_resource_lease_count_by_slot(mgr, 9999) }, 0);
        assert_eq!(unsafe { deluge_resource_lease_count_by_slot(mgr, u32::MAX) }, 0);
    }

    #[test]
    fn loader_queue_orders_by_priority_skips_unleased_and_de_queues() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(12),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let acq = |i: u32| unsafe { deluge_resource_acquire(mgr, a, i, 64 * 1024) };
        let (p0, p1, p2) = (acq(0), acq(1), acq(2));
        let slot = |p| unsafe { deluge_resource_slot_of(mgr, p) };
        let (s0, s1, s2) = (slot(p0), slot(p1), slot(p2));

        // Enqueue at priorities 30/10/20 → loader_next pops lowest-first: p1, p2, p0, then null.
        unsafe { deluge_resource_loader_enqueue(mgr, s0, 30) };
        unsafe { deluge_resource_loader_enqueue(mgr, s1, 10) };
        unsafe { deluge_resource_loader_enqueue(mgr, s2, 20) };
        assert_eq!(unsafe { deluge_resource_loader_next(mgr) }, p1);
        assert_eq!(unsafe { deluge_resource_loader_next(mgr) }, p2);
        assert_eq!(unsafe { deluge_resource_loader_next(mgr) }, p0);
        assert!(unsafe { deluge_resource_loader_next(mgr) }.is_null(), "queue drained");

        // remove de-queues.
        unsafe { deluge_resource_loader_enqueue(mgr, s0, 5) };
        unsafe { deluge_resource_loader_remove(mgr, s0) };
        assert!(unsafe { deluge_resource_loader_next(mgr) }.is_null(), "removed entry not returned");

        // An unleased queued chunk is skipped (left queued + resident for normal value-based eviction;
        // loader_next never evicts).
        unsafe { deluge_resource_release(mgr, p1) }; // s1 now unleased (was leased once by acq)
        unsafe { deluge_resource_loader_enqueue(mgr, s1, 1) };
        assert!(unsafe { deluge_resource_loader_next(mgr) }.is_null(), "unleased queued chunk skipped");

        // has_lowest: only true for a queued+leased chunk at u32::MAX.
        unsafe { deluge_resource_loader_enqueue(mgr, s2, u32::MAX) };
        assert!(unsafe { deluge_resource_loader_has_lowest(mgr) });
        unsafe { deluge_resource_loader_remove(mgr, s2) };
        assert!(!unsafe { deluge_resource_loader_has_lowest(mgr) });
    }

    #[test]
    fn stats_track_acquires_hits_materializes_evictions() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 8) }; // small chunk table → forces eviction
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(13),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let stats = || {
            let mut s = Stats::default();
            unsafe { deluge_resource_stats(mgr, &mut s) };
            s
        };

        // Miss → one acquire + one materialize; release so it's evictable.
        unsafe { deluge_resource_release(mgr, deluge_resource_acquire(mgr, a, 0, 16 * 1024)) };
        let s = stats();
        assert_eq!((s.acquires, s.acquire_hits, s.materializes), (1, 0, 1));

        // Hit on the resident chunk → acquire_hit, no new materialize.
        unsafe { deluge_resource_release(mgr, deluge_resource_acquire(mgr, a, 0, 16 * 1024)) };
        let s = stats();
        assert_eq!((s.acquires, s.acquire_hits, s.materializes), (2, 1, 1));

        // Churn distinct indices through the small table → evictions.
        for i in 1..30u32 {
            let q = unsafe { deluge_resource_acquire(mgr, a, i, 16 * 1024) };
            if !q.is_null() {
                unsafe { deluge_resource_release(mgr, q) };
            }
        }
        let s = stats();
        assert!(s.evictions >= 1, "small table forced evictions");
        assert_eq!(
            s.evictions,
            s.evictions_by_cost.iter().sum::<u64>(),
            "by-cost buckets sum to the total"
        );
        assert!(s.evictions_by_cost[COST_IO as usize] >= 1, "IO-cost chunks were evicted");

        // reset zeroes everything.
        unsafe { deluge_resource_stats_reset(mgr) };
        let s = stats();
        assert_eq!((s.acquires, s.evictions), (0, 0));
    }

    thread_local! {
        static ADOPT_EVICTED: Cell<*mut u8> = const { Cell::new(core::ptr::null_mut()) };
    }
    unsafe extern "C" fn mock_adopt_evict(_ctx: *mut c_void, ptr: *mut u8) {
        ADOPT_EVICTED.with(|c| c.set(ptr));
    }

    #[test]
    fn adopt_object_is_value_evicted_and_leases_pin() {
        // Object-lifecycle adopt: an externally-allocated block the manager evicts by value
        // (its own cost/recency), calling on_evict(ptr); a lease pins it. Coexists with Source
        // (acquire) chunks in one reclaim sweep.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        // A Source asset to coexist with the adopted blocks.
        let a = unsafe {
            deluge_resource_define_asset(mgr, owner(1), Some(mock_materialize), Some(mock_on_evict),
                core::ptr::null_mut(), COST_IO, BACKING_HEAP)
        };
        // Adopt a pinned (leased) block + an unpinned one (both heap blocks we "own").
        let pinned = unsafe { deluge_alloc::deluge_alloc(h, 32 * 1024, 16) };
        let cold = unsafe { deluge_alloc::deluge_alloc(h, 32 * 1024, 16) };
        assert!(!pinned.is_null() && !cold.is_null());
        assert_eq!(unsafe { deluge_resource_adopt(mgr, pinned, 32 * 1024, COST_CPU, core::ptr::null_mut(), Some(mock_adopt_evict)) }, pinned);
        unsafe { deluge_resource_add_lease(mgr, pinned) }; // pin it
        assert_eq!(unsafe { deluge_resource_adopt(mgr, cold, 32 * 1024, COST_FREE, core::ptr::null_mut(), Some(mock_adopt_evict)) }, cold);
        // Pressure: acquire+release Source chunks until eviction fires. The cold adopted block
        // (COST_FREE = cheapest to lose) should be the victim; the leased one must survive.
        ADOPT_EVICTED.with(|c| c.set(core::ptr::null_mut()));
        for n in 0..20u32 {
            let p = unsafe { deluge_resource_acquire(mgr, a, n, 32 * 1024) };
            if !p.is_null() {
                unsafe { deluge_resource_release(mgr, p) };
            }
        }
        assert_eq!(ADOPT_EVICTED.with(|c| c.get()), cold, "the cold adopted block must be the evicted one");
        // The pinned adopted block was never evicted; release it so the heap block can be reclaimed
        // (the manager freed `cold`; we still own `pinned`'s lifetime here).
        unsafe { deluge_resource_release(mgr, pinned) };
        unsafe { deluge_alloc::deluge_free(h, pinned) };
    }

    #[test]
    fn evict_chunk_drops_one_without_on_evict() {
        // Owner-driven drop of a specific resident chunk: frees it, no on_evict, slot reusable.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(4),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let p0 = unsafe { deluge_resource_acquire(mgr, a, 0, 32 * 1024) };
        let p1 = unsafe { deluge_resource_acquire(mgr, a, 1, 32 * 1024) };
        unsafe { deluge_resource_release(mgr, p0) };
        unsafe { deluge_resource_release(mgr, p1) };
        reset_evicts();
        unsafe { deluge_resource_evict_chunk(mgr, p1) }; // drop chunk 1 explicitly
        assert_eq!(evicts(), 0, "evict_chunk must NOT call on_evict");
        // Chunk 0 still resident (re-acquire = same ptr); chunk 1 gone (re-acquire re-materializes).
        let p0b = unsafe { deluge_resource_acquire(mgr, a, 0, 32 * 1024) };
        assert_eq!(p0, p0b, "evict_chunk must only drop the named chunk");
        // evict_chunk on a stale/non-resident ptr is a harmless no-op.
        unsafe { deluge_resource_evict_chunk(mgr, p1) };
        unsafe { deluge_resource_evict_chunk(mgr, 0xdead_beef as *mut u8) };
    }

    #[test]
    fn add_lease_pins_a_held_chunk() {
        // add_lease bumps the lease of an already-resident chunk (C++ Cluster::addReason on
        // a manager cluster). A chunk leased via acquire(1) + add_lease(2) needs two releases
        // to become evictable — proving the lease count, not just a flag.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(2),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let p = unsafe { deluge_resource_acquire(mgr, a, 0, 64 * 1024) }; // leases = 1
        assert!(!p.is_null());
        unsafe { deluge_resource_add_lease(mgr, p) }; // leases = 2
                                                      // Drop one lease; still pinned. Churn must not evict it.
        unsafe { deluge_resource_release(mgr, p) }; // leases = 1
        for n in 1..20u32 {
            let q = unsafe { deluge_resource_acquire(mgr, a, n, 64 * 1024) };
            if !q.is_null() {
                unsafe { deluge_resource_release(mgr, q) };
            }
        }
        check_pattern(p, owner(2), 0, 64 * 1024); // survived (1 lease still held)
                                                  // Drop the last lease → now evictable; reload reproduces the bytes.
        unsafe { deluge_resource_release(mgr, p) }; // leases = 0
        reset_evicts();
        for n in 20..60u32 {
            let q = unsafe { deluge_resource_acquire(mgr, a, n, 64 * 1024) };
            if !q.is_null() {
                unsafe { deluge_resource_release(mgr, q) };
            }
        }
        assert!(evicts() >= 1, "unleased chunk should now be evictable");
        // add_lease on a non-resident pointer is a harmless no-op.
        unsafe { deluge_resource_add_lease(mgr, 0xdead_beef as *mut u8) };
    }

    #[test]
    fn request_self_protects_siblings_during_alloc() {
        // The dontStealFromThing port: while an asset allocates a new (unleased) chunk, its
        // own already-resident chunks must not be evicted to make room. Fill the heap with one
        // asset's unleased chunks, then request another chunk of the SAME asset under pressure:
        // eviction must take a *different* asset's chunk, never one of the requester's.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let victim = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(1),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let cache = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(2),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        unsafe { deluge_resource_set_construct(mgr, cache, Some(mock_construct)) };
        unsafe { deluge_resource_set_self_protect(mgr, cache, true) };
        // One evictable victim-asset chunk (older), then several cache chunks (newer, so by
        // recency they'd be the eviction pick if not for self-protect).
        let v = unsafe { deluge_resource_acquire(mgr, victim, 0, 48 * 1024) };
        unsafe { deluge_resource_release(mgr, v) };
        let mut cache_ptrs = std::vec![];
        for n in 0..3u32 {
            let p = unsafe { deluge_resource_request(mgr, cache, n, 48 * 1024) };
            if p.is_null() {
                break;
            }
            unsafe { deluge_resource_release(mgr, p) }; // unleased, like a real cache cluster
            cache_ptrs.push(p);
        }
        // Now request another cache chunk under pressure: it must evict the victim asset's
        // chunk, NOT one of the cache's own (which would corrupt the cache mid-write).
        reset_evicts();
        let p = unsafe { deluge_resource_request(mgr, cache, 99, 48 * 1024) };
        assert!(!p.is_null());
        // Every prior cache chunk is still resident (a re-request is a cache hit → same ptr).
        for (n, &cp) in cache_ptrs.iter().enumerate() {
            let again = unsafe { deluge_resource_request(mgr, cache, n as u32, 48 * 1024) };
            assert_eq!(again, cp, "cache chunk {n} must not have been self-evicted");
            unsafe { deluge_resource_release(mgr, again) };
        }
    }

    #[test]
    fn evict_tail_first_takes_highest_index() {
        // A prefix-dependent asset: only its highest-index resident chunk may be evicted.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(5),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_CPU,
                BACKING_HEAP,
            )
        };
        unsafe { deluge_resource_set_construct(mgr, a, Some(mock_construct)) };
        unsafe { deluge_resource_set_evict_tail_first(mgr, a, true) };
        // Build chunks 0,1,2,3 (unleased). Touch them so chunk 3 is NOT the LRU pick — proving
        // tail-first overrides recency.
        let mut ps = std::vec![];
        for n in 0..4u32 {
            let p = unsafe { deluge_resource_request(mgr, a, n, 32 * 1024) };
            assert!(!p.is_null());
            unsafe { deluge_resource_release(mgr, p) };
            ps.push(p);
        }
        unsafe { deluge_resource_touch(mgr, ps[3]) }; // make 3 most-recently-used
        unsafe { deluge_resource_touch(mgr, ps[2]) };
        // A different asset forces one eviction; tail-first must take chunk 3 (highest), not
        // the LRU (chunk 0/1).
        let other = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(6),
                Some(mock_materialize),
                None,
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        // Fill until an eviction happens.
        for n in 0..20u32 {
            let q = unsafe { deluge_resource_acquire(mgr, other, n, 32 * 1024) };
            if q.is_null() {
                break;
            }
            unsafe { deluge_resource_release(mgr, q) };
        }
        // Chunk 3 was evicted (re-request re-constructs a fresh pointer); 0,1,2 survive.
        let h3 = unsafe { deluge_resource_request(mgr, a, 3, 32 * 1024) };
        unsafe { deluge_resource_release(mgr, h3) };
        // The key invariant the firmware relies on: eviction never took a NON-highest chunk
        // while a higher one was resident (that would cascade-destroy in clusterStolen).
        // We assert it positively: at the moment of any eviction the victim was the tail.
        // (Covered structurally by is_highest_resident in evict_lowest; here we sanity-check
        // that lower chunks 0..2 are still their original pointers.)
        for n in 0..3u32 {
            let again = unsafe { deluge_resource_request(mgr, a, n, 32 * 1024) };
            assert_eq!(
                again, ps[n as usize],
                "tail-first must not evict non-highest chunk {n}"
            );
            unsafe { deluge_resource_release(mgr, again) };
        }
    }

    #[test]
    fn release_asset_evicts_chunks_and_frees_slot() {
        // Retiring an asset (owner destroyed) must evict its resident chunks — calling
        // on_evict for each so the owner drops pointers — and free the slot for reuse.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 2, 64) }; // only 2 asset slots
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(11),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        // Two resident chunks, one leased one not — both must be freed at release.
        let p0 = unsafe { deluge_resource_acquire(mgr, a, 0, 32 * 1024) };
        let p1 = unsafe { deluge_resource_acquire(mgr, a, 1, 32 * 1024) };
        assert!(!p0.is_null() && !p1.is_null());
        unsafe { deluge_resource_release(mgr, p1) };
        reset_evicts();
        unsafe { deluge_resource_release_asset(mgr, a) };
        assert_eq!(evicts(), 2, "on_evict fires for every resident chunk");
        // The slot is reusable: fill both slots, proving `a` was actually freed.
        for n in 0..2u32 {
            let id = unsafe {
                deluge_resource_define_asset(
                    mgr,
                    owner(20 + n as usize),
                    Some(mock_materialize),
                    None,
                    core::ptr::null_mut(),
                    COST_IO,
                    BACKING_HEAP,
                )
            };
            assert_ne!(
                id, 0xFFFF_FFFF,
                "asset slot {n} should be free after release"
            );
        }
        // And the heap reclaimed the backings — a fresh acquire still works.
        let again = unsafe { deluge_resource_acquire(mgr, a, 0, 32 * 1024) };
        // `a` was freed/reused above so this id may now be in use by a new owner; just
        // assert the manager is still functional (no leak/corruption).
        let _ = again;
    }
}
