//! deluge_sample_stream — the streaming-file slot registry behind
//! `include/libdeluge/sample_stream.h`'s `deluge_sample_stream_*` C-ABI. A
//! fixed-capacity table (`registry`) of open efatfs streaming handles, each pairing an efatfs
//! read handle with the resource-manager asset id + geometry a caller supplies once known,
//! registering the asset's `DelugeStreamingFillContext` with the resource manager itself the
//! moment both are present — reproducing `SampleStream::register_fill_context`'s own no-op-until-
//! defined behaviour natively. See `registry`'s module doc for the slot semantics and `abi` for
//! the C-ABI wrappers.
#![no_std]

use core::ffi::{c_char, c_void};

pub mod abi;
pub mod registry;

/// Mirrors `deluge_resource.h`'s `DELUGE_RESOURCE_NO_ASSET` — the "no asset yet" sentinel a
/// freshly opened slot starts with (`registry::Slot::asset_id`) until
/// `deluge_sample_stream_set_asset_id` assigns a real one. A separate literal, not a dependency on
/// `deluge_resource` (this crate reaches the manager only through late-bound `unsafe extern "C"`
/// symbols — see the crate's Cargo.toml doc), matching this workspace's established "each side
/// re-mirrors the one constant it needs" convention (`deluge_sample_reader::reader`'s own
/// `UNKNOWN_LENGTH_SENTINEL` doc).
pub const DELUGE_RESOURCE_NO_ASSET: u32 = 0xFFFF_FFFF;

/// Mirrors `include/libdeluge/sample_stream.h`'s `DelugeSampleStreamGeometry` exactly (verbatim
/// field order/types): `include/libdeluge/streaming_fill.h`'s `DelugeStreamingFillContext` minus
/// `efatfs_handle` — the registry supplies that field itself, from the slot's own efatfs handle,
/// when it assembles the full fill-context (see `registry::try_register_fill_context`). A
/// separate type from [`DelugeStreamingFillContext`] below: the geometry crosses this crate's own
/// C-ABI, while the fill-context crosses only the consumed `deluge_streaming_set_fill_context`.
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

/// `include/libdeluge/streaming_fill.h`'s `DelugeStreamingFillContext`, the struct
/// `deluge_streaming_set_fill_context` takes by value: `deluge_sample_fill`'s definition, which
/// exports that function. Only `registry::try_register_fill_context` builds one, from a slot's own
/// efatfs handle plus its stored [`DelugeSampleStreamGeometry`].
pub(crate) use deluge_sample_fill::FillContext as DelugeStreamingFillContext;

// ── Externs this crate consumes — real bodies elsewhere (efatfs_fs.rs / efatfs_host_shim.rs, the
// resource manager's own C ABI); mocked under `#[cfg(test)]` by `mock_backing` below, exactly as
// `deluge_sample_reader::lib`'s own `host_streaming_stubs` mocks its externs for its crate's tests.
unsafe extern "C" {
    /// `include/libdeluge/file_io.h`. Open a sample file for streaming reads.
    pub fn deluge_efatfs_open(
        path: *const c_char,
        out_handle: *mut deluge_sample_fill::DelugeHandle,
        out_table_full: *mut bool,
    ) -> bool;
    /// `include/libdeluge/file_io.h`. Close a streaming file handle.
    pub fn deluge_efatfs_close(handle: deluge_sample_fill::DelugeHandle);
    /// `include/libdeluge/file_io.h`. Synchronous card read at an absolute byte offset.
    pub fn deluge_efatfs_read_at(
        handle: deluge_sample_fill::DelugeHandle,
        byte_offset: u32,
        dst: *mut c_void,
        count: u32,
        out_read: *mut u32,
    ) -> bool;
    /// `include/libdeluge/sample_stream.h`. The single process-wide resource-manager singleton.
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    /// `include/libdeluge/streaming_fill.h`. Register (or replace) an asset's fill-context.
    pub fn deluge_streaming_set_fill_context(
        mgr: *mut c_void,
        asset: u32,
        ctx: DelugeStreamingFillContext,
    );
    /// `deluge_resource.h`. Retire an asset, freeing any of its still-resident chunks.
    fn deluge_resource_release_asset(mgr: *mut c_void, asset: u32);
}

/// Safe stand-ins for this crate's six consumed externs, driving an in-test fake — since a plain
/// `cargo test` on this crate links none of the real efatfs/resource-manager bodies. Mirrors
/// `deluge_sample_reader::lib`'s own `host_streaming_stubs` module (one crate-level `#[cfg(test)]`
/// module providing every stub definition every test module needs — a `#[no_mangle]` symbol may
/// only be defined once in a linked binary, so this can't be duplicated per test module).
#[cfg(test)]
pub(crate) mod mock_backing {
    extern crate std;

    use core::ffi::{c_char, c_void};
    use std::cell::RefCell;
    use std::ffi::CStr;
    use std::string::{String, ToString};
    use std::vec::Vec;

    use crate::DelugeStreamingFillContext;

    /// A stand-in "resource manager" pointer: never dereferenced by this crate (it's only ever
    /// forwarded to `deluge_streaming_set_fill_context`/`deluge_resource_release_asset`, both
    /// mocked here too), so any fixed non-null value proves `try_register_fill_context`'s own
    /// null-manager guard doesn't spuriously trip.
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

    /// Serialises the tests that open registry slots. The mocks' state is per thread, but the
    /// registry is one process-wide table, and a test that fills every slot of it fails any test that
    /// opens a slot while it runs. Hold the guard for the whole test.
    pub(crate) fn serial() -> std::sync::MutexGuard<'static, ()> {
        static SERIAL: std::sync::Mutex<()> = std::sync::Mutex::new(());
        SERIAL
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }

    /// Clear every recorded/configured bit of mock state — call at the top of every test (this
    /// module's state is `thread_local`, so `cargo test`'s per-test thread already isolates
    /// concurrent tests from each other; `reset()` just gives each test a clean slate on ITS OWN
    /// thread, matching the sibling crates' own harness convention).
    pub(crate) fn reset() {
        OPEN_RESULT.with(|c| *c.borrow_mut() = (0, true, false));
        LAST_OPENED_PATH.with(|c| c.borrow_mut().clear());
        CLOSED_HANDLES.with(|c| c.borrow_mut().clear());
        READ_RESULT_BYTES.with(|c| *c.borrow_mut() = 0);
        LAST_READ_CALL.with(|c| *c.borrow_mut() = None);
        LAST_FILL_CONTEXT.with(|c| *c.borrow_mut() = None);
        RELEASED_ASSETS.with(|c| c.borrow_mut().clear());
    }

    /// Configure the next (and every subsequent, until reset) `deluge_efatfs_open` call's result.
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

    /// Configure how many bytes the next (and every subsequent, until reset) `deluge_efatfs_read_at`
    /// call reports read — the call still always "succeeds" (returns `true`); a registry-level
    /// failed-read case has no test in this rung (mirrors the header's own "false only on a real
    /// I/O failure" contract, which this mock has no need to model beyond the count it hands back).
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

    /// How many times `deluge_streaming_set_fill_context` has been called since the last reset —
    /// lets a test assert registration happened exactly once, not merely that the last call's
    /// payload looks right.
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
    ) -> bool {
        // SAFETY: `path` is a live, NUL-terminated C string for the duration of this call (every
        // caller in this crate's own tests passes a `c"..."` literal or an equivalent static).
        let s = unsafe { CStr::from_ptr(path) }
            .to_string_lossy()
            .to_string();
        LAST_OPENED_PATH.with(|c| *c.borrow_mut() = s);
        let (handle, ok, table_full) = OPEN_RESULT.with(|c| *c.borrow());
        if ok {
            // SAFETY: `out_handle` is the caller's valid local out-param (this crate's own
            // `registry::open`, the sole caller).
            unsafe { *out_handle = handle };
        }
        if !out_table_full.is_null() {
            // SAFETY: non-null per the check above.
            unsafe { *out_table_full = table_full };
        }
        ok
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_efatfs_close(handle: u32) {
        CLOSED_HANDLES.with(|c| c.borrow_mut().push(handle));
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_efatfs_read_at(
        handle: u32,
        byte_offset: u32,
        dst: *mut c_void,
        count: u32,
        out_read: *mut u32,
    ) -> bool {
        LAST_READ_CALL.with(|c| *c.borrow_mut() = Some((handle, byte_offset, count)));
        let bytes = READ_RESULT_BYTES.with(|c| *c.borrow()).min(count);
        // SAFETY: `dst` is the caller's destination buffer, valid for at least `count` bytes
        // (this crate's own `registry::read_at`, the sole caller); `bytes <= count` by
        // construction above.
        unsafe {
            let d = dst as *mut u8;
            for i in 0..bytes {
                *d.add(i as usize) = byte_offset.wrapping_add(i) as u8;
            }
            *out_read = bytes;
        }
        true
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
