//! Safe Rust wrapper over `cpp/harness_shim.cpp`'s `region_fill_diff_finish_fill_over_buffers` — the
//! C++ reference side of the fill-differential: a fresh, from-scratch replica of `finish_fill`'s
//! convert+stitch orchestration (`async_fill.cpp:110-162`) over plain buffers, calling the SAME
//! `convert_cluster_data`/`stitch_boundaries` primitives `fill_logic::finish_convert_stitch` calls (via
//! `deluge_sample_convert`) — wired up independently, in C++, straight from the real
//! `convert.h`/`stitch.h` headers, not a re-derivation of the Rust side's own logic.
//!
//! [`Geometry`]/[`Neighbour`]/[`ConvertState`] mirror `fill_logic`'s own `FillGeometry`/`NeighbourView`/
//! `ConvertState` field-for-field, kept as independent types for the same reason `fill_logic`'s own doc
//! gives for not sharing types with `streaming_loader`: this module has no dependency on
//! `deluge-bsp-rust` at all (it only talks to the cc-compiled C++ slice), so it stays fully decoupled.

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

/// Geometry the C++ reference needs — mirrors `fill_logic::FillGeometry`'s fields exactly (see this
/// module's doc for why it's an independent type rather than a shared one).
#[derive(Clone, Copy)]
pub struct Geometry {
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub first_cluster_index_with_no_audio_data: i32,
    pub cluster_size: usize,
    pub cluster_size_magnitude: usize,
    pub raw_data_format: u8,
}

/// One neighbour's payload + convert-state, mirrors `fill_logic::NeighbourView` field-for-field.
pub struct Neighbour<'a> {
    /// The neighbour's FULL `payload_with_trailing_slack()` buffer (`cluster_size + 7` bytes).
    pub payload: &'a mut [u8],
    /// The neighbour's `first_three_bytes_pre_data_conversion` (read only as the NEXT neighbour).
    pub unconverted_head: &'a [u8; 3],
    /// The neighbour's OWN `start_converted` flag (read/written only as the NEXT neighbour).
    pub start_converted: &'a mut bool,
    /// The neighbour's OWN `end_converted` flag (read/written only as the PREV neighbour).
    pub end_converted: &'a mut bool,
}

/// This chunk's own convert-state, mirrors `fill_logic::ConvertState` field-for-field.
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
pub struct ConvertState {
    pub first_three_bytes: [u8; 3],
    pub start_converted: bool,
    pub end_converted: bool,
}

/// Run the C++ reference's convert+stitch orchestration over `payload` (the FULL
/// `payload_with_trailing_slack()` span, `geo.cluster_size + 7` bytes) — mirrors
/// `fill_logic::finish_convert_stitch`'s own signature/contract exactly, so a differential test can
/// drive both sides with IDENTICAL call shapes over independently-seeded clones of the same inputs.
///
/// # Panics
/// Panics if `payload.len() < geo.cluster_size + 7`, or if `geo.cluster_size < 11` — same
/// preconditions as `fill_logic::finish_convert_stitch` (the prev edge slices
/// `[cluster_size-4, cluster_size+7)`, which underflows below that).
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

    // SAFETY: `payload` is a valid, exclusively-borrowed `cluster_size + 7`-byte buffer (asserted
    // above). `self_state`'s three fields are valid in/out locations for the whole call.
    // `prev`/`next`, when `Some`, are valid exclusively-borrowed buffers of the length just captured
    // (or null/0 when absent) — the C++ side writes only within `[0, len)` of each and the four flags,
    // matching these borrows; every raw part above is derived from a still-live reference (the `match`
    // above only decomposes, it never drops anything before the call below).
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
