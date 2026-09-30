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

#[cfg(all(target_os = "none", feature = "host-runtime"))]
compile_error!(
    "`host-runtime` is the host binary's executor; build the device with \
     `--no-default-features --features rtt` (`cargo device` and `dbt build` do)"
);

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

/// The C ABI's plumbing and its build-time checks. First, because its macros are textually scoped.
#[macro_use]
pub mod abi;

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
pub mod sys {
    include!(concat!(env!("OUT_DIR"), "/libdeluge_sys.rs"));
}
/// Host stand-in for the above (no C++ app / bindgen output off-target). Used
/// unless `host_app` is enabled, in which case the real bindgen output below
/// takes over.
#[cfg(all(not(target_os = "none"), not(feature = "host_app")))]
#[path = "abi/sys_host.rs"]
pub mod sys;
/// `host_app` feature: the real host-ABI bindgen output (see build.rs), for
/// when the host-built C++ `deluge_app` object closure is linked in too.
/// Replaces the `sys_host.rs` stand-ins above with the genuine generated
/// types.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
pub mod sys {
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
#[path = "abi/sys_host.rs"]
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

/// The boot latch (`libdeluge/boot.h`): set once, right after `deluge_app_init` returns.
pub mod boot;
/// C++ memory-model bring-up (SDRAM bss/data, global ctors).
#[cfg(target_os = "none")]
mod boot_mem;
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
/// Host-only no-op stubs for the peripheral (MIDI/audio/CV-gate/signals) C ABI
/// under `host_app`. Boot-and-idle never exercises real peripheral I/O on host;
/// the real `audio`/`cv_gate`/`midi`/`signals` modules below stay device-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod host_link_stubs;
/// libdeluge/scheduler.h — the cooperative task scheduler, implemented on the
/// Embassy executor (one task per registered Deluge task).
pub mod scheduler;
/// Real impls of the simplest services (system.h, clock.h, memory.h).
mod services;
/// Which execution tier is running the caller (`libdeluge/tier.h`).
pub mod tier;
/// Counts synchronous card calls made after boot on the interaction tier (`libdeluge/tier.h`).
pub mod tier_meter;

/// Platform bring-up and the executors: the device entry and the host harness entry.
/// Audio I/O.
pub mod audio;
/// The single-flight storage channels and the loaders that answer through them.
pub mod channel;
/// The board's peripherals.
pub mod peripherals;
/// The SD card and its filesystem.
pub mod storage;

pub mod runtime;

/// The deluge-sdk board support crate, for harnesses that drive the board directly.
pub use deluge_bsp;
