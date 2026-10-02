//! The boot latch (`libdeluge/boot.h`): false until `deluge_app_init` returns, then true for good.
use core::sync::atomic::{AtomicBool, Ordering};

static FINISHED: AtomicBool = AtomicBool::new(false);

/// Marks boot finished. Idempotent.
pub fn mark_finished() {
    FINISHED.store(true, Ordering::Release);
}

/// Returns whether boot has finished.
pub fn finished() -> bool {
    FINISHED.load(Ordering::Acquire)
}

/// Returns whether boot has finished; the C-ABI form of [`finished`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_boot_finished() -> bool {
    finished()
}

abi_pin!(deluge_boot_finished);
