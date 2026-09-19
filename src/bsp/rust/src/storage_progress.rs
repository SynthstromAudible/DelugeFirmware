//! One counter shared by every storage worker: "did any storage driver get to run?".
//!
//! A caller that has asked the channel many times while this has not moved once is not waiting on a
//! slow card -- it is starving the driver, because it never yielded. That is the one shape the
//! elapsed-time bound in `recorder_write_policy.h` cannot see: a tight spinner freezes the executor,
//! so virtual time never advances and the deadline never arrives.
//!
//! Shared rather than per-channel on purpose. "Nothing but me has run" is a statement about the whole
//! executor, and a per-channel counter could not answer it anyway: a worker parked inside a
//! legitimately slow operation looks exactly like a wedged one.

use core::sync::atomic::{AtomicU32, Ordering};

static PROGRESS: AtomicU32 = AtomicU32::new(0);

/// Called by each storage worker whenever its state machine advances: it woke, or it took a request.
pub fn note_progress() {
    PROGRESS.fetch_add(1, Ordering::Relaxed);
}

/// Monotonic, and WRAPS. Callers compare successive readings for inequality, never ordering.
/// [task] [isr]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_storage_driver_progress() -> u32 {
    PROGRESS.load(Ordering::Relaxed)
}
