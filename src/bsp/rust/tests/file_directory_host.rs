//! Host tests for the directory channel's slot logic: keying, the card-generation stamp checked at
//! take, and that every discarded handle is handed back to be closed.
#![cfg(not(target_os = "none"))]

use deluge_bsp_rust::channel::file_directory;

use file_directory::{DirOutcome, DirTake, DirectorySlot, drain, publish_into, take_from};

/// Nonzero, so a publish that dropped its stamp (leaving 0) cannot still satisfy every take.
const CARD: u32 = 3;

/// `DELUGE_ERR_NOT_FOUND`, as the bare `i8` the channel carries (`DelugeStatus` is `int8_t` and its
/// failures are negative).
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

/// A take for another path must leave the entry alone: reporting it as an orphan would close a
/// handle another collect is still going to ask for.
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

/// `onListingFailed` shows the status, so it must survive the slot.
#[test]
fn a_failure_carries_its_status() {
    let mut slot = DirectorySlot::new();
    publish_into(&mut slot, "GONE", DirOutcome::Failed(NOT_FOUND), CARD);
    assert_eq!(
        take_from(&mut slot, "GONE", CARD),
        DirTake::Ready(DirOutcome::Failed(NOT_FOUND))
    );
}

/// A stale handle is refused at the take, removed, and handed back: dropping it silently would leak
/// one of the eight directory handles per card swap.
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

/// A stale failure has nothing to close; reporting handle 0 would close someone else's live slot.
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

/// A publish stamped before a flush's bump can land after it (the flush's clear may never run); the
/// take refuses it.
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

/// A browser that navigates away leaves its answer uncollected; the next publish displaces it and
/// must hand its handle back.
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
