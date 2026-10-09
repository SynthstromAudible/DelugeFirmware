//! `libdeluge/fault.h` — what the portable crash reporter needs from this board.
//!
//! The pad-grid pattern is the app's (`src/deluge/io/debug/fault_pattern.c`), so a photographed
//! panel decodes the same on any board. Only the board-specific parts live here: getting bytes to
//! the PIC, and saying which addresses are code or stack.
//!
//! Everything here runs from a CPU fault vector (no executor, a possibly wrecked heap, interrupts
//! masked) or from FREEZE_WITH_ERROR, whose freeze the user can resume. The transport is therefore
//! a bare FIFO poll, under its own critical section; [`control`](super::control)'s queue and
//! `deluge_bsp::pic`'s async senders need a live executor and would hang. It leaves the PIC's TX
//! DMA channel set up, so those senders work again after a resumed freeze.

use deluge_bsp::uart::PIC_CH;

use crate::sys::DelugeFaultRanges;

/// Capacity of the static staging buffer that [`deluge_fault_pad_write`] fills until the flush.
///
/// The pattern is at most 393 bytes (9 column-pair selects plus 8 pairs × 16 pads × 3 colour
/// bytes), so this never wraps. Static, because a fault handler cannot allocate.
const STAGE_CAP: usize = 512;
static mut STAGE: [u8; STAGE_CAP] = [0; STAGE_CAP];
static mut STAGED: usize = 0;

/// Writes the code and stack bounds the reporter uses to classify fault-time pointers.
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
    // SAFETY: `out` is non-null and caller-owned; the externs are linker address markers, taken
    // by `addr_of!` (never `&`) so only their addresses are used.
    unsafe {
        // Code is `[_start, __sram_heap_start)`: every return address is in `.text`, and the range
        // excludes `.bss`, the heap and the stacks, whose words would otherwise pass as return
        // addresses and send a reader after phantom frames. `.rodata` is still inside (no linker
        // symbol marks the text/rodata boundary), so a rodata pointer can slip through.
        (*out).code_start = core::ptr::addr_of!(_start) as usize;
        (*out).code_end = core::ptr::addr_of!(__sram_heap_start) as usize;
        (*out).stack_start = core::ptr::addr_of!(program_stack_start) as usize;
        (*out).stack_end = core::ptr::addr_of!(program_stack_end) as usize;
        // No alternate stack: application code runs only on the program stack.
        (*out).alt_stack_start = 0;
        (*out).alt_stack_end = 0;
    }
}

/// Reads the Data Fault Address Register: the address the faulting access tried.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_fault_data_address() -> usize {
    let dfar: usize;
    // SAFETY: reads CP15 c6 (DFAR), which has no side effects.
    unsafe {
        core::arch::asm!("mrc p15, 0, {}, c6, c0, 0", out(reg) dfar, options(nomem, nostack, preserves_flags));
    }
    dfar
}

/// Stages one byte for the PIC.
///
/// Silently drops past [`STAGE_CAP`]: a truncated pattern still shows the leading pointers.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_fault_pad_write(byte: u8) {
    // SAFETY: one reporter at a time: `deluge_cpu_fault_report` admits one and parks every later
    // arrival, and a freeze runs on the app's task with nothing else drawing a pattern.
    unsafe {
        let n = STAGED;
        if n < STAGE_CAP {
            STAGE[n] = byte;
            STAGED = n + 1;
        }
    }
}

/// Pushes the staged bytes to the PIC and blocks until they are on the wire.
///
/// Lets a PIC TX DMA transfer that is already running finish first: writing the FIFO under it
/// would interleave two byte streams, and stopping it would cut a PIC message in half and leave
/// the PIC reading the pattern out of step. Every wait is bounded, so a PIC or DMA channel that
/// stops making progress cannot hang the report; only a channel that never finishes is stopped.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_fault_pad_flush() {
    /// Spin budget per FIFO top-up: far longer than a 16-byte FIFO takes to drain at 200 kbaud
    /// (~640 µs), and still finite.
    const SPIN_LIMIT: u32 = 2_000_000;
    /// Spin budget for a DMA transfer already running: a transfer is at most 64 bytes, ~20 ms
    /// even at the PIC's 31 250 baud power-on rate, and this allows several times that.
    const DMA_DRAIN_LIMIT: u32 = 16 * SPIN_LIMIT;

    let channel = deluge_bsp::system::PIC_DMA_TX_CH;
    // A freeze arrives with interrupts on. Mask them for the flush, so no sender starts a DMA
    // transfer between the drain and the FIFO writes; a fault vector has them masked already, and
    // this restores whichever state it found.
    critical_section::with(|_| {
        // SAFETY: interrupts are masked, so nothing else drives the PIC's TX DMA channel or the
        // SCIF FIFO until this returns.
        unsafe {
            let mut spins = 0u32;
            while rza1l_hal::dmac::transfer_in_flight(channel) {
                spins += 1;
                if spins >= DMA_DRAIN_LIMIT {
                    // The transfer is wedged: stop it rather than wait forever, and accept that
                    // the PIC may misread the start of the pattern.
                    rza1l_hal::dmac::stop(channel);
                    break;
                }
                core::hint::spin_loop();
            }

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

            // Let the last FIFO load reach the PIC before the caller parks or resumes.
            for _ in 0..SPIN_LIMIT {
                core::hint::spin_loop();
            }

            STAGED = 0;
        }
    });
}

abi_pin!(
    deluge_fault_data_address,
    deluge_fault_pad_flush,
    deluge_fault_pad_write,
    deluge_fault_ranges
);
