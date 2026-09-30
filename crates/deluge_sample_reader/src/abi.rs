//! The `deluge_sample_reader_*`/`deluge_sample_read` C ABI
//! (`include/libdeluge/sample_reader.h`). The lifecycle trio — `open`/`seek`/`close` — heap-boxes
//! a [`crate::reader::Reader`]; `window`/`advance`/`ok` are the read core. `deluge_sample_read` is
//! the stateless copy-out convenience — a thin shim over [`crate::reader::Reader::read`], which
//! composes `open` -> `window`/`advance` -> drop itself, so there is still only ONE residency path.
//!
//! Unlike `deluge_sample_source`'s `DelugeSampleSource` (a fixed, allocation-free static pool,
//! because that port's `open()`/`close()` can fire on the audio render ISR at note-start/-end),
//! this reader is explicitly a NON-voice, off-audio-thread handle — the design doc's own semantics
//! section is blocking by design ("non-voice runs off the audio thread"). Nothing here needs to
//! avoid the heap, so `open` is a plain `Box::new` + `Box::into_raw`, and `close` is `Box::from_raw`
//! + drop — no pool, no slot-recovery cast tricks.

use alloc::boxed::Box;
use core::ffi::c_void;

use crate::reader::{ReadHint, Reader};

/// Opaque per-reader handle, mirroring `DelugeSampleReader`. Every non-null pointer of this type
/// this module ever hands out from `open()` is actually a `Box<Reader>` turned into a raw pointer
/// via `Box::into_raw` — `close()` is the only legal way to reclaim it (`Box::from_raw`).
#[repr(C)]
pub struct DelugeSampleReader {
    _opaque: [u8; 0],
}

/// Open a reader over `source_id`'s sample residency, positioned at `start_frame` and reading in
/// `direction`. See the header doc (`deluge_sample_reader_open`) and [`Reader::open`] for the full
/// contract, including how `source_id`'s identity resolves geometry.
///
/// Never returns null: heap allocation only aborts (no `#[panic_handler]` unwind path
/// on this dependency graph — see the crate's lib.rs doc), so there is no OOM-null case to report,
/// unlike `deluge_sample_source_open`'s fixed pool, which can legitimately exhaust.
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

/// Random-access reposition: release any pin `reader` currently holds and move its cursor to
/// `frame`. No-op on a null `reader` (mirrors the header's null-tolerant contract, matching the
/// sibling `deluge_sample_source_*` ABI's own convention for a handle that could — in a future
/// task, or a caller's own bug — legitimately be null).
///
/// # Safety
/// `reader`, if non-null, must be a live pointer previously returned by
/// `deluge_sample_reader_open` and not yet passed to `deluge_sample_reader_close`.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reader_seek(reader: *mut DelugeSampleReader, frame: u64) {
    if reader.is_null() {
        return;
    }
    // SAFETY: non-null per the check above; live and not yet closed per this fn's own contract —
    // the same pointer `open()` produced from a `Box<Reader>`.
    let r = unsafe { &mut *(reader as *mut Reader) };
    r.seek(frame);
}

/// Release `reader` and any pin it still holds. No-op on a null `reader`.
///
/// # Safety
/// `reader`, if non-null, must be a live pointer previously returned by
/// `deluge_sample_reader_open`, not yet closed, and must not be used again after this call.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reader_close(reader: *mut DelugeSampleReader) {
    if reader.is_null() {
        return;
    }
    // SAFETY: `reader` was returned by `deluge_sample_reader_open` as `Box::into_raw(Box::new(..))`
    // cast to `*mut DelugeSampleReader` (same address, `Reader`'s layout underneath); non-null per
    // the check above; live and not yet closed per this fn's own contract. Reclaiming it as
    // `Box<Reader>` and dropping releases `held_lease`, if any, exactly once.
    let reader = unsafe { Box::from_raw(reader as *mut Reader) };
    drop(reader);
}

/// Mirrors `include/libdeluge/sample_reader.h`'s `DelugeFrameWindow` field-for-field.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct DelugeFrameWindow {
    pub frames: *const c_void,
    pub frame_count: u32,
}

/// The contiguous run of valid, already-converted frames at `reader`'s cursor. BLOCKS to make the
/// data resident. See the header doc and [`Reader::window`] for the full contract. A null `reader`
/// reports `{null, 0}` (matching a not-yet-open/never-successfully-opened reader — there is no
/// cursor to report a window for, so this is the same shape as this reader's own EOF report, not a
/// distinct error case at the ABI boundary).
///
/// # Safety
/// `reader`, if non-null, must be a live pointer previously returned by
/// `deluge_sample_reader_open` and not yet passed to `deluge_sample_reader_close`.
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
    // SAFETY: non-null per the check above; live and not yet closed per this fn's own contract.
    let r = unsafe { &mut *(reader as *mut Reader) };
    let (frames, frame_count) = r.window();
    DelugeFrameWindow {
        frames: frames as *const c_void,
        frame_count,
    }
}

/// Advance `reader`'s cursor `frames` frames in its own direction. No-op on a null `reader`. See
/// the header doc and [`Reader::advance`] for the full contract.
///
/// # Safety
/// `reader`, if non-null, must be a live pointer previously returned by
/// `deluge_sample_reader_open` and not yet passed to `deluge_sample_reader_close`.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reader_advance(
    reader: *mut DelugeSampleReader,
    frames: u32,
) {
    if reader.is_null() {
        return;
    }
    // SAFETY: non-null per the check above; live and not yet closed per this fn's own contract.
    let r = unsafe { &mut *(reader as *mut Reader) };
    r.advance(frames);
}

/// True unless the last `window()` call failed to make its data resident — distinct from ordinary
/// end-of-audio (`frame_count == 0`). A null `reader` reports `false` (mirrors a reader that was
/// never validly opened — there is no cursor to be "ok").
///
/// # Safety
/// `reader`, if non-null, must be a live pointer previously returned by
/// `deluge_sample_reader_open` and not yet passed to `deluge_sample_reader_close`.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reader_ok(reader: *const DelugeSampleReader) -> bool {
    if reader.is_null() {
        return false;
    }
    // SAFETY: non-null per the check above; live and not yet closed per this fn's own contract.
    let r = unsafe { &*(reader as *const Reader) };
    r.ok()
}

/// The stateless copy-out convenience. See the header doc (`deluge_sample_read`) and
/// [`Reader::read`] for the full contract -- this is a thin type-cast shim over it, matching every
/// other `abi.rs` wrapper in this module.
///
/// # Safety
/// `dest`, if `dest_bytes > 0`, must be valid for at least `dest_bytes` writable bytes.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_read(
    source_id: u32,
    start_frame: u64,
    num_frames: u32,
    dest: *mut c_void,
    dest_bytes: usize,
) -> u32 {
    // SAFETY: forwarded from this fn's own contract.
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

/// Stateless, passive resident-peek: the resident, already-converted frames at `start_frame` of
/// `source_id`'s residency, as a zero-copy run to the containing cluster's own boundary in
/// `direction`. See the header doc (`deluge_sample_peek`) and [`crate::reader::peek`] for the
/// full contract -- this is a thin type-cast shim over it, matching every other wrapper in this
/// module. Takes no pointer argument, so (unlike most of this module) there is no pointer
/// contract to forward -- a plain, non-`unsafe` `extern "C" fn`, like `deluge_sample_reader_open`.
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

/// Invalidate every currently-resident cluster of `source_id`'s sample: flag each unloadable and
/// cancel any queued load. See the header doc (`deluge_sample_invalidate`) and [`crate::reader::invalidate`]
/// for the full contract. Stateless, keyed by `source_id` like `deluge_sample_peek` — a plain,
/// non-`unsafe` `extern "C" fn`.
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

    /// Placement-construct a real `StreamedChunk` at `dest`: `Resource::request` (used to mint
    /// the leases these tests attach to a reader) requires SOME construct callback attached to
    /// the asset (see `deluge_resource`'s own `request_constructs_without_loading_then_leases`
    /// test — a construct-less asset refuses `request`), and `invalidate`'s own tests read the
    /// resulting chunk's `unloadable` flag back through the real
    /// `deluge_sample_fill::chunk::unloadable` accessor — which requires a genuinely
    /// constructed backing to reborrow soundly (mirrors `reader::tests::window_tests`'s own
    /// `real_chunk_construct`; see `lib.rs`'s `host_streaming_stubs` module doc for why this
    /// crate uses real C-ABI construct/accessors rather than stubs).
    ///
    /// # Safety
    /// `dest` must be a writable slot of at least `deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET`
    /// bytes — every asset this module's harness defines requests `CHUNK_SIZE` (4096) bytes per
    /// chunk, ample headroom for the small `StreamedChunk` header.
    unsafe extern "C" fn real_chunk_construct(
        ctx: *mut c_void,
        owner: *mut c_void,
        index: u32,
        dest: *mut u8,
    ) {
        // SAFETY: forwarding the caller's contract (above) to the real construct.
        unsafe {
            deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
                ctx,
                owner,
                index,
                dest as *mut c_void,
            );
        }
    }

    /// A `FillContext` compatible with `CHUNK_SIZE` (4096 = 2^12) — just enough for
    /// `deluge_sample_reader_open` (`Reader::open`) to resolve real geometry; this
    /// module's tests exercise the raw C-ABI lifecycle surface, never `window()`, so the exact
    /// values beyond `cluster_size`/`cluster_size_magnitude` don't matter.
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

    /// A `FillContext` sized for exactly two resident clusters (`audio_data_length_bytes ==
    /// 2 * CHUNK_SIZE`) — for `invalidate`'s own test, which (unlike every other test in this
    /// module) needs `deluge_sample_invalidate`'s cluster-span math (`ceil((start + length) /
    /// cluster_size)`) to resolve to a real, non-zero span rather than `abi_fill_context`'s
    /// unbounded `0`.
    fn two_cluster_fill_context() -> FillContext {
        FillContext {
            audio_data_length_bytes: (CHUNK_SIZE * 2) as u64,
            ..abi_fill_context()
        }
    }

    /// Build a manager over a fresh test heap (leaking its backing arena) with one requestable
    /// asset attached — the same harness shape `reader.rs`'s own tests use, duplicated here (each
    /// test module keeps its own small copy, matching the sibling crates' convention) since this
    /// module tests the raw C-ABI surface specifically, not `Reader`'s internal state. Also
    /// registers `ctx` as the asset's fill-context and routes the `deluge_streaming_resource_manager`
    /// stub — every caller must hold [`TEST_LOCK`] for its whole run (see that static's own doc).
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
        // SAFETY: `handle` is live; `real_chunk_construct` has the required C-ABI signature. No
        // materialize needed -- this module never `acquire`s through the asset, only
        // `Resource::request`s directly in the test bodies below.
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
        // SAFETY: `handle`/`asset` are live/valid per the call above.
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

    /// The harness every test in this module but `invalidate`'s own uses — a thin wrapper over
    /// [`manager_and_asset_with_context`] with the shared single-cluster [`abi_fill_context`].
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

        // Reach through the opaque pointer to attach a real lease (mirroring what `window()`
        // itself does), the same way `reader.rs`'s own tests do.
        let lease = resource.request(asset, 0, CHUNK_SIZE).expect("request");
        let slot = resource.slot_of(lease.chunk());
        assert_eq!(resource.lease_count_by_slot(slot), 1);
        // SAFETY: `ptr` is live; `Reader`'s layout underlies `DelugeSampleReader`, matching every
        // other cast in this module.
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
        // SAFETY: `ptr` is live; see the cast note above.
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
        // SAFETY: `reader` is null (the case under test) -- every fn null-checks before any deref.
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
        // Two-cluster sample. Build the manager, register its fill-context, make it the active
        // manager -- the shared harness, sized for two clusters (see `two_cluster_fill_context`).
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
        ); // non-vacuity
        resource.loader_enqueue(s1, 0xFFFF_FFFF); // re-enqueue what loader_next just popped

        // SAFETY: `l0`/`l1`'s chunks were placement-constructed by `real_chunk_construct` above
        // (this harness's registered construct callback) and stay live (leases held) here.
        unsafe {
            assert!(!deluge_sample_fill::chunk::unloadable(
                l0.chunk().as_ptr() as *mut c_void
            ));
            assert!(!deluge_sample_fill::chunk::unloadable(
                l1.chunk().as_ptr() as *mut c_void
            ));
        }

        deluge_sample_invalidate(asset);

        // Both clusters were flagged unloadable (the real `StreamedChunk` accessor, read directly).
        // SAFETY: both chunks are still live (leases held) for the duration of this read.
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
        // The queue was drained (loader_remove ran for the queued cluster).
        assert!(
            resource.loader_next().is_none(),
            "invalidate dequeued the queued cluster"
        );

        drop((l0, l1));
        // Each `cargo test` test runs on its own thread and `ACTIVE_MANAGER` is a `thread_local`
        // (see `set_active_manager`'s own doc), so there is nothing shared left to restore here --
        // unlike `POOL`/`ACTIVE_MANAGER` in crates that serialize tests over real statics.
    }

    /// Mirrors `resident_bytes_for_full_clusters_short_last_and_sentinel`'s own sentinel/zero
    /// coverage (`reader.rs`'s tests), but for `invalidate`: `audio_data_length_bytes ==
    /// UNKNOWN_LENGTH_SENTINEL` (still recording) has no finite geometric cluster-count bound, and
    /// `== 0` is the same "length not known yet" shape -- both must make `invalidate` return
    /// promptly as a no-op (bounded, not a ~4.3-billion-iteration scan) rather than flag or dequeue
    /// anything, even when a cluster IS resident.
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

            // SAFETY: `l0`'s chunk was placement-constructed by `real_chunk_construct` and stays
            // live (lease held) for the duration of this read.
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
