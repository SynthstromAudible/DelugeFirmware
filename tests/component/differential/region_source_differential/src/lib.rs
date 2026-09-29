//! `region-source-differential` — a payload-offset gate: proves
//! `deluge_sample_source::cursor::SampleSource<ManagerResidency>` resolves a
//! region's REAL payload (not the chunk header) when driven over a real
//! streamed chunk backing (`deluge_sample_fill::chunk::StreamedChunk`, pure
//! Rust, no C++ shim). See `tests/real_chunk_gate.rs` for the gate itself.
#![deny(unsafe_op_in_unsafe_fn)]

/// The cluster-index-dependent, byte-distinguishable seed pattern: cluster
/// `index`'s byte `k` is `(index * 100 + k) & 0xFF`. Identical formula to the
/// retired region_differential harness's `ops::make_ramp` / `region_fill_differential::ramp`.
#[must_use]
pub fn make_ramp(index: u32, n: usize) -> Vec<u8> {
    (0..n)
        .map(|k| ((index as usize * 100 + k) & 0xFF) as u8)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn make_ramp_is_deterministic_and_index_dependent() {
        assert_eq!(make_ramp(0, 3), vec![0, 1, 2]);
        assert_eq!(make_ramp(1, 3), vec![100, 101, 102]);
        assert_ne!(make_ramp(0, 3), make_ramp(1, 3));
    }
}
