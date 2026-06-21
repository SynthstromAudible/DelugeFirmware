//! deluge_alloc — TLSF + slab allocator core for the Deluge, behind a stable C
//! ABI (libdeluge/alloc.h). Phase 3 of docs/dev/allocator_implementation_plan.md.
//!
//! - TLSF backbone (`tlsf`): O(1) general allocator, 16-byte-aligned by
//!   construction, validated by the C++ conformance suite (tests/spec_alloc, the
//!   same AllocatorUnderTest suite the legacy MemoryRegion passes) plus host
//!   unit/proptest/fuzz tests.
//! - Slab cache pool (`slab`): the "stealables" pager — uniform slots, LRU,
//!   pinning, registered as the heap's reclaim hook.
//! - `DelugeGlobalAlloc`: a `GlobalAlloc` so one heap can back the C++ firmware,
//!   Rust `alloc`, and embassy.
//!
//! Builds as a host staticlib (i686/x86_64, std — tests/conformance) and a
//! bare-metal device staticlib (armv7a-none-eabihf, no_std; `cargo build-device`).

// `no_std` only for the bare-metal device build (target_os = "none"). On the host
// the crate is a normal `std` crate, so unit tests, fuzz targets, and the C++
// conformance staticlib all build with the standard (unwinding) runtime. The
// allocator logic in `tlsf` uses only `core`, so it is bit-identical either way.
#![cfg_attr(target_os = "none", no_std)]
#![allow(clippy::missing_safety_doc)]

pub mod slab;
mod tlsf;

use core::ptr;
pub use tlsf::Tlsf;
use tlsf::ALIGN;

#[cfg(any(test, feature = "fuzzing"))]
pub mod testing;

// No `#[panic_handler]` here: this is a library rlib, not the final artifact. The
// `deluge_rust` umbrella staticlib that links into the firmware provides the one
// panic handler for the whole dependency graph. (On the host, std provides it.)

/// Opaque heap handle (a `Tlsf` control block living at the front of the arena).
#[repr(C)]
pub struct DelugeHeap {
    _opaque: [u8; 0],
}

#[inline]
fn align_up(x: usize, a: usize) -> usize {
    (x + a - 1) & !(a - 1)
}

#[inline]
unsafe fn heap<'a>(h: *mut DelugeHeap) -> &'a mut Tlsf {
    &mut *(h as *mut Tlsf)
}

/// Build a heap over `[base, base+size)`. The TLSF control block is placed at the
/// front of the region and the remainder becomes the managed pool, so no
/// allocator-in-allocator and no external metadata storage.
#[no_mangle]
pub unsafe extern "C" fn deluge_heap_create(base: *mut u8, size: usize) -> *mut DelugeHeap {
    if base.is_null() {
        return ptr::null_mut();
    }
    let start = base as usize;
    let ctrl_align = core::mem::align_of::<Tlsf>().max(ALIGN);
    let aligned = align_up(start, ctrl_align);
    let head = aligned - start;
    let control = Tlsf::control_size();
    if size < head + control + ALIGN * 4 {
        return ptr::null_mut();
    }
    let tlsf = aligned as *mut Tlsf;
    Tlsf::init(tlsf);
    let pool_start = align_up(aligned + control, ALIGN);
    let pool_size = (start + size) - pool_start;
    (*tlsf).add_pool(pool_start as *mut u8, pool_size);
    tlsf as *mut DelugeHeap
}

/// The arena memory is owned by the caller; this just invalidates the handle.
#[no_mangle]
pub unsafe extern "C" fn deluge_heap_destroy(_h: *mut DelugeHeap) {}

#[no_mangle]
pub unsafe extern "C" fn deluge_alloc(h: *mut DelugeHeap, size: usize, align: usize) -> *mut u8 {
    if h.is_null() {
        return ptr::null_mut();
    }
    let p = heap(h).malloc(size, align);
    if !p.is_null() {
        return p;
    }
    // Out of space: drive the reclaim hook (the slab cache pool) and retry. The
    // retry loop lives here, NOT inside Tlsf::malloc, so no &mut Tlsf borrow is
    // held across the callback — the hook re-enters via deluge_free (a separate
    // borrow). A depth-1 `reclaiming` guard makes a nested reclaim fail fast
    // instead of recursing (no MMU guard page on the device).
    loop {
        if heap(h).is_reclaiming() {
            return ptr::null_mut();
        }
        let Some((cb, ctx)) = heap(h).reclaim_hook() else {
            return ptr::null_mut();
        };
        heap(h).set_reclaiming(true);
        let freed = cb(ctx, size);
        heap(h).set_reclaiming(false);
        if !freed {
            return ptr::null_mut();
        }
        let p = heap(h).malloc(size, align);
        if !p.is_null() {
            return p;
        }
    }
}

/// Register the reclaim hook (called by the slab pool). One hook per heap.
#[no_mangle]
pub unsafe extern "C" fn deluge_heap_register_reclaim(
    h: *mut DelugeHeap,
    cb: tlsf::ReclaimFn,
    ctx: *mut core::ffi::c_void,
) {
    if h.is_null() {
        return;
    }
    heap(h).set_reclaim(cb, ctx);
}

#[no_mangle]
pub unsafe extern "C" fn deluge_free(h: *mut DelugeHeap, p: *mut u8) {
    if h.is_null() {
        return;
    }
    heap(h).free(p);
}

#[no_mangle]
pub unsafe extern "C" fn deluge_realloc(
    h: *mut DelugeHeap,
    p: *mut u8,
    new_size: usize,
    align: usize,
) -> *mut u8 {
    if h.is_null() {
        return ptr::null_mut();
    }
    heap(h).realloc(p, new_size, align)
}

/// Bytes guaranteed usable at `p` (the block's payload size, >= the requested size).
#[no_mangle]
pub unsafe extern "C" fn deluge_usable_size(h: *mut DelugeHeap, p: *mut u8) -> usize {
    if h.is_null() {
        return 0;
    }
    heap(h).usable_size(p)
}

/// A `core::alloc::GlobalAlloc` backed by a single `DelugeHeap`, so the *same*
/// heap can serve the C++ firmware (via the C ABI) and Rust `alloc` collections /
/// embassy's `#[global_allocator]` — the unified-heap synergy goal of the
/// redesign. The heap is installed at boot via `init`. Single-threaded use is
/// assumed, exactly as the existing firmware allocator does; no internal locking
/// is added speculatively (a future multi-core/DMA sharer would add it here).
pub struct DelugeGlobalAlloc {
    heap: core::sync::atomic::AtomicPtr<DelugeHeap>,
}

impl DelugeGlobalAlloc {
    pub const fn new() -> Self {
        Self {
            heap: core::sync::atomic::AtomicPtr::new(ptr::null_mut()),
        }
    }
    /// Install the heap this allocator routes to. Call once at boot, before any
    /// allocation through it.
    pub fn init(&self, h: *mut DelugeHeap) {
        self.heap.store(h, core::sync::atomic::Ordering::SeqCst);
    }
    #[inline]
    fn heap_ptr(&self) -> *mut DelugeHeap {
        self.heap.load(core::sync::atomic::Ordering::SeqCst)
    }
}

impl Default for DelugeGlobalAlloc {
    fn default() -> Self {
        Self::new()
    }
}

unsafe impl core::alloc::GlobalAlloc for DelugeGlobalAlloc {
    unsafe fn alloc(&self, layout: core::alloc::Layout) -> *mut u8 {
        deluge_alloc(self.heap_ptr(), layout.size(), layout.align())
    }
    unsafe fn dealloc(&self, p: *mut u8, _layout: core::alloc::Layout) {
        deluge_free(self.heap_ptr(), p);
    }
    unsafe fn realloc(&self, p: *mut u8, layout: core::alloc::Layout, new_size: usize) -> *mut u8 {
        deluge_realloc(self.heap_ptr(), p, new_size, layout.align())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::{Harness, Op};

    // A tiny xorshift so the tests are deterministic without an rng dependency.
    struct Rng(u64);
    impl Rng {
        fn next(&mut self) -> u64 {
            let mut x = self.0;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            self.0 = x;
            x
        }
    }

    // 16-aligned arena (Vec<u128>) — real BSP regions are page-aligned; miri tracks
    // an allocation's declared alignment, so a Vec<u8> arena would (correctly) flag
    // the TLSF's 16-aligned headers as misaligned. Keep the returned buffer alive.
    fn aligned_arena(bytes: usize) -> (Vec<u128>, *mut DelugeHeap) {
        let words = bytes / 16 + 1;
        let mut buf = vec![0u128; words];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        (buf, h)
    }

    #[test]
    fn basic_alignment_and_usable() {
        let (buf, h) = aligned_arena(1 << 20);
        assert!(!h.is_null());
        for s in [1usize, 7, 16, 100, 513, 4096, 30000] {
            let p = unsafe { deluge_alloc(h, s, 16) };
            assert!(!p.is_null(), "alloc {s} failed");
            assert_eq!(p as usize % 16, 0);
            assert!(unsafe { deluge_usable_size(h, p) } >= s);
            unsafe { deluge_free(h, p) };
        }
        let _ = &buf;
    }

    #[test]
    fn over_aligned_requests() {
        let (buf, h) = aligned_arena(1 << 20);
        for &align in &[32usize, 64, 128, 256, 1024, 4096] {
            let p = unsafe { deluge_alloc(h, 1000, align) };
            assert!(!p.is_null(), "alloc align {align} failed");
            assert_eq!(p as usize % align, 0, "ptr {p:p} not aligned to {align}");
            unsafe { deluge_free(h, p) };
        }
        let _ = &buf;
    }

    #[test]
    fn realloc_shrinks_in_place() {
        let (buf, h) = aligned_arena(1 << 20);
        unsafe {
            let p = deluge_alloc(h, 4000, 16);
            for i in 0..4000 {
                *p.add(i) = (i & 0xFF) as u8;
            }
            // Shrink: pointer-stable, no copy, data preserved (the wave_table /
            // patch_cable_set trim pattern).
            let s = deluge_realloc(h, p, 1000, 16);
            assert_eq!(s, p, "shrink should be in-place (pointer stable)");
            for i in 0..1000 {
                assert_eq!(*s.add(i), (i & 0xFF) as u8);
            }
            // Grow has no in-place consumer, so it may move — only data fidelity
            // is guaranteed.
            let g = deluge_realloc(h, s, 8000, 16);
            assert!(!g.is_null());
            for i in 0..1000 {
                assert_eq!(*g.add(i), (i & 0xFF) as u8, "grow lost byte {i}");
            }
            deluge_free(h, g);
        }
        let _ = &buf;
    }

    #[test]
    fn global_alloc_routes_to_heap() {
        use core::alloc::{GlobalAlloc, Layout};
        let (buf, h) = aligned_arena(1 << 20);
        let ga = DelugeGlobalAlloc::new();
        ga.init(h);
        unsafe {
            let l = Layout::from_size_align(1000, 16).unwrap();
            let p = ga.alloc(l);
            assert!(!p.is_null());
            assert_eq!(p as usize % 16, 0);
            core::ptr::write_bytes(p, 0xCD, 1000);
            let p2 = ga.realloc(p, l, 4000);
            assert!(!p2.is_null());
            for i in 0..1000 {
                assert_eq!(*p2.add(i), 0xCD, "realloc lost byte {i}");
            }
            ga.dealloc(p2, Layout::from_size_align(4000, 16).unwrap());
        }
        let _ = &buf;
    }

    // Deep randomized churn across several seeds — the Rust analogue of the C++
    // conformance RandomAllocFragmentation/Alignment16 suite.
    #[test]
    fn randomized_churn_invariants() {
        for seed in [1u64, 7, 42, 0xDEADBEEF, 0x1234_5678_9abc_def0] {
            let mut rng = Rng(seed);
            let mut h = Harness::new(1 << 20, 512);
            for _ in 0..40_000 {
                let slot = (rng.next() % 512) as usize;
                let r = rng.next();
                let size = ((r >> 8) % 20_000) as usize + 1;
                let align = 1usize << ((r >> 32) % 5); // 1..16
                let op = match r % 3 {
                    0 => Op::Alloc { slot, size, align },
                    1 => Op::Free { slot },
                    _ => Op::Realloc { slot, size, align },
                };
                h.step(op).unwrap_or_else(|e| panic!("seed {seed}: {e}"));
            }
            h.drain_and_check()
                .unwrap_or_else(|e| panic!("seed {seed} drain: {e}"));
        }
    }

    // The exact path the fuzzer exercises, but with deterministic pseudo-bytes so
    // it runs under `cargo test` even without cargo-fuzz installed.
    #[test]
    fn fuzz_replay_smoke() {
        for seed in [3u64, 99, 0xABCD] {
            let mut rng = Rng(seed);
            let mut bytes = vec![0u8; 8192];
            for b in bytes.iter_mut() {
                *b = rng.next() as u8;
            }
            crate::testing::run_bytes(&bytes).unwrap_or_else(|e| panic!("fuzz seed {seed}: {e}"));
        }
    }

    // Same for the slab model (pinning / eviction invariants).
    #[test]
    fn slab_fuzz_replay_smoke() {
        for seed in [5u64, 123, 0xFACE] {
            let mut rng = Rng(seed);
            let mut bytes = vec![0u8; 8192];
            for b in bytes.iter_mut() {
                *b = rng.next() as u8;
            }
            crate::testing::run_slab_bytes(&bytes)
                .unwrap_or_else(|e| panic!("slab fuzz seed {seed}: {e}"));
        }
    }
}

#[cfg(test)]
mod proptests {
    use crate::testing::{Harness, Op};
    use proptest::prelude::*;

    // Generate a single op. align is an exponent 0..=8 → 1..256 (the harness
    // rounds to a power of two and clamps); size is bounded to keep the arena
    // from trivially filling.
    fn any_op() -> impl Strategy<Value = Op> {
        let slot = 0usize..256;
        let size = 1usize..70_000;
        let alog = 0u32..9;
        prop_oneof![
            (slot.clone(), size.clone(), alog.clone()).prop_map(|(slot, size, a)| Op::Alloc {
                slot,
                size,
                align: 1 << a
            }),
            slot.clone().prop_map(|slot| Op::Free { slot }),
            (slot, size, alog).prop_map(|(slot, size, a)| Op::Realloc {
                slot,
                size,
                align: 1 << a
            }),
        ]
    }

    proptest! {
        // Generated op sequences must never violate an invariant; proptest shrinks
        // any counterexample to a minimal failing sequence.
        #[test]
        fn churn_never_violates_invariants(ops in prop::collection::vec(any_op(), 0..3000)) {
            let mut h = Harness::new(1 << 20, 256);
            for op in ops {
                h.step(op).map_err(TestCaseError::fail)?;
            }
            h.drain_and_check().map_err(TestCaseError::fail)?;
        }
    }

    use crate::testing::{SlabModel, SlabOp};

    fn any_slab_op() -> impl Strategy<Value = SlabOp> {
        let i = 0usize..64;
        prop_oneof![
            3 => Just(SlabOp::Acquire),
            2 => i.clone().prop_map(SlabOp::Pin),
            2 => i.clone().prop_map(SlabOp::Unpin),
            1 => i.clone().prop_map(SlabOp::Touch),
            1 => i.clone().prop_map(SlabOp::Release),
            2 => (1usize..3000).prop_map(SlabOp::GeneralAlloc),
            1 => i.prop_map(SlabOp::GeneralFree),
        ]
    }

    proptest! {
        // The slab/eviction/pinning invariants must hold under any op sequence:
        // pinned slots are never evicted or corrupted, and no two live slots alias.
        // Small slots / case count keep this in the ~seconds range; the cargo-fuzz
        // `slab` target is for deep coverage-guided runs.
        #![proptest_config(ProptestConfig::with_cases(48))]
        #[test]
        fn slab_never_violates_invariants(ops in prop::collection::vec(any_slab_op(), 0..600)) {
            let mut m = SlabModel::new(64 * 1024, 512, 32);
            for op in ops {
                m.step(op).map_err(TestCaseError::fail)?;
            }
        }
    }
}
