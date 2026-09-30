//! Reader differential: `deluge_sample_reader`'s `open`/`window`/`advance`/`deluge_sample_read`
//! over real streamed chunks must return the same bytes as [`oracle_frame`], a direct read of each
//! chunk's resident payload at the location [`mapping`] computes.
//!
//! The oracle never goes through the reader, and [`mapping`] is written independently of the
//! reader's own `locate`, so a divergence in either's arithmetic, or in the reader's straddle
//! handling, shows up as a byte mismatch.
//!
//! # The synthetic sample
//!
//! Each case builds a [`ChunkHarness`]: a real slab-backed `deluge_resource` manager with real
//! streamed chunks. Each cluster's payload is seeded with [`ramp`], then every cluster is passed
//! through `deluge_sample_fill::native_finish` in index order, so the real stitch publishes each
//! cluster's head into its predecessor's 7-byte trailing slack, which frames straddling a cluster
//! boundary read. The format is `Native`, so conversion leaves the ramp unchanged, and
//! `audio_data_start_bytes` is 0, so [`mapping::Geometry::total_frames`] is the exact end of audio.
//! All clusters are filled before a reader opens, except in the one forced-failure case, which
//! leaves its cluster unconstructed.
#![cfg(not(target_os = "none"))]

use core::ffi::c_void;
use core::ptr;
use core::sync::atomic::{AtomicPtr, Ordering};

use deluge_resource::{
    BACKING_SLAB, deluge_resource_create, deluge_resource_define_asset, deluge_resource_request,
    deluge_resource_set_construct, deluge_resource_set_slab,
};
use deluge_sample_fill::{FillContext, deluge_streaming_set_fill_context};
use deluge_sample_reader::abi::{
    DelugeFrameWindow, deluge_sample_read, deluge_sample_reader_advance,
    deluge_sample_reader_close, deluge_sample_reader_ok, deluge_sample_reader_open,
    deluge_sample_reader_seek, deluge_sample_reader_window,
};
use deluge_sample_reader::reader::ReadHint;

use region_read_differential::mapping::{self, Geometry as MapGeo};
use region_read_differential::ramp;

// Host stand-ins for the C-ABI symbols `deluge_resource` calls. Each test is single-threaded with
// no audio ISR, so no-ops are correct.
#[unsafe(no_mangle)]
extern "C" fn ENTER_CRITICAL_SECTION() {}
#[unsafe(no_mangle)]
extern "C" fn EXIT_CRITICAL_SECTION() {}
#[unsafe(no_mangle)]
extern "C" fn deluge_in_interrupt() -> bool {
    false
}
// Never on the audio context here; the timer only feeds latency stats nothing reads.
#[unsafe(no_mangle)]
extern "C" fn deluge_in_audio_context() -> bool {
    false
}
#[unsafe(no_mangle)]
extern "C" fn deluge_debug_now_frames() -> u32 {
    0
}

/// Link stub for the synchronous card read (`libdeluge/file_io.h`) that `deluge_sample_reader`'s
/// reservation fill references. Never reached here; fails without touching `dst`.
#[unsafe(no_mangle)]
extern "C" fn deluge_efatfs_read_at(
    _handle: u32,
    _byte_offset: u32,
    _dst: *mut c_void,
    _count: u32,
    _out_read: *mut u32,
) -> deluge_sample_fill::DelugeStatus {
    // `DELUGE_ERR_IO`: a failed card read.
    -5
}

/// Link stub for the reader's async fill request. Nothing answers it, so a cluster the harness did
/// not prefill never becomes ready.
#[unsafe(no_mangle)]
extern "C" fn deluge_streaming_request_chunk(
    _chunk_backing: *mut deluge_sample_fill::chunk::DelugeChunkBacking,
) {
}

/// The manager `deluge_streaming_resource_manager` returns to `native_finish`.
static ACTIVE_MANAGER: AtomicPtr<c_void> = AtomicPtr::new(ptr::null_mut());

#[unsafe(no_mangle)]
extern "C" fn deluge_streaming_resource_manager() -> *mut c_void {
    ACTIVE_MANAGER.load(Ordering::Relaxed)
}

/// Construct callback forwarding to `deluge_sample_fill::chunk::deluge_streaming_chunk_construct`,
/// the one production uses for streamed sample chunks.
///
/// Needed only because `ConstructFn` takes `dest: *mut u8` and the target takes `*mut c_void`;
/// Rust function-pointer types don't unify the two.
unsafe extern "C" fn construct_streamed_chunk(
    ctx: *mut c_void,
    owner: *mut c_void,
    index: u32,
    dest: *mut u8,
) {
    // SAFETY: forwards `dest` unchanged under the construct contract `deluge_resource_request`
    // upholds for this callback.
    unsafe {
        deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
            ctx,
            owner,
            index,
            dest as *mut c_void,
        );
    }
}

/// Serializes every test: `deluge_sample_fill`'s fill-context table is a global keyed by asset id
/// (every fresh manager starts at 0), and [`ACTIVE_MANAGER`] is global too.
static TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// The smallest realistic cluster size: one sector.
const CLUSTER_SIZE: u32 = 512;
const CLUSTER_MAGNITUDE: u32 = 9;

/// 24-bit mono. A 3-byte frame stride does not divide [`CLUSTER_SIZE`], so frames straddle cluster
/// boundaries.
const BYTE_DEPTH: u8 = 3;
const NUM_CHANNELS: u8 = 1;
const FRAME_STRIDE: u32 = BYTE_DEPTH as u32 * NUM_CHANNELS as u32;

/// A fill context for `num_full_clusters` full clusters plus an optional short last cluster of
/// `short_last_cluster_bytes` (0 for none).
fn fill_context(num_full_clusters: u32, short_last_cluster_bytes: u32) -> FillContext {
    let audio_data_length_bytes =
        num_full_clusters as u64 * CLUSTER_SIZE as u64 + short_last_cluster_bytes as u64;
    FillContext {
        efatfs_handle: 0,
        audio_data_start_pos_bytes: 0,
        audio_data_length_bytes,
        first_cluster_index_with_no_audio_data: -1, // unused by Native
        cluster_size: CLUSTER_SIZE,
        cluster_size_magnitude: CLUSTER_MAGNITUDE,
        raw_data_format: 0, // Native: conversion is a no-op
        byte_depth: BYTE_DEPTH,
        num_channels: NUM_CHANNELS,
    }
}

fn map_geometry(ctx: &FillContext) -> MapGeo {
    MapGeo {
        audio_data_start_bytes: ctx.audio_data_start_pos_bytes,
        audio_data_length_bytes: ctx.audio_data_length_bytes,
        cluster_size_bytes: ctx.cluster_size,
        byte_depth: ctx.byte_depth,
        num_channels: ctx.num_channels,
    }
}

/// A real manager with real, constructed streamed chunks over a real heap.
struct ChunkHarness {
    asset: u32,
    /// Backing pointer per cluster index, each leased by the harness and never released, so every
    /// chunk stays resident whatever the reader does.
    backings: Vec<*mut u8>,
    /// Kept alive for the harness's whole life; never read directly.
    _arena: Vec<u128>,
}

impl ChunkHarness {
    /// Builds the harness for `ctx`, seeding and finishing the first `prefill` clusters. The
    /// forced-failure case passes 0, leaving the reader to construct the chunk.
    fn new(ctx: FillContext, prefill: u32) -> Self {
        let words = (4 * 1024 * 1024usize).div_ceil(16); // 4 MiB arena
        let mut arena: Vec<u128> = vec![0u128; words];
        let base = arena.as_mut_ptr() as *mut u8;
        // SAFETY: `arena` is a 16-aligned `words * 16`-byte allocation kept alive by the harness.
        let heap = unsafe { deluge_alloc::deluge_heap_create(base, words * 16) };
        assert!(!heap.is_null());

        let payload_offset =
            deluge_sample_fill::chunk::deluge_streamed_chunk_payload_offset() as usize;
        let backing_size = payload_offset + CLUSTER_SIZE as usize + 7; // + kTrailingSlackBytes

        // SAFETY: `heap` is the live handle just created above; capacity comfortably exceeds every
        // case's own cluster count.
        let slab =
            unsafe { deluge_alloc::slab::deluge_slab_create_unmanaged(heap, backing_size, 32) };
        assert!(!slab.is_null());
        // SAFETY: `heap` is live.
        let handle = unsafe { deluge_resource_create(heap, 4, 32) };
        assert!(!handle.is_null());
        // SAFETY: `handle`/`slab` are both live, over the same heap.
        unsafe { deluge_resource_set_slab(handle, slab) };
        // SAFETY: `handle` is live.
        let asset = unsafe {
            deluge_resource_define_asset(
                handle,
                ptr::null_mut(),
                None,
                None,
                ptr::null_mut(),
                1,
                BACKING_SLAB,
            )
        };
        // SAFETY: `handle`/`asset` are live/valid; `construct_streamed_chunk` has the exact
        // `ConstructFn` C-ABI signature.
        unsafe {
            deluge_resource_set_construct(handle, asset, Some(construct_streamed_chunk));
        }
        // Must be set before any reader or `native_finish` call.
        ACTIVE_MANAGER.store(handle as *mut c_void, Ordering::Relaxed);
        deluge_streaming_set_fill_context(ptr::null_mut(), asset, ctx);

        let mut backings = Vec::with_capacity(prefill as usize);
        for index in 0..prefill {
            // SAFETY: `handle`/`asset` are live. The slab ignores `size`.
            let backing = unsafe { deluge_resource_request(handle, asset, index, backing_size) };
            assert!(!backing.is_null(), "request failed for cluster {index}");
            // SAFETY: `backing` was just constructed; its payload holds `CLUSTER_SIZE` bytes and is
            // reached through the accessor, never assumed equal to the backing.
            let payload = unsafe { deluge_sample_fill::chunk::payload(backing as *mut c_void) };
            let seed = ramp(index, CLUSTER_SIZE as usize);
            // SAFETY: `payload` is `CLUSTER_SIZE` bytes, exclusively held until `native_finish`;
            // `seed` is the same length.
            unsafe { core::ptr::copy_nonoverlapping(seed.as_ptr(), payload, seed.len()) };
            backings.push(backing);
        }
        // Finish in index order: the real stitch publishes each cluster's head into its (already
        // ready) predecessor's trailing slack.
        for (index, &backing) in backings.iter().enumerate() {
            assert!(
                deluge_sample_fill::native_finish(backing as *mut c_void, true),
                "native_finish failed for cluster {index}"
            );
        }

        ChunkHarness {
            asset,
            backings,
            _arena: arena,
        }
    }

    fn backing(&self, index: u32) -> *mut u8 {
        self.backings[index as usize]
    }
}

/// The oracle: `frame_bytes` bytes at within-cluster `byte_offset`, read straight from the chunk's
/// payload.
///
/// Valid while `byte_offset + frame_bytes <= cluster_size + 7`: each backing holds the payload plus
/// the 7-byte trailing slack.
fn oracle_frame(
    h: &ChunkHarness,
    cluster_index: u32,
    byte_offset: u32,
    frame_bytes: u32,
) -> Vec<u8> {
    let backing = h.backing(cluster_index);
    // SAFETY: `backing` is a resident, `native_finish`ed chunk (`ChunkHarness::new`).
    let payload = unsafe { deluge_sample_fill::chunk::payload(backing as *mut c_void) };
    let mut out = vec![0u8; frame_bytes as usize];
    // SAFETY: the payload is backed by `cluster_size + 7` bytes, and every case keeps
    // `byte_offset < cluster_size` and `frame_bytes <= 4`.
    unsafe {
        core::ptr::copy_nonoverlapping(
            payload.add(byte_offset as usize),
            out.as_mut_ptr(),
            frame_bytes as usize,
        )
    };
    out
}

/// Reads `num_frames` frames from `start_frame` in `direction` through the oracle, stopping at the
/// end of audio, and concatenates them in visited order.
fn oracle_read(
    h: &ChunkHarness,
    geo: &MapGeo,
    start_frame: u64,
    num_frames: u32,
    direction: i8,
) -> Vec<u8> {
    let total = geo.total_frames();
    let mut out = Vec::new();
    let mut frame = start_frame as i64;
    for _ in 0..num_frames {
        if frame < 0 || frame as u64 >= total {
            break;
        }
        let (cluster_index, byte_offset) = mapping::locate(frame as u64, geo);
        out.extend(oracle_frame(h, cluster_index, byte_offset, FRAME_STRIDE));
        frame += direction as i64;
    }
    out
}

/// Reads through the reader's zero-copy handle (`open` → `window`/`advance` → `close`),
/// collecting frames in visited order.
///
/// A backward reader's window points at its current (highest) frame, with earlier frames at lower
/// addresses, so the walk uses a negative stride and matches [`oracle_read`]'s order.
///
/// Returns `(collected_bytes, frames_visited, reader_ok_at_the_end)`.
fn handle_read(
    asset: u32,
    start_frame: u64,
    num_frames: u32,
    direction: i8,
    hint: ReadHint,
) -> (Vec<u8>, u32, bool) {
    let ptr = deluge_sample_reader_open(asset, start_frame, direction, hint);
    assert!(!ptr.is_null());
    let mut collected = Vec::new();
    let mut remaining = num_frames;
    let mut visited = 0u32;
    loop {
        if remaining == 0 {
            break;
        }
        // SAFETY: `ptr` is live, not yet closed.
        let w: DelugeFrameWindow = unsafe { deluge_sample_reader_window(ptr) };
        if w.frame_count == 0 {
            break;
        }
        let n = w.frame_count.min(remaining);
        let base = w.frames as *const u8;
        for k in 0..n {
            let step = if direction >= 0 {
                k as isize
            } else {
                -(k as isize)
            };
            // SAFETY: `window` guarantees `n <= frame_count` whole (possibly straddling) frames of
            // `FRAME_STRIDE` bytes from `base` in the reader's direction.
            let frame_ptr = unsafe { base.offset(step * FRAME_STRIDE as isize) };
            for b in 0..FRAME_STRIDE {
                // SAFETY: see above.
                collected.push(unsafe { *frame_ptr.add(b as usize) });
            }
        }
        // SAFETY: `ptr` is live, not yet closed.
        unsafe { deluge_sample_reader_advance(ptr, n) };
        remaining -= n;
        visited += n;
    }
    // SAFETY: `ptr` is live, not yet closed.
    let ok = unsafe { deluge_sample_reader_ok(ptr) };
    // SAFETY: `ptr` is live, not yet closed, and not used again after this call.
    unsafe { deluge_sample_reader_close(ptr) };
    (collected, visited, ok)
}

/// Reads through the stateless `deluge_sample_read` copy-out (always forward,
/// `ReadHint::Cached`). Returns `(bytes_written, frames_written)`.
fn via_read(asset: u32, start_frame: u64, num_frames: u32) -> (Vec<u8>, u32) {
    let dest_bytes = num_frames as usize * FRAME_STRIDE as usize;
    let mut dest = vec![0xAAu8; dest_bytes];
    // SAFETY: `dest` is exactly `dest_bytes` bytes long.
    let written = unsafe {
        deluge_sample_read(
            asset,
            start_frame,
            num_frames,
            dest.as_mut_ptr() as *mut c_void,
            dest_bytes,
        )
    };
    dest.truncate(written as usize * FRAME_STRIDE as usize);
    (dest, written)
}

// Cases: each asserts the handle read matches the oracle, and forward cases check
// `deluge_sample_read` against it too.

#[test]
fn within_one_cluster_forward_matches_oracle_and_read() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0); // 3 full clusters, 1536 bytes, total_frames = 512
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);

    let (start, count) = (10u64, 20u32); // inside cluster 0
    let expected = oracle_read(&h, &geo, start, count, 1);
    let (actual, visited, ok) = handle_read(h.asset, start, count, 1, ReadHint::Cached);
    assert_eq!(visited, count);
    assert!(ok);
    assert_eq!(
        actual, expected,
        "handle read diverged from the current-path oracle"
    );

    let (via, via_frames) = via_read(h.asset, start, count);
    assert_eq!(via_frames, count);
    assert_eq!(
        via, expected,
        "deluge_sample_read diverged from the current-path oracle"
    );
    assert_eq!(
        via, actual,
        "deluge_sample_read and the handle loop must agree with each other"
    );
}

#[test]
fn straddle_forward_across_one_cluster_boundary_matches_oracle_and_read() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0);
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);

    // Frames 165..185 cross the cluster 0/1 boundary; frame 170 straddles it at byte 510.
    let (start, count) = (165u64, 20u32);
    let expected = oracle_read(&h, &geo, start, count, 1);
    let (actual, visited, ok) = handle_read(h.asset, start, count, 1, ReadHint::Cached);
    assert_eq!(visited, count);
    assert!(ok);
    assert_eq!(actual, expected);

    let (via, via_frames) = via_read(h.asset, start, count);
    assert_eq!(via_frames, count);
    assert_eq!(via, expected);
}

#[test]
fn straddle_backward_across_one_cluster_boundary_matches_oracle() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0);
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);

    // Frame 185 (cluster 1, offset 43) back 25 frames to 161 (cluster 0, offset 483): the same
    // boundary in reverse.
    let (start, count) = (185u64, 25u32);
    let expected = oracle_read(&h, &geo, start, count, -1);
    let (actual, visited, ok) = handle_read(h.asset, start, count, -1, ReadHint::Cached);
    assert_eq!(visited, count);
    assert!(ok);
    assert_eq!(actual, expected);
}

#[test]
fn last_cluster_short_read_matches_oracle_and_read() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    // 3 full clusters + 100 bytes = 1636 bytes, so 545 whole frames; the 1-byte tail is not one.
    let ctx = fill_context(3, 100);
    let h = ChunkHarness::new(ctx, 4);
    let geo = map_geometry(&ctx);
    assert_eq!(geo.total_frames(), 545);

    // Only 5 frames remain from 540: the read comes back short, not padded, and the reader stays
    // ok, since this is end of audio, not a failure.
    let (start, count) = (540u64, 20u32);
    let expected = oracle_read(&h, &geo, start, count, 1);
    assert_eq!(expected.len(), 5 * FRAME_STRIDE as usize);

    let (actual, visited, ok) = handle_read(h.asset, start, count, 1, ReadHint::Cached);
    assert_eq!(visited, 5, "only 5 real frames remain past frame 540");
    assert!(ok, "running out of real audio is EOF, not a failure");
    assert_eq!(actual, expected);

    let (via, via_frames) = via_read(h.asset, start, count);
    assert_eq!(via_frames, 5);
    assert_eq!(via, expected);
}

#[test]
fn reverse_full_sample_scan_matches_oracle() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0);
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);
    let total = geo.total_frames();

    let expected = oracle_read(&h, &geo, total - 1, total as u32, -1);
    assert_eq!(expected.len(), total as usize * FRAME_STRIDE as usize);

    let (actual, visited, ok) = handle_read(h.asset, total - 1, total as u32, -1, ReadHint::Scan);
    assert_eq!(
        visited as u64, total,
        "a full backward scan must visit every frame"
    );
    assert!(ok);
    assert_eq!(
        actual, expected,
        "a whole-sample backward scan diverged from the oracle"
    );
}

#[test]
fn whole_sample_forward_scan_matches_oracle_and_read_and_ends_at_true_eof() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0);
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);
    let total = geo.total_frames();

    let expected = oracle_read(&h, &geo, 0, total as u32, 1);
    assert_eq!(expected.len(), total as usize * FRAME_STRIDE as usize);

    let (actual, visited, ok) = handle_read(h.asset, 0, total as u32, 1, ReadHint::Scan);
    assert_eq!(visited as u64, total);
    assert_eq!(actual, expected);

    // One frame past the end: `frame_count == 0` with the reader still ok. Contrast
    // `unlanded_fill_sets_not_ok_distinct_from_eof`, where the reader is not ok.
    let ptr = deluge_sample_reader_open(h.asset, total, 1, ReadHint::Cached);
    // SAFETY: `ptr` is live.
    let w = unsafe { deluge_sample_reader_window(ptr) };
    assert!(w.frames.is_null());
    assert_eq!(w.frame_count, 0);
    // SAFETY: `ptr` is live.
    assert!(
        unsafe { deluge_sample_reader_ok(ptr) },
        "true end-of-audio must leave reader_ok() true"
    );
    // SAFETY: `ptr` is live, not used again after this call.
    unsafe { deluge_sample_reader_close(ptr) };
    assert!(ok);

    let (via, via_frames) = via_read(h.asset, 0, total as u32);
    assert_eq!(via_frames as u64, total);
    assert_eq!(via, expected);
}

#[test]
fn random_seek_sequence_matches_oracle_at_every_stop() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0);
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);
    let total = geo.total_frames();

    // A fixed hop sequence crossing boundaries in both directions and visiting the end, with
    // `seek` between stops.
    let stops: [u64; 8] = [37, 402, 5, 170, 511, 0, 256, 340];
    const PER_STOP: u32 = 6;

    let ptr = deluge_sample_reader_open(h.asset, 0, 1, ReadHint::Cached);
    for &frame in &stops {
        // SAFETY: `ptr` is live.
        unsafe { deluge_sample_reader_seek(ptr, frame) };
        let want = PER_STOP.min((total - frame) as u32);
        let expected = oracle_read(&h, &geo, frame, want, 1);

        let mut collected = Vec::new();
        let mut remaining = want;
        while remaining > 0 {
            // SAFETY: `ptr` is live.
            let w = unsafe { deluge_sample_reader_window(ptr) };
            if w.frame_count == 0 {
                break;
            }
            let n = w.frame_count.min(remaining);
            let base = w.frames as *const u8;
            for b in 0..(n * FRAME_STRIDE) {
                // SAFETY: `window` guarantees `n * FRAME_STRIDE` bytes forward from `base`.
                collected.push(unsafe { *base.add(b as usize) });
            }
            // SAFETY: `ptr` is live.
            unsafe { deluge_sample_reader_advance(ptr, n) };
            remaining -= n;
        }
        assert_eq!(
            collected, expected,
            "seek to frame {frame} then reading {want} frames diverged from the oracle"
        );
    }
    // SAFETY: `ptr` is live.
    assert!(unsafe { deluge_sample_reader_ok(ptr) });
    // SAFETY: `ptr` is live, not used again.
    unsafe { deluge_sample_reader_close(ptr) };
}

/// A fill that never lands, as opposed to EOF: the cluster is never constructed and the fill
/// request is never answered, so `reader_ok()` must become false.
#[test]
fn unlanded_fill_sets_not_ok_distinct_from_eof() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(1, 0); // one cluster of audio
    let h = ChunkHarness::new(ctx, 0); // never prefilled

    let ptr = deluge_sample_reader_open(h.asset, 0, 1, ReadHint::Cached);
    // SAFETY: `ptr` is live.
    assert!(
        unsafe { deluge_sample_reader_ok(ptr) },
        "open itself must still succeed (geometry resolves)"
    );
    // SAFETY: `ptr` is live.
    let w = unsafe { deluge_sample_reader_window(ptr) };
    assert!(w.frames.is_null());
    assert_eq!(w.frame_count, 0);
    // SAFETY: `ptr` is live.
    assert!(
        !unsafe { deluge_sample_reader_ok(ptr) },
        "a fill that never lands must clear reader_ok() -- distinct from true EOF, which leaves it true"
    );
    // SAFETY: `ptr` is live, not used again.
    unsafe { deluge_sample_reader_close(ptr) };
}

// Non-vacuity: shifting the oracle by what a wrong reader offset or stride would produce must make
// the same comparison fail.

#[test]
fn non_vacuity_an_off_by_one_start_frame_is_detected() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0);
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);

    let (start, count) = (165u64, 20u32); // the forward straddle range
    let (actual, _, _) = handle_read(h.asset, start, count, 1, ReadHint::Cached);
    let expected = oracle_read(&h, &geo, start, count, 1);
    assert_eq!(
        actual, expected,
        "precondition: the real (unperturbed) comparison must agree before perturbing"
    );

    // Shift the oracle one frame, as a reader off by one frame would be.
    let mutated = oracle_read(&h, &geo, start + 1, count, 1);
    assert_ne!(
        actual, mutated,
        "non-vacuity FAILED: a one-frame-offset oracle mutation was not detected -- the comparison \
         has no teeth"
    );
}

#[test]
fn non_vacuity_an_off_by_one_stride_is_detected() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0);
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);

    let (start, count) = (10u64, 20u32); // within one cluster, isolating the stride
    let (actual, _, _) = handle_read(h.asset, start, count, 1, ReadHint::Cached);
    let expected = oracle_read(&h, &geo, start, count, 1);
    assert_eq!(
        actual, expected,
        "precondition: the real comparison must agree before perturbing"
    );

    // Change the oracle's stride by one byte, as a reader with the wrong frame stride would.
    let mutated_geo = MapGeo {
        byte_depth: geo.byte_depth + 1,
        ..geo
    };
    let mutated = oracle_read(&h, &mutated_geo, start, count, 1);
    assert_ne!(
        actual, mutated,
        "non-vacuity FAILED: a one-byte stride mutation was not detected -- the comparison has no \
         teeth"
    );
}
