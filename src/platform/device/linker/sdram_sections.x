/* Supplementary linker fragment, added to rza1-rt's link.x by its own -T: places the Deluge
 * application's custom memory sections and the C++ .init_array, and defines the boundary symbols
 * the boot (boot_mem.rs) and the memory regions (services.rs) read:
 *
 *   SDRAM heap   = [__sdram_static_end, RUST_SDRAM_BASE)                  -- region 0
 *   internal_sml = [__frunk_bss_end,    __frunk_slack_end)                -- SRAM (frunk)
 *
 * The SRAM heap (region 1) is rza1-rt's link.x's [__sram_heap_start, __sram_heap_end).
 *
 * Initialised SDRAM content (.sdram_init) is linked at its SDRAM address only; the image carries it
 * deflated in .sdram_init_pack, written by the build's staging, since the debugger can't write
 * SDRAM at download. The Rust boot inflates it after Board::new. NOLOAD bss is zeroed by the boot.
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
    /* Not NOLOAD: an object with a non-zero initial value (a constructor the compiler evaluated) then
     * makes the section PROGBITS, which the build's post-link check (scripts/tasks/zero_sections.py)
     * rejects, naming it; NOLOAD would drop the value silently and the object would start as zeros. */
    .sdram_bss : {
        __sdram_bss_start = .;
        *(.sdram_bss .sdram_bss*)
        /* The storage channels' state (request queues, results, and the embassy task pools,
         * which `#[embassy_executor::task]` generates with no way to place them) and the efatfs
         * mount's tables. Only the storage tasks and their C-ABI entry points touch them, and
         * none is a DMA target: SD transfers go through SD_DMA_BUF. Matched by the static's
         * v0-mangled module path. A rename of either module silently returns its statics to
         * SRAM, which shows as lost headroom, never as a fault. */
        *(.bss._RNv*22deluge_platform_device7channel*)
        *(.bss._RNv*22deluge_platform_device7storage6efatfs6device*)
        /* The libraries scripts/layers.toml's [[placement]] table places in SDRAM: their zero-initialised data,
         * which their own tier touches after the boot has zeroed this region. */
        /* BEGIN placement bss */
        __placement_ui_bss_start = .;
        *(.bss.*6deluge2ui*)
        __placement_ui_bss_end = .;
        /* END placement bss */
        __sdram_bss_end = .;
    } > SDRAM
} INSERT BEFORE .bss;

/* ---- Internal (SRAM), zero-initialised ---- */
SECTIONS {
    /* The app's small-alloc "frunk" arena (PLACE_INTERNAL_FRUNK): the allocator
     * metadata arrays live in .frunk_bss; the trailing slack is the
     * "small internal" allocation heap [__frunk_bss_end, __frunk_slack_end). */
    .frunk_bss : ALIGN(16) { /* not NOLOAD, as .sdram_bss above */
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
        /* The code run from SDRAM, which the crash reporter counts as code (peripherals/fault.rs). */
        __sdram_text_start = .;
        /* The libraries scripts/layers.toml's [[placement]] table places in SDRAM, by namespace. */
        /* BEGIN placement text */
        /* deluge::ui: the UI tier, at menu and input rate; it reaches the audio tier only through base/notify queues */
        __placement_ui_text_start = .;
        *(.text.*6deluge2ui*)
        __placement_ui_text_end = .;
        /* END placement text */
        *(.sdram_text .sdram_text*)
        /* The GUI's code (build/device.rs archives it apart, built without LTO so its objects
         * survive to the link). An inline or template function that the GUI shares with other
         * code comes here too when the link keeps the GUI's copy. */
        *libdeluge_app_gui.a:*(.text .text.*)
        /* The UI's strings code (gui/l10n), a library of its own, built without LTO for the same reason. */
        *libdeluge_l10n.a:*(.text .text.*)
        __sdram_text_end = .;
        /* The placed libraries' read-only and initialised data, their vtables among them. */
        /* BEGIN placement data */
        __placement_ui_data_start = .;
        *(.rodata.*6deluge2ui* .data.*6deluge2ui*)
        __placement_ui_data_end = .;
        /* END placement data */
        *(.sdram_data .sdram_data*)
        *(.sdram_rodata .sdram_rodata*)
        /* The efatfs mount's initialised tables (open handles, the mounted volume); as above. */
        *(.data._RNv*22deluge_platform_device7storage6efatfs6device*)
        /* The setting descriptor and name tables, with their extras and conditions: every C++
         * `menu_tables` namespace's read-only data. A menu or an Edit reads them, never the audio
         * tier or an interrupt. Matched by the namespace in the mangled name, so a rename returns
         * them to SRAM, as for the storage statics above. */
        __menu_tables_start = .;
        *(.rodata.*11menu_tables*)
        __menu_tables_end = .;
        /* The DEVICE rows' path handles (`deluge::project::device_path`'s constants), which the scenes and
         * the host's device port read; their text is `menu_tables` data, above. Matched by the struct's
         * name in the mangled name, so a rename returns them to SRAM. */
        *(.rodata.*6deluge7project11device_path*)
        /* The host's port objects and their vtables (`deluge::gui::shell_host`), the waveforms port's
         * column caches among them (its object is `constinit`, so `.data`), which only the
         * scenes and the GUI's own code, already here, call through. */
        *(.data.*6deluge3gui10shell_host* .rodata.*6deluge3gui10shell_host*)
        /* The C++ unwind tables' entries, which rza1-rt's link.x would place in SRAM; the index is
         * .sdram_exidx below. */
        . = ALIGN(4);
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > SDRAM

    /* The C++ unwind index. Only a throw reads it and the tables above, never before the boot has
     * inflated them and rarely after, so they cost SDRAM rather than SRAM. Their entries are prel31
     * offsets, which reach the SRAM code from here (+-1 GB). Its own output section because lld
     * puts SHT_ARM_EXIDX in no PROGBITS one; right after .sdram_init, so the pack covers both as one
     * span (scripts/tasks/sdram_pack.py) and the boot inflates [__sdram_init_start, __sdram_init_end). */
    .sdram_exidx : ALIGN(4) {
        __sdram_exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __sdram_exidx_end = .;
    } > SDRAM
    __sdram_init_end = __sdram_exidx_end;
    /* The unwinder reads only .sdram_exidx (build/device.rs points __exidx_start/__exidx_end at it),
     * so an entry left in link.x's .ARM.exidx would make every throw through it terminate. */
    ASSERT(SIZEOF(.ARM.exidx) == 0 && __sdram_exidx_end > __sdram_exidx_start,
           "sdram_sections.x: the unwind index is not all in .sdram_exidx")

    /* The deflated load copy, reserved zero-filled at half the content and written after the link; the
     * staging fails when it does not fit. The content deflates to about 48% (code to about half, the setting
     * and menu tables to about a tenth), so the reserve keeps a margin of about 2%. Every byte of it is room
     * the image needs below the RTT reserve, though not runtime SRAM: the SRAM heap starts here. */
    .sdram_init_pack : ALIGN(16) {
        __sdram_init_pack_start = .;
        BYTE(0)
        . = __sdram_init_pack_start + (__sdram_init_end - __sdram_init_start) / 2 + 256;
        __sdram_init_pack_end = .;
    } > RAM

    /* The end of all static SDRAM content, where the SDRAM heap starts. `.sdram_bss` precedes
     * `.sdram_init` in the region, but neither the boot nor the heap relies on which comes first. */
    __sdram_static_end = MAX(__sdram_bss_end, __sdram_init_end);

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

    /* The heap runs from the tail to __sram_heap_end, so every zero-init section must lie below the
     * tail: one above it would be inside the heap. */
    ASSERT(__bss_end__ <= __sram_heap_base && __frunk_slack_end <= __sram_heap_base && ttb_mmu1_end <= __sram_heap_base,
           "sdram_sections.x: zero-init data lies above the boot-only tail, inside the SRAM heap")

    /* The setting tables are about 16 KB: far less here means the pattern above no longer
     * matches them and they sit in SRAM's .rodata. */
    ASSERT(__menu_tables_end - __menu_tables_start > 8192,
           "sdram_sections.x: the setting tables fell back to SRAM (no menu_tables data in .sdram_init)")

    /* The placed libraries' minimum sizes (scripts/layers.toml [[placement]]): far less means a rename stopped a
     * pattern matching and the library sits in SRAM. */
    /* BEGIN placement asserts */
    ASSERT(__placement_ui_text_end - __placement_ui_text_start > 153600,
           "sdram_sections.x: deluge::ui's code fell back to SRAM (scripts/layers.toml [[placement]])")
    ASSERT(__placement_ui_data_end - __placement_ui_data_start > 65536,
           "sdram_sections.x: deluge::ui's data fell back to SRAM (scripts/layers.toml [[placement]])")
    ASSERT(__placement_ui_bss_end - __placement_ui_bss_start > 8192,
           "sdram_sections.x: deluge::ui's zero-initialised data fell back to SRAM (scripts/layers.toml [[placement]])")
    /* END placement asserts */

    /* The SRAM heap's floor: the on-chip memory left between the image and the RTT reserve. rza1-rt's
     * link.x refuses an image that leaves none; this refuses one that leaves less than the floor. The
     * heap is filled at boot whatever its size (alloc_fast falls back to SDRAM), so a smaller one
     * costs speed rather than memory, but each SRAM-resident addition comes out of it. A test image
     * that carries extra code (the rig harness) passes a lower floor as __sram_heap_floor_test. */
    __sram_heap_floor = DEFINED(__sram_heap_floor_test) ? __sram_heap_floor_test : 100 * 1024;
    ASSERT(__sram_heap_base <= __sram_heap_end && __sram_heap_end - __sram_heap_base >= __sram_heap_floor,
           "sdram_sections.x: the SRAM heap is below its floor (__sram_heap_floor): the image grew into it")
} INSERT AFTER .ARM.exidx;
