//! Drives `deluge_sample_fill::native_finish` over real streamed chunks in a real `deluge_resource`
//! manager: its `try_acquire` → payload accessor → convert-state accessor → stitch → write-back
//! chain, checked against the C++ reference.
//!
//! Two bugs lived in that chain, and this test fails if either returns:
//!
//! 1. **Double store.** Two fill paths kept convert state in separate stores, so a boundary stitch
//!    between chunks filled by different paths read an empty store. Both paths now go through
//!    `native_finish` and the chunk's own convert-state accessors.
//! 2. **Backing vs payload.** Neighbour payload slices were built from the chunk's backing pointer
//!    rather than its payload (`backing + payload offset`), so the stitch touched header bytes.
//!
//! # Harness
//!
//! `native_begin`/`native_finish` (feature `native_fill`) and the chunk storage are called
//! directly. The resource-manager symbols come from `deluge_resource`, the chunk accessors from
//! `deluge_sample_fill`. Only `deluge_streaming_resource_manager` is provided here, from an
//! `AtomicPtr` set by [`ChunkHarness::new`]. Chunks are built by the production construct callback,
//! `deluge_streaming_chunk_construct`.
//!
//! Not covered: `native_begin` (covered by `fill_logic`'s tests and `differential.rs`; payloads are
//! seeded directly as if read), the efatfs read (covered by `fs_differential`), and on-device
//! timing.
//!
//! # Why it catches both bugs
//!
//! - **Backing vs payload:** `CLUSTER_SIZE + 7` lies strictly between `size_of::<StreamedChunk>()`
//!   and the payload offset ([`geometry_and_layout_invariants_hold`]), so a slice taken from the
//!   backing lands on the header and the marked front guard, never the seeded payload.
//! - **Double store:** a first visit to a neighbour cannot tell the stores apart, because "already
//!   converted" is false either way. So prev is reloaded and finished again with self as its ready
//!   next neighbour; self's `start_converted` is now true, which sends the stitch down the "already
//!   converted" branch (recompute from self's captured `unconverted_head`). An empty second store
//!   would take the other branch and produce different bytes.
//!   [`neighbour_convert_state_is_read_back_through_the_shared_store`] covers the write side.
#![cfg(not(target_os = "none"))]

use core::ffi::c_void;
use core::ptr;
use core::sync::atomic::{AtomicPtr, Ordering};

use deluge_resource::{
    BACKING_HEAP, DelugeResource, deluge_resource_create, deluge_resource_define_asset,
    deluge_resource_request, deluge_resource_set_construct,
};
use deluge_sample_fill::FillContext;
use deluge_sample_fill::chunk::StreamedChunk;

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

/// The manager `deluge_streaming_resource_manager` returns to `native_begin`/`native_finish`.
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

/// Cluster size, chosen so `CLUSTER_SIZE + 7` lies strictly between `size_of::<StreamedChunk>()`
/// and the payload offset; see [`geometry_and_layout_invariants_hold`].
const CLUSTER_SIZE: u32 = 32;
const CLUSTER_MAGNITUDE: u32 = 5; // 2^5 = 32

/// A misaligned start, non-native `UNSIGNED_8` data, and enough audio that none of clusters 0-2 is
/// the last.
fn geo() -> FillContext {
    FillContext {
        efatfs_handle: 0, // unused: no read step
        audio_data_start_pos_bytes: 1,
        audio_data_length_bytes: 100_000,
        first_cluster_index_with_no_audio_data: 10,
        cluster_size: CLUSTER_SIZE,
        cluster_size_magnitude: CLUSTER_MAGNITUDE,
        raw_data_format: 2, // Unsigned8
        byte_depth: 1,      // Unsigned8
        num_channels: 1,
    }
}

fn cpp_geo() -> region_fill_differential::cpp_ref::Geometry {
    let g = geo();
    region_fill_differential::cpp_ref::Geometry {
        audio_data_start_pos_bytes: g.audio_data_start_pos_bytes,
        audio_data_length_bytes: g.audio_data_length_bytes,
        first_cluster_index_with_no_audio_data: g.first_cluster_index_with_no_audio_data,
        cluster_size: g.cluster_size as usize,
        cluster_size_magnitude: g.cluster_size_magnitude as usize,
        raw_data_format: g.raw_data_format,
    }
}

/// A manager over a test heap with one asset whose construct callback builds a real
/// `StreamedChunk` ([`construct_streamed_chunk`]).
struct ChunkHarness {
    handle: *mut DelugeResource,
    asset: u32,
    payload_offset: usize,
    header_size: usize,
    backing_size: usize,
    _buf: Vec<u128>,
}

impl ChunkHarness {
    fn new() -> Self {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf = vec![0u128; words];
        // SAFETY: `buf` is a 16-aligned `words * 16`-byte arena, moved into the returned harness
        // and so alive as long as `handle`.
        let heap = unsafe { fs_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        // SAFETY: `heap` is the live handle just created above.
        let handle = unsafe { deluge_resource_create(heap, 4, 8) };
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
        // SAFETY: `handle`/`asset` are live; `construct_streamed_chunk` has the `ConstructFn`
        // signature.
        unsafe {
            deluge_resource_set_construct(handle, asset, Some(construct_streamed_chunk));
        }
        let payload_offset =
            deluge_sample_fill::chunk::deluge_streamed_chunk_payload_offset() as usize;
        let header_size = core::mem::size_of::<StreamedChunk>();
        let backing_size = payload_offset + CLUSTER_SIZE as usize + 7;
        // Must be set before any `native_begin`/`native_finish` call.
        ACTIVE_MANAGER.store(handle as *mut c_void, Ordering::Relaxed);

        // `native_finish` resolves the asset's fill context and fails closed without one.
        deluge_sample_fill::deluge_streaming_set_fill_context(ptr::null_mut(), asset, geo());

        ChunkHarness {
            handle,
            asset,
            payload_offset,
            header_size,
            backing_size,
            _buf: buf,
        }
    }

    /// Requests and constructs chunk `index` without I/O, returning its backing pointer (the
    /// `StreamedChunk`, not the payload; see [`Self::payload_of`]).
    fn request(&self, index: u32) -> *mut u8 {
        // SAFETY: `self.handle`/`self.asset` are live and valid; `self.backing_size` is exactly
        // what `construct_streamed_chunk` (via `deluge_streamed_chunk_payload_offset`) expects.
        unsafe { deluge_resource_request(self.handle, self.asset, index, self.backing_size) }
    }

    /// Returns the chunk's `CLUSTER_SIZE + 7`-byte payload span at `backing + payload_offset`.
    ///
    /// # Safety
    ///
    /// `backing` must be a live chunk from [`Self::request`], exclusively held for the returned
    /// slice's lifetime.
    unsafe fn payload_of<'a>(&self, backing: *mut u8) -> &'a mut [u8] {
        // SAFETY: the backing holds `payload_offset + CLUSTER_SIZE + 7` bytes (`backing_size`);
        // exclusivity is the caller's contract.
        unsafe {
            core::slice::from_raw_parts_mut(
                backing.add(self.payload_offset),
                CLUSTER_SIZE as usize + 7,
            )
        }
    }

    /// Returns the unnamed front-guard padding `[header_size, payload_offset)`, which `finish` must
    /// never touch.
    ///
    /// # Safety
    ///
    /// Same contract as [`Self::payload_of`].
    unsafe fn front_guard_of<'a>(&self, backing: *mut u8) -> &'a mut [u8] {
        let len = self.payload_offset - self.header_size;
        // SAFETY: the range lies within the chunk's backing; exclusivity is the caller's contract.
        unsafe { core::slice::from_raw_parts_mut(backing.add(self.header_size), len) }
    }
}

/// A requested chunk with a seeded payload and a snapshot of its marked front guard.
struct SeededChunk {
    backing: *mut u8,
    front_guard_before: Vec<u8>,
}

fn seed(h: &ChunkHarness, index: u32, tag: u32) -> SeededChunk {
    let backing = h.request(index);
    assert!(!backing.is_null(), "request failed for index {index}");
    // SAFETY: `backing` was just constructed by `request` and nothing else holds it.
    let payload = unsafe { h.payload_of(backing) };
    payload.copy_from_slice(&region_fill_differential::ramp(
        tag,
        CLUSTER_SIZE as usize + 7,
    ));
    // Mark the front guard so later reads can prove `finish` never touched it.
    // SAFETY: same as above.
    let guard = unsafe { h.front_guard_of(backing) };
    guard.fill(0xC0u8.wrapping_add(tag as u8));
    let front_guard_before = guard.to_vec();
    SeededChunk {
        backing,
        front_guard_before,
    }
}

/// Finishes prev, then next, then self over real chunks, so self's `native_finish` gathers two
/// ready neighbours and stitches both edges. The result must match the C++ reference run in the
/// same order, for a non-native format.
///
/// A second pass then reloads and re-finishes prev with self as its ready next neighbour, which
/// only a single shared convert-state store gets right (see the module doc).
#[test]
fn cross_path_finish_matches_cpp_reference_over_real_streamed_chunks() {
    let h = ChunkHarness::new();
    let prev = seed(&h, 0, 1000);
    let self_c = seed(&h, 1, 1);
    let next = seed(&h, 2, 2000);

    // prev and next first, each with no ready neighbour, then self with both ready.
    // SAFETY: `prev` is a resident chunk the harness holds.
    assert!(unsafe { deluge_sample_fill::native_finish(prev.backing as *mut c_void, true) });
    // SAFETY: `next` is a resident chunk the harness holds.
    assert!(unsafe { deluge_sample_fill::native_finish(next.backing as *mut c_void, true) });
    // SAFETY: `self_c` is a resident chunk the harness holds.
    assert!(unsafe { deluge_sample_fill::native_finish(self_c.backing as *mut c_void, true) });

    // SAFETY: all three backings are still resident.
    let self_payload_after = unsafe { h.payload_of(self_c.backing) }.to_vec();
    let prev_payload_after = unsafe { h.payload_of(prev.backing) }.to_vec();
    let next_payload_after = unsafe { h.payload_of(next.backing) }.to_vec();

    // The C++ reference, same order and seeds.
    let mut prev_buf = region_fill_differential::ramp(1000, CLUSTER_SIZE as usize + 7);
    let mut self_buf = region_fill_differential::ramp(1, CLUSTER_SIZE as usize + 7);
    let mut next_buf = region_fill_differential::ramp(2000, CLUSTER_SIZE as usize + 7);
    let g = cpp_geo();

    let mut prev_state = region_fill_differential::cpp_ref::ConvertState::default();
    let mut next_state = region_fill_differential::cpp_ref::ConvertState::default();
    let mut self_state = region_fill_differential::cpp_ref::ConvertState::default();

    region_fill_differential::cpp_ref::finish_fill_over_buffers(
        &mut prev_buf,
        0,
        &g,
        &mut prev_state,
        None,
        None,
    );
    region_fill_differential::cpp_ref::finish_fill_over_buffers(
        &mut next_buf,
        2,
        &g,
        &mut next_state,
        None,
        None,
    );
    // self: both neighbours' post-finish buffers and flags feed in.
    let (mut prev_start_unused, mut prev_end) = (false, prev_state.end_converted);
    let (mut next_start, mut next_end_unused) = (next_state.start_converted, false);
    region_fill_differential::cpp_ref::finish_fill_over_buffers(
        &mut self_buf,
        1,
        &g,
        &mut self_state,
        Some(region_fill_differential::cpp_ref::Neighbour {
            payload: &mut prev_buf,
            unconverted_head: &prev_state.first_three_bytes, // unread on the prev side
            start_converted: &mut prev_start_unused,
            end_converted: &mut prev_end,
        }),
        Some(region_fill_differential::cpp_ref::Neighbour {
            payload: &mut next_buf,
            unconverted_head: &next_state.first_three_bytes,
            start_converted: &mut next_start,
            end_converted: &mut next_end_unused,
        }),
    );

    assert_eq!(
        self_payload_after, self_buf,
        "self payload diverged from the C++ reference — the cross-path glue produced different \
         bytes than an independently-implemented orchestration over the SAME inputs/order"
    );
    assert_eq!(
        prev_payload_after, prev_buf,
        "prev payload diverged (self's finish stitches prev's OWN tail bytes)"
    );
    assert_eq!(
        next_payload_after, next_buf,
        "next payload diverged (self's finish stitches next's OWN head bytes)"
    );

    // Both edges must really convert, or the scenario would pass vacuously.
    assert!(
        self_state.start_converted && self_state.end_converted,
        "expected a real straddle conversion on both self edges — geometry/format sanity check"
    );

    // Read the neighbours' convert state back through the accessor, bypassing `native_finish`.
    neighbour_convert_state_is_read_back_through_the_shared_store(&prev, &next);

    // Reload prev and finish it again with self as its ready next neighbour. It must read self's
    // `start_converted == true` from the shared store to take the "already converted" branch.
    {
        let prev_reload_tag = 3000;
        // SAFETY: `prev.backing` is still resident; this models a fresh read into the same slot.
        unsafe { h.payload_of(prev.backing) }.copy_from_slice(&region_fill_differential::ramp(
            prev_reload_tag,
            CLUSTER_SIZE as usize + 7,
        ));
        // SAFETY: `prev` is a resident chunk the harness holds.
        assert!(unsafe { deluge_sample_fill::native_finish(prev.backing as *mut c_void, true) });
        // SAFETY: `prev.backing` is still resident.
        let prev_reloaded_after = unsafe { h.payload_of(prev.backing) }.to_vec();

        let mut prev_buf2 =
            region_fill_differential::ramp(prev_reload_tag, CLUSTER_SIZE as usize + 7);
        let mut prev_state2 = region_fill_differential::cpp_ref::ConvertState::default();
        // `self_buf`/`self_state` are the reference's self results. As next neighbour, self supplies
        // its `start_converted` and `unconverted_head`; its payload is only read.
        let mut self_start_for_reload = self_state.start_converted;
        let mut self_end_unused = false;
        region_fill_differential::cpp_ref::finish_fill_over_buffers(
            &mut prev_buf2,
            0,
            &g,
            &mut prev_state2,
            None,
            Some(region_fill_differential::cpp_ref::Neighbour {
                payload: &mut self_buf,
                unconverted_head: &self_state.first_three_bytes,
                start_converted: &mut self_start_for_reload,
                end_converted: &mut self_end_unused,
            }),
        );

        assert_eq!(
            prev_reloaded_after, prev_buf2,
            "prev's RELOAD payload diverged from the C++ reference — the second finish's read of \
             self's shared convert-state (start_converted/unconverted_head) didn't match what \
             self's own finish actually published, the double-store bug's exact symptom"
        );
    }

    // The front guards must be untouched.
    for (name, c) in [("prev", &prev), ("self", &self_c), ("next", &next)] {
        // SAFETY: all three backings are still resident.
        let guard_after = unsafe { h.front_guard_of(c.backing) };
        assert_eq!(
            guard_after, c.front_guard_before,
            "{name}'s front-guard padding was touched by finish — a backing-vs-payload confusion \
             would corrupt exactly this region"
        );
    }
}

/// Checks prev's and next's convert state, read back through the chunk accessor after self's
/// `finish`, holds the flags self set rather than the zeroed default.
fn neighbour_convert_state_is_read_back_through_the_shared_store(
    prev: &SeededChunk,
    next: &SeededChunk,
) {
    // SAFETY: both backings are still resident, constructed `StreamedChunk`s.
    let prev_state_now =
        unsafe { deluge_sample_fill::chunk::convert_state(prev.backing as *mut c_void) };
    // SAFETY: same as above.
    let next_state_now =
        unsafe { deluge_sample_fill::chunk::convert_state(next.backing as *mut c_void) };

    // Self converted both edges, so it must have set these shared flags.
    assert!(
        prev_state_now.end_converted,
        "prev's end_converted must read back TRUE through the shared store after self's finish \
         wrote it back — a zeroed default here means the write-back went to a different store than \
         this readback (the double-store bug's exact symptom)"
    );
    assert!(
        next_state_now.start_converted,
        "next's start_converted must read back TRUE through the shared store after self's finish \
         wrote it back — same double-store symptom as prev's end_converted above"
    );
}

/// The layout assumption behind the backing-vs-payload check: `CLUSTER_SIZE + 7` must exceed
/// `size_of::<StreamedChunk>()` (so a wrong span reaches the marked front guard) and not exceed the
/// payload offset (so it never reaches the payload). If `StreamedChunk` grows, raise
/// `CLUSTER_SIZE`.
#[test]
fn geometry_and_layout_invariants_hold() {
    let header_size = core::mem::size_of::<StreamedChunk>();
    let payload_offset = deluge_sample_fill::chunk::deluge_streamed_chunk_payload_offset() as usize;
    let wrong_span_len = CLUSTER_SIZE as usize + 7;
    assert!(
        wrong_span_len > header_size,
        "CLUSTER_SIZE + 7 ({wrong_span_len}) must exceed size_of::<StreamedChunk>() \
         ({header_size}) so a backing-vs-payload bug's wrong span reaches the front-guard marker"
    );
    assert!(
        wrong_span_len <= payload_offset,
        "CLUSTER_SIZE + 7 ({wrong_span_len}) must not exceed the payload offset ({payload_offset}) \
         so a backing-vs-payload bug's wrong span never reaches the REAL payload region"
    );
}
