//! Host end-to-end fill: read → convert → stitch → publish over a real `deluge_resource` manager,
//! checked against the C++ reference.
//!
//! The fill orchestration (resolve geometry, read, gather neighbours, `finish_convert_stitch`,
//! publish) is reproduced by hand over `fill_logic`. The file is an in-memory buffer read through
//! [`synthetic_read_at`], which has the contract of the BSP's `storage::efatfs::host::read_at`; the
//! FAT read path is covered by `fs_differential`. Convert state is kept in local
//! `fill_logic::ConvertState` values, zeroed as for a never-finished chunk;
//! `tests/native_finish_glue.rs` drives the real chunk store and `native_finish` instead.
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

/// One sector, the smallest cluster `begin` handles: its read length is in whole sectors, so a
/// smaller cluster would read nothing.
const CLUSTER_SIZE: u32 = 512;
const CLUSTER_MAGNITUDE: u32 = 9; // 2^9 = 512
const PAYLOAD_LEN: usize = CLUSTER_SIZE as usize + 7;

/// The asset's `construct` callback (an asset needs one to be requestable): fills the backing with
/// a known byte instead of heap garbage.
unsafe extern "C" fn mock_construct(
    _ctx: *mut c_void,
    _owner: *mut c_void,
    _index: u32,
    dest: *mut u8,
) {
    // SAFETY: `dest` is the manager's fresh `PAYLOAD_LEN`-byte backing; every request here uses
    // that size.
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
        // SAFETY: `buf` is a 16-aligned `words * 16`-byte arena, moved into the returned
        // `TestManager` and so alive as long as `handle`.
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

    /// Reserves chunk `index`'s `PAYLOAD_LEN`-byte backing under a hard lease, without I/O; the
    /// caller fills it.
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

    /// The RT-safe resident-and-ready probe the fill uses to gather neighbours: null until
    /// `mark_ready`.
    fn try_acquire(&self, index: u32) -> *mut u8 {
        // SAFETY: `self.handle`/`self.asset` are live and valid.
        unsafe { deluge_resource_try_acquire(self.handle, self.asset, index) }
    }
}

/// Reads `dst.len()` bytes from absolute `byte_offset` of `file`; returns `true` iff `dst` was
/// filled.
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
        audio_data_start_pos_bytes: 3,    // misaligned
        audio_data_length_bytes: 100_000, // none of the 3 clusters is the last
        first_cluster_index_with_no_audio_data: 50,
        cluster_size: CLUSTER_SIZE,
        cluster_size_magnitude: CLUSTER_MAGNITUDE,
        raw_data_format: 2, // UNSIGNED_8
    }
}

/// Three clusters of raw data in a position-dependent pattern.
fn synthetic_file() -> Vec<u8> {
    region_fill_differential::ramp(0xF11E, 3 * CLUSTER_SIZE as usize)
}

/// The read step for one cluster: `begin` gives the offset and sector count, and the read fills
/// `dest`'s leading `cluster_size` bytes. The 7-byte trailing slack is written only by the stitch.
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

/// Fills the middle of three chunks and checks the result matches the C++ reference over the same
/// bytes, and that the chunk is invisible to `try_acquire` until `mark_ready` publishes it.
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
    // uniquely held here (no other alias exists) for the extent of the
    // `read_into`/`finish_convert_stitch` calls below.
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

    assert!(
        mgr.try_acquire(1).is_null(),
        "try_acquire must be null before mark_ready (chunk is still just Loading)"
    );

    // The finish step: no chunk has been finished before, so all convert state starts zeroed.
    let mut self_state = fill_logic::ConvertState::default();
    let mut prev_state = fill_logic::ConvertState::default();
    let mut next_state = fill_logic::ConvertState::default();

    {
        // SAFETY: the same three backings, still unaliased; each slice lives only for this call.
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

    // The C++ reference over freshly read copies of the same bytes, with the same zeroed state.
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

    // SAFETY: `self_ptr` is still leased (never released) and the earlier mutable borrows have
    // ended.
    let self_bytes_now = unsafe { core::slice::from_raw_parts(self_ptr, PAYLOAD_LEN) };
    assert_eq!(
        self_bytes_now,
        expect_self.as_slice(),
        "end-to-end self payload mismatch vs. the C++ reference"
    );
    assert_eq!(self_state.first_three_bytes, expect_state.first_three_bytes);
    assert_eq!(self_state.start_converted, expect_state.start_converted);
    assert_eq!(self_state.end_converted, expect_state.end_converted);

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
