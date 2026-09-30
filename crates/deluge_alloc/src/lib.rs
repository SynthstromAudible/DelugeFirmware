//! TLSF + slab allocator core for the Deluge, behind the C ABI in `libdeluge/alloc.h`.
//!
//! - TLSF backbone (`tlsf`): O(1) general allocator, 16-byte aligned by construction. The C++
//!   conformance suite in `tests/unit/alloc` and the host unit/proptest tests validate it.
//! - [`slab`]: the cache pool for stealable memory — uniform slots, LRU, pinning — registered as
//!   the heap's reclaim hook.
//! - [`DelugeGlobalAlloc`]: a [`GlobalAlloc`](core::alloc::GlobalAlloc) over one heap, so Rust
//!   `alloc` and the C++ app can share it.
//!
//! Every heap step runs inside a [`section`], so heaps may be shared across execution contexts.

// `no_std` only on bare metal; host builds use std. `tlsf` uses only `core` either way.
#![cfg_attr(target_os = "none", no_std)]
#![allow(clippy::missing_safety_doc)]

pub mod abi;
pub mod section;
pub mod slab;
mod tlsf;

use core::ptr;
use section::{Section, STEP_HEAP};
pub use tlsf::Tlsf;
use tlsf::ALIGN;

#[cfg(any(test, feature = "fuzzing"))]
pub mod testing;

/// Opaque heap handle: the [`Tlsf`] control block at the front of the arena.
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

/// Builds a heap over `[base, base + size)`.
///
/// The TLSF control block sits at the front of the region and the rest becomes the pool, so no
/// external metadata is needed. Returns null if `base` is null or the region is too small.
///
/// # Safety
///
/// `[base, base + size)` must be writable memory owned by the caller for the heap's lifetime.
pub unsafe fn deluge_heap_create(base: *mut u8, size: usize) -> *mut DelugeHeap {
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

/// Invalidates the handle; the arena memory stays the caller's.
#[no_mangle]
pub unsafe extern "C" fn deluge_heap_destroy(_h: *mut DelugeHeap) {}

/// Allocates `size` bytes aligned to `align`, driving the reclaim hook and retrying when the heap
/// is full. Returns null on failure or a null heap.
///
/// # Safety
///
/// `h` must be null or a live handle from [`deluge_heap_create`].
pub unsafe fn deluge_alloc(h: *mut DelugeHeap, size: usize, align: usize) -> *mut u8 {
    if h.is_null() {
        return ptr::null_mut();
    }
    let p = {
        let _s = Section::enter(STEP_HEAP);
        heap(h).malloc(size, align)
    };
    if !p.is_null() {
        return p;
    }
    // Out of space: run the reclaim hook (slab / resource-manager eviction) and retry. The hook
    // runs outside any section — it re-enters through `deluge_free` and may be slow — and the loop
    // lives here rather than in `Tlsf::malloc` so no `&mut Tlsf` is held across it. The
    // `reclaiming` guard is claimed and released in single steps, so a nested or concurrent reclaim
    // fails fast instead of recursing (the device has no guard page) or spinning.
    loop {
        let hook = {
            let _s = Section::enter(STEP_HEAP);
            if heap(h).is_reclaiming() {
                None
            } else {
                let hook = heap(h).reclaim_hook();
                if hook.is_some() {
                    heap(h).set_reclaiming(true);
                }
                hook
            }
        };
        let Some((cb, ctx)) = hook else {
            return ptr::null_mut();
        };
        let freed = cb(ctx, size);
        {
            let _s = Section::enter(STEP_HEAP);
            heap(h).set_reclaiming(false);
        }
        if !freed {
            return ptr::null_mut();
        }
        let p = {
            let _s = Section::enter(STEP_HEAP);
            heap(h).malloc(size, align)
        };
        if !p.is_null() {
            return p;
        }
    }
}

/// Registers the heap's reclaim hook, replacing any previous one (one hook per heap).
///
/// # Safety
///
/// `h` must be null or a live heap handle; `cb` must be safe to call with `ctx` while the heap
/// lives.
pub unsafe fn deluge_heap_register_reclaim(
    h: *mut DelugeHeap,
    cb: tlsf::ReclaimFn,
    ctx: *mut core::ffi::c_void,
) {
    if h.is_null() {
        return;
    }
    // One step: registration happens at runtime, and a failing render allocation must never see the
    // callback stored without its context.
    let _s = Section::enter(STEP_HEAP);
    heap(h).set_reclaim(cb, ctx);
}

/// Frees `p`. Null `h` or `p` is a no-op.
///
/// # Safety
///
/// `p` must be null or a live block from this heap.
pub unsafe fn deluge_free(h: *mut DelugeHeap, p: *mut u8) {
    if h.is_null() || p.is_null() {
        return;
    }
    let _s = Section::enter(STEP_HEAP);
    heap(h).free(p);
}

/// Resizes `p` to `new_size`, with `realloc` semantics for null `p` and zero size.
///
/// Shrinking stays in place in one step; growth allocates a new block, copies outside any section,
/// and frees the old one. On failure returns null and leaves `p` intact.
///
/// # Safety
///
/// `h` must be null or a live heap handle; `p` must be null or a live block from it.
pub unsafe fn deluge_realloc(
    h: *mut DelugeHeap,
    p: *mut u8,
    new_size: usize,
    align: usize,
) -> *mut u8 {
    if h.is_null() {
        return ptr::null_mut();
    }
    if p.is_null() {
        return deluge_alloc(h, new_size, align);
    }
    if new_size == 0 {
        deluge_free(h, p);
        return ptr::null_mut();
    }
    let (shrunk, cur) = {
        let _s = Section::enter(STEP_HEAP);
        heap(h).try_shrink_in_place(p, new_size, align)
    };
    if shrunk {
        return p;
    }
    let np = deluge_alloc(h, new_size, align);
    if np.is_null() {
        return ptr::null_mut(); // original left intact
    }
    ptr::copy_nonoverlapping(p, np, cur.min(new_size));
    deluge_free(h, p);
    np
}

/// Returns the bytes usable at `p`: the block's payload size, at least the size requested.
///
/// # Safety
///
/// `h` must be null or a live heap handle; `p` must be a live block from it.
pub unsafe fn deluge_usable_size(h: *mut DelugeHeap, p: *mut u8) -> usize {
    if h.is_null() {
        return 0;
    }
    let _s = Section::enter(STEP_HEAP);
    heap(h).usable_size(p)
}

/// Walks the heap's physical block chain and returns whether its boundary tags are intact.
///
/// Read-only and never allocates; reads only block headers, so it is ASan-safe. A null handle
/// passes. Takes no section: its caller, the `MEM_GUARD` periodic check, runs on the audio render,
/// which nothing that touches a heap preempts.
#[no_mangle]
pub unsafe extern "C" fn deluge_heap_check(h: *mut DelugeHeap) -> bool {
    if h.is_null() {
        return true;
    }
    heap(h).check()
}

/// A [`GlobalAlloc`](core::alloc::GlobalAlloc) backed by one [`DelugeHeap`], so the same heap can
/// serve the C++ app (through the C ABI) and Rust `alloc`.
///
/// Install the heap with [`init`](Self::init) before the first allocation; until then every
/// allocation fails. Concurrency is handled by the heap's own sections, as for the C ABI.
pub struct DelugeGlobalAlloc {
    heap: core::sync::atomic::AtomicPtr<DelugeHeap>,
}

impl DelugeGlobalAlloc {
    /// Returns an allocator with no heap installed.
    pub const fn new() -> Self {
        Self {
            heap: core::sync::atomic::AtomicPtr::new(ptr::null_mut()),
        }
    }
    /// Installs the heap this allocator routes to. Call once at boot, before any allocation.
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

    // Deterministic xorshift, to avoid an rng dependency.
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

    // A 16-aligned arena: miri tracks declared alignment, so a `Vec<u8>` arena would flag the
    // TLSF's 16-aligned headers as misaligned. Keep the returned buffer alive.
    fn aligned_arena(bytes: usize) -> (Vec<u128>, *mut DelugeHeap) {
        let words = bytes / 16 + 1;
        let mut buf = vec![0u128; words];
        let h = unsafe { deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        (buf, h)
    }

    /// A fresh heap passes its own check even when the region's end is not 16-aligned.
    ///
    /// The device's frunk heap takes its bounds from linker symbols with no alignment guarantee
    /// (base and end both ≡ 4 mod 16). The pool size inherits the unaligned end, so `add_pool`
    /// must round the first block to `ALIGN`, or the trailing sentinel lands misaligned and the
    /// periodic walk reports corruption before anything is allocated.
    #[test]
    fn a_heap_whose_region_end_is_unaligned_still_passes_its_own_check() {
        // Keep the arena 16-aligned (for miri), then offset the base by 4 so base and end both sit
        // at 4 mod 16, like the device's frunk region.
        let mut buf = vec![0u128; 4096];
        let base = unsafe { (buf.as_mut_ptr() as *mut u8).add(4) };
        let size = 32768usize; // multiple of 16, so an unaligned base gives an unaligned end
        assert_eq!(
            base as usize % 16,
            4,
            "test must reproduce the unaligned-base case"
        );
        assert_eq!(
            (base as usize + size) % 16,
            4,
            "and the unaligned END, which is the bug"
        );

        let h = unsafe { deluge_heap_create(base, size) };
        assert!(!h.is_null(), "creation itself must succeed");
        assert!(
            unsafe { deluge_heap_check(h) },
            "a freshly created heap must pass its own integrity walk"
        );

        // And it must actually work, not merely validate.
        let p = unsafe { deluge_alloc(h, 100, 16) };
        assert!(
            !p.is_null(),
            "allocation from the unaligned-end heap failed"
        );
        unsafe { deluge_free(h, p) };
        assert!(
            unsafe { deluge_heap_check(h) },
            "check must still pass after alloc/free"
        );
        drop(buf);
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
    fn heap_check_walks_clean_and_catches_corruption() {
        unsafe {
            let (buf, h) = aligned_arena(1 << 20);
            assert!(deluge_heap_check(h), "fresh heap should be intact");
            let a = deluge_alloc(h, 100, 16);
            let b = deluge_alloc(h, 4000, 16);
            let c = deluge_alloc(h, 64, 16);
            assert!(deluge_heap_check(h), "intact after allocs");
            deluge_free(h, b); // a free block now sits between a and c
            assert!(deluge_heap_check(h), "intact after a free");

            // Smash block `a`'s size_and_flags word (a 16-byte header sits right before
            // the payload; prev_phys is its first pointer, size_and_flags the next word).
            // Setting the top bit makes the block claim it runs past the pool end.
            let ptr_sz = core::mem::size_of::<usize>();
            let saf = a.sub(16 - ptr_sz) as *mut usize;
            let orig = saf.read();
            saf.write(orig | (1usize << (usize::BITS - 1)));
            assert!(!deluge_heap_check(h), "corruption must be detected");
            saf.write(orig); // restore so teardown is sane
            assert!(deluge_heap_check(h), "restored heap is intact again");
            let _ = (c, &buf);
        }
    }

    #[test]
    fn realloc_shrinks_in_place() {
        let (buf, h) = aligned_arena(1 << 20);
        unsafe {
            let p = deluge_alloc(h, 4000, 16);
            for i in 0..4000 {
                *p.add(i) = (i & 0xFF) as u8;
            }
            // Shrink is in place and preserves data (callers trim buffers this way).
            let s = deluge_realloc(h, p, 1000, 16);
            assert_eq!(s, p, "shrink should be in-place (pointer stable)");
            for i in 0..1000 {
                assert_eq!(*s.add(i), (i & 0xFF) as u8);
            }
            // Grow may move; only the data is guaranteed.
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

    // Randomized churn across several seeds, mirroring the C++ conformance fragmentation suite.
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

    // The fuzz target's path, fed deterministic pseudo-bytes so it runs under `cargo test`.
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

    use crate::section::test_hooks;

    #[test]
    fn each_heap_step_is_noted_inside_its_section() {
        test_hooks::install();
        let (_buf, h) = aligned_arena(64 * 1024);
        let p = unsafe { deluge_alloc(h, 100, 16) };
        assert!(!p.is_null());
        let _ = unsafe { deluge_usable_size(h, p) };
        unsafe { deluge_free(h, p) };
        assert_eq!(
            test_hooks::take_events(),
            ["enter", "heap", "exit", "enter", "heap", "exit", "enter", "heap", "exit"]
        );
    }

    #[test]
    fn global_alloc_steps_are_noted() {
        use core::alloc::{GlobalAlloc, Layout};
        test_hooks::install();
        let (_buf, h) = aligned_arena(64 * 1024);
        let ga = DelugeGlobalAlloc::new();
        ga.init(h);
        let layout = Layout::from_size_align(48, 8).unwrap();
        let p = unsafe { ga.alloc(layout) };
        unsafe { ga.dealloc(p, layout) };
        let events = test_hooks::take_events();
        assert_eq!(events.iter().filter(|e| **e == "heap").count(), 2);
    }

    thread_local! {
        static DEPTH_IN_HOOK: core::cell::Cell<u32> = const { core::cell::Cell::new(u32::MAX) };
    }
    extern "C" fn reclaim_recording_depth(_ctx: *mut core::ffi::c_void, _bytes: usize) -> bool {
        DEPTH_IN_HOOK.with(|d| d.set(test_hooks::DEPTH.with(|x| x.get())));
        false
    }

    #[test]
    fn reclaim_hook_runs_outside_the_section() {
        test_hooks::install();
        let (_buf, h) = aligned_arena(16 * 1024);
        unsafe { deluge_heap_register_reclaim(h, reclaim_recording_depth, core::ptr::null_mut()) };
        let p = unsafe { deluge_alloc(h, 1 << 20, 16) }; // cannot fit: runs the hook
        assert!(p.is_null());
        assert_eq!(DEPTH_IN_HOOK.with(|d| d.get()), 0);
    }

    #[test]
    fn realloc_growth_copies_outside_the_section() {
        test_hooks::install();
        let (_buf, h) = aligned_arena(64 * 1024);
        let p = unsafe { deluge_alloc(h, 64, 16) };
        for i in 0..64u8 {
            unsafe { *p.add(i as usize) = i };
        }
        test_hooks::take_events();
        let q = unsafe { deluge_realloc(h, p, 4096, 16) };
        assert!(!q.is_null());
        for i in 0..64u8 {
            assert_eq!(unsafe { *q.add(i as usize) }, i);
        }
        // The shrink attempt, the new block, the old block's free: three separate sections.
        assert_eq!(
            test_hooks::take_events(),
            ["enter", "heap", "exit", "enter", "heap", "exit", "enter", "heap", "exit"]
        );
    }

    #[test]
    fn two_contexts_keep_one_heap_consistent() {
        use crate::section::test_hooks::{self, Mode, MODE};
        test_hooks::install();
        let (buf, h) = aligned_arena(1 << 20);
        let hp = h as usize;

        // One context's alloc/free churn: patterned blocks, checked before each free. The
        // render-shaped context holds the host section for a whole block, as the audio render does;
        // the other masks per step.
        fn churn(hp: usize, seed: u64, rounds: usize, block: bool) {
            let h = hp as *mut DelugeHeap;
            let mut rng = Rng(seed);
            let mut live: Vec<(usize, usize, u8)> = Vec::new();
            for _ in 0..rounds {
                if block {
                    test_hooks::lock();
                }
                for _ in 0..32 {
                    if live.len() < 64 && (rng.next() & 1 == 0 || live.is_empty()) {
                        let size = 16 + (rng.next() % 2048) as usize;
                        let p = unsafe { deluge_alloc(h, size, 16) };
                        if !p.is_null() {
                            let tag = (rng.next() & 0xff) as u8;
                            unsafe { core::ptr::write_bytes(p, tag, size) };
                            live.push((p as usize, size, tag));
                        }
                    } else {
                        let i = (rng.next() as usize) % live.len();
                        let (p, size, tag) = live.swap_remove(i);
                        let p = p as *mut u8;
                        for k in 0..size {
                            assert_eq!(unsafe { *p.add(k) }, tag, "a live block was overwritten");
                        }
                        unsafe { deluge_free(h, p) };
                    }
                }
                if block {
                    test_hooks::unlock();
                }
            }
            if block {
                test_hooks::lock();
            }
            for (p, _, _) in live {
                unsafe { deluge_free(h, p as *mut u8) };
            }
            if block {
                test_hooks::unlock();
            }
        }

        let render = std::thread::spawn(move || {
            MODE.with(|m| m.set(Mode::StressRender));
            churn(hp, 0x9e37_79b9, 4000, true);
        });
        let interaction = std::thread::spawn(move || {
            MODE.with(|m| m.set(Mode::StressInteraction));
            churn(hp, 0x85eb_ca6b, 4000, false);
        });
        render.join().unwrap();
        interaction.join().unwrap();
        assert!(unsafe { deluge_heap_check(hp as *mut DelugeHeap) });
        drop(buf);
    }

    extern "C" fn reclaim_nothing(_ctx: *mut core::ffi::c_void, _bytes: usize) -> bool {
        false
    }

    #[test]
    fn reclaim_registration_is_one_heap_step() {
        // Registration happens at runtime: a failing render allocation must never see the callback
        // stored without its context.
        test_hooks::install();
        let (_buf, h) = aligned_arena(16 * 1024);
        test_hooks::take_events();
        unsafe { deluge_heap_register_reclaim(h, reclaim_nothing, core::ptr::null_mut()) };
        assert_eq!(test_hooks::take_events(), ["enter", "heap", "exit"]);
    }
}

#[cfg(test)]
mod proptests {
    use crate::testing::{Harness, Op};
    use proptest::prelude::*;

    // One op: align is 2^(0..=8); size is bounded so the arena does not trivially fill.
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
        // No generated op sequence may violate an invariant.
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
        // Pinned slots are never evicted or corrupted and no two live slots alias, under any op
        // sequence. The case count is kept small for speed.
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
