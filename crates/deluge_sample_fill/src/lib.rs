//! The shared cluster-fill core. See Cargo.toml's header for the crate's role.
//! `fill_logic` provides the pure geometry math; the fill-context table is defined below; the
//! `native_fill`-gated sync fill core is `mod native` at the bottom of this file.
#![no_std]

use core::cell::RefCell;
use core::ffi::c_void;

use embassy_sync::blocking_mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

pub mod chunk;
pub mod fill_logic;

// ── Always-compiled C ABI: per-asset fill-context table ─────
// Registered by C++ at sample-load (`deluge_streaming_define_asset()`/`SampleStream::open_read_stream()`,
// see `chunk_residency.cpp`/`sample_stream.cpp`);
// read by the native fill task via [`fill_context_for`] (`prod::ProdOps::begin`/`finish`).
// See `include/libdeluge/streaming_fill.h`'s doc for the C-side contract.

/// Fixed capacity for the per-asset fill-context table, keyed by asset id. Mirrors `kAssetCap`
/// (`general_memory_allocator.cpp`) — the resource manager's asset-table capacity — so every asset
/// id the manager can ever hand out has a slot here. A plain literal, not read from the C++ side (the
/// two are kept in sync by hand); if `kAssetCap` is ever raised, raise this too. A `DELUGE_RESOURCE_NO_ASSET`
/// (`u32::MAX`) id is naturally rejected by the same bounds check as any other out-of-range id.
///
/// At this size (4096 × 32 bytes = 128 KiB) the table does NOT fit in on-chip SRAM at debug
/// opt-levels — measured: `cargo device`'s `dev`-profile link failed with the SRAM region
/// overflowing (a `.bss` placement here pushed `.ARM.exidx` past the RTT reservation), while the
/// SAME table built clean under `--release` (see `linker/memory_rtt.x`'s own "a large live feature
/// ... needs [release-like density] to fit at debug opt-levels" caveat — this table just measurably
/// hit that ceiling too). Rather than shrink the table below a size that would make it useless for
/// real per-asset coverage, [`FILL_CONTEXTS`] is placed in SDRAM instead (`.sdram_bss`, 64 MiB, a
/// rounding error at this size) on device — the same fix already used for
/// `audio::RENDER_BLOCK`/`INPUT_BLOCK`, and `ffi_extra::NEWLIB_HEAP`.
const FILL_CONTEXT_CAP: usize = 4096;

/// Mirrors `include/libdeluge/streaming_fill.h`'s `DelugeStreamingFillContext` exactly (verbatim
/// field order/types) — the per-asset geometry `deluge_streaming_set_fill_context` registers at
/// sample-load and the native fill task reads via [`fill_context_for`]. See that header's doc for
/// what each field means; `raw_data_format` mirrors
/// `RawDataFormat` (`audio_file_format.h`) as its `u8` underlying representation, not re-exposed as a
/// Rust enum here (nothing on this side interprets it yet).
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct FillContext {
    pub efatfs_handle: u32,
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub first_cluster_index_with_no_audio_data: i32,
    pub cluster_size: u32,
    pub cluster_size_magnitude: u32,
    pub raw_data_format: u8,
    /// Bytes per channel-sample (e.g. 2 for 16-bit, 3 for 24-bit). Not read by the fill
    /// (`to_fill_geometry` drops it) — carried here so the reader can resolve frame
    /// stride Rust-side, by asset.
    pub byte_depth: u8,
    /// Channel count (1 = mono, 2 = stereo). Not read by the fill (`to_fill_geometry` drops
    /// it) — carried here so the reader can resolve frame stride Rust-side, by asset.
    pub num_channels: u8,
}

impl FillContext {
    /// The table's "unregistered" sentinel: `cluster_size == 0` never occurs in a real registration
    /// (`Cluster::size` is always a nonzero power of two — see `sample_stream.cpp`'s
    /// `register_fill_context`), so it's used in place of `Option<FillContext>`'s discriminant. That
    /// saves the tag's padding (`Option<FillContext>` is 40 bytes vs. this struct's own 32 — see the
    /// FFI layout guard below) across [`FILL_CONTEXT_CAP`] entries, which matters here: see that
    /// constant's doc for why this table is on a tight SRAM budget.
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

/// FFI layout guard, mirroring the `static_assert`s in `async_fill.cpp` — see that file's comment
/// for the byte-offset derivation. Unlike [`StreamingFillDescriptor`], this struct holds no pointer,
/// so the layout is identical on the 32-bit device and the 64-bit host_app build: two leading `u32`s,
/// then the `u64` (already 8-aligned), then `i32`/`u32`/`u32`, then the trailing `u8`, padded up to
/// the `u64` member's 8-byte alignment.
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

/// The per-asset fill-context table: written on the main/load thread
/// (`deluge_streaming_set_fill_context`, called from `deluge_streaming_define_asset()`/
/// `SampleStream::open_read_stream()` at sample-load) and read by `streaming_loader::streaming_fill_task`
/// (`streaming_loader::prod::ProdOps::begin`/`finish`, via [`fill_context_for`]). Neither side ever runs
/// on the audio render ISR — asset definition happens at sample-load, and the fill task runs on the
/// same thread-mode Embassy executor the C++ enqueue path does — so this table doesn't need the
/// resource manager's asymmetric ISR-skipping `Masked` critical section (`deluge_resource::sync`).
/// Using it here would also be the wrong dependency: this crate is a plain `no_std` rlib with its own
/// test suite (`tests/fill_context_host.rs`), which does NOT link `deluge_resource` — only
/// `deluge-bsp-rust` (via `host_app`) pulls that crate in. A plain `embassy_sync` blocking
/// `Mutex<CriticalSectionRawMutex, _>` needs no new mechanism and no new dependency, and is available
/// in every context this crate compiles in (device, `host_app`, and this crate's own host tests
/// alike).
///
/// `.sdram_bss` on device only (see [`FILL_CONTEXT_CAP`]'s doc for why): that section is zeroed by
/// `boot_mem::init_sdram_memory()` before any app code runs (including this table's first possible
/// writer/reader), so the all-zero initializer below — every entry `FillContext::UNREGISTERED` — is
/// exactly what's already there; the explicit initializer just keeps `host_app`/host-test behaviour
/// (plain `.bss`, zeroed by the normal C runtime/process image) identical in substance.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static FILL_CONTEXTS: Mutex<CriticalSectionRawMutex, RefCell<[FillContext; FILL_CONTEXT_CAP]>> =
    Mutex::new(RefCell::new([FillContext::UNREGISTERED; FILL_CONTEXT_CAP]));

/// Register (or replace) asset `asset`'s streaming fill-context. See
/// `include/libdeluge/streaming_fill.h`'s doc for the C-side contract; `mgr` is unused today (there
/// is exactly one process-wide resource manager) but kept in the signature for parity with the rest
/// of the manager-scoped C ABI. Out-of-range asset ids (`>= FILL_CONTEXT_CAP`, which also catches
/// `DELUGE_RESOURCE_NO_ASSET`) are silently ignored, as is the pathological `ctx.cluster_size == 0`
/// (indistinguishable from [`FillContext::UNREGISTERED`] — see its doc; never occurs from the real
/// C++ caller).
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

/// Look up `asset`'s registered fill-context, or `None` if it was never registered (or `asset` is out
/// of range). Read side of [`deluge_streaming_set_fill_context`]; wired into
/// `streaming_loader::prod::ProdOps::begin`/`finish` (its real, non-test caller), and
/// exercised directly by this crate's own `tests/fill_context_host.rs`.
pub fn fill_context_for(asset: u32) -> Option<FillContext> {
    if asset as usize >= FILL_CONTEXT_CAP {
        return None;
    }
    let ctx = FILL_CONTEXTS.lock(|table| table.borrow()[asset as usize]);
    (ctx.cluster_size != 0).then_some(ctx)
}

/// Mirrors `include/libdeluge/streaming_fill.h`'s `StreamingFillDescriptor`
/// exactly (verbatim field order/types) — this is the C-ABI boundary type.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct StreamingFillDescriptor {
    pub dest: *mut u8,
    pub num_sectors: u32,
    pub ok: bool,
    pub handle: u32,
    pub byte_offset: u32,
}

/// FFI layout guard, mirroring the `static_assert`s in `async_fill.cpp` — see that file's
/// comment for the byte-offset derivation. `core::mem::offset_of!` + `size_of` are both `const`,
/// so this is a compile-time check with no runtime cost; a field-order/type drift on either side
/// fails the build instead of silently corrupting the read across the boundary. After `ok` (1 byte
/// at ptr+4) come 3 pad bytes, then `handle` at ptr+8 and `byte_offset` at ptr+12, and the struct
/// pads up to the pointer's alignment → ptr+16 (20 on the 4-byte-ptr device, 24 on the 8-byte-ptr
/// host).
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

/// The per-chunk convert-state: `first_three_bytes` is the PRE-conversion first 3 raw bytes of the
/// chunk's payload (a neighbour's boundary stitch needs the byte pattern spanning the cluster
/// boundary before this chunk's own in-place conversion overwrote it); `start_converted`/
/// `end_converted` are idempotency guards so a boundary is never re-stitched once it's already been
/// handled from the other side. A plain in-crate type: it no longer crosses the C-ABI
/// — [`crate::chunk::convert_state`]/[`crate::chunk::set_convert_state`] read/write it directly
/// against the Rust-owned `StreamedChunk`, and `native_finish` (`native.rs`) is its only other
/// consumer — so it carries no `#[repr(C)]`/layout guard.
#[derive(Clone, Copy)]
pub struct DelugeChunkConvertState {
    pub first_three_bytes: [u8; 3],
    pub start_converted: bool,
    pub end_converted: bool,
}

// ── native_fill-gated sync fill core ───────────────────────────
// See `native`'s own module doc for what/why. Default-off (see this crate's Cargo.toml
// `[features]` doc); `deluge-bsp-rust` and `region_fill_differential` both turn it on.
#[cfg(feature = "native_fill")]
mod native;
#[cfg(feature = "native_fill")]
pub use native::{native_begin, native_finish};
