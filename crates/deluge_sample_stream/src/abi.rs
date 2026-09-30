//! The `deluge_sample_stream_*` C ABI (`libdeluge/sample_stream.h`): pass-through wrappers over
//! [`registry`], which validates every handle.

use core::ffi::c_char;

use deluge_sample_fill::{DelugeHandle, DelugeStatus, DELUGE_OK};

use crate::registry;
use crate::DelugeSampleStreamGeometry;

/// Opens `path` for streaming reads in a fresh slot; returns `0` on failure. See
/// [`registry::open`] for `out_table_full`.
///
/// # Safety
///
/// `path` must be a valid, NUL-terminated C string. `out_table_full`, if non-null, must be valid
/// for one write.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_stream_open(
    path: *const c_char,
    out_table_full: *mut bool,
) -> DelugeHandle {
    // SAFETY: this function's own contract is `registry::open`'s.
    unsafe { registry::open(path, out_table_full) }
}

/// Closes `handle`'s efatfs handle and releases its asset, if one was assigned. A no-op on `0` or
/// an already-closed handle.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub extern "C" fn deluge_sample_stream_close(handle: DelugeHandle) {
    registry::close(handle);
}

/// Stores `geo` on `handle`'s slot, registering its fill-context once an asset id is also
/// present. A no-op on an invalid handle.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub extern "C" fn deluge_sample_stream_set_geometry(
    handle: DelugeHandle,
    geo: DelugeSampleStreamGeometry,
) {
    registry::set_geometry(handle, geo);
}

/// Returns `handle`'s asset id, or `DELUGE_RESOURCE_NO_ASSET` for an invalid handle or one with
/// no asset yet.
///
/// Named `_get_` because `deluge_sample_stream_asset_id(DelugeStreamBacking*)` is a different,
/// C++-defined symbol in the same header.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub extern "C" fn deluge_sample_stream_get_asset_id(handle: DelugeHandle) -> u32 {
    registry::asset_id(handle)
}

/// Assigns asset `id` to `handle`'s slot, registering its fill-context once a geometry is also
/// present. A no-op on an invalid handle.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub extern "C" fn deluge_sample_stream_set_asset_id(handle: DelugeHandle, id: u32) {
    registry::set_asset_id(handle, id);
}

/// Reads up to `len` bytes at `byte_offset` of `handle`'s file into `buf`.
///
/// On success writes the byte count to `*out_read` (0 at or past EOF) and returns `DELUGE_OK`.
/// On failure returns `DELUGE_ERR_PARAM` for an invalid handle, or the efatfs read status, and
/// leaves `*out_read` untouched.
///
/// # Safety
///
/// If `len > 0`, `buf` must be valid for `len` writable bytes. `out_read` must be null or valid
/// for one write.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_stream_read_at(
    handle: DelugeHandle,
    byte_offset: u32,
    buf: *mut u8,
    len: u32,
    out_read: *mut u32,
) -> DelugeStatus {
    // SAFETY: `buf`/`len` are covered by this function's contract.
    match unsafe { registry::read_at(handle, byte_offset, buf, len) } {
        Ok(n) => {
            if !out_read.is_null() {
                // SAFETY: non-null, and writable per this function's contract.
                unsafe { *out_read = n };
            }
            DELUGE_OK
        }
        Err(status) => status,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mock_backing;
    use crate::DELUGE_RESOURCE_NO_ASSET;
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
    fn abi_wrappers_round_trip_through_the_registry() {
        let _serial = mock_backing::serial();
        mock_backing::reset();
        mock_backing::set_open_result(11, true, false);
        let mut tf = false;
        // SAFETY: a real `c"..."` literal path; `&mut tf` is a valid local out-param.
        let h = unsafe { deluge_sample_stream_open(c"WIRE.WAV".as_ptr(), &mut tf) };
        assert_ne!(h, 0);
        assert!(!tf);

        assert_eq!(
            deluge_sample_stream_get_asset_id(h),
            DELUGE_RESOURCE_NO_ASSET
        );
        deluge_sample_stream_set_asset_id(h, 3);
        assert_eq!(deluge_sample_stream_get_asset_id(h), 3);
        deluge_sample_stream_set_geometry(h, sample_geometry());

        let (asset, ctx) = mock_backing::last_fill_context().expect("registered once both are set");
        assert_eq!(asset, 3);
        assert_eq!(ctx.efatfs_handle, 11);

        mock_backing::set_read_result(4);
        let mut buf = [0u8; 4];
        let mut got: u32 = 0;
        // SAFETY: `buf` is a valid 4-byte local buffer; `got` is a valid out-param.
        let status = unsafe {
            deluge_sample_stream_read_at(h, 0, buf.as_mut_ptr(), buf.len() as u32, &mut got)
        };
        assert_eq!(status, deluge_sample_fill::DELUGE_OK);
        assert_eq!(got, 4);

        deluge_sample_stream_close(h);
        assert_eq!(mock_backing::closed_handles(), std::vec![11]);
    }
}
