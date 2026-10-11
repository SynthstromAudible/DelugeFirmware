/* Supplementary linker fragment, added on top of rza1l.x by a second -T: places the Deluge
 * application's custom memory sections and the C++ .init_array, and defines the boundary symbols
 * the boot (boot_mem.rs) and the memory regions (services.rs) read:
 *
 *   SDRAM heap   = [__sdram_static_end, RUST_SDRAM_BASE)                  -- region 0
 *   internal_sml = [__frunk_bss_end,    __frunk_slack_end)                -- SRAM (frunk)
 *
 * The SRAM heap (region 1) is rza1l.x's [__sram_heap_start, __sram_heap_end).
 *
 * Loadable SDRAM content (.sdram_init) gets a VMA in SDRAM but an LMA in SRAM (`> SDRAM AT> RAM`),
 * since the debugger can't write SDRAM at download; the Rust boot copies it after init_clocks.
 * NOLOAD bss is zeroed by the boot.
 *
 * An input section goes to the FIRST output section whose pattern matches it, in command-line
 * script order and before the INSERTs are applied. rza1l.x's `.bss` takes every `.bss.*` and its
 * `.data` every `.data.*`, so this fragment is passed before rza1l.x (build/device.rs) for its
 * patterns that move statics to SDRAM to win. The INSERTs then place the sections: `.sdram_bss`
 * before `.bss`, the rest after it.
 */

/* ---- External (SDRAM), zero-initialised ---- */
SECTIONS {
    /* SDRAM zero-init region (NOLOAD).
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
        /* The storage channels' state (request queues, results, and the embassy task pools,
         * which `#[embassy_executor::task]` generates with no way to place them) and the efatfs
         * mount's tables. Only the storage tasks and their C-ABI entry points touch them, and
         * none is a DMA target: SD transfers go through SD_DMA_BUF. Matched by the static's
         * v0-mangled module path. A rename of either module silently returns its statics to
         * SRAM, which shows as lost headroom, never as a fault. */
        *(.bss._RNv*15deluge_bsp_rust7channel*)
        *(.bss._RNv*15deluge_bsp_rust7storage6efatfs6device*)
        __sdram_bss_end = .;
    } > SDRAM
} INSERT BEFORE .bss;

/* ---- External (SDRAM), initialised; and the internal (SRAM) extras ---- */
SECTIONS {
    /* Initialised SDRAM content: VMA in SDRAM, LMA in SRAM (copied at boot). */
    .sdram_init : {
        PROVIDE(__sdram_init_start = .);
        *(.sdram_text .sdram_text*)
        *(.sdram_data .sdram_data*)
        *(.sdram_rodata .sdram_rodata*)
        /* The efatfs mount's initialised tables (open handles, the mounted volume); as above. */
        *(.data._RNv*15deluge_bsp_rust7storage6efatfs6device*)
        PROVIDE(__sdram_init_end = .);
    } > SDRAM AT> RAM
    PROVIDE(__sdram_init_lma = LOADADDR(.sdram_init));

    /* The end of all static SDRAM content, where the SDRAM heap starts. `.sdram_bss` precedes
     * `.sdram_init` in the region (see the block order above), but neither the boot nor the heap
     * relies on which comes first. */
    __sdram_static_end = MAX(__sdram_bss_end, ADDR(.sdram_init) + SIZEOF(.sdram_init));

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

    /* NOTE: the SRAM heap's base is rza1l.x's __sram_heap_start (= end of the WHOLE image). It
     * must NOT be set here: this block is INSERT'd before .text/.rodata/.data, so any `.` value
     * here lands mid-image and the heap would overrun the code. The last section in this block is
     * a `> RAM` one, so `.` leaves it tracking SRAM, not the SDRAM VMA. */
} INSERT AFTER .bss;
