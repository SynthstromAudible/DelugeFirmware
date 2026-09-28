//! [`PeekableMockDriver`]: the deterministic virtual clock behind this
//! crate's discrete-event executor loop (advance to the next timer deadline,
//! poll to quiescence, repeat).
//!
//! `embassy_time::MockDriver` (the `mock-driver` feature's shipped type)
//! exposes `advance(Duration)` and `now()`/`get()` but no "what's the next
//! scheduled deadline" accessor: its internal `next_expiration` both peeks
//! and fires in one call, and the return value is never surfaced. Jumping
//! straight to the next due timer instead of stepping tick-by-tick needs
//! that peek, hence [`PeekableMockDriver`] — an independent implementation
//! of the same `embassy_time_driver::Driver` trait with the peek added.
use core::cell::RefCell;
use core::task::Waker;

use critical_section::Mutex as CsMutex;
use embassy_time_queue_utils::Queue;

/// Registered as this package's `embassy_time_driver::Driver` impl via
/// `time_driver_impl!` below. See the module docs for why it exists instead
/// of `embassy_time::MockDriver`.
pub struct PeekableMockDriver(CsMutex<RefCell<Inner>>);

/// Virtual clock state: the current tick plus the queue of pending timer
/// wakers.
struct Inner {
    now: u64,
    queue: Queue,
}

// Registers `DRIVER` as the process-wide `embassy_time_driver::Driver`,
// clock starting at tick 0 with an empty wake queue.
embassy_time_driver::time_driver_impl!(static DRIVER: PeekableMockDriver = PeekableMockDriver(
    CsMutex::new(RefCell::new(Inner { now: 0, queue: Queue::new() }))
));

impl embassy_time_driver::Driver for PeekableMockDriver {
    /// Returns the current virtual tick.
    fn now(&self) -> u64 {
        critical_section::with(|cs| self.0.borrow_ref(cs).now)
    }

    /// Registers `waker` to be woken once virtual time reaches `at`.
    ///
    /// Immediately re-runs `next_expiration` against the current tick so a
    /// deadline that's already due fires right away rather than waiting for
    /// the next `advance_ticks` call.
    fn schedule_wake(&self, at: u64, waker: &Waker) {
        critical_section::with(|cs| {
            let mut inner = self.0.borrow_ref_mut(cs);
            inner.queue.schedule_wake(at, waker);
            let now = inner.now;
            inner.queue.next_expiration(now);
        });
    }
}

impl PeekableMockDriver {
    /// Returns the process-wide driver instance.
    pub fn get() -> &'static Self {
        &DRIVER
    }

    /// Jump virtual time forward by `ticks`, synchronously firing (waking)
    /// every timer whose deadline is now `<= now`.
    pub fn advance_ticks(&self, ticks: u64) {
        critical_section::with(|cs| {
            let mut inner = self.0.borrow_ref_mut(cs);
            inner.now += ticks;
            let now = inner.now;
            inner.queue.next_expiration(now);
        });
    }

    /// The next scheduled wake tick, or `None` if no task has an outstanding
    /// timer.
    ///
    /// This is only safe to trust at a quiescence point: `next_expiration`
    /// both peeks and fires (wakes anything with `expires_at <= now`), but
    /// the driver loop only ever calls this right after `advance_ticks`,
    /// which already fired everything due at-or-before the current `now`.
    /// So in practice this call has nothing left to fire and just walks the
    /// queue for the soonest upcoming deadline.
    pub fn peek_next_deadline(&self) -> Option<u64> {
        critical_section::with(|cs| {
            let mut inner = self.0.borrow_ref_mut(cs);
            let now = inner.now;
            let next = inner.queue.next_expiration(now);
            (next != u64::MAX).then_some(next)
        })
    }
}
