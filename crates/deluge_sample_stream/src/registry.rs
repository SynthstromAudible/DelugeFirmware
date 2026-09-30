//! The stream-slot table: a fixed-capacity registry of open efatfs streaming handles.
//!
//! A handle is `slot_index + 1`; `0` is the invalid handle. Every entry point validates its handle,
//! so `0`, out-of-range and closed handles are harmless.
//!
//! # Fill-context registration
//!
//! [`set_geometry`] and [`set_asset_id`] both end in `try_register_fill_context`, which does
//! nothing until the slot has both a real asset id and a geometry, so a half-built context is never
//! registered.

use core::cell::RefCell;
use core::ffi::{c_char, c_void};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::blocking_mutex::Mutex;

use deluge_sample_fill::{DelugeStatus, DELUGE_ERR_PARAM, DELUGE_OK};

use crate::{DelugeSampleStreamGeometry, DelugeStreamingFillContext, DELUGE_RESOURCE_NO_ASSET};

/// Slot-table capacity, matching `deluge_sample_source`'s pool size.
// TODO: confirm against `AudioFileManager`'s audio-file cap.
pub const CAP: usize = 256;

/// One open stream.
///
/// `efatfs_handle` is always a live handle: a slot exists only after a successful
/// `deluge_efatfs_open`. `asset_id` is [`DELUGE_RESOURCE_NO_ASSET`] until [`set_asset_id`].
struct Slot {
    efatfs_handle: deluge_sample_fill::DelugeHandle,
    asset_id: u32,
    geometry: Option<DelugeSampleStreamGeometry>,
}

/// The slot table.
///
/// A plain critical-section mutex is enough: no entry point runs on the audio render ISR (`open`,
/// `close` and the setters run on the main/load thread, `read_at` in the synchronous fill).
static REGISTRY: Mutex<CriticalSectionRawMutex, RefCell<[Option<Slot>; CAP]>> =
    Mutex::new(RefCell::new([const { None }; CAP]));

/// Returns `handle - 1` as a table index, or `None` for `0` or an out-of-range handle.
fn slot_index(handle: u32) -> Option<usize> {
    if handle == 0 {
        return None;
    }
    let index = (handle - 1) as usize;
    (index < CAP).then_some(index)
}

/// Writes `value` through `out` if it is non-null.
///
/// # Safety
///
/// `out` must be null or valid for one write.
unsafe fn write_out(out: *mut bool, value: bool) {
    if !out.is_null() {
        // SAFETY: non-null, and valid per this function's contract.
        unsafe { *out = value };
    }
}

/// Opens `path` for streaming reads and claims a slot for it. Returns the handle, or `0` on
/// failure.
///
/// `out_table_full`, if non-null, is always written: `true` when the failure was a full handle
/// table, either this registry's or efatfs's own, so the caller can report exhaustion rather than
/// "file not found". A just-opened efatfs handle that finds no free slot is closed, not leaked.
///
/// # Safety
///
/// `path` must be a valid, NUL-terminated C string. `out_table_full`, if non-null, must be valid
/// for one write.
pub unsafe fn open(path: *const c_char, out_table_full: *mut bool) -> u32 {
    let mut handle: u32 = 0;
    let mut efatfs_table_full = false;
    // SAFETY: `path` is passed through untouched under the caller's contract; the out-params are
    // valid locals.
    let opened = unsafe { crate::deluge_efatfs_open(path, &mut handle, &mut efatfs_table_full) };
    if opened != DELUGE_OK {
        // SAFETY: `out_table_full` is null or valid per the caller's contract.
        unsafe { write_out(out_table_full, efatfs_table_full) };
        return 0;
    }

    let slot = Slot {
        efatfs_handle: handle,
        asset_id: DELUGE_RESOURCE_NO_ASSET,
        geometry: None,
    };
    let claimed = REGISTRY.lock(|table| {
        let mut table = table.borrow_mut();
        let free = table.iter().position(Option::is_none)?;
        table[free] = Some(slot);
        Some(free)
    });

    match claimed {
        Some(index) => {
            // SAFETY: as above.
            unsafe { write_out(out_table_full, false) };
            (index + 1) as u32
        }
        None => {
            // No free slot: close the just-opened efatfs handle rather than leak it.
            // SAFETY: `handle` is live and owned by nobody else.
            unsafe { crate::deluge_efatfs_close(handle) };
            // SAFETY: as above.
            unsafe { write_out(out_table_full, true) };
            0
        }
    }
}

/// Closes `handle`'s efatfs handle and releases its asset, if one was assigned. A no-op on an
/// invalid or already-closed handle.
pub fn close(handle: u32) {
    let Some(index) = slot_index(handle) else {
        return;
    };
    let Some(slot) = REGISTRY.lock(|table| table.borrow_mut()[index].take()) else {
        return;
    };
    // SAFETY: the slot held the only copy of this live handle, and `take()` cleared it, so no other
    // `close` can reach it.
    unsafe { crate::deluge_efatfs_close(slot.efatfs_handle) };
    if slot.asset_id != DELUGE_RESOURCE_NO_ASSET {
        // SAFETY: returns the process-wide manager, which lives for the rest of the process once
        // non-null.
        let mgr = unsafe { crate::deluge_streaming_resource_manager() };
        if !mgr.is_null() {
            // SAFETY: `mgr` is non-null.
            unsafe { crate::deluge_resource_release_asset(mgr, slot.asset_id) };
        }
    }
}

/// Stores `geo` on `handle`'s slot, then calls `try_register_fill_context`. A no-op on an
/// invalid handle.
pub fn set_geometry(handle: u32, geo: DelugeSampleStreamGeometry) {
    let Some(index) = slot_index(handle) else {
        return;
    };
    REGISTRY.lock(|table| {
        if let Some(slot) = table.borrow_mut()[index].as_mut() {
            slot.geometry = Some(geo);
        }
    });
    try_register_fill_context(index);
}

/// Returns `handle`'s asset id, or [`DELUGE_RESOURCE_NO_ASSET`] for an invalid handle or one with
/// no asset yet.
pub fn asset_id(handle: u32) -> u32 {
    let Some(index) = slot_index(handle) else {
        return DELUGE_RESOURCE_NO_ASSET;
    };
    REGISTRY.lock(|table| {
        table.borrow()[index]
            .as_ref()
            .map_or(DELUGE_RESOURCE_NO_ASSET, |slot| slot.asset_id)
    })
}

/// Stores asset `id` on `handle`'s slot, then calls `try_register_fill_context`. A no-op on an
/// invalid handle.
pub fn set_asset_id(handle: u32, id: u32) {
    let Some(index) = slot_index(handle) else {
        return;
    };
    REGISTRY.lock(|table| {
        if let Some(slot) = table.borrow_mut()[index].as_mut() {
            slot.asset_id = id;
        }
    });
    try_register_fill_context(index);
}

/// Reads up to `len` bytes at `byte_offset` of `handle`'s file into `buf`.
///
/// Returns the byte count (`Ok(0)` at or past EOF is not a failure), `Err(DELUGE_ERR_PARAM)` for an
/// invalid handle, or the `deluge_efatfs_read_at` status. Keeping EOF and failure apart lets the
/// C++ `SampleStreamReadSource` forward them faithfully.
///
/// # Safety
///
/// If `len > 0`, `buf` must be valid for at least `len` writable bytes.
pub unsafe fn read_at(
    handle: u32,
    byte_offset: u32,
    buf: *mut u8,
    len: u32,
) -> Result<u32, DelugeStatus> {
    let index = slot_index(handle).ok_or(DELUGE_ERR_PARAM)?;
    // `None`: a closed slot.
    let efatfs_handle = REGISTRY
        .lock(|table| {
            table.borrow()[index]
                .as_ref()
                .map(|slot| slot.efatfs_handle)
        })
        .ok_or(DELUGE_ERR_PARAM)?;
    // No "still recording" check: a still-recording sample never has a slot (the C++ facade
    // registers its handle-less fill-context directly), so every slot's handle is readable.
    let mut out_read: u32 = 0;
    // SAFETY: `buf` is valid for `len` bytes per this function's contract; `out_read` is a local.
    let status = unsafe {
        crate::deluge_efatfs_read_at(
            efatfs_handle,
            byte_offset,
            buf as *mut c_void,
            len,
            &mut out_read,
        )
    };
    if status == DELUGE_OK {
        Ok(out_read)
    } else {
        Err(status)
    }
}

/// Registers `index`'s fill-context with the resource manager once the slot has both a real
/// asset id and a geometry; otherwise does nothing.
fn try_register_fill_context(index: usize) {
    let ready = REGISTRY.lock(|table| {
        let table = table.borrow();
        let slot = table[index].as_ref()?;
        if slot.asset_id == DELUGE_RESOURCE_NO_ASSET {
            return None;
        }
        let geo = slot.geometry?;
        Some((slot.asset_id, slot.efatfs_handle, geo))
    });
    let Some((asset_id, efatfs_handle, geo)) = ready else {
        return;
    };
    // SAFETY: no preconditions; returns the process-wide manager or null.
    let mgr = unsafe { crate::deluge_streaming_resource_manager() };
    if mgr.is_null() {
        return; // No manager yet (boot ordering).
    }
    let ctx = DelugeStreamingFillContext {
        efatfs_handle,
        audio_data_start_pos_bytes: geo.audio_data_start_pos_bytes,
        audio_data_length_bytes: geo.audio_data_length_bytes,
        first_cluster_index_with_no_audio_data: geo.first_cluster_index_with_no_audio_data,
        cluster_size: geo.cluster_size,
        cluster_size_magnitude: geo.cluster_size_magnitude,
        raw_data_format: geo.raw_data_format,
        byte_depth: geo.byte_depth,
        num_channels: geo.num_channels,
    };
    // SAFETY: `mgr` is non-null.
    unsafe { crate::deluge_streaming_set_fill_context(mgr, asset_id, ctx) };
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mock_backing;
    extern crate std;

    fn sample_geometry() -> DelugeSampleStreamGeometry {
        DelugeSampleStreamGeometry {
            audio_data_start_pos_bytes: 44,
            audio_data_length_bytes: 8192,
            first_cluster_index_with_no_audio_data: -1,
            cluster_size: 4096,
            cluster_size_magnitude: 12,
            raw_data_format: 0,
            byte_depth: 2,
            num_channels: 1,
        }
    }

    #[test]
    fn open_stores_handle_then_close_releases() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(
            /*handle=*/ 42, /*ok=*/ true, /*table_full=*/ false,
        );
        let mut tf = false;
        // SAFETY: a C string literal and a local.
        let h = unsafe { crate::registry::open(c"SAMPLES/X.WAV".as_ptr(), &mut tf) };
        assert_ne!(h, 0);
        assert_eq!(mock_backing::last_opened_path(), "SAMPLES/X.WAV");
        crate::registry::close(h);
        assert_eq!(mock_backing::closed_handles(), std::vec![42]);
        // second close is a no-op
        crate::registry::close(h);
        assert_eq!(mock_backing::closed_handles(), std::vec![42]);
    }

    #[test]
    fn fill_context_registers_once_on_the_second_of_geometry_then_asset_id() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(7, true, false);
        let mut tf = false;
        // SAFETY: a C string literal and a local.
        let h = unsafe { open(c"SAMPLES/Y.WAV".as_ptr(), &mut tf) };
        assert_ne!(h, 0);

        let geo = sample_geometry();
        set_geometry(h, geo);
        assert_eq!(
            mock_backing::fill_context_call_count(),
            0,
            "geometry alone (no asset id yet) must not register"
        );

        set_asset_id(h, 99);
        assert_eq!(
            mock_backing::fill_context_call_count(),
            1,
            "the second of {{geometry, asset_id}} must register exactly once"
        );

        let (asset, ctx) = mock_backing::last_fill_context().expect("a context was registered");
        assert_eq!(asset, 99);
        assert_eq!(ctx.efatfs_handle, 7);
        assert_eq!(
            ctx.audio_data_start_pos_bytes,
            geo.audio_data_start_pos_bytes
        );
        assert_eq!(ctx.audio_data_length_bytes, geo.audio_data_length_bytes);
        assert_eq!(
            ctx.first_cluster_index_with_no_audio_data,
            geo.first_cluster_index_with_no_audio_data
        );
        assert_eq!(ctx.cluster_size, geo.cluster_size);
        assert_eq!(ctx.cluster_size_magnitude, geo.cluster_size_magnitude);
        assert_eq!(ctx.raw_data_format, geo.raw_data_format);
        assert_eq!(ctx.byte_depth, geo.byte_depth);
        assert_eq!(ctx.num_channels, geo.num_channels);

        close(h);
        assert_eq!(mock_backing::closed_handles(), std::vec![7]);
    }

    #[test]
    fn table_full_on_the_capacity_plus_one_open_closes_the_extra_handle() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(99, true, false);

        let mut handles = std::vec::Vec::new();
        for _ in 0..CAP {
            let mut tf = false;
            // SAFETY: a C string literal and a local.
            let h = unsafe { open(c"S.WAV".as_ptr(), &mut tf) };
            assert_ne!(h, 0, "the first CAP opens must all succeed");
            assert!(!tf);
            handles.push(h);
        }

        let mut tf = false;
        // SAFETY: a C string literal and a local.
        let h = unsafe { open(c"S.WAV".as_ptr(), &mut tf) };
        assert_eq!(h, 0, "the CAP+1'th open must fail");
        assert!(tf, "and report table-full");
        assert_eq!(
            mock_backing::closed_handles(),
            std::vec![99],
            "the just-opened efatfs handle for the rejected slot must be closed, not leaked"
        );

        for h in handles {
            close(h);
        }
    }

    #[test]
    fn read_at_on_a_real_handle_forwards_the_call_and_returns_the_byte_count() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(55, true, false);
        let mut tf = false;
        // SAFETY: a C string literal and a local.
        let h = unsafe { open(c"S.WAV".as_ptr(), &mut tf) };
        assert_ne!(h, 0);
        mock_backing::set_read_result(10);

        let mut buf = [0u8; 16];
        // SAFETY: `buf` is a valid 16-byte local buffer.
        let n = unsafe { read_at(h, 1234, buf.as_mut_ptr(), buf.len() as u32) };
        assert_eq!(n, Ok(10));
        assert_eq!(mock_backing::last_read_call(), Some((55, 1234, 16)));

        close(h);
    }

    #[test]
    fn close_releases_the_assigned_asset_but_not_an_unassigned_one() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(21, true, false);

        // Slot with an assigned asset id: close must release it.
        let mut tf = false;
        // SAFETY: a C string literal and a local.
        let with_asset = unsafe { open(c"ASSIGNED.WAV".as_ptr(), &mut tf) };
        assert_ne!(with_asset, 0);
        set_asset_id(with_asset, 77);
        close(with_asset);
        assert_eq!(
            mock_backing::released_assets(),
            std::vec![77],
            "close must release the slot's assigned asset id"
        );

        // Slot with no asset id ever assigned: close must not call release at all.
        let mut tf2 = false;
        // SAFETY: a C string literal and a local.
        let without_asset = unsafe { open(c"UNASSIGNED.WAV".as_ptr(), &mut tf2) };
        assert_ne!(without_asset, 0);
        close(without_asset);
        assert_eq!(
            mock_backing::released_assets(),
            std::vec![77],
            "close on a slot with no asset id assigned must not call deluge_resource_release_asset"
        );
    }

    #[test]
    fn open_failure_propagates_table_full_and_returns_zero() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(0, false, true);
        let mut tf = false;
        // SAFETY: a C string literal and a local.
        let h = unsafe { open(c"MISSING.WAV".as_ptr(), &mut tf) };
        assert_eq!(h, 0);
        assert!(tf);
    }

    #[test]
    fn zero_and_out_of_range_handles_are_tolerated_everywhere() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        close(0);
        assert_eq!(asset_id(0), DELUGE_RESOURCE_NO_ASSET);
        set_asset_id(0, 5);
        set_geometry(0, sample_geometry());
        let mut buf = [0u8; 4];
        // SAFETY: `buf` is a valid 4-byte local buffer.
        let n = unsafe { read_at(0, 0, buf.as_mut_ptr(), buf.len() as u32) };
        assert_eq!(
            n,
            Err(DELUGE_ERR_PARAM),
            "an invalid handle is a failed read, not a 0-byte success"
        );

        let out_of_range = (CAP as u32) + 1000;
        close(out_of_range);
        assert_eq!(asset_id(out_of_range), DELUGE_RESOURCE_NO_ASSET);
    }
}
