//! Link symbols outside the libdeluge headers: the device's C-runtime shims (`_sbrk`, `_fini`),
//! which glibc provides on host, and the host app's stand-ins for the linker-script boundary
//! symbols `rza1l.x` provides on the device.
#![allow(non_upper_case_globals, unused_variables)]

// newlib's malloc, which libgcc's emulated TLS needs (e.g. the C++ exception runtime's
// `__cxa_eh_globals`), calls `_sbrk`; -lnosys's version always fails. This one serves a dedicated
// SDRAM heap, keeping the tight SRAM heap free.
#[cfg(target_os = "none")]
#[unsafe(link_section = ".sdram_bss")]
static mut NEWLIB_HEAP: [u8; 1024 * 1024] = [0; 1024 * 1024];
#[cfg(target_os = "none")]
static NEWLIB_BRK: core::sync::atomic::AtomicUsize = core::sync::atomic::AtomicUsize::new(0);

/// Moves newlib's heap break by `incr`; returns the previous break, or `(void*)-1` on exhaustion.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn _sbrk(incr: isize) -> *mut core::ffi::c_void {
    use core::sync::atomic::Ordering;
    let base = core::ptr::addr_of_mut!(NEWLIB_HEAP) as *mut u8 as usize;
    let len = 1024 * 1024usize;
    let old = NEWLIB_BRK.load(Ordering::Relaxed);
    let new = old as isize + incr;
    if new < 0 || new as usize > len {
        return usize::MAX as *mut core::ffi::c_void; // (void*)-1 → ENOMEM
    }
    NEWLIB_BRK.store(new as usize, Ordering::Relaxed);
    (base + old) as *mut core::ffi::c_void
}

// With -nostartfiles there is no crtn `_fini`.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn _fini() {}

// The app takes only the addresses of these `extern uint32_t` boundary symbols
// (memory/stack_guard.cpp, memory/heaps.cpp). On host they are `.set` aliases onto the edges of
// real statics, since Rust has no linker-level alias.
#[cfg(feature = "host_app")]
mod boundary_markers {
    /// The "frunk" (small internal) slack region's size. `init_heaps()` builds a real heap over
    /// `[__frunk_bss_end, __frunk_slack_end)`.
    const HOST_FRUNK_BYTES: usize = 262_144;
    // The heap requires a 16-byte-aligned base; a plain `[u8; N]` static is byte-aligned.
    #[repr(C, align(16))]
    struct HeapRegion<const N: usize>([u8; N]);
    static mut HOST_FRUNK: HeapRegion<HOST_FRUNK_BYTES> = HeapRegion([0; HOST_FRUNK_BYTES]);

    /// The one address both `program_stack_start` and `program_stack_end` alias. Equal bounds tell
    /// `checkStack` there is no known stack region, so it returns at once.
    static HOST_STACK_MARKER: u32 = 0;

    core::arch::global_asm!(
        ".global __frunk_bss_end",
        ".set __frunk_bss_end, {frunk}",
        ".global __frunk_slack_end",
        ".set __frunk_slack_end, {frunk} + {frunk_bytes}",
        ".global program_stack_start",
        ".set program_stack_start, {marker}",
        ".global program_stack_end",
        ".set program_stack_end, {marker}",
        frunk = sym HOST_FRUNK,
        frunk_bytes = const HOST_FRUNK_BYTES,
        marker = sym HOST_STACK_MARKER,
    );
}
