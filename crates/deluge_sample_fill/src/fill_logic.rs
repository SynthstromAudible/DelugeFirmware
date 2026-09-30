//! Pure fill arithmetic and convert/stitch sequencing for one cluster, over plain values and
//! buffers: no FFI, no statics, no `unsafe`.
//!
//! - [`begin`] resolves a cluster's read: its sector count (short for the last cluster) and byte
//!   offset in the file.
//! - [`finish_convert_stitch`] converts a freshly read cluster and stitches it against its loaded
//!   neighbours, through `deluge_sample_convert`.
//!
//! # 32-bit arithmetic
//!
//! [`begin`] computes the audio end position as a 64-bit sum truncated to `u32`, and the byte
//! offsets in 32 bits, matching the app's cluster addressing. It differs from 64-bit arithmetic
//! only for audio ending past 4 GiB, which FAT32 cannot hold.

/// A sample's geometry as [`begin`] and [`finish_convert_stitch`] need it: the fill-related subset
/// of [`FillContext`](crate::FillContext).
#[allow(dead_code)]
pub struct FillGeometry {
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub first_cluster_index_with_no_audio_data: i32,
    pub cluster_size: u32,
    pub cluster_size_magnitude: u32,
    pub raw_data_format: u8,
}

/// The "still recording, length unknown" length sentinel. `deluge_sample_source` has the same
/// constant; it depends on this crate, so this one cannot import it.
#[allow(dead_code)]
const UNKNOWN_LENGTH_SENTINEL: u64 = 0x8FFF_FFFF_FFFF_FFFF;

/// One cluster's read: how many 512-byte sectors to transfer and where the cluster starts in the
/// file. `ok` is false if the cluster lies wholly past the audio data, a fill that should not have
/// been requested.
#[allow(dead_code)]
pub struct BeginResult {
    pub num_sectors: u32,
    pub byte_offset: u32,
    pub ok: bool,
}

/// Resolves cluster `index`'s read from geometry alone.
///
/// A full cluster is read unless the audio length is known (nonzero and not the unknown-length
/// sentinel) and ends inside this cluster, in which case the sector count rounds the remaining
/// bytes up.
#[allow(dead_code)]
pub fn begin(index: u32, geo: &FillGeometry) -> BeginResult {
    let mut num_sectors: u32 = geo.cluster_size >> 9;

    // A zero or unknown length reads the full cluster.
    if geo.audio_data_length_bytes != 0 && geo.audio_data_length_bytes != UNKNOWN_LENGTH_SENTINEL {
        // 64-bit sum truncated to 32 bits; see the module docs.
        let audio_data_end_pos_bytes =
            geo.audio_data_length_bytes
                .wrapping_add(geo.audio_data_start_pos_bytes as u64) as u32;
        let start_byte_this_cluster = index << geo.cluster_size_magnitude;
        // Unsigned 32-bit difference reinterpreted as signed: negative when the cluster starts past
        // the end.
        let bytes_to_read = audio_data_end_pos_bytes.wrapping_sub(start_byte_this_cluster) as i32;
        if bytes_to_read <= 0 {
            // The cluster lies wholly past the audio data: skip the read.
            return BeginResult {
                num_sectors: 0,
                byte_offset: 0,
                ok: false,
            };
        }
        if bytes_to_read < geo.cluster_size as i32 {
            // Round up to whole sectors; `bytes_to_read > 0`, so no negative shift.
            num_sectors = (((bytes_to_read - 1) >> 9) + 1) as u32;
        }
    }

    let byte_offset = index << geo.cluster_size_magnitude;

    BeginResult {
        num_sectors,
        byte_offset,
        ok: true,
    }
}

// Convert and stitch. The algorithms live in `deluge_sample_convert`; this only decides which bytes
// each step sees and builds the neighbour edges. Publishing the chunk is `native_finish`'s job.

/// A chunk's convert state as [`finish_convert_stitch`] reads and writes it; the same shape as
/// [`crate::DelugeChunkConvertState`].
///
/// `first_three_bytes` holds the payload's first 3 bytes before conversion, which a neighbour's
/// stitch reads; `start_converted` and `end_converted` stop a boundary being stitched twice.
#[allow(dead_code)]
#[derive(Clone, Copy, Default, PartialEq, Eq)]
#[cfg_attr(test, derive(Debug))]
pub struct ConvertState {
    pub first_three_bytes: [u8; 3],
    pub start_converted: bool,
    pub end_converted: bool,
}

/// A loaded neighbour's payload and convert state, for [`finish_convert_stitch`] to build a stitch
/// edge from. One shape serves both directions; each side reads only its own fields.
#[allow(dead_code)]
pub struct NeighbourView<'a> {
    /// The neighbour's whole payload including trailing slack, `cluster_size + 7` bytes: a prev
    /// neighbour's edge, `[cluster_size - 4, cluster_size + 7)`, reaches into its slack.
    pub payload: &'a mut [u8],
    /// The neighbour's first 3 bytes before conversion; read only for a next neighbour.
    pub unconverted_head: &'a [u8; 3],
    /// The neighbour's `start_converted` flag; used only for a next neighbour.
    pub start_converted: &'a mut bool,
    /// The neighbour's `end_converted` flag; used only for a prev neighbour.
    pub end_converted: &'a mut bool,
}

/// Maps a `RawDataFormat` byte to `deluge_sample_convert`'s enum. An out-of-range byte, which
/// registration never produces, becomes `Native`, a no-op convert and stitch.
#[allow(dead_code)]
fn raw_data_format_from_u8(v: u8) -> deluge_sample_convert::RawDataFormat {
    use deluge_sample_convert::RawDataFormat::*;
    match v {
        1 => Float,
        2 => Unsigned8,
        3 => EndiannessWrong16,
        4 => EndiannessWrong24,
        5 => EndiannessWrong32,
        _ => Native, // 0, and any out-of-range byte.
    }
}

/// Converts cluster `index`'s freshly read `payload` and stitches it against its neighbours.
///
/// `payload` is the chunk's whole buffer including trailing slack, `cluster_size + 7` bytes;
/// conversion touches only the first `cluster_size`, the stitch the whole span. Conversion fills
/// `self_state.first_three_bytes`; the stitch may set its boundary flags and the neighbours'. Pass
/// `None` for a neighbour that is absent or not yet loaded.
///
/// # Panics
///
/// Panics if `payload` or a neighbour's buffer is shorter than `cluster_size + 7`, or if
/// `cluster_size < 4`. Real cluster sizes are at least 512.
#[allow(dead_code)]
pub fn finish_convert_stitch(
    payload: &mut [u8],
    index: u32,
    geo: &FillGeometry,
    self_state: &mut ConvertState,
    prev: Option<NeighbourView<'_>>,
    next: Option<NeighbourView<'_>>,
) {
    let format = raw_data_format_from_u8(geo.raw_data_format);
    let cluster_size = geo.cluster_size as usize;
    let cluster_size_magnitude = geo.cluster_size_magnitude as usize;
    assert!(
        payload.len() >= cluster_size + 7,
        "finish_convert_stitch: payload must be the full payload_with_trailing_slack() span \
         (cluster_size + 7 bytes)"
    );

    // Convert only the cluster itself, never the trailing slack.
    deluge_sample_convert::convert_cluster(
        &mut payload[..cluster_size],
        index as i32,
        format,
        deluge_sample_convert::ConvertGeometry {
            audio_data_start_pos_bytes: geo.audio_data_start_pos_bytes,
            audio_data_length_bytes: geo.audio_data_length_bytes,
            first_cluster_index_with_no_audio_data: geo.first_cluster_index_with_no_audio_data,
        },
        cluster_size,
        cluster_size_magnitude,
        &mut self_state.first_three_bytes,
    );

    // Prev's tail is the 11 bytes `[cluster_size - 4, cluster_size + 7)` of its buffer; next's head
    // is its first 7 bytes.
    let prev_edge = prev.map(|p| deluge_sample_convert::PrevEdge {
        tail: &mut p.payload[cluster_size - 4..cluster_size + 7],
        end_boundary_converted: p.end_converted,
    });
    let next_edge = next.map(|n| deluge_sample_convert::NextEdge {
        head: &mut n.payload[0..7],
        unconverted_head: n.unconverted_head,
        start_boundary_converted: n.start_converted,
    });

    // Stitch over the whole span, slack included.
    deluge_sample_convert::stitch_boundaries(
        payload,
        index as i32,
        format,
        geo.audio_data_start_pos_bytes,
        cluster_size,
        &mut self_state.start_converted,
        &mut self_state.end_converted,
        prev_edge,
        next_edge,
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 32 KiB clusters (magnitude 15), matching a common real-world FAT cluster size.
    const CLUSTER_SIZE: u32 = 32768;
    const CLUSTER_MAGNITUDE: u32 = 15;

    fn geo(audio_data_length_bytes: u64) -> FillGeometry {
        FillGeometry {
            audio_data_start_pos_bytes: 0,
            audio_data_length_bytes,
            first_cluster_index_with_no_audio_data: -1,
            cluster_size: CLUSTER_SIZE,
            cluster_size_magnitude: CLUSTER_MAGNITUDE,
            raw_data_format: 0,
        }
    }

    #[test]
    fn full_cluster_at_index_zero() {
        // Audio data extends well past this cluster's end (32768..65536).
        let g = geo(100_000);
        let r = begin(0, &g);
        assert!(r.ok);
        assert_eq!(r.num_sectors, CLUSTER_SIZE >> 9); // 64
        assert_eq!(r.byte_offset, 0);
    }

    #[test]
    fn full_cluster_at_mid_stream_index() {
        // index 1 -> start_byte_this_cluster = 32768; audio ends at 100000, still
        // more than a full cluster ahead (67232 bytes to read).
        let g = geo(100_000);
        let r = begin(1, &g);
        assert!(r.ok);
        assert_eq!(r.num_sectors, 64);
        assert_eq!(r.byte_offset, 32768);
    }

    #[test]
    fn short_last_cluster_rounds_sectors_up() {
        // index 3 -> start_byte_this_cluster = 98304; audio ends at 100000, so
        // bytes_to_read = 1696 (< cluster_size): short last cluster.
        // ((1696 - 1) >> 9) + 1 = (1695 >> 9) + 1 = 3 + 1 = 4 sectors (2048 bytes,
        // the smallest whole-sector count covering 1696 bytes).
        //
        // `deluge_sample_source::geometry::resident_bytes_for` gives 1696 bytes for the same
        // geometry, which the 2048 covers.
        let g = geo(100_000);
        let r = begin(3, &g);
        assert!(r.ok);
        assert_eq!(r.num_sectors, 4);
        assert_eq!(r.byte_offset, 98304);
    }

    #[test]
    fn cluster_wholly_past_audio_end_is_not_ok() {
        // index 4 -> start_byte_this_cluster = 131072, already past audio_data_end
        // (100000): bytes_to_read wraps negative -> ok=false, geometry error.
        let g = geo(100_000);
        let r = begin(4, &g);
        assert!(!r.ok);
        assert_eq!(r.num_sectors, 0);
        assert_eq!(r.byte_offset, 0);
    }

    #[test]
    fn unknown_length_sentinel_yields_full_cluster() {
        let g = geo(UNKNOWN_LENGTH_SENTINEL);
        let r = begin(7, &g);
        assert!(r.ok);
        assert_eq!(r.num_sectors, 64);
        assert_eq!(r.byte_offset, 7 << CLUSTER_MAGNITUDE);
    }

    #[test]
    fn zero_length_yields_full_cluster() {
        let g = geo(0);
        let r = begin(2, &g);
        assert!(r.ok);
        assert_eq!(r.num_sectors, 64);
        assert_eq!(r.byte_offset, 2 << CLUSTER_MAGNITUDE);
    }

    #[test]
    fn nonzero_audio_data_start_pos_shifts_the_end() {
        // A sample whose WAV data starts 1000 bytes into the file: audio_data_end =
        // 1000 + 50000 = 51000. index 1 -> start_byte_this_cluster = 32768,
        // bytes_to_read = 18232 (< cluster_size): short last cluster.
        // ((18232 - 1) >> 9) + 1 = (18231 >> 9) + 1 = 35 + 1 = 36 sectors.
        let mut g = geo(50_000);
        g.audio_data_start_pos_bytes = 1000;
        let r = begin(1, &g);
        assert!(r.ok);
        assert_eq!(r.num_sectors, 36);
        assert_eq!(r.byte_offset, 32768);
    }

    // `finish_convert_stitch` tests. The geometry is small, misaligned and 8-bit unsigned, so each
    // boundary converts a real straddling word. Each test compares `finish_convert_stitch` against
    // the same `deluge_sample_convert` calls made directly on a copy of the inputs: that checks the
    // wiring; the conversion itself is `deluge_sample_convert`'s to test.
    mod finish_convert_stitch_tests {
        use super::*;
        use deluge_sample_convert::{ConvertGeometry, NextEdge, PrevEdge, RawDataFormat};

        const CLUSTER_SIZE: usize = 32;
        const MAGNITUDE: u32 = 5;

        fn stitch_geo() -> FillGeometry {
            FillGeometry {
                audio_data_start_pos_bytes: 1, // misalignment = 1 & 0b11 = 1 (nonzero)
                audio_data_length_bytes: 1000,
                // No index used below is the last audio cluster.
                first_cluster_index_with_no_audio_data: 10,
                cluster_size: CLUSTER_SIZE as u32,
                cluster_size_magnitude: MAGNITUDE,
                raw_data_format: 2, // Unsigned8
            }
        }

        /// `cluster_size + 7` bytes, each `base.wrapping_add(offset)` — distinguishable,
        /// deterministic.
        fn ramp(base: u8) -> [u8; CLUSTER_SIZE + 7] {
            core::array::from_fn(|i| base.wrapping_add(i as u8))
        }

        /// The [`ConvertGeometry`] matching `stitch_geo()`.
        fn convert_geometry() -> ConvertGeometry {
            ConvertGeometry {
                audio_data_start_pos_bytes: 1,
                audio_data_length_bytes: 1000,
                first_cluster_index_with_no_audio_data: 10,
            }
        }

        #[test]
        fn convert_then_stitch_matches_calling_the_underlying_crate_directly_both_neighbours() {
            let geo = stitch_geo();
            let next_unconv: [u8; 3] = [70, 71, 72];

            // Run A: through finish_convert_stitch.
            let mut self_a = ramp(10);
            let mut prev_a = ramp(50);
            let mut next_a = ramp(90);
            let mut self_state_a = ConvertState::default();
            let mut prev_end_a = false;
            let mut prev_start_unused_a = false;
            let mut next_start_a = false;
            let mut next_end_unused_a = false;
            finish_convert_stitch(
                &mut self_a,
                1,
                &geo,
                &mut self_state_a,
                Some(NeighbourView {
                    payload: &mut prev_a,
                    unconverted_head: &[0; 3], // unread on the prev side
                    start_converted: &mut prev_start_unused_a,
                    end_converted: &mut prev_end_a,
                }),
                Some(NeighbourView {
                    payload: &mut next_a,
                    unconverted_head: &next_unconv,
                    start_converted: &mut next_start_a,
                    end_converted: &mut next_end_unused_a,
                }),
            );

            // Run B: directly through deluge_sample_convert, mirroring what A should have done.
            // --
            let mut self_b = ramp(10);
            let mut prev_b = ramp(50);
            let mut next_b = ramp(90);
            let mut head_b = [0u8; 3];
            deluge_sample_convert::convert_cluster(
                &mut self_b[..CLUSTER_SIZE],
                1,
                RawDataFormat::Unsigned8,
                convert_geometry(),
                CLUSTER_SIZE,
                MAGNITUDE as usize,
                &mut head_b,
            );
            let mut self_start_b = false;
            let mut self_end_b = false;
            let mut prev_end_b = false;
            let mut next_start_b = false;
            deluge_sample_convert::stitch_boundaries(
                &mut self_b,
                1,
                RawDataFormat::Unsigned8,
                1,
                CLUSTER_SIZE,
                &mut self_start_b,
                &mut self_end_b,
                Some(PrevEdge {
                    tail: &mut prev_b[CLUSTER_SIZE - 4..CLUSTER_SIZE + 7],
                    end_boundary_converted: &mut prev_end_b,
                }),
                Some(NextEdge {
                    head: &mut next_b[0..7],
                    unconverted_head: &next_unconv,
                    start_boundary_converted: &mut next_start_b,
                }),
            );

            assert_eq!(self_a, self_b, "self payload diverged");
            assert_eq!(prev_a, prev_b, "prev payload diverged");
            assert_eq!(next_a, next_b, "next payload diverged");
            assert_eq!(self_state_a.first_three_bytes, head_b);
            assert_eq!(self_state_a.start_converted, self_start_b);
            assert_eq!(self_state_a.end_converted, self_end_b);
            assert_eq!(prev_end_a, prev_end_b);
            assert_eq!(next_start_a, next_start_b);
            // Sanity: a real boundary conversion actually happened (misalignment != 0, non-native).
            assert!(self_state_a.start_converted);
            assert!(self_state_a.end_converted);
        }

        #[test]
        fn prev_only_matches_the_underlying_crate_directly() {
            let geo = stitch_geo();

            let mut self_a = ramp(5);
            let mut prev_a = ramp(60);
            let mut self_state_a = ConvertState::default();
            let mut prev_end_a = false;
            let mut prev_start_unused_a = false;
            finish_convert_stitch(
                &mut self_a,
                1,
                &geo,
                &mut self_state_a,
                Some(NeighbourView {
                    payload: &mut prev_a,
                    unconverted_head: &[0; 3],
                    start_converted: &mut prev_start_unused_a,
                    end_converted: &mut prev_end_a,
                }),
                None,
            );

            let mut self_b = ramp(5);
            let mut prev_b = ramp(60);
            let mut head_b = [0u8; 3];
            deluge_sample_convert::convert_cluster(
                &mut self_b[..CLUSTER_SIZE],
                1,
                RawDataFormat::Unsigned8,
                convert_geometry(),
                CLUSTER_SIZE,
                MAGNITUDE as usize,
                &mut head_b,
            );
            let mut self_start_b = false;
            let mut self_end_b = false;
            let mut prev_end_b = false;
            deluge_sample_convert::stitch_boundaries(
                &mut self_b,
                1,
                RawDataFormat::Unsigned8,
                1,
                CLUSTER_SIZE,
                &mut self_start_b,
                &mut self_end_b,
                Some(PrevEdge {
                    tail: &mut prev_b[CLUSTER_SIZE - 4..CLUSTER_SIZE + 7],
                    end_boundary_converted: &mut prev_end_b,
                }),
                None,
            );

            assert_eq!(self_a, self_b);
            assert_eq!(prev_a, prev_b);
            assert_eq!(self_state_a.start_converted, self_start_b);
            assert!(
                self_state_a.start_converted,
                "prev present -> self_start set true"
            );
            assert_eq!(self_state_a.end_converted, self_end_b);
            assert!(
                !self_state_a.end_converted,
                "no next -> self_end left false"
            );
            assert_eq!(prev_end_a, prev_end_b);
        }

        #[test]
        fn next_only_matches_the_underlying_crate_directly() {
            let geo = stitch_geo();
            let next_unconv: [u8; 3] = [33, 34, 35];

            let mut self_a = ramp(7);
            let mut next_a = ramp(80);
            let mut self_state_a = ConvertState::default();
            let mut next_start_a = false;
            let mut next_end_unused_a = false;
            finish_convert_stitch(
                &mut self_a,
                1,
                &geo,
                &mut self_state_a,
                None,
                Some(NeighbourView {
                    payload: &mut next_a,
                    unconverted_head: &next_unconv,
                    start_converted: &mut next_start_a,
                    end_converted: &mut next_end_unused_a,
                }),
            );

            let mut self_b = ramp(7);
            let mut next_b = ramp(80);
            let mut head_b = [0u8; 3];
            deluge_sample_convert::convert_cluster(
                &mut self_b[..CLUSTER_SIZE],
                1,
                RawDataFormat::Unsigned8,
                convert_geometry(),
                CLUSTER_SIZE,
                MAGNITUDE as usize,
                &mut head_b,
            );
            let mut self_start_b = false;
            let mut self_end_b = false;
            let mut next_start_b = false;
            deluge_sample_convert::stitch_boundaries(
                &mut self_b,
                1,
                RawDataFormat::Unsigned8,
                1,
                CLUSTER_SIZE,
                &mut self_start_b,
                &mut self_end_b,
                None,
                Some(NextEdge {
                    head: &mut next_b[0..7],
                    unconverted_head: &next_unconv,
                    start_boundary_converted: &mut next_start_b,
                }),
            );

            assert_eq!(self_a, self_b);
            assert_eq!(next_a, next_b);
            assert_eq!(self_state_a.start_converted, self_start_b);
            assert!(
                !self_state_a.start_converted,
                "no prev -> self_start left false"
            );
            assert_eq!(self_state_a.end_converted, self_end_b);
            assert!(
                self_state_a.end_converted,
                "next present -> self_end set true"
            );
            assert_eq!(next_start_a, next_start_b);
        }

        #[test]
        fn no_neighbours_converts_but_leaves_flags_and_buffer_unstitched() {
            let geo = stitch_geo();
            let mut self_a = ramp(3);
            let mut self_state_a = ConvertState::default();
            finish_convert_stitch(&mut self_a, 1, &geo, &mut self_state_a, None, None);

            let mut self_b = ramp(3);
            let mut head_b = [0u8; 3];
            deluge_sample_convert::convert_cluster(
                &mut self_b[..CLUSTER_SIZE],
                1,
                RawDataFormat::Unsigned8,
                convert_geometry(),
                CLUSTER_SIZE,
                MAGNITUDE as usize,
                &mut head_b,
            );
            // With no neighbours the stitch is a no-op, so run B skips it.
            assert_eq!(self_a, self_b);
            assert_eq!(self_state_a.first_three_bytes, head_b);
            assert!(!self_state_a.start_converted);
            assert!(!self_state_a.end_converted);
        }

        /// A boundary is visited from both sides as each chunk finishes. The first visit (chunk 0,
        /// with chunk 1 as `next`) converts the straddle and sets both chunks' flags; the second
        /// (chunk 1, with chunk 0 as `prev`) must not convert it again.
        #[test]
        fn second_visit_from_the_other_side_is_idempotent() {
            let geo = stitch_geo();

            // Run A: through finish_convert_stitch, chunk 0 then chunk 1.
            let mut c0_a = ramp(1);
            let mut c1_a = ramp(200);
            let mut c0_state_a = ConvertState::default();
            let mut c1_state_a = ConvertState::default();

            // Visit 1: chunk 0's own finish, chunk 1 as its `next`.
            {
                let NeighbourStateSplit {
                    start: c1_start,
                    end: c1_end,
                } = split(&mut c1_state_a);
                finish_convert_stitch(
                    &mut c0_a,
                    0,
                    &geo,
                    &mut c0_state_a,
                    None,
                    Some(NeighbourView {
                        payload: &mut c1_a,
                        // Chunk 1 is not yet converted, so this is not yet meaningful.
                        unconverted_head: &[0; 3],
                        start_converted: c1_start,
                        end_converted: c1_end,
                    }),
                );
            }
            assert!(
                c1_state_a.start_converted,
                "the first visit must mark the shared boundary converted on chunk 1's side too"
            );

            // Visit 2: chunk 1's own finish, chunk 0 as its `prev`. `prev.end_converted` is already
            // true, which must suppress a second straddle conversion.
            {
                let NeighbourStateSplit {
                    start: _c0_start_unused,
                    end: c0_end,
                } = split(&mut c0_state_a);
                finish_convert_stitch(
                    &mut c1_a,
                    1,
                    &geo,
                    &mut c1_state_a,
                    Some(NeighbourView {
                        payload: &mut c0_a,
                        unconverted_head: &[0; 3], // unread on the prev side
                        start_converted: _c0_start_unused,
                        end_converted: c0_end,
                    }),
                    None,
                );
            }

            // Run B: the same two visits, directly against deluge_sample_convert.
            let mut c0_b = ramp(1);
            let mut c1_b = ramp(200);
            let mut c0_head_b = [0u8; 3];
            let mut c1_head_b = [0u8; 3];
            let mut c0_start_b = false;
            let mut c0_end_b = false;
            let mut c1_start_b = false;
            let mut c1_end_b = false;

            deluge_sample_convert::convert_cluster(
                &mut c0_b[..CLUSTER_SIZE],
                0,
                RawDataFormat::Unsigned8,
                convert_geometry(),
                CLUSTER_SIZE,
                MAGNITUDE as usize,
                &mut c0_head_b,
            );
            deluge_sample_convert::stitch_boundaries(
                &mut c0_b,
                0,
                RawDataFormat::Unsigned8,
                1,
                CLUSTER_SIZE,
                &mut c0_start_b,
                &mut c0_end_b,
                None,
                Some(NextEdge {
                    head: &mut c1_b[0..7],
                    unconverted_head: &[0; 3],
                    start_boundary_converted: &mut c1_start_b,
                }),
            );

            deluge_sample_convert::convert_cluster(
                &mut c1_b[..CLUSTER_SIZE],
                1,
                RawDataFormat::Unsigned8,
                convert_geometry(),
                CLUSTER_SIZE,
                MAGNITUDE as usize,
                &mut c1_head_b,
            );
            deluge_sample_convert::stitch_boundaries(
                &mut c1_b,
                1,
                RawDataFormat::Unsigned8,
                1,
                CLUSTER_SIZE,
                &mut c1_start_b,
                &mut c1_end_b,
                Some(PrevEdge {
                    tail: &mut c0_b[CLUSTER_SIZE - 4..CLUSTER_SIZE + 7],
                    end_boundary_converted: &mut c0_end_b,
                }),
                None,
            );

            assert_eq!(c0_a, c0_b, "chunk 0 payload diverged");
            assert_eq!(c1_a, c1_b, "chunk 1 payload diverged");
            assert_eq!(c0_state_a.end_converted, c0_end_b);
            assert_eq!(c1_state_a.start_converted, c1_start_b);
            assert_eq!(c1_state_a.first_three_bytes, c1_head_b);
        }

        /// Splits a `&mut ConvertState` into separate borrows of its two flags, for a
        /// `NeighbourView`.
        struct NeighbourStateSplit<'a> {
            start: &'a mut bool,
            end: &'a mut bool,
        }
        fn split(s: &mut ConvertState) -> NeighbourStateSplit<'_> {
            NeighbourStateSplit {
                start: &mut s.start_converted,
                end: &mut s.end_converted,
            }
        }
    }
}
