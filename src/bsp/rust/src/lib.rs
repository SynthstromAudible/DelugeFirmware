//! Rust/Embassy firmware image for the Synthstrom Deluge (RZ/A1L).
//!
//! The Deluge's BSP, on the device and on the host (the harnesses and `dbt sim`). It
//! OWNS reset, `main`/`_start` and the Embassy executor: the HAL startup
//! (`rza1l_hal` `_start` → `_reset_handler` → `bl main`) lands in [`main`] here,
//! which brings up the platform on the deluge-sdk HAL+BSP and calls the portable
//! C++ application's `deluge_app_init()`, whose registered tasks the executor runs.
//!
//! The C++ application is compiled by CMake into `libdeluge_app.a` and linked in
//! by `build.rs`; this crate implements the `<libdeluge/...>` C-ABI services it
//! calls. See docs/dev/libdeluge_bsp_design.md.
#![cfg_attr(target_os = "none", no_std)]
#![cfg_attr(target_os = "none", no_main)]
#![feature(impl_trait_in_assoc_type)]
// `audio_load_task`'s future nests the load driver's async calls inside the
// `LoadOps` trait's own `async fn` futures, and `embassy_executor::task` makes
// rustc compute the layout of the whole `TaskPool<Fut>` in one go. That exceeds
// the default 128-deep query limit ("queries overflow the depth limit" while
// computing the layout of `TaskPool<..audio_load_task::Fut..>`), which is a
// compiler budget, not a sign of runaway recursion — the future is a fixed,
// finite size. Raise it rather than flattening the driver into one function and
// losing the structure that makes finish/abandon exactly-once.
#![recursion_limit = "512"]
// The plain host build (no `host_app`) compiles the storage channels and loaders for their host
// tests, but not the C++ app that calls them, so their C-ABI halves are unreachable there.
#![cfg_attr(not(any(target_os = "none", feature = "host_app")), allow(dead_code))]
// The storage traits run on one executor and are never required to be `Send`.
#![allow(async_fn_in_trait)]

// Link-only: the C++ app (archived into this image by build.rs) calls the
// deluge_resource_* residency C ABI and the deluge_{alloc,slab_*} allocator C ABI.
// `extern crate` forces both rlibs onto the link line so those #[no_mangle]
// symbols resolve; nothing here references them from Rust. deluge_alloc arrives
// transitively via deluge_resource (a distinct crate from the `deluge_alloc`
// aliased above — that one is the sibling deluge-sdk allocator).
#[cfg(target_os = "none")]
extern crate deluge_resource;
/// `host_app` feature: host-side sibling of the above. The host-built C++
/// `deluge_app` object closure (build.rs's `run_host_app`) calls the same
/// deluge_resource_*/deluge_{alloc,slab_*,heap_*} C ABI; `deluge_resource` is an
/// optional dep enabled by `host_app` (see Cargo.toml) so this `extern crate`
/// forces its rlib onto the host link line too.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_resource;

// Link-only, same reasoning as the `deluge_resource` pair above: the C++ app's
// parseSampleHeader/parseWaveTableHeader (storage/audio/audio_file_format.cpp) call the
// `deluge_audio_header_*` C ABI, whose sole definition is this crate's `abi.rs` -- there is no
// weak C++ fallback -- so without this `extern crate` rustc/lld would drop the rlib from the link.
#[cfg(target_os = "none")]
extern crate deluge_audio_format;
/// `host_app` feature: host-side sibling of the above — see that `extern crate`'s doc.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_audio_format;

// Link-only, same reasoning as the `deluge_resource` pair above. The C++ reader
// (sample_low_level_reader.cpp) CALLS the `deluge_sample_source_*`/`deluge_sample_region_*` C ABI;
// `deluge_sample_source`'s crate (`abi.rs`) is the sole definition of those symbols -- there is no
// C++ fallback in sample_source.cpp -- gated `cfg(any(target_os = "none", feature =
// "host_app"))` to match these two `extern crate` arms exactly. Without this, rustc/lld would never pull
// `deluge_sample_source`'s single-object rlib into the link at all (nothing in this crate's own Rust
// code references it), leaving those C ABI symbols undefined at link time on device/host_app.
#[cfg(target_os = "none")]
extern crate deluge_sample_source;
/// `host_app` feature: host-side sibling of the above — see that `extern crate`'s doc.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_sample_source;

// Link-only, same reasoning as the `deluge_sample_source` pair above. Its `abi.rs`
// defines the lifecycle trio of the `deluge_sample_reader_*` C ABI (`include/libdeluge/
// sample_reader.h`), gated identically. Nothing calls it yet (no consumer has migrated to it), so
// without this `extern crate` rustc/lld would drop the whole rlib from the link; this just
// proves the symbols are present and compile clean end-to-end ahead of a real caller being wired up.
#[cfg(target_os = "none")]
extern crate deluge_sample_reader;
/// `host_app` feature: host-side sibling of the above — see that `extern crate`'s doc.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_sample_reader;

// Link-only, same reasoning as the `deluge_sample_reader` pair above. Its `abi.rs`
// will define the `deluge_sample_stream_*` C ABI (`include/libdeluge/
// sample_stream.h`). Nothing calls it yet (no consumer has migrated to it), so
// without this `extern crate` rustc/lld would drop the whole rlib from the link; this just proves
// the symbols are present and compile clean end-to-end ahead of the facade wiring a real caller.
#[cfg(target_os = "none")]
extern crate deluge_sample_stream;
/// `host_app` feature: host-side sibling of the above — see that `extern crate`'s doc.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
extern crate deluge_sample_stream;

/// `abi_pin!`/`abi_const!`/`abi_layout!`: the checking variants where `sys` is the bindgen output, the no-ops where
/// it is `sys_host.rs`. See `abi_pin_macros.rs`.
#[cfg(any(target_os = "none", feature = "host_app"))]
#[macro_use]
#[path = "abi_pin_macros.rs"]
mod abi_pin_macros;
#[cfg(not(any(target_os = "none", feature = "host_app")))]
#[macro_use]
#[path = "abi_pin_noop.rs"]
mod abi_pin_macros;

/// The ABI's handle rule: 0 is none, and a table slot crosses as `slot + 1`. Compiled unconditionally; every
/// module that hands a table handle across the ABI converts through it.
pub mod abi_handle;

/// The libdeluge C ABI generated from include/libdeluge/*.h: POD types,
/// `DELUGE_*` constants, and a declaration of every header function (the
/// crate's own definitions of the service functions live in [`ffi`] and the
/// modules beside it). A header type an in-tree crate defines is that crate's
/// type here, re-exported under the header's name (see `build_crate_types.rs`).
/// No C++ app is linked on host
/// unless `host_app` is enabled (see build.rs), so there is no bindgen output
/// to include.
#[cfg(target_os = "none")]
#[allow(non_camel_case_types, non_upper_case_globals, dead_code)]
mod sys {
    include!(concat!(env!("OUT_DIR"), "/libdeluge_sys.rs"));
}
/// Host stand-in for the above (no C++ app / bindgen output off-target). Used
/// unless `host_app` is enabled, in which case the real bindgen output below
/// takes over.
#[cfg(all(not(target_os = "none"), not(feature = "host_app")))]
#[path = "sys_host.rs"]
mod sys;
/// `host_app` feature: the real host-ABI bindgen output (see build.rs), for
/// when the host-built C++ `deluge_app` object closure is linked in too.
/// Replaces the `sys_host.rs` stand-ins above with the genuine generated
/// types.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod sys {
    #![allow(
        non_upper_case_globals,
        non_camel_case_types,
        non_snake_case,
        dead_code
    )]
    include!(concat!(env!("OUT_DIR"), "/libdeluge_sys.rs"));
}
/// `sys_host.rs` compiled again beside the real `sys`, so its pins check the plain host build's stand-ins against
/// the headers.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[path = "sys_host.rs"]
mod sys_host_check;

/// The header's own definitions of the types `sys` binds to an in-tree crate's types (see `build_crate_types.rs`),
/// which `abi_layout!` checks the crates' definitions against.
#[cfg(any(target_os = "none", feature = "host_app"))]
#[allow(
    non_upper_case_globals,
    non_camel_case_types,
    non_snake_case,
    dead_code
)]
mod sys_layout {
    include!(concat!(env!("OUT_DIR"), "/libdeluge_layout.rs"));
}

/// Signature and value pins for what this crate's modules do not define themselves: the in-tree crates' exports,
/// the Rust side's own declarations of header functions, and the constants copied by hand.
#[cfg(any(target_os = "none", feature = "host_app"))]
mod abi_pins;

/// audio_io.h — duplex block audio over the SSI0 DMA rings.
#[cfg(target_os = "none")]
mod audio;
/// audio_io.h — host null-sink render pump: the priority-0 task actually
/// calls `deluge_app_render` and discards the output, instead of the no-op
/// stub in `host_link_stubs.rs`.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod audio_host;
/// The async audio-file load task and its request/collect C ABI: owns the AWAITED
/// file I/O of a card sample's load — open a candidate, size it, walk its header
/// — and publishes a plain result the C++ side collects on its own tier. It calls
/// no C++ itself; see its module doc for the two constraints that force that. Compiled
/// on the Embassy BSP unconditionally (device, or host under `host_app`) so
/// `deluge_audio_load_request`/`_collect` are always defined --
/// the app declares no fallback for them, so omitting this module would leave them
/// undefined at link time. The loader has no switch -- see its module doc's "Always on".
pub mod audio_loader;
/// The drive's render-window pacing, which `audio` runs.
pub mod audio_pacing;
/// board.h — capability descriptor + GPIO/audio/CV bring-up. Compiled on host
/// too under `host_app` (the descriptor/probe are pure data/logic; the GPIO/CV
/// bring-up bodies get host no-op siblings — see board.rs).
#[cfg(any(target_os = "none", feature = "host_app"))]
mod board;
/// The boot latch (`libdeluge/boot.h`): set once, right after `deluge_app_init` returns.
pub mod boot;
/// C++ memory-model bring-up (SDRAM bss/data, global ctors).
#[cfg(target_os = "none")]
mod boot_mem;
/// Capture stamps: where on the TX play timeline an input event happened. See its module doc.
pub mod capture_stamp;
/// card.h — `deluge_card_flush`, the one ejection hook every loader on this BSP is
/// invalidated through. Gated identically to `audio_loader` above, which it calls into: the
/// symbol has to exist wherever a loader's tables do.
pub mod card;
/// control_surface.h — pads/buttons/encoders + LEDs (M2b WIP).
mod control;
/// cv_gate.h — CV/gate outputs + external trigger clock.
#[cfg(target_os = "none")]
mod cv_gate;
/// display.h — main OLED output over deluge_bsp::oled.
mod display;
/// Storage-generic, host-testable core of the efatfs read path
/// (`HandleTable` + `FileContext` detach/reattach + generation guard + fill
/// loop). `efatfs_fs` wraps it with the device statics/mutexes/FFI; host tests
/// drive it directly. Also reachable under `host_app`
/// (any target) so `efatfs_host_shim` — the
/// host counterpart of `efatfs_fs` — can reuse it unchanged; see that module's
/// doc.
#[cfg(any(target_os = "none", feature = "host_app"))]
mod efatfs_core;
/// The single-owner `embedded-fatfs` mount — one `FileSystem` behind an
/// async `Mutex`, the only way live code touches the vendored FS.
/// `mount()`/`with_fs()` are invoked from this crate's boot path and the
/// streaming read path.
#[cfg(target_os = "none")]
mod efatfs_fs;
/// Host counterpart of `efatfs_fs` — mounts `embedded-fatfs` over a
/// block device backed by the SD_BUS-guarded async helpers (see that
/// module's doc) so a host harness can measure the real efatfs read path.
/// Test infrastructure only; no device path touched.
#[cfg(feature = "host_app")]
mod efatfs_host_shim;
/// `block_device_driver::BlockDevice<512>` over the real SD driver
/// (`deluge_bsp::sd`), feeding the `BufStream`/`embedded-fatfs` stack —
/// device-only counterpart of `fs_differential`'s host `FileBlockDevice`.
#[cfg(target_os = "none")]
mod fat_block_device;
/// `libdeluge/fault.h`: the board half of the portable pad-grid crash reporter — the
/// blocking PIC transport and the code/stack ranges, both usable from a fault vector.
#[cfg(target_os = "none")]
mod fault;
/// The libdeluge C-ABI service implementations the C++ app calls (stubs).
/// Compiled on host too under `host_app` (bodies are already host-safe).
#[cfg(any(target_os = "none", feature = "host_app"))]
mod ffi;
/// Non-header app/BSP symbols (USB-host globals, FatFS glue, NE10, runtime shims).
/// Compiled on host too under `host_app`; `_sbrk`/`_fini` stay device-only
/// (host glibc/crt provide them) and the linker-boundary stand-ins
/// (`program_stack_*`, `__frunk_*`) are host-only (device gets them from the
/// linker script).
#[cfg(any(target_os = "none", feature = "host_app"))]
mod ffi_extra;
/// file_bytes.h — the whole-file byte channel: read, write or rename a small settings file on the
/// storage tier and hand the BYTES to a caller that must not wait on the card. Gated identically to
/// `file_directory` above, and to `card` — which reaches this module's `flush_tables` and so must
/// never be compiled without it. The four settings consumers (runtime feature settings, the MIDI
/// device table, MIDI follow's defaults, performance view) always enter this channel after boot: it
/// has no feature switch.
///
/// Unlike `file_prefix` it borrows nothing from `audio_loader`: reading a whole file and writing one
/// back are not `LoadOps`'s four operations, so it declares its own three-operation trait.
pub mod file_bytes;
/// file_directory.h — the single-slot directory channel: open a directory on the storage tier and
/// hand the ready handle to a caller that must not wait on the card. Gated identically to
/// `file_prefix` below, and to `card` — which reaches this module's `flush_tables` and so must
/// never be compiled without it. It has no feature switch: after boot, every browser listing, SysEx directory
/// request and recording-folder scan opens its directory through it.
///
/// Unlike `file_prefix` it borrows nothing from `audio_loader`: a directory open is not one of
/// `LoadOps`'s four file operations, so it declares its own two-operation trait.
pub mod file_directory;
/// file_prefix.h — the single-slot prefix channel: read the first N bytes of a file on the
/// storage tier and hand them to a caller that must not wait on the card. Gated identically to
/// `audio_loader` above, whose `LoadOps`/`ProdOps` its driver reuses, and to `card` — which
/// reaches this module's `flush_tables` and so must never be compiled without it. Its callers are
/// `LoadSongUI`'s song-artwork preview and `DxSyxBrowser`'s cartridge load. The channel has no
/// feature switch.
pub mod file_prefix;
/// The stream channel: request/collect over the recorder's stream-write handles. Compiled
/// unconditionally; the channel has no feature switch.
pub mod file_stream;
/// The stream channel's answer slot: pure, so it is host-tested without a card. Compiled unconditionally,
/// beside `file_stream` above, which it is the storage for.
pub mod file_stream_slot;
/// flash.h — persistent settings flash over deluge_bsp::flash / spibsc.
mod flash;
/// Host-only no-op stubs for the peripheral (MIDI/audio/CV-gate/signals) C ABI
/// under `host_app`. Boot-and-idle never exercises real peripheral I/O on host;
/// the real `audio`/`cv_gate`/`midi`/`signals` modules below stay device-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod host_link_stubs;
/// The emulator's card as a directory: a memory disk filled from it, written back when the emulator closes.
#[cfg(feature = "sim")]
mod memory_card;
/// midi_io.h — DIN MIDI over deluge_bsp::uart (+ USB-MIDI peripheral, see usb).
#[cfg(target_os = "none")]
mod midi;
/// Does a FINALIZED multi-cluster recording's residency table get sized correctly,
/// and does region index 1+ read back correctly through the region port on THIS target? See its
/// module doc. `host_app`-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod recorder_finalize_probe;
/// Does a still-recording sample's live read-back resolve on THIS target? See its
/// module doc. `host_app`-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod recorder_probe;
/// Streaming-underrun harness: the reusable, thread-agnostic scenario driver
/// (load a real song, start playback, start a concurrent recording, step N audio
/// blocks) — see its module doc. `host_app`-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod scenario;
/// libdeluge/scheduler.h — the cooperative task scheduler, implemented on the
/// Embassy executor (one task per registered Deluge task).
mod scheduler;
/// block_device.h — the C-ABI SD-card entry points (`deluge_block_ready`/
/// `deluge_block_read`/`deluge_block_write`) and the shared `SD_BUS`/async
/// helpers over `deluge_bsp::sd`. The app's real transfers go through
/// `fat_block_device::SdBlockDevice` (device) and
/// `efatfs_host_shim::HostSdBlockDevice` (host) instead — see `sd.rs`'s
/// module doc.
pub mod sd;
/// Streaming-underrun harness: packs a real FAT SD image from the golden
/// harness's song/sample corpus for [`scenario`] to load. `host_app`-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod sd_image;
/// Content-addressed naming/reuse policy for [`sd_image`]'s temp FAT images. Split out so it
/// stays plain-host testable without the `host_app` CMake build (tests/sd_image_path_host.rs).
#[cfg(not(target_os = "none"))]
pub mod sd_image_path;
/// Real impls of the simplest services (system.h, clock.h, memory.h).
mod services;
/// signals.h — board GPIO signals, battery, MIDI/gate timer.
#[cfg(target_os = "none")]
mod signals;
/// The blocking-I/O seam: a plain non-yielding block on device, and on host a spin driven
/// by a harness-registered progress hook. See the module doc for why host needs the hook.
pub mod sim_block;
/// The async cluster-fill task and its wakeup C ABI: drains the resource
/// manager's loader queue on this executor, awaiting the SD read instead of
/// running it inline. Compiled on the Embassy BSP unconditionally (device, or
/// host under `host_app`).
pub mod streaming_loader;
/// Which execution tier is running the caller (`libdeluge/tier.h`).
pub mod tier;
/// Counts synchronous card calls made after boot on the interaction tier (`libdeluge/tier.h`).
pub mod tier_meter;
/// USB device bring-up — USB-MIDI 1.0 peripheral (Deluge → computer).
#[cfg(target_os = "none")]
mod usb;

/// Platform bring-up and the executors: the device entry and the host harness entry.
pub mod runtime;
