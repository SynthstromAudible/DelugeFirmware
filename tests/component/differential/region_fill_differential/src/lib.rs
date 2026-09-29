//! `region-fill-differential` — proves the native Rust
//! `fill_logic::finish_convert_stitch` is byte-identical to the C++ orchestration it
//! replaced (`finish_fill`, `storage/audio/stream/async_fill.cpp:110-162`) at the ORCHESTRATION level
//! — which bytes get converted, which neighbour edges get built from where, in what order — not just
//! the underlying convert/stitch primitives (`convert_cluster_data`/`stitch_boundaries`), which
//! `sample_convert`'s own suite already proved byte-identical against SIMDe.
//!
//! Inherits its structure from the retired region_differential harness: a standalone Cargo crate (NOT a
//! workspace member — keeps this test-only harness off the firmware/BSP Cargo graph); `build.rs` `cc`-compiles a
//! C++ reference slice ([`cpp_ref`]) into the test binary; `tests/` holds the differential gate
//! (`tests/differential.rs`, including its non-vacuity/perturb case) and the host end-to-end pipeline
//! test (`tests/host_end_to_end.rs`).
//!
//! The Rust side under test, `fill_logic::finish_convert_stitch`, is NOT re-exported from here —
//! `deluge-bsp-rust` is bin-only (no `[lib]` target), so each test file pulls `src/fill_logic.rs` in
//! unmodified via `#[path]`, the same convention `tests/fill_logic_host.rs` already uses from INSIDE
//! that crate — this crate just does it from a sibling directory instead.
#![deny(unsafe_op_in_unsafe_fn)]

pub mod cpp_ref;

/// The buffer-role/index-dependent, byte-distinguishable seed pattern used across the differential and
/// host end-to-end tests: tag `t`'s byte `k` is `(t*100 + k) & 0xFF`. This is the retired
/// region_differential harness's ramp formula, generalized from "cluster index" to a
/// plain `tag` so a test can seed self/prev/next with
/// distinguishable patterns even when two of them share a cluster index across different scenarios.
#[must_use]
pub fn ramp(tag: u32, n: usize) -> Vec<u8> {
    (0..n)
        .map(|k| ((tag as usize * 100 + k) & 0xFF) as u8)
        .collect()
}
