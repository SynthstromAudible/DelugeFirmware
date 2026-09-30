//! The stream-slot table: a fixed-capacity registry of open efatfs streaming handles, each
//! carrying the resource-manager asset id + geometry a caller supplies once known. Handles are
//! `slot_index + 1` (`0` stays reserved as the invalid/fail handle, mirroring
//! `deluge_sample_source`'s own pool convention) — every entry point below null/range-guards its
//! `handle` argument before touching [`REGISTRY`].
//!
//! # Fill-context registration
//! [`set_geometry`]/[`set_asset_id`] both funnel through [`try_register_fill_context`], which is a
//! no-op until BOTH a real asset id and a geometry have been stored on the slot — reproducing
//! `SampleStream::register_fill_context`'s own no-op-until-defined behaviour (see that fn's own
//! doc) rather than the two setters racing to register a half-built context.

use core::cell::RefCell;
use core::ffi::{c_char, c_void};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::blocking_mutex::Mutex;

use deluge_sample_fill::{DelugeStatus, DELUGE_ERR_PARAM, DELUGE_OK};

use crate::{DelugeSampleStreamGeometry, DelugeStreamingFillContext, DELUGE_RESOURCE_NO_ASSET};

/// Fixed slot-table capacity. Set to 256 to match the sibling `deluge_sample_source` crate's own
/// fixed-pool size; still to be confirmed against `AudioFileManager`'s own audio-file cap.
pub const CAP: usize = 256;

/// One registered streaming handle: the efatfs read handle (always a real, open handle — a slot
/// exists only after a successful `deluge_efatfs_open`, which never hands out `DELUGE_HANDLE_NONE`;
/// a still-recording sample never opens a slot at all), the resource-manager
/// asset id ([`DELUGE_RESOURCE_NO_ASSET`] until [`set_asset_id`] assigns one), and the geometry
/// [`set_geometry`] stores, if any.
struct Slot {
    efatfs_handle: deluge_sample_fill::DelugeHandle,
    asset_id: u32,
    geometry: Option<DelugeSampleStreamGeometry>,
}

/// The slot table itself. A plain blocking `Mutex<CriticalSectionRawMutex, _>`, not
/// `deluge_resource::sync::Masked`: this crate has no dependency on `deluge_resource` (it reaches
/// the manager only through the late-bound externs in `lib.rs`), and neither `open`/`close`/
/// `set_geometry`/`set_asset_id` (main/load thread) nor `read_at` (the synchronous
/// fill) ever run on the audio render ISR — mirrors `deluge_sample_fill::FILL_CONTEXTS`'s own
/// choice and rationale (see that static's doc).
static REGISTRY: Mutex<CriticalSectionRawMutex, RefCell<[Option<Slot>; CAP]>> =
    Mutex::new(RefCell::new([const { None }; CAP]));

/// `handle - 1` as a table index, or `None` for the reserved invalid handle (`0`) or an
/// out-of-range one. Shared by every entry point below.
fn slot_index(handle: u32) -> Option<usize> {
    if handle == 0 {
        return None;
    }
    let index = (handle - 1) as usize;
    (index < CAP).then_some(index)
}

/// Write `value` through `out`, if non-null.
///
/// # Safety
/// `out`, if non-null, must be valid for one write.
unsafe fn write_out(out: *mut bool, value: bool) {
    if !out.is_null() {
        // SAFETY: non-null per the check above; validity is this fn's own caller-supplied
        // contract.
        unsafe { *out = value };
    }
}

/// Open `path` for streaming reads and register a fresh slot for it. See
/// `deluge_sample_stream_open`'s header doc (`include/libdeluge/sample_stream.h`) for the full
/// contract.
///
/// `out_table_full`, if non-null, is always written (mirrors `deluge_efatfs_open`'s own contract):
/// `true` iff the failure was specifically this registry's own table being full (as opposed to
/// `deluge_efatfs_open` itself failing, e.g. its own handle-table full or the file not existing) —
/// distinguishing the two lets a caller map this registry's own exhaustion to a dedicated error
/// rather than a misleading "file not found".
// `path`/`out_table_full`'s validity are this fn's own caller-supplied contract (documented on
// each internal SAFETY comment below, mirroring the C-ABI wrapper's own doc); this crate's fixed
// registry shape keeps every raw-pointer touch here, so the deref is silenced rather than
// promoted to `unsafe fn` -- the same choice `deluge_sample_fill::native::native_begin` makes for
// its own C-ABI-facing pointer arg.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub fn open(path: *const c_char, out_table_full: *mut bool) -> u32 {
    let mut handle: u32 = 0;
    let mut efatfs_table_full = false;
    // SAFETY: `path` is forwarded verbatim to `deluge_efatfs_open` — this fn never itself
    // interprets it, so `path`'s validity is exactly that extern's own contract, forwarded from
    // this fn's own caller; `&mut handle`/`&mut efatfs_table_full` are valid local out-params.
    let opened = unsafe { crate::deluge_efatfs_open(path, &mut handle, &mut efatfs_table_full) };
    if opened != DELUGE_OK {
        // SAFETY: `out_table_full`'s validity is this fn's own caller-supplied contract.
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
            // SAFETY: see the fn-level SAFETY note above.
            unsafe { write_out(out_table_full, false) };
            (index + 1) as u32
        }
        None => {
            // No free slot: never silently drop the just-opened efatfs handle -- close it,
            // report table-full, fail.
            // SAFETY: `handle` is the live handle `deluge_efatfs_open` just returned above, not
            // yet closed or handed to any slot.
            unsafe { crate::deluge_efatfs_close(handle) };
            // SAFETY: see the fn-level SAFETY note above.
            unsafe { write_out(out_table_full, true) };
            0
        }
    }
}

/// Release `handle`'s slot: close its efatfs handle (if any) and release its resource-manager
/// asset (if one was ever assigned). Idempotent — a no-op on `0`, an out-of-range handle, or a
/// handle already closed.
pub fn close(handle: u32) {
    let Some(index) = slot_index(handle) else {
        return;
    };
    let Some(slot) = REGISTRY.lock(|table| table.borrow_mut()[index].take()) else {
        return; // Already closed -- idempotent no-op.
    };
    // Always close the slot's efatfs handle. A registry slot only ever exists after a successful
    // `deluge_efatfs_open` (see `open`), so `efatfs_handle` is always a real handle.
    // SAFETY: `slot.efatfs_handle` was returned live by `deluge_efatfs_open` in `open` and has not
    // been closed since -- this slot held the only copy of it, and `take()` above already cleared the
    // slot for any re-entrant/concurrent `close` on the same handle.
    unsafe { crate::deluge_efatfs_close(slot.efatfs_handle) };
    if slot.asset_id != DELUGE_RESOURCE_NO_ASSET {
        // SAFETY: the process-wide resource-manager singleton, live for the process's remaining
        // life once non-null (mirrors every other crate's own use of this extern).
        let mgr = unsafe { crate::deluge_streaming_resource_manager() };
        if !mgr.is_null() {
            // SAFETY: `mgr` non-null per the check above; `slot.asset_id` was registered through
            // this same manager by `try_register_fill_context`.
            unsafe { crate::deluge_resource_release_asset(mgr, slot.asset_id) };
        }
    }
}

/// Store `geo` on `handle`'s slot, then attempt fill-context registration
/// ([`try_register_fill_context`]). No-op on an invalid/out-of-range/freed `handle`.
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

/// `handle`'s currently assigned resource-manager asset id, or [`DELUGE_RESOURCE_NO_ASSET`] on an
/// invalid/out-of-range/freed handle or a slot that hasn't been assigned one yet.
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

/// Store `id` on `handle`'s slot, then attempt fill-context registration
/// ([`try_register_fill_context`]). No-op on an invalid/out-of-range/freed `handle`.
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

/// Read up to `len` bytes at `byte_offset` of `handle`'s open file into `buf`. Returns `Ok(n)`
/// with the bytes actually read -- `Ok(0)` is a legitimate read at/after EOF, NOT a failure -- or
/// the failure's `DelugeStatus`: `DELUGE_ERR_PARAM` for an invalid/out-of-range/freed `handle`,
/// otherwise `deluge_efatfs_read_at`'s own. Mirroring `deluge_efatfs_read_at`'s own
/// success/byte-count split keeps the C++ `SampleStreamReadSource` a faithful forward of
/// `EfatfsReadSource` (EOF -> `Ok(0)`, failure -> `Err`) rather than collapsing the two into a
/// bare `0`.
///
/// # Safety
/// `buf`, if `len > 0`, must be valid for at least `len` writable bytes.
pub unsafe fn read_at(
    handle: u32,
    byte_offset: u32,
    buf: *mut u8,
    len: u32,
) -> Result<u32, DelugeStatus> {
    let index = slot_index(handle).ok_or(DELUGE_ERR_PARAM)?;
    // `None` => freed slot.
    let efatfs_handle = REGISTRY
        .lock(|table| {
            table.borrow()[index]
                .as_ref()
                .map(|slot| slot.efatfs_handle)
        })
        .ok_or(DELUGE_ERR_PARAM)?;
    // No "still recording" short-circuit here. A registry slot is created only AFTER a successful
    // `deluge_efatfs_open` (see `open`), so its handle is always a real, readable one. A
    // still-recording sample never opens a registry slot at all (it registers its handle-less
    // fill-context directly with the resource manager via the facade's no-slot path).
    let mut out_read: u32 = 0;
    // SAFETY: `buf` valid for `len` bytes per this fn's own contract; `out_read` is a valid local
    // out-param.
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

/// Register `index`'s slot's fill-context with the resource manager, once BOTH a real asset id
/// and a geometry are present -- see the module doc's "Fill-context registration" section.
/// Otherwise a no-op (matching `register_fill_context`'s own no-op-until-defined behaviour).
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
    // SAFETY: the process-wide resource-manager singleton (mirrors every other crate's own use of
    // this extern).
    let mgr = unsafe { crate::deluge_streaming_resource_manager() };
    if mgr.is_null() {
        return; // No manager yet (e.g. boot ordering) -- nothing to register through.
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
    // SAFETY: `mgr` non-null per the check above.
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
        let h = crate::registry::open(c"SAMPLES/X.WAV".as_ptr(), &mut tf);
        assert_ne!(h, 0);
        assert_eq!(mock_backing::last_opened_path(), "SAMPLES/X.WAV");
        crate::registry::close(h);
        assert_eq!(mock_backing::closed_handles(), std::vec![42]);
        // second close is a no-op
        crate::registry::close(h);
        assert_eq!(mock_backing::closed_handles(), std::vec![42]);
    }

    /// (a): `set_geometry` before `set_asset_id` registers exactly ONCE, on the second call, with
    /// a ctx whose `efatfs_handle` is the slot's own handle and geometry fields match what was
    /// passed.
    #[test]
    fn fill_context_registers_once_on_the_second_of_geometry_then_asset_id() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(7, true, false);
        let mut tf = false;
        let h = open(c"SAMPLES/Y.WAV".as_ptr(), &mut tf);
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

    /// (b): opening CAP+1 streams: the last returns 0 with `*out_table_full == true`, and the
    /// extra efatfs handle is closed (never silently dropped).
    #[test]
    fn table_full_on_the_capacity_plus_one_open_closes_the_extra_handle() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(99, true, false);

        let mut handles = std::vec::Vec::new();
        for _ in 0..CAP {
            let mut tf = false;
            let h = open(c"S.WAV".as_ptr(), &mut tf);
            assert_ne!(h, 0, "the first CAP opens must all succeed");
            assert!(!tf);
            handles.push(h);
        }

        let mut tf = false;
        let h = open(c"S.WAV".as_ptr(), &mut tf);
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

    /// (d): `read_at` on a real handle forwards the byte_offset/len and returns the mock's byte
    /// count.
    #[test]
    fn read_at_on_a_real_handle_forwards_the_call_and_returns_the_byte_count() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(55, true, false);
        let mut tf = false;
        let h = open(c"S.WAV".as_ptr(), &mut tf);
        assert_ne!(h, 0);
        mock_backing::set_read_result(10);

        let mut buf = [0u8; 16];
        // SAFETY: `buf` is a valid 16-byte local buffer.
        let n = unsafe { read_at(h, 1234, buf.as_mut_ptr(), buf.len() as u32) };
        assert_eq!(n, Ok(10));
        assert_eq!(mock_backing::last_read_call(), Some((55, 1234, 16)));

        close(h);
    }

    /// `close`'s asset-release branch: a slot with a real asset id assigned releases it through
    /// `deluge_resource_release_asset` on close; a slot with no asset id ever assigned (still
    /// `DELUGE_RESOURCE_NO_ASSET`) must NOT call it at all.
    #[test]
    fn close_releases_the_assigned_asset_but_not_an_unassigned_one() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(21, true, false);

        // Slot with an assigned asset id: close must release it.
        let mut tf = false;
        let with_asset = open(c"ASSIGNED.WAV".as_ptr(), &mut tf);
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
        let without_asset = open(c"UNASSIGNED.WAV".as_ptr(), &mut tf2);
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
        let h = open(c"MISSING.WAV".as_ptr(), &mut tf);
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
