//! Pure byte-range arithmetic for the native cluster fill: [`begin`]
//! reimplements `deluge::audio::stream::begin_fill`'s "resolve where/how much" step
//! (`storage/audio/stream/async_fill.cpp:76-98`) — the last-cluster short-read
//! sector-count calc and the cluster's byte offset within the file — from geometry
//! alone. No `StreamedChunk`, no FFI, no statics: this is arithmetic over plain
//! values, so it's trivially host-testable and needs no `unsafe`.
//!
//! Wired into `streaming_loader::prod::ProdOps::begin`/`finish`, replacing the
//! `deluge_streaming_begin_fill`/`_finish_fill` upcalls.
//!
//! ## `#[allow(dead_code)]` despite being wired
//!
//! This module (like `streaming_loader::fill_context_for`) is compiled unconditionally
//! (no `#[cfg]` — see `main.rs`'s `mod fill_logic;`), but its one real caller,
//! `streaming_loader::prod::ProdOps`, only compiles on the device OR under the `host_app` feature (see
//! that module's "Two compilation tiers" doc). A PLAIN host build/check/clippy of this bin (no
//! `--target`, no `--features host_app` — e.g. what a bare `cargo clippy --all-targets` runs, and what
//! `tests/streaming_fill_host.rs`'s own `#[path]` recompilation of `streaming_loader.rs` exercises)
//! therefore still never reaches these items outside `#[cfg(test)]`, so each carries
//! `#[allow(dead_code)]` — not because nothing calls them (they are wired into a real,
//! non-test call site), but because that call site doesn't exist on EVERY tier this file compiles on.
//!
//! ## Mirroring `begin_fill` bit-for-bit, including its 32-bit truncation
//!
//! `begin_fill`'s C++ source (`sample->audioDataLengthBytes` is `uint64_t`,
//! `sample->audioDataStartPosBytes` is `uint32_t` — see `model/sample/sample.h`)
//! computes `audioDataEndPosBytes` as a 64-bit sum but stores it in a **`uint32_t`**
//! local, silently truncating mod 2^32; `startByteThisCluster` and `bytesToRead`
//! (`int32_t`) are 32-bit throughout too. [`begin`] reproduces this literally —
//! `audio_data_end_pos_bytes` is computed via a 64-bit add then narrowed to `u32`
//! with `as`, exactly like the implicit C++ narrowing conversion — rather than
//! doing the arithmetic in 64 bits throughout. This only diverges from a "clean"
//! 64-bit implementation for sample files whose end position exceeds 4 GiB (not a
//! real case FAT32 or this firmware supports), so it's a safe, deliberate match to
//! the literal C++, not a bug being reproduced for its own sake — see the
//! "Cross-check" note below for why `deluge_sample_source::geometry::resident_bytes_for`
//! does NOT share this truncation.

/// Per-cluster geometry `begin` (and [`finish_convert_stitch`]) need, mirroring the fields of
/// `DelugeStreamingFillContext` (`include/libdeluge/streaming_fill.h`). `begin` itself only touches
/// the first four fields; `first_cluster_index_with_no_audio_data` and `raw_data_format` are read by
/// [`finish_convert_stitch`]'s convert/stitch step — kept in ONE struct rather than
/// two narrower ones since both are built from the exact same `FillContext` registration record at
/// each call site (`streaming_loader::prod::{to_fill_geometry, resolve}`).
///
/// `#[allow(dead_code)]`: see the module doc.
#[allow(dead_code)]
pub struct FillGeometry {
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub first_cluster_index_with_no_audio_data: i32,
    pub cluster_size: u32,
    pub cluster_size_magnitude: u32,
    pub raw_data_format: u8,
}

/// The "still recording, length unknown" sentinel (`sample_recorder.cpp`) —
/// identical to `deluge_sample_source::geometry::UNKNOWN_LENGTH_SENTINEL`, kept as
/// its own literal here rather than importing that crate (see the module doc's
/// "Cross-check" note for why the two modules stay independent).
///
/// `#[allow(dead_code)]`: see the module doc.
#[allow(dead_code)]
const UNKNOWN_LENGTH_SENTINEL: u64 = 0x8FFF_FFFF_FFFF_FFFF;

/// The resolved byte range for one cluster's read: how many 512-byte sectors to
/// transfer and where the cluster starts in the file, or `ok = false` if the
/// geometry places this cluster entirely past the audio data (a fill that
/// shouldn't have been requested — `begin_fill`'s own "Shouldn't really still
/// happen" `D_PRINTLN`).
///
/// `#[allow(dead_code)]`: see the module doc.
#[allow(dead_code)]
pub struct BeginResult {
    pub num_sectors: u32,
    pub byte_offset: u32,
    pub ok: bool,
}

/// Reimplements `begin_fill`'s short-last-cluster sector math from geometry alone.
///
/// `index` is the cluster index (`StreamedChunk::cluster_index`, always
/// non-negative in practice — mirrored here as `u32` rather than `begin_fill`'s
/// `int32_t` since the shifts below produce the identical bit pattern either way
/// for any in-range index).
///
/// `#[allow(dead_code)]`: see the module doc.
#[allow(dead_code)]
pub fn begin(index: u32, geo: &FillGeometry) -> BeginResult {
    let mut num_sectors: u32 = geo.cluster_size >> 9;

    // `begin_fill`: `if (sample->audioDataLengthBytes && sample->audioDataLengthBytes
    // != 0x8FFFFFFFFFFFFFFF)` — a zero length short-circuits the `&&` exactly like the
    // sentinel does, so both fall straight through to the full-cluster default below.
    if geo.audio_data_length_bytes != 0 && geo.audio_data_length_bytes != UNKNOWN_LENGTH_SENTINEL {
        // `uint32_t audioDataEndPosBytes = sample->audioDataLengthBytes +
        // sample->audioDataStartPosBytes;` — 64-bit add, then truncated to 32 bits by
        // the implicit narrowing conversion into the `uint32_t` local. `wrapping_add`
        // then `as u32` reproduces that truncation bit-for-bit (see module doc).
        let audio_data_end_pos_bytes =
            geo.audio_data_length_bytes
                .wrapping_add(geo.audio_data_start_pos_bytes as u64) as u32;
        // `uint32_t startByteThisCluster = clusterIndex << Cluster::size_magnitude;`
        let start_byte_this_cluster = index << geo.cluster_size_magnitude;
        // `int32_t bytesToRead = audioDataEndPosBytes - startByteThisCluster;` — unsigned
        // 32-bit subtraction, then reinterpreted as `int32_t`; `wrapping_sub` then `as
        // i32` is the same bit-preserving conversion C++'s narrowing-to-signed does here.
        let bytes_to_read = audio_data_end_pos_bytes.wrapping_sub(start_byte_this_cluster) as i32;
        if bytes_to_read <= 0 {
            // `D_PRINTLN("fail thing"); // Shouldn't really still happen` — geometry
            // places this cluster entirely past the audio data; skip the read.
            return BeginResult {
                num_sectors: 0,
                byte_offset: 0,
                ok: false,
            };
        }
        if bytes_to_read < geo.cluster_size as i32 {
            // `numSectors = ((bytesToRead - 1) >> 9) + 1;` — `bytes_to_read > 0` here
            // (checked above), so `bytes_to_read - 1 >= 0`: no negative right-shift.
            num_sectors = (((bytes_to_read - 1) >> 9) + 1) as u32;
        }
        // Otherwise, just leave it at the normal number of sectors.
    }

    // `byte_offset = static_cast<uint32_t>(clusterIndex) << Cluster::size_magnitude;`
    // — the same shift as `start_byte_this_cluster` above, computed unconditionally
    // (the un-taken branch above never needed it, but the formula is identical).
    let byte_offset = index << geo.cluster_size_magnitude;

    BeginResult {
        num_sectors,
        byte_offset,
        ok: true,
    }
}

// ── `finish`'s pure convert + stitch core ──────────────────────────────────────
//
// Reimplements `finish_fill`'s post-read tail (`storage/audio/stream/async_fill.cpp:107-163`, minus
// the `loaded`/`mark_ready` publish step, which stays in `streaming_loader::prod::ProdOps::finish` —
// that half touches the manager/chunk, not plain buffers): `convert_data_if_necessary()`
// (`cluster.cpp:76` -> `convert_cluster_data`, here [`deluge_sample_convert::convert_cluster`]) then
// the neighbour-edge stitch (`stitch_boundaries`, `stitch.cpp`, here
// [`deluge_sample_convert::stitch_boundaries`]). Deliberately does NOT re-transliterate either
// algorithm — both are already implemented, tested, and NEON-verified in `deluge_sample_convert`;
// this module only reproduces the ORCHESTRATION `finish_fill` does around them: which
// bytes to convert (the plain `payload()`, not the trailing slack), which span to stitch (the FULL
// `payload_with_trailing_slack()`), and how to build each neighbour's edge from a `NeighbourView`.

/// The per-chunk convert-state `finish`'s convert/stitch tail reads/writes for a chunk and its
/// neighbours: `first_three_bytes` is the pre-conversion first 3 bytes a neighbour's stitch reads;
/// `start_converted`/`end_converted` are the boundary idempotency guards. Deliberately a SEPARATE
/// type, not a re-export of [`crate::DelugeChunkConvertState`] (the live `StreamedChunk`
/// convert-state store's own type): that type needs `crate::chunk::convert_state`/
/// `set_convert_state`, so it only compiles on the device or under `host_app`, while this module
/// (and [`finish_convert_stitch`]) stays a plain, FFI-free buffer operation that compiles and tests
/// on EVERY tier, including a bare host build without `host_app` — the same reason [`FillGeometry`]
/// doesn't reuse `streaming_loader::FillContext` either. `streaming_loader::prod::ProdOps::finish`
/// (the only real caller, gated to the tier where both types exist) converts between the two with a
/// trivial field-for-field copy.
///
/// `#[allow(dead_code)]`: see the module doc.
#[allow(dead_code)]
#[derive(Clone, Copy, Default, PartialEq, Eq)]
#[cfg_attr(test, derive(Debug))]
pub struct ConvertState {
    pub first_three_bytes: [u8; 3],
    pub start_converted: bool,
    pub end_converted: bool,
}

/// One neighbour chunk's payload + convert-state, as [`finish_convert_stitch`] needs it to build a
/// `deluge_sample_convert` stitch edge. Mirrors the neighbour-gathering shape
/// `async_fill.cpp:116-157` builds (`StitchPrevEdge`/`StitchNextEdge`), unified into ONE shape for
/// both directions since [`finish_convert_stitch`] itself slices out whichever edge a given side
/// needs (see its doc). A `None` neighbour (not a `NeighbourView` at all) means "absent, or present
/// but not yet loaded" — the caller's job to determine (mirrors `prevCluster && prevCluster->loaded`),
/// same as `deluge_sample_convert::stitch_boundaries`'s own `Option<PrevEdge/NextEdge>`.
///
/// `#[allow(dead_code)]`: see the module doc.
#[allow(dead_code)]
pub struct NeighbourView<'a> {
    /// The neighbour's FULL `payload_with_trailing_slack()` buffer — `cluster_size + 7` bytes, the
    /// same length [`finish_convert_stitch`]'s own `payload` parameter needs. A prev neighbour's
    /// stitch edge (`[cluster_size-4, cluster_size+7)`) reaches all the way to the end of ITS OWN
    /// trailing slack, so the shorter plain `payload()` (`cluster_size` bytes) is not enough.
    pub payload: &'a mut [u8],
    /// The neighbour's `first_three_bytes_pre_data_conversion` — read only when this view is used as
    /// the NEXT neighbour (`stitch_boundaries` never reads it from the prev side).
    pub unconverted_head: &'a [u8; 3],
    /// The neighbour's OWN `start_converted` flag — read/written only when this view is used as the
    /// NEXT neighbour.
    pub start_converted: &'a mut bool,
    /// The neighbour's OWN `end_converted` flag — read/written only when this view is used as the
    /// PREV neighbour.
    pub end_converted: &'a mut bool,
}

/// Map `DelugeStreamingFillContext`/`FillGeometry`'s `raw_data_format` byte (mirrors
/// `RawDataFormat`, `audio_file_format.h`) to `deluge_sample_convert`'s own enum. Any value outside
/// the real C++ enum's range (0..=5) — which the trusted C++ registration path never actually
/// produces, see `sample_stream.cpp`'s `register_fill_context` — degrades to `Native` (a no-op
/// convert/stitch), the same safe default `FillContext::UNREGISTERED`'s `cluster_size == 0` sentinel
/// already relies on elsewhere in this crate to fail closed rather than misinterpret garbage.
///
/// `#[allow(dead_code)]`: see the module doc.
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

/// Reimplements `finish_fill`'s convert + stitch tail (see the section doc above) over plain
/// buffers: `payload` is `index`'s own chunk, its FULL `payload_with_trailing_slack()` span
/// (`geo.cluster_size + 7` bytes — `convert_cluster` below only touches the leading `cluster_size` of
/// it, matching `convert_data_if_necessary()`'s own `payload()`-only call; `stitch_boundaries` needs
/// the whole thing). `self_state` is this chunk's own convert-state (read AND written — `convert`
/// fills `first_three_bytes`; `stitch` may set `start_converted`/`end_converted`). `prev`/`next` are
/// `None` when that neighbour is absent or not yet loaded (the caller's job — see [`NeighbourView`]'s
/// doc), `Some` otherwise.
///
/// # Panics
/// Panics if `payload.len() < geo.cluster_size as usize + 7`, or if `geo.cluster_size < 11` (the prev
/// edge slices `[cluster_size-4, cluster_size+7)`, which underflows below that) — neither occurs for
/// any real `Cluster::size` (a power of two well above 11, e.g. the device's actual cluster sizes are
/// always at least 512 bytes).
///
/// `#[allow(dead_code)]`: see the module doc.
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

    // `cluster.convert_data_if_necessary()` (cluster.cpp:76-91): converts only `payload()`
    // (`cluster_size` bytes), never the trailing slack.
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

    // Build each neighbour's edge (async_fill.cpp:133-157): prev's tail is the 11 bytes
    // `[cluster_size-4, cluster_size+7)` of ITS OWN trailing-slack buffer; next's head is the first 7
    // bytes of its (plain) payload. `NeighbourView::payload` is always the full trailing-slack span
    // (see that type's doc), so both slices are always in range.
    let prev_edge = prev.map(|p| deluge_sample_convert::PrevEdge {
        tail: &mut p.payload[cluster_size - 4..cluster_size + 7],
        end_boundary_converted: p.end_converted,
    });
    let next_edge = next.map(|n| deluge_sample_convert::NextEdge {
        head: &mut n.payload[0..7],
        unconverted_head: n.unconverted_head,
        start_boundary_converted: n.start_converted,
    });

    // `stitch_boundaries(self_span, ...)` (async_fill.cpp:159-162): over the FULL
    // `payload_with_trailing_slack()` span.
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
        // Cross-check: `deluge_sample_source::geometry::resident_bytes_for` with the
        // same geometry (in bytes, not sectors) returns exactly 1696 residency bytes
        // for this index — 4 sectors * 512 = 2048 bytes safely covers that. See the
        // module doc's "Cross-check" note for why this is asserted by hand here
        // rather than by importing that crate.
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

    // ── finish_convert_stitch ──────────────────────────────────────────────────
    //
    // Small, misaligned (`audio_data_start_pos_bytes & 0b11 != 0`), non-24-bit geometry (UNSIGNED_8,
    // same shape `deluge_sample_convert`'s own `stitch_unsigned8_misaligned_both_neighbors` test
    // uses), so a real straddling word gets converted at each boundary rather than the `need_copy7`
    // word-aligned shortcut. Every test below proves correctness DIFFERENTIALLY: run
    // `finish_convert_stitch`, then separately call `deluge_sample_convert::convert_cluster` +
    // `stitch_boundaries` directly (the same calls `finish_convert_stitch` should be making) on a
    // fresh clone of the same inputs, and assert the two runs land on byte-identical buffers/flags —
    // this proves the WIRING (which bytes/flags get threaded where) without re-deriving the
    // conversion/stitch math itself, which `deluge_sample_convert`'s own suite already covers.
    mod finish_convert_stitch_tests {
        use super::*;
        use deluge_sample_convert::{ConvertGeometry, NextEdge, PrevEdge, RawDataFormat};

        const CLUSTER_SIZE: usize = 32;
        const MAGNITUDE: u32 = 5;

        fn stitch_geo() -> FillGeometry {
            FillGeometry {
                audio_data_start_pos_bytes: 1, // misalignment = 1 & 0b11 = 1 (nonzero)
                audio_data_length_bytes: 1000,
                first_cluster_index_with_no_audio_data: 10, // no index used below is "last audio cluster"
                cluster_size: CLUSTER_SIZE as u32,
                cluster_size_magnitude: MAGNITUDE,
                raw_data_format: 2, // Unsigned8
            }
        }

        /// `cluster_size + 7` bytes, each `base.wrapping_add(offset)` — distinguishable, deterministic.
        fn ramp(base: u8) -> [u8; CLUSTER_SIZE + 7] {
            core::array::from_fn(|i| base.wrapping_add(i as u8))
        }

        /// convert_geometry! `ConvertGeometry` mirrors `stitch_geo()`'s first three fields exactly.
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

            // -- Run A: through finish_convert_stitch. --
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

            // -- Run B: directly through deluge_sample_convert, mirroring what A should have done. --
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
            // No stitch call at all on the B side: with neither neighbour, stitch_boundaries is a
            // documented no-op over self_data (see deluge_sample_convert's own
            // `stitch_no_neighbors_unchanged` test), so skipping the call entirely is equivalent.
            assert_eq!(self_a, self_b);
            assert_eq!(self_state_a.first_three_bytes, head_b);
            assert!(!self_state_a.start_converted);
            assert!(!self_state_a.end_converted);
        }

        /// The real idempotency shape: a cluster boundary gets visited from BOTH sides as each
        /// neighbour's own `finish` runs (mirrors `async_fill.cpp`'s `extra_bytes_at_end_converted` /
        /// `extra_bytes_at_start_converted` cross-chunk flags). The FIRST visit (chunk 0's own
        /// `finish`, with chunk 1 as its `next`) performs the real straddle conversion and sets BOTH
        /// its own `self_end_converted` and chunk 1's `start_converted` flag. The SECOND visit (chunk
        /// 1's own `finish`, with chunk 0 as its `prev`) must NOT re-run that conversion — proven
        /// again differentially: mirroring the exact same two calls directly against
        /// `deluge_sample_convert` and checking the end states agree.
        #[test]
        fn second_visit_from_the_other_side_is_idempotent() {
            let geo = stitch_geo();

            // -- Run A: through finish_convert_stitch, chunk 0 then chunk 1. --
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
                        unconverted_head: &[0; 3], // chunk 1 not yet converted -> not yet meaningful
                        start_converted: c1_start,
                        end_converted: c1_end,
                    }),
                );
            }
            assert!(
                c1_state_a.start_converted,
                "the first visit must mark the shared boundary converted on chunk 1's side too"
            );

            // Visit 2: chunk 1's own finish, chunk 0 as its `prev`. Uses chunk 0's now-converted
            // `first_three_bytes` as chunk 1's `unconverted_head` input is N/A here (that field is
            // only read from the NEXT side) -- what matters is `prev.end_converted` already being
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

            // -- Run B: the same two visits, directly against deluge_sample_convert. --
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

        /// Splits a `&mut ConvertState` into independent `&mut bool` borrows of its two flags, so a
        /// caller can hand one to a `NeighbourView` while separately reading the other — mirrors how
        /// `streaming_loader::prod::ProdOps::finish` borrows a neighbour's convert-state (test-only
        /// plumbing; production code borrows two DISTINCT `ConvertState` copies instead, so it never
        /// needs this split).
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
