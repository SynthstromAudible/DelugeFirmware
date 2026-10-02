//! Byte-identical gate for `deluge_sample_reader`.
//!
//! `open`/`window`/`advance`/`deluge_sample_read` must return the same bytes, for the same frames,
//! as reading a chunk's resident payload directly at an independently computed location
//! ([`mapping`]). The gate is `tests/differential.rs`. A standalone crate, not a workspace member.
#![deny(unsafe_op_in_unsafe_fn)]

pub mod mapping;

/// Returns `n` bytes of the seed pattern for `tag`: byte `k` is `(tag * 100 + k) & 0xFF`, the same
/// formula as `region_fill_differential::ramp`.
#[must_use]
pub fn ramp(tag: u32, n: usize) -> Vec<u8> {
    (0..n)
        .map(|k| ((tag as usize * 100 + k) & 0xFF) as u8)
        .collect()
}
