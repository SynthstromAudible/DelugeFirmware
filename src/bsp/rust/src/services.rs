//! The simplest libdeluge services: `libdeluge/system.h` (critical sections, context queries,
//! logging), `clock.h` and `memory.h`.
//!
//! The `[task]`/`[audio]`/`[isr]` tags name the contexts a function may be called from.
#![allow(non_snake_case)]

#[cfg(target_os = "none")]
use core::sync::atomic::{AtomicU32, Ordering};

#[cfg(has_app)]
use crate::sys::{
    DelugeMemoryKind_DELUGE_MEM_FAST_INTERNAL as KIND_INTERNAL,
    DelugeMemoryKind_DELUGE_MEM_LARGE_EXTERNAL as KIND_EXTERNAL, DelugeMemoryRegion, DelugeStatus,
    DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM, DelugeStatus_DELUGE_OK as DELUGE_OK,
};

// Linker boundary symbols (rza1l.x): the internal SRAM heap and the end of the SDRAM .bss.
#[cfg(target_os = "none")]
unsafe extern "C" {
    static __sram_heap_start: u8;
    static __sram_heap_end: u8;
    static __sdram_bss_end: u8;
}

// ── system.h ────────────────────────────────────────────────────────────────

/// The critical-section nesting depth; only the outermost pair toggles the CPU interrupt mask.
#[cfg(target_os = "none")]
static CS_DEPTH: AtomicU32 = AtomicU32::new(0);

/// Whether IRQs were unmasked when the outermost critical section opened, so closing it restores
/// that state instead of unmasking: a section opened with IRQs already masked (at boot, or inside a
/// Rust `critical_section`) must leave them masked.
#[cfg(target_os = "none")]
static CS_IRQS_WERE_ENABLED: core::sync::atomic::AtomicBool =
    core::sync::atomic::AtomicBool::new(false);

// Host: a per-thread depth. Each thread takes the global `critical-section` mutex on its
// outermost enter, so the host's executor threads exclude each other; a process-global depth
// would let a second thread in while the first holds the lock.
#[cfg(not(target_os = "none"))]
std::thread_local! {
    static CS_DEPTH_TL: core::cell::Cell<u32> = const { core::cell::Cell::new(0) };
    static CS_TOKEN_TL: core::cell::Cell<Option<critical_section::RestoreState>> =
        const { core::cell::Cell::new(None) };
}

/// Masks interrupts (nestable). `[task]` `[isr]`
#[unsafe(no_mangle)]
pub extern "C" fn ENTER_CRITICAL_SECTION() {
    #[cfg(target_os = "none")]
    {
        // CPSR.I is set while IRQs are masked (cortex-ar's `i()` is the raw bit).
        let irqs_enabled = !cortex_ar::register::Cpsr::read().i();
        cortex_ar::interrupt::disable();
        if CS_DEPTH.fetch_add(1, Ordering::Relaxed) == 0 {
            CS_IRQS_WERE_ENABLED.store(irqs_enabled, Ordering::Relaxed);
        }
    }
    #[cfg(not(target_os = "none"))]
    CS_DEPTH_TL.with(|depth| {
        if depth.get() == 0 {
            // SAFETY: released by EXIT when this thread's depth returns to 0. `acquire` blocks
            // while another thread holds the section.
            let token = unsafe { critical_section::acquire() };
            CS_TOKEN_TL.with(|t| t.set(Some(token)));
        }
        depth.set(depth.get() + 1);
    });
}

/// Restores the interrupt mask the outermost critical section found when this closes it. `[task]` `[isr]`
#[unsafe(no_mangle)]
pub extern "C" fn EXIT_CRITICAL_SECTION() {
    #[cfg(target_os = "none")]
    if CS_DEPTH.fetch_sub(1, Ordering::Relaxed) <= 1 && CS_IRQS_WERE_ENABLED.load(Ordering::Relaxed)
    {
        // SAFETY: balanced with ENTER_CRITICAL_SECTION; IRQs were unmasked when it opened.
        unsafe { cortex_ar::interrupt::enable() };
    }
    #[cfg(not(target_os = "none"))]
    CS_TOKEN_TL.with(|t| {
        let closed = CS_DEPTH_TL.with(|depth| {
            let d = depth.get().saturating_sub(1);
            depth.set(d);
            d == 0
        });
        if closed {
            // SAFETY: the token was stashed by this thread's matching outermost ENTER.
            if let Some(token) = t.take() {
                unsafe { critical_section::release(token) };
            }
        }
    });
}

/// Whether the CPU is in IRQ or FIQ mode (CPSR mode bits). `[task]` `[isr]`
#[cfg(target_os = "none")]
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

/// Always false: the host has no interrupt context. `[task]` `[isr]`
#[cfg(not(target_os = "none"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_in_interrupt() -> bool {
    false
}

/// The audio render context's nesting depth: nonzero while the audio SGI handler is on the stack.
/// A count, not a flag, so exit restores the enclosing context's state.
#[cfg(target_os = "none")]
static AUDIO_CONTEXT: AtomicU32 = AtomicU32::new(0);

/// Marks entry into the audio render context and returns the previous state, to pass to
/// [`audio_context_exit`]. Call only from the audio SGI handler. `[isr]`
#[cfg(target_os = "none")]
pub fn audio_context_enter() -> bool {
    AUDIO_CONTEXT.fetch_add(1, Ordering::Relaxed) != 0
}

/// Undoes one [`audio_context_enter`]. `_prev` is unused (the count carries the nesting); it
/// makes the pairing visible at the call site. `[isr]`
#[cfg(target_os = "none")]
pub fn audio_context_exit(_prev: bool) {
    AUDIO_CONTEXT.fetch_sub(1, Ordering::Relaxed);
}

/// Whether the audio SGI handler is on the stack: the one context allowed to skip the resource
/// manager's mask, because it preempts every other context that touches the manager.
///
/// Not `deluge_in_interrupt()`: the shared IRQ entry switches to SYS mode, so that is false in
/// every handler. The storage executor's handler, which audio preempts, must keep masking.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_in_audio_context() -> bool {
    AUDIO_CONTEXT.load(Ordering::Relaxed) != 0
}

/// Always false on host: every thread masks, so the global critical-section mutex serializes
/// them. `[task]` `[isr]`
#[cfg(not(target_os = "none"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_in_audio_context() -> bool {
    false
}

/// Writes debug text, with no implicit newline, to the RTT channel. Best effort. `[task]` `[isr]`
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

/// Busy-waits `us` microseconds. `[task]`
///
/// A busy-wait, not a yield: a delay can be called mid-operation, where yielding would let other
/// tasks re-enter non-reentrant state.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_delay_us(us: u32) {
    embassy_time::block_for(embassy_time::Duration::from_micros(us as u64));
}

/// Busy-waits `ms` milliseconds. `[task]`
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_delay_ms(ms: u32) {
    embassy_time::block_for(embassy_time::Duration::from_millis(ms as u64));
}

/// The monotonic clock, in microseconds. `[task]` `[isr]`
#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_now() -> u64 {
    embassy_time::Instant::now().as_micros()
}

// ── memory.h ────────────────────────────────────────────────────────────────
//
// The device's regions come from linker symbols; the host app's, below, are process statics.

/// The number of allocatable memory regions. `[task]`
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_region_count() -> u8 {
    2
}

/// Describes region `index`: 0 is large external (SDRAM, below the Rust allocator's slice), 1 is
/// fast internal (the SRAM heap). The app takes its heap bounds from here rather than from linker
/// symbols, because this layout puts the exception stacks between the heap and the program
/// stack. `[task]`
#[cfg(target_os = "none")]
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

/// A writable scratch address whose contents are never read. `[task]` `[audio]` `[isr]`
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_scratch() -> *mut core::ffi::c_void {
    static mut SCRATCH: [u8; 256] = [0; 256];
    core::ptr::addr_of_mut!(SCRATCH) as *mut core::ffi::c_void
}

// ── memory.h (host_app) ─────────────────────────────────────────────────────
//
// The host app's `GeneralMemoryAllocator` gets two process statics as its regions.

/// The external region's size: room for the stealable, external and small external heaps.
#[cfg(feature = "host_app")]
const HOST_SDRAM_BYTES: usize = 67_108_864;
/// The internal (on-chip SRAM stand-in) region's size.
#[cfg(feature = "host_app")]
const HOST_INTERNAL_BYTES: usize = 2_097_152;

/// A heap region, 16-byte aligned because `deluge_heap_create` requires it (a plain `[u8; N]`
/// static is only byte-aligned).
#[cfg(feature = "host_app")]
#[repr(C, align(16))]
struct HeapRegion<const N: usize>([u8; N]);

/// Region 0: large external (SDRAM stand-in).
#[cfg(feature = "host_app")]
static mut HOST_SDRAM: HeapRegion<HOST_SDRAM_BYTES> = HeapRegion([0; HOST_SDRAM_BYTES]);
/// Region 1: fast internal (SRAM stand-in).
#[cfg(feature = "host_app")]
static mut HOST_INTERNAL: HeapRegion<HOST_INTERNAL_BYTES> = HeapRegion([0; HOST_INTERNAL_BYTES]);

/// The number of allocatable memory regions. `[task]`
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_region_count() -> u8 {
    2
}

/// Describes region `index`: 0 is large external, 1 is fast internal. `[task]`
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_region(index: u8, out: *mut DelugeMemoryRegion) -> DelugeStatus {
    let (base, size, kind): (*mut u8, usize, _) = match index {
        0 => (
            core::ptr::addr_of_mut!(HOST_SDRAM) as *mut u8,
            HOST_SDRAM_BYTES,
            KIND_EXTERNAL,
        ),
        1 => (
            core::ptr::addr_of_mut!(HOST_INTERNAL) as *mut u8,
            HOST_INTERNAL_BYTES,
            KIND_INTERNAL,
        ),
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

/// A writable scratch address whose contents are never read. `[task]` `[audio]` `[isr]`
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_scratch() -> *mut core::ffi::c_void {
    static mut HOST_SCRATCH: [u8; 256] = [0; 256];
    core::ptr::addr_of_mut!(HOST_SCRATCH) as *mut core::ffi::c_void
}

#[cfg(test)]
mod cs_tests {
    use super::{ENTER_CRITICAL_SECTION, EXIT_CRITICAL_SECTION};
    use std::sync::Arc;
    use std::sync::atomic::{AtomicPtr, Ordering};

    #[test]
    fn cross_thread_exclusion_no_lost_updates() {
        // A non-atomic counter mutated only inside the section: no increment may be lost.
        let counter = Box::into_raw(Box::new(0u32));
        let shared = Arc::new(AtomicPtr::new(counter));
        const ITERS: u32 = 200_000;

        let handles: Vec<_> = (0..2)
            .map(|_| {
                let shared = Arc::clone(&shared);
                std::thread::spawn(move || {
                    let p = shared.load(Ordering::Relaxed);
                    for _ in 0..ITERS {
                        ENTER_CRITICAL_SECTION();
                        // SAFETY: p is live for the test; the section serializes access.
                        unsafe { *p = (*p).wrapping_add(1) };
                        EXIT_CRITICAL_SECTION();
                    }
                })
            })
            .collect();
        for h in handles {
            h.join().unwrap();
        }
        // SAFETY: all threads joined; sole owner again.
        let final_val = unsafe { *shared.load(Ordering::Relaxed) };
        // SAFETY: reclaim the box.
        unsafe { drop(Box::from_raw(shared.load(Ordering::Relaxed))) };
        assert_eq!(
            final_val,
            2 * ITERS,
            "lost updates ⇒ no cross-thread exclusion"
        );
    }

    #[test]
    fn nesting_on_one_thread_does_not_deadlock() {
        ENTER_CRITICAL_SECTION();
        ENTER_CRITICAL_SECTION();
        EXIT_CRITICAL_SECTION();
        EXIT_CRITICAL_SECTION();
    }
}

abi_pin!(
    deluge_clock_delay_ms,
    deluge_clock_delay_us,
    deluge_clock_now,
    deluge_in_audio_context,
    deluge_in_interrupt,
    deluge_log,
    deluge_memory_region,
    deluge_memory_region_count,
    deluge_memory_scratch,
    ENTER_CRITICAL_SECTION,
    EXIT_CRITICAL_SECTION,
);
