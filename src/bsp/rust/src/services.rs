//! Real implementations of the simplest libdeluge services (system.h, clock.h)
//! over the deluge-sdk HAL/Embassy. These replace the M0 stubs in [`crate::ffi`]
//! for the same symbols (the stubs were removed there to avoid duplicates).
#![allow(non_snake_case)]

use core::sync::atomic::{AtomicU32, Ordering};

use crate::sys::{
    DelugeMemoryKind_DELUGE_MEM_FAST_INTERNAL as KIND_INTERNAL,
    DelugeMemoryKind_DELUGE_MEM_LARGE_EXTERNAL as KIND_EXTERNAL, DelugeMemoryRegion, DelugeStatus,
    DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM, DelugeStatus_DELUGE_OK as DELUGE_OK,
};

// Linker boundary symbols (rza1l.x): the internal SRAM heap and the end of the
// SDRAM .bss. Used to describe the allocatable regions to the app.
unsafe extern "C" {
    static __sram_heap_start: u8;
    static __sram_heap_end: u8;
    static __sdram_bss_end: u8;
}

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

/// Delay `us` microseconds. On the worker fiber, yield the fiber for the duration
/// (the executor keeps running); off the fiber, busy-wait via the OSTM-backed
/// embassy-time driver. [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_delay_us(us: u32) {
    let dur = embassy_time::Duration::from_micros(us as u64);
    if crate::fiber::on_fiber() {
        crate::fiber::block_on_fiber(embassy_time::Timer::after(dur));
    }
    else {
        embassy_time::block_for(dur);
    }
}

/// Delay `ms` milliseconds (yields the worker fiber when on it; see above). [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_delay_ms(ms: u32) {
    let dur = embassy_time::Duration::from_millis(ms as u64);
    if crate::fiber::on_fiber() {
        crate::fiber::block_on_fiber(embassy_time::Timer::after(dur));
    }
    else {
        embassy_time::block_for(dur);
    }
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

/// Number of allocatable memory regions the board provides. [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_region_count() -> u8 {
    2
}

/// Describe region `index`: 0 = large external (SDRAM, below the Rust allocator's
/// reserved slice), 1 = fast internal (the SRAM heap `[__sram_heap_start,
/// __sram_heap_end)`). The app sources its internal-heap bounds from this instead
/// of reading raw linker symbols, whose meaning differs in this BSP's layout
/// (per-mode exception stacks sit between the heap and the program stack). [task]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_region(index: u8, out: *mut DelugeMemoryRegion) -> DelugeStatus {
    let (base, size, kind) = match index {
        0 => {
            let b = core::ptr::addr_of!(__sdram_bss_end) as usize;
            (b, crate::boot_mem::RUST_SDRAM_BASE - b, KIND_EXTERNAL)
        }
        1 => {
            let b = core::ptr::addr_of!(__sram_heap_start) as usize;
            let e = core::ptr::addr_of!(__sram_heap_end) as usize;
            (b, e - b, KIND_INTERNAL)
        }
        _ => return DELUGE_ERR_PARAM,
    };
    // SAFETY: the app passes a valid DelugeMemoryRegion out-pointer.
    unsafe {
        (*out).base = base as *mut core::ffi::c_void;
        (*out).size = size as u32;
        (*out).kind = kind;
    }
    DELUGE_OK
}

/// A writable scratch address whose contents are never read. [task] [audio] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_scratch() -> *mut core::ffi::c_void {
    static mut SCRATCH: [u8; 256] = [0; 256];
    core::ptr::addr_of_mut!(SCRATCH) as *mut core::ffi::c_void
}
