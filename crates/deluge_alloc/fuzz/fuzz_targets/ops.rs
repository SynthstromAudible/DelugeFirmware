#![no_main]

//! Fuzz target: drives the allocator with a fuzzer-chosen alloc/free/realloc sequence and checks
//! every invariant after each op. A violation panics, which libFuzzer records as a crash.

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    if let Err(e) = deluge_alloc::testing::run_bytes(data) {
        panic!("allocator invariant violated: {e}");
    }
});
