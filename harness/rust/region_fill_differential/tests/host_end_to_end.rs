//! Host end-to-end: drives the full read -> convert -> stitch -> publish pipeline over a
//! REAL `deluge_resource` manager (pure Rust, no C++), reading a SYNTHETIC "file" (an in-memory byte
//! buffer) instead of `efatfs_host_shim::read_at` — see below for why, and what's covered elsewhere
//! instead.
//!
//! ## Why not `efatfs_host_shim::read_at` / real `ProdOps` (a deliberate, documented deviation)
//!
//! `efatfs_host_shim.rs` is gated `#![cfg(feature = "host_app")]`, and `deluge-bsp-rust`'s `host_app`
//! Cargo feature does far more than pull in the storage stack this test would actually touch: its
//! `build.rs` archives the FULL host-built C++ `deluge_app` object closure and force-roots the link at
//! `deluge_app_init` (`-Wl,-u,deluge_app_init`) — which then needs the WHOLE boot-time provider surface
//! satisfied (board/display/USB/OLED init, ~20+ symbols), not just the storage path. Verified
//! empirically: enabling `--features host_app,async_streaming_loader` on a plain `deluge_resource`-
//! only host test (normally built with neither feature) fails to link with 20+ undefined symbols
//! (`deluge_control_enable_oled`, `deluge_board_init_early`, `openUSBHost`,
//! `deluge_board_unlock_data_cache`, ...) — exactly the "cc-compiling a large C++ closure" case the
//! brief calls out as the trigger to fall back to a host adapter instead. (`ProdOps`'s two
//! `StreamedChunk` accessors — `deluge_streaming_chunk_payload`/`_set_loaded` — used to be a second
//! reason `ProdOps`/`fill_once` were out of reach here, back when they were C++-defined in
//! `async_fill.cpp`; the streamed chunk's storage now lives in `deluge_sample_fill::chunk` (Rust), so
//! that reason no longer applies — the `host_app` closure above remains the one still standing.)
//!
//! So this test takes the "host adapter" path the brief explicitly sanctions, extended one step
//! further: it also stands in a plain in-memory buffer for the SD-backed file `efatfs_host_shim::
//! read_at` would otherwise read, rather than pulling in the whole `host_app`-gated storage stack
//! (`crate::sd` / `deluge-bsp` / `embedded-fatfs`) for a read whose CONTRACT (fill `dst` from
//! `byte_offset`, report success) is trivial. [`synthetic_read_at`] below matches
//! `efatfs_host_shim::read_at`'s own documented contract exactly ("Read `dst.len()` bytes from
//! absolute `byte_offset` of the file behind `handle`. Returns `true` iff the full buffer was
//! filled."). This test's job is the fill ORCHESTRATION (read -> convert -> stitch -> publish), not
//! re-proving the FAT read path — that path (and the per-asset fill-context REGISTRATION table this
//! test also skips, building a [`fill_logic::FillGeometry`] by hand instead) already has its own
//! dedicated host coverage (`tests/streaming_fill_context_host.rs`).
//!
//! Everything else here is real: a genuine `deluge_resource` manager (built over a real heap via its
//! C-ABI), and the real `fill_logic::begin`/`finish_convert_stitch`. Convert-state
//! itself is plain local `fill_logic::ConvertState` values rather than the chunk's own real store
//! (the live store now lives on the streamed chunk's convert-state accessors, in Rust,
//! but this test still keeps its own plain local values, since driving the real store
//! would mean pulling in `ProdOps`/`fill_once` from `deluge-bsp-rust`, out of reach here for the
//! `host_app`-closure reason above; `tests/native_finish_glue.rs` is the gate that drives the real
//! store instead — see its own module doc). A never-`finish`ed chunk's state is simply zeroed
//! `ConvertState::default()`, matching what the real store would report for a chunk nothing has ever
//! written. Only the orchestration `ProdOps::begin`/`finish` themselves perform (resolve geometry,
//! drive the read, gather neighbours, call `finish_convert_stitch`, publish) is reproduced by hand
//! here, mirroring `streaming_loader.rs`'s `prod` module's own logic (not `fill_once`/`ProdOps`
//! directly, which live behind the `host_app` closure).
#![cfg(not(target_os = "none"))]

use core::ffi::c_void;
use core::ptr;

use deluge_resource::{
    BACKING_HEAP, DelugeResource, deluge_resource_create, deluge_resource_define_asset,
    deluge_resource_mark_ready, deluge_resource_release, deluge_resource_request,
    deluge_resource_set_construct, deluge_resource_try_acquire,
};

use deluge_sample_fill::fill_logic;
use fill_logic::{FillGeometry, NeighbourView, begin, finish_convert_stitch};

// Single-threaded-per-test critical section: `deluge_resource`'s `sync::Masked` calls these three
// C-ABI symbols; this test never models the audio-ISR context, so inert no-op stand-ins are
// semantically correct (nothing here can race with itself — a single thread, no concurrent "ISR").
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

/// One sector (512 bytes) — the smallest realistic `Cluster::size` (`begin`'s sector math right-shifts
/// by 9, so anything smaller than a sector rounds its default full-cluster read down to zero sectors;
/// the fill-differential's own `simd_geo` deliberately uses a SMALLER 128-byte cluster for tighter
/// SIMD/tail coverage, which is fine there since that test never calls `begin` — but this end-to-end
/// test exercises `begin` for real, so it needs a realistic size).
const CLUSTER_SIZE: u32 = 512;
const CLUSTER_MAGNITUDE: u32 = 9; // 2^9 = 512
const PAYLOAD_LEN: usize = CLUSTER_SIZE as usize + 7;

/// `deluge_resource_request`'s async `construct` callback: no I/O, just makes the chunk's backing
/// bytes deterministic (not uninitialized heap garbage) before the synthetic read/stitch fill in the
/// meaningful bytes. A requestable asset needs SOME `construct` callback to be requestable at all.
unsafe extern "C" fn mock_construct(
    _ctx: *mut c_void,
    _owner: *mut c_void,
    _index: u32,
    dest: *mut u8,
) {
    // SAFETY: `dest` comes from the manager's just-allocated `PAYLOAD_LEN`-byte backing (the fixed
    // `size` this test always passes to `deluge_resource_request`).
    unsafe { core::ptr::write_bytes(dest, 0xEE, PAYLOAD_LEN) };
}

/// A manager over a throwaway 16-aligned test heap, plus one requestable test asset.
struct TestManager {
    handle: *mut DelugeResource,
    asset: u32,
    _buf: Vec<u128>,
}

impl TestManager {
    fn new(chunk_cap: usize) -> Self {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf = vec![0u128; words];
        // SAFETY: `buf` is a live, 16-aligned, `words * 16`-byte arena for as long as `self` (and thus
        // `buf`) is alive — `buf` is moved into the returned `TestManager`, never freed while `handle`
        // is in use.
        let heap = unsafe { fs_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        // SAFETY: `heap` is the live handle just created above.
        let handle = unsafe { deluge_resource_create(heap, 4, chunk_cap) };
        assert!(!handle.is_null());
        // SAFETY: `handle` is live for the duration of this call.
        let asset = unsafe {
            deluge_resource_define_asset(
                handle,
                ptr::null_mut(),
                None,
                None,
                ptr::null_mut(),
                1,
                BACKING_HEAP,
            )
        };
        // SAFETY: `handle`/`asset` are live and valid.
        unsafe { deluge_resource_set_construct(handle, asset, Some(mock_construct)) };
        TestManager {
            handle,
            asset,
            _buf: buf,
        }
    }

    /// Reserve chunk `index`'s backing under a hard lease, `PAYLOAD_LEN` bytes — the async/`request`
    /// (no-I/O) path the real production `begin` uses too (an external loader fills the data after).
    fn request(&self, index: u32) -> *mut u8 {
        // SAFETY: `self.handle`/`self.asset` are live and valid.
        unsafe { deluge_resource_request(self.handle, self.asset, index, PAYLOAD_LEN) }
    }

    fn release(&self, chunk: *mut u8) {
        // SAFETY: `chunk` was returned by `request` above and is still resident.
        unsafe { deluge_resource_release(self.handle, chunk) };
    }

    fn mark_ready(&self, chunk: *mut u8) {
        // SAFETY: same as `release`.
        unsafe { deluge_resource_mark_ready(self.handle, chunk) };
    }

    /// The RT-safe "resident and ready" probe — mirrors `ProdOps`'s own neighbour-gather check
    /// (`deluge_resource_try_acquire`) and is what this test uses to assert "mark-ready is reachable"
    /// concretely: null before `mark_ready`, non-null (and identical to the leased pointer) after.
    fn try_acquire(&self, index: u32) -> *mut u8 {
        // SAFETY: `self.handle`/`self.asset` are live and valid.
        unsafe { deluge_resource_try_acquire(self.handle, self.asset, index) }
    }
}

/// Stand-in for `efatfs_host_shim::read_at` — SAME documented contract ("Read `dst.len()` bytes from
/// absolute `byte_offset` of the file behind `handle`. Returns `true` iff the full buffer was
/// filled."), over a plain in-memory byte slice instead of a mounted FAT file. See the module doc for
/// why.
fn synthetic_read_at(file: &[u8], byte_offset: u32, dst: &mut [u8]) -> bool {
    let start = byte_offset as usize;
    let Some(end) = start.checked_add(dst.len()) else {
        return false;
    };
    let Some(src) = file.get(start..end) else {
        return false;
    };
    dst.copy_from_slice(src);
    true
}

fn geo() -> FillGeometry {
    FillGeometry {
        audio_data_start_pos_bytes: 3, // misaligned: 3 & 0b11 = 3 (nonzero) -> real boundary conversion
        audio_data_length_bytes: 100_000, // well past 3 clusters -> none of them is the last
        first_cluster_index_with_no_audio_data: 50,
        cluster_size: CLUSTER_SIZE,
        cluster_size_magnitude: CLUSTER_MAGNITUDE,
        raw_data_format: 2, // UNSIGNED_8
    }
}

/// A synthetic "file" spanning exactly 3 clusters' worth of raw data, with a distinguishable byte
/// pattern (so a wrong-offset read would be obviously wrong, even though nothing here asserts on it
/// directly — the C++-reference cross-check below is the real assertion).
fn synthetic_file() -> Vec<u8> {
    region_fill_differential::ramp(0xF11E, 3 * CLUSTER_SIZE as usize)
}

/// One cluster's worth of the read step: `begin` resolves where/how much (mirrors `begin_fill`'s
/// descriptor), the synthetic read fills the leading `cluster_size` bytes of `dest` (the trailing
/// 7-byte slack is never read — only ever written later by `stitch`/`construct`, matching real
/// `payload()` vs. `payload_with_trailing_slack()` semantics).
fn read_into(file: &[u8], geo: &FillGeometry, index: u32, dest: &mut [u8]) {
    let r = begin(index, geo);
    assert!(r.ok, "begin() geometry error for index {index}");
    let n = (r.num_sectors as usize) * 512;
    assert!(
        n <= dest.len(),
        "read would overflow the chunk's payload buffer"
    );
    let filled = synthetic_read_at(file, r.byte_offset, &mut dest[..n]);
    assert!(filled, "synthetic read failed for index {index}");
}

/// Full pipeline: request 3 chunks (prev, self, next) from a real manager, read each via the synthetic
/// file, run `finish_convert_stitch` on the middle one (mirroring `ProdOps::finish`'s neighbour-gather
/// + convert/stitch + sidecar write-back + publish), then assert:
///   - the resulting bytes match an INDEPENDENTLY computed expected converted+stitched output (the SAME
///     C++ reference the fill-differential test uses, over the SAME synthetic bytes) — tying this
///     end-to-end run back to the byte-exactness gate, not just re-asserting the wiring in isolation;
///   - mark-ready is reachable: `try_acquire` is null before `mark_ready` (a `request`ed-but-not-ready
///     chunk must stay invisible to the RT-safe path) and returns the SAME pointer after.
#[test]
fn read_convert_stitch_publish_matches_cpp_reference_and_mark_ready_is_reachable() {
    let mgr = TestManager::new(8);
    let geo = geo();
    let file = synthetic_file();

    let prev_ptr = mgr.request(0);
    let self_ptr = mgr.request(1);
    let next_ptr = mgr.request(2);
    assert!(!prev_ptr.is_null() && !self_ptr.is_null() && !next_ptr.is_null());

    // SAFETY: each pointer is the just-`request`ed, `PAYLOAD_LEN`-byte backing for its own chunk,
    // uniquely held here (no other alias exists) for the extent of the `read_into`/`finish_convert_stitch`
    // calls below.
    {
        let prev_buf = unsafe { core::slice::from_raw_parts_mut(prev_ptr, PAYLOAD_LEN) };
        read_into(&file, &geo, 0, prev_buf);
    }
    {
        let self_buf = unsafe { core::slice::from_raw_parts_mut(self_ptr, PAYLOAD_LEN) };
        read_into(&file, &geo, 1, self_buf);
    }
    {
        let next_buf = unsafe { core::slice::from_raw_parts_mut(next_ptr, PAYLOAD_LEN) };
        read_into(&file, &geo, 2, next_buf);
    }

    // Not yet ready: the RT-safe probe must report absent.
    assert!(
        mgr.try_acquire(1).is_null(),
        "try_acquire must be null before mark_ready (chunk is still just Loading)"
    );

    // -- ProdOps::finish's own orchestration, driven by hand (see the module doc): gather each
    // neighbour's convert-state (none of these three chunks has ever been `finish`ed before, so each
    // starts at the zeroed default — matching what the real `StreamedChunk` accessors would report
    // for a chunk nothing has ever written), run finish_convert_stitch, then publish. --
    let mut self_state = fill_logic::ConvertState::default();
    let mut prev_state = fill_logic::ConvertState::default();
    let mut next_state = fill_logic::ConvertState::default();

    {
        // SAFETY: same three pointers, still the sole aliases; each slice is exclusively borrowed for
        // exactly this call.
        let self_buf = unsafe { core::slice::from_raw_parts_mut(self_ptr, PAYLOAD_LEN) };
        let prev_buf = unsafe { core::slice::from_raw_parts_mut(prev_ptr, PAYLOAD_LEN) };
        let next_buf = unsafe { core::slice::from_raw_parts_mut(next_ptr, PAYLOAD_LEN) };
        let prev_head = prev_state.first_three_bytes;
        let next_head = next_state.first_three_bytes;

        finish_convert_stitch(
            self_buf,
            1,
            &geo,
            &mut self_state,
            Some(NeighbourView {
                payload: prev_buf,
                unconverted_head: &prev_head, // unread on the prev side
                start_converted: &mut prev_state.start_converted,
                end_converted: &mut prev_state.end_converted,
            }),
            Some(NeighbourView {
                payload: next_buf,
                unconverted_head: &next_head,
                start_converted: &mut next_state.start_converted,
                end_converted: &mut next_state.end_converted,
            }),
        );
    }

    mgr.release(prev_ptr);
    mgr.release(next_ptr);

    mgr.mark_ready(self_ptr);

    // -- Cross-check against the SAME C++ reference the fill-differential uses, over freshly re-read
    // clones of the SAME synthetic bytes — independent of the manager plumbing above; this is what
    // ties the end-to-end pipeline back to the byte-exactness gate. Both neighbours are fresh (never
    // independently `finish`ed) here too, so their `unconverted_head`/`end_converted` inputs are the
    // same zeroed defaults `self_state`/`prev_state`/`next_state` started from above. --
    let mut expect_self = vec![0u8; PAYLOAD_LEN];
    let mut expect_prev = vec![0u8; PAYLOAD_LEN];
    let mut expect_next = vec![0u8; PAYLOAD_LEN];
    read_into(&file, &geo, 0, &mut expect_prev);
    read_into(&file, &geo, 1, &mut expect_self);
    read_into(&file, &geo, 2, &mut expect_next);
    let mut expect_state = region_fill_differential::cpp_ref::ConvertState::default();
    let (mut ep_start_unused, mut ep_end) = (false, false);
    let (mut en_start, mut en_end_unused) = (false, false);
    region_fill_differential::cpp_ref::finish_fill_over_buffers(
        &mut expect_self,
        1,
        &region_fill_differential::cpp_ref::Geometry {
            audio_data_start_pos_bytes: geo.audio_data_start_pos_bytes,
            audio_data_length_bytes: geo.audio_data_length_bytes,
            first_cluster_index_with_no_audio_data: geo.first_cluster_index_with_no_audio_data,
            cluster_size: geo.cluster_size as usize,
            cluster_size_magnitude: geo.cluster_size_magnitude as usize,
            raw_data_format: geo.raw_data_format,
        },
        &mut expect_state,
        Some(region_fill_differential::cpp_ref::Neighbour {
            payload: &mut expect_prev,
            unconverted_head: &[0; 3],
            start_converted: &mut ep_start_unused,
            end_converted: &mut ep_end,
        }),
        Some(region_fill_differential::cpp_ref::Neighbour {
            payload: &mut expect_next,
            unconverted_head: &[0; 3],
            start_converted: &mut en_start,
            end_converted: &mut en_end_unused,
        }),
    );

    // SAFETY: `self_ptr` is still resident (leased since `request`, never released) and still the sole
    // borrow at this point (the earlier mutable borrows all ended at the close of their own blocks
    // above).
    let self_bytes_now = unsafe { core::slice::from_raw_parts(self_ptr, PAYLOAD_LEN) };
    assert_eq!(
        self_bytes_now,
        expect_self.as_slice(),
        "end-to-end self payload mismatch vs. the C++ reference"
    );
    assert_eq!(self_state.first_three_bytes, expect_state.first_three_bytes);
    assert_eq!(self_state.start_converted, expect_state.start_converted);
    assert_eq!(self_state.end_converted, expect_state.end_converted);

    // mark-ready is reachable: the RT-safe probe now reports the chunk resident + ready, returning the
    // SAME backing pointer `mark_ready` published.
    let ready = mgr.try_acquire(1);
    assert!(
        !ready.is_null(),
        "try_acquire must succeed once mark_ready has published the chunk"
    );
    assert_eq!(
        ready, self_ptr,
        "try_acquire must return the SAME backing pointer mark_ready published"
    );
}
