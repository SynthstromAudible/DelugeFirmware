//! Host tests for the byte channel's pure parts: answer keying, card-generation stamps and
//! displacement in [`BytesSlot`], `copy_out`, request admission and the work-buffer claim.

use deluge_bsp_rust::app_loop;
use deluge_bsp_rust::channel::file_bytes;

use file_bytes::{
    ByteOp, ByteOutcome, ByteRequest, ByteTake, BytesPath, BytesSlot, ClaimOnce, CopyOut,
    FileTimestamp, copy_out, drain, mark_in_flight, publish_into, take_from,
};

/// Nonzero: a publish that dropped its stamp would leave 0.
const CARD: u32 = 3;

/// `DELUGE_ERR_NOT_FOUND`, as the bare `i8` the channel carries statuses in.
const NOT_FOUND: i8 = -8;

const SETTINGS: &str = "SETTINGS/CommunityFeatures.XML";

/// A read's answer. The outcome carries only the length (the bytes live in the channel's result
/// buffer), so these tests pin keying, stamps and displacement, not the bytes.
fn bytes(text: &str) -> ByteOutcome {
    ByteOutcome::Bytes(text.len() as u32)
}

#[test]
fn a_published_answer_is_taken_exactly_once() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::Read,
        SETTINGS,
        bytes("<settings/>"),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Read, SETTINGS, CARD),
        ByteTake::Ready(bytes("<settings/>"))
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Read, SETTINGS, CARD),
        ByteTake::Absent,
        "a second take must find nothing -- READY is terminal"
    );
}

/// The key carries the operation kind: `SETTINGS/MIDIDevices.XML` is both read and written, and a
/// path-only match would hand a read's bytes to a write's caller.
#[test]
fn a_read_answer_is_not_a_write_answer_for_the_same_path() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::Read,
        SETTINGS,
        bytes("<settings/>"),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Write, SETTINGS, CARD),
        ByteTake::Absent,
        "the write collect must not be handed the read's answer"
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Read, SETTINGS, CARD),
        ByteTake::Ready(bytes("<settings/>")),
        "and the read's own answer must still be there to collect"
    );
}

/// A different path is absent, and the entry it did not match is left for its own caller.
#[test]
fn an_answer_for_another_path_is_absent_and_undisturbed() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::Read,
        SETTINGS,
        bytes("<settings/>"),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Read, "SETTINGS/MIDIFollow.XML", CARD),
        ByteTake::Absent
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Read, SETTINGS, CARD),
        ByteTake::Ready(bytes("<settings/>"))
    );
}

/// The safety property: the flush's clear may not land, so the take must refuse a stale entry and
/// remove it, or a take at some later generation could find it again.
#[test]
fn an_answer_stamped_for_a_departed_card_is_refused_and_removed() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::Read,
        SETTINGS,
        bytes("<settings/>"),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Read, SETTINGS, CARD + 1),
        ByteTake::Absent
    );
    assert!(
        slot.is_empty(),
        "a refused answer must be removed, not left for a later generation to find"
    );
}

/// A publish displaces an uncollected answer rather than being refused, so a caller that gives up
/// mid-poll cannot wedge the slot. (Admission protects non-idempotent answers; see
/// `answer_blocks`.)
#[test]
fn a_publish_displaces_an_uncollected_answer() {
    let mut slot = BytesSlot::new();
    publish_into(&mut slot, ByteOp::Read, SETTINGS, bytes("<old/>"), CARD);
    publish_into(&mut slot, ByteOp::Write, SETTINGS, ByteOutcome::Done, CARD);
    assert_eq!(
        take_from(&mut slot, ByteOp::Read, SETTINGS, CARD),
        ByteTake::Absent,
        "the abandoned read answer is gone"
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Write, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Done)
    );
}

/// A failure is a published, terminal answer; publishing nothing would read as UNKNOWN and the
/// caller would retry forever.
#[test]
fn a_failure_is_published_and_carries_its_status() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::Read,
        SETTINGS,
        ByteOutcome::Failed(NOT_FOUND),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Read, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Failed(NOT_FOUND)),
        "NOT_FOUND must survive as itself: the caller writes defaults for it and writes nothing at \
         all for an unreadable card"
    );
}

#[test]
fn drain_empties_the_slot() {
    let mut slot = BytesSlot::new();
    publish_into(&mut slot, ByteOp::Write, SETTINGS, ByteOutcome::Done, CARD);
    drain(&mut slot);
    assert!(slot.is_empty());
    assert_eq!(
        take_from(&mut slot, ByteOp::Write, SETTINGS, CARD),
        ByteTake::Absent
    );
}

/// A rename is keyed on its destination, the name the caller goes on to use.
#[test]
fn a_rename_is_keyed_on_its_destination() {
    let request = ByteRequest::Rename {
        from: BytesPath::try_from("CommunityFeatures.XML").expect("fits"),
        to: BytesPath::try_from(SETTINGS).expect("fits"),
    };
    assert_eq!(request.key(), (ByteOp::Rename, SETTINGS));
    assert_eq!(
        request.key_owned(),
        (ByteOp::Rename, BytesPath::try_from(SETTINGS).expect("fits"))
    );
}

/// "Known absent" is an answer, not a failure: the startup fail-safe writes to the card on one and
/// not the other.
#[test]
fn a_known_absence_is_an_answer_not_a_failure() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::Exists,
        SETTINGS,
        ByteOutcome::Exists(false),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Exists, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Exists(false))
    );
}

/// A check that could not be answered must not arrive as `Exists(false)`.
#[test]
fn an_unanswerable_check_is_not_a_known_absence() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::Exists,
        SETTINGS,
        ByteOutcome::Failed(NOT_FOUND),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Exists, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Failed(NOT_FOUND)),
        "a failure must survive as a failure, not collapse into 'absent'"
    );
}

#[test]
fn an_unlink_answer_is_not_an_exists_answer_for_the_same_path() {
    let mut slot = BytesSlot::new();
    publish_into(&mut slot, ByteOp::Unlink, SETTINGS, ByteOutcome::Done, CARD);
    assert_eq!(
        take_from(&mut slot, ByteOp::Exists, SETTINGS, CARD),
        ByteTake::Absent,
        "the existence collect must not be handed the deletion's answer"
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Unlink, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Done)
    );
}

#[test]
fn an_exists_request_is_keyed_on_its_path() {
    let request = ByteRequest::Exists {
        path: BytesPath::try_from(SETTINGS).expect("fits"),
    };
    assert_eq!(request.key(), (ByteOp::Exists, SETTINGS));
    assert_eq!(
        request.key_owned(),
        (ByteOp::Exists, BytesPath::try_from(SETTINGS).expect("fits"))
    );
}

#[test]
fn a_dir_exists_request_is_keyed_on_its_path() {
    let request = ByteRequest::DirExists {
        path: BytesPath::try_from(SETTINGS).expect("fits"),
    };
    assert_eq!(request.key(), (ByteOp::DirExists, SETTINGS));
    assert_eq!(
        request.key_owned(),
        (
            ByteOp::DirExists,
            BytesPath::try_from(SETTINGS).expect("fits")
        )
    );
}

/// A folder check and a file check on the same path are different questions: a file check fails
/// for every folder, so neither may be handed the other's answer.
#[test]
fn a_dir_exists_answer_is_not_an_exists_answer_for_the_same_path() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::DirExists,
        SETTINGS,
        ByteOutcome::Exists(true),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::Exists, SETTINGS, CARD),
        ByteTake::Absent,
        "the file existence collect must not be handed the folder check's answer"
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::DirExists, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Exists(true))
    );
}

#[test]
fn an_unlink_request_is_keyed_on_its_path() {
    let request = ByteRequest::Unlink {
        path: BytesPath::try_from(SETTINGS).expect("fits"),
    };
    assert_eq!(request.key(), (ByteOp::Unlink, SETTINGS));
    assert_eq!(
        request.key_owned(),
        (ByteOp::Unlink, BytesPath::try_from(SETTINGS).expect("fits"))
    );
}

/// A path creation's answer is not a timestamp's.
#[test]
fn a_mkdir_answer_is_not_a_set_time_answer_for_the_same_path() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::MkdirAll,
        SETTINGS,
        ByteOutcome::Done,
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::SetTime, SETTINGS, CARD),
        ByteTake::Absent,
        "the timestamp collect must not be handed the creation's answer"
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::MkdirAll, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Done)
    );
}

#[test]
fn a_set_time_request_is_keyed_on_its_path() {
    let request = ByteRequest::SetTime {
        path: BytesPath::try_from(SETTINGS).expect("fits"),
        timestamp: FileTimestamp {
            year: 2026,
            month: 9,
            day: 8,
            hour: 12,
            minute: 0,
            second: 0,
        },
    };
    assert_eq!(request.key(), (ByteOp::SetTime, SETTINGS));
    assert_eq!(
        request.key_owned(),
        (
            ByteOp::SetTime,
            BytesPath::try_from(SETTINGS).expect("fits")
        )
    );
}

#[test]
fn a_mkdir_all_request_is_keyed_on_its_path() {
    let request = ByteRequest::MkdirAll {
        path: BytesPath::try_from("PATTERNS/RHYTHMIC/KIT").expect("fits"),
    };
    assert_eq!(request.key(), (ByteOp::MkdirAll, "PATTERNS/RHYTHMIC/KIT"));
}

/// A read's block is not a write's count, even for the same path.
#[test]
fn a_read_at_answer_is_not_a_write_at_answer() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::WriteAt,
        SETTINGS,
        ByteOutcome::Wrote(512),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::ReadAt, SETTINGS, CARD),
        ByteTake::Absent,
        "the read collect must not be handed the write's count"
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::WriteAt, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Wrote(512))
    );
}

/// A zero-length block is an answer (end of file), not a failure: a transfer detects its end this
/// way.
#[test]
fn an_empty_block_is_an_answer_not_a_failure() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::ReadAt,
        SETTINGS,
        ByteOutcome::Block(0),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::ReadAt, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Block(0))
    );
}

/// A copy is keyed on its destination, like a rename.
#[test]
fn a_copy_is_keyed_on_its_destination() {
    let request = ByteRequest::Copy {
        from: BytesPath::try_from("A.WAV").expect("fits"),
        to: BytesPath::try_from(SETTINGS).expect("fits"),
    };
    assert_eq!(request.key(), (ByteOp::Copy, SETTINGS));
    assert_eq!(
        request.key_owned(),
        (ByteOp::Copy, BytesPath::try_from(SETTINGS).expect("fits"))
    );
}

/// `copy_out` copies exactly the answer and leaves the rest of the caller's buffer alone.
#[test]
fn a_copy_out_copies_exactly_the_answer() {
    let src = *b"<settings/>....";
    let mut dst = [0xAAu8; 16];
    assert_eq!(copy_out(&src, 11, &mut dst), CopyOut::Copied(11));
    assert_eq!(&dst[..11], b"<settings/>");
    assert!(
        dst[11..].iter().all(|&b| b == 0xAA),
        "bytes past the answer must be left alone"
    );
}

/// A caller buffer too small for the answer gets nothing: a truncated settings file would parse
/// with its remaining settings reset.
#[test]
fn a_copy_out_into_too_small_a_buffer_writes_nothing() {
    let src = *b"<settings/>";
    let mut dst = [0xAAu8; 4];
    assert_eq!(copy_out(&src, 11, &mut dst), CopyOut::TooBig);
    assert_eq!(dst, [0xAA; 4]);
}

/// An impossible answer (longer than its buffer) writes nothing and is `Invalid` even when the
/// destination is also too small, so it never becomes a terminal failure for a real file.
#[test]
fn a_copy_out_longer_than_its_source_is_invalid_and_writes_nothing() {
    let src = *b"<s/>";
    let mut dst = [0xAAu8; 16];
    assert_eq!(copy_out(&src, 5, &mut dst), CopyOut::Invalid);
    assert_eq!(dst, [0xAA; 16]);
    let mut small = [0xAAu8; 2];
    assert_eq!(copy_out(&src, 5, &mut small), CopyOut::Invalid);
    assert_eq!(small, [0xAA; 2]);
}

/// The work-buffer grant succeeds once, ever.
#[test]
fn a_claim_once_grants_exactly_one_claim() {
    let grant = ClaimOnce::new();
    assert!(grant.claim(), "the first claim is granted");
    assert!(!grant.claim(), "a second claim would alias the buffer");
    assert!(!grant.claim());
}

/// Only the two buffered writes own part of the request buffer, which the task's take-time copy
/// keys on.
#[test]
fn a_write_request_carries_its_length_not_its_bytes() {
    let write = ByteRequest::Write {
        path: BytesPath::try_from(SETTINGS).expect("fits"),
        len: 11,
    };
    assert_eq!(write.key(), (ByteOp::Write, SETTINGS));
    assert_eq!(write.payload_len(), Some(11));

    let write_at = ByteRequest::WriteAt {
        path: BytesPath::try_from(SETTINGS).expect("fits"),
        offset: 4096,
        len: 512,
    };
    assert_eq!(write_at.key(), (ByteOp::WriteAt, SETTINGS));
    assert_eq!(write_at.payload_len(), Some(512));

    let read = ByteRequest::Read {
        path: BytesPath::try_from(SETTINGS).expect("fits"),
    };
    assert_eq!(
        read.payload_len(),
        None,
        "a read owns none of the request buffer"
    );
}

#[test]
fn a_caller_buffer_request_is_refused_while_another_is_queued() {
    assert!(file_bytes::accepts(None, ByteOp::WriteFrom));
    assert!(!file_bytes::accepts(Some(ByteOp::Read), ByteOp::WriteFrom));
    assert!(!file_bytes::accepts(
        Some(ByteOp::WriteFrom),
        ByteOp::ReadInto
    ));
}

#[test]
fn a_second_write_from_is_refused_not_replaced() {
    assert!(!file_bytes::accepts(
        Some(ByteOp::WriteFrom),
        ByteOp::WriteFrom
    ));
}

/// Every request is refused while another is queued, a read over a read included; the queued one
/// is kept.
#[test]
fn the_existing_read_rule_is_unchanged() {
    assert_eq!(
        file_bytes::accepts(Some(ByteOp::Read), ByteOp::Read),
        file_bytes::READ_REPLACES_READ
    );
    assert!(!file_bytes::READ_REPLACES_READ);
}

fn path(text: &str) -> BytesPath {
    BytesPath::try_from(text).expect("fits")
}

/// A rename, an unlink and a caller-buffer write do something a second run would not; every
/// other kind reaches the same state run twice.
#[test]
fn only_renames_unlinks_and_caller_buffer_writes_are_not_idempotent() {
    for op in [ByteOp::Rename, ByteOp::Unlink, ByteOp::WriteFrom] {
        assert!(file_bytes::non_idempotent(op), "{op:?}");
    }
    for op in [
        ByteOp::Read,
        ByteOp::Write,
        ByteOp::Exists,
        ByteOp::DirExists,
        ByteOp::MkdirAll,
        ByteOp::SetTime,
        ByteOp::ReadAt,
        ByteOp::WriteAt,
        ByteOp::Stat,
        ByteOp::Copy,
        ByteOp::ReadInto,
    ] {
        assert!(!file_bytes::non_idempotent(op), "{op:?}");
    }
}

#[test]
fn an_uncollected_rename_answer_refuses_a_new_request() {
    let mut slot = BytesSlot::new();
    publish_into(&mut slot, ByteOp::Rename, SETTINGS, ByteOutcome::Done, CARD);
    assert!(!slot.admits_new_request(CARD));
}

#[test]
fn a_collected_answer_no_longer_refuses() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::WriteFrom,
        SETTINGS,
        ByteOutcome::Wrote(5),
        CARD,
    );
    assert!(!slot.admits_new_request(CARD));
    assert_eq!(
        take_from(&mut slot, ByteOp::WriteFrom, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Wrote(5))
    );
    assert!(slot.admits_new_request(CARD));
}

#[test]
fn an_abandoned_answer_ages_out_after_its_bound() {
    let mut slot = BytesSlot::new();
    publish_into(&mut slot, ByteOp::Unlink, SETTINGS, ByteOutcome::Done, CARD);
    for _ in 0..file_bytes::PROTECTED_ANSWER_REFUSALS {
        assert!(!slot.admits_new_request(CARD));
    }
    assert!(slot.admits_new_request(CARD));
}

#[test]
fn an_idempotent_answer_is_still_displaceable() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::Stat,
        SETTINGS,
        ByteOutcome::Size(9),
        CARD,
    );
    assert!(slot.admits_new_request(CARD));
}

#[test]
fn an_answer_from_a_departed_card_does_not_refuse() {
    let mut slot = BytesSlot::new();
    publish_into(&mut slot, ByteOp::Rename, SETTINGS, ByteOutcome::Done, CARD);
    assert!(slot.admits_new_request(CARD + 1));
}

#[test]
fn a_republish_restarts_the_age_bound() {
    let mut slot = BytesSlot::new();
    publish_into(&mut slot, ByteOp::Unlink, SETTINGS, ByteOutcome::Done, CARD);
    for _ in 0..file_bytes::PROTECTED_ANSWER_REFUSALS {
        assert!(!slot.admits_new_request(CARD));
    }
    publish_into(&mut slot, ByteOp::Rename, SETTINGS, ByteOutcome::Done, CARD);
    assert!(!slot.admits_new_request(CARD));
}

/// A caller-buffer request from a gone card is dropped unrun, so its buffer is never named as
/// running; one from the current card runs and names its buffer.
#[test]
fn a_stale_caller_buffer_request_is_dropped_at_take_and_never_marks_its_buffer() {
    let read_into = ByteRequest::ReadInto {
        path: path(SETTINGS),
        dst: 0x1000,
        cap: 64,
    };
    assert_eq!(file_bytes::admit_at_take(&read_into, CARD, CARD + 1), None);
    assert_eq!(
        file_bytes::admit_at_take(&read_into, CARD, CARD),
        Some(0x1000)
    );

    let write_from = ByteRequest::WriteFrom {
        path: path(SETTINGS),
        src: 0x2000,
        len: 64,
        may_overwrite: false,
    };
    assert_eq!(file_bytes::admit_at_take(&write_from, CARD, CARD + 1), None);
    assert_eq!(
        file_bytes::admit_at_take(&write_from, CARD, CARD),
        Some(0x2000)
    );
}

/// Every kind from a gone card is dropped unrun, or a request that survived the flush would run
/// against the new card. From the current card, each runs, naming no buffer.
#[test]
fn a_stale_request_of_every_kind_is_dropped_at_take() {
    let p = path(SETTINGS);
    let requests = [
        ByteRequest::Read { path: p.clone() },
        ByteRequest::Write {
            path: p.clone(),
            len: 4,
        },
        ByteRequest::Rename {
            from: path("SETTINGS/old.XML"),
            to: p.clone(),
        },
        ByteRequest::Unlink { path: p.clone() },
        ByteRequest::Exists { path: p.clone() },
        ByteRequest::DirExists { path: p.clone() },
        ByteRequest::MkdirAll { path: p.clone() },
        ByteRequest::SetTime {
            path: p.clone(),
            timestamp: FileTimestamp {
                year: 2026,
                month: 9,
                day: 30,
                hour: 0,
                minute: 0,
                second: 0,
            },
        },
        ByteRequest::ReadAt {
            path: p.clone(),
            offset: 0,
            len: 4,
        },
        ByteRequest::WriteAt {
            path: p.clone(),
            offset: 0,
            len: 4,
        },
        ByteRequest::Stat { path: p.clone() },
        ByteRequest::Copy {
            from: path("SETTINGS/src.XML"),
            to: p.clone(),
        },
    ];
    for request in &requests {
        assert_eq!(
            file_bytes::admit_at_take(request, CARD, CARD + 1),
            None,
            "{request:?}"
        );
        assert_eq!(
            file_bytes::admit_at_take(request, CARD, CARD),
            Some(0),
            "{request:?}"
        );
    }
}

// The C ABI over the process-wide statics.

/// Clearing the in-flight mark wakes the app (`DELUGE_WAKE_STORAGE`); marking it does not. No test
/// runs the task, so `mark_in_flight` is driven directly, as the other channel tests do.
#[test]
fn finishing_an_operation_wakes_the_app() {
    app_loop::reset_reasons_for_test();
    embassy_futures::block_on(mark_in_flight(Some((
        ByteOp::Read,
        BytesPath::try_from(SETTINGS).unwrap(),
    ))));
    assert_eq!(
        app_loop::reasons_for_test() & app_loop::WAKE_STORAGE,
        0,
        "marking an operation in flight must not wake the app early"
    );
    embassy_futures::block_on(mark_in_flight(None));
    assert_ne!(
        app_loop::reasons_for_test() & app_loop::WAKE_STORAGE,
        0,
        "clearing the in-flight mark must wake the app"
    );
}
