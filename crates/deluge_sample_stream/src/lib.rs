//! The streaming-file slot registry behind the `deluge_sample_stream_*` C ABI
//! (`libdeluge/sample_stream.h`).
//!
//! [`registry`] holds a fixed table of open efatfs streaming handles. Each slot pairs a read handle
//! with the resource-manager asset id and geometry the C++ `SampleStream` facade supplies, and
//! registers the asset's `DelugeStreamingFillContext` itself once both are present. [`abi`] holds
//! the C-ABI wrappers.
#![no_std]

use core::ffi::{c_char, c_void};

pub mod abi;
pub mod registry;

/// `DELUGE_RESOURCE_NO_ASSET` from `deluge_resource.h`: the asset id of a slot that has none yet.
///
/// Mirrored by hand: this crate reaches the resource manager only through late-bound externs,
/// never a `deluge_resource` dependency.
pub const DELUGE_RESOURCE_NO_ASSET: u32 = 0xFFFF_FFFF;

/// Mirrors `DelugeSampleStreamGeometry` (`libdeluge/sample_stream.h`) field for field.
///
/// This is `DelugeStreamingFillContext` minus `efatfs_handle`, which the registry fills in from
/// the slot's own handle when it builds the context.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct DelugeSampleStreamGeometry {
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub first_cluster_index_with_no_audio_data: i32,
    pub cluster_size: u32,
    pub cluster_size_magnitude: u32,
    pub raw_data_format: u8,
    pub byte_depth: u8,
    pub num_channels: u8,
}

/// `DelugeStreamingFillContext` (`libdeluge/streaming_fill.h`), as passed by value to
/// `deluge_streaming_set_fill_context`.
pub(crate) use deluge_sample_fill::FillContext as DelugeStreamingFillContext;

// Externs this crate consumes: efatfs from the BSP (`storage::efatfs`), the rest from the
// resource manager's C ABI. Mocked under `cfg(test)` by `mock_backing`.
unsafe extern "C" {
    /// Opens a sample file for streaming reads (`libdeluge/file_io.h`).
    pub fn deluge_efatfs_open(
        path: *const c_char,
        out_handle: *mut deluge_sample_fill::DelugeHandle,
        out_table_full: *mut bool,
    ) -> deluge_sample_fill::DelugeStatus;
    /// Closes a streaming file handle (`libdeluge/file_io.h`).
    pub fn deluge_efatfs_close(handle: deluge_sample_fill::DelugeHandle);
    /// Reads synchronously from the card at an absolute byte offset (`libdeluge/file_io.h`).
    pub fn deluge_efatfs_read_at(
        handle: deluge_sample_fill::DelugeHandle,
        byte_offset: u32,
        dst: *mut c_void,
        count: u32,
        out_read: *mut u32,
    ) -> deluge_sample_fill::DelugeStatus;
    /// Returns the process-wide resource manager (`libdeluge/sample_stream.h`).
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    /// Registers or replaces an asset's fill-context (`libdeluge/streaming_fill.h`).
    pub fn deluge_streaming_set_fill_context(
        mgr: *mut c_void,
        asset: u32,
        ctx: DelugeStreamingFillContext,
    );
    /// Retires an asset, freeing its resident chunks (`deluge_resource.h`).
    fn deluge_resource_release_asset(mgr: *mut c_void, asset: u32);
}

/// In-test fakes for the consumed externs, which a plain `cargo test` does not link.
///
/// One crate-level module: a `#[no_mangle]` symbol may be defined only once per binary.
#[cfg(test)]
pub(crate) mod mock_backing {
    extern crate std;

    use core::ffi::{c_char, c_void};
    use std::cell::RefCell;
    use std::ffi::CStr;
    use std::string::{String, ToString};
    use std::vec::Vec;

    use crate::DelugeStreamingFillContext;

    /// A non-null stand-in manager pointer. Never dereferenced; it only has to pass the
    /// registry's null-manager guard.
    fn fake_manager() -> *mut c_void {
        core::ptr::dangling_mut::<c_void>()
    }

    std::thread_local! {
        static OPEN_RESULT: RefCell<(u32, bool, bool)> = const { RefCell::new((0, true, false)) };
        static LAST_OPENED_PATH: RefCell<String> = const { RefCell::new(String::new()) };
        static CLOSED_HANDLES: RefCell<Vec<u32>> = const { RefCell::new(Vec::new()) };
        static READ_RESULT_BYTES: RefCell<u32> = const { RefCell::new(0) };
        static LAST_READ_CALL: RefCell<Option<(u32, u32, u32)>> = const { RefCell::new(None) };
        static LAST_FILL_CONTEXT: RefCell<Option<(u32, DelugeStreamingFillContext)>> =
            const { RefCell::new(None) };
        static RELEASED_ASSETS: RefCell<Vec<u32>> = const { RefCell::new(Vec::new()) };
    }

    /// Serialises tests that open registry slots.
    ///
    /// The mock state is per thread, but the registry is process-wide: a test that fills every slot
    /// fails any test that opens one meanwhile. Hold the guard for the whole test.
    pub(crate) fn serial() -> std::sync::MutexGuard<'static, ()> {
        static SERIAL: std::sync::Mutex<()> = std::sync::Mutex::new(());
        SERIAL
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }

    /// Clears this thread's mock state. Call at the top of every test.
    pub(crate) fn reset() {
        OPEN_RESULT.with(|c| *c.borrow_mut() = (0, true, false));
        LAST_OPENED_PATH.with(|c| c.borrow_mut().clear());
        CLOSED_HANDLES.with(|c| c.borrow_mut().clear());
        READ_RESULT_BYTES.with(|c| *c.borrow_mut() = 0);
        LAST_READ_CALL.with(|c| *c.borrow_mut() = None);
        LAST_FILL_CONTEXT.with(|c| *c.borrow_mut() = None);
        RELEASED_ASSETS.with(|c| c.borrow_mut().clear());
    }

    /// Sets the result of every `deluge_efatfs_open` call until the next reset.
    pub(crate) fn set_open_result(handle: u32, ok: bool, table_full: bool) {
        OPEN_RESULT.with(|c| *c.borrow_mut() = (handle, ok, table_full));
    }

    /// The path passed to the most recent `deluge_efatfs_open` call.
    pub(crate) fn last_opened_path() -> String {
        LAST_OPENED_PATH.with(|c| c.borrow().clone())
    }

    /// Every handle passed to `deluge_efatfs_close` so far, in call order.
    pub(crate) fn closed_handles() -> Vec<u32> {
        CLOSED_HANDLES.with(|c| c.borrow().clone())
    }

    /// Sets the byte count every `deluge_efatfs_read_at` call reports until the next reset. The
    /// call always returns `DELUGE_OK`.
    pub(crate) fn set_read_result(bytes: u32) {
        READ_RESULT_BYTES.with(|c| *c.borrow_mut() = bytes);
    }

    /// The `(handle, byte_offset, count)` of the most recent `deluge_efatfs_read_at` call.
    pub(crate) fn last_read_call() -> Option<(u32, u32, u32)> {
        LAST_READ_CALL.with(|c| *c.borrow())
    }

    /// The `(asset, ctx)` of the most recent `deluge_streaming_set_fill_context` call.
    pub(crate) fn last_fill_context() -> Option<(u32, DelugeStreamingFillContext)> {
        LAST_FILL_CONTEXT.with(|c| *c.borrow())
    }

    /// The number of `deluge_streaming_set_fill_context` calls since the last reset.
    pub(crate) fn fill_context_call_count() -> usize {
        LAST_FILL_CONTEXT_CALLS.with(|c| *c.borrow())
    }

    /// Every asset id passed to `deluge_resource_release_asset` so far, in call order.
    pub(crate) fn released_assets() -> Vec<u32> {
        RELEASED_ASSETS.with(|c| c.borrow().clone())
    }

    std::thread_local! {
        static LAST_FILL_CONTEXT_CALLS: RefCell<usize> = const { RefCell::new(0) };
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_efatfs_open(
        path: *const c_char,
        out_handle: *mut u32,
        out_table_full: *mut bool,
    ) -> deluge_sample_fill::DelugeStatus {
        // SAFETY: every caller in this crate's tests passes a `c"..."` literal.
        let s = unsafe { CStr::from_ptr(path) }
            .to_string_lossy()
            .to_string();
        LAST_OPENED_PATH.with(|c| *c.borrow_mut() = s);
        let (handle, ok, table_full) = OPEN_RESULT.with(|c| *c.borrow());
        if ok {
            // SAFETY: the sole caller, `registry::open`, passes a valid local out-param.
            unsafe { *out_handle = handle };
        }
        if !out_table_full.is_null() {
            // SAFETY: non-null per the check above.
            unsafe { *out_table_full = table_full };
        }
        // `DELUGE_ERR_NOT_FOUND`: what a real open of a missing file answers.
        const DELUGE_ERR_NOT_FOUND: deluge_sample_fill::DelugeStatus = -8;
        if ok {
            deluge_sample_fill::DELUGE_OK
        } else {
            DELUGE_ERR_NOT_FOUND
        }
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_efatfs_close(handle: deluge_sample_fill::DelugeHandle) {
        CLOSED_HANDLES.with(|c| c.borrow_mut().push(handle));
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_efatfs_read_at(
        handle: deluge_sample_fill::DelugeHandle,
        byte_offset: u32,
        dst: *mut c_void,
        count: u32,
        out_read: *mut u32,
    ) -> deluge_sample_fill::DelugeStatus {
        LAST_READ_CALL.with(|c| *c.borrow_mut() = Some((handle, byte_offset, count)));
        let bytes = READ_RESULT_BYTES.with(|c| *c.borrow()).min(count);
        // SAFETY: `registry::read_at`, the sole caller, passes `dst` valid for `count` bytes, and
        // `bytes <= count`.
        unsafe {
            let d = dst as *mut u8;
            for i in 0..bytes {
                *d.add(i as usize) = byte_offset.wrapping_add(i) as u8;
            }
            *out_read = bytes;
        }
        deluge_sample_fill::DELUGE_OK
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_resource_manager() -> *mut c_void {
        fake_manager()
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_set_fill_context(
        _mgr: *mut c_void,
        asset: u32,
        ctx: DelugeStreamingFillContext,
    ) {
        LAST_FILL_CONTEXT.with(|c| *c.borrow_mut() = Some((asset, ctx)));
        LAST_FILL_CONTEXT_CALLS.with(|c| *c.borrow_mut() += 1);
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_resource_release_asset(_mgr: *mut c_void, asset: u32) {
        RELEASED_ASSETS.with(|c| c.borrow_mut().push(asset));
    }
}
