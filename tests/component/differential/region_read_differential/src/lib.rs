//! `region-read-differential` — the reader's byte-identical gate: proves
//! `deluge_sample_reader`'s `open`/`window`/`advance`/`deluge_sample_read` deliver the exact same
//! bytes, for the exact same frames, as a direct read of a chunk's own resident payload buffer at
//! an independently computed frame -> (cluster, byte-offset) location. See
//! `docs/superpowers/specs/2026-07-27-u1-sample-range-reader-design.md`'s "Testing / gates" section.
//!
//! Mirrors `region_fill_differential`'s structure: a standalone Cargo crate (NOT a
//! workspace member — keeps this test-only harness off the firmware/BSP Cargo graph); `tests/`
//! holds the differential gate. Pure Rust: the oracle (`tests/differential.rs`'s `oracle_frame`)
//! reads a chunk's payload directly through the Rust `deluge_sample_fill::chunk` accessor, so this
//! crate needs no `build.rs`/C++ at all.
#![deny(unsafe_op_in_unsafe_fn)]

pub mod mapping;

/// The buffer-role/index-dependent, byte-distinguishable seed pattern used across the differential:
/// tag `t`'s byte `k` is `(t*100 + k) & 0xFF`. Mirrors `region_fill_differential::ramp`'s (and
/// the retired region_differential harness's `ops::make_ramp`'s) exact formula, reused here unchanged so a cluster's
/// expected post-seed bytes are trivial to compute independently of this crate's own harness code.
#[must_use]
pub fn ramp(tag: u32, n: usize) -> Vec<u8> {
    (0..n)
        .map(|k| ((tag as usize * 100 + k) & 0xFF) as u8)
        .collect()
}
