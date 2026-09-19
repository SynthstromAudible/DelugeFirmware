//! The single-slot directory channel. One listing at a time is already enforced by the browser
//! (`Browser::listingInProgress_`), so this is a slot, not a table: no depth, no eviction, no
//! ordering.
//!
//! `deluge-bsp-rust` is bin-only (no `[lib]` target -- see `Cargo.toml`), so this recompiles
//! `src/file_directory.rs` unmodified into this test binary via `#[path]`, the same convention
//! `tests/file_prefix_host.rs` and `tests/audio_loader_host.rs` use. Unlike the prefix channel this
//! module borrows nothing from `audio_loader`: its one I/O operation is a directory open, which is
//! not in `LoadOps`, so it declares its own two-operation trait and comes along alone.
#![cfg(not(target_os = "none"))]

#[path = "../src/file_directory.rs"]
mod file_directory;

use file_directory::{DirOutcome, DirTake, DirectorySlot, drain, publish_into, take_from};

/// Nonzero on purpose: 0 is what a dropped stamp would leave behind, so a slot built at 0 would let
/// a publish that ignored its generation still satisfy every take.
const CARD: u32 = 3;

/// `DELUGE_ERR_NOT_FOUND`. This module carries a status as a bare `i8` -- `DelugeStatus`'s real
/// underlying type (`typedef enum DelugeStatus : int8_t`, `types.h`) -- rather than the bindgen
/// enum, so it compiles in every carrier that `#[path]`-includes it, including the ones with no
/// bindings at all. The values are NEGATIVE, so an unsigned carrier would have silently reported
/// every failure as some large positive status.
const NOT_FOUND: i8 = -8;

#[test]
fn a_published_handle_is_taken_exactly_once() {
    let mut slot = DirectorySlot::new();
    assert_eq!(
        publish_into(&mut slot, "SONGS", DirOutcome::Opened(7), CARD),
        None
    );
    assert_eq!(
        take_from(&mut slot, "SONGS", CARD),
        DirTake::Ready(DirOutcome::Opened(7))
    );
    assert_eq!(
        take_from(&mut slot, "SONGS", CARD),
        DirTake::Absent { orphan: None },
        "a second take must find nothing, and must not invent a handle to close"
    );
}

/// A different path is absent, and -- the half that matters -- the entry it did not match is LEFT
/// ALONE. Reporting it as an orphan would have the taker close a handle another path's collect is
/// still going to ask for.
#[test]
fn a_handle_for_another_path_is_absent_and_undisturbed() {
    let mut slot = DirectorySlot::new();
    publish_into(&mut slot, "SONGS", DirOutcome::Opened(7), CARD);
    assert_eq!(
        take_from(&mut slot, "KITS", CARD),
        DirTake::Absent { orphan: None }
    );
    assert_eq!(
        take_from(&mut slot, "SONGS", CARD),
        DirTake::Ready(DirOutcome::Opened(7)),
        "the mismatched take must not have consumed the entry"
    );
}

/// A failure is carried, not collapsed: `onListingFailed` shows the underlying status, so an
/// unreadable card and a missing folder do not become the same message.
#[test]
fn a_failure_carries_its_status() {
    let mut slot = DirectorySlot::new();
    publish_into(&mut slot, "GONE", DirOutcome::Failed(NOT_FOUND), CARD);
    assert_eq!(
        take_from(&mut slot, "GONE", CARD),
        DirTake::Ready(DirOutcome::Failed(NOT_FOUND))
    );
}

/// The belt: a handle opened on the previous card is refused at the TAKE, removed from the slot,
/// and -- because the snapshot table holds only `MAX_DIR_HANDLES` (8) -- handed back so the taker
/// can close it. Dropping it silently would burn one of eight slots per card swap.
#[test]
fn a_handle_from_a_previous_card_is_refused_and_handed_back_to_close() {
    let mut slot = DirectorySlot::new();
    publish_into(&mut slot, "SONGS", DirOutcome::Opened(7), CARD);
    assert_eq!(
        take_from(&mut slot, "SONGS", CARD + 1),
        DirTake::Absent { orphan: Some(7) },
        "a stale handle is nobody's to enumerate and must be closed, not merely withheld"
    );
    assert!(
        slot.is_empty(),
        "the stale entry must be removed, not merely withheld"
    );
}

/// A stale FAILURE is refused the same way, but there is nothing to close. Reporting an orphan here
/// would have the taker close handle 0 -- a real, live slot belonging to somebody else.
#[test]
fn a_failure_from_a_previous_card_is_refused_with_nothing_to_close() {
    let mut slot = DirectorySlot::new();
    publish_into(&mut slot, "GONE", DirOutcome::Failed(NOT_FOUND), CARD);
    assert_eq!(
        take_from(&mut slot, "GONE", CARD + 1),
        DirTake::Absent { orphan: None }
    );
    assert!(slot.is_empty());
}

/// The race a flush cannot win: there is no happens-before edge from the generation bump to a
/// concurrent publish's read of it, so a publish stamped BEFORE the bump can land after it. The
/// take is what refuses it -- the flush's clear is `try_lock`ed and may never run at all.
#[test]
fn a_handle_stamped_before_a_flush_cannot_be_taken_after_it() {
    let mut slot = DirectorySlot::new();
    publish_into(&mut slot, "SONGS", DirOutcome::Opened(7), CARD);
    assert_eq!(
        take_from(&mut slot, "SONGS", CARD + 1),
        DirTake::Absent { orphan: Some(7) }
    );
    assert!(slot.is_empty());
}

/// Publishing over an uncollected answer displaces it, and a displaced OPEN handle is exactly as
/// orphaned as a stale one. Single-flight makes this rare, not impossible: a browser that navigates
/// away leaves its answer uncollected, and the next listing's publish lands on top of it.
#[test]
fn a_publish_over_an_uncollected_handle_hands_the_old_one_back() {
    let mut slot = DirectorySlot::new();
    publish_into(&mut slot, "SONGS", DirOutcome::Opened(7), CARD);
    assert_eq!(
        publish_into(&mut slot, "KITS", DirOutcome::Opened(2), CARD),
        Some(7),
        "the displaced handle must be closed by the publisher"
    );
    assert_eq!(
        take_from(&mut slot, "KITS", CARD),
        DirTake::Ready(DirOutcome::Opened(2))
    );
}

/// `drain` is what `flush_tables` uses: empty the slot and hand back whatever handle it was holding,
/// so an ejection does not strand one of the eight.
#[test]
fn draining_the_slot_hands_back_an_open_handle() {
    let mut slot = DirectorySlot::new();
    publish_into(&mut slot, "SONGS", DirOutcome::Opened(7), CARD);
    assert_eq!(drain(&mut slot), Some(7));
    assert!(slot.is_empty());
    assert_eq!(
        drain(&mut slot),
        None,
        "draining an empty slot invents nothing"
    );
}
