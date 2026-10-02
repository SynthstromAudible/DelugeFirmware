//! The region-port cursor behind the `deluge_sample_source_*` / `deluge_sample_region_*` C ABI
//! (`libdeluge/sample_source.h`).
//!
//! [`cursor::SampleSource`] is the per-reader residency state machine, generic over a
//! [`residency::Residency`] provider; [`manager_residency::ManagerResidency`] is the production
//! provider over the `deluge_resource` manager, and [`abi`] exports the C ABI over a static pool of
//! cursors.
#![no_std]

pub mod abi;
pub mod cursor;
pub mod geometry;
pub mod manager_residency;
pub mod residency;

// Test-only definitions of the C-ABI symbols `deluge_resource` calls out to. In a real link the
// BSP (`services.rs`) and the app provide them; `deluge_resource`'s own test stubs are
// crate-private, so this crate's test binary must define them or it fails to link.
#[cfg(test)]
mod host_critical_section_stubs {
    extern crate std;
    use core::cell::Cell;

    std::thread_local! {
        static DEPTH: Cell<u32> = const { Cell::new(0) };
        static TOKEN: Cell<Option<critical_section::RestoreState>> = const { Cell::new(None) };
        static FAKE_NOW: Cell<u32> = const { Cell::new(1) };
    }

    /// The clock `deluge_resource` measures loader service latency with (the app's
    /// `resource_clock.cpp` in the firmware). A monotonic counter meets the manager's only
    /// requirement: successive reads never go backwards.
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

    // These tests model neither interrupts nor the audio context, so every caller masks.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_interrupt() -> bool {
        false
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_audio_context() -> bool {
        false
    }
}
