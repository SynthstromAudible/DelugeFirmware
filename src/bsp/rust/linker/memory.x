/* Board memory layout for the Synthstrom Deluge (RZ/A1L) — RTT disabled.
 * INCLUDE'd by rza1l.x (from the rza1l-hal crate). Matches the C++ firmware's
 * linker_script_rz_a1l.ld and deluge-sdk's controller-firmware/memory.x. */

MEMORY {
    /* 2.875 MB on-chip SRAM (pages 0-4, non-retention banks).
       0x20000000-0x2001FFFF is data-retention RAM; the bootloader refuses to
       copy a firmware image whose CODE_START falls below 0x20020000, so the
       image must start at or above that address. */
    RAM (rwx) : ORIGIN = 0x20020000, LENGTH = 0x002E0000

    /* 64 MB external SDRAM (CS3) */
    SDRAM (rwx) : ORIGIN = 0x0C000000, LENGTH = 0x04000000
}

/* Stack sizes */
PROGRAM_STACK_SIZE = 0x8000;   /* 32 KB - application / SYS mode */
IRQ_STACK_SIZE     = 0x2000;   /*  8 KB */
FIQ_STACK_SIZE     = 0x2000;   /*  8 KB */
SVC_STACK_SIZE     = 0x2000;   /*  8 KB */
ABT_STACK_SIZE     = 0x2000;   /*  8 KB */

/* Top byte address of on-chip SRAM (anchors stack sections) */
INTERNAL_RAM_END = 0x20300000;
