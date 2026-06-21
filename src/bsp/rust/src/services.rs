//! Real implementations of the simplest libdeluge services (system.h, clock.h)
//! over the deluge-sdk HAL/Embassy. These replace the M0 stubs in [`crate::ffi`]
//! for the same symbols (the stubs were removed there to avoid duplicates).
#![allow(non_snake_case)]

use core::sync::atomic::{AtomicU32, Ordering};

// ── system.h ────────────────────────────────────────────────────────────────

/// Nesting depth for ENTER/EXIT_CRITICAL_SECTION — only the outermost pair
/// actually toggles interrupts.
static CS_DEPTH: AtomicU32 = AtomicU32::new(0);

/// Mask interrupts (nestable). [task] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn ENTER_CRITICAL_SECTION() {
    cortex_ar::interrupt::disable();
    CS_DEPTH.fetch_add(1, Ordering::Relaxed);
}

/// Unmask interrupts if this closes the outermost critical section. [task] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn EXIT_CRITICAL_SECTION() {
    if CS_DEPTH.fetch_sub(1, Ordering::Relaxed) <= 1 {
        // SAFETY: balanced with ENTER_CRITICAL_SECTION; re-enabling at depth 0.
        unsafe { cortex_ar::interrupt::enable() };
    }
}

/// True if executing in IRQ/FIQ context (CPSR mode bits). [task] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_in_interrupt() -> bool {
    let cpsr: u32;
    // SAFETY: reads CPSR, no side effects.
    unsafe {
        core::arch::asm!("mrs {}, cpsr", out(reg) cpsr, options(nomem, nostack, preserves_flags));
    }
    let mode = cpsr & 0x1f;
    mode == 0x12 /* IRQ */ || mode == 0x11 /* FIQ */
}

/// Platform is already brought up by the Rust `main` before the app runs, so
/// this is a no-op that just reports success. [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_platform_init() -> i32 {
    0 // DELUGE_OK
}

/// Emit debug text (no implicit newline) to the RTT channel. Best-effort. [task] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_log(text: *const core::ffi::c_char) {
    if text.is_null() {
        return;
    }
    // SAFETY: caller passes a NUL-terminated C string.
    let cstr = unsafe { core::ffi::CStr::from_ptr(text) };
    if let Ok(s) = cstr.to_str() {
        #[cfg(feature = "rtt")]
        rtt_target::rprint!("{}", s);
        let _ = s;
    }
}

// ── clock.h ─────────────────────────────────────────────────────────────────

/// Busy-wait `us` microseconds via the OSTM-backed embassy-time driver. [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_delay_us(us: u32) {
    embassy_time::block_for(embassy_time::Duration::from_micros(us as u64));
}

/// Busy-wait `ms` milliseconds. [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_delay_ms(ms: u32) {
    embassy_time::block_for(embassy_time::Duration::from_millis(ms as u64));
}

/// Monotonic high-resolution counter, in microseconds (1 µs ticks). [task] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_now() -> u64 {
    embassy_time::Instant::now().as_micros()
}

/// Same monotonic source for the scheduler. [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_monotonic() -> u64 {
    embassy_time::Instant::now().as_micros()
}

/// Ticks per second of [`deluge_clock_now`] (µs resolution). [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_ticks_per_second() -> u64 {
    1_000_000
}

/// Hz of [`deluge_clock_monotonic`]. [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_monotonic_hz() -> u64 {
    1_000_000
}

// ── memory.h ────────────────────────────────────────────────────────────────

/// One past the application-usable external (SDRAM) region. Capped below the
/// Rust allocator's reserved slice so the app's heap and the Rust SDRAM heap
/// don't overlap. [task] [audio] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_external_end() -> usize {
    crate::boot_mem::RUST_SDRAM_BASE
}

/// Base of the fast internal (on-chip SRAM) region. [task] [audio] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_internal_begin() -> usize {
    0x2000_0000
}

/// A writable scratch address whose contents are never read. [task] [audio] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_scratch() -> *mut core::ffi::c_void {
    static mut SCRATCH: [u8; 256] = [0; 256];
    core::ptr::addr_of_mut!(SCRATCH) as *mut core::ffi::c_void
}
