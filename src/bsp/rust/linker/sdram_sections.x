/* Supplementary linker fragment: place the Deluge application's custom
 * SDRAM-resident sections (PLACE_SDRAM_* / frunk) into the SDRAM region and
 * define the boundary symbols the app/allocator reference. Added on top of
 * rza1l.x (which has no knowledge of these app sections) via a second -T with
 * `INSERT AFTER`.
 *
 * M1 TODO: rza1l_hal::allocator::SDRAM currently claims all 64 MB from
 * 0x0C000000; it must instead start at __sdram_bss_end so it doesn't overlap
 * these linker-placed sections. The heap/stack symbols below are placeholders
 * for M0 link-clean and need real values when the memory model is reconciled.
 */
SECTIONS {
    .frunk_bss (NOLOAD) : {
        PROVIDE(__frunk_bss_start = .);
        *(.frunk_bss .frunk_bss*)
        PROVIDE(__frunk_bss_end = .);
        PROVIDE(__frunk_slack_end = .);
    } > SDRAM

    .sdram_text : {
        *(.sdram_text .sdram_text*)
    } > SDRAM

    .sdram_data : {
        PROVIDE(__sdram_data_start = .);
        *(.sdram_data .sdram_data*)
        PROVIDE(__sdram_data_end = .);
    } > SDRAM

    .sdram_rodata : {
        *(.sdram_rodata .sdram_rodata*)
    } > SDRAM

    .sdram_bss (NOLOAD) : {
        PROVIDE(__sdram_bss_start = .);
        *(.sdram_bss .sdram_bss*)
        PROVIDE(__sdram_bss_end = .);
    } > SDRAM

    /* App heap follows the SDRAM-placed data (M0 placeholder boundaries). */
    PROVIDE(__heap_start__ = .);
    PROVIDE(__heap_start = .);
    PROVIDE(program_stack_end = ORIGIN(SDRAM) + LENGTH(SDRAM));
} INSERT AFTER .bss;
