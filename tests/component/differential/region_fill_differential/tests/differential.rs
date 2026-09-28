//! Fill-differential: drives the SAME raw cluster bytes + geometry +
//! neighbour states through (A) the C++ reference (`region_fill_differential::cpp_ref`, a from-scratch
//! replica of `finish_fill`'s orchestration straight over `convert.h`/`stitch.h`) and (B) the Rust
//! `fill_logic::finish_convert_stitch`, and asserts byte-identical converted+stitched
//! output — the self payload, both neighbours' mutated boundary bytes, and all four boundary flags.
//!
//! `fill_logic` lives in the shared `deluge_sample_fill` crate, so this test depends on the crate
//! directly rather than pulling the file in via `#[path]`. `fill_logic` has no CS/extern dependencies of its own (pure
//! buffer arithmetic plus calls into `deluge_sample_convert`, which has none either), so unlike
//! the retired region_differential harness and this crate's own `tests/host_end_to_end.rs`, this file
//! needs no critical-section stubs.
//!
//! Two validating shapes — a byte-identity differential and a non-vacuity
//! perturbation check — the pattern this crate inherited from the retired
//! region_differential harness:
//!   1. BYTE-IDENTICAL cases across the required coverage (every `RawDataFormat`, the SIMD-prefix/
//!      scalar-tail boundary, a short last cluster, misaligned-both-neighbours mid-stream).
//!   2. NON-VACUITY: the Rust side's OWN output is deliberately perturbed (one output byte flipped)
//!      after both sides have run, and the SAME comparison that passed on the real output must now
//!      fail — proving the comparison actually has teeth, not just that it never fires.
use deluge_sample_fill::fill_logic;
use fill_logic::{ConvertState as RustState, FillGeometry, NeighbourView, finish_convert_stitch};
use region_fill_differential::cpp_ref::{
    self, ConvertState as CppState, Geometry as CppGeometry, Neighbour as CppNeighbour,
};
use region_fill_differential::ramp;

/// One side's full observable output: the mutated self/prev/next payloads plus the resulting
/// convert-state and neighbour flags — everything `finish_fill`/`finish_convert_stitch` can touch.
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

/// Run the Rust side (`fill_logic::finish_convert_stitch`) over freshly ramp-seeded inputs.
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

/// Run the C++ side (`cpp_ref::finish_fill_over_buffers`) over a FRESH clone of the same ramp-seeded
/// inputs `run_rust` used (same tags -> byte-identical seeds).
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

/// Field-by-field comparison, returning the first mismatch's description rather than panicking
/// directly — lets the non-vacuity test assert a specific case FAILS this comparison, not just that
/// some assertion somewhere panics.
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

/// Run both sides over `geo`/`index`/neighbour presence and assert they're byte-identical.
fn assert_byte_identical(geo: &FillGeometry, index: u32, has_prev: bool, has_next: bool) {
    let rust = run_rust(geo, index, has_prev, has_next);
    let cpp = run_cpp(geo, index, has_prev, has_next);
    diff(&rust, &cpp).unwrap_or_else(|e| {
        panic!("fill-differential mismatch (index={index}, has_prev={has_prev}, has_next={has_next}): {e}")
    });
}

// ---------------------------------------------------------------------------------------------------
// Required coverage
// ---------------------------------------------------------------------------------------------------

/// A mid-stream, misaligned (`audio_data_start_pos_bytes & 0b11 != 0`) geometry with a large-enough
/// cluster (128 bytes) that every format's conversion range spans multiple SIMD blocks (16-byte lanes
/// for the byte/word formats, 48-byte groups for 24-bit) PLUS a nonzero scalar tail — this single
/// geometry shape already exercises the SIMD-prefix/scalar-tail boundary for every format below (see
/// `simd_prefix_scalar_tail_boundary_is_explicit` for a dedicated, narrowly-documented case too).
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

/// Every `RawDataFormat` (0..=5, mirroring `fill_logic::raw_data_format_from_u8`'s real range),
/// mid-stream, with BOTH neighbours present — also covers "misaligned-both-neighbours mid-stream" for
/// every format, not just one.
#[test]
fn each_raw_data_format_byte_identical_both_neighbours() {
    for format in 0u8..=5 {
        let geo = simd_geo(format);
        assert_byte_identical(&geo, 5, true, true);
    }
}

/// Dedicated, explicitly-documented SIMD-prefix/scalar-tail case: `ENDIANNESS_WRONG_32` (a SIMD-path
/// format on every host) over a 128-byte cluster with a misaligned start — the mid-stream conversion
/// range is `[audio_start_pos_bytes & 0b11, cluster_size - 3)` = `[3, 125)`, 122 bytes: `argon`'s
/// 16-byte-lane vectorizeable prefix covers 112 of those (7 full lanes), leaving a genuine 10-byte
/// scalar tail (122 - 112) that only the scalar `convert_word_range` loop (not `convert_range_simd`)
/// touches — exactly the boundary this case is named for.
#[test]
fn simd_prefix_scalar_tail_boundary_is_explicit() {
    let geo = simd_geo(5); // ENDIANNESS_WRONG_32
    assert_byte_identical(&geo, 5, true, true);
}

/// A short last cluster: `first_cluster_index_with_no_audio_data == index + 1` makes THIS cluster the
/// last one carrying audio data, so `convert_cluster_data`'s `is_last_audio_cluster` branch truncates
/// the conversion range to `audio_region_end_offset()` (well inside `cluster_size`) instead of running
/// to `cluster_size - 3`. No next neighbour (there is no meaningful "next" past the last audio
/// cluster); prev present (the boundary with the second-to-last cluster is still real).
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

/// Misaligned, both neighbours, dedicated case (UNSIGNED_8 — the same shape
/// `fill_logic.rs`'s own existing `stitch_unsigned8_misaligned_both_neighbors`-style tests use),
/// named explicitly per the brief's required-coverage list even though
/// `each_raw_data_format_byte_identical_both_neighbours` already exercises this shape for every format.
#[test]
fn misaligned_both_neighbours_mid_stream() {
    let geo = simd_geo(2); // UNSIGNED_8
    assert_byte_identical(&geo, 9, true, true);
}

/// Bonus robustness case beyond the brief's required list: no neighbours at all (mirrors
/// `fill_logic.rs`'s own `no_neighbours_converts_but_leaves_flags_and_buffer_unstitched` test) — proves
/// the differential agrees on the "convert only, stitch skipped/no-op" path too.
#[test]
fn no_neighbours_byte_identical() {
    let geo = simd_geo(4); // ENDIANNESS_WRONG_24 (the special-cased 3-byte-group format)
    assert_byte_identical(&geo, 5, false, false);
}

/// `NATIVE` (format 0): `convert_cluster_data` is a no-op, but `stitch_boundaries`'s `stitch_prev` step
/// still unconditionally refreshes the previous cluster's overhang from this cluster's head, regardless
/// of format — a real, non-trivial differential case even though no byte conversion happens.
#[test]
fn native_format_still_stitches_overhang() {
    let geo = simd_geo(0); // NATIVE
    assert_byte_identical(&geo, 5, true, true);
}

// ---------------------------------------------------------------------------------------------------
// NON-VACUITY: the comparison must actually detect a real divergence, not just never fire.
// ---------------------------------------------------------------------------------------------------

/// Deliberately corrupts the RUST side's own output after both sides have run — standing in for a
/// hypothetical bug in `fill_logic::finish_convert_stitch` that diverges from the real C++
/// orchestration — and asserts [`diff`] DETECTS it. A C++-vs-Rust diff that only ever compares equal
/// values proves nothing; this is what proves the comparison above has teeth (the same pattern the
/// retired region_differential harness's own `Perturb`/non-vacuity test used).
#[test]
fn perturbed_rust_output_is_detected() {
    let geo = simd_geo(3); // ENDIANNESS_WRONG_16
    let index = 5;
    let mut rust = run_rust(&geo, index, true, true);
    let cpp = run_cpp(&geo, index, true, true);

    // Sanity: the REAL (unperturbed) outputs agree — otherwise this test would trivially "detect a
    // divergence" for the wrong reason (a pre-existing real bug, not the injected one).
    diff(&rust, &cpp).expect("precondition: real outputs must agree before perturbing");

    // Flip one bit of one byte of the self payload — the smallest possible corruption.
    rust.self_payload[0] ^= 0x01;

    let result = diff(&rust, &cpp);
    assert!(
        result.is_err(),
        "non-vacuity FAILED: a corrupted Rust output was not detected by the comparison"
    );
    assert_eq!(result.unwrap_err(), "self_payload diverged");
}

/// A second perturbation shape: corrupt a FLAG instead of a payload byte, proving the flag comparisons
/// (not just the payload `Vec` equality checks) are load-bearing too.
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
