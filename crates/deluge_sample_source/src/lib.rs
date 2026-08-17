//! The region-port residency state machine: a native Rust reimplementation of the
//! `deluge_sample_source_*` cursor contract, byte-identical to the C++ backing.
#![no_std]

pub mod abi;
pub mod cursor;
pub mod geometry;
pub mod manager_residency;
pub mod residency;

// `deluge_resource::sync` calls out to three C-ABI critical-section primitives that,
// in the real firmware/host-sim link, the BSP (`src/bsp/rust/src/services.rs`)
// provides. `deluge_resource`'s own `cargo test` binary supplies host stubs for
// them internally (`sync::stubs`, `#[cfg(test)]`-gated and crate-private), but that
// module isn't visible to downstream crates — so this crate's own test binary,
// which links `deluge_resource` as a plain (non-test) rlib, must provide the same
// three symbols itself or every test here fails to link. Single-threaded-per-test
// critical section backed by the `critical-section` host `std` impl, mirroring
// `deluge_resource::sync::stubs` (minus its enter/exit counters, which nothing here
// asserts on).
#[cfg(test)]
mod host_critical_section_stubs {
    extern crate std;
    use core::cell::Cell;

    std::thread_local! {
        static DEPTH: Cell<u32> = const { Cell::new(0) };
        static TOKEN: Cell<Option<critical_section::RestoreState>> = const { Cell::new(None) };
        static FAKE_NOW: Cell<u32> = const { Cell::new(1) };
    }

    /// The clock `deluge_resource` measures loader service latency with. In the firmware the app
    /// provides it (`src/deluge/io/debug/resource_clock.cpp`, returning
    /// `AudioEngine::audioSampleTimer`); a host test binary links the manager without the app, so it
    /// must supply one or the reference from `Manager::loader_next` fails to link. A monotonic counter
    /// satisfies the manager's only requirement — successive reads must not go backwards.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_debug_now_frames() -> u32 {
        FAKE_NOW.with(|c| {
            let n = c.get().wrapping_add(1);
            c.set(n);
            n
        })
    }

    #[unsafe(no_mangle)]
    extern "C" fn ENTER_CRITICAL_SECTION() {
        DEPTH.with(|d| {
            if d.get() == 0 {
                // SAFETY: released by the matching EXIT once this thread's depth hits 0.
                let t = unsafe { critical_section::acquire() };
                TOKEN.with(|tok| tok.set(Some(t)));
            }
            d.set(d.get() + 1);
        });
    }

    #[unsafe(no_mangle)]
    extern "C" fn EXIT_CRITICAL_SECTION() {
        let closed = DEPTH.with(|d| {
            let n = d.get().saturating_sub(1);
            d.set(n);
            n == 0
        });
        if closed {
            TOKEN.with(|tok| {
                if let Some(t) = tok.take() {
                    // SAFETY: stashed by this thread's outermost ENTER, above.
                    unsafe { critical_section::release(t) };
                }
            });
        }
    }

    // This crate's tests are single-threaded and never model the audio-ISR context
    // (that asymmetry is `deluge_resource`'s own concern, proved in its crate) — so
    // "never in an interrupt" is the right constant answer here.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_interrupt() -> bool {
        false
    }

    // Same reasoning as `deluge_in_interrupt` above: never the audio context here, so
    // `deluge_resource`'s mask is always taken under this crate's tests.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_audio_context() -> bool {
        false
    }
}
