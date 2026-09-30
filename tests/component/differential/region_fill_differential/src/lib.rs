//! Differential gate for `deluge_sample_fill::fill_logic::finish_convert_stitch`.
//!
//! Checks the Rust fill's convert+stitch orchestration (which bytes are converted, which neighbour
//! edges are built from where, in what order) is byte-identical to an independent C++ reference
//! ([`cpp_ref`], compiled by `build.rs`). The primitives themselves (`convert_cluster_data`,
//! `stitch_boundaries`) are covered by `deluge_sample_convert`'s own tests.
//!
//! A standalone crate, not a workspace member, so its C++ build stays off the firmware Cargo graph.
//! The gate is `tests/differential.rs`; `tests/host_end_to_end.rs` and
//! `tests/native_finish_glue.rs` run the fill over a real resource manager.
#![deny(unsafe_op_in_unsafe_fn)]

pub mod cpp_ref;

/// Returns `n` bytes of the seed pattern for `tag`: byte `k` is `(tag * 100 + k) & 0xFF`.
///
/// Tests seed the self/prev/next buffers with different tags so every byte's origin is
/// identifiable.
#[must_use]
pub fn ramp(tag: u32, n: usize) -> Vec<u8> {
    (0..n)
        .map(|k| ((tag as usize * 100 + k) & 0xFF) as u8)
        .collect()
}
