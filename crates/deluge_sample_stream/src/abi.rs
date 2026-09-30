//! The `deluge_sample_stream_*` C-ABI (`include/libdeluge/sample_stream.h`). Thin wrappers over
//! [`crate::registry`] — each guards its `handle` against `0`/out-of-range/freed slots inside
//! `registry` itself (see that module's doc), so every wrapper here is a direct, uncomplicated
//! pass-through.

use core::ffi::c_char;

use deluge_sample_fill::{DelugeHandle, DelugeStatus, DELUGE_OK};

use crate::registry;
use crate::DelugeSampleStreamGeometry;

/// Open `path` for streaming reads and register a fresh slot for it. `0` on failure. See
/// [`registry::open`] for the full contract, including `out_table_full`'s always-written
/// semantics.
///
/// # Safety
/// `path` must be a valid, NUL-terminated C string. `out_table_full`, if non-null, must be valid
/// for one write.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub unsafe extern "C" fn deluge_sample_stream_open(
    path: *const c_char,
    out_table_full: *mut bool,
) -> DelugeHandle {
    registry::open(path, out_table_full)
}

/// Release `handle`'s slot: close its efatfs handle (if any) and release its resource-manager
/// asset (if one was ever assigned). Idempotent — a no-op on `0` or an already-closed handle.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub extern "C" fn deluge_sample_stream_close(handle: DelugeHandle) {
    registry::close(handle);
}

/// Store `geo` on `handle`'s slot, registering its streaming fill-context with the resource
/// manager once both a geometry and a real asset id are present. No-op on an invalid `handle`.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub extern "C" fn deluge_sample_stream_set_geometry(
    handle: DelugeHandle,
    geo: DelugeSampleStreamGeometry,
) {
    registry::set_geometry(handle, geo);
}

/// `handle`'s currently assigned resource-manager asset id, or `DELUGE_RESOURCE_NO_ASSET`
/// (`0xFFFFFFFF`) on an invalid handle or one with no asset id assigned yet.
///
/// Named `_get_` (not the plain `deluge_sample_stream_asset_id` its sibling setter's naming would
/// suggest): `sample_stream.h` also declares an unrelated, already-exported
/// `deluge_sample_stream_asset_id(DelugeStreamBacking*)` (`sample_stream.cpp`'s
/// `SampleStream*`-keyed accessor, consumed by `deluge_sample_source::abi`) — reusing that exact
/// symbol name here for an incompatible signature would be a duplicate-symbol link error the
/// moment both crates link into the same binary.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub extern "C" fn deluge_sample_stream_get_asset_id(handle: DelugeHandle) -> u32 {
    registry::asset_id(handle)
}

/// Assign `id` as `handle`'s resource-manager asset, registering its streaming fill-context once
/// both an asset id and a geometry are present. No-op on an invalid `handle`.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub extern "C" fn deluge_sample_stream_set_asset_id(handle: DelugeHandle, id: u32) {
    registry::set_asset_id(handle, id);
}

/// Read up to `len` bytes at `byte_offset` of `handle`'s open file into `buf`. On success writes the
/// bytes actually read to `*out_read` (0 is a legitimate read at/after EOF) and returns `DELUGE_OK`;
/// on a failed read returns `DELUGE_ERR_PARAM` for an invalid/out-of-range/freed handle, or the
/// underlying efatfs read's own status, leaving `*out_read` untouched. Mirrors
/// `deluge_efatfs_read_at`'s success/byte-count split. See [`registry::read_at`] for the full
/// contract.
///
/// # Safety
/// `buf`, if `len > 0`, must be valid for at least `len` writable bytes; `out_read` must be a valid
/// writable `u32`.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub unsafe extern "C" fn deluge_sample_stream_read_at(
    handle: DelugeHandle,
    byte_offset: u32,
    buf: *mut u8,
    len: u32,
    out_read: *mut u32,
) -> DelugeStatus {
    // SAFETY: forwarded from this fn's own contract.
    match unsafe { registry::read_at(handle, byte_offset, buf, len) } {
        Ok(n) => {
            if !out_read.is_null() {
                // SAFETY: `out_read` non-null and writable per this fn's own contract.
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
