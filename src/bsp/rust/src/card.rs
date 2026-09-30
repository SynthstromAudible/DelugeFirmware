//! What every loader on this BSP has to forget when the card is pulled (`libdeluge/card.h`), and
//! the card generation they stamp their answers with.
//!
//! The generation is a property of the card, not of any one loader, so every loader that caches
//! something read from the card shares this one counter. Read it with [`generation`] where a load
//! begins and compare it where the result is taken. Sampling it at publish time instead would read
//! the generation a flush has already installed and let a stale result through.

use core::sync::atomic::{AtomicU32, Ordering};

/// Which card the loaders are reading, bumped once per ejection. A wrapping counter: `!=` is the
/// only test, and telling apart two generations 2^32 ejections apart is not something anything needs.
static GENERATION: AtomicU32 = AtomicU32::new(0);

/// The generation a load starting now belongs to.
pub fn generation() -> u32 {
    GENERATION.load(Ordering::Acquire)
}

/// Every channel that keeps answers read from the card. Each clears its queued requests and waiting answers; none
/// may block, because the ejection path runs this with the card already gone.
const FLUSHES: [fn(); 5] = [
    super::audio_loader::flush_tables,
    super::file_prefix::flush_tables,
    super::file_directory::flush_tables,
    super::file_bytes::flush_tables,
    super::file_stream::flush_tables,
];

/// Discard everything every loader read from the card that has just been removed. See `card.h`.
///
/// The bump comes FIRST and cannot fail; each module's clear is best-effort housekeeping behind a
/// `try_lock`. Every loader that caches card-derived state is cleared here, in one call — that is
/// the shape the header promises ("a second loader gets covered by calling this, not by growing a
/// second flush"), so a new loader adds a line to [`FLUSHES`] rather than an entry point of its own.
///
/// No correctness rests on a clear landing — the stamp, compared at take, is the safety property,
/// and it holds no matter which locks were taken or which contexts the two sides ran in (see
/// [`super::audio_loader::take_from`], [`super::file_prefix::take_from`] and
/// [`super::file_directory::take_from`]). The byte and stream channels stamp their queued REQUESTS
/// too, and their tasks drop one asked under a card that has gone unrun (see
/// [`super::file_bytes::admit_at_take`]), so a request a clear missed never reaches the new card.
///
/// The directory channel's clear is the one that costs a RESOURCE when it is skipped rather than
/// merely leaving a dead entry: it holds one of eight efatfs directory handles. See that module's
/// `flush_tables`.
///
/// Nothing here may be re-argued from where a loader is scheduled: a loader owns its own executor
/// on device but a plain `std::thread` under `host_app`, so any claim of the form "these two cannot
/// run at once" is false on some configuration that builds this file.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_card_flush() {
    GENERATION.fetch_add(1, Ordering::AcqRel);
    for flush in FLUSHES {
        flush();
    }
}

abi_pin!(deluge_card_flush);
