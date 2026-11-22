//! Link symbols outside the libdeluge headers: the device's C-runtime shims (`_sbrk`, `_fini`),
//! which glibc provides on host, the device's replacements for libstdc++'s failure reporters, and
//! the host app's stand-ins for the linker-script boundary symbols `rza1-rt's link.x` provides on the device.
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

/// Replaces libstdc++'s default terminate handler, `__gnu_cxx::__verbose_terminate_handler`.
///
/// The stock handler demangles the active exception's type name and writes it to stderr, which
/// links libiberty's demangler and newlib's stdio (about 36 KB of SRAM) only to print to a stream
/// nothing reads. Panicking instead reports through the panic handler: RTT plus the fault pattern
/// on the pads.
#[cfg(target_os = "none")]
#[unsafe(export_name = "_ZN9__gnu_cxx27__verbose_terminate_handlerEv")]
pub extern "C" fn verbose_terminate_handler() -> ! {
    panic!("C++ std::terminate");
}

/// Replaces `std::__glibcxx_assert_fail(file, line, function, condition)`, which libstdc++ calls
/// when a `__glibcxx_assert` fails.
///
/// The stock one prints with `fprintf`, which links newlib's full floating-point printf. Panicking
/// reports the same four facts through the panic handler.
///
/// # Safety
///
/// Each non-null pointer must be a NUL-terminated string, as libstdc++ passes.
#[cfg(target_os = "none")]
#[unsafe(export_name = "_ZSt21__glibcxx_assert_failPKciS0_S0_")]
pub unsafe extern "C" fn glibcxx_assert_fail(
    file: *const core::ffi::c_char,
    line: core::ffi::c_int,
    function: *const core::ffi::c_char,
    condition: *const core::ffi::c_char,
) -> ! {
    // SAFETY: the caller passes NUL-terminated strings or null.
    let text = |p: *const core::ffi::c_char| {
        if p.is_null() {
            "?"
        } else {
            unsafe { core::ffi::CStr::from_ptr(p) }
                .to_str()
                .unwrap_or("?")
        }
    };
    panic!(
        "libstdc++ assertion `{}` failed in {} ({}:{})",
        text(condition),
        text(function),
        text(file),
        line
    );
}

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
