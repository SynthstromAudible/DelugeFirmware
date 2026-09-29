//! deluge_rust — umbrella staticlib for the Deluge Rust core.
//!
//! Links into the firmware / sim / conformance tests and re-exports the member
//! libraries' C ABIs so their `#[no_mangle]` symbols land in the final
//! `libdeluge_rust.a`:
//!   - `deluge_alloc`        — TLSF + slab allocator (`libdeluge/alloc.h`)
//!   - `deluge_resource`     — cached-asset / resource manager (`deluge_resource.h`)
//!   - `deluge_audio_format` — WAV/AIFF header decoding (`libdeluge/audio_format.h`)
//!
//! It also provides the single bare-metal `#[panic_handler]` for the whole no_std
//! dependency graph (the member crates are plain rlibs with none of their own).

// `no_std` only for the bare-metal device build (target_os = "none"); on the host
// it's a normal std crate so the conformance staticlibs link with std's runtime.
#![cfg_attr(target_os = "none", no_std)]

// Re-export the members so their public items (incl. the `#[no_mangle] extern "C"`
// entry points) are reachable from this staticlib's root and thus retained.
pub use deluge_alloc::*;
pub use deluge_audio_format::abi::*;
pub use deluge_resource::*;

// Link-only force-link for the sim's `sim` feature (see Cargo.toml), mirroring
// `src/bsp/rust/src/main.rs`'s device `extern crate deluge_sample_source`.
// Unlike `deluge_alloc`/`deluge_resource` above, nothing in this crate's own Rust code
// references `deluge_sample_source`'s items -- there is no `pub use` to keep it live -- so
// without this, rustc/lld would never pull its single-object rlib into `libdeluge_rust.a`'s
// link at all, even with the optional dependency + `sim` feature wired in Cargo.toml, and the
// sim's C++ reader would keep resolving `sample_source.cpp`'s weak fallback.
#[cfg(feature = "sim")]
extern crate deluge_sample_source;

// Force-link the Rust cluster fill + the std critical-section impl into libdeluge_rust.a,
// same reasoning as `deluge_sample_source` above — nothing in this crate's Rust code references their
// items, so without an explicit `extern crate` rustc/lld would not retain their objects in the
// staticlib, and the sim's C++ would keep resolving async_fill.cpp's weak fill wrappers. Retaining the
// fill member lets the strong override be pulled from the archive; retaining
// critical_section keeps its `_critical_section_1_0_acquire` impl present for FILL_CONTEXTS's mutex.
#[cfg(feature = "sim")]
extern crate critical_section;
#[cfg(feature = "sim")]
extern crate deluge_sample_fill;

// Force-link the sample range-reader into libdeluge_rust.a, same reasoning as the
// fill/cursor above — nothing in this crate's Rust code references its items, so without an explicit
// `extern crate` rustc/lld would drop its objects from the staticlib, and the migrated C++ consumers'
// `deluge_sample_reader_*` references would go unresolved at the sim link. No weak fallback to override
// here (unlike the fill) — retention alone suffices.
#[cfg(feature = "sim")]
extern crate deluge_sample_reader;

// Force-link the streaming-file slot registry into libdeluge_rust.a, same
// reasoning as the reader above — nothing in this crate's Rust code references its items, so
// without an explicit `extern crate` rustc/lld would drop its objects from the staticlib. No
// consumer yet (the C++ facade will flip onto it later), so retention alone suffices.
#[cfg(feature = "sim")]
extern crate deluge_sample_stream;

// Bare-metal panic handler (device only); the host build uses std's. This is the
// one panic handler for the entire dependency graph.
#[cfg(target_os = "none")]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    extern "C" {
        fn abort() -> !;
    }
    unsafe { abort() }
}
