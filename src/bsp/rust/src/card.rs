//! `include/libdeluge/card.h` — what every loader on this BSP has to forget when the card is
//! pulled.
//!
//! One entry point, [`deluge_card_flush`]. It lives here rather than in `audio_loader.rs` because
//! it is a property of THE CARD, not of any one loader: an ejection invalidates everything read
//! from that card at once, and the counter it bumps ([`deluge_card_generation`]) is shared by every
//! loader that caches card-derived state.
//!
//! # Reach the loaders through `super::`, never `crate::`
//!
//! Like `audio_loader.rs`, `file_prefix.rs` and `file_directory.rs` beside it, this file is
//! `#[path]`-recompiled into
//! other packages (the BSP's own `tests/audio_loader_host.rs`, `crates/deluge_audio_loader_host`
//! under that, and the `golden_vt_render` harness), and in one of them it is nested a module deep
//! rather than sitting at the crate root. `super::audio_loader` names the sibling module in every
//! one of those nestings; `crate::audio_loader` names it in only some. Every carrier that compiles
//! this file must therefore provide BOTH sibling modules. The shared counter needs no such care —
//! [`deluge_card_generation`] is a real dependency crate, so every build resolves the same static.

/// Discard everything every loader read from the card that has just been removed. See `card.h`.
///
/// The bump comes FIRST and cannot fail; each module's clear is best-effort housekeeping behind a
/// `try_lock`. Every loader that caches card-derived state is cleared here, in one call — that is
/// the shape the header promises ("a second loader gets covered by calling this, not by growing a
/// second flush"), so a new loader adds a line here rather than an entry point of its own.
///
/// No correctness rests on a clear landing — the stamp, compared at take, is the safety property,
/// and it holds no matter which locks were taken or which contexts the two sides ran in (see
/// [`super::audio_loader::take_from`], [`super::file_prefix::take_from`] and
/// [`super::file_directory::take_from`]).
///
/// The directory channel's clear is the one that costs a RESOURCE when it is skipped rather than
/// merely leaving a dead entry: it holds one of eight efatfs directory handles. See that module's
/// `flush_tables`.
///
/// Nothing here may be re-argued from where a loader is scheduled: a loader owns its own executor
/// on device but a plain `std::thread` under `host_app`, so any claim of the form "these two cannot
/// run at once" is false on some configuration that builds this file.
///
/// Always exported, on every configuration this module compiles in, so the C++ call site links
/// regardless of which build produced this BSP image — the same rule
/// `deluge_audio_load_async_active` follows. That is also why `flush_tables` has a no-op sibling on
/// builds without the loader: the bump still has to happen.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_card_flush() {
    deluge_card_generation::bump();
    super::audio_loader::flush_tables();
    super::file_prefix::flush_tables();
    super::file_directory::flush_tables();
    super::file_bytes::flush_tables();
}
