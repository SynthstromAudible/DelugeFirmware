//! The single-slot whole-file byte channel. One operation at a time across all three kinds, so this
//! is a slot, not a table: no depth, no eviction, no ordering.
//!
//! `deluge-bsp-rust` is bin-only (no `[lib]` target -- see `Cargo.toml`), so this recompiles
//! `src/file_bytes.rs` unmodified into this test binary via `#[path]`, the same convention
//! `tests/file_directory_host.rs` and `tests/file_prefix_host.rs` use. Like the directory channel it
//! borrows nothing from `audio_loader`: reading a whole file and writing one back are not `LoadOps`'s
//! four operations, so it declares its own three-operation trait and comes along alone.
#![cfg(feature = "async_file_bytes")]
#![cfg(not(target_os = "none"))]

#[path = "../src/file_bytes.rs"]
mod file_bytes;

use file_bytes::{
    BlockBuf, ByteBuf, ByteOp, ByteOutcome, ByteRequest, ByteTake, BytesPath, BytesSlot, ClaimOnce,
    CopyOut, FileTimestamp, copy_out, drain, publish_into, take_from,
};

/// Nonzero on purpose: 0 is what a dropped stamp would leave behind, so a slot built at 0 would let
/// a publish that ignored its generation still satisfy every take.
const CARD: u32 = 3;

/// `DELUGE_ERR_NOT_FOUND`. This module carries a status as a bare `i8` -- `DelugeStatus`'s real
/// underlying type (`typedef enum DelugeStatus : int8_t`, `types.h`) -- rather than the bindgen enum,
/// so it compiles in every carrier that `#[path]`-includes it. The values are NEGATIVE, so an
/// unsigned carrier would have silently reported every failure as some large positive status.
const NOT_FOUND: i8 = -8;

const SETTINGS: &str = "SETTINGS/CommunityFeatures.XML";

fn bytes(text: &str) -> ByteOutcome {
    ByteOutcome::Bytes(ByteBuf::from_slice(text.as_bytes()).expect("fits"))
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

/// **The reason the key carries the operation kind.** `SETTINGS/MIDIDevices.XML` is both read and
/// written, and a collect matching on the path alone would hand a read's bytes to a write's caller,
/// which would then report to the user that their settings had reached the card.
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

/// A different path is absent, and -- the half that matters -- the entry it did not match is LEFT
/// ALONE. Discarding it would drop an answer whose own caller is still going to ask for it.
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

/// **The safety property.** `flush_tables`'s clear is `try_lock`ed and may not land, so a stale entry
/// can still be resident. The take is what refuses it -- and it must REMOVE it as well as withhold
/// it, or a later take at some future generation finds it again, which is exactly the wrong-card bug
/// the stamp exists to prevent.
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

/// A publish displaces an uncollected answer rather than being refused, and that is deliberate: a
/// caller that gives up mid-poll would otherwise leave the slot occupied with nothing able to clear
/// it. See `enqueue_request`'s doc -- it is safe only because all three operations are idempotent.
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

/// A failure is a published, TERMINAL answer -- not the absence of one. Publishing nothing would
/// report UNKNOWN, whose documented meaning is "request it (again)", so a settings file that cannot
/// be read would be re-requested on every tick forever.
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

/// A rename is keyed on its DESTINATION, because that is the name the caller goes on to use -- and
/// the source is where the file no longer is.
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

/// The reason `Exists(false)` is not `Failed`. "Known absent" is an ANSWER; a collect that could not
/// establish existence is not, and the startup fail-safe writes to the card on one and not the other.
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

/// And the other half: a check that could not be answered must not arrive as `Exists(false)`.
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

/// Keying holds for the two new kinds too: a path creation's answer is not a timestamp's.
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

/// A zero-length block is an ANSWER -- end of file -- and must survive as one. A transfer detects
/// its own end this way, so collapsing it into a failure would break termination.
#[test]
fn an_empty_block_is_an_answer_not_a_failure() {
    let mut slot = BytesSlot::new();
    publish_into(
        &mut slot,
        ByteOp::ReadAt,
        SETTINGS,
        ByteOutcome::Block(BlockBuf::new()),
        CARD,
    );
    assert_eq!(
        take_from(&mut slot, ByteOp::ReadAt, SETTINGS, CARD),
        ByteTake::Ready(ByteOutcome::Block(BlockBuf::new()))
    );
}

/// A copy is keyed on its DESTINATION, like a rename -- that is the name the caller goes on to use.
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

/// Every collect that hands bytes to C++ goes through `copy_out`. It copies exactly the answer and
/// leaves the rest of the caller's buffer alone.
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

/// A caller buffer too small for the answer gets NOTHING: a settings file cut short parses as one
/// whose remaining settings were absent, which silently resets them.
#[test]
fn a_copy_out_into_too_small_a_buffer_writes_nothing() {
    let src = *b"<settings/>";
    let mut dst = [0xAAu8; 4];
    assert_eq!(copy_out(&src, 11, &mut dst), CopyOut::TooBig);
    assert_eq!(dst, [0xAA; 4]);
}

/// An answer claiming more bytes than its buffer holds cannot be produced. If one appears it writes
/// nothing, and it is reported as `Invalid` even when the destination is ALSO too small -- the
/// collect turns `Invalid` into UNKNOWN, never into a terminal failure for a real file.
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

/// What hands the work buffer to the task: one claim, ever.
#[test]
fn a_claim_once_grants_exactly_one_claim() {
    let grant = ClaimOnce::new();
    assert!(grant.claim(), "the first claim is granted");
    assert!(!grant.claim(), "a second claim would alias the buffer");
    assert!(!grant.claim());
}
