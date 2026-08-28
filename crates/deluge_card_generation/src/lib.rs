//! The one card generation, shared by every loader that caches something read from the card.
//!
//! Lifted out of `audio_loader.rs` because it is a property of THE CARD, not of the audio loader: a
//! second loader caching card-derived state needs the same stamp, and a second hand-rolled counter
//! is a second chance to reintroduce the bug the take-time belt closed.
//!
//! # How to use it
//!
//! Read it where a load BEGINS and compare it where a result is TAKEN. Sampling it at publish time
//! instead reads the generation a flush has already installed and lets the stale result straight
//! through — the same "sampled too late" defect this belt exists to close.
//!
//! A clear of a loader's tables is housekeeping; the stamp compared at take is the safety property.
//! Nothing about that may be re-argued from where a loader happens to be scheduled: a loader owns
//! its own executor on device but a plain `std::thread` under `host_app`, so any claim of the form
//! "these two cannot run at once" is false on some configuration that builds it.
#![no_std]

use core::sync::atomic::{AtomicU32, Ordering};

/// Which card the loaders are reading. Bumped once per ejection by [`bump`]; anything a loader
/// began under an older value has nothing to say about the card that is in the slot now.
///
/// A plain wrapping counter, and the wrap is harmless: `!=` is the only test, and distinguishing
/// two generations 2^32 ejections apart is not a property anything needs.
static GENERATION: AtomicU32 = AtomicU32::new(0);

/// The generation a load starting *now* belongs to. See [`GENERATION`].
pub fn current() -> u32 {
    GENERATION.load(Ordering::Acquire)
}

/// Invalidate everything read from the card that is now gone.
///
/// Cannot fail, which is why callers order it AHEAD of their best-effort table clears: no window
/// may exist in which a table has been emptied but the generation still reads as the one a load in
/// flight is about to publish under.
pub fn bump() {
    GENERATION.fetch_add(1, Ordering::AcqRel);
}
