//! The stream channel's answer slot: one answer at a time, keyed by kind, handle and path, stamped with the card
//! generation it was asked under.
//!
//! `deluge-bsp-rust` is bin-only, so this recompiles `src/file_stream_slot.rs` into the test binary via `#[path]`.
#![cfg(not(target_os = "none"))]

#[allow(dead_code)]
#[path = "../src/file_stream_slot.rs"]
mod file_stream_slot;

use file_stream_slot::{AnswerSlot, MAX_PATH_CHARS, OpKey, OpKind, Take};

/// Nonzero on purpose: a publish that dropped its stamp would leave 0, and a slot tested at 0 would not notice.
const CARD: u32 = 3;

fn key(kind: OpKind, handle: u32, path: &str) -> OpKey {
    OpKey::new(kind, handle, path).expect("test paths fit")
}

#[test]
fn a_published_answer_is_taken_exactly_once() {
    let mut slot = AnswerSlot::new();
    let k = key(OpKind::WriteAt, 2, "");
    slot.publish(k.clone(), Ok(512), CARD);
    assert_eq!(slot.take(&k, CARD), Take::Ready(512));
    assert_eq!(slot.take(&k, CARD), Take::Absent);
}

#[test]
fn a_failure_is_taken_as_failed() {
    let mut slot = AnswerSlot::new();
    let k = key(OpKind::Open, 0, "SONGS/A.XML");
    slot.publish(k.clone(), Err(0), CARD);
    assert_eq!(slot.take(&k, CARD), Take::Failed(0));
}

#[test]
fn a_wrong_path_leaves_the_answer() {
    let mut slot = AnswerSlot::new();
    let mine = key(OpKind::Open, 0, "SONGS/A.XML");
    slot.publish(mine.clone(), Ok(100), CARD);
    assert_eq!(
        slot.take(&key(OpKind::Open, 0, "SONGS/B.XML"), CARD),
        Take::Absent
    );
    assert_eq!(slot.take(&mine, CARD), Take::Ready(100));
}

#[test]
fn a_wrong_kind_or_handle_leaves_the_answer() {
    let mut slot = AnswerSlot::new();
    let mine = key(OpKind::WriteAt, 2, "");
    slot.publish(mine.clone(), Ok(1), CARD);
    assert_eq!(slot.take(&key(OpKind::ReadAt, 2, ""), CARD), Take::Absent);
    assert_eq!(slot.take(&key(OpKind::WriteAt, 3, ""), CARD), Take::Absent);
    assert_eq!(slot.take(&mine, CARD), Take::Ready(1));
}

#[test]
fn an_answer_from_an_earlier_card_is_removed_not_returned() {
    let mut slot = AnswerSlot::new();
    let k = key(OpKind::WriteAt, 1, "");
    slot.publish(k.clone(), Ok(900), CARD);
    assert_eq!(slot.take(&k, CARD + 1), Take::Absent);
    assert!(slot.is_empty());
    assert_eq!(slot.take(&k, CARD), Take::Absent);
}

#[test]
fn a_discarded_open_answer_is_orphaned_for_closing() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "");
    slot.publish(open.clone(), Ok(5), CARD);
    slot.discard(&open);
    assert!(slot.is_empty());
    assert_eq!(slot.take_orphan(CARD), Some(5));
    assert_eq!(slot.take_orphan(CARD), None);
}

#[test]
fn a_failed_open_leaves_no_orphan() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "");
    slot.publish(open.clone(), Err(0), CARD);
    slot.discard(&open);
    assert!(!slot.has_orphans());
}

#[test]
fn a_displaced_open_answer_is_orphaned() {
    let mut slot = AnswerSlot::new();
    slot.publish(key(OpKind::Open, 0, ""), Ok(6), CARD);
    slot.publish(key(OpKind::Stats, 0, ""), Ok(1000), CARD);
    assert_eq!(slot.take_orphan(CARD), Some(6));
}

#[test]
fn a_flushed_open_answer_is_orphaned() {
    let mut slot = AnswerSlot::new();
    slot.publish(key(OpKind::Open, 0, ""), Ok(1), CARD);
    slot.drain();
    assert!(slot.is_empty());
    assert_eq!(slot.take_orphan(CARD), Some(1));
}

#[test]
fn a_stale_orphan_is_not_returned_for_closing() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "");
    slot.publish(open.clone(), Ok(7), CARD);
    // The take is on the NEW card: the answer is stale, so it retires as an orphan stamped with the OLD
    // generation (CARD), not the one just asked for.
    assert_eq!(slot.take(&open, CARD + 1), Take::Absent);
    // A remount already reset the old card's stream table and may have reused handle 7 for a live stream on the
    // new card, so closing it now would close someone else's file. Asking for orphans AT THE CURRENT generation
    // must not hand it back.
    assert_eq!(slot.take_orphan(CARD + 1), None);
    assert!(!slot.has_orphans());
}

#[test]
fn a_current_orphan_is_returned() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "");
    slot.publish(open.clone(), Ok(5), CARD);
    slot.discard(&open);
    assert_eq!(slot.take_orphan(CARD), Some(5));
}

#[test]
fn a_collected_open_answer_is_not_orphaned() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "");
    slot.publish(open.clone(), Ok(4), CARD);
    assert_eq!(slot.take(&open, CARD), Take::Ready(4));
    assert!(!slot.has_orphans());
}

#[test]
fn discarding_another_key_leaves_the_answer() {
    let mut slot = AnswerSlot::new();
    let mine = key(OpKind::Open, 0, "A");
    slot.publish(mine.clone(), Ok(3), CARD);
    slot.discard(&key(OpKind::Open, 0, "B"));
    assert_eq!(slot.take(&mine, CARD), Take::Ready(3));
}

#[test]
fn a_path_longer_than_the_bound_has_no_key() {
    let long = "x".repeat(MAX_PATH_CHARS + 1);
    assert!(OpKey::new(OpKind::Open, 0, &long).is_none());
    assert!(OpKey::new(OpKind::Open, 0, &long[..MAX_PATH_CHARS]).is_some());
}

#[test]
fn an_open_collected_with_another_path_leaves_the_answer() {
    let mut slot = AnswerSlot::new();
    let mine = key(OpKind::Open, 0, "SAMPLES/RECORD/REC001.WAV");
    slot.publish(mine.clone(), Ok(3), CARD);
    assert_eq!(
        slot.take(&key(OpKind::Open, 0, "SAMPLES/RECORD/REC002.WAV"), CARD),
        Take::Absent
    );
    assert_eq!(slot.take(&mine, CARD), Take::Ready(3));
}

#[test]
fn a_failed_open_is_taken_with_its_status() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "SAMPLES/RECORD/REC001.WAV");
    // DELUGE_ERR_NOT_FOUND (-8), carried as its byte.
    let not_found = u32::from((-8i8) as u8);
    slot.publish(open.clone(), Err(not_found), CARD);
    assert_eq!(slot.take(&open, CARD), Take::Failed(not_found));
}
