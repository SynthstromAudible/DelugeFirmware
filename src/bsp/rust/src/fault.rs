//! `libdeluge/fault.h` — what the portable crash reporter needs from this board.
//!
//! The pad-grid pattern itself is the app's (`src/deluge/io/debug/fault_pattern.c`), so a
//! photographed panel decodes the same whichever board drew it. Only the two
//! board-specific acts live here: getting a byte to the PIC, and saying
//! which addresses are code or stack.
//!
//! Everything in this file runs from a CPU fault vector. The Embassy executor is not
//! running, the heap may be wrecked, and interrupts are masked — so the transport here
//! is a bare FIFO poll. It cannot use [`crate::control`]'s queue or `deluge_bsp::pic`'s
//! async senders, both of which need a live executor to make progress: reaching for
//! them from a fault would simply hang, which is how a crash ends up invisible.

use deluge_bsp::uart::PIC_CH;

use crate::sys::DelugeFaultRanges;

/// Bytes staged by [`deluge_fault_pad_write`] until the flush.
///
/// The pattern is at most 9 column-pair selects plus 8 column-pairs × 16 pads × 3
/// colour bytes = 393 bytes, so this never has to wrap. Static, because a fault
/// handler cannot allocate.
const STAGE_CAP: usize = 512;
static mut STAGE: [u8; STAGE_CAP] = [0; STAGE_CAP];
static mut STAGED: usize = 0;

/// Code and stack bounds for classifying fault-time pointers.
///
/// `code_*` spans the whole on-chip SRAM image rather than a `.text`-only pair: this
/// BSP is RAM-linked and its code, rodata and data all sit in that window, so a
/// tighter range would need linker symbols the layout does not currently define, and
/// erring wide only costs the report an occasional false candidate — erring narrow
/// would silently drop real return addresses, which is worse.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_fault_ranges(out: *mut DelugeFaultRanges) {
    if out.is_null() {
        return;
    }
    unsafe extern "C" {
        static program_stack_start: u8;
        static program_stack_end: u8;
        /// Image entry point — the lowest `.text` address, so the bottom of the code range.
        static _start: u8;
        /// Base of the SRAM heap, which sits above `.text`/`.rodata` and below the stacks.
        static __sram_heap_start: u8;
    }
    // SAFETY: `out` is a valid DelugeFaultRanges the caller owns; the two externs are
    // linker-provided address markers, taken by `addr_of!` (never `&`) so the
    // subtraction stays plain integer arithmetic — see boot_mem::zero's doc for what
    // reference-derived pointer arithmetic did to the boot memsets.
    unsafe {
        // Code bounds, as tight as the available linker symbols allow: `[_start, __sram_heap_start)`.
        //
        // This used to be the whole on-chip SRAM window, 0x2000_0000..0x2030_0000, on the reasoning
        // that erring wide only costs an occasional false candidate. Measured on device, that was too
        // generous by far. The window also contains `.bss` (below `_start`), the SRAM heap and all
        // five mode stacks (above), so ANY word on the faulting stack that happened to point at RAM
        // passed the "is this a return address" test. A real E199 report came back with two of its
        // three stack-walk hits being bss addresses — `__bss_start__` and `__frunk_bss_end` — which
        // is worse than useless: a plausible-looking address that decodes to a variable sends whoever
        // is reading the panel after a phantom frame.
        //
        // Bounding it to the image's own text keeps every genuine return address (they are all in
        // `.text`, which begins at `_start`) while rejecting bss, heap and stack words outright.
        // `.rodata` is still inside the range — it sits between text and the heap and there is no
        // linker symbol here marking the text/rodata boundary — so a rodata pointer can still slip
        // through. That is a much smaller surface than the previous one.
        (*out).code_start = core::ptr::addr_of!(_start) as usize;
        (*out).code_end = core::ptr::addr_of!(__sram_heap_start) as usize;
        (*out).stack_start = core::ptr::addr_of!(program_stack_start) as usize;
        (*out).stack_end = core::ptr::addr_of!(program_stack_end) as usize;
        // No alternate stack: application code runs only on the program stack.
        (*out).alt_stack_start = 0;
        (*out).alt_stack_end = 0;
    }
}

/// Stage one byte for the PIC. Silently drops past [`STAGE_CAP`] — a truncated
/// pattern still shows the leading pointers, which is worth more than no pattern.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_fault_pad_write(byte: u8) {
    // No re-entry guard needed here: `handle_cpu_fault` admits one reporter and parks
    // every later arrival, so only one caller ever reaches this.
    // SAFETY: single-threaded fault context with interrupts masked; nothing else
    // touches STAGE/STAGED once a fault has begun.
    unsafe {
        let n = STAGED;
        if n < STAGE_CAP {
            STAGE[n] = byte;
            STAGED = n + 1;
        }
    }
}

/// Push the staged bytes to the PIC and block until they are on the wire.
///
/// Takes the UART away from its DMA channel first. The PIC TX is normally driven by
/// DMA (see `deluge_bsp::uart`'s `init_dma_tx`), and writing the FIFO underneath a
/// live DMA transfer would interleave two byte streams and corrupt the pattern. In a
/// fault nothing is going to want the DMA channel back.
///
/// Every wait is bounded. A PIC that has stopped accepting bytes must lose the race
/// rather than hang the report — the pattern is the only thing left that can say what
/// happened.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_fault_pad_flush() {
    /// Spin budget per FIFO top-up. At 200 kbaud a 16-byte FIFO drains in ~640 µs;
    /// this is far longer than that and still finite.
    const SPIN_LIMIT: u32 = 2_000_000;

    // SAFETY: fault context, interrupts masked. Stopping the DMA channel and writing
    // the SCIF FIFO directly is sound precisely because no driver will run again.
    unsafe {
        rza1l_hal::dmac::stop(deluge_bsp::system::PIC_DMA_TX_CH);

        let staged = STAGED;
        let mut sent = 0usize;
        let mut spins = 0u32;
        while sent < staged {
            let wrote = rza1l_hal::uart::try_write_fifo(PIC_CH, &STAGE[sent..staged]);
            if wrote == 0 {
                spins += 1;
                if spins >= SPIN_LIMIT {
                    break; // transmitter wedged — give up rather than hang the report
                }
                core::hint::spin_loop();
                continue;
            }
            sent += wrote;
            spins = 0;
        }

        // Let the last FIFO load reach the PIC before the caller parks forever.
        for _ in 0..SPIN_LIMIT {
            core::hint::spin_loop();
        }

        STAGED = 0;
    }
}
