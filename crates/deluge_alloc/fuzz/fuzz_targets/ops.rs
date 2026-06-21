#![no_main]

//! Fuzz target: drive the allocator with a fuzzer-chosen alloc/free/realloc
//! sequence and assert every invariant after each op. A violation panics, which
//! libFuzzer records as a crash with the reproducing input.

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    if let Err(e) = deluge_alloc::testing::run_bytes(data) {
        panic!("allocator invariant violated: {e}");
    }
});
