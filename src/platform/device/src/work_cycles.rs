//! The CPU cycles spent on the work at hand: the cycle counter less the time metered interrupts took.
//!
//! The USB interrupt moves audio packets thousands of times a second and runs inside whatever it
//! interrupts, so a span timed with the bare counter is charged for it. The USB handler is the one
//! that is metered (`peripherals::usb`); the cycles of entering and leaving it are not.
//!
//! Pure but for the meter, so the host tests cover it.

use core::sync::atomic::{AtomicU32, Ordering};

/// Cycles metered interrupts have taken, wrapping.
static INTERRUPT_CYCLES: AtomicU32 = AtomicU32::new(0);

/// Adds a metered interrupt's `cycles`. `[isr]`
pub fn add_interrupt(cycles: u32) {
    INTERRUPT_CYCLES.fetch_add(cycles, Ordering::Relaxed);
}

/// Cycles metered interrupts have taken, wrapping.
pub fn interrupt_cycles() -> u32 {
    INTERRUPT_CYCLES.load(Ordering::Relaxed)
}

/// `counter` less `meter`, both wrapping, read with no metered interrupt between the two reads: one
/// there would be in the meter and not in the counter.
pub fn read(mut meter: impl FnMut() -> u32, mut counter: impl FnMut() -> u32) -> u32 {
    loop {
        let before = meter();
        let now = counter();
        if meter() == before {
            return now.wrapping_sub(before);
        }
    }
}
