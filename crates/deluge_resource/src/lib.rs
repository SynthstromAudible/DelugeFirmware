//! The Deluge's cached-asset resource manager, behind the C ABI in `deluge_resource.h`.
//!
//! A value-scored, lease-based cache of reconstructable assets on the `deluge_alloc` TLSF heap.
//! [`manager`] holds the core and the C ABI, [`facade`] a safe Rust interface, [`value`] the
//! eviction policy, and [`sync`] the critical-section helpers every shared access goes through.
//!
//! A `no_std` rlib with no `#[panic_handler]` (the final binary provides it) and no global
//! allocator: all bookkeeping is fixed-capacity, so the heap's reclaim hook can never re-enter the
//! manager through an allocation of its own.

#![cfg_attr(target_os = "none", no_std)]
#![allow(clippy::missing_safety_doc)]

pub mod facade;
pub mod manager;
pub mod sync;
pub mod value;

#[cfg(test)]
mod testing;

pub use facade::{Chunk, Resource};
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
        // Any churn sequence must hold the harness invariants.
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
    // A chunk's contents, derived from (owner, index) so a reload is verifiable.
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

    // A heap over a `Vec<u128>`: miri tracks declared alignment, and a `Vec<u8>` arena would
    // flag the TLSF's 16-aligned headers. The caller keeps the buffer alive (no leak for miri).
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
    fn cache_hit_same_asset_index_still_leases() {
        // The identity re-validation must not reject a genuine cache hit.
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
        let p1 = unsafe { deluge_resource_acquire(mgr, a, 0, 4096) };
        assert!(!p1.is_null());
        let p2 = unsafe { deluge_resource_acquire(mgr, a, 0, 4096) };
        assert!(!p2.is_null());
        assert_eq!(
            p1, p2,
            "same (asset,index) must hit the same resident chunk"
        );

        let mut s = Stats::default();
        unsafe { deluge_resource_stats(mgr, &mut s) };
        assert!(
            s.acquire_hits >= 1,
            "the second acquire must have been recorded as a cache hit"
        );
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
        // The fuzz entry point on deterministic bytes.
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
        // BACKING_SLAB chunks come from an unmanaged slab; the manager drives their eviction.
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
    // Stamps 0xC0, so a test can tell "constructed, not loaded" from a materialize.
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
        // A request()ed chunk is not ready, so try_acquire misses until mark_ready.
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
        assert!(
            evicts() >= 1,
            "released chunk is evictable after try_acquire leases drop"
        );
    }

    #[test]
    fn acquire_is_ready_immediately() {
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
        assert_eq!(
            unsafe { deluge_resource_lease_count_by_slot(mgr, slot) },
            2,
            "by-slot tracks add_lease"
        );
        unsafe { deluge_resource_release(mgr, p) };
        unsafe { deluge_resource_release(mgr, p) };
        assert_eq!(
            unsafe { deluge_resource_lease_count_by_slot(mgr, slot) },
            0,
            "unleased but still resident → 0"
        );

        // Edge cases: bogus ptr → NO_SLOT; out-of-range / NO_SLOT slot → 0 leases.
        assert_eq!(
            unsafe { deluge_resource_slot_of(mgr, 0xdead_beef as *mut u8) },
            u32::MAX
        );
        assert_eq!(unsafe { deluge_resource_lease_count_by_slot(mgr, 9999) }, 0);
        assert_eq!(
            unsafe { deluge_resource_lease_count_by_slot(mgr, u32::MAX) },
            0
        );
    }

    #[test]
    fn chunk_ident_c_abi_reports_asset_and_index_and_false_on_a_miss() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
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
        let p = unsafe { deluge_resource_acquire(mgr, a, 3, 64 * 1024) };
        assert!(!p.is_null());

        let (mut out_asset, mut out_index) = (u32::MAX, u32::MAX);
        let hit = unsafe { deluge_resource_chunk_ident(mgr, p, &mut out_asset, &mut out_index) };
        assert!(hit);
        assert_eq!(out_asset, a);
        assert_eq!(out_index, 3);

        // A pointer the manager never handed out reports false, leaving the out-params untouched.
        let (mut miss_asset, mut miss_index) = (0xAAAA_AAAAu32, 0xBBBB_BBBBu32);
        let miss = unsafe {
            deluge_resource_chunk_ident(
                mgr,
                0xdead_beef as *mut u8,
                &mut miss_asset,
                &mut miss_index,
            )
        };
        assert!(!miss);
        assert_eq!(miss_asset, 0xAAAA_AAAA, "out-param untouched on a miss");
        assert_eq!(miss_index, 0xBBBB_BBBB, "out-param untouched on a miss");

        // A null handle also reports false rather than dereferencing anything.
        assert!(!unsafe {
            deluge_resource_chunk_ident(core::ptr::null_mut(), p, &mut out_asset, &mut out_index)
        });
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
        assert!(
            unsafe { deluge_resource_loader_next(mgr) }.is_null(),
            "queue drained"
        );

        // remove de-queues.
        unsafe { deluge_resource_loader_enqueue(mgr, s0, 5) };
        unsafe { deluge_resource_loader_remove(mgr, s0) };
        assert!(
            unsafe { deluge_resource_loader_next(mgr) }.is_null(),
            "removed entry not returned"
        );

        // An unleased queued chunk is skipped and left resident for normal eviction.
        unsafe { deluge_resource_release(mgr, p1) }; // s1 now unleased (was leased once by acq)
        unsafe { deluge_resource_loader_enqueue(mgr, s1, 1) };
        assert!(
            unsafe { deluge_resource_loader_next(mgr) }.is_null(),
            "unleased queued chunk skipped"
        );

        // has_lowest: only true for a queued+leased chunk at u32::MAX.
        unsafe { deluge_resource_loader_enqueue(mgr, s2, u32::MAX) };
        assert!(unsafe { deluge_resource_loader_has_lowest(mgr) });
        unsafe { deluge_resource_loader_remove(mgr, s2) };
        assert!(!unsafe { deluge_resource_loader_has_lowest(mgr) });
    }

    /// The queue's scans visit only queued slots, through a bitset a scan clears lazily: a slot
    /// de-queued and then queued again must be found again, as must one evicted and reused.
    #[test]
    fn a_slot_queued_again_after_its_stale_bit_is_cleared_is_found() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(15),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        let p0 = unsafe { deluge_resource_acquire(mgr, a, 0, 64 * 1024) };
        let s0 = unsafe { deluge_resource_slot_of(mgr, p0) };

        unsafe { deluge_resource_loader_enqueue(mgr, s0, 3) };
        unsafe { deluge_resource_loader_remove(mgr, s0) };
        // This scan finds the bit stale and clears it.
        assert!(!unsafe { deluge_resource_loader_has_any(mgr) });
        unsafe { deluge_resource_loader_enqueue(mgr, s0, 3) };
        assert!(unsafe { deluge_resource_loader_has_any(mgr) });
        assert_eq!(unsafe { deluge_resource_loader_next(mgr) }, p0);
        assert!(unsafe { deluge_resource_loader_next(mgr) }.is_null());

        // A slot in the last bitset word, past the first 32, is found too.
        let ps: Vec<_> = (1..40)
            .map(|i| unsafe { deluge_resource_acquire(mgr, a, i, 1024) })
            .collect();
        let last = *ps.last().unwrap();
        let s_last = unsafe { deluge_resource_slot_of(mgr, last) };
        assert!(
            s_last >= 32,
            "the test needs a slot in the second word, got {s_last}"
        );
        unsafe { deluge_resource_loader_enqueue(mgr, s_last, 1) };
        assert_eq!(unsafe { deluge_resource_loader_next(mgr) }, last);
    }

    /// Every queue wait and fill the stats count, from either enqueue, also lands in exactly one
    /// histogram bucket.
    #[test]
    fn latency_histograms_count_every_sample() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        let a = unsafe {
            deluge_resource_define_asset(
                mgr,
                owner(14),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        for i in 0..3 {
            let p = unsafe { deluge_resource_acquire(mgr, a, i, 4096) };
            assert!(!p.is_null());
            let slot = unsafe { deluge_resource_slot_of(mgr, p) };
            // The fill task's own requests come in through the owned enqueue; it is timed too.
            if i == 2 {
                unsafe { deluge_resource_loader_enqueue_owned(mgr, slot, 0) };
            } else {
                unsafe { deluge_resource_loader_enqueue(mgr, slot, 0) };
            }
            assert_eq!(unsafe { deluge_resource_loader_next(mgr) }, p);
            unsafe { deluge_resource_mark_ready(mgr, p) };
            if i == 2 {
                unsafe { deluge_resource_loader_release_owned(mgr, slot) };
            }
        }
        let mut s = Stats::default();
        unsafe { deluge_resource_stats(mgr, &mut s) };
        assert_eq!(s.queue_wait_samples, 3);
        assert_eq!(s.fill_samples, 3);
        let sum = |b: &[u32]| b.iter().map(|&n| u64::from(n)).sum::<u64>();
        assert_eq!(sum(&s.queue_wait_buckets), s.queue_wait_samples);
        assert_eq!(sum(&s.fill_buckets), s.fill_samples);
    }

    /// A re-enqueue of a still-queued chunk only ever makes it more urgent: a less urgent
    /// requester (a lowest-priority reservation re-requesting a chunk a voice is waiting on) must
    /// not push it to the back of the queue. Once popped, an enqueue starts afresh, so a failed
    /// read can still be requeued at the lowest priority.
    #[test]
    fn loader_re_enqueue_only_raises_priority_while_queued() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
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
        let acq = |i: u32| unsafe { deluge_resource_acquire(mgr, a, i, 64 * 1024) };
        let (p0, p1, p2) = (acq(0), acq(1), acq(2));
        let slot = |p| unsafe { deluge_resource_slot_of(mgr, p) };
        let (s0, s1, s2) = (slot(p0), slot(p1), slot(p2));

        // Plain protocol: s0 is wanted urgently, then re-requested at the lowest priority.
        unsafe { deluge_resource_loader_enqueue(mgr, s0, 10) };
        unsafe { deluge_resource_loader_enqueue(mgr, s1, 20) };
        unsafe { deluge_resource_loader_enqueue(mgr, s0, u32::MAX) };
        assert!(
            !unsafe { deluge_resource_loader_has_lowest(mgr) },
            "a less urgent re-enqueue keeps the queued priority"
        );
        // A more urgent re-enqueue still raises it.
        unsafe { deluge_resource_loader_enqueue(mgr, s1, 5) };
        assert_eq!(unsafe { deluge_resource_loader_next(mgr) }, p1);
        assert_eq!(unsafe { deluge_resource_loader_next(mgr) }, p0);

        // Once popped, an enqueue sets the priority afresh (the failed-read requeue).
        unsafe { deluge_resource_loader_enqueue(mgr, s0, u32::MAX) };
        assert!(unsafe { deluge_resource_loader_has_lowest(mgr) });
        unsafe { deluge_resource_loader_remove(mgr, s0) };

        // The owned protocol follows the same rule.
        unsafe { deluge_resource_loader_enqueue_owned(mgr, s2, 10) };
        unsafe { deluge_resource_loader_enqueue_owned(mgr, s2, u32::MAX) };
        assert!(!unsafe { deluge_resource_loader_has_lowest(mgr) });
        assert_eq!(unsafe { deluge_resource_loader_next(mgr) }, p2);
        unsafe { deluge_resource_loader_release_owned(mgr, s2) };
    }

    /// `loader_next` serves only leased chunks, so a plain entry whose enqueuer drops its lease is
    /// never loaded; `loader_enqueue_owned` gives the queue its own lease. Also covers the release
    /// discipline that keeps a queue lease from pinning the chunk forever.
    #[test]
    fn loader_enqueue_owned_survives_the_requester_dropping_its_lease() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
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
        let acq = |i: u32| unsafe { deluge_resource_acquire(mgr, a, i, 64 * 1024) };
        let slot = |p| unsafe { deluge_resource_slot_of(mgr, p) };
        let leases = |s| unsafe { deluge_resource_lease_count_by_slot(mgr, s) };
        let external = |s| unsafe { deluge_resource_external_lease_count_by_slot(mgr, s) };
        let (p0, p1) = (acq(0), acq(1));
        let (s0, s1) = (slot(p0), slot(p1));

        // Plain protocol: the enqueuer drops its lease and the entry is skipped.
        unsafe { deluge_resource_loader_enqueue(mgr, s0, 0) };
        unsafe { deluge_resource_release(mgr, p0) };
        assert!(
            unsafe { deluge_resource_loader_next(mgr) }.is_null(),
            "plain enqueue: dropping the requester's lease discards the queue entry"
        );

        // The owned protocol: same sequence, but the queue holds a lease of its own.
        unsafe { deluge_resource_loader_enqueue_owned(mgr, s1, 0) };
        assert_eq!(leases(s1), 2, "the requester's lease plus the queue's own");
        assert_eq!(external(s1), 1, "external count excludes the queue's own");
        unsafe { deluge_resource_release(mgr, p1) }; // the requester walks away
        assert_eq!(external(s1), 0, "nobody outside the queue wants it now");
        assert_eq!(
            unsafe { deluge_resource_loader_next(mgr) },
            p1,
            "the owned entry survives the requester dropping its lease"
        );

        // The drain's terminal path releases it: nothing is left pinning the chunk.
        assert!(
            unsafe { deluge_resource_loader_release_owned(mgr, s1) },
            "reports that there was a queue lease to release"
        );
        assert_eq!(
            leases(s1),
            0,
            "no lease left behind — the chunk is evictable"
        );
        assert!(
            !unsafe { deluge_resource_loader_release_owned(mgr, s1) },
            "idempotent: a second release is a no-op, not an underflow"
        );

        // Idempotent in the lease: a re-enqueue updates the priority without taking a second one.
        unsafe { deluge_resource_loader_enqueue_owned(mgr, s1, 5) };
        unsafe { deluge_resource_loader_enqueue_owned(mgr, s1, 1) };
        assert_eq!(leases(s1), 1, "one queue lease however many enqueues");

        // Erasing an entry drops its queue lease too, or `loader_remove` would pin the chunk.
        unsafe { deluge_resource_loader_remove(mgr, s1) };
        assert_eq!(leases(s1), 0, "remove released the queue's lease");
        assert!(unsafe { deluge_resource_loader_next(mgr) }.is_null());

        // A chunk enqueued under the plain protocol has no queue lease to release.
        let p2 = acq(2);
        let s2 = slot(p2);
        unsafe { deluge_resource_loader_enqueue(mgr, s2, 0) };
        assert!(
            !unsafe { deluge_resource_loader_release_owned(mgr, s2) },
            "plain-protocol chunk: nothing for the queue to release"
        );
        assert_eq!(leases(s2), 1, "and its requester's own lease is untouched");
    }

    #[test]
    fn stats_track_acquires_hits_materializes_evictions() {
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 8) }; // small table forces eviction
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
        assert!(
            s.evictions_by_cost[COST_IO as usize] >= 1,
            "IO-cost chunks were evicted"
        );

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
        // Adopted blocks are evicted by rank alongside asset chunks, and a lease pins them.
        let (_buf, h) = arena(256 * 1024);
        let mgr = unsafe { deluge_resource_create(h, 16, 64) };
        // A Source asset to coexist with the adopted blocks.
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
        // Adopt a pinned (leased) block + an unpinned one (both heap blocks we "own").
        let pinned = unsafe { deluge_alloc::deluge_alloc(h, 32 * 1024, 16) };
        let cold = unsafe { deluge_alloc::deluge_alloc(h, 32 * 1024, 16) };
        assert!(!pinned.is_null() && !cold.is_null());
        assert_eq!(
            unsafe {
                deluge_resource_adopt(
                    mgr,
                    pinned,
                    32 * 1024,
                    COST_CPU,
                    core::ptr::null_mut(),
                    Some(mock_adopt_evict),
                )
            },
            pinned
        );
        unsafe { deluge_resource_add_lease(mgr, pinned) }; // pin it
        assert_eq!(
            unsafe {
                deluge_resource_adopt(
                    mgr,
                    cold,
                    32 * 1024,
                    COST_FREE,
                    core::ptr::null_mut(),
                    Some(mock_adopt_evict),
                )
            },
            cold
        );
        // Pressure: acquire+release Source chunks until eviction fires. The cold adopted block
        // (COST_FREE = cheapest to lose) should be the victim; the leased one must survive.
        ADOPT_EVICTED.with(|c| c.set(core::ptr::null_mut()));
        for n in 0..20u32 {
            let p = unsafe { deluge_resource_acquire(mgr, a, n, 32 * 1024) };
            if !p.is_null() {
                unsafe { deluge_resource_release(mgr, p) };
            }
        }
        assert_eq!(
            ADOPT_EVICTED.with(|c| c.get()),
            cold,
            "the cold adopted block must be the evicted one"
        );
        // The manager freed `cold`; `pinned` is still adopted, so free it here.
        unsafe { deluge_resource_release(mgr, pinned) };
        unsafe { deluge_alloc::deluge_free(h, pinned) };
    }

    #[test]
    fn evict_chunk_drops_one_without_on_evict() {
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
        // Chunk 0 still resident (re-acquire = same ptr); chunk 1 gone (re-acquire
        // re-materializes).
        let p0b = unsafe { deluge_resource_acquire(mgr, a, 0, 32 * 1024) };
        assert_eq!(p0, p0b, "evict_chunk must only drop the named chunk");
        // evict_chunk on a stale/non-resident ptr is a harmless no-op.
        unsafe { deluge_resource_evict_chunk(mgr, p1) };
        unsafe { deluge_resource_evict_chunk(mgr, 0xdead_beef as *mut u8) };
    }

    #[test]
    fn add_lease_pins_a_held_chunk() {
        // acquire + add_lease holds two leases, so it takes two releases to become evictable.
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
        unsafe { deluge_resource_release(mgr, p) }; // leases = 1: still pinned through churn
        for n in 1..20u32 {
            let q = unsafe { deluge_resource_acquire(mgr, a, n, 64 * 1024) };
            if !q.is_null() {
                unsafe { deluge_resource_release(mgr, q) };
            }
        }
        check_pattern(p, owner(2), 0, 64 * 1024); // survived
        unsafe { deluge_resource_release(mgr, p) }; // leases = 0: now evictable
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
        // While a self-protecting asset allocates under pressure, eviction must take another
        // asset's chunk, never one of its own.
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
        // Under pressure, this must evict the victim asset's chunk, not a cache chunk.
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
        // Chunks 0..=3, unleased, with 3 touched most recently: tail-first must override recency.
        let mut ps = std::vec![];
        for n in 0..4u32 {
            let p = unsafe { deluge_resource_request(mgr, a, n, 32 * 1024) };
            assert!(!p.is_null());
            unsafe { deluge_resource_release(mgr, p) };
            ps.push(p);
        }
        unsafe { deluge_resource_touch(mgr, ps[3]) }; // make 3 most-recently-used
        unsafe { deluge_resource_touch(mgr, ps[2]) };
        // Another asset forces an eviction, which must take chunk 3 rather than the LRU chunk.
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
        let h3 = unsafe { deluge_resource_request(mgr, a, 3, 32 * 1024) };
        unsafe { deluge_resource_release(mgr, h3) };
        // Chunks 0..3 must keep their original backings: a non-highest chunk was never evicted.
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
        // Retiring an asset frees its chunks (calling on_evict for each) and its slot.
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
        // One leased and one unleased chunk; both are freed.
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
        // `a`'s id may now belong to a new asset; only check the manager still works.
        let again = unsafe { deluge_resource_acquire(mgr, a, 0, 32 * 1024) };
        let _ = again;
    }

    #[test]
    fn eviction_revalidate_never_frees_leased() {
        reset_evicts();
        let mut buf = std::vec![0u128; (256 * 1024usize).div_ceil(16)];
        let heap = unsafe { deluge_heap_create(buf.as_mut_ptr() as *mut u8, buf.len() * 16) };
        let m = unsafe { deluge_resource_create(heap, 2, 2) }; // 2 chunk slots
        let asset = unsafe {
            deluge_resource_define_asset(
                m,
                owner(1),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                crate::COST_IO,
                BACKING_HEAP,
            )
        };
        // Fill both slots; keep a hard lease on the first.
        let p0 = unsafe { deluge_resource_acquire(m, asset, 0, 4096) };
        let p1 = unsafe { deluge_resource_acquire(m, asset, 1, 4096) };
        assert!(!p0.is_null() && !p1.is_null());
        unsafe { deluge_resource_release(m, p1) }; // slot 1 now evictable, slot 0 leased
        let p2 = unsafe { deluge_resource_acquire(m, asset, 2, 4096) };
        assert!(
            !p2.is_null(),
            "should have evicted the unleased slot and succeeded"
        );
        // The leased chunk (index 0) must still be resident + un-corrupted.
        assert_eq!(
            unsafe { *p0 },
            pattern(owner(1), 0),
            "leased chunk was wrongly evicted"
        );
        assert_eq!(evicts(), 1, "exactly the one unleased chunk was evicted");
        let _ = buf;
    }

    #[test]
    fn concurrent_lease_churn_holds_invariants() {
        use core::sync::atomic::{AtomicBool, Ordering};
        use std::sync::Arc;

        // The handle is shared across contexts, as on device; the arena outlives both threads.
        #[derive(Clone, Copy)]
        struct H(*mut crate::DelugeResource);
        // SAFETY: every shared access goes through the masked helpers under test.
        unsafe impl Send for H {}

        let mut buf = std::vec![0u128; (512 * 1024usize).div_ceil(16)];
        let heap = unsafe { deluge_heap_create(buf.as_mut_ptr() as *mut u8, buf.len() * 16) };
        let m = unsafe { deluge_resource_create(heap, 2, 16) };
        let asset = unsafe {
            deluge_resource_define_asset(
                m,
                owner(1),
                Some(mock_materialize),
                None,
                core::ptr::null_mut(),
                crate::COST_IO,
                BACKING_HEAP,
            )
        };
        let handle = H(m);
        let stop = Arc::new(AtomicBool::new(false));

        let stop_a = Arc::clone(&stop);
        let a = std::thread::spawn(move || {
            // Force whole-struct capture: disjoint capture would take just the raw-pointer field,
            // bypassing `H`'s Send.
            let handle = handle;
            let H(m) = handle;
            for _ in 0..20_000 {
                let p = unsafe { deluge_resource_acquire(m, asset, 0, 4096) };
                if !p.is_null() {
                    unsafe { deluge_resource_add_lease(m, p) };
                    unsafe { deluge_resource_touch(m, p) };
                    unsafe { deluge_resource_release(m, p) };
                    unsafe { deluge_resource_release(m, p) };
                }
                if stop_a.load(Ordering::Relaxed) {
                    break;
                }
            }
        });
        let b = std::thread::spawn(move || {
            let handle = handle; // whole-struct capture, as above
            let H(m) = handle;
            for i in 0..20_000u32 {
                let p = unsafe { deluge_resource_acquire(m, asset, i % 4, 4096) };
                if !p.is_null() {
                    unsafe { deluge_resource_mark_dirty(m, p, true) };
                    unsafe { deluge_resource_mark_dirty(m, p, false) };
                    unsafe { deluge_resource_release(m, p) };
                }
            }
        });
        a.join().unwrap();
        stop.store(true, Ordering::Relaxed);
        b.join().unwrap();
        // No panic or deadlock, and the cache is intact afterwards.
        let p = unsafe { deluge_resource_acquire(m, asset, 0, 4096) };
        assert!(!p.is_null(), "acquire after churn must succeed");
        check_pattern(p, owner(1), 0, 4096);
        unsafe { deluge_resource_release(m, p) };
        let _ = buf; // keep the arena alive to here
    }

    #[test]
    fn concurrent_enqueue_drain_holds_invariants() {
        // Enqueue from one thread races the drain (`loader_next` then `mark_ready`, as the
        // streaming fill task does) on another. Priorities vary independently of the index, so a
        // still-queued slot is re-enqueued with a different priority while it may be mid-pop.
        use core::sync::atomic::{AtomicBool, Ordering};
        use std::sync::Arc;

        // Genuine cross-thread contention on the masked helpers.
        #[derive(Clone, Copy)]
        struct H(*mut crate::DelugeResource);
        // SAFETY: every shared access goes through the masked helpers.
        unsafe impl Send for H {}

        const CHUNK_CAP: usize = 16;
        const NUM_INDICES: u32 = 4; // < CHUNK_CAP: leaves headroom, no eviction pressure
        const ITERS: u32 = 20_000;

        let mut buf = std::vec![0u128; (512 * 1024usize).div_ceil(16)];
        let heap = unsafe { deluge_heap_create(buf.as_mut_ptr() as *mut u8, buf.len() * 16) };
        let m = unsafe { deluge_resource_create(heap, 2, CHUNK_CAP) };
        let asset = unsafe {
            deluge_resource_define_asset(
                m,
                owner(1),
                Some(mock_materialize),
                None,
                core::ptr::null_mut(),
                crate::COST_IO,
                BACKING_HEAP,
            )
        };
        unsafe { deluge_resource_set_construct(m, asset, Some(mock_construct)) };
        let handle = H(m);
        let stop = Arc::new(AtomicBool::new(false));

        // Producer: request (leased, constructed) then enqueue. `priority` cycles on a slower
        // period than `index`, so re-enqueues change the priority.
        let stop_p = Arc::clone(&stop);
        let producer = std::thread::spawn(move || {
            // Force whole-struct capture (see concurrent_lease_churn_holds_invariants).
            let handle = handle;
            let H(m) = handle;
            for i in 0..ITERS {
                let index = i % NUM_INDICES;
                let priority = (i / NUM_INDICES) % 4;
                let p = unsafe { deluge_resource_request(m, asset, index, 4096) };
                if !p.is_null() {
                    let slot = unsafe { deluge_resource_slot_of(m, p) };
                    unsafe { deluge_resource_loader_enqueue(m, slot, priority) };
                }
                if stop_p.load(Ordering::Relaxed) {
                    break;
                }
            }
        });

        // Consumer: pop, mark ready, then drop the requester's lease.
        let consumer = std::thread::spawn(move || {
            let handle = handle; // whole-struct capture, as above
            let H(m) = handle;
            let mut drained = 0u32;
            for _ in 0..ITERS {
                let p = unsafe { deluge_resource_loader_next(m) };
                if !p.is_null() {
                    unsafe { deluge_resource_mark_ready(m, p) };
                    unsafe { deluge_resource_release(m, p) };
                    drained += 1;
                }
            }
            drained
        });

        producer.join().unwrap();
        stop.store(true, Ordering::Relaxed);
        let drained = consumer.join().unwrap();

        // Drain what the producer left queued.
        loop {
            let p = unsafe { deluge_resource_loader_next(m) };
            if p.is_null() {
                break;
            }
            unsafe { deluge_resource_mark_ready(m, p) };
            unsafe { deluge_resource_release(m, p) };
        }

        assert!(
            drained > 0,
            "the consumer should have drained at least one enqueued chunk"
        );

        // The manager is still coherent afterwards.
        let p = unsafe { deluge_resource_request(m, asset, 0, 4096) };
        assert!(!p.is_null(), "request after churn must succeed");
        unsafe { deluge_resource_release(m, p) };
        let _ = buf; // keep the arena alive to here
    }

    /// A manager with `chunk_cap` slots and one requestable asset, for the index tests.
    fn indexed_mgr(
        chunk_cap: usize,
        arena_bytes: usize,
    ) -> (
        Vec<u128>,
        *mut DelugeResource,
        u32,
        *mut deluge_alloc::DelugeHeap,
    ) {
        let (buf, h) = arena(arena_bytes);
        let m = unsafe { deluge_resource_create(h, 16, chunk_cap) };
        let a = unsafe {
            deluge_resource_define_asset(
                m,
                owner(1),
                Some(mock_materialize),
                Some(mock_on_evict),
                core::ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        unsafe { deluge_resource_set_construct(m, a, Some(mock_construct)) };
        (buf, m, a, h)
    }

    fn stats_of(m: *mut DelugeResource) -> Stats {
        let mut s = Stats::default();
        unsafe { deluge_resource_stats(m, &mut s) };
        s
    }

    /// A lookup miss must not walk the whole chunk table. Asserted on slots visited, which is
    /// exact, rather than time, which would be flaky.
    #[test]
    fn a_missing_key_does_not_scan_the_chunk_table() {
        let chunk_cap = 512;
        let (buf, m, a, _h) = indexed_mgr(chunk_cap, 4 * 1024 * 1024);
        // One resident chunk, so the table is non-trivially populated.
        let p = unsafe { deluge_resource_request(m, a, 0, 4096) };
        assert!(!p.is_null());

        unsafe { deluge_resource_stats_reset(m) };
        // A non-resident index: the miss path.
        assert!(unsafe { deluge_resource_peek(m, a, 999) }.is_null());
        let s = stats_of(m);

        assert!(s.scan_resident_calls >= 1, "the lookup should have run");
        assert!(
            s.scan_resident_slots < chunk_cap as u64 / 8,
            "a miss visited {} slots of {chunk_cap} — it is still scanning the table",
            s.scan_resident_slots
        );
        unsafe { deluge_resource_release(m, p) };
        let _ = buf;
    }

    /// A pointer lookup, which backs every pointer-keyed entry point, must not walk the table.
    #[test]
    fn a_pointer_lookup_does_not_scan_the_chunk_table() {
        let chunk_cap = 512;
        let (buf, m, a, _h) = indexed_mgr(chunk_cap, 8 * 1024 * 1024);
        // Populate enough slots that a linear scan would be obvious in the counter.
        let ptrs: Vec<*mut u8> = (0..64)
            .map(|i| {
                let p = unsafe { deluge_resource_request(m, a, i, 4096) };
                assert!(!p.is_null());
                p
            })
            .collect();

        unsafe { deluge_resource_stats_reset(m) };
        // The last one: the worst case for a linear scan.
        let last = *ptrs.last().unwrap();
        unsafe { deluge_resource_add_lease(m, last) };
        unsafe { deluge_resource_release(m, last) };
        let s = stats_of(m);

        assert!(s.scan_ptr_calls >= 1, "the lookup should have run");
        assert!(
            s.scan_ptr_slots < 16 * s.scan_ptr_calls,
            "a pointer lookup visited {} slots over {} calls — it is still scanning",
            s.scan_ptr_slots,
            s.scan_ptr_calls
        );
        unsafe { debug_assert_index_consistent(m) };
        for p in ptrs {
            unsafe { deluge_resource_release(m, p) };
        }
        let _ = buf;
    }

    /// The index agrees with a brute-force scan for every probed key across churn.
    ///
    /// A false positive is caught by re-validation, but a false negative is silent: the caller
    /// would allocate a second chunk for a resident key.
    #[test]
    fn indexed_lookup_agrees_with_a_brute_force_scan_across_churn() {
        // A small arena, so eviction runs and the churn exercises index removal too.
        let (buf, m, a, _h) = indexed_mgr(256, 512 * 1024);
        let mut leased: Vec<(u32, *mut u8)> = Vec::new();

        // Deterministic pseudo-random churn (xorshift): request some indices, release others.
        let mut x: u32 = 0x1234_5678;
        let next = |x: &mut u32| {
            *x ^= *x << 13;
            *x ^= *x >> 17;
            *x ^= *x << 5;
            *x
        };
        for _ in 0..300 {
            let idx = next(&mut x) % 40;
            if next(&mut x) % 3 == 0 {
                if let Some(pos) = leased.iter().position(|&(i, _)| i == idx) {
                    let (_, p) = leased.remove(pos);
                    unsafe { deluge_resource_release(m, p) }; // now evictable
                }
            } else if !leased.iter().any(|&(i, _)| i == idx) {
                let p = unsafe { deluge_resource_request(m, a, idx, 4096) };
                if !p.is_null() {
                    leased.push((idx, p));
                }
            }

            // Compare with the manager's own brute-force scan; eviction can drop any unleased
            // chunk, so the test cannot model residency itself.
            for probe in 0..48u32 {
                let (indexed, linear) = unsafe { debug_find_resident_both(m, a, probe) };
                assert_eq!(
                    indexed, linear,
                    "index disagrees with the linear scan for cluster {probe}"
                );
            }
        }
        unsafe { debug_assert_index_consistent(m) };
        for (_, p) in leased {
            unsafe { deluge_resource_release(m, p) };
        }
        let _ = buf;
    }

    /// `evict_chunk` and `release_asset` clear slots outside `evict_slot`; each must unlink them
    /// from the index, or a dangling link points into a recycled slot.
    #[test]
    fn teardown_paths_keep_the_index_consistent() {
        let (buf, m, a, _h) = indexed_mgr(128, 2 * 1024 * 1024);
        let ptrs: Vec<*mut u8> = (0..12)
            .map(|i| {
                let p = unsafe { deluge_resource_request(m, a, i, 4096) };
                assert!(!p.is_null());
                p
            })
            .collect();
        unsafe { debug_assert_index_consistent(m) };

        // evict_chunk: the owner discards two chunks outright.
        for p in ptrs.iter().take(2) {
            unsafe { deluge_resource_release(m, *p) };
            unsafe { deluge_resource_evict_chunk(m, *p) };
        }
        unsafe { debug_assert_index_consistent(m) };
        for probe in 0..12u32 {
            let (indexed, linear) = unsafe { debug_find_resident_both(m, a, probe) };
            assert_eq!(indexed, linear, "after evict_chunk, cluster {probe}");
        }

        // release_asset: full owner teardown drops every remaining chunk.
        for p in ptrs.iter().skip(2) {
            unsafe { deluge_resource_release(m, *p) };
        }
        unsafe { deluge_resource_release_asset(m, a) };
        unsafe { debug_assert_index_consistent(m) };
        for probe in 0..12u32 {
            let (indexed, linear) = unsafe { debug_find_resident_both(m, a, probe) };
            assert_eq!(indexed, linear, "after release_asset, cluster {probe}");
            assert!(indexed.is_none(), "cluster {probe} should be gone");
        }
        let _ = buf;
    }

    /// Adopted chunks, which are not in the key index, coexist with indexed lookups.
    #[test]
    fn adopted_chunks_stay_out_of_the_index_without_breaking_lookups() {
        let (buf, m, a, h) = indexed_mgr(64, 2 * 1024 * 1024);
        let p = unsafe { deluge_resource_request(m, a, 7, 4096) };
        assert!(!p.is_null());

        let blocks: Vec<*mut u8> = (0..8)
            .map(|_| {
                let raw = unsafe { deluge_alloc::deluge_alloc(h, 1024, 16) };
                assert!(!raw.is_null());
                unsafe { deluge_resource_adopt(m, raw, 1024, COST_IO, core::ptr::null_mut(), None) }
            })
            .collect();
        for b in &blocks {
            assert!(!b.is_null(), "adopt should succeed");
        }

        // The indexed lookup is unaffected by the adopted chunks sharing a key.
        assert!(!unsafe { deluge_resource_peek(m, a, 7) }.is_null());
        assert!(unsafe { deluge_resource_peek(m, a, 8) }.is_null());

        unsafe { deluge_resource_release(m, p) };
        let _ = buf;
    }
}
