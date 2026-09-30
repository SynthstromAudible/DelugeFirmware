#![no_main]

//! Fuzz target: drives the slab cache pool (acquire/pin/unpin/touch/release plus competing general
//! allocations) and checks after each op that pinned slots are never evicted or corrupted and no
//! two live slots alias. A violation panics, which libFuzzer records as a crash.

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    if let Err(e) = deluge_alloc::testing::run_slab_bytes(data) {
        panic!("slab invariant violated: {e}");
    }
});
