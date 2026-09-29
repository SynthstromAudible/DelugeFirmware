/* Supplementary linker fragment (added on top of rza1l.x via a second -T with
 * `INSERT AFTER .bss`): place the Deluge application's custom memory sections and
 * the C++ .init_array, and define the boundary symbols the app's
 * GeneralMemoryAllocator reads (general_memory_allocator.cpp):
 *
 *   stealable    = [__sdram_bss_end, RUST_SDRAM_BASE)                -- SDRAM
 *   internal     = [__heap_start,    program_stack_start)            -- SRAM
 *   internal_sml = [__frunk_bss_end, __frunk_slack_end)              -- SRAM (frunk)
 *
 * `program_stack_start` comes from rza1l.x (top of SRAM). The .frunk_bss/.heap
 * are INTERNAL (PLACE_INTERNAL_FRUNK), i.e. SRAM — NOT SDRAM.
 *
 * Loadable SDRAM content (.sdram_*) gets a VMA in SDRAM but an LMA in SRAM
 * (`> SDRAM AT> RAM`) since the debugger can't write SDRAM at download; the Rust
 * boot copies it after init_clocks. NOLOAD bss is zeroed by the boot.
 *
 * Ordering note: the SDRAM sections come first so that the LAST section before
 * the bare `__heap_start = .` is a `> RAM` section — otherwise `.` tracks the
 * SDRAM VMA and __heap_start lands in SDRAM (the bug that put it at 0x0c0181a0).
 */
SECTIONS {
    /* ---- External (SDRAM) ---- */
    /* Initialised SDRAM content: VMA in SDRAM, LMA in SRAM (copied at boot). */
    .sdram_init : {
        PROVIDE(__sdram_init_start = .);
        *(.sdram_text .sdram_text*)
        *(.sdram_data .sdram_data*)
        *(.sdram_rodata .sdram_rodata*)
        PROVIDE(__sdram_init_end = .);
    } > SDRAM AT> RAM
    PROVIDE(__sdram_init_lma = LOADADDR(.sdram_init));

    /* SDRAM zero-init region (NOLOAD) — start of the SDRAM "stealable" heap.
     *
     * The boundary symbols are defined unconditionally, NOT with PROVIDE. PROVIDE
     * emits a symbol only if something still references it, which made the symbol
     * table mirror the optimiser instead of describing the layout: when the boot's
     * zeroing loop was silently optimised away (see boot_mem.rs's `zero`), the
     * `_start` symbols simply vanished from the ELF and nothing failed. Defining
     * them outright means they always describe the region, so `nm` can be trusted
     * and a future disappearance of the zeroing shows up as dead code rather than
     * as a missing symbol nobody looks for. */
    .sdram_bss (NOLOAD) : {
        __sdram_bss_start = .;
        *(.sdram_bss .sdram_bss*)
        __sdram_bss_end = .;
    } > SDRAM

    /* ---- Internal (SRAM) ---- */
    /* The app's small-alloc "frunk" arena (PLACE_INTERNAL_FRUNK): the allocator
     * metadata arrays live in .frunk_bss; the trailing slack is the
     * "small internal" allocation heap [__frunk_bss_end, __frunk_slack_end). */
    .frunk_bss (NOLOAD) : ALIGN(16) {
        __frunk_bss_start = .; /* unconditional, as for .sdram_bss above */
        *(.frunk_bss .frunk_bss*)
        __frunk_bss_end = .;
        . += 0x8000; /* 32 KB small-internal heap */
        __frunk_slack_end = .;
    } > RAM

    /* C++ global constructors — loadable in SRAM, iterated by the Rust boot. */
    .init_array : ALIGN(4) {
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);
    } > RAM

    /* NOTE: __heap_start (the app's internal-heap BASE) is defined by rza1l.x as
     * __sram_heap_start (= end of the WHOLE image). It must NOT be set here: this
     * fragment is INSERT'd before .text/.rodata/.data, so any `.`/__init_array_end
     * value here lands mid-image and the app heap would overrun the code. */
} INSERT AFTER .bss;
