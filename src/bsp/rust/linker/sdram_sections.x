/* Supplementary linker fragment, added to rza1-rt's link.x by its own -T: places the Deluge
 * application's custom memory sections and the C++ .init_array, and defines the boundary symbols
 * the boot (boot_mem.rs) and the memory regions (services.rs) read:
 *
 *   SDRAM heap   = [__sdram_static_end, RUST_SDRAM_BASE)                  -- region 0
 *   internal_sml = [__frunk_bss_end,    __frunk_slack_end)                -- SRAM (frunk)
 *
 * The SRAM heap (region 1) is rza1-rt's link.x's [__sram_heap_start, __sram_heap_end).
 *
 * Loadable SDRAM content (.sdram_init) gets a VMA in SDRAM but an LMA in SRAM (`> SDRAM AT> RAM`),
 * since the debugger can't write SDRAM at download; the Rust boot copies it after Board::new.
 * NOLOAD bss is zeroed by the boot.
 *
 * An input section goes to the FIRST output section whose pattern matches it, in command-line
 * script order and before the INSERTs are applied. rza1-rt's link.x's `.bss` takes every `.bss.*` and its
 * `.data` every `.data.*`, so this fragment is passed before rza1-rt's link.x (build/device.rs) for its
 * patterns that move statics to SDRAM to win. The INSERTs then place the sections: `.sdram_bss`
 * before `.bss`, `.frunk_bss` after it, and the boot-only tail after `.ARM.exidx`.
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
        /* The scenes' statics: the menu's node-id tables, the scene singletons and their buffers,
         * which the UI tier alone touches, after the boot has zeroed this region. Matched by the
         * `deluge::scenes` namespace in the mangled name. */
        *(.bss.*6deluge6scenes*)
        __sdram_bss_end = .;
    } > SDRAM
} INSERT BEFORE .bss;

/* ---- Internal (SRAM), zero-initialised ---- */
SECTIONS {
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
} INSERT AFTER .bss;

/* ---- The image's tail: what only the boot reads ----
 *
 * The SDRAM content's load copy, compressed, and the C++ constructor list end the image, after
 * .ARM.exidx. The boot inflates the one into SDRAM and runs the other (boot_mem.rs) before the app
 * creates its heaps, and neither is read again, so the SRAM heap starts where this tail does
 * (`__sram_heap_base`, which rza1-rt's link.x honours). Placing them here also keeps the vector
 * table the image's first loadable byte, where the bootloader reads its header. */
SECTIONS {
    /* Initialised SDRAM content, linked at its SDRAM address only: the ELF carries the bytes, the
     * image carries them deflated in .sdram_init_pack below. The build's staging
     * (scripts/tasks/sdram_pack.py) packs them and makes this section no-load, so neither the image
     * nor a debugger's load writes SDRAM before the boot has started it. */
    .sdram_init : ALIGN(16) {
        PROVIDE(__sdram_init_start = .);
        *(.sdram_text .sdram_text*)
        *(.sdram_data .sdram_data*)
        *(.sdram_rodata .sdram_rodata*)
        /* The efatfs mount's initialised tables (open handles, the mounted volume); as above. */
        *(.data._RNv*15deluge_bsp_rust7storage6efatfs6device*)
        /* The setting descriptor and name tables, with their extras and conditions: every C++
         * `menu_tables` namespace's read-only data. A menu or an Edit reads them, never the audio
         * tier or an interrupt. Matched by the namespace in the mangled name, so a rename returns
         * them to SRAM, as for the storage statics above. */
        __menu_tables_start = .;
        *(.rodata.*11menu_tables*)
        __menu_tables_end = .;
        /* The scenes' menu: its node and cell tables, read by the menu scene and its shortcuts, never
         * the audio tier or an interrupt. Matched by the `deluge::scenes::menu` namespace in the
         * mangled name, so a rename returns them to SRAM, as for the setting tables above. */
        __scenes_menu_tables_start = .;
        *(.rodata.*6deluge6scenes4menu*)
        __scenes_menu_tables_end = .;
        /* The menus' vtables: the legacy menu items' and the scenes'. Virtual calls on a menu item
         * or a scene come from the UI tier only. */
        *(.rodata._ZTV*9menu_item*)
        *(.rodata._ZTV*6deluge6scenes*)
        PROVIDE(__sdram_init_end = .);
    } > SDRAM

    /* The deflated load copy, reserved zero-filled at an eighth of the content (the setting and
     * menu tables deflate to about a tenth) and written after the link; the staging fails when it
     * does not fit. */
    .sdram_init_pack : ALIGN(16) {
        __sdram_init_pack_start = .;
        BYTE(0)
        . = __sdram_init_pack_start + SIZEOF(.sdram_init) / 8 + 256;
        __sdram_init_pack_end = .;
    } > RAM

    /* The code that inflates it (boot_mem.rs, sdram_pack.rs and the inflate crate it calls), run
     * once before the heap exists: it is part of the tail the heap takes back. */
    .boot_text : ALIGN(4) {
        *(.text.*8boot_mem*)
        *(.text.*10sdram_pack*)
        *(.text.*11miniz_oxide*)
        *(.rodata.*11miniz_oxide*)
    } > RAM

    /* The end of all static SDRAM content, where the SDRAM heap starts. `.sdram_bss` precedes
     * `.sdram_init` in the region, but neither the boot nor the heap relies on which comes first. */
    __sdram_static_end = MAX(__sdram_bss_end, ADDR(.sdram_init) + SIZEOF(.sdram_init));

    /* C++ global constructors, iterated by the Rust boot. Last, and `> RAM`, so the location
     * counter leaves this block tracking SRAM rather than the SDRAM VMA: rza1-rt's link.x takes the
     * image's end from it. */
    .init_array : ALIGN(4) {
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);
    } > RAM

    __sram_heap_base = ALIGN(__sdram_init_pack_start, 16);

    /* The setting tables are about 16 KB: far less here means the pattern above no longer
     * matches them and they sit in SRAM's .rodata. */
    ASSERT(__menu_tables_end - __menu_tables_start > 8192,
           "sdram_sections.x: the setting tables fell back to SRAM (no menu_tables data in .sdram_init)")

    /* The scenes' menu tables are about 119 KB: far less here means the pattern above no longer
     * matches them and they sit in SRAM's .rodata. */
    ASSERT(__scenes_menu_tables_end - __scenes_menu_tables_start > 65536,
           "sdram_sections.x: the scenes' menu tables fell back to SRAM (no deluge::scenes::menu data in .sdram_init)")
} INSERT AFTER .ARM.exidx;
