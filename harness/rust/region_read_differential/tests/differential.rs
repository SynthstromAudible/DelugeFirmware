//! The reader differential: drives `deluge_sample_reader`'s
//! `open`/`window`/`advance`/`deluge_sample_read` over REAL, constructed streamed chunks (a real
//! `deluge_resource` manager, a synthetic sample whose converted cluster bytes are KNOWN) and asserts
//! the bytes it returns are BYTE-IDENTICAL to a direct read of each chunk's own resident payload
//! buffer (this file's own oracle, [`oracle_frame`]) at the location this crate's independently
//! reimplemented frame -> (cluster, byte-offset) arithmetic ([`mapping`]) computes.
//!
//! ## Why this is a genuine, non-vacuous cross-check
//!
//! [`mapping`] (this crate's `src/mapping.rs`) is an INDEPENDENT reimplementation of the
//! frame -> (cluster, byte-offset) arithmetic, kept deliberately separate from
//! `deluge_sample_reader::reader`'s own (private) `locate`/`Geometry` — see that module's own doc.
//! [`oracle_frame`] fetches its bytes straight from a chunk's own resident payload buffer (through
//! `deluge_sample_fill::chunk::payload`), addressed using this crate's OWN
//! mapping — never through the reader under test. The reader resolves clusters and offsets through
//! its OWN (different) code path (`Reader::window`'s `locate`/`acquire_and_fill`/the self-pin). If
//! either side's arithmetic — or the reader's straddle/stitch handling — diverges, the byte
//! comparison below catches it directly, not a hand-derived approximation of it.
//!
//! Every cluster's trailing 7-byte slack (the straddle mechanism both the reader and this file's
//! oracle rely on) is published by the REAL `deluge_sample_fill::native_finish` — the SAME stitch
//! (`deluge_sample_convert::stitch_boundaries`) production fills use — so a straddling frame's oracle
//! bytes are the real stitched bytes, not hand-seeded ones (see [`ChunkHarness::new`]).
//!
//! **Note.** [`oracle_frame`] reads the payload pointer directly — `cluster_size + 7` bytes,
//! through the same `deluge_sample_fill::chunk::payload` accessor the reader itself uses — with no
//! C++ involved. That accessor is equivalent to the streamed chunk's `payload_with_trailing_slack()`
//! semantics: "the payload pointer, `cluster_size + 7` bytes".
//!
//! ## The synthetic sample
//!
//! Every case builds a fresh [`ChunkHarness`]: a real `deluge_resource` manager (slab-backed, the
//! same backing kind production streaming clusters use) over a real heap, with `N` real streamed
//! chunks constructed via `deluge_resource_request` (payload is never the same address as backing —
//! `deluge_sample_reader`'s own tests flag this too; payload is always reached through the real
//! `deluge_sample_fill::chunk::payload` accessor). Each cluster's own `cluster_size` bytes are seeded
//! with a deterministic, per-cluster-index ramp (`region_read_differential::ramp`), then EVERY
//! cluster is `native_finish`ed, in increasing index order, so the REAL stitch
//! (`deluge_sample_convert::stitch_boundaries`, via `deluge_sample_fill::native_finish`) publishes
//! each cluster's own head into its predecessor's trailing 7-byte slack — the exact mechanism
//! `Reader::window`'s own straddle handling relies on (see its doc). Every geometry here uses
//! `RawDataFormat::Native` (0), so the convert step is a no-op and the seeded ramp survives into the
//! resident payload unchanged, and `audio_data_start_bytes == 0`, so [`mapping::Geometry::total_frames`]'s
//! simple "frame's whole span fits in `[0, audio_data_length_bytes)`" EOF rule holds exactly (see its
//! own doc). All `N` clusters are pre-filled before any reader is opened, so `deluge_efatfs_read_at`
//! (this file's own stub, always failing) is never actually exercised except by the ONE dedicated
//! forced-failure case, which deliberately leaves its cluster unconstructed.
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

// -- Single-threaded-per-test critical section: `deluge_resource`'s `sync::Masked` calls these
// three C-ABI symbols; every test here is a plain, single-threaded exercise (never the audio-ISR
// context), so inert no-op stand-ins are semantically correct. Same stand-ins
// `region_fill_differential`'s own test files use. --
#[unsafe(no_mangle)]
extern "C" fn ENTER_CRITICAL_SECTION() {}
#[unsafe(no_mangle)]
extern "C" fn EXIT_CRITICAL_SECTION() {}
#[unsafe(no_mangle)]
extern "C" fn deluge_in_interrupt() -> bool {
    false
}
// `deluge_resource` asks two more things of its host since 2026-08-16: whether it is standing on the
// audio render context (to skip its critical section there -- never true here, so it always masks), and
// the audio sample timer for its loader-latency stats (instrumentation only; nothing here reads them).
#[unsafe(no_mangle)]
extern "C" fn deluge_in_audio_context() -> bool {
    false
}
#[unsafe(no_mangle)]
extern "C" fn deluge_debug_now_frames() -> u32 {
    0
}

/// `deluge_sample_reader::reader::Reader`'s synchronous card-read primitive
/// (`include/libdeluge/streaming_fill.h`). Unconditionally fails, without touching `dst` — this
/// harness pre-fills and `native_finish`es every cluster it constructs BEFORE any reader is opened
/// (see the module doc), so `acquire_leased` is always a cache hit and this stub is never reached
/// except by [`forced_read_failure_sets_not_ok_distinct_from_eof`], which deliberately leaves its
/// one cluster unconstructed so the reader must fall through to a real fill attempt — exactly the
/// case this stub exists to fail.
#[unsafe(no_mangle)]
extern "C" fn deluge_efatfs_read_at(
    _handle: u32,
    _byte_offset: u32,
    _dst: *mut c_void,
    _count: u32,
    _out_read: *mut u32,
) -> bool {
    false
}

/// No async streaming-fill task in this differential harness, so `Reader::acquire_and_fill` takes
/// its synchronous `fill_now` route (through `deluge_efatfs_read_at` above) — the same path this
/// harness has always exercised. `deluge_streaming_fill_chunk_blocking` below is the async-BSP
/// alternative, never reached here (this returns false); both must exist for the binary to link.
#[unsafe(no_mangle)]
extern "C" fn deluge_streaming_async_active() -> bool {
    false
}

#[unsafe(no_mangle)]
extern "C" fn deluge_streaming_fill_chunk_blocking(_chunk_backing: *mut c_void) -> bool {
    false
}

/// The one process-wide resource manager pointer `deluge_streaming_resource_manager` (just below)
/// hands back to `deluge_sample_fill::native_finish` (called by [`ChunkHarness::new`] to seed each
/// cluster's real stitched trailing slack) — the same test-local plumbing
/// `region_fill_differential`'s `tests/native_finish_glue.rs` uses for its own `ACTIVE_MANAGER`.
static ACTIVE_MANAGER: AtomicPtr<c_void> = AtomicPtr::new(ptr::null_mut());

#[unsafe(no_mangle)]
extern "C" fn deluge_streaming_resource_manager() -> *mut c_void {
    ACTIVE_MANAGER.load(Ordering::Relaxed)
}

/// Placement-construct callback matching `deluge_resource::ConstructFn`'s exact signature
/// (`dest: *mut u8`), forwarding to the real `deluge_sample_fill::chunk::deluge_streaming_chunk_construct`
/// — the SAME callback the resource manager invokes for a real streamed SAMPLE chunk in production
/// (`chunk_residency.cpp`). A thin wrapper is needed only because that function's own `dest`
/// parameter is typed `*mut c_void` (its C-ABI declared shape) while `ConstructFn` requires
/// `*mut u8` — the two are ABI-identical (both a plain data pointer), just declared with different
/// pointee types on each side of the boundary, so Rust's function-pointer typing (unlike C's) won't
/// let one satisfy the other directly.
unsafe extern "C" fn construct_streamed_chunk(
    ctx: *mut c_void,
    owner: *mut c_void,
    index: u32,
    dest: *mut u8,
) {
    // SAFETY: forwards `dest` unchanged (only its declared pointee type differs) to the real
    // construct function, under the same manager construct-contract this function's own caller
    // (`deluge_resource_request`, via `deluge_resource_set_construct`) upholds.
    unsafe {
        deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
            ctx,
            owner,
            index,
            dest as *mut c_void,
        );
    }
}

/// Serializes every test in this file — mirrors `deluge_sample_reader`'s own `host_streaming_stubs::TEST_LOCK`:
/// `deluge_sample_fill`'s per-asset `FILL_CONTEXTS` table is a GLOBAL static keyed by bare asset id
/// (each fresh manager hands out ids starting from 0), and this file's own `ACTIVE_MANAGER` is a
/// single (non-thread-local) global too — two tests running concurrently (`cargo test`'s default)
/// could otherwise collide on either. Every test takes this lock for its whole run.
static TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// 512 bytes (`2^9`) — realistic minimum cluster size (>= one 512-byte sector), matching
/// `deluge_sample_reader`'s own `reader::tests::window_tests`.
const CLUSTER_SIZE: u32 = 512;
const CLUSTER_MAGNITUDE: u32 = 9;

/// 24-bit mono: `byte_depth = 3, num_channels = 1` -> `frame_stride = 3`, which does NOT divide
/// `CLUSTER_SIZE` (`512 % 3 == 2`) — the real case `sample_recorder.cpp` sets, and the stride the
/// straddle/scan cases must use: this is where the boundary arithmetic matters.
const BYTE_DEPTH: u8 = 3;
const NUM_CHANNELS: u8 = 1;
const FRAME_STRIDE: u32 = BYTE_DEPTH as u32 * NUM_CHANNELS as u32;

/// `num_full_clusters` full (`CLUSTER_SIZE`-byte) clusters, plus an optional trailing short cluster
/// of `short_last_cluster_bytes` real bytes (`0` for none — every cluster is full). The harness must
/// then pre-fill `num_full_clusters + (short_last_cluster_bytes > 0) as u32` clusters total.
fn fill_context(num_full_clusters: u32, short_last_cluster_bytes: u32) -> FillContext {
    let audio_data_length_bytes =
        num_full_clusters as u64 * CLUSTER_SIZE as u64 + short_last_cluster_bytes as u64;
    FillContext {
        efatfs_handle: 0,
        audio_data_start_pos_bytes: 0,
        audio_data_length_bytes,
        first_cluster_index_with_no_audio_data: -1, // Native format never reads this (see module doc)
        cluster_size: CLUSTER_SIZE,
        cluster_size_magnitude: CLUSTER_MAGNITUDE,
        raw_data_format: 0, // Native -- convert is a no-op, the seeded ramp survives verbatim
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

/// A real manager + real, constructed streamed chunks over a real heap — see the module doc.
struct ChunkHarness {
    asset: u32,
    /// Cluster index -> its resident backing pointer. Every entry is held under a lease taken by
    /// this harness's own initial `deluge_resource_request` call and NEVER released — the chunk
    /// stays resident (and un-evictable) for this harness's whole life, regardless of what a reader
    /// under test later acquires/releases on top (mirrors
    /// `region_fill_differential::tests::native_finish_glue::ChunkHarness`'s identical convention).
    backings: Vec<*mut u8>,
    /// Kept alive for the harness's whole life; never read directly.
    _arena: Vec<u128>,
}

impl ChunkHarness {
    /// `ctx.audio_data_length_bytes` determines `num_clusters` implicitly (the caller passes the
    /// SAME `ctx` this constructs from); `prefill` is the number of clusters to actually seed +
    /// `native_finish` (0 for the forced-failure case, which wants a fresh, never-filled chunk that
    /// the reader itself must construct on demand).
    fn new(ctx: FillContext, prefill: u32) -> Self {
        let words = (4 * 1024 * 1024usize).div_ceil(16); // 4 MiB arena -- ample for a handful of clusters
        let mut arena: Vec<u128> = vec![0u128; words];
        let base = arena.as_mut_ptr() as *mut u8;
        // SAFETY: `arena` is a live, 16-aligned, `words * 16`-byte allocation for as long as `self`
        // (and thus `arena`) is alive.
        let heap = unsafe { deluge_alloc::deluge_heap_create(base, words * 16) };
        assert!(!heap.is_null());

        // The Rust-owned streamed-chunk payload offset (U4d) — the real, compiler-computed value
        // reported over its own C-ABI (`deluge_streamed_chunk_payload_offset`), not hand-derived.
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
        // SAFETY: `handle` is the live manager just created; stored for `deluge_streaming_resource_manager`
        // to hand back — must happen before any reader call or `native_finish` call reads it.
        ACTIVE_MANAGER.store(handle as *mut c_void, Ordering::Relaxed);
        deluge_streaming_set_fill_context(ptr::null_mut(), asset, ctx);

        let mut backings = Vec::with_capacity(prefill as usize);
        for index in 0..prefill {
            // SAFETY: `handle`/`asset` are live/valid; `backing_size` matches the slab's own slot
            // size (BACKING_SLAB ignores the `size` argument regardless -- see `cluster.h`'s
            // `kSlabBackedSizeIgnored` doc).
            let backing = unsafe { deluge_resource_request(handle, asset, index, backing_size) };
            assert!(!backing.is_null(), "request failed for cluster {index}");
            // SAFETY: `backing` was just resident-constructed above; its payload is `CLUSTER_SIZE`
            // bytes, reached through the REAL accessor (never payload == backing -- the SR2d-4
            // lesson).
            let payload = unsafe { deluge_sample_fill::chunk::payload(backing as *mut c_void) };
            let seed = ramp(index, CLUSTER_SIZE as usize);
            // SAFETY: `payload` is `CLUSTER_SIZE` bytes, exclusively held here (nothing else
            // touches it until `native_finish` below); `seed` is exactly `CLUSTER_SIZE` bytes.
            unsafe { core::ptr::copy_nonoverlapping(seed.as_ptr(), payload, seed.len()) };
            backings.push(backing);
        }
        // Finish every cluster in increasing order -- Native format's convert step is a no-op, so
        // the seeded ramp survives; the REAL stitch still runs, publishing each cluster's own head
        // into its predecessor's trailing slack once the predecessor is already resident+ready
        // (see the module doc).
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

/// The differential's own oracle: `frame_bytes` bytes of a frame's interleaved samples, starting at
/// within-cluster byte offset `byte_offset`, read straight from the chunk's resident payload buffer
/// (`deluge_sample_fill::chunk::payload`) — valid for any `byte_offset + frame_bytes <= cluster_size + 7`
/// (the straddle case; every real chunk this harness constructs is backed by exactly that many bytes
/// past the payload base — see [`ChunkHarness::new`]'s `backing_size`). See the module doc's U4d note
/// for why this reads the payload pointer directly rather than through a C++
/// `payload_with_trailing_slack()` call: the two were always the same bytes.
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
    // SAFETY: `payload` is backed by `cluster_size + 7` valid bytes (this harness's own
    // `backing_size`); `byte_offset + frame_bytes` stays within that span for every case this file
    // drives (frame_bytes <= 4, byte_offset < cluster_size).
    unsafe {
        core::ptr::copy_nonoverlapping(
            payload.add(byte_offset as usize),
            out.as_mut_ptr(),
            frame_bytes as usize,
        )
    };
    out
}

/// The independent oracle read: walk `num_frames` frames from `start_frame` in `direction`,
/// stopping at [`MapGeo::total_frames`]'s own end-of-audio boundary, concatenating each frame's
/// [`oracle_frame`] bytes in VISITED order (so a backward walk's output is naturally comparable to
/// the reader's own backward-visited byte order -- see [`handle_read`]'s doc).
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

/// Drive the reader's zero-copy handle (`open` -> `window`/`advance` -> `close`), collecting every
/// visited frame's bytes in VISITED order: for a forward reader that's increasing frame order (the
/// window's pointer plus increasing byte offsets); for a backward reader (`direction == -1`),
/// `window()`'s own contract has the pointer sit at the CURRENT (highest) frame of the run with
/// `frame_count` frames extending toward DECREASING addresses (see
/// `deluge_sample_reader::reader::Reader::window`'s own doc) -- so this walks `k` from `0..n` with a
/// NEGATIVE per-frame stride when `direction < 0`, landing on the same decreasing-frame-index
/// sequence [`oracle_read`] produces for the same `direction`.
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
            // SAFETY: `base` is `window`'s own pinned, resident payload pointer; per its contract
            // `n <= frame_count` whole (possibly straddling) frames of `FRAME_STRIDE` bytes are
            // valid starting at `base` and continuing in the reader's own `direction` -- exactly
            // the span `base + step*FRAME_STRIDE .. + FRAME_STRIDE` this loop reads.
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

/// Drive the stateless `deluge_sample_read` copy-out (always forward, `ReadHint::Cached` — see the
/// header's own contract). Returns `(bytes_written, frames_written)`.
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

// =====================================================================================================
// The battery: within-cluster, straddle (fwd/bwd), last-cluster short read, reverse full scan, whole
// forward scan, random-seek sequence -- every one asserting handle_read == oracle_read, and (forward
// cases) via_read == oracle_read too, so all three agree.
// =====================================================================================================

#[test]
fn within_one_cluster_forward_matches_oracle_and_read() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0); // 3 full clusters, 1536 bytes, total_frames = 512
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);

    let (start, count) = (10u64, 20u32); // well inside cluster 0 -- no boundary in play
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

    // Frame 165..185: crosses cluster 0/1's boundary (the straddling frame sits at frame 170, byte
    // 510) and continues well past it into cluster 1's own plain payload -- the boundary re-pin
    // near cluster ends with a non-dividing stride, and the re-pin across the boundary within one
    // continuous read.
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

    // Start at frame 185 (cluster 1, offset 43) and read 25 frames backward -- lands on frame 161
    // (cluster 0, offset 483), crossing the SAME boundary as the forward case above, in reverse.
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
    // 3 full clusters + a short 4th cluster (100 real bytes) -> audio_data_length_bytes = 1636,
    // total_frames = 545 (1636 / 3, floor -- the trailing 1-byte partial frame is NOT valid).
    let ctx = fill_context(3, 100);
    let h = ChunkHarness::new(ctx, 4);
    let geo = map_geometry(&ctx);
    assert_eq!(geo.total_frames(), 545);

    // Ask for 20 frames starting at 540 -- only 5 real frames remain (540..545); must come back
    // short, not padded, and reader_ok() must stay true (this is genuine end-of-audio, not a
    // failure).
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

    // Distinct-from-failure check: reading one MORE frame past the end must report EOF
    // (frame_count == 0) while reader_ok() stays TRUE -- contrast with
    // `forced_read_failure_sets_not_ok_distinct_from_eof` below, where frame_count == 0 too but
    // reader_ok() is false.
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

    // A deterministic "random" hop sequence (not sequential, crosses boundaries in both
    // directions, includes a couple of near-the-end positions) -- one reader, `seek` between
    // stops, `window`/`advance` after each.
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
                // SAFETY: `base` is `window`'s own pinned payload, valid for `n * FRAME_STRIDE`
                // bytes forward from here per its own contract.
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

/// A case that forces a read failure (rather than EOF): a cluster the harness never constructs, so
/// the reader must fall through to `Reader::acquire_and_fill`'s fresh-`request` + `fill_now` path,
/// which calls this file's own `deluge_efatfs_read_at` stub -- unconditionally `false`. Asserts
/// `reader_ok()` -> false, DISTINCT from `frame_count == 0 && reader_ok() == true`
/// (`whole_sample_forward_scan_matches_oracle_and_read_and_ends_at_true_eof`'s own EOF check).
#[test]
fn forced_read_failure_sets_not_ok_distinct_from_eof() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(1, 0); // one full cluster's worth of real audio -- but never prefilled
    let h = ChunkHarness::new(ctx, 0); // prefill = 0: cluster 0 is registered geometry-wise, never resident

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
        "a forced read failure must clear reader_ok() -- distinct from true EOF, which leaves it true"
    );
    // SAFETY: `ptr` is live, not used again.
    unsafe { deluge_sample_reader_close(ptr) };
}

// =====================================================================================================
// NON-VACUITY: a passing comparison above proves nothing unless a
// REAL divergence is provably caught. Perturb the oracle's own frame position/geometry by exactly
// the amounts a genuinely wrong reader offset/stride would produce, and confirm the SAME comparison
// this file uses throughout now FAILS.
// =====================================================================================================

#[test]
fn non_vacuity_an_off_by_one_start_frame_is_detected() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let ctx = fill_context(3, 0);
    let h = ChunkHarness::new(ctx, 3);
    let geo = map_geometry(&ctx);

    let (start, count) = (165u64, 20u32); // the same straddling range as the forward straddle case
    let (actual, _, _) = handle_read(h.asset, start, count, 1, ReadHint::Cached);
    let expected = oracle_read(&h, &geo, start, count, 1);
    assert_eq!(
        actual, expected,
        "precondition: the real (unperturbed) comparison must agree before perturbing"
    );

    // Perturb the OFFSET the oracle is read from by one frame -- standing in for a hypothetical
    // reader bug that resolved `current_frame` one frame off (this class of off-by-one is exactly
    // where boundary bugs tend to hide).
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

    let (start, count) = (10u64, 20u32); // a plain within-cluster range -- isolates the stride mutation
    let (actual, _, _) = handle_read(h.asset, start, count, 1, ReadHint::Cached);
    let expected = oracle_read(&h, &geo, start, count, 1);
    assert_eq!(
        actual, expected,
        "precondition: the real comparison must agree before perturbing"
    );

    // Perturb the STRIDE (byte_depth) the oracle maps frames with by one byte -- standing in for a
    // hypothetical reader bug that resolved the wrong frame stride from geometry.
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
