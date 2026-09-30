//! Fill differential: the same raw cluster bytes, geometry and neighbour state go through the C++
//! reference ([`cpp_ref`]) and `fill_logic::finish_convert_stitch`, and the self payload, both
//! neighbours' boundary bytes and all four boundary flags must match.
//!
//! Byte-identity cases cover every `RawDataFormat`, the SIMD-prefix/scalar-tail boundary, a short
//! last cluster and misaligned mid-stream neighbours. Non-vacuity cases perturb the Rust output and
//! require the comparison to fail.
use deluge_sample_fill::fill_logic;
use fill_logic::{ConvertState as RustState, FillGeometry, NeighbourView, finish_convert_stitch};
use region_fill_differential::cpp_ref::{
    self, ConvertState as CppState, Geometry as CppGeometry, Neighbour as CppNeighbour,
};
use region_fill_differential::ramp;

/// Everything one side can change: the self/prev/next payloads, the convert state and the
/// neighbour flags.
#[derive(Debug, PartialEq, Eq)]
struct CaseResult {
    self_payload: Vec<u8>,
    prev_payload: Vec<u8>,
    next_payload: Vec<u8>,
    self_first_three_bytes: [u8; 3],
    self_start_converted: bool,
    self_end_converted: bool,
    prev_end_converted: bool,
    next_start_converted: bool,
}

fn to_cpp_geo(g: &FillGeometry) -> CppGeometry {
    CppGeometry {
        audio_data_start_pos_bytes: g.audio_data_start_pos_bytes,
        audio_data_length_bytes: g.audio_data_length_bytes,
        first_cluster_index_with_no_audio_data: g.first_cluster_index_with_no_audio_data,
        cluster_size: g.cluster_size as usize,
        cluster_size_magnitude: g.cluster_size_magnitude as usize,
        raw_data_format: g.raw_data_format,
    }
}

/// Runs `fill_logic::finish_convert_stitch` over freshly ramp-seeded inputs.
fn run_rust(geo: &FillGeometry, index: u32, has_prev: bool, has_next: bool) -> CaseResult {
    let payload_len = geo.cluster_size as usize + 7;
    let mut self_buf = ramp(index, payload_len);
    let mut prev_buf = ramp(1000 + index, payload_len);
    let mut next_buf = ramp(2000 + index, payload_len);
    let next_unconv: [u8; 3] = [77, 78, 79];
    let mut state = RustState::default();
    let (mut prev_start_unused, mut prev_end) = (false, false);
    let (mut next_start, mut next_end_unused) = (false, false);

    finish_convert_stitch(
        &mut self_buf,
        index,
        geo,
        &mut state,
        has_prev.then(|| NeighbourView {
            payload: &mut prev_buf,
            unconverted_head: &[0; 3], // unread on the prev side
            start_converted: &mut prev_start_unused,
            end_converted: &mut prev_end,
        }),
        has_next.then(|| NeighbourView {
            payload: &mut next_buf,
            unconverted_head: &next_unconv,
            start_converted: &mut next_start,
            end_converted: &mut next_end_unused,
        }),
    );

    CaseResult {
        self_payload: self_buf,
        prev_payload: prev_buf,
        next_payload: next_buf,
        self_first_three_bytes: state.first_three_bytes,
        self_start_converted: state.start_converted,
        self_end_converted: state.end_converted,
        prev_end_converted: prev_end,
        next_start_converted: next_start,
    }
}

/// Runs [`cpp_ref::finish_fill_over_buffers`] over the same seeds as [`run_rust`].
fn run_cpp(geo: &FillGeometry, index: u32, has_prev: bool, has_next: bool) -> CaseResult {
    let payload_len = geo.cluster_size as usize + 7;
    let mut self_buf = ramp(index, payload_len);
    let mut prev_buf = ramp(1000 + index, payload_len);
    let mut next_buf = ramp(2000 + index, payload_len);
    let next_unconv: [u8; 3] = [77, 78, 79];
    let mut state = CppState::default();
    let (mut prev_start_unused, mut prev_end) = (false, false);
    let (mut next_start, mut next_end_unused) = (false, false);

    cpp_ref::finish_fill_over_buffers(
        &mut self_buf,
        index,
        &to_cpp_geo(geo),
        &mut state,
        has_prev.then(|| CppNeighbour {
            payload: &mut prev_buf,
            unconverted_head: &[0; 3],
            start_converted: &mut prev_start_unused,
            end_converted: &mut prev_end,
        }),
        has_next.then(|| CppNeighbour {
            payload: &mut next_buf,
            unconverted_head: &next_unconv,
            start_converted: &mut next_start,
            end_converted: &mut next_end_unused,
        }),
    );

    CaseResult {
        self_payload: self_buf,
        prev_payload: prev_buf,
        next_payload: next_buf,
        self_first_three_bytes: state.first_three_bytes,
        self_start_converted: state.start_converted,
        self_end_converted: state.end_converted,
        prev_end_converted: prev_end,
        next_start_converted: next_start,
    }
}

/// Compares two outputs field by field, returning the first mismatch; the non-vacuity tests assert
/// on the result.
fn diff(rust: &CaseResult, cpp: &CaseResult) -> Result<(), String> {
    if rust.self_payload != cpp.self_payload {
        return Err("self_payload diverged".into());
    }
    if rust.prev_payload != cpp.prev_payload {
        return Err("prev_payload diverged".into());
    }
    if rust.next_payload != cpp.next_payload {
        return Err("next_payload diverged".into());
    }
    if rust.self_first_three_bytes != cpp.self_first_three_bytes {
        return Err("self_first_three_bytes diverged".into());
    }
    if rust.self_start_converted != cpp.self_start_converted {
        return Err("self_start_converted diverged".into());
    }
    if rust.self_end_converted != cpp.self_end_converted {
        return Err("self_end_converted diverged".into());
    }
    if rust.prev_end_converted != cpp.prev_end_converted {
        return Err("prev_end_converted diverged".into());
    }
    if rust.next_start_converted != cpp.next_start_converted {
        return Err("next_start_converted diverged".into());
    }
    Ok(())
}

/// Runs both sides and asserts identical output.
fn assert_byte_identical(geo: &FillGeometry, index: u32, has_prev: bool, has_next: bool) {
    let rust = run_rust(geo, index, has_prev, has_next);
    let cpp = run_cpp(geo, index, has_prev, has_next);
    diff(&rust, &cpp).unwrap_or_else(|e| {
        panic!("fill-differential mismatch (index={index}, has_prev={has_prev}, has_next={has_next}): {e}")
    });
}

// Coverage cases

/// A misaligned mid-stream geometry with a 128-byte cluster, so every format's conversion range
/// spans several SIMD blocks plus a scalar tail.
fn simd_geo(format: u8) -> FillGeometry {
    FillGeometry {
        audio_data_start_pos_bytes: 3, // misaligned: 3 & 0b11 = 3 (nonzero)
        audio_data_length_bytes: 100_000,
        first_cluster_index_with_no_audio_data: 50, // far from the mid-stream index used below
        cluster_size: 128,
        cluster_size_magnitude: 7, // 2^7 = 128
        raw_data_format: format,
    }
}

/// Every `RawDataFormat` (0..=5), misaligned mid-stream with both neighbours.
#[test]
fn each_raw_data_format_byte_identical_both_neighbours() {
    for format in 0u8..=5 {
        let geo = simd_geo(format);
        assert_byte_identical(&geo, 5, true, true);
    }
}

/// The SIMD-prefix/scalar-tail boundary for `ENDIANNESS_WRONG_32`: the conversion range `[3, 125)`
/// is 122 bytes, seven 16-byte SIMD lanes plus a 10-byte scalar tail.
#[test]
fn simd_prefix_scalar_tail_boundary_is_explicit() {
    let geo = simd_geo(5); // ENDIANNESS_WRONG_32
    assert_byte_identical(&geo, 5, true, true);
}

/// A short last cluster: audio ends inside it, so conversion stops at the audio end rather than
/// `cluster_size - 3`. Previous neighbour only.
#[test]
fn short_last_cluster_byte_identical() {
    let geo = FillGeometry {
        audio_data_start_pos_bytes: 3,
        audio_data_length_bytes: 3 + 4 * 128 + 20, // ends 20 bytes into cluster index 4
        first_cluster_index_with_no_audio_data: 5, // cluster 4 is the last one with audio data
        cluster_size: 128,
        cluster_size_magnitude: 7,
        raw_data_format: 2, // UNSIGNED_8
    };
    assert_byte_identical(&geo, 4, true, false);
}

#[test]
fn misaligned_both_neighbours_mid_stream() {
    let geo = simd_geo(2); // UNSIGNED_8
    assert_byte_identical(&geo, 9, true, true);
}

/// No neighbours: convert only, no stitch.
#[test]
fn no_neighbours_byte_identical() {
    let geo = simd_geo(4); // ENDIANNESS_WRONG_24 (the special-cased 3-byte-group format)
    assert_byte_identical(&geo, 5, false, false);
}

/// `NATIVE`: nothing is converted, but the previous cluster's overhang is still refreshed from this
/// cluster's head.
#[test]
fn native_format_still_stitches_overhang() {
    let geo = simd_geo(0); // NATIVE
    assert_byte_identical(&geo, 5, true, true);
}

// Non-vacuity: the comparison must detect a real divergence.

/// A flipped bit in the Rust side's self payload is caught by [`diff`].
#[test]
fn perturbed_rust_output_is_detected() {
    let geo = simd_geo(3); // ENDIANNESS_WRONG_16
    let index = 5;
    let mut rust = run_rust(&geo, index, true, true);
    let cpp = run_cpp(&geo, index, true, true);

    // The unperturbed outputs must agree, or a real bug would satisfy this test.
    diff(&rust, &cpp).expect("precondition: real outputs must agree before perturbing");

    rust.self_payload[0] ^= 0x01;

    let result = diff(&rust, &cpp);
    assert!(
        result.is_err(),
        "non-vacuity FAILED: a corrupted Rust output was not detected by the comparison"
    );
    assert_eq!(result.unwrap_err(), "self_payload diverged");
}

/// A corrupted flag, rather than a payload byte, is also caught.
#[test]
fn perturbed_rust_flag_is_detected() {
    let geo = simd_geo(2); // UNSIGNED_8
    let index = 5;
    let mut rust = run_rust(&geo, index, true, true);
    let cpp = run_cpp(&geo, index, true, true);
    diff(&rust, &cpp).expect("precondition: real outputs must agree before perturbing");

    rust.next_start_converted = !rust.next_start_converted;

    let result = diff(&rust, &cpp);
    assert!(
        result.is_err(),
        "non-vacuity FAILED: a corrupted Rust flag was not detected by the comparison"
    );
    assert_eq!(result.unwrap_err(), "next_start_converted diverged");
}
