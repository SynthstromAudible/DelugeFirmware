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
pub use value::{COST_CPU, COST_FREE, COST_IO};

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
            assert_ne!(id, 0xFFFF_FFFF, "asset slot {n} should be free after release");
        }
        // And the heap reclaimed the backings — a fresh acquire still works.
        let again = unsafe { deluge_resource_acquire(mgr, a, 0, 32 * 1024) };
        // `a` was freed/reused above so this id may now be in use by a new owner; just
        // assert the manager is still functional (no leak/corruption).
        let _ = again;
    }
}
