//! The shared cluster-fill core.
//!
//! - [`fill_logic`]: the pure fill geometry and convert/stitch sequencing.
//! - [`chunk`]: streamed-chunk construction and payload resolution.
//! - The per-asset fill-context table ([`deluge_streaming_set_fill_context`],
//!   [`fill_context_for`]) and the `libdeluge/streaming_fill.h` boundary types.
//! - `native` (feature `native_fill`): the synchronous, manager-reaching fill.
#![no_std]

use core::cell::RefCell;
use core::ffi::c_void;

use embassy_sync::blocking_mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

pub mod chunk;
pub mod fill_logic;

// Per-asset fill-context table: C++ registers each sample's geometry at load; the fill and the
// sample readers read it back. The C-side contract is in `libdeluge/streaming_fill.h`.

/// Capacity of the fill-context table, keyed by asset id.
///
/// Must equal the resource manager's asset capacity, `kAssetCap` in
/// `general_memory_allocator.cpp`, so every asset id has a slot; the two are kept in sync by hand.
/// `DELUGE_RESOURCE_NO_ASSET` (`u32::MAX`) fails the same bounds check as any out-of-range id.
const FILL_CONTEXT_CAP: usize = 4096;

/// `DelugeHandle` from `libdeluge/types.h`: 0 is none, and a table slot crosses as `slot + 1`.
/// This crate cannot see the bindgen output, so the BSP's `abi::pins` checks the copy.
pub type DelugeHandle = u32;

/// `DelugeStatus` from `libdeluge/types.h`, for the efatfs calls the sibling crates import.
/// Checked by the BSP's `abi::pins`, like [`DelugeHandle`].
pub type DelugeStatus = i8;
/// `DelugeStatus`'s success value.
pub const DELUGE_OK: DelugeStatus = 0;
/// `DelugeStatus`'s invalid-argument value.
pub const DELUGE_ERR_PARAM: DelugeStatus = -2;

/// A sample's streaming geometry. Mirrors `DelugeStreamingFillContext` in
/// `libdeluge/streaming_fill.h`, which documents the fields.
///
/// `raw_data_format` is `RawDataFormat`'s `u8` representation.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct FillContext {
    pub efatfs_handle: DelugeHandle,
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub first_cluster_index_with_no_audio_data: i32,
    pub cluster_size: u32,
    pub cluster_size_magnitude: u32,
    pub raw_data_format: u8,
    /// Bytes per channel-sample (2 for 16-bit, 3 for 24-bit). Unused by the fill; the readers use
    /// it for frame stride.
    pub byte_depth: u8,
    /// Channel count (1 = mono, 2 = stereo). Unused by the fill; the readers use it for frame
    /// stride.
    pub num_channels: u8,
}

impl FillContext {
    /// The table's empty-slot sentinel. A real cluster size is a nonzero power of two, so
    /// `cluster_size == 0` marks an unregistered slot without the 8 bytes per entry an `Option`
    /// tag would cost.
    const UNREGISTERED: FillContext = FillContext {
        efatfs_handle: 0,
        audio_data_start_pos_bytes: 0,
        audio_data_length_bytes: 0,
        first_cluster_index_with_no_audio_data: 0,
        cluster_size: 0,
        cluster_size_magnitude: 0,
        raw_data_format: 0,
        byte_depth: 0,
        num_channels: 0,
    };
}

// Layout guard, matching the `static_assert`s in `async_fill.cpp`. No pointers, so the layout is
// the same on the 32-bit device and 64-bit hosts.
const _: () = {
    assert!(core::mem::offset_of!(FillContext, efatfs_handle) == 0);
    assert!(core::mem::offset_of!(FillContext, audio_data_start_pos_bytes) == 4);
    assert!(core::mem::offset_of!(FillContext, audio_data_length_bytes) == 8);
    assert!(core::mem::offset_of!(FillContext, first_cluster_index_with_no_audio_data) == 16);
    assert!(core::mem::offset_of!(FillContext, cluster_size) == 20);
    assert!(core::mem::offset_of!(FillContext, cluster_size_magnitude) == 24);
    assert!(core::mem::offset_of!(FillContext, raw_data_format) == 28);
    assert!(core::mem::offset_of!(FillContext, byte_depth) == 29);
    assert!(core::mem::offset_of!(FillContext, num_channels) == 30);
    assert!(size_of::<FillContext>() == 32);
};

/// The per-asset fill-context table, written at sample load and read by the fill and the readers.
///
/// Each access is one short `critical-section` lock around a 32-byte copy; the final artifact
/// supplies the `critical-section` implementation.
///
/// On device it lives in `.sdram_bss`: at 128 KiB it does not fit on-chip SRAM at debug
/// opt-levels. Boot zeroes that section before app code runs, which matches the all-
/// [`UNREGISTERED`](FillContext::UNREGISTERED) initializer.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static FILL_CONTEXTS: Mutex<CriticalSectionRawMutex, RefCell<[FillContext; FILL_CONTEXT_CAP]>> =
    Mutex::new(RefCell::new([FillContext::UNREGISTERED; FILL_CONTEXT_CAP]));

/// Registers or replaces `asset`'s fill context. See `libdeluge/streaming_fill.h`.
///
/// `_mgr` is unused (there is one process-wide resource manager) and kept for parity with the
/// manager-scoped C ABI. Out-of-range ids, including `DELUGE_RESOURCE_NO_ASSET`, and a zero
/// `cluster_size` are ignored.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streaming_set_fill_context(
    _mgr: *mut c_void,
    asset: u32,
    ctx: FillContext,
) {
    if asset as usize >= FILL_CONTEXT_CAP || ctx.cluster_size == 0 {
        return;
    }
    FILL_CONTEXTS.lock(|table| table.borrow_mut()[asset as usize] = ctx);
}

/// Returns `asset`'s registered fill context, or `None` if it was never registered or is out of
/// range.
pub fn fill_context_for(asset: u32) -> Option<FillContext> {
    if asset as usize >= FILL_CONTEXT_CAP {
        return None;
    }
    let ctx = FILL_CONTEXTS.lock(|table| table.borrow()[asset as usize]);
    (ctx.cluster_size != 0).then_some(ctx)
}

/// One sector read for the fill. Mirrors `StreamingFillDescriptor` in `libdeluge/streaming_fill.h`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct StreamingFillDescriptor {
    pub dest: *mut u8,
    pub num_sectors: u32,
    pub ok: bool,
    pub handle: DelugeHandle,
    pub byte_offset: u32,
}

// Layout guard, matching the `static_assert`s in `async_fill.cpp`: 20 bytes with 4-byte pointers,
// 24 with 8-byte ones.
const _: () = {
    assert!(core::mem::offset_of!(StreamingFillDescriptor, dest) == 0);
    assert!(core::mem::offset_of!(StreamingFillDescriptor, num_sectors) == size_of::<*mut u8>());
    assert!(core::mem::offset_of!(StreamingFillDescriptor, ok) == size_of::<*mut u8>() + 4);
    assert!(core::mem::offset_of!(StreamingFillDescriptor, handle) == size_of::<*mut u8>() + 8);
    assert!(
        core::mem::offset_of!(StreamingFillDescriptor, byte_offset) == size_of::<*mut u8>() + 12
    );
    assert!(size_of::<StreamingFillDescriptor>() == size_of::<*mut u8>() + 16);
};

/// A chunk's convert state, read and written through [`chunk::convert_state`] and
/// [`chunk::set_convert_state`]. Rust-only, so no `#[repr(C)]`.
#[derive(Clone, Copy)]
pub struct DelugeChunkConvertState {
    /// The payload's first 3 bytes before conversion: a neighbour's boundary stitch needs the bytes
    /// spanning the boundary as they were before this chunk converted in place.
    pub first_three_bytes: [u8; 3],
    /// Whether the start boundary has been stitched, so it is never stitched twice.
    pub start_converted: bool,
    /// Whether the end boundary has been stitched.
    pub end_converted: bool,
}

#[cfg(feature = "native_fill")]
pub mod native;
#[cfg(feature = "native_fill")]
pub use native::{native_begin, native_finish};
