//! Payload-offset gate for `deluge_sample_source::cursor::SampleSource<ManagerResidency>`: over a
//! real streamed chunk, the cursor must resolve a region's payload, not the chunk header. The gate
//! is `tests/real_chunk_gate.rs`.
#![deny(unsafe_op_in_unsafe_fn)]

/// Returns `n` bytes of the seed pattern for cluster `index`: byte `k` is
/// `(index * 100 + k) & 0xFF`.
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
