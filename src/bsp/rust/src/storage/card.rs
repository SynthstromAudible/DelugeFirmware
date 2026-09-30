//! Card ejection (`libdeluge/card.h`): the card generation and the flush of every loader's
//! card-derived state.
//!
//! Every loader that caches something read from the card stamps it with [`generation`], read where
//! the load begins and compared where the result is taken. Sampling it at publish time instead
//! would read the generation a flush has already installed and let a stale result through.

use core::sync::atomic::{AtomicU32, Ordering};

/// The current card generation, bumped once per ejection. Wraps; only compared with `!=`.
static GENERATION: AtomicU32 = AtomicU32::new(0);

/// Returns the generation a load starting now belongs to.
pub fn generation() -> u32 {
    GENERATION.load(Ordering::Acquire)
}

/// Every channel that keeps answers read from the card; a new loader adds its flush here.
///
/// Each clears its queued requests and waiting answers, and none may block: the ejection path runs
/// them with the card already gone.
const FLUSHES: [fn(); 5] = [
    crate::channel::audio_loader::flush_tables,
    crate::channel::file_prefix::flush_tables,
    crate::channel::file_directory::flush_tables,
    crate::channel::file_bytes::flush_tables,
    crate::channel::file_stream::flush_tables,
];

/// Discards everything the loaders read from the card that has just been removed.
///
/// The generation bump comes first and cannot fail; each channel's clear is best-effort behind a
/// `try_lock`. Correctness rests on the generation stamp compared at take (for example
/// [`crate::channel::audio_loader::take_from`]), not on a clear landing. The byte and stream
/// channels also stamp queued requests and drop one asked under an earlier card
/// ([`crate::channel::file_bytes::admit_at_take`]), so a missed clear never reaches the new card.
///
/// A skipped directory-channel clear does cost a resource: each entry holds one of eight efatfs
/// directory handles.
///
/// Do not assume a loader and this function cannot run at once: loaders run on their own executor
/// on device but on `std::thread`s under `host_app`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_card_flush() {
    GENERATION.fetch_add(1, Ordering::AcqRel);
    for flush in FLUSHES {
        flush();
    }
}

abi_pin!(deluge_card_flush);
