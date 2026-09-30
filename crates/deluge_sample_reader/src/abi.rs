//! The `deluge_sample_reader_*` / `deluge_sample_read` C ABI (`libdeluge/sample_reader.h`).
//!
//! `open` heap-boxes a [`Reader`] and `close` drops it; `window`/`advance`/`ok` are the read core.
//! The stateless `deluge_sample_read`, `deluge_sample_peek` and `deluge_sample_invalidate` are
//! thin shims over [`crate::reader`].
//!
//! Unlike `deluge_sample_source`, whose static pool exists because it opens and closes on the audio
//! ISR, this reader is used only off the audio thread and may block, so it uses the heap freely.

use alloc::boxed::Box;
use core::ffi::c_void;

use crate::reader::{ReadHint, Reader};

/// Opaque reader handle, mirroring `DelugeSampleReader`.
///
/// Every non-null pointer `deluge_sample_reader_open` returns is a `Box<Reader>` from
/// `Box::into_raw`; only `deluge_sample_reader_close` may reclaim it.
#[repr(C)]
pub struct DelugeSampleReader {
    _opaque: [u8; 0],
}

/// Opens a reader over `source_id`'s residency at `start_frame`, reading in `direction`. See
/// [`Reader::open`].
///
/// Never returns null: allocation failure aborts rather than returning.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub extern "C" fn deluge_sample_reader_open(
    source_id: u32,
    start_frame: u64,
    direction: i8,
    hint: ReadHint,
) -> *mut DelugeSampleReader {
    let reader = Reader::open(source_id, start_frame, direction, hint);
    Box::into_raw(Box::new(reader)) as *mut DelugeSampleReader
}

/// Releases any pin `reader` holds and moves its cursor to `frame`. A no-op on a null `reader`.
///
/// # Safety
///
/// `reader` must be null or a live pointer from `deluge_sample_reader_open` not yet passed to
/// `deluge_sample_reader_close`.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reader_seek(reader: *mut DelugeSampleReader, frame: u64) {
    if reader.is_null() {
        return;
    }
    // SAFETY: non-null, and a live `Box<Reader>` pointer per this function's contract.
    let r = unsafe { &mut *(reader as *mut Reader) };
    r.seek(frame);
}

/// Releases `reader` and any pin it still holds. A no-op on a null `reader`.
///
/// # Safety
///
/// `reader` must be null or a live pointer from `deluge_sample_reader_open` not yet closed. It must
/// not be used after this call.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reader_close(reader: *mut DelugeSampleReader) {
    if reader.is_null() {
        return;
    }
    // SAFETY: non-null, and the live `Box<Reader>` pointer `open` produced, per this function's
    // contract. Dropping it releases `held_lease`, if any, exactly once.
    let reader = unsafe { Box::from_raw(reader as *mut Reader) };
    drop(reader);
}

/// Mirrors `DelugeFrameWindow` (`libdeluge/sample_reader.h`) field for field.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct DelugeFrameWindow {
    pub frames: *const c_void,
    pub frame_count: u32,
}

/// Returns the contiguous run of converted frames at `reader`'s cursor, blocking until it is
/// resident. See [`Reader::window`]. A null `reader` reports `{null, 0}`, the same shape as EOF.
///
/// # Safety
///
/// `reader` must be null or a live pointer from `deluge_sample_reader_open` not yet passed to
/// `deluge_sample_reader_close`.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reader_window(
    reader: *mut DelugeSampleReader,
) -> DelugeFrameWindow {
    if reader.is_null() {
        return DelugeFrameWindow {
            frames: core::ptr::null(),
            frame_count: 0,
        };
    }
    // SAFETY: non-null, and live per this function's contract.
    let r = unsafe { &mut *(reader as *mut Reader) };
    let (frames, frame_count) = r.window();
    DelugeFrameWindow {
        frames: frames as *const c_void,
        frame_count,
    }
}

/// Advances `reader`'s cursor by `frames` in its direction. A no-op on a null `reader`. See
/// [`Reader::advance`].
///
/// # Safety
///
/// `reader` must be null or a live pointer from `deluge_sample_reader_open` not yet passed to
/// `deluge_sample_reader_close`.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reader_advance(
    reader: *mut DelugeSampleReader,
    frames: u32,
) {
    if reader.is_null() {
        return;
    }
    // SAFETY: non-null, and live per this function's contract.
    let r = unsafe { &mut *(reader as *mut Reader) };
    r.advance(frames);
}

/// Returns `true` unless the last `window()` failed to make its data resident, which is distinct
/// from end of audio (`frame_count == 0`). A null `reader` reports `false`.
///
/// # Safety
///
/// `reader` must be null or a live pointer from `deluge_sample_reader_open` not yet passed to
/// `deluge_sample_reader_close`.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reader_ok(reader: *const DelugeSampleReader) -> bool {
    if reader.is_null() {
        return false;
    }
    // SAFETY: non-null, and live per this function's contract.
    let r = unsafe { &*(reader as *const Reader) };
    r.ok()
}

/// Copies frames from `source_id` into `dest` without a caller-held reader. See [`Reader::read`].
///
/// # Safety
///
/// If `dest_bytes > 0`, `dest` must be valid for `dest_bytes` writable bytes.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_read(
    source_id: u32,
    start_frame: u64,
    num_frames: u32,
    dest: *mut c_void,
    dest_bytes: usize,
) -> u32 {
    // SAFETY: `dest`/`dest_bytes` are covered by this function's contract.
    unsafe {
        Reader::read(
            source_id,
            start_frame,
            num_frames,
            dest as *mut u8,
            dest_bytes,
        )
    }
}

/// Returns the frames at `start_frame` of `source_id` that are already resident and converted, up
/// to the containing cluster's boundary in `direction`, without loading anything. See
/// [`crate::reader::peek`].
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub extern "C" fn deluge_sample_peek(
    source_id: u32,
    start_frame: u64,
    direction: i8,
) -> DelugeFrameWindow {
    let (frames, frame_count) = crate::reader::peek(source_id, start_frame, direction);
    DelugeFrameWindow {
        frames: frames as *const c_void,
        frame_count,
    }
}

/// Flags every resident cluster of `source_id`'s sample unloadable and cancels any queued load.
/// See [`crate::reader::invalidate`].
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub extern "C" fn deluge_sample_invalidate(source_id: u32) {
    crate::reader::invalidate(source_id);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::host_streaming_stubs::{set_active_manager, TEST_LOCK};
    use crate::reader::UNKNOWN_LENGTH_SENTINEL;
    use deluge_resource::facade::Resource;
    use deluge_resource::value::COST_IO;
    use deluge_resource::DelugeResource;
    use deluge_sample_fill::{deluge_streaming_set_fill_context, FillContext};
    extern crate std;
    use core::ffi::c_void;
    use std::vec::Vec;

    const CHUNK_SIZE: usize = 4096;

    struct TestHeap {
        _buf: Vec<u128>,
    }

    /// Placement-constructs a real `StreamedChunk` at `dest`.
    ///
    /// `Resource::request` refuses an asset with no construct callback, and the `invalidate` tests
    /// read `unloadable` back through the real accessor, which needs a constructed backing.
    ///
    /// # Safety
    ///
    /// `dest` must be writable for at least `deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET`
    /// bytes; this harness's chunks are `CHUNK_SIZE` bytes.
    unsafe extern "C" fn real_chunk_construct(
        ctx: *mut c_void,
        owner: *mut c_void,
        index: u32,
        dest: *mut u8,
    ) {
        // SAFETY: the caller's contract is the real construct's.
        unsafe {
            deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
                ctx,
                owner,
                index,
                dest as *mut c_void,
            );
        }
    }

    /// A `FillContext` for `CHUNK_SIZE` clusters, enough for `Reader::open` to resolve geometry.
    /// These tests never call `window()`, so the other values do not matter.
    fn abi_fill_context() -> FillContext {
        FillContext {
            efatfs_handle: 0,
            audio_data_start_pos_bytes: 0,
            audio_data_length_bytes: 0,
            first_cluster_index_with_no_audio_data: -1,
            cluster_size: CHUNK_SIZE as u32,
            cluster_size_magnitude: 12,
            raw_data_format: 0,
            byte_depth: 2,
            num_channels: 1,
        }
    }

    /// A `FillContext` two clusters long, so `invalidate` has a non-zero cluster span.
    fn two_cluster_fill_context() -> FillContext {
        FillContext {
            audio_data_length_bytes: (CHUNK_SIZE * 2) as u64,
            ..abi_fill_context()
        }
    }

    /// Builds a manager over a leaked test heap with one requestable asset, registers `ctx` as its
    /// fill-context, and makes it the active manager. Callers must hold [`TEST_LOCK`].
    fn manager_and_asset_with_context(ctx: FillContext) -> (*mut DelugeResource, u32) {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf: Vec<u128> = std::vec![0u128; words];
        let ptr = buf.as_mut_ptr() as *mut u8;
        // SAFETY: `buf` is leaked below so the arena stays alive for the whole test binary's life.
        let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
        std::mem::forget(TestHeap { _buf: buf });
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
        assert!(!handle.is_null());
        // SAFETY: `handle` is live. No materialize callback: these tests only `Resource::request`.
        let asset = unsafe {
            deluge_resource::deluge_resource_define_asset(
                handle,
                core::ptr::null_mut(),
                None,
                None,
                core::ptr::null_mut(),
                COST_IO,
                deluge_resource::manager::BACKING_HEAP,
            )
        };
        // SAFETY: `handle` and `asset` are live.
        unsafe {
            deluge_resource::deluge_resource_set_construct(
                handle,
                asset,
                Some(real_chunk_construct),
            )
        };
        deluge_streaming_set_fill_context(core::ptr::null_mut(), asset, ctx);
        set_active_manager(handle as *mut c_void);
        (handle, asset)
    }

    /// [`manager_and_asset_with_context`] with [`abi_fill_context`].
    fn test_manager_and_asset() -> (*mut DelugeResource, u32) {
        manager_and_asset_with_context(abi_fill_context())
    }

    #[test]
    fn open_returns_non_null_and_close_on_a_lease_free_reader_is_a_clean_no_leak_round_trip() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (_handle, asset) = test_manager_and_asset();
        let ptr = deluge_sample_reader_open(asset, 0, 1, ReadHint::Cached);
        assert!(!ptr.is_null(), "open must return a non-null handle");
        // SAFETY: `ptr` is live, not yet closed.
        unsafe { assert!(deluge_sample_reader_ok(ptr)) };
        // SAFETY: `ptr` is live, not yet closed.
        unsafe { deluge_sample_reader_close(ptr) };
    }

    #[test]
    fn seek_through_the_abi_updates_current_frame_and_drops_a_held_lease() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (handle, asset) = test_manager_and_asset();
        // SAFETY: `handle` is live for the test's duration.
        let resource = unsafe { Resource::from_handle(handle) };

        let ptr = deluge_sample_reader_open(asset, 5, 1, ReadHint::Cached);
        assert!(!ptr.is_null());

        // Attach a real lease through the opaque pointer, as `window()` would.
        let lease = resource.request(asset, 0, CHUNK_SIZE).expect("request");
        let slot = resource.slot_of(lease.chunk());
        assert_eq!(resource.lease_count_by_slot(slot), 1);
        // SAFETY: `ptr` is a live `Box<Reader>` pointer.
        unsafe { (*(ptr as *mut Reader)).held_lease = Some(lease) };

        // SAFETY: `ptr` is live, not yet closed.
        unsafe { deluge_sample_reader_seek(ptr, 99) };

        // SAFETY: `ptr` is still live.
        let current_frame = unsafe { (*(ptr as *mut Reader)).current_frame() };
        assert_eq!(current_frame, 99, "seek must move the cursor");
        assert_eq!(
            resource.lease_count_by_slot(slot),
            0,
            "seek must release the held lease"
        );

        // SAFETY: `ptr` is live, not yet closed.
        unsafe { deluge_sample_reader_close(ptr) };
    }

    #[test]
    fn close_on_a_held_lease_releases_it_without_leak() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (handle, asset) = test_manager_and_asset();
        // SAFETY: `handle` is live for the test's duration.
        let resource = unsafe { Resource::from_handle(handle) };

        let ptr = deluge_sample_reader_open(asset, 0, 1, ReadHint::Cached);
        assert!(!ptr.is_null());

        let lease = resource.request(asset, 1, CHUNK_SIZE).expect("request");
        let slot = resource.slot_of(lease.chunk());
        // SAFETY: `ptr` is a live `Box<Reader>` pointer.
        unsafe { (*(ptr as *mut Reader)).held_lease = Some(lease) };
        assert_eq!(resource.lease_count_by_slot(slot), 1);

        // SAFETY: `ptr` is live, not yet closed, and not used again after this call.
        unsafe { deluge_sample_reader_close(ptr) };

        assert_eq!(
            resource.lease_count_by_slot(slot),
            0,
            "close must release the held lease, with no leak"
        );
    }

    #[test]
    fn null_reader_is_tolerated_by_seek_close_advance_and_ok() {
        // SAFETY: every function null-checks before dereferencing.
        unsafe {
            deluge_sample_reader_seek(core::ptr::null_mut(), 5);
            deluge_sample_reader_advance(core::ptr::null_mut(), 5);
            assert!(!deluge_sample_reader_ok(core::ptr::null()));
            let w = deluge_sample_reader_window(core::ptr::null_mut());
            assert!(w.frames.is_null());
            assert_eq!(w.frame_count, 0);
            deluge_sample_reader_close(core::ptr::null_mut());
        }
    }

    #[test]
    fn invalidate_flags_and_dequeues_every_resident_cluster() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (handle, asset) = manager_and_asset_with_context(two_cluster_fill_context());

        // SAFETY: `handle` is live for the test's duration.
        let resource = unsafe { Resource::from_handle(handle) };
        // Make cluster 0 resident+ready and cluster 1 reserved+queued (unfilled).
        let l0 = resource.request(asset, 0, CHUNK_SIZE).unwrap();
        resource.mark_ready(l0.chunk());
        let l1 = resource.request(asset, 1, CHUNK_SIZE).unwrap();
        resource.loader_enqueue(resource.slot_of(l1.chunk()), 0xFFFF_FFFF);
        let s1 = resource.slot_of(l1.chunk());
        assert!(
            resource.loader_next().is_some(),
            "precondition: queue is non-empty"
        );
        resource.loader_enqueue(s1, 0xFFFF_FFFF); // Re-enqueue what `loader_next` popped.

        // SAFETY: both chunks were constructed by `real_chunk_construct` and are leased.
        unsafe {
            assert!(!deluge_sample_fill::chunk::unloadable(
                l0.chunk().as_ptr() as *mut c_void
            ));
            assert!(!deluge_sample_fill::chunk::unloadable(
                l1.chunk().as_ptr() as *mut c_void
            ));
        }

        deluge_sample_invalidate(asset);

        // SAFETY: both chunks are still leased.
        unsafe {
            assert!(
                deluge_sample_fill::chunk::unloadable(l0.chunk().as_ptr() as *mut c_void),
                "cluster 0 flagged unloadable"
            );
            assert!(
                deluge_sample_fill::chunk::unloadable(l1.chunk().as_ptr() as *mut c_void),
                "cluster 1 flagged unloadable"
            );
        }
        assert!(
            resource.loader_next().is_none(),
            "invalidate dequeued the queued cluster"
        );

        drop((l0, l1));
    }

    /// An unknown (`UNKNOWN_LENGTH_SENTINEL`, still recording) or zero length gives no finite
    /// cluster bound, so `invalidate` must return at once without touching a resident cluster,
    /// rather than scan ~4.3 billion clusters.
    #[test]
    fn invalidate_is_a_bounded_no_op_when_length_is_unknown_or_zero() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        for length in [UNKNOWN_LENGTH_SENTINEL, 0] {
            let ctx = FillContext {
                audio_data_length_bytes: length,
                ..two_cluster_fill_context()
            };
            let (handle, asset) = manager_and_asset_with_context(ctx);

            // SAFETY: `handle` is live for the test's duration.
            let resource = unsafe { Resource::from_handle(handle) };
            let l0 = resource.request(asset, 0, CHUNK_SIZE).unwrap();
            resource.mark_ready(l0.chunk());
            let slot = resource.slot_of(l0.chunk());

            deluge_sample_invalidate(asset);

            // SAFETY: the chunk was constructed by `real_chunk_construct` and is leased.
            assert!(
                !unsafe {
                    deluge_sample_fill::chunk::unloadable(l0.chunk().as_ptr() as *mut c_void)
                },
                "length {length:#x} must invalidate nothing -- no finite bound available"
            );
            assert_eq!(
                resource.lease_count_by_slot(slot),
                1,
                "the resident cluster's lease must be untouched by a bounded no-op"
            );

            drop(l0);
        }
    }
}
