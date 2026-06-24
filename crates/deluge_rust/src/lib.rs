//! deluge_rust — umbrella staticlib for the Deluge Rust core.
//!
//! Links into the firmware / sim / conformance tests and re-exports the member
//! libraries' C ABIs so their `#[no_mangle]` symbols land in the final
//! `libdeluge_rust.a`:
//!   - `deluge_alloc`    — TLSF + slab allocator (`libdeluge/alloc.h`)
//!   - `deluge_resource` — cached-asset / resource manager (`deluge_resource.h`)
//!
//! It also provides the single bare-metal `#[panic_handler]` for the whole no_std
//! dependency graph (the member crates are plain rlibs with none of their own).

// `no_std` only for the bare-metal device build (target_os = "none"); on the host
// it's a normal std crate so the conformance staticlibs link with std's runtime.
#![cfg_attr(target_os = "none", no_std)]

// Re-export the members so their public items (incl. the `#[no_mangle] extern "C"`
// entry points) are reachable from this staticlib's root and thus retained.
pub use deluge_alloc::*;
pub use deluge_resource::*;

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
