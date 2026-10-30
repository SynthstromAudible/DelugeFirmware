/* Board memory layout for the Synthstrom Deluge (RZ/A1L) — RTT enabled.
 * INCLUDE'd by rza1-rt's link.x (its RTT form) when built with --features rtt
 * (the default). Mirrors deluge-sdk's deluge-bsp/memory_rtt.x, with a smaller RTT reserve. */

MEMORY {
    /* 2.875 MB on-chip SRAM, minus the RTT region carved out below. */
    RAM (rwx) : ORIGIN = 0x20020000, LENGTH = 0x002E0000

    /* 64 MB external SDRAM (CS3) */
    SDRAM (rwx) : ORIGIN = 0x0C000000, LENGTH = 0x04000000

    /* RTT ring buffer, at the top of SRAM immediately below the exception/program
       stacks (which start at INTERNAL_RAM_END - 0x10000 = 0x202F0000). Placing it
       here — rather than mid-SRAM — lets the image + `__sram_heap` span grow all
       the way up to the RTT region instead of being capped ~192 KB lower, which is
       what a large live component (e.g. the embedded-fatfs mount)
       needs to fit at debug opt-levels. RTT addresses are auto-discovered from the
       ELF's `_SEGGER_RTT` symbol (deluge_run.py / cortex-debug `address: auto`), so
       no tooling address is hard-coded to the value below.

       SIZED TO THE BUFFER, NOT ROUNDED UP TO 64 KB. `.rtt_buffer` occupies 16,432
       bytes; the old 64 KB region over-reserved by ~48 KB. Note this size sets the
       image ceiling directly: SRAM is allocated UPWARDS from 0x20020000, so the
       image collides with ORIGIN, and shrinking the region only helps if ORIGIN
       moves up with it. Both must change together, which is why the size is a
       symbol used by both.

       0x5000 = 20 KB: the 16,432-byte buffer plus slack, 4 KB-aligned. If the
       buffer outgrows it, `.rtt_buffer` overflows NCACHE_RTT_RAM and the link
       fails loudly rather than silently colliding with the stacks.

       The uncached mirror is a HARDWARE address alias (+0x40000000), not an MMU
       mapping — the MMU only sets cache attributes, at 1 MB section granularity —
       so the reserve has no page-granularity constraint. It only has to cover the
       same physical bytes, rounded to a 32-byte cache line so no line straddles
       the boundary between the uncached buffer and cached data above it.

       Cached alias: 0x202EB000–0x202EFFFF; uncached mirror is +0x40000000. */
    RTT_RAM   (rw) : ORIGIN = 0x202EB000, LENGTH = 0x00005000  /* 20 KB cached   */
    NCACHE_RTT_RAM (rw): ORIGIN = 0x602EB000, LENGTH = 0x00005000  /* 20 KB uncached */
}

/* Stack sizes */
PROGRAM_STACK_SIZE = 0x8000;   /* 32 KB - application / SYS mode */
IRQ_STACK_SIZE     = 0x2000;   /*  8 KB */
FIQ_STACK_SIZE     = 0x2000;   /*  8 KB */
SVC_STACK_SIZE     = 0x2000;   /*  8 KB */
ABT_STACK_SIZE     = 0x2000;   /*  8 KB */

/* Top byte address of on-chip SRAM (anchors stack sections) */
INTERNAL_RAM_END = 0x20300000;
