//! The deterministic virtual clock behind the discrete-event loop: [`PeekableMockDriver`].
//!
//! `embassy_time::MockDriver` has no way to ask for the next scheduled deadline, which jumping
//! straight to the next due timer needs; this is the same kind of driver with that peek added.
use core::cell::RefCell;
use core::task::Waker;

use critical_section::Mutex as CsMutex;
use embassy_time_queue_utils::Queue;

/// The process-wide `embassy_time_driver::Driver`: a virtual clock advanced only by
/// [`advance_ticks`](Self::advance_ticks).
pub struct PeekableMockDriver(CsMutex<RefCell<Inner>>);

/// Virtual clock state: the current tick and the queue of pending timer wakers.
struct Inner {
    now: u64,
    queue: Queue,
}

// The clock starts at tick 0 with an empty wake queue.
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
    /// A deadline that is already due fires at once rather than at the next `advance_ticks`.
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

    /// Advances virtual time by `ticks`, waking every timer now due.
    pub fn advance_ticks(&self, ticks: u64) {
        critical_section::with(|cs| {
            let mut inner = self.0.borrow_ref_mut(cs);
            inner.now += ticks;
            let now = inner.now;
            inner.queue.next_expiration(now);
        });
    }

    /// Returns the next scheduled wake tick, or `None` if no timer is outstanding.
    ///
    /// Also wakes anything already due (the queue's peek fires too), so it is a pure peek only at
    /// a quiescence point after [`advance_ticks`](Self::advance_ticks), which is where the loop
    /// calls it.
    pub fn peek_next_deadline(&self) -> Option<u64> {
        critical_section::with(|cs| {
            let mut inner = self.0.borrow_ref_mut(cs);
            let now = inner.now;
            let next = inner.queue.next_expiration(now);
            (next != u64::MAX).then_some(next)
        })
    }
}
