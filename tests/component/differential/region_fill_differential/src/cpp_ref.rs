//! Safe wrapper over `cpp/harness_shim.cpp`'s `region_fill_diff_finish_fill_over_buffers`, the C++
//! side of the fill differential.
//!
//! The shim is an independent C++ implementation of the convert+stitch orchestration over plain
//! buffers, built directly on the real `convert.h`/`stitch.h` primitives rather than derived from
//! the Rust side. [`Geometry`], [`Neighbour`] and [`ConvertState`] mirror `fill_logic`'s
//! `FillGeometry`, `NeighbourView` and `ConvertState` field for field, but are separate types so
//! this side shares no code with the implementation under test.

mod ffi {
    unsafe extern "C" {
        #[allow(clippy::too_many_arguments)]
        pub fn region_fill_diff_finish_fill_over_buffers(
            self_data: *mut u8,
            self_len: usize,
            cluster_index: i32,
            format: u8,
            audio_data_start_pos_bytes: u32,
            audio_data_length_bytes: u64,
            first_cluster_index_with_no_audio_data: i32,
            cluster_size: usize,
            cluster_size_magnitude: usize,
            self_unconverted_head_out: *mut u8, // 3 bytes
            self_start_converted: *mut bool,
            self_end_converted: *mut bool,
            prev_payload: *mut u8, // null => absent/not loaded
            prev_payload_len: usize,
            prev_end_converted: *mut bool,
            next_payload: *mut u8, // null => absent/not loaded
            next_payload_len: usize,
            next_unconverted_head: *const u8, // 3 bytes; only read if next_payload != null
            next_start_converted: *mut bool,
        );
    }
}

/// Per-cluster geometry for the C++ reference; mirrors `fill_logic::FillGeometry`.
#[derive(Clone, Copy)]
pub struct Geometry {
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub first_cluster_index_with_no_audio_data: i32,
    pub cluster_size: usize,
    pub cluster_size_magnitude: usize,
    pub raw_data_format: u8,
}

/// One neighbour's payload and convert state; mirrors `fill_logic::NeighbourView`.
pub struct Neighbour<'a> {
    /// The neighbour's full payload-with-trailing-slack buffer (`cluster_size + 7` bytes).
    pub payload: &'a mut [u8],
    /// The neighbour's unconverted first three bytes; read only for the next neighbour.
    pub unconverted_head: &'a [u8; 3],
    /// The neighbour's `start_converted` flag; used only for the next neighbour.
    pub start_converted: &'a mut bool,
    /// The neighbour's `end_converted` flag; used only for the previous neighbour.
    pub end_converted: &'a mut bool,
}

/// The filled chunk's own convert state; mirrors `fill_logic::ConvertState`.
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
pub struct ConvertState {
    pub first_three_bytes: [u8; 3],
    pub start_converted: bool,
    pub end_converted: bool,
}

/// Runs the C++ convert+stitch orchestration over `payload`, the full `geo.cluster_size + 7`-byte
/// payload-with-trailing-slack span.
///
/// Takes the same arguments as `fill_logic::finish_convert_stitch`, so a test can drive both sides
/// identically over separately seeded copies of the same inputs.
///
/// # Panics
///
/// Panics if `payload.len() < geo.cluster_size + 7` or `geo.cluster_size < 11` (the previous
/// neighbour's edge slices `[cluster_size - 4, cluster_size + 7)`), matching
/// `fill_logic::finish_convert_stitch`.
pub fn finish_fill_over_buffers(
    payload: &mut [u8],
    index: u32,
    geo: &Geometry,
    self_state: &mut ConvertState,
    prev: Option<Neighbour<'_>>,
    next: Option<Neighbour<'_>>,
) {
    assert!(
        payload.len() >= geo.cluster_size + 7,
        "finish_fill_over_buffers: payload must be cluster_size + 7 bytes"
    );
    assert!(
        geo.cluster_size >= 11,
        "finish_fill_over_buffers: cluster_size must be >= 11"
    );

    let (prev_ptr, prev_len, prev_flag) = match prev {
        Some(p) => (
            p.payload.as_mut_ptr(),
            p.payload.len(),
            p.end_converted as *mut bool,
        ),
        None => (core::ptr::null_mut(), 0, core::ptr::null_mut()),
    };
    let (next_ptr, next_len, next_unconv, next_flag) = match next {
        Some(n) => (
            n.payload.as_mut_ptr(),
            n.payload.len(),
            n.unconverted_head.as_ptr(),
            n.start_converted as *mut bool,
        ),
        None => (
            core::ptr::null_mut(),
            0,
            core::ptr::null(),
            core::ptr::null_mut(),
        ),
    };

    // SAFETY: `payload` is an exclusive `cluster_size + 7`-byte buffer (asserted above) and
    // `self_state`'s fields are valid in/out locations. Each neighbour pointer comes from a live
    // exclusive borrow of the captured length (or is null/0 when absent); the C++ side writes only
    // within `[0, len)` of each buffer and to the flags.
    unsafe {
        ffi::region_fill_diff_finish_fill_over_buffers(
            payload.as_mut_ptr(),
            payload.len(),
            index as i32,
            geo.raw_data_format,
            geo.audio_data_start_pos_bytes,
            geo.audio_data_length_bytes,
            geo.first_cluster_index_with_no_audio_data,
            geo.cluster_size,
            geo.cluster_size_magnitude,
            self_state.first_three_bytes.as_mut_ptr(),
            &mut self_state.start_converted,
            &mut self_state.end_converted,
            prev_ptr,
            prev_len,
            prev_flag,
            next_ptr,
            next_len,
            next_unconv,
            next_flag,
        );
    }
}
