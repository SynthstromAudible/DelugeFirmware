//! The boot latch: false until the app's run loop starts, then true for good. Set once, right after
//! `deluge_app_init` returns -- where the worker pump's first poll used to latch the same thing.
use core::sync::atomic::{AtomicBool, Ordering};

static FINISHED: AtomicBool = AtomicBool::new(false);

/// Mark boot finished. Idempotent.
pub fn mark_finished() {
    FINISHED.store(true, Ordering::Release);
}

/// Whether boot has finished.
pub fn finished() -> bool {
    FINISHED.load(Ordering::Acquire)
}

/// C-ABI twin of [`mark_finished`] (`include/libdeluge/boot.h`).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_boot_mark_finished() {
    mark_finished();
}

/// C-ABI twin of [`finished`] (`include/libdeluge/boot.h`).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_boot_finished() -> bool {
    finished()
}
