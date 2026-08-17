//! Real implementations of the simplest libdeluge services (system.h, clock.h)
//! over the deluge-sdk HAL/Embassy. These replace the stubs in [`crate::ffi`]
//! for the same symbols (the stubs were removed there to avoid duplicates).
#![allow(non_snake_case)]

#[cfg(target_os = "none")]
use core::sync::atomic::{AtomicU32, Ordering};

#[cfg(target_os = "none")]
use crate::sys::{
    DelugeMemoryKind_DELUGE_MEM_FAST_INTERNAL as KIND_INTERNAL,
    DelugeMemoryKind_DELUGE_MEM_LARGE_EXTERNAL as KIND_EXTERNAL, DelugeMemoryRegion, DelugeStatus,
    DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM, DelugeStatus_DELUGE_OK as DELUGE_OK,
};

// `host_app` feature: the C++ app is actually linked on host (see build.rs),
// so it needs real memory/cache providers too — same `sys` identifiers as the
// device block above (the two cfgs are mutually exclusive, so no clash).
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
use crate::sys::{
    DelugeMemoryKind_DELUGE_MEM_FAST_INTERNAL as KIND_INTERNAL,
    DelugeMemoryKind_DELUGE_MEM_LARGE_EXTERNAL as KIND_EXTERNAL, DelugeMemoryRegion, DelugeStatus,
    DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM, DelugeStatus_DELUGE_OK as DELUGE_OK,
};

// Linker boundary symbols (rza1l.x): the internal SRAM heap and the end of the
// SDRAM .bss. Used to describe the allocatable regions to the app. Device-only
// (no rza1l.x linker script on host).
#[cfg(target_os = "none")]
unsafe extern "C" {
    static __sram_heap_start: u8;
    static __sram_heap_end: u8;
    static __sdram_bss_end: u8;
}

// ── system.h ────────────────────────────────────────────────────────────────

/// Device nesting depth for ENTER/EXIT — only the outermost pair toggles the
/// CPU interrupt mask. Single context per core, so a plain atomic is correct.
#[cfg(target_os = "none")]
static CS_DEPTH: AtomicU32 = AtomicU32::new(0);

// Host per-thread critical-section nesting depth. Each thread independently
// acquires the global `critical-section` mutex on its OUTERMOST enter and
// nests within itself, so two real OS threads (the `"deluge-audio"` executor
// and the fiber/executor thread in the preemptive harness) are mutually
// excluded — unlike a process-global depth, which would let a second thread
// enter while the first still holds the lock. Device uses the CPU IRQ mask
// (`CS_DEPTH` below), which is genuinely single-context per core.
#[cfg(not(target_os = "none"))]
std::thread_local! {
    static CS_DEPTH_TL: core::cell::Cell<u32> = const { core::cell::Cell::new(0) };
    static CS_TOKEN_TL: core::cell::Cell<Option<critical_section::RestoreState>> =
        const { core::cell::Cell::new(None) };
}

/// Mask interrupts (nestable). [task] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn ENTER_CRITICAL_SECTION() {
    #[cfg(target_os = "none")]
    {
        cortex_ar::interrupt::disable();
        CS_DEPTH.fetch_add(1, Ordering::Relaxed);
    }
    #[cfg(not(target_os = "none"))]
    CS_DEPTH_TL.with(|depth| {
        if depth.get() == 0 {
            // SAFETY: paired with the release in EXIT once this thread's depth
            // returns to 0. `critical_section::acquire` is per-thread reentrant
            // and blocks until any other thread's outstanding section releases.
            let token = unsafe { critical_section::acquire() };
            CS_TOKEN_TL.with(|t| t.set(Some(token)));
        }
        depth.set(depth.get() + 1);
    });
}

/// Unmask interrupts if this closes this context's outermost critical section. [task] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn EXIT_CRITICAL_SECTION() {
    #[cfg(target_os = "none")]
    if CS_DEPTH.fetch_sub(1, Ordering::Relaxed) <= 1 {
        // SAFETY: balanced with ENTER_CRITICAL_SECTION; re-enabling at depth 0.
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

/// True if executing in IRQ/FIQ context (CPSR mode bits). [task] [isr]
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

/// Host stand-in: no ARM CPSR / IRQ context exists on host, so this always
/// reports false — the host executor is single-threaded and cooperative, with
/// no true interrupt preemption to detect. [task] [isr]
#[cfg(not(target_os = "none"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_in_interrupt() -> bool {
    false
}

/// Nesting depth of the audio render context: non-zero while the audio SGI handler is on the
/// stack. Not an `AtomicBool`, because audio can nest on top of the lower-priority streaming
/// SGI handler — on exit the marker must revert to the *enclosing* context rather than clear
/// unconditionally.
#[cfg(target_os = "none")]
static AUDIO_CONTEXT: AtomicU32 = AtomicU32::new(0);

/// Mark entry into the audio render context. Returns the previous marker state, so a call
/// site reads as a balanced pair with [`audio_context_exit`]. Call ONLY from the audio SGI
/// handler. [isr]
#[cfg(target_os = "none")]
pub fn audio_context_enter() -> bool {
    AUDIO_CONTEXT.fetch_add(1, Ordering::Relaxed) != 0
}

/// Undo one [`audio_context_enter`]. `_prev` is accepted and ignored — the counter itself
/// carries the nesting; the parameter exists so the pairing is visible at the call site. [isr]
#[cfg(target_os = "none")]
pub fn audio_context_exit(_prev: bool) {
    AUDIO_CONTEXT.fetch_sub(1, Ordering::Relaxed);
}

/// True iff the audio SGI handler is on the stack — the one context permitted to skip the
/// resource manager's mask, because it preempts every other context that touches it. See
/// `libdeluge/system.h` and `deluge_resource::sync`'s module doc.
///
/// Deliberately NOT `deluge_in_interrupt()`: that is a CPSR mode test, true in the streaming
/// fill executor's SGI handler too — which audio preempts, so it must keep masking.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_in_audio_context() -> bool {
    AUDIO_CONTEXT.load(Ordering::Relaxed) != 0
}

/// Host stand-in: host runs audio on a real OS thread with no interrupt context, and the
/// asymmetric mask skip is deliberately NOT taken there — every thread masks, so the global
/// critical-section mutex serializes them (see `deluge_resource::sync`'s module doc).
/// [task] [isr]
#[cfg(not(target_os = "none"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_in_audio_context() -> bool {
    false
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

/// Reset the device. Does not return. On host there is no hardware reset
/// vector, so this tears down the process the same way a watchdog reset would
/// end the firmware's execution. [task]
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_system_reset() -> ! {
    std::process::abort()
}

// ── clock.h ─────────────────────────────────────────────────────────────────

/// Busy-wait `us` microseconds via the OSTM-backed embassy-time driver. [task]
///
/// NOTE: kept as `block_for`, NOT a fiber-yield. A delay can be
/// called mid-operation (incl. inside SD/FatFS sequences); yielding the fiber there
/// would let other tasks re-enter non-reentrant state. See sd.rs.
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
//
// Device-only: describes the C++ app's SRAM/SDRAM regions via linker-symbol
// bounds (`crate::boot_mem` and `crate::sys::DelugeMemoryRegion`), which only
// exist on the device build. The host_app build gets its own real
// implementations of these same symbols, backed by process memory instead of
// linker symbols — see the "memory.h + cache maintenance (host_app)" section
// below.

/// One past the application-usable external (SDRAM) region. Capped below the
/// Rust allocator's reserved slice so the app's heap and the Rust SDRAM heap
/// don't overlap. [task] [audio] [isr]
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_external_end() -> usize {
    crate::boot_mem::RUST_SDRAM_BASE
}

/// Base of the fast internal (on-chip SRAM) region. [task] [audio] [isr]
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_internal_begin() -> usize {
    0x2000_0000
}

/// Number of allocatable memory regions the board provides. [task]
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_region_count() -> u8 {
    2
}

/// Describe region `index`: 0 = large external (SDRAM, below the Rust allocator's
/// reserved slice), 1 = fast internal (the SRAM heap `[__sram_heap_start,
/// __sram_heap_end)`). The app sources its internal-heap bounds from this instead
/// of reading raw linker symbols, whose meaning differs in this BSP's layout
/// (per-mode exception stacks sit between the heap and the program stack). [task]
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

/// A writable scratch address whose contents are never read. [task] [audio] [isr]
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_scratch() -> *mut core::ffi::c_void {
    static mut SCRATCH: [u8; 256] = [0; 256];
    core::ptr::addr_of_mut!(SCRATCH) as *mut core::ffi::c_void
}

// ── memory.h + cache maintenance (host_app) ────────────────────────────────
//
// `host_app` feature: the C++ app is actually linked and run on host (see
// build.rs), so its `GeneralMemoryAllocator` needs *real* backing memory
// (process `static` byte arrays), not the no-op stand-ins above (which are
// `target_os = "none"`-only and reference device linker-boundary symbols that
// don't exist on host). Mirrors `src/bsp/host/host_bsp.c` exactly: same two
// regions, same `HOST_SDRAM_BYTES`/`HOST_INTERNAL_BYTES` sizes, no DMA on host
// so cache maintenance is a no-op.

/// 64 MiB — holds stealable + external + external_small (host_bsp.c's
/// `HOST_SDRAM_BYTES`).
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
const HOST_SDRAM_BYTES: usize = 67_108_864;
/// 2 MiB on-chip-SRAM-equivalent internal heap region (host_bsp.c's
/// `HOST_INTERNAL_BYTES`).
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
const HOST_INTERNAL_BYTES: usize = 2_097_152;

/// Plain `[u8; N]` statics default to alignment 1 — `deluge_heap_create`
/// (`deluge_alloc::tlsf::Tlsf::add_pool`) requires its `mem` pointer 16-byte
/// aligned (`tlsf::ALIGN`) and dereferences a `BlockHeader` through it
/// unconditionally, so an unaligned region faults immediately on the first
/// allocation (found running the host_app boot smoke: "misaligned pointer
/// dereference ... must be a multiple of 0x10" inside `add_pool`). C's
/// `host_bsp.c` gets away with an unaligned `uint8_t[]` because nothing there
/// enforces the alignment at the type level; Rust's raw-pointer-dereference
/// check does. Force it explicitly rather than relying on the allocator's
/// internal 16-byte carve-out to happen to land aligned.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[repr(C, align(16))]
struct HeapRegion<const N: usize>([u8; N]);

/// Region 0: large external (SDRAM-equivalent) backing store.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
static mut HOST_SDRAM: HeapRegion<HOST_SDRAM_BYTES> = HeapRegion([0; HOST_SDRAM_BYTES]);
/// Region 1: fast internal (SRAM-equivalent) backing store.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
static mut HOST_INTERNAL: HeapRegion<HOST_INTERNAL_BYTES> = HeapRegion([0; HOST_INTERNAL_BYTES]);

/// Number of allocatable memory regions the board provides. [task]
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_region_count() -> u8 {
    2
}

/// Describe region `index`: 0 = large external (host-process SDRAM stand-in),
/// 1 = fast internal (host-process SRAM stand-in). [task]
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_region(index: u8, out: *mut DelugeMemoryRegion) -> DelugeStatus {
    // `addr_of_mut!` on a `static mut` just forms a raw pointer to the place
    // (no reference created, no read/write) — safe to call outside `unsafe`.
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

/// One past the application-usable external (SDRAM-equivalent) region. [task] [audio] [isr]
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_external_end() -> usize {
    core::ptr::addr_of!(HOST_SDRAM) as usize + HOST_SDRAM_BYTES
}

/// Base of the fast internal (SRAM-equivalent) region. [task] [audio] [isr]
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_internal_begin() -> usize {
    core::ptr::addr_of!(HOST_INTERNAL) as usize
}

/// A writable scratch address whose contents are never read. [task] [audio] [isr]
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_scratch() -> *mut core::ffi::c_void {
    static mut HOST_SCRATCH: [u8; 256] = [0; 256];
    core::ptr::addr_of_mut!(HOST_SCRATCH) as *mut core::ffi::c_void
}

/// Cache line size in bytes (alignment unit for DMA-coherent buffers). No real
/// DMA on host; matches the device's actual RZ/A1 line size for allocator
/// alignment behavior parity. [task]
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_cache_line_size() -> u32 {
    64
}

/// No DMA on host → cache maintenance is a no-op (host_bsp.c parity). [task] [isr]
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_cache_clean(_addr: *const core::ffi::c_void, _size: u32) {}

/// No DMA on host → cache maintenance is a no-op (host_bsp.c parity). [task] [isr]
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_cache_invalidate(_addr: *const core::ffi::c_void, _size: u32) {}

#[cfg(all(test, not(target_os = "none")))]
mod cs_tests {
    use super::{ENTER_CRITICAL_SECTION, EXIT_CRITICAL_SECTION};
    use std::sync::Arc;
    use std::sync::atomic::{AtomicPtr, Ordering};

    #[test]
    fn cross_thread_exclusion_no_lost_updates() {
        // A deliberately non-atomic counter shared by raw pointer, mutated only
        // inside ENTER/EXIT. If the section provides real cross-thread exclusion,
        // no increment is lost.
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
