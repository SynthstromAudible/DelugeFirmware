//! C++ memory bring-up: zero the SDRAM `.bss` regions, copy `.sdram_init` from its SRAM load
//! address, and run the C++ global constructors.
//!
//! Runs after `Board::new` (SDRAM up) and before any application code. The section symbols come
//! from `linker/sdram_sections.x`.

unsafe extern "C" {
    static __frunk_bss_start: u8;
    static __frunk_bss_end: u8;
    static __sdram_bss_start: u8;
    static __sdram_bss_end: u8;
    static __sdram_init_start: u8;
    static __sdram_init_end: u8;
    static __sdram_init_lma: u8;
    static __init_array_start: u8;
    static __init_array_end: u8;
}

/// Base address of SDRAM.
///
/// The application owns the low part (its heap runs from `__heap_start` up to
/// [`RUST_SDRAM_BASE`]); the Rust allocator (DMA and audio buffers) owns the top
/// [`RUST_SDRAM_RESERVE`] bytes.
pub const SDRAM_BASE: usize = 0x0C00_0000;
/// SDRAM size in bytes.
pub const SDRAM_SIZE: usize = 64 * 1024 * 1024;
/// Bytes reserved at the top of SDRAM for the Rust allocator.
pub const RUST_SDRAM_RESERVE: usize = 8 * 1024 * 1024;
/// First address of the Rust-owned SDRAM slice, and the end of the app's external memory.
pub const RUST_SDRAM_BASE: usize = SDRAM_BASE + SDRAM_SIZE - RUST_SDRAM_RESERVE;

/// Zeroes the SDRAM and frunk `.bss` regions and copies `.sdram_init` from its SRAM LMA to its
/// SDRAM VMA.
///
/// `.sdram_init` holds code (the GUI, and `PLACE_SDRAM_TEXT` functions) as well as data. The copy
/// lands in the L1 D-cache, which instruction fetches do not see, so it is cleaned out to the L2
/// (unified, and on the fetch path) and the I-cache and branch predictor are invalidated before
/// anything can branch into it.
///
/// # Safety
///
/// Call exactly once, after SDRAM is initialised and before any code reads SDRAM-resident globals.
pub unsafe fn init_sdram_memory() {
    unsafe {
        zero(
            core::ptr::addr_of!(__frunk_bss_start),
            core::ptr::addr_of!(__frunk_bss_end),
        );
        zero(
            core::ptr::addr_of!(__sdram_bss_start),
            core::ptr::addr_of!(__sdram_bss_end),
        );

        let src = core::ptr::addr_of!(__sdram_init_lma);
        let dst = core::ptr::addr_of!(__sdram_init_start) as *mut u8;
        let len = core::ptr::addr_of!(__sdram_init_end) as usize
            - core::ptr::addr_of!(__sdram_init_start) as usize;
        if len > 0 {
            core::ptr::copy_nonoverlapping(src, dst, len);
            rza1_hal::cache::dma_clean_range(dst as usize, dst as usize + len);
            // BPIALL; l1_i_invalidate_all's DSB + ISB complete it.
            core::arch::asm!("mcr p15, 0, {0}, c7, c5, 6", in(reg) 0u32, options(nomem, nostack));
            rza1_hal::cache::l1_i_invalidate_all();
        }
    }
}

/// Runs the C++ global constructors (`.init_array`).
///
/// # Safety
///
/// Call exactly once, after [`init_sdram_memory`] (the globals may live in SDRAM) and before any
/// code that depends on constructed globals.
pub unsafe fn run_init_array() {
    type Ctor = unsafe extern "C" fn();
    unsafe {
        let mut p = core::ptr::addr_of!(__init_array_start) as *const Ctor;
        let end = core::ptr::addr_of!(__init_array_end) as *const Ctor;
        while p < end {
            (*p)();
            p = p.add(1);
        }
    }
}

/// Zeroes `[start, end)`.
///
/// Callers must pass raw pointers made with `addr_of!`, never references to the linker symbols.
/// Subtracting references to two distinct objects is UB to LLVM, which folds the length to zero
/// and deletes the memset, leaving every `PLACE_SDRAM_BSS`/`PLACE_INTERNAL_FRUNK` global (the
/// allocator's metadata included) holding the previous session's RAM. Casting each pointer to
/// `usize` keeps the subtraction plain integer arithmetic.
///
/// # Safety
///
/// `[start, end)` must be a writable region with `start <= end`.
unsafe fn zero(start: *const u8, end: *const u8) {
    let len = (end as usize) - (start as usize);
    if len > 0 {
        unsafe { core::ptr::write_bytes(start as *mut u8, 0, len) };
    }
}
