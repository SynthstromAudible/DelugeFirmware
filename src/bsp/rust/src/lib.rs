//! The Deluge's board support package in Rust, on Embassy.
//!
//! This crate implements the `libdeluge/*.h` C ABI that the portable C++ application calls, on the
//! deluge-sdk HAL and BSP, and runs the application's tasks on Embassy executors. The thin binary
//! (`src/main.rs`) calls into [`runtime`].
//!
//! # Configurations
//!
//! - **Device** (`target_os = "none"`): owns reset. The HAL's `_start` lands in the binary's
//!   `main`, which calls `runtime::start`: platform bring-up, then `deluge_app_init()`. The C++
//!   application (`libdeluge_app.a`) is linked in by the build script.
//! - **Host** (default features): the library and its tests, with no C++ app linked. `sys` is the
//!   hand-written stand-in `abi/sys_host.rs`, and the ABI pins are no-ops.
//! - **`host_app`**: the host binary also links the host-built C++ app, and `sys` is bindgen output
//!   for the host ABI.
//! - **`sim`**: `host_app` running as the brain of deluge-sdk's desktop simulator (`dbt sim`).
//!
//! Without `host-runtime` (as `golden_vt_render` uses it), the library brings no executor or time
//! driver of its own.
//!
//! # Modules
//!
//! - [`abi`], `sys`: the C ABI's plumbing, its build-time checks, and the header bindings.
//! - [`runtime`], [`scheduler`], [`boot`], [`tier`], [`tier_meter`]: bring-up, executors, and which
//!   tier is running.
//! - [`audio`], [`peripherals`]: the board's I/O.
//! - [`storage`], [`channel`]: the SD card, its filesystem, and the storage tier's channels.
//! - `ffi`, `ffi_extra`, `services`, `host_link_stubs`: the remaining C-ABI services and link
//!   symbols.
#![cfg_attr(target_os = "none", no_std)]
#![feature(impl_trait_in_assoc_type)]
// Computing the layout of `audio_load_task`'s `TaskPool<Fut>` (nested `async fn` trait futures)
// exceeds the default query depth of 128. The future is finite; flattening the driver to fit
// would lose the structure that makes finish/abandon exactly-once.
#![recursion_limit = "512"]
// The plain host build (no `host_app`) compiles the storage channels and loaders for their host
// tests, but not the C++ app that calls them, so their C-ABI halves are unreachable there.
#![cfg_attr(not(has_app), allow(dead_code))]
// The storage traits run on one executor and are never required to be `Send`.
#![allow(async_fn_in_trait)]

// `host_app` and `sim` are host features: a device build with either would define every host C-ABI
// export beside the device one.
#[cfg(all(target_os = "none", feature = "host_app"))]
compile_error!("`host_app` (and `sim`, which enables it) is host-only");

#[cfg(all(target_os = "none", feature = "host-runtime"))]
compile_error!(
    "`host-runtime` is the host binary's executor; build the device with \
     `--no-default-features --features rtt` (`dbt build` does)"
);

// Link-only: the C++ app calls these crates' `#[no_mangle]` C ABI (residency and allocator, audio
// headers, sample source/reader/stream), which has no C++ fallback. Nothing in this crate's Rust
// references them, so without `extern crate` the linker would drop their rlibs.
#[cfg(has_app)]
extern crate deluge_audio_format;
#[cfg(has_app)]
extern crate deluge_resource;
#[cfg(has_app)]
extern crate deluge_sample_reader;
#[cfg(has_app)]
extern crate deluge_sample_source;
#[cfg(has_app)]
extern crate deluge_sample_stream;

// First, because its macros are textually scoped.
#[macro_use]
pub mod abi;

/// The libdeluge C ABI, generated from `libdeluge/*.h`: POD types, `DELUGE_*` constants, and a
/// declaration of every header function.
///
/// A header type that an in-tree crate defines is that crate's type here, re-exported under the
/// header's name (see `build/crate_types.rs`).
#[cfg(target_os = "none")]
#[allow(non_camel_case_types, non_upper_case_globals, dead_code)]
// The headers' comments are Doxygen, which rustdoc misreads.
#[allow(rustdoc::all)]
pub mod sys {
    include!(concat!(env!("OUT_DIR"), "/libdeluge_sys.rs"));
}
// The plain host build has no bindgen output; `sys_host.rs` stands in.
#[cfg(not(has_app))]
#[path = "abi/sys_host.rs"]
pub mod sys;
/// The libdeluge C ABI, generated from `libdeluge/*.h` for the host ABI.
#[cfg(feature = "host_app")]
pub mod sys {
    #![allow(
        non_upper_case_globals,
        non_camel_case_types,
        non_snake_case,
        dead_code
    )]
    // The headers' comments are Doxygen, which rustdoc misreads.
    #![allow(rustdoc::all)]
    include!(concat!(env!("OUT_DIR"), "/libdeluge_sys.rs"));
}
// `sys_host.rs` again beside the real `sys`, so its pins check the stand-ins against the headers.
#[cfg(feature = "host_app")]
#[path = "abi/sys_host.rs"]
mod sys_host_check;

/// The headers' own definitions of the types `sys` binds to in-tree crates' types, which
/// `abi_layout!` checks those crates' definitions against.
#[cfg(has_app)]
#[allow(
    non_upper_case_globals,
    non_camel_case_types,
    non_snake_case,
    dead_code
)]
mod sys_layout {
    include!(concat!(env!("OUT_DIR"), "/libdeluge_layout.rs"));
}

pub mod boot;
#[cfg(target_os = "none")]
mod boot_mem;
// The C-ABI services and link symbols the C++ app needs: present wherever it is linked.
#[cfg(has_app)]
mod ffi;
#[cfg(has_app)]
mod ffi_extra;
#[cfg(feature = "host_app")]
mod host_link_stubs;
pub mod scheduler;
mod services;
pub mod tier;
pub mod tier_meter;

pub mod audio;
pub mod channel;
pub mod peripherals;
pub mod storage;

pub mod runtime;

/// The deluge-sdk board support crate, for harnesses that drive the board directly.
pub use deluge_bsp;
