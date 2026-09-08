//! The whole-file byte channel: `include/libdeluge/file_bytes.h`'s Rust backing.
//!
//! # What it is for
//!
//! The settings family -- runtime feature settings, the MIDI device table, MIDI follow's defaults,
//! performance view -- reads and writes small whole files synchronously, from whichever tier
//! happened to touch a menu. Every one of those calls reaches efatfs through
//! `sim_block::block_on`, which spins its caller until the card answers, and by the execution model
//! the interaction tier may never wait on a lower one. This channel replaces that spin with a
//! request the caller drops off and an answer it collects on a later tick.
//!
//! # What it is NOT
//!
//! It carries BYTES, not a handle, and no XML crosses it. Nothing in the settings family streams:
//! each file is serialised in one pass into memory and parsed in one pass out of it. So every line
//! of parsing, policy and migration stays on the C++ side, and only the source and the sink move.
//! A caller therefore never holds an open file across a tick, which is what keeps
//! `sd.rs:470-490`'s single-entry guarantee intact for free -- the Rust side is the only thing that
//! ever has a file open, and it serialises.
//!
//! # One operation at a time, keyed by KIND as well as by path
//!
//! [`REQUEST`], [`IN_FLIGHT`] and [`RESULT`] each hold at most one entry, and every entry carries
//! the [`ByteOp`] it belongs to. Keying on the kind is not tidiness: `SETTINGS/MIDIDevices.XML` is
//! both read and written, and a collect that matched on the path alone would hand a read's bytes to
//! a write's caller, which would then believe its settings had reached the card.
//!
//! Single-flight rather than a queue because the filesystem underneath serialises everything anyway
//! (`with_fs` is one mutex), and because it is what makes the refusal below observable.
//!
//! # Reads coalesce; writes do NOT
//!
//! A read request for something already in flight is dropped, exactly as `file_directory`'s is: the
//! collect reports the one answer, and re-asking a question already asked costs nothing.
//!
//! A write, rename or unlink request that arrives while the channel is busy is **REFUSED** -- it returns
//! `false`, and nothing is queued or replaced. This is the one place this channel deliberately
//! differs from `file_directory`, and the difference is the point of it. `LatestWins` behaviour
//! drops the middle of three rapid requests; for a listing that is invisible, but two rapid toggles
//! of a setting are two distinct intents and superseding the first loses a settings change that the
//! user watched themselves make. A caller told `false` still holds its intent and retries.
//!
//! # A write creates its parent directory
//!
//! Which is why there is no `mkdir` operation here. Every settings caller's `mkdir SETTINGS_FOLDER`
//! existed only to make its own write succeed, and a card WRITE is no more allowed on the
//! interaction tier than a read is -- so moving the write without the `mkdir` would have moved half
//! the violation.
//!
//! # Card generations
//!
//! Every answer is stamped with the card generation the operation BEGAN under, and the stamp is
//! checked at the TAKE -- never only at publish, for the reason `file_directory` gives at length:
//! `deluge_card_flush`'s clear is `try_lock`ed and may not land at all. Unlike that channel there is
//! no scarce handle to strand here; a refused answer is simply dropped, and its caller reads
//! `UNKNOWN` and asks again.

use core::ffi::{CStr, c_char};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
#[cfg(feature = "async_file_bytes_active")]
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

/// Longest path this channel keys on, in CHARACTERS -- `DELUGE_FILE_BYTES_MAX_PATH` minus its NUL,
/// the same bound `file_directory::DirPath` carries and for the same reason (a C buffer size counts
/// the terminator; the text it can hold is one byte shorter).
const MAX_PATH_CHARS: usize = 255;

/// The largest file this channel will carry.
///
/// **Set by measurement, in both directions.** Real settings files on a real card measure 1.4-2.0
/// KiB (`CommunityFeatures.XML` 1453 B, `MIDIFollow.XML` 1957 B), so this is roughly 4x the largest
/// one that exists. And it cannot simply be `FileWriter`'s 32 KiB buffer, which is what bounds the
/// serialiser: one of these lives in [`REQUEST`] and one in [`RESULT`], and at 32 KiB each the
/// resulting 64 KiB of on-chip `.bss` pushes `.ARM.exidx` into `.rtt_cached_reserve` and the device
/// image stops linking. 16 KiB was measured to still link, so there is headroom to raise this if a
/// settings file ever grows -- but raising it is a device-link question, not a free change.
///
/// A file above this bound is never truncated: the read reports `NO_MEMORY` and the write is
/// refused. A settings file cut short parses as one whose remaining settings were absent, which
/// silently resets every setting past the cut -- strictly worse than a visible failure.
pub const MAX_BYTES: usize = 8192;

/// One path, inline. **Nothing in this module may touch the Rust global heap** -- that allocator
/// assumes single-threaded use and this driver runs on a preempting executor. See
/// `audio_loader::LoadPath`, which says it at length.
pub type BytesPath = heapless::String<MAX_PATH_CHARS>;

/// `DelugeTimestamp`, hand-mirrored.
///
/// Spelled here for the reason every status in this module is: it compiles in carriers with no
/// bindgen bindings at all. Pinned from the other side by `file_bytes_abi.cpp`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct FileTimestamp {
    pub year: u16,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
}

/// One file's contents, inline, for the same reason [`BytesPath`] is.
pub type ByteBuf = heapless::Vec<u8, MAX_BYTES>;

/// `DELUGE_ERR_NO_FILESYSTEM`, as a bare `i8`.
///
/// Spelled here for the reason `file_directory` spells its own: this module compiles in carriers
/// with no bindgen bindings at all. It is a NAMED constant and never written inline in a pattern --
/// an unknown identifier in pattern position is a fresh BINDING, not a comparison, which in
/// `file_directory` silently matched every error until it was caught.
const NO_FILESYSTEM: i8 = -11;
/// `DELUGE_ERR_NO_MEMORY`, as a bare `i8`. What a file too large for [`MAX_BYTES`] reports.
const NO_MEMORY: i8 = -13;
/// `DELUGE_ERR_IO`, as a bare `i8`. A short write is reported as this rather than as success.
const ERR_IO: i8 = -5;
/// `DELUGE_ERR_NOT_FOUND`, as a bare `i8`. The existence op's ANSWER "no", as distinct from every
/// other failure, which means "could not tell".
///
/// A NAMED constant, never written inline in the match below, for the reason `NO_FILESYSTEM` gives:
/// an unknown identifier in pattern position is a fresh BINDING, not a comparison, and this module's
/// sibling shipped exactly that bug -- it compiled, matched EVERY error, and the only complaint was a
/// non-snake-case warning among thirty-odd others.
const NOT_FOUND: i8 = -8;
/// `DELUGE_ERR_EXISTS`, as a bare `i8`. What every segment but the last of an existing path reports,
/// and therefore SUCCESS for "make sure this path is there".
///
/// A NAMED constant, never written inline in a pattern, for the reason `NOT_FOUND` gives above.
const EXISTS: i8 = -9;

/// `DelugeFileOpenMode::READ`, matching `file_io.h`'s C enum's declaration order.
const OPEN_READ: u8 = 0;
/// `DelugeFileOpenMode::WRITE_CREATE`: create, or open and keep an existing file's bytes. The write
/// path truncates explicitly afterwards rather than relying on a create mode to do it.
const OPEN_WRITE_CREATE: u8 = 1;

/// `deluge_file_bytes_collect_*`: requested or in flight; ask again, do not re-request.
pub const DELUGE_FILE_BYTES_PENDING: u8 = 0;
/// `deluge_file_bytes_collect_*`: the operation completed and its answer was removed from the slot.
pub const DELUGE_FILE_BYTES_READY: u8 = 1;
/// `deluge_file_bytes_collect_*`: nothing known about this path. Request it (again).
pub const DELUGE_FILE_BYTES_UNKNOWN: u8 = 2;
/// `deluge_file_bytes_collect_*`: the operation did not happen. Terminal -- do NOT re-request.
pub const DELUGE_FILE_BYTES_FAILED: u8 = 3;

/// The values this module hand-duplicates from `file_bytes.h`, pinned.
///
/// Asserting a constant against its own literal looks tautological, and that is the point: it makes
/// a change take two deliberate edits instead of one.
///
/// **The status numbering is the audio loader's, the prefix channel's and the directory channel's,
/// value for value**, and that is a safety property rather than tidiness. Two of these headers will
/// plausibly be included in one translation unit, where
/// `deluge_file_bytes_collect_read(..) == DELUGE_FILE_DIRECTORY_READY` compiles clean and nothing
/// cross-checks it. Under a divergent numbering that expression would mean something else entirely.
const _: () = {
    assert!(MAX_PATH_CHARS == 255); // DELUGE_FILE_BYTES_MAX_PATH - 1
    assert!(MAX_BYTES == 8192); // DELUGE_FILE_BYTES_MAX_BYTES
    assert!(NO_FILESYSTEM == -11); // DELUGE_ERR_NO_FILESYSTEM
    assert!(NO_MEMORY == -13); // DELUGE_ERR_NO_MEMORY
    assert!(ERR_IO == -5); // DELUGE_ERR_IO
    assert!(NOT_FOUND == -8); // DELUGE_ERR_NOT_FOUND
    assert!(EXISTS == -9); // DELUGE_ERR_EXISTS
    assert!(DELUGE_FILE_BYTES_PENDING == 0);
    assert!(DELUGE_FILE_BYTES_READY == 1);
    assert!(DELUGE_FILE_BYTES_UNKNOWN == 2);
    assert!(DELUGE_FILE_BYTES_FAILED == 3);
};

/// Which operation an entry belongs to.
///
/// Part of every key. `SETTINGS/MIDIDevices.XML` is both read and written, and a collect matching on
/// the path alone would hand a read's bytes to a write's caller.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ByteOp {
    Read,
    Write,
    Rename,
    Unlink,
    Exists,
    MkdirAll,
    SetTime,
}

/// What one operation produced.
#[derive(Clone, PartialEq, Eq, Debug)]
pub enum ByteOutcome {
    /// A read's bytes. Whoever takes it owns them; nothing else holds a reference.
    Bytes(ByteBuf),
    /// A write, rename or unlink that reached the card.
    Done,
    /// An existence answer: `true` if the path is there, `false` if it is known not to be.
    ///
    /// A separate variant from `Done` because "the question was answered, and the answer is no" is
    /// not the same as "the operation succeeded" -- collapsing them is what makes an absent file look
    /// like a present one, and vice versa.
    Exists(bool),
    /// The operation failed, carrying the status rather than collapsing it. The callers distinguish
    /// "no such file" (write the defaults) from "the card is unreadable" (write nothing, or the
    /// user's real settings are replaced by defaults), which is the distinction
    /// `existence_policy.h`'s `Presence` exists to preserve.
    ///
    /// An `i8` because that is `DelugeStatus`'s real underlying type (`typedef enum DelugeStatus :
    /// int8_t`), and its values are NEGATIVE -- an unsigned carrier would report every failure as
    /// some large positive status.
    Failed(i8),
}

/// One finished operation, stamped with the card generation it BEGAN under.
struct ByteEntry {
    op: ByteOp,
    key: BytesPath,
    generation: u32,
    outcome: ByteOutcome,
}

/// The one answer this channel holds, if it holds one.
///
/// Passed to [`publish_into`]/[`take_from`]/[`drain`] by reference rather than reached for as a
/// static, so the slot logic is compiled and unit-tested on every build -- including the ones where
/// the live [`RESULT`] does not exist.
#[derive(Default)]
pub struct BytesSlot {
    entry: Option<ByteEntry>,
}

impl BytesSlot {
    pub const fn new() -> Self {
        Self { entry: None }
    }
    pub fn is_empty(&self) -> bool {
        self.entry.is_none()
    }
}

/// Publish `outcome` under `(op, key)`, stamped with `generation`, replacing whatever the slot held.
///
/// A displacement here is not the routine event it is in `file_directory`: single-flight across all
/// three operation kinds means the slot can only still be occupied if a previous answer was never
/// collected, which happens when a caller gives up mid-poll. Nothing is stranded by dropping it --
/// unlike a directory handle, bytes have no owner to hand back to.
///
/// An over-long key is REFUSED and the slot left cleared: unreachable through the FFI, which refuses
/// an over-long path before it ever reaches the driver, but an answer nobody can key is an answer
/// nobody can ever take, and leaving a stale neighbour resident in its place is strictly worse than
/// an empty slot.
pub fn publish_into(
    slot: &mut BytesSlot,
    op: ByteOp,
    key: &str,
    outcome: ByteOutcome,
    generation: u32,
) {
    slot.entry = None;
    let Ok(key) = BytesPath::try_from(key) else {
        debug_assert!(false, "an over-long key cannot reach a publish");
        return;
    };
    slot.entry = Some(ByteEntry {
        op,
        key,
        generation,
        outcome,
    });
}

/// What [`take_from`] found for the operation it was asked about.
#[derive(Clone, PartialEq, Eq, Debug)]
pub enum ByteTake {
    /// This operation's outcome, stamped for the taker's own card generation.
    Ready(ByteOutcome),
    /// Nothing here for this operation. Either the slot holds another one's answer (untouched), or
    /// it held this one's answer stamped for a card that has gone -- in which case the entry has
    /// been REMOVED.
    Absent,
}

/// Take `(op, path)`'s outcome if the slot holds one stamped `generation`.
///
/// A key match with a stale stamp is REMOVED as well as withheld: leaving it resident would let a
/// later take at some future generation find it again, which is the wrong-card bug the flush exists
/// to prevent.
///
/// **This is the safety property, and the only one:** `flush_tables`'s clear is `try_lock`ed and may
/// not land, so nothing upstream of here can be relied on to have removed a stale entry.
///
/// A key MISMATCH leaves the slot alone. It is not this take's answer to discard: the operation it
/// belongs to may be about to collect it.
pub fn take_from(slot: &mut BytesSlot, op: ByteOp, path: &str, generation: u32) -> ByteTake {
    let Some(entry) = slot.entry.as_ref() else {
        return ByteTake::Absent;
    };
    if entry.op != op || entry.key != path {
        return ByteTake::Absent;
    }
    let stamped = entry.generation;
    let entry = slot.entry.take().expect("checked just above");
    if stamped == generation {
        ByteTake::Ready(entry.outcome)
    } else {
        ByteTake::Absent
    }
}

/// Empty the slot. What `flush_tables` uses on an ejection.
pub fn drain(slot: &mut BytesSlot) {
    slot.entry = None;
}

// ── The driver ──────────────────────────────────────────────────────────────

/// The filesystem operations this channel needs, behind a trait so the module compiles and is
/// tested in carriers that have no `crate::efatfs_fs`.
#[cfg(feature = "async_file_bytes")]
pub trait ByteOps {
    /// Read the whole of `path`, or the status that stopped it. A file longer than [`MAX_BYTES`]
    /// must report `NO_MEMORY` and NOT a truncated prefix -- a settings file cut short parses as one
    /// whose remaining settings were absent, silently resetting them.
    async fn read_all(&self, path: &str) -> Result<ByteBuf, i8>;
    /// Write `bytes` over `path`, creating the parent directory if it is absent and truncating any
    /// longer file already there.
    async fn write_all(&self, path: &str, bytes: &[u8]) -> Result<(), i8>;
    /// Rename `from` to `to`.
    async fn rename(&self, from: &str, to: &str) -> Result<(), i8>;
    /// Delete `path`.
    async fn unlink(&self, path: &str) -> Result<(), i8>;
    /// Whether `path` exists.
    ///
    /// `Ok(true)` and `Ok(false)` are both ANSWERS; `Err` means the question could not be answered,
    /// which is not the same as "no" and must never be collapsed into one.
    async fn exists(&self, path: &str) -> Result<bool, i8>;
    /// Create `path` and every missing directory above it. An existing path is SUCCESS.
    async fn mkdir_all(&self, path: &str) -> Result<(), i8>;
    /// Set `path`'s modification time.
    async fn set_time(&self, path: &str, timestamp: FileTimestamp) -> Result<(), i8>;
}

/// One request, as the driver receives it.
#[derive(Clone, PartialEq, Eq, Debug)]
pub enum ByteRequest {
    Read {
        path: BytesPath,
    },
    /// The bytes are OWNED, copied out of the caller's buffer at request time. Borrowing would be
    /// cheaper by one buffer and would put a lifetime rule at every C++ call site -- and a caller
    /// doing something after the call returns is the exact shape of defect this ladder keeps
    /// producing.
    Write {
        path: BytesPath,
        bytes: ByteBuf,
    },
    Rename {
        from: BytesPath,
        to: BytesPath,
    },
    Unlink {
        path: BytesPath,
    },
    Exists {
        path: BytesPath,
    },
    MkdirAll {
        path: BytesPath,
    },
    SetTime {
        path: BytesPath,
        timestamp: FileTimestamp,
    },
}

impl ByteRequest {
    /// The `(kind, key)` an answer to this request will be filed under. The key is the DESTINATION
    /// for a rename, because that is the name the caller goes on to use.
    pub fn key(&self) -> (ByteOp, &str) {
        match self {
            Self::Read { path } => (ByteOp::Read, path.as_str()),
            Self::Write { path, .. } => (ByteOp::Write, path.as_str()),
            Self::Rename { to, .. } => (ByteOp::Rename, to.as_str()),
            Self::Unlink { path } => (ByteOp::Unlink, path.as_str()),
            Self::Exists { path } => (ByteOp::Exists, path.as_str()),
            Self::MkdirAll { path } => (ByteOp::MkdirAll, path.as_str()),
            Self::SetTime { path, .. } => (ByteOp::SetTime, path.as_str()),
        }
    }

    /// [`Self::key`], owned. What the in-flight mark holds, so that mark cannot borrow the request
    /// the driver is about to consume — and so it needs no fallible re-parse, which would have to
    /// invent a key on failure and would then report PENDING for whatever that invented key names.
    pub fn key_owned(&self) -> (ByteOp, BytesPath) {
        match self {
            Self::Read { path } => (ByteOp::Read, path.clone()),
            Self::Write { path, .. } => (ByteOp::Write, path.clone()),
            Self::Rename { to, .. } => (ByteOp::Rename, to.clone()),
            Self::Unlink { path } => (ByteOp::Unlink, path.clone()),
            Self::Exists { path } => (ByteOp::Exists, path.clone()),
            Self::MkdirAll { path } => (ByteOp::MkdirAll, path.clone()),
            Self::SetTime { path, .. } => (ByteOp::SetTime, path.clone()),
        }
    }
}

/// Run one request, mapping its result onto what the slot carries.
///
/// Deliberately trivial. It exists so this channel's shape matches `file_directory::dir_io_once`,
/// `file_prefix::prefix_io_once` and `audio_loader::load_io_once` -- one named function that is the
/// whole of the channel's card contact -- rather than because the mapping is subtle.
#[cfg(feature = "async_file_bytes")]
pub async fn bytes_io_once<O: ByteOps>(ops: &O, request: &ByteRequest) -> ByteOutcome {
    let result = match request {
        ByteRequest::Read { path } => match ops.read_all(path).await {
            Ok(bytes) => return ByteOutcome::Bytes(bytes),
            Err(status) => Err(status),
        },
        ByteRequest::Write { path, bytes } => ops.write_all(path, bytes).await,
        ByteRequest::Rename { from, to } => ops.rename(from, to).await,
        ByteRequest::Unlink { path } => ops.unlink(path).await,
        ByteRequest::MkdirAll { path } => ops.mkdir_all(path).await,
        ByteRequest::SetTime { path, timestamp } => ops.set_time(path, *timestamp).await,
        ByteRequest::Exists { path } => match ops.exists(path).await {
            Ok(present) => return ByteOutcome::Exists(present),
            Err(status) => Err(status),
        },
    };
    match result {
        Ok(()) => ByteOutcome::Done,
        Err(status) => ByteOutcome::Failed(status),
    }
}

// ── The live channel ────────────────────────────────────────────────────────

/// Raised to wake [`file_bytes_task`] when an operation is newly requested. Always compiled -- with
/// the task absent nobody awaits it and signalling is a harmless flag set.
pub static BYTES_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// The one outstanding request, if any.
///
/// A single slot rather than a queue, and unlike `file_directory`'s a newer request never REPLACES
/// an older one: see the module doc on why a dropped write is not a dropped listing.
#[cfg(feature = "async_file_bytes_active")]
static REQUEST: Mutex<CriticalSectionRawMutex, Option<ByteRequest>> = Mutex::new(None);

/// The `(kind, key)` running right now, if any. Exists so the collects can answer `PENDING` rather
/// than `UNKNOWN` in the window between a request leaving [`REQUEST`] and its answer reaching
/// [`RESULT`] -- an `UNKNOWN` there would tell the caller to re-request work that is already
/// running, and it would do so on every tick.
#[cfg(feature = "async_file_bytes_active")]
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<(ByteOp, BytesPath)>> = Mutex::new(None);

/// The finished operation waiting to be collected. See [`BytesSlot`].
#[cfg(feature = "async_file_bytes_active")]
static RESULT: Mutex<CriticalSectionRawMutex, BytesSlot> = Mutex::new(BytesSlot::new());

/// Drop what this channel is holding. The card generation's bump belongs to the CALLER
/// (`card.rs`'s `deluge_card_flush`), which performs it before calling here.
///
/// Runs synchronously from C++ on the interaction tier and must never wait, so all three are
/// `try_lock`ed. **Every one of them may fail, and none of them costs correctness**: the clears are
/// housekeeping, and the safety property is the STAMP compared in [`take_from`]. A surviving entry
/// carries the generation of the card that has gone and is therefore uncollectable, not
/// stale-but-live. Unlike `file_directory` there is no scarce handle to strand, so a skipped clear
/// here costs nothing at all until the next publish overwrites it.
#[cfg(feature = "async_file_bytes_active")]
pub(crate) fn flush_tables() {
    if let Ok(mut request) = REQUEST.try_lock() {
        *request = None;
    }
    if let Ok(mut in_flight) = IN_FLIGHT.try_lock() {
        *in_flight = None;
    }
    if let Ok(mut result) = RESULT.try_lock() {
        drain(&mut result);
    }
}

/// No byte channel on this build: the statics do not exist and nothing was ever published, so there
/// is nothing to discard.
#[cfg(not(feature = "async_file_bytes_active"))]
pub(crate) fn flush_tables() {}

/// Whether the Rust byte channel backs `file_bytes.h` on this BSP/config -- the switch the C++ side
/// asks before choosing the async path over doing the I/O itself. The symbol must always exist so
/// that call site links regardless of which config produced this image; only the return value moves.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_bytes_async_active() -> bool {
    cfg!(feature = "async_file_bytes_active")
}

// ── The request entry points ────────────────────────────────────────────────

/// Request the whole of `path` be read, and wake the driver. See `file_bytes.h`.
///
/// Silently drops an unusable request (NULL, non-UTF-8, over-long, or a busy channel): there is no
/// failure the caller could act on that re-requesting does not already cover, and the collect it
/// runs next tick reports `UNKNOWN`, whose documented meaning is exactly that. A read is the one
/// operation here for which that is true -- see the module doc.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_read(path: *const c_char) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return;
    };
    let _ = enqueue_request(ByteRequest::Read { path });
}

/// Request `len` bytes be written over `path`, and wake the driver. See `file_bytes.h`.
///
/// Returns `false` when the request was REFUSED, which the caller must act on: unlike a read, a
/// dropped write is a lost settings change rather than a question worth re-asking.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string, and `bytes` must point to at least `len`
/// readable bytes. Both are copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_write(
    path: *const c_char,
    bytes: *const u8,
    len: u32,
) -> bool {
    if bytes.is_null() || len as usize > MAX_BYTES {
        return false;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return false;
    };
    // SAFETY: `bytes` points to at least `len` readable bytes, by the caller's contract, and the
    // slice is consumed into an owned buffer before this function returns -- so nothing outlives it.
    let src = unsafe { core::slice::from_raw_parts(bytes, len as usize) };
    let Ok(bytes) = ByteBuf::from_slice(src) else {
        return false; // Cannot happen: `len` is bounded above. Refused rather than clipped.
    };
    enqueue_request(ByteRequest::Write { path, bytes })
}

/// Request `from` be renamed to `to`, and wake the driver. See `file_bytes.h`.
///
/// Refused rather than dropped, like a write: a migration that silently did not happen leaves the
/// file where the next boot will migrate it again.
///
/// # Safety
///
/// `from` and `to` must be valid NUL-terminated C strings. Both are copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_rename(
    from: *const c_char,
    to: *const c_char,
) -> bool {
    // SAFETY: the caller's contract, above.
    let (Some(from), Some(to)) = (unsafe { cstr_to_path(from) }, unsafe { cstr_to_path(to) })
    else {
        return false;
    };
    enqueue_request(ByteRequest::Rename { from, to })
}

/// Request `path` be deleted, and wake the driver. See `file_bytes.h`.
///
/// Refused rather than dropped, like a write: a caller that believes a file is gone when it is not
/// will act on that belief.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_unlink(path: *const c_char) -> bool {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return false;
    };
    enqueue_request(ByteRequest::Unlink { path })
}

/// Request an existence check on `path`, and wake the driver. See `file_bytes.h`.
///
/// Dropped rather than refused, like a read: an unanswered question is covered by the next collect's
/// UNKNOWN, whose documented meaning is "ask again".
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_exists(path: *const c_char) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return;
    };
    let _ = enqueue_request(ByteRequest::Exists { path });
}

/// Request `path` and every missing directory above it be created, and wake the driver. See
/// `file_bytes.h`.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_mkdir_all(path: *const c_char) -> bool {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return false;
    };
    enqueue_request(ByteRequest::MkdirAll { path })
}

/// Request `path`'s modification time be set, and wake the driver. See `file_bytes.h`.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. It is copied out before this returns; the
/// timestamp arrives by value and needs no lifetime at all.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_set_time(
    path: *const c_char,
    timestamp: FileTimestamp,
) -> bool {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return false;
    };
    enqueue_request(ByteRequest::SetTime { path, timestamp })
}

/// Install the request and wake the driver, REFUSING if the channel is already busy.
///
/// `try_lock` rather than a block: this is called synchronously from C++ in whatever context the app
/// happens to be in, and must never wait. Contention reports refusal, which is the safe direction --
/// a caller told `false` retries, whereas a caller told `true` for a request that was dropped waits
/// forever.
///
/// The occupancy test spans REQUEST *and* IN_FLIGHT — but deliberately **not** RESULT.
///
/// An operation picked up but not yet published still owns the channel, so it blocks. An answer
/// nobody has collected does not, and must not: a caller that gives up mid-poll would otherwise
/// leave its answer sitting in the slot with nothing on the system able to remove it, and every
/// subsequent request from every consumer would be refused forever. That is a wedged channel, which
/// is the failure this ladder has already shipped once and had to catch with a neutering test.
///
/// So a publish DISPLACES an uncollected answer, and the displaced caller reads `UNKNOWN` and asks
/// again. That is safe here only because all three operations are idempotent — reading a file twice
/// and writing the same bytes twice both reach the same state. **If a non-idempotent operation is
/// ever added to this channel, this is the decision that has to change**, not the caller.
#[cfg(feature = "async_file_bytes_active")]
fn enqueue_request(request: ByteRequest) -> bool {
    let Ok(mut slot) = REQUEST.try_lock() else {
        return false; // Contended; the caller retries.
    };
    if slot.is_some() || !in_flight_idle() {
        return false;
    }
    *slot = Some(request);
    drop(slot);
    BYTES_WAKE.signal(());
    true
}

/// Whether nothing is running right now.
///
/// Contention reports NOT idle, for the same reason [`enqueue_request`] refuses on contention: it is
/// the direction whose worst case is a retry rather than a request that vanishes.
#[cfg(feature = "async_file_bytes_active")]
fn in_flight_idle() -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight.is_none(),
        Err(_) => false,
    }
}

/// No driver on this build: drop it. Nothing reaches here in practice -- the C++ side gates on
/// [`deluge_file_bytes_async_active`], which is false in this configuration. Reports `false` so a
/// caller that somehow did reach it takes its synchronous arm rather than polling forever.
#[cfg(not(feature = "async_file_bytes_active"))]
fn enqueue_request(_request: ByteRequest) -> bool {
    false
}

// ── The collect entry points ────────────────────────────────────────────────

/// Collect the bytes read for `path`, if they are ready. See `file_bytes.h`.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string; `out_buf` must point to at least `buf_cap`
/// writable bytes; `out_len` and `out_status` must point to writable objects of their types.
/// `out_buf` and `out_len` are written ONLY on [`DELUGE_FILE_BYTES_READY`], `out_status` ONLY on
/// [`DELUGE_FILE_BYTES_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_read(
    path: *const c_char,
    out_buf: *mut u8,
    buf_cap: u32,
    out_len: *mut u32,
    out_status: *mut i8,
) -> u8 {
    if out_buf.is_null() || out_len.is_null() || out_status.is_null() {
        return DELUGE_FILE_BYTES_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_FILE_BYTES_UNKNOWN;
    };
    match probe(ByteOp::Read, &path) {
        Probe::Ready(ByteOutcome::Bytes(bytes)) => {
            if bytes.len() > buf_cap as usize {
                // The answer is gone either way -- the take removed it -- so this is terminal, and
                // that is deliberate. Reporting PENDING would have the caller poll forever, and
                // reporting a truncated read would have it parse a settings file cut short, whose
                // remaining settings then read as absent and silently reset.
                //
                // SAFETY: non-null (checked above); written only on FAILED, as the header promises.
                unsafe { *out_status = NO_MEMORY };
                return DELUGE_FILE_BYTES_FAILED;
            }
            // SAFETY: `out_buf` has at least `buf_cap` writable bytes by the caller's contract, and
            // `bytes.len() <= buf_cap` was just checked. The regions cannot overlap: the source is
            // this module's own `.bss`.
            unsafe {
                core::ptr::copy_nonoverlapping(bytes.as_ptr(), out_buf, bytes.len());
                *out_len = bytes.len() as u32;
            }
            DELUGE_FILE_BYTES_READY
        }
        // A `Done` filed under Read cannot happen: `bytes_io_once` produces `Bytes` or `Failed` for
        // a read, and the take matched on `ByteOp::Read`. Reported as UNKNOWN rather than asserted
        // because the alternative -- READY with `out_len` unwritten -- hands the caller an
        // uninitialised length.
        Probe::Ready(ByteOutcome::Done) | Probe::Ready(ByteOutcome::Exists(_)) => {
            DELUGE_FILE_BYTES_UNKNOWN
        }
        Probe::Ready(ByteOutcome::Failed(status)) => {
            // SAFETY: as above, and written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_FILE_BYTES_FAILED
        }
        Probe::Pending => DELUGE_FILE_BYTES_PENDING,
        Probe::Unknown => DELUGE_FILE_BYTES_UNKNOWN,
    }
}

/// Collect the outcome of the write to `path`, if it has finished. See `file_bytes.h`.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written ONLY on [`DELUGE_FILE_BYTES_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_write(
    path: *const c_char,
    out_status: *mut i8,
) -> u8 {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::Write, path, out_status) }
}

/// Collect the outcome of the rename to `to`, if it has finished. See `file_bytes.h`.
///
/// # Safety
///
/// `to` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written ONLY on [`DELUGE_FILE_BYTES_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_rename(
    to: *const c_char,
    out_status: *mut i8,
) -> u8 {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::Rename, to, out_status) }
}

/// Collect the outcome of the deletion of `path`, if it has finished. See `file_bytes.h`.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written ONLY on [`DELUGE_FILE_BYTES_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_unlink(
    path: *const c_char,
    out_status: *mut i8,
) -> u8 {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::Unlink, path, out_status) }
}

/// Collect the existence answer for `path`, if there is one. See `file_bytes.h`.
///
/// Not routed through [`collect_completion`] because it writes `out_present`, and because the
/// distinction it carries -- READY+false is an ANSWER, FAILED is "could not tell" -- is the whole
/// reason this operation exists.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string; `out_present` must point to a writable `bool` and
/// `out_status` to a writable `i8`. `out_present` is written ONLY on [`DELUGE_FILE_BYTES_READY`],
/// `out_status` ONLY on [`DELUGE_FILE_BYTES_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_exists(
    path: *const c_char,
    out_present: *mut bool,
    out_status: *mut i8,
) -> u8 {
    if out_present.is_null() || out_status.is_null() {
        return DELUGE_FILE_BYTES_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_FILE_BYTES_UNKNOWN;
    };
    match probe(ByteOp::Exists, &path) {
        Probe::Ready(ByteOutcome::Exists(present)) => {
            // SAFETY: non-null (checked above); written only on READY, as the header promises.
            unsafe { *out_present = present };
            DELUGE_FILE_BYTES_READY
        }
        Probe::Ready(ByteOutcome::Failed(status)) => {
            // SAFETY: as above, and written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_FILE_BYTES_FAILED
        }
        // Neither can be filed under Exists -- `bytes_io_once` produces `Exists` or `Failed` for this
        // op, and the take matched on `ByteOp::Exists`. Reported as UNKNOWN rather than asserted
        // because the alternative is READY with `out_present` left uninitialised.
        Probe::Ready(ByteOutcome::Bytes(_)) | Probe::Ready(ByteOutcome::Done) => {
            DELUGE_FILE_BYTES_UNKNOWN
        }
        Probe::Pending => DELUGE_FILE_BYTES_PENDING,
        Probe::Unknown => DELUGE_FILE_BYTES_UNKNOWN,
    }
}

/// Collect the outcome of the directory creation for `path`, if it has finished. See `file_bytes.h`.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written ONLY on [`DELUGE_FILE_BYTES_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_mkdir_all(
    path: *const c_char,
    out_status: *mut i8,
) -> u8 {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::MkdirAll, path, out_status) }
}

/// Collect the outcome of the timestamp write for `path`, if it has finished. See `file_bytes.h`.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written ONLY on [`DELUGE_FILE_BYTES_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_set_time(
    path: *const c_char,
    out_status: *mut i8,
) -> u8 {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::SetTime, path, out_status) }
}

/// The shared body of the two completion collects, which differ only in the [`ByteOp`] they key on.
///
/// # Safety
///
/// As the two callers document: `path` a valid NUL-terminated C string, `out_status` a writable
/// `i8` written only on FAILED.
unsafe fn collect_completion(op: ByteOp, path: *const c_char, out_status: *mut i8) -> u8 {
    if out_status.is_null() {
        return DELUGE_FILE_BYTES_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_FILE_BYTES_UNKNOWN;
    };
    match probe(op, &path) {
        Probe::Ready(ByteOutcome::Done) => DELUGE_FILE_BYTES_READY,
        Probe::Ready(ByteOutcome::Failed(status)) => {
            // SAFETY: non-null (checked above); written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_FILE_BYTES_FAILED
        }
        // Neither can be filed under a completion op -- see the same case in
        // `deluge_file_bytes_collect_read`.
        Probe::Ready(ByteOutcome::Bytes(_)) | Probe::Ready(ByteOutcome::Exists(_)) => {
            DELUGE_FILE_BYTES_UNKNOWN
        }
        Probe::Pending => DELUGE_FILE_BYTES_PENDING,
        Probe::Unknown => DELUGE_FILE_BYTES_UNKNOWN,
    }
}

/// What one forward pass over the channel found.
///
/// Deliberately produced by a SINGLE pass. Asking "is it ready?" and "is it still coming?" as two
/// separate passes reintroduces the race the ordering exists to close: the driver can publish and
/// clear its in-flight mark between them, and the second pass then reports `UNKNOWN` for an answer
/// the first pass merely missed -- so the caller re-requests work that has already landed.
enum Probe {
    Ready(ByteOutcome),
    Pending,
    Unknown,
}

/// One forward pass: REQUEST, then IN_FLIGHT, then RESULT.
///
/// # Why the probe order is REQUEST -> IN_FLIGHT -> RESULT
///
/// An entry only ever moves forward: [`REQUEST`] -> [`IN_FLIGHT`] -> [`RESULT`]. Three rules make a
/// forward pass unable to miss one, and all three are load-bearing -- they are the audio loader's,
/// the prefix channel's and the directory channel's, unchanged:
///
/// 1. [`take_next_request`] sets `IN_FLIGHT` while still holding the `REQUEST` lock.
/// 2. The driver publishes to `RESULT` *before* clearing `IN_FLIGHT`, so that handoff overlaps
///    rather than gaps.
/// 3. [`is_requested`] and [`is_in_flight`] report `true` when they cannot take their lock -- it is
///    the LOCK, not the table, that covers the REQUEST -> IN_FLIGHT window, in which the entry
///    genuinely is in neither.
///
/// Reading in the reverse order can report `UNKNOWN` for an operation that has already landed, so
/// the caller re-requests work that is done -- and for a WRITE, re-running a write is not merely
/// wasteful, it is a second trip to the card for a change already made.
#[cfg(feature = "async_file_bytes_active")]
fn probe(op: ByteOp, path: &str) -> Probe {
    if is_requested(op, path) || is_in_flight(op, path) {
        return Probe::Pending;
    }
    // A contended slot means the driver is mid-publish. `Pending` is the honest answer: never claim
    // `Unknown` on contention, or the caller re-requests work that is about to land.
    let Ok(mut result) = RESULT.try_lock() else {
        return Probe::Pending;
    };
    // The card generation is read HERE, at the take, not sampled earlier: it is the collector's own
    // view of which card is in the slot right now, and deciding from it is what makes an answer's
    // fate independent of whether the flush that invalidated it managed to take this lock.
    match take_from(&mut result, op, path, deluge_card_generation::current()) {
        ByteTake::Ready(outcome) => Probe::Ready(outcome),
        ByteTake::Absent => Probe::Unknown,
    }
}

/// No driver on this build: nothing was ever requested, so nothing is coming. `Unknown`, never
/// `Pending` -- `Pending` would tell a caller to keep asking forever.
#[cfg(not(feature = "async_file_bytes_active"))]
fn probe(_op: ByteOp, _path: &str) -> Probe {
    Probe::Unknown
}

/// Whether `(op, path)` is the request not yet picked up.
///
/// **Contention reports `true`, and that is load-bearing, not defensive.** The driver marks a
/// request in flight while still holding this lock, so a collect racing that handoff finds the lock
/// taken -- mapping it to `false` here would report `UNKNOWN` for a request mid-handoff, which is
/// exactly the bug the probe order exists to prevent. Do not "simplify" this branch.
#[cfg(feature = "async_file_bytes_active")]
fn is_requested(op: ByteOp, path: &str) -> bool {
    match REQUEST.try_lock() {
        Ok(request) => request.as_ref().is_some_and(|r| r.key() == (op, path)),
        Err(_) => true,
    }
}

/// Whether `(op, path)` is the operation running right now. Contention reports `true`, for the same
/// reason as [`is_requested`] -- and with the same warning.
#[cfg(feature = "async_file_bytes_active")]
fn is_in_flight(op: ByteOp, path: &str) -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight
            .as_ref()
            .is_some_and(|(k, p)| *k == op && p.as_str() == path),
        Err(_) => true,
    }
}

/// Copy a C string into a fixed-capacity [`BytesPath`]. `None` for NULL, non-UTF-8, or a path longer
/// than the bound -- a path that does not fit is refused rather than truncated, because a truncated
/// path names a *different* file, which may well exist and would then be read or, far worse,
/// overwritten.
///
/// # Safety
///
/// `ptr` must be NULL or a valid NUL-terminated C string.
unsafe fn cstr_to_path(ptr: *const c_char) -> Option<BytesPath> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: the caller's contract, above.
    let cstr = unsafe { CStr::from_ptr(ptr) };
    BytesPath::try_from(cstr.to_str().ok()?).ok()
}

// ── The production ops ──────────────────────────────────────────────────────
//
// Only compiled where the efatfs backing is actually linked: on device under `efatfs_streaming`,
// and on host under `host_app`. Kept separate from the driver above so the driver -- and every rule
// in this file -- compiles and is tested in the `#[path]` carriers, which have no `crate::`.

/// The efatfs file operations behind the production ops: the device mount on device, the host shim
/// under `host_app`. One alias rather than a `#[cfg]` at every call site -- the same shape
/// `file_directory::backing` uses, and for the same reason.
#[cfg(all(
    feature = "async_file_bytes",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
mod backing {
    #[cfg(target_os = "none")]
    pub(super) use crate::efatfs_fs::{
        mount, task_file_close, task_file_open, task_file_read_exact, task_file_size,
        task_file_truncate, task_file_write, task_mkdir, task_rename, task_set_time, task_unlink,
    };
    #[cfg(not(target_os = "none"))]
    pub(super) use crate::efatfs_host_shim::{
        mount, task_file_close, task_file_open, task_file_read_exact, task_file_size,
        task_file_truncate, task_file_write, task_mkdir, task_rename, task_set_time, task_unlink,
    };
}

/// The real [`ByteOps`], wired to the efatfs task-context file table. Holds no state: every
/// operation is keyed by path.
#[cfg(all(
    feature = "async_file_bytes",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
pub struct ProdByteOps;

#[cfg(all(
    feature = "async_file_bytes",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
impl ProdByteOps {
    pub fn new() -> Self {
        Self
    }

    /// Open `path`, mounting first if the volume is not mounted yet.
    ///
    /// MOUNTING IS REAL CARD I/O, which is exactly why it happens here: `StorageManager::initSD()`
    /// used to do it before every settings access, and with this channel active those callers run on
    /// the interaction tier. Mounted lazily rather than before every open, so the steady state costs
    /// nothing. `mount()` bumps `deluge_efatfs_mount_generation`, which is how the C++ side still
    /// learns to run `firstCardRead()`.
    async fn open_mounted(path: &str, mode: u8) -> Result<u32, i8> {
        match backing::task_file_open(path, mode).await {
            Err(NO_FILESYSTEM) => {
                backing::mount().await.map_err(|()| NO_FILESYSTEM)?;
                backing::task_file_open(path, mode)
                    .await
                    .map_err(|status| status as i8)
            }
            other => other.map_err(|status| status as i8),
        }
    }

    /// Create one directory, treating "it is already there" as success.
    ///
    /// EXISTS is not a failure for "make sure this path is there" -- it is the commonest outcome of
    /// every segment but the last, and treating it as an error would make the walk fail on any path
    /// that partly exists, which is nearly all of them.
    async fn mkdir_one(path: &str) -> Result<(), i8> {
        match backing::task_mkdir(path).await {
            Ok(()) => Ok(()),
            Err(EXISTS) => Ok(()),
            Err(NO_FILESYSTEM) => {
                // Not mounted. MOUNTING IS REAL CARD I/O, which is exactly why it happens here rather
                // than in the caller -- the same reason every other operation in this module mounts
                // lazily.
                backing::mount().await.map_err(|()| NO_FILESYSTEM)?;
                match backing::task_mkdir(path).await {
                    Ok(()) => Ok(()),
                    Err(EXISTS) => Ok(()),
                    Err(status) => Err(status as i8),
                }
            }
            Err(status) => Err(status as i8),
        }
    }

    /// The parent directory of `path`, or `None` when it names something at the card root.
    fn parent_of(path: &str) -> Option<&str> {
        path.rfind('/').map(|cut| &path[..cut])
    }
}

#[cfg(all(
    feature = "async_file_bytes",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
impl ByteOps for ProdByteOps {
    async fn read_all(&self, path: &str) -> Result<ByteBuf, i8> {
        let handle = Self::open_mounted(path, OPEN_READ).await?;
        // Every path below must reach the close: a leaked handle costs one of the task-context file
        // table's slots for the rest of the mount, so the result is bound and closed rather than
        // returned with `?`.
        let result = async {
            let size = backing::task_file_size(handle)
                .await
                .map_err(|status| status as i8)?;
            if size as usize > MAX_BYTES {
                // NOT a truncated prefix. A settings file cut short parses as one whose remaining
                // settings were absent, which silently resets every setting past the cut.
                return Err(NO_MEMORY);
            }
            let mut buf = ByteBuf::new();
            buf.resize_default(size as usize).map_err(|()| NO_MEMORY)?;
            let read = backing::task_file_read_exact(handle, &mut buf)
                .await
                .map_err(|status| status as i8)?;
            // A short read is a failure, not a smaller file: `task_file_size` just said how long it
            // is, so anything less means the read stopped early.
            if read != buf.len() {
                return Err(ERR_IO);
            }
            Ok(buf)
        }
        .await;
        backing::task_file_close(handle).await;
        result
    }

    async fn write_all(&self, path: &str, bytes: &[u8]) -> Result<(), i8> {
        if let Some(parent) = Self::parent_of(path) {
            // Result deliberately discarded: EXISTS is the commonest outcome and is not a failure
            // for "make sure this directory is there", and anything else surfaces as the open
            // failing, which is the answer the caller acts on. This is the `mkdir SETTINGS_FOLDER`
            // that each of the four consumers used to do inline, on whatever tier it was standing.
            let _ = backing::task_mkdir(parent).await;
        }
        let handle = Self::open_mounted(path, OPEN_WRITE_CREATE).await?;
        let result = async {
            // Truncate FIRST. WRITE_CREATE keeps an existing file's bytes, so writing a shorter
            // settings file over a longer one would leave the old tail behind -- and a settings XML
            // with a second, older document glued to its end is worse than either file alone.
            backing::task_file_truncate(handle, 0)
                .await
                .map_err(|status| status as i8)?;
            let written = backing::task_file_write(handle, bytes)
                .await
                .map_err(|status| status as i8)?;
            // A short write is reported as a failure rather than as success. The caller's whole
            // purpose is that these bytes are on the card; a partial settings file is not a smaller
            // success, it is a corrupt file that will parse.
            if written != bytes.len() {
                return Err(ERR_IO);
            }
            Ok(())
        }
        .await;
        // The close is what flushes: `task_file_close` releases the context, and dropping the handle
        // without it would leave the write unflushed as well as leaking the slot.
        backing::task_file_close(handle).await;
        result
    }

    async fn unlink(&self, path: &str) -> Result<(), i8> {
        backing::task_unlink(path)
            .await
            .map_err(|status| status as i8)
    }

    async fn exists(&self, path: &str) -> Result<bool, i8> {
        // Open for READ and read the outcome, which is what `deluge::io::presence_from_open` does on
        // the C++ side. NOT_FOUND is the ANSWER "no"; every other failure is "could not tell", and
        // the two must stay apart all the way to `StartupPresence`.
        //
        // Deliberately NOT expressed as a read of the file's bytes: the paths this answers for are
        // SONG files, far above `MAX_BYTES`, so a read would report `NO_MEMORY` for a song that is
        // there and a caller collapsing that into "absent" would overwrite it.
        match ProdByteOps::open_mounted(path, OPEN_READ).await {
            Ok(handle) => {
                backing::task_file_close(handle).await;
                Ok(true)
            }
            Err(NOT_FOUND) => Ok(false),
            Err(status) => Err(status),
        }
    }

    async fn mkdir_all(&self, path: &str) -> Result<(), i8> {
        // The walk that used to live in `smSysex::createPathDirectories` and
        // `Browser::createFoldersRecursiveIfNotExists`, moved below the seam once so neither caller
        // needs a phase machine to do it.
        //
        // Leading and repeated separators produce empty segments, which are skipped rather than
        // passed to mkdir: `task_mkdir("")` is not a directory anyone asked for.
        let mut start = 0usize;
        for (index, byte) in path.char_indices() {
            if byte == '/' {
                if index > start {
                    ProdByteOps::mkdir_one(&path[..index]).await?;
                }
                start = index + 1;
            }
        }
        if path.len() > start {
            ProdByteOps::mkdir_one(path).await?;
        }
        Ok(())
    }

    async fn set_time(&self, path: &str, timestamp: FileTimestamp) -> Result<(), i8> {
        backing::task_set_time(
            path,
            timestamp.year,
            timestamp.month,
            timestamp.day,
            timestamp.hour,
            timestamp.minute,
            timestamp.second,
        )
        .await
        .map_err(|status| status as i8)
    }

    async fn rename(&self, from: &str, to: &str) -> Result<(), i8> {
        if let Some(parent) = Self::parent_of(to) {
            let _ = backing::task_mkdir(parent).await;
        }
        backing::task_rename(from, to)
            .await
            .map_err(|status| status as i8)
    }
}

// ── The task ────────────────────────────────────────────────────────────────

/// The whole-file worker: wakes on [`BYTES_WAKE`], runs the outstanding operation, publishes its
/// answer, then goes back to sleep.
///
/// [`IN_FLIGHT`] is set before the operation and cleared only after the answer is published, so the
/// collects never see a gap in which a running operation looks unknown.
///
/// # A failed operation publishes a FAILURE, and publishing it is not optional
///
/// Publishing nothing would report `UNKNOWN`, whose documented meaning is "request it (again)" -- so
/// a settings file that cannot be read would be re-requested on every single tick, forever. A
/// published failure ends that: it is terminal, exactly as a published answer is, and it carries the
/// status so the caller can tell "no such file, write the defaults" from "the card is unreadable,
/// write nothing".
#[cfg(all(
    feature = "async_file_bytes_active",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
#[embassy_executor::task]
pub async fn file_bytes_task() {
    BYTES_WAKE.wait().await;
    let ops = ProdByteOps::new();
    loop {
        while let Some((request, generation)) = take_next_request().await {
            let outcome = bytes_io_once(&ops, &request).await;
            // Publish BEFORE clearing IN_FLIGHT, never after: the entry is briefly in both tables,
            // which is harmless, whereas the reverse leaves a window in which it is in neither and a
            // collect would report `UNKNOWN` for an operation that just succeeded -- prompting the
            // caller to run it again.
            //
            // `generation` was sampled before the operation above, so a card pulled during it is
            // what makes this publish a no-op.
            let (op, key) = request.key();
            publish_result(op, key, outcome, generation).await;
            mark_in_flight(None).await;
        }
        BYTES_WAKE.wait().await;
    }
}

/// Take the outstanding request and mark it in flight, atomically with respect to the collects, and
/// report the card generation the operation begins under.
///
/// The mark happens while the [`REQUEST`] lock is still held, so there is no instant at which the
/// request has left the slot but not yet appeared as in-flight. A collect racing this either sees it
/// requested, or blocks on the `try_lock` and reports `PENDING` -- never `UNKNOWN`.
///
/// The generation is read HERE, before the first `await` that can suspend this task and therefore
/// before any I/O, because that is what makes it mean "the card this operation is touching". Reading
/// it later -- at publish time -- reads the value a flush has already installed and defeats the
/// check entirely.
#[cfg(all(
    feature = "async_file_bytes_active",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
async fn take_next_request() -> Option<(ByteRequest, u32)> {
    let mut slot = REQUEST.lock().await;
    let request = slot.take()?;
    let generation = deluge_card_generation::current();
    mark_in_flight(Some(request.key_owned())).await;
    Some((request, generation))
}

/// Record (or clear) the operation running right now. Called by [`file_bytes_task`] while it still
/// holds the [`REQUEST`] lock -- see [`take_next_request`].
///
/// Also the seam the host tests drive to exercise the collects' IN_FLIGHT branch, which no test
/// could reach otherwise because no test runs the task.
#[cfg(feature = "async_file_bytes_active")]
pub async fn mark_in_flight(entry: Option<(ByteOp, BytesPath)>) {
    *IN_FLIGHT.lock().await = entry;
}

/// Publish one finished operation into the live [`RESULT`] slot, stamped with the card generation it
/// began under, and skip the publish entirely if that card is already gone.
///
/// # The early-drop check is an optimisation, not the guarantee
///
/// There is no happens-before edge from `deluge_card_generation::bump`'s `fetch_add` to this `load`
/// when this task wins the `RESULT` lock (the two synchronise only through the lock, in the other
/// direction), so this check can legitimately read the pre-flush generation, pass, and publish. The
/// entry it publishes still carries the pre-flush stamp, and [`take_from`] drops it. Do not rewrite
/// this as the safety property.
///
/// The check happens with the lock already held, not before acquiring it, so that it is at least
/// ordered against a flush that DID take the lock.
#[cfg(feature = "async_file_bytes_active")]
pub async fn publish_result(op: ByteOp, key: &str, outcome: ByteOutcome, generation: u32) {
    let mut result = RESULT.lock().await;
    if generation != deluge_card_generation::current() {
        // The card this ran against is gone. A FAILED answer is dropped here too, and must be: the
        // failure belongs to the card that has gone, and reporting it as terminal against the card
        // now in the slot would put an error on screen for a file nobody has touched yet.
        return;
    }
    publish_into(&mut result, op, key, outcome, generation);
}
