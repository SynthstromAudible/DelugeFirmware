//! Stubs for app/BSP symbols that live OUTSIDE the libdeluge C-ABI headers:
//! BSP globals/functions the app references directly (USB-host, trigger
//! clock), the FatFS `disk_*` diskio glue, NE10 DSP entry points (see note), and
//! a couple of C-runtime/linker shims.
//!
//! Compiled on host too under the `host_app` feature (see main.rs). Most of
//! this module is already host-portable as-is (the BSP globals, the USB
//! no-ops, the fault-pointer stub, the NE10 stubs); `_sbrk`/`_fini` stay
//! device-only (host glibc/crt already provide them — see below), and the
//! `program_stack_*`/`__frunk_*` linker-boundary stand-ins near the bottom are
//! host-only (device gets the real symbols from the `rza1l.x` linker script).
#![allow(non_upper_case_globals, unused_variables)]

// --- newlib heap for malloc -------------------------------------------------
// libgcc's emulated TLS (__emutls_get_address → emutls_alloc) and other newlib
// internals call malloc, which needs _sbrk. -lnosys's _sbrk just fails, so any
// thread_local access (notably the C++ exception runtime's per-thread
// __cxa_eh_globals) aborts. Provide a real _sbrk over a dedicated heap in SDRAM
// (don't eat the tight SRAM heap; zeroed by boot_mem via .sdram_bss).
//
// Device-only: host glibc already provides a real _sbrk (backing the host
// libc's malloc arena); defining our own here would conflict with it at link
// time (duplicate symbol) for no benefit — the host C++ app just uses the
// host allocator directly.
#[cfg(target_os = "none")]
#[unsafe(link_section = ".sdram_bss")]
static mut NEWLIB_HEAP: [u8; 1024 * 1024] = [0; 1024 * 1024];
#[cfg(target_os = "none")]
static NEWLIB_BRK: core::sync::atomic::AtomicUsize = core::sync::atomic::AtomicUsize::new(0);

/// newlib heap break. Returns the previous break, or (void*)-1 on exhaustion.
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

// --- BSP globals the app reads ---
#[unsafe(no_mangle)]
pub static mut anythingInitiallyAttachedAsUSBHost: u8 = 0;

// --- USB host/peripheral control (BSP) ---
#[unsafe(no_mangle)]
pub extern "C" fn openUSBHost() {}
#[unsafe(no_mangle)]
pub extern "C" fn closeUSBHost() {}
#[unsafe(no_mangle)]
pub extern "C" fn openUSBPeripheral() {}

// FatFS diskio glue (disk_initialize/status/ioctl/read/write/timerproc,
// get_fattime) is implemented in `sd` (SD card over deluge_bsp::sd).

// `fault_handler_print_freeze_pointers` (the app's FREEZE_WITH_ERROR reporter) used to
// be an empty stub here, so a FREEZE_WITH_ERROR on this BSP silently reported nothing.
// The real pad-grid renderer is now portable (src/deluge/io/debug/fault_pattern.c) and
// links into this image, with [`crate::fault`] supplying its board half — so the stub
// is gone rather than shadowing it.

// --- C runtime shim: with -nostartfiles there's no crtn _fini. ---
// Device-only: the host build uses the normal glibc crt (crti/crtn), which
// already provides _fini — a custom one here would collide at link time.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn _fini() {}

// --- NE10 DSP entry points. TODO: the Debug libNE10.a doesn't compile the FFT/
// FIR/IIR sources; investigate the NE10 CMake. Stubbed so the link succeeds; these must
// become real before the FFT analysis path is exercised. ---
macro_rules! ne10_stub {
    ($($name:ident),+ $(,)?) => { $(
        #[unsafe(no_mangle)]
        pub extern "C" fn $name() -> i32 { 0 }
    )+ };
}
ne10_stub!(
    ne10_fft_alloc_c2c_float32_c,
    ne10_fft_c2c_1d_float32_c,
    ne10_fft_c2c_1d_float32_neon,
    ne10_fft_c2c_1d_int16_c,
    ne10_fft_c2c_1d_int16_neon,
    ne10_fft_c2r_1d_float32_c,
    ne10_fft_c2r_1d_float32_neon,
    ne10_fft_c2r_1d_int16_c,
    ne10_fft_c2r_1d_int16_neon,
    ne10_fft_r2c_1d_float32_c,
    ne10_fft_r2c_1d_float32_neon,
    ne10_fft_r2c_1d_int16_c,
    ne10_fft_r2c_1d_int16_neon,
    ne10_fir_decimate_float_c,
    ne10_fir_float_c,
    ne10_fir_interpolate_float_c,
    ne10_fir_lattice_float_c,
    ne10_fir_sparse_float_c,
    ne10_iir_lattice_float_c,
);

// --- Linker-boundary stand-ins (host_app only) ------------------------------
// On device these four `extern uint32_t` symbols (memory/stack_guard.cpp,
// memory/heaps.cpp) come from the `rza1l.x` linker script's PROVIDE()'d
// boundary marks; the app only ever takes their *address* (`&symbol`), never
// reads/writes through them. On host they are `.set`-aliased onto the edges
// of real `static` byte arrays with `global_asm!` + `.set` + `sym` operands
// (Rust statics have no linker-level alias mechanism), so no separate storage
// is allocated purely to be pointed at.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
mod boundary_markers {
    /// 256 KiB "frunk" (small-internal) slack region. Real backing storage: `deluge::memory::init_heaps()`
    /// builds an actual `DelugeHeap` over `[__frunk_bss_end, __frunk_slack_end)`
    /// and the app allocates/writes into it.
    const HOST_FRUNK_BYTES: usize = 262_144;
    // `#[repr(align(16))]`: `deluge::memory::init_heaps()` builds a real
    // `DelugeHeap` (`deluge_alloc::tlsf`) over this region, which requires its
    // base 16-byte aligned and dereferences a `BlockHeader` through it
    // unconditionally — a plain `[u8; N]` static (alignment 1) faults on the
    // first allocation. See the identical fix + full explanation on
    // `services.rs`'s `HeapRegion`.
    #[repr(C, align(16))]
    struct HeapRegion<const N: usize>([u8; N]);
    static mut HOST_FRUNK: HeapRegion<HOST_FRUNK_BYTES> = HeapRegion([0; HOST_FRUNK_BYTES]);

    /// Backing address for `program_stack_start`/`program_stack_end`, aliased
    /// to the SAME address below — never dereferenced,
    /// only compared. `stack_guard.cpp`'s `checkStack` reads this as "no known
    /// SoC-stack region" and returns immediately, matching its own comment:
    /// "the host sim, which runs on the OS-managed native stack".
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
