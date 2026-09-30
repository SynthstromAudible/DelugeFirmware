//! C++ memory-model bring-up that must run AFTER `init_clocks` (SDRAM up) and
//! BEFORE the application code: zero the SDRAM `.bss` regions, copy initialised
//! SDRAM content from its SRAM load address, and run the C++ global constructors
//! (`.init_array`). Symbols come from `linker/sdram_sections.x`.

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

/// SDRAM split: the application owns the low part (its heap runs from
/// `__heap_start` up to the base of the Rust-owned slice below); the Rust
/// allocator (deluge-bsp DMA/audio buffers) gets this reserved slice at the top.
pub const SDRAM_BASE: usize = 0x0C00_0000;
pub const SDRAM_SIZE: usize = 64 * 1024 * 1024;
pub const RUST_SDRAM_RESERVE: usize = 8 * 1024 * 1024;
/// First address of the Rust-owned SDRAM slice = the app's external memory end.
pub const RUST_SDRAM_BASE: usize = SDRAM_BASE + SDRAM_SIZE - RUST_SDRAM_RESERVE;

/// Zero the SDRAM `.bss` regions and copy `.sdram_init` from its SRAM LMA to its
/// SDRAM VMA. Must be called once, after `init_clocks` (SDRAM controller up).
///
/// # Safety
/// Call exactly once, after SDRAM is initialised and before any app code reads
/// SDRAM-resident globals.
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
        }
    }
}

/// Run the C++ global constructors. Must be called after [`init_sdram_memory`]
/// (their storage may be in SDRAM) and before the application runs.
///
/// # Safety
/// Call exactly once, before any code that depends on constructed globals.
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

/// Zero `[start, end)`.
///
/// Takes RAW POINTERS, and the callers must produce them with `addr_of!` — never
/// `&SYMBOL`. This previously took `&u8` references and computed
/// `end as usize - start as usize`, which subtracts pointers derived from two
/// *distinct* objects: UB in LLVM's model, so the difference folded away, `len > 0`
/// went false, and **both memsets were deleted from the image**. The fingerprint was
/// that `__sdram_bss_start`/`__frunk_bss_start` were absent from the linked ELF
/// (nothing referenced them any more) while the `_end` symbols, which other code
/// also reads, were still there.
///
/// The consequence was not subtle: no SDRAM or frunk `.bss` was ever cleared, so
/// every `PLACE_SDRAM_BSS`/`PLACE_INTERNAL_FRUNK` global — including the allocator's
/// own metadata arrays — started each boot holding whatever the last session left in
/// RAM. Casting each raw pointer to `usize` first keeps this as plain integer
/// arithmetic, with no provenance claim for the optimiser to exploit.
unsafe fn zero(start: *const u8, end: *const u8) {
    let len = (end as usize) - (start as usize);
    if len > 0 {
        unsafe { core::ptr::write_bytes(start as *mut u8, 0, len) };
    }
}
