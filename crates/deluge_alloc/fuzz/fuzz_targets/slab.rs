#![no_main]

//! Fuzz target: drive the slab cache pool (acquire / pin / unpin / touch /
//! release + competing general allocations) and assert the eviction/pinning
//! invariants after each op — pinned slots are never evicted or corrupted, and
//! no two live slots alias. A violation panics → libFuzzer crash + repro input.

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    if let Err(e) = deluge_alloc::testing::run_slab_bytes(data) {
        panic!("slab invariant violated: {e}");
    }
});
