//! The whole-file byte channel: the Rust side of `libdeluge/file_bytes.h`.
//!
//! Lets a caller that must not wait on the card (the interaction tier) read, write, rename, delete,
//! stat, copy or probe a file: it drops off a request and collects the answer on a later tick,
//! while `file_bytes_task` runs the operation on the storage tier.
//!
//! # Bytes, not handles
//!
//! Small files (up to [`MAX_BYTES`], e.g. settings XML) cross as bytes copied through this module's
//! buffers; parsing, policy and migration stay in C++. No caller holds an open file across a tick:
//! only the task ever has a file open, one operation at a time.
//!
//! # One operation at a time, keyed by kind and path
//!
//! `REQUEST`, `IN_FLIGHT` and `RESULT` each hold at most one entry, keyed by [`ByteOp`] as
//! well as the path: `SETTINGS/MIDIDevices.XML` is both read and written, and a path-only match
//! would hand a read's bytes to a write's caller, which would then believe its settings were saved.
//! Single-flight rather than a queue because the filesystem serialises everything anyway (`with_fs`
//! is one mutex), and it makes refusal observable.
//!
//! # Every request is refused while the channel is busy
//!
//! Busy means a request is queued, an operation is in flight, or an uncollected answer of a
//! [`non_idempotent`] kind is protected for its owner; see `enqueue_request`. Nothing is queued
//! or replaced. A refused read, existence check or stat returns nothing: its collect reads PENDING
//! if the same request is the one in flight (so duplicate reads coalesce) and UNKNOWN otherwise,
//! and re-asking costs nothing. Every other request returns `false` when refused, and the caller
//! keeps its intent and retries. Unlike `file_directory`, a newer request never supersedes an older
//! one: two rapid toggles of a setting are two intents, and dropping the first loses a change.
//!
//! # Caller-buffer operations
//!
//! [`ByteOp::ReadInto`] and [`ByteOp::WriteFrom`] move whole documents (songs, kits, presets) far
//! larger than [`MAX_BYTES`] without copying: the request carries a pointer into the caller's
//! buffer. The caller keeps the buffer alive and untouched until the collect is terminal and
//! [`deluge_file_bytes_holds`] no longer names it. The pointer travels as a `usize` so the request
//! is `Send`, and becomes a slice only inside the task, for one operation. These requests never
//! replace or get replaced; see [`accepts`].
//!
//! # Card generations
//!
//! Every request is stamped with the card generation it was asked under, and the task drops one
//! whose card has gone unrun ([`admit_at_take`]). Every answer is stamped with the generation its
//! operation began under and checked at the take ([`take_from`]), never only at publish:
//! `deluge_card_flush`'s clears are `try_lock`ed and may not land. A dropped answer owns nothing;
//! its caller reads UNKNOWN and asks again.

use core::ffi::{CStr, c_char};
use core::sync::atomic::{AtomicBool, Ordering};

use crate::channel::{
    DELUGE_CHANNEL_FAILED, DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_READY, DELUGE_CHANNEL_UNKNOWN,
    DELUGE_PATH_MAX, DelugeChannelStatus,
};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::mutex::MutexGuard;
use embassy_sync::signal::Signal;
use embassy_time::{Duration, Instant};

/// The longest path this channel keys on, in bytes of text: [`DELUGE_PATH_MAX`] less the NUL.
pub(crate) const MAX_PATH_CHARS: usize = DELUGE_PATH_MAX - 1;

/// The largest file this channel copies through its own buffers.
///
/// About 4x the largest real settings file (~2 KiB). Sizes the three buffers `REQUEST_BYTES`,
/// `WORK_BYTES` and `RESULT_BYTES` (in SDRAM on device). Pinned against
/// `DELUGE_FILE_BYTES_MAX_BYTES`, so changing it is a change on both sides of the ABI.
///
/// A larger file is never truncated: the read reports `NO_MEMORY` and the write is refused. A
/// settings file cut short would parse with its remaining settings silently reset.
pub const MAX_BYTES: usize = 8192;

/// One path, inline.
///
/// Nothing in this module may use the Rust global heap: that allocator assumes single-threaded use
/// and this driver runs on a preempting executor.
pub type BytesPath = heapless::String<MAX_PATH_CHARS>;

/// A hand mirror of `DelugeTimestamp` (`libdeluge/types.h`), for the plain host build, which has no
/// bindgen output. Passed by value across the ABI; the asserts below pin it to the C layout.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct FileTimestamp {
    pub year: u16,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
}

/// The timestamp type [`deluge_file_bytes_request_set_time`] takes: bindgen's `DelugeTimestamp`
/// where there is bindgen output (so the export's `abi_pin!` holds), else [`FileTimestamp`].
#[cfg(has_app)]
type AbiTimestamp = crate::sys::DelugeTimestamp;
#[cfg(not(has_app))]
type AbiTimestamp = FileTimestamp;

// `DelugeTimestamp`'s layout (`libdeluge/types.h`).
const _: () = {
    assert!(core::mem::size_of::<FileTimestamp>() == 8);
    assert!(core::mem::align_of::<FileTimestamp>() == 2);
    assert!(core::mem::offset_of!(FileTimestamp, year) == 0);
    assert!(core::mem::offset_of!(FileTimestamp, month) == 2);
    assert!(core::mem::offset_of!(FileTimestamp, day) == 3);
    assert!(core::mem::offset_of!(FileTimestamp, hour) == 4);
    assert!(core::mem::offset_of!(FileTimestamp, minute) == 5);
    assert!(core::mem::offset_of!(FileTimestamp, second) == 6);
};

/// The largest single offset-addressed transfer, in bytes (`DELUGE_FILE_BYTES_BLOCK`).
///
/// A transfer bound, not a buffer: block reads and writes use the same [`MAX_BYTES`] buffers as
/// whole files, and [`ByteOps::copy_file`] moves its chunks at this size.
pub const BLOCK_BYTES: usize = 1024;

/// `DELUGE_ERR_NO_FILESYSTEM`, as a bare `i8` (the plain host build has no bindgen output).
///
/// These status constants must be used by name in patterns, never re-spelled: an unknown
/// identifier in pattern position is a fresh binding that matches every value.
pub(crate) const NO_FILESYSTEM: i8 = -11;
/// `DELUGE_ERR_NO_MEMORY`, as a bare `i8`. What a file too large for [`MAX_BYTES`] reports.
pub(crate) const NO_MEMORY: i8 = -13;
/// `DELUGE_ERR_IO`, as a bare `i8`. A short write is reported as this rather than as success.
pub(crate) const ERR_IO: i8 = -5;
/// `DELUGE_ERR_NOT_FOUND`, as a bare `i8`. For an existence check this is the answer "no"; every
/// other failure means "could not tell".
pub(crate) const NOT_FOUND: i8 = -8;
/// `DELUGE_ERR_EXISTS`, as a bare `i8`. Success for "make sure this directory is there".
pub(crate) const EXISTS: i8 = -9;

/// `DelugeFileOpenMode::READ` (`libdeluge/file_io.h`).
pub(crate) const OPEN_READ: u8 = 0;
/// `DelugeFileOpenMode::WRITE_CREATE`. Truncates an existing file (see [`ByteOps::write_at`]'s
/// implementation for why block writes avoid it).
pub(crate) const OPEN_WRITE_CREATE: u8 = 1;
/// `DelugeStreamMode::READ` (`libdeluge/stream_io.h`): the caller-buffer operations' open modes.
pub(crate) const STREAM_READ: u8 = 0;
/// `DelugeStreamMode::CREATE`: create, truncating an existing file.
pub(crate) const STREAM_CREATE: u8 = 1;
/// `DelugeStreamMode::CREATE_NEW`: create only if the name is free, else `EXISTS`.
pub(crate) const STREAM_CREATE_NEW: u8 = 2;

/// Which operation an entry belongs to; part of every key (see the module docs).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ByteOp {
    Read,
    Write,
    Rename,
    Unlink,
    Exists,
    DirExists,
    MkdirAll,
    SetTime,
    ReadAt,
    WriteAt,
    Stat,
    Copy,
    /// A whole file read into the caller's buffer.
    ReadInto,
    /// A whole file written from the caller's buffer.
    WriteFrom,
}

/// Whether a new read may replace a queued one. `false`: every request is refused while another is
/// queued.
pub const READ_REPLACES_READ: bool = false;

/// Whether a new request of kind `new` may enter the request slot while `queued` is in it.
///
/// A caller-buffer request never replaces and is never replaced: replacing a queued document write
/// would silently lose a save.
pub fn accepts(queued: Option<ByteOp>, new: ByteOp) -> bool {
    match queued {
        None => true,
        Some(q) if caller_buffer(q) || caller_buffer(new) => false,
        Some(_) => READ_REPLACES_READ,
    }
}

/// Whether `op` reads into or writes from a buffer its caller owns.
fn caller_buffer(op: ByteOp) -> bool {
    matches!(op, ByteOp::ReadInto | ByteOp::WriteFrom)
}

/// Whether running `op` a second time does something the first run did not.
///
/// A rerun rename or unlink reports `NOT_FOUND` for work that happened. A rerun caller-buffer write
/// either finds its own file (exclusive: `EXISTS`, so a save offers to overwrite what it just
/// wrote) or writes up to a megabyte again. Every other kind reaches the same state when run twice.
pub fn non_idempotent(op: ByteOp) -> bool {
    matches!(op, ByteOp::Rename | ByteOp::Unlink | ByteOp::WriteFrom)
}

/// How long an uncollected answer of a [`non_idempotent`] kind may go on refusing new requests
/// before the channel gives up on its owner and lets a new request displace it. Measured from the
/// first request it refused, so never shorter than from its publish.
///
/// A time, not a count of refusals, because callers ask when something wakes them rather than on
/// every tick: a count would take longer to reach the fewer callers there were asking, and could
/// outlast a refused caller's own bound.
///
/// Ordered against both sides:
/// - Well before any refused caller gives up. The shortest bound among the byte channel's callers
///   is 0.3 s (SysEx's `deluge::storage::kSysExStallUs`, the startup prelude's and the recorder's
///   `kMaxUnproductiveUs`); the rest use the 3.6 s channel bound (`deluge::io::kChannelStallUs`)
///   or longer. An answer that ages out raises no wake, so what admits a refused caller is its own
///   re-ask after the span: either a re-ask it makes before judging its bound (SysEx, the startup
///   fail-safe and the saves ask first, so their deadline run, at least 0.3 s after their first
///   refusal, is admitted), or a re-ask it schedules within the span plus a margin (the settings
///   machine, which judges its bound before asking, re-asks within 150 ms). Either way the span
///   must be well inside the shortest bound, as a third of it is.
/// - Well after any owner still collecting has taken it. Every owner of these answers (document,
///   preset and startup-song saves, SysEx path operations, the recorder's abort) collects on the
///   `DELUGE_WAKE_STORAGE` the publish raises or on a job tick of at most 6 ms, so an answer that
///   ages out belongs to an owner that has gone.
pub const PROTECTED_ANSWER_SPAN: Duration = Duration::from_millis(PROTECTED_ANSWER_SPAN_MS);

/// [`PROTECTED_ANSWER_SPAN`] in milliseconds, pinned against
/// `DELUGE_FILE_BYTES_PROTECTED_ANSWER_SPAN_MS`, which C++ callers schedule their re-ask by.
pub const PROTECTED_ANSWER_SPAN_MS: u64 = 100;

/// Whether an answer of kind `held`, stamped `stamp`, holds the channel against a new request asked
/// under `generation`, when it has been refusing requests for `refusing_for`.
pub fn answer_blocks(
    held: Option<ByteOp>,
    stamp: Option<u32>,
    generation: u32,
    refusing_for: Duration,
) -> bool {
    match (held, stamp) {
        (Some(op), Some(stamp)) => {
            non_idempotent(op) && stamp == generation && refusing_for < PROTECTED_ANSWER_SPAN
        }
        _ => false,
    }
}

/// Decides what the driver does with a request it takes: `None` drops it unrun, `Some(buffer)` runs
/// it with `buffer` named as running (0 for a kind with no caller buffer).
///
/// A request asked under a card that has gone is dropped: running it would touch the new card, and
/// its answer could never be collected. This, not the flush's `try_lock`ed clear, is what keeps an
/// old card's request off the new card.
pub fn admit_at_take(request: &ByteRequest, asked_under: u32, current: u32) -> Option<usize> {
    if asked_under != current {
        return None;
    }
    Some(request.caller_buffer())
}

/// What one operation produced.
#[derive(Clone, PartialEq, Eq, Debug)]
pub enum ByteOutcome {
    /// A whole-file read's length. The bytes are in the result buffer, published in the same
    /// `RESULT` lock hold as this entry, and copied out in the same hold as the take.
    Bytes(u32),
    /// A write, rename or unlink that reached the card.
    Done,
    /// An existence answer: `true` if the path is there, `false` if it is known not to be.
    Exists(bool),
    /// One offset-addressed read's length, its bytes in the result buffer as for `Bytes`. Distinct
    /// so a block is never handed to a caller expecting a whole file.
    Block(u32),
    /// A `stat`'s answer.
    Size(u32),
    /// A `write_at`'s or `WriteFrom`'s byte count. A short `write_at` is reported as is; the caller
    /// decides what it means.
    Wrote(u32),
    /// A `ReadInto`'s length: that many bytes are in the caller's buffer.
    Filled(u32),
    /// A caller-buffer operation that did not happen. `needed` is the file's size when a `ReadInto`
    /// found it larger than the buffer (`status` is then `NO_MEMORY`), and 0 otherwise.
    Refused { status: i8, needed: u32 },
    /// The operation failed with this `DelugeStatus` (an `int8_t`; failures are negative).
    ///
    /// Callers must tell "no such file" (write the defaults) from "the card is unreadable" (write
    /// nothing), so the status is never collapsed.
    Failed(i8),
}

/// What a byte-channel answer is keyed by: the operation and its path.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct ByteKey {
    pub op: ByteOp,
    pub path: BytesPath,
}

impl PartialEq<(ByteOp, &str)> for ByteKey {
    fn eq(&self, other: &(ByteOp, &str)) -> bool {
        self.op == other.0 && self.path.as_str() == other.1
    }
}

// No byte outcome carries a handle, so the default `orphan` (always `None`) is right.
impl crate::channel::Answer for ByteOutcome {}

/// The byte channel's answer slot: a [`crate::channel::AnswerSlot`] plus when the protected answer
/// first refused a request.
///
/// Passed to [`publish_into`], [`take_from`] and [`drain`] by reference rather than reached as a
/// static, so the slot logic is unit-tested without the live `RESULT`.
pub struct BytesSlot {
    inner: crate::channel::AnswerSlot<ByteKey, ByteOutcome>,
    /// When the answer held first refused a request, or `None` if it has refused none. See
    /// [`PROTECTED_ANSWER_SPAN`].
    refusing_since: Option<Instant>,
}

impl BytesSlot {
    /// An empty slot.
    pub const fn new() -> Self {
        Self {
            inner: crate::channel::AnswerSlot::new(),
            refusing_since: None,
        }
    }
    /// Whether the slot holds no answer.
    pub fn is_empty(&self) -> bool {
        self.inner.is_empty()
    }

    /// Whether a new request asked under `generation` at `now` may enter the channel, as far as the
    /// answer held goes. The first refusal starts [`PROTECTED_ANSWER_SPAN`].
    pub fn admits_new_request(&mut self, generation: u32, now: Instant) -> bool {
        let held = self.inner.key().map(|k| k.op);
        let refusing_for = self
            .refusing_since
            .map_or(Duration::MIN, |since| now.saturating_duration_since(since));
        if answer_blocks(held, self.inner.generation(), generation, refusing_for) {
            self.refusing_since.get_or_insert(now);
            return false;
        }
        true
    }

    /// Whether the answer held has refused a request, so taking it should wake the caller it held
    /// off.
    pub fn has_refused(&self) -> bool {
        self.refusing_since.is_some()
    }
}

impl Default for BytesSlot {
    fn default() -> Self {
        Self::new()
    }
}

/// What [`copy_out`] did with one answer's bytes.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CopyOut {
    /// All `len` bytes reached the destination.
    Copied(u32),
    /// The destination is shorter than the answer. Nothing was written.
    TooBig,
    /// The answer claims more bytes than its buffer holds, which no producer can publish. Nothing
    /// was written; the collect reports UNKNOWN rather than panicking on the interaction tier.
    Invalid,
}

/// Copies the first `len` bytes of `src` into the front of `dst`, or nothing at all.
///
/// Every collect that hands bytes to C++ goes through here, so there is never a partial copy.
/// `Invalid` is checked first, so an impossible answer is not mistaken for a file that does not
/// fit.
pub fn copy_out(src: &[u8], len: u32, dst: &mut [u8]) -> CopyOut {
    let Some(bytes) = src.get(..len as usize) else {
        return CopyOut::Invalid;
    };
    let Some(into) = dst.get_mut(..len as usize) else {
        return CopyOut::TooBig;
    };
    into.copy_from_slice(bytes);
    CopyOut::Copied(len)
}

/// A one-time grant: the first [`claim`](Self::claim) succeeds and every later one fails.
///
/// Hands the work buffer to the task. A type rather than a bare flag so it is host-tested.
pub struct ClaimOnce(AtomicBool);

impl ClaimOnce {
    /// An unclaimed grant.
    pub const fn new() -> Self {
        Self(AtomicBool::new(false))
    }

    /// Returns `true` the first time only.
    pub fn claim(&self) -> bool {
        !self.0.swap(true, Ordering::AcqRel)
    }
}

/// Publishes `outcome` under `(op, key)`, stamped with `generation`, replacing whatever the slot
/// held.
///
/// Anything displaced is an uncollected answer that was not holding the channel (idempotent, past
/// [`PROTECTED_ANSWER_SPAN`], or from a gone card); its owner reads UNKNOWN and asks again. It
/// owns nothing to release.
///
/// An over-long key (unreachable through the FFI, which refuses such paths) leaves the slot empty.
pub fn publish_into(
    slot: &mut BytesSlot,
    op: ByteOp,
    key: &str,
    outcome: ByteOutcome,
    generation: u32,
) {
    slot.inner.drain();
    slot.refusing_since = None;
    let Ok(path) = BytesPath::try_from(key) else {
        debug_assert!(false, "an over-long key cannot reach a publish");
        return;
    };
    slot.inner
        .publish(ByteKey { op, path }, outcome, generation);
}

/// What [`take_from`] found for the operation it was asked about.
#[derive(Clone, PartialEq, Eq, Debug)]
pub enum ByteTake {
    /// This operation's outcome, stamped for the taker's own card generation.
    Ready(ByteOutcome),
    /// Nothing here for this operation. Either the slot holds another operation's answer
    /// (untouched), or it held this one's stamped for a card that has gone, which has been removed.
    Absent,
}

/// Takes `(op, path)`'s outcome if the slot holds one stamped `generation`.
///
/// This check is the safety property against wrong-card answers: `flush_tables`'s clear is
/// `try_lock`ed and may not land. A key match with a stale stamp is removed as well as withheld; a
/// key mismatch leaves the slot alone for the operation it belongs to.
pub fn take_from(slot: &mut BytesSlot, op: ByteOp, path: &str, generation: u32) -> ByteTake {
    let taken = match slot.inner.take(&(op, path), generation) {
        crate::channel::Take::Ready(outcome) => ByteTake::Ready(outcome),
        crate::channel::Take::Absent { .. } => ByteTake::Absent,
    };
    if slot.inner.is_empty() {
        slot.refusing_since = None;
    }
    taken
}

/// Empties the slot, as `flush_tables` does on an ejection.
pub fn drain(slot: &mut BytesSlot) {
    slot.inner.drain();
    slot.refusing_since = None;
}

// ── The driver ──────────────────────────────────────────────────────────────

/// The filesystem operations this channel needs, behind a trait so the driver is host-tested
/// without efatfs.
pub trait ByteOps {
    /// Reads the whole of `path` into the front of `into`, returning its length. A file longer than
    /// [`MAX_BYTES`] must report `NO_MEMORY`, never a truncated prefix.
    async fn read_all(&self, path: &str, into: &mut [u8; MAX_BYTES]) -> Result<usize, i8>;
    /// Writes `bytes` as the whole of `path`, creating the parent directory if it is absent.
    async fn write_all(&self, path: &str, bytes: &[u8]) -> Result<(), i8>;
    /// Renames `from` to `to`, creating `to`'s parent directory if it is absent.
    async fn rename(&self, from: &str, to: &str) -> Result<(), i8>;
    /// Deletes `path`.
    async fn unlink(&self, path: &str) -> Result<(), i8>;
    /// Whether `path` exists as a file.
    ///
    /// `Ok(false)` is the answer "no"; `Err` means the question could not be answered, and must
    /// never be collapsed into "no".
    async fn exists(&self, path: &str) -> Result<bool, i8>;
    /// Whether `path` exists as a directory, with the same three states as [`Self::exists`].
    async fn dir_exists(&self, path: &str) -> Result<bool, i8>;
    /// Creates `path` and every missing directory above it. An existing path is success.
    async fn mkdir_all(&self, path: &str) -> Result<(), i8>;
    /// Sets `path`'s modification time.
    async fn set_time(&self, path: &str, timestamp: FileTimestamp) -> Result<(), i8>;
    /// Reads up to `into.len()` bytes from `path` at `offset`, returning how many. A short result
    /// means end-of-file, not failure.
    async fn read_at(&self, path: &str, offset: u32, into: &mut [u8]) -> Result<usize, i8>;
    /// Writes `bytes` into `path` at `offset` without truncating, creating the file if absent.
    async fn write_at(&self, path: &str, offset: u32, bytes: &[u8]) -> Result<u32, i8>;
    /// Returns `path`'s size in bytes.
    async fn stat(&self, path: &str) -> Result<u32, i8>;
    /// Copies `from` to `to`, whole, moving up to [`BLOCK_BYTES`] at a time through `scratch`.
    async fn copy_file(&self, from: &str, to: &str, scratch: &mut [u8]) -> Result<(), i8>;
    /// Reads the whole of `path` into `into`, returning its length.
    ///
    /// # Errors
    ///
    /// A file larger than `into` is refused with `NO_MEMORY` and its size, and nothing is read. A
    /// failed open reports the open's status (`NOT_FOUND` for a missing file). A short read is
    /// `IO`.
    async fn read_into(&self, path: &str, into: &mut [u8]) -> Result<u32, (i8, u32)>;
    /// Writes `bytes` as the whole of `path`, creating every missing directory above it.
    ///
    /// # Errors
    ///
    /// With `exclusive`, the open itself fails with `EXISTS` if the file is there, so nothing can
    /// slip in between a check and the create. Any other failed open reports its status. A short
    /// write is `IO`.
    async fn write_new(&self, path: &str, bytes: &[u8], exclusive: bool) -> Result<u32, i8>;
}

/// One request, as the driver receives it.
#[derive(Clone, PartialEq, Eq, Debug)]
pub enum ByteRequest {
    Read {
        path: BytesPath,
    },
    /// The bytes are copied into the request buffer at request time, under the `REQUEST` lock, so
    /// the caller's buffer has no lifetime rule.
    Write {
        path: BytesPath,
        len: u32,
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
    DirExists {
        path: BytesPath,
    },
    MkdirAll {
        path: BytesPath,
    },
    SetTime {
        path: BytesPath,
        timestamp: FileTimestamp,
    },
    ReadAt {
        path: BytesPath,
        offset: u32,
        len: u32,
    },
    /// Bytes in the request buffer, as for `Write`.
    WriteAt {
        path: BytesPath,
        offset: u32,
        len: u32,
    },
    Stat {
        path: BytesPath,
    },
    Copy {
        from: BytesPath,
        to: BytesPath,
    },
    /// `dst` is the address of the caller's `cap`-byte buffer.
    ReadInto {
        path: BytesPath,
        dst: usize,
        cap: u32,
    },
    /// `src` is the address of the caller's `len` bytes.
    WriteFrom {
        path: BytesPath,
        src: usize,
        len: u32,
        may_overwrite: bool,
    },
}

impl ByteRequest {
    /// Returns the `(kind, key)` this request's answer is filed under. The key is the destination
    /// for a rename or copy, the name the caller goes on to use.
    pub fn key(&self) -> (ByteOp, &str) {
        match self {
            Self::Read { path } => (ByteOp::Read, path.as_str()),
            Self::Write { path, .. } => (ByteOp::Write, path.as_str()),
            Self::Rename { to, .. } => (ByteOp::Rename, to.as_str()),
            Self::Unlink { path } => (ByteOp::Unlink, path.as_str()),
            Self::Exists { path } => (ByteOp::Exists, path.as_str()),
            Self::DirExists { path } => (ByteOp::DirExists, path.as_str()),
            Self::MkdirAll { path } => (ByteOp::MkdirAll, path.as_str()),
            Self::SetTime { path, .. } => (ByteOp::SetTime, path.as_str()),
            Self::ReadAt { path, .. } => (ByteOp::ReadAt, path.as_str()),
            Self::WriteAt { path, .. } => (ByteOp::WriteAt, path.as_str()),
            Self::Stat { path } => (ByteOp::Stat, path.as_str()),
            Self::Copy { to, .. } => (ByteOp::Copy, to.as_str()),
            Self::ReadInto { path, .. } => (ByteOp::ReadInto, path.as_str()),
            Self::WriteFrom { path, .. } => (ByteOp::WriteFrom, path.as_str()),
        }
    }

    /// Returns [`Self::key`], owned, for the in-flight mark (which cannot borrow the request the
    /// driver consumes).
    pub fn key_owned(&self) -> (ByteOp, BytesPath) {
        match self {
            Self::Read { path } => (ByteOp::Read, path.clone()),
            Self::Write { path, .. } => (ByteOp::Write, path.clone()),
            Self::Rename { to, .. } => (ByteOp::Rename, to.clone()),
            Self::Unlink { path } => (ByteOp::Unlink, path.clone()),
            Self::Exists { path } => (ByteOp::Exists, path.clone()),
            Self::DirExists { path } => (ByteOp::DirExists, path.clone()),
            Self::MkdirAll { path } => (ByteOp::MkdirAll, path.clone()),
            Self::SetTime { path, .. } => (ByteOp::SetTime, path.clone()),
            Self::ReadAt { path, .. } => (ByteOp::ReadAt, path.clone()),
            Self::WriteAt { path, .. } => (ByteOp::WriteAt, path.clone()),
            Self::Stat { path } => (ByteOp::Stat, path.clone()),
            Self::Copy { to, .. } => (ByteOp::Copy, to.clone()),
            Self::ReadInto { path, .. } => (ByteOp::ReadInto, path.clone()),
            Self::WriteFrom { path, .. } => (ByteOp::WriteFrom, path.clone()),
        }
    }

    /// Returns how many bytes of the request buffer belong to this request: `Some` for the two
    /// buffered writes, `None` for every other kind.
    pub fn payload_len(&self) -> Option<u32> {
        match self {
            Self::Write { len, .. } | Self::WriteAt { len, .. } => Some(*len),
            _ => None,
        }
    }

    /// Returns the address of the caller's buffer this request uses; 0 for a kind with none.
    pub fn caller_buffer(&self) -> usize {
        match self {
            Self::ReadInto { dst, .. } => *dst,
            Self::WriteFrom { src, .. } => *src,
            _ => 0,
        }
    }
}

/// Runs one request and maps its result onto a [`ByteOutcome`]. The whole of the channel's card
/// contact.
pub async fn bytes_io_once<O: ByteOps>(
    ops: &O,
    request: &ByteRequest,
    work: &mut [u8; MAX_BYTES],
) -> ByteOutcome {
    // `work` is the task's own buffer: a write's bytes were copied into it at take, a read leaves
    // its bytes there for `publish_result`, and a copy moves its chunks through it.
    let result = match request {
        ByteRequest::Read { path } => match ops.read_all(path, work).await {
            Ok(len) => return ByteOutcome::Bytes(len as u32),
            Err(status) => Err(status),
        },
        // `get`, not an index: an impossible length must fail the write, not panic the task.
        ByteRequest::Write { path, len } => match work.get(..*len as usize) {
            Some(bytes) => ops.write_all(path, bytes).await,
            None => Err(NO_MEMORY),
        },
        ByteRequest::Rename { from, to } => ops.rename(from, to).await,
        ByteRequest::Unlink { path } => ops.unlink(path).await,
        ByteRequest::MkdirAll { path } => ops.mkdir_all(path).await,
        ByteRequest::SetTime { path, timestamp } => ops.set_time(path, *timestamp).await,
        ByteRequest::Copy { from, to } => ops.copy_file(from, to, work).await,
        ByteRequest::ReadAt { path, offset, len } => {
            let into = &mut work[..(*len as usize).min(BLOCK_BYTES)];
            match ops.read_at(path, *offset, into).await {
                Ok(read) => return ByteOutcome::Block(read as u32),
                Err(status) => Err(status),
            }
        }
        ByteRequest::WriteAt { path, offset, len } => match work.get(..*len as usize) {
            Some(bytes) => match ops.write_at(path, *offset, bytes).await {
                Ok(written) => return ByteOutcome::Wrote(written),
                Err(status) => Err(status),
            },
            None => Err(NO_MEMORY),
        },
        ByteRequest::Stat { path } => match ops.stat(path).await {
            Ok(size) => return ByteOutcome::Size(size),
            Err(status) => Err(status),
        },
        ByteRequest::Exists { path } => match ops.exists(path).await {
            Ok(present) => return ByteOutcome::Exists(present),
            Err(status) => Err(status),
        },
        ByteRequest::DirExists { path } => match ops.dir_exists(path).await {
            Ok(present) => return ByteOutcome::Exists(present),
            Err(status) => Err(status),
        },
        ByteRequest::ReadInto { path, dst, cap } => {
            // SAFETY: by the caller's contract (`libdeluge/file_bytes.h`), `dst` points at `cap`
            // writable bytes that stay alive and untouched until its collect is terminal.
            let into = unsafe { core::slice::from_raw_parts_mut(*dst as *mut u8, *cap as usize) };
            return match ops.read_into(path, into).await {
                Ok(len) => ByteOutcome::Filled(len),
                Err((status, needed)) => ByteOutcome::Refused { status, needed },
            };
        }
        ByteRequest::WriteFrom {
            path,
            src,
            len,
            may_overwrite,
        } => {
            // SAFETY: as for `ReadInto`: `src` points at `len` readable bytes the caller leaves
            // alone until its collect is terminal.
            let bytes = unsafe { core::slice::from_raw_parts(*src as *const u8, *len as usize) };
            return match ops.write_new(path, bytes, !*may_overwrite).await {
                Ok(written) => ByteOutcome::Wrote(written),
                Err(status) => ByteOutcome::Refused { status, needed: 0 },
            };
        }
    };
    match result {
        Ok(()) => ByteOutcome::Done,
        Err(status) => ByteOutcome::Failed(status),
    }
}

// ── The live channel ────────────────────────────────────────────────────────

/// Wakes `file_bytes_task` when an operation is requested. Where the task is not built, signalling
/// it is harmless.
pub static BYTES_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// The one outstanding request, if any. A newer request never replaces it; see the module docs.
static REQUEST: Mutex<CriticalSectionRawMutex, Option<Queued>> = Mutex::new(None);

/// A request waiting for the task, with the card generation it was made under.
struct Queued {
    request: ByteRequest,
    generation: u32,
}

/// The address of the caller's buffer the running operation uses; 0 when there is none. Set under
/// the [`REQUEST`] lock the operation is taken from and cleared once the operation is done with the
/// buffer, so [`deluge_file_bytes_holds`] never sees a gap between queued and running. A flush does
/// not clear it: the running operation still uses the buffer.
static RUNNING_BUFFER: core::sync::atomic::AtomicUsize = core::sync::atomic::AtomicUsize::new(0);

/// The `(kind, key)` running right now, if any, so a collect answers PENDING rather than UNKNOWN
/// between a request leaving [`REQUEST`] and its answer reaching [`RESULT`].
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<(ByteOp, BytesPath)>> = Mutex::new(None);

/// The finished operation waiting to be collected. See [`BytesSlot`].
static RESULT: Mutex<CriticalSectionRawMutex, BytesSlot> = Mutex::new(BytesSlot::new());

/// A submitted write's bytes, from the submit until the task copies them into its work buffer.
/// Reached only through [`request_bytes`], which demands the [`REQUEST`] guard.
///
/// In `.sdram_bss` on device (NOLOAD, zeroed before any task runs) to keep it out of on-chip SRAM.
/// Only plain byte arrays go there: they are valid zeroed, while the `Mutex<Option<..>>` slots are
/// not, so those stay on-chip.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static mut REQUEST_BYTES: [u8; MAX_BYTES] = [0; MAX_BYTES];

/// The running operation's bytes: a write's copy, a read's result before it is published, or a
/// copy's current chunk. The task's alone (`claim_work_bytes`). Placed as [`REQUEST_BYTES`] is.
///
/// Separate from the request buffer because an eject clears the in-flight mark while the operation
/// may still be running, and a new write can then be admitted into the request buffer.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static mut WORK_BYTES: [u8; MAX_BYTES] = [0; MAX_BYTES];

/// A published read's bytes, paired with the entry in [`RESULT`]. Reached only through
/// [`result_bytes`], which demands the [`RESULT`] guard. Placed as [`REQUEST_BYTES`] is.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static mut RESULT_BYTES: [u8; MAX_BYTES] = [0; MAX_BYTES];

/// Returns the request buffer for as long as `guard` is borrowed.
///
/// Taking the [`REQUEST`] guard by `&mut` makes the borrow checker prove the lock is held for every
/// use of the bytes and that no two references to them are live at once.
fn request_bytes<'g>(
    _guard: &'g mut MutexGuard<'_, CriticalSectionRawMutex, Option<Queued>>,
) -> &'g mut [u8; MAX_BYTES] {
    // SAFETY: nothing else names REQUEST_BYTES, and a reference is handed out only while an
    // exclusive borrow of the REQUEST guard (the only `Option<Queued>` lock here) is held, so this
    // is the only live reference.
    unsafe { &mut *core::ptr::addr_of_mut!(REQUEST_BYTES) }
}

/// Returns the result buffer for as long as `guard` is borrowed. See [`request_bytes`].
fn result_bytes<'g>(
    _guard: &'g mut MutexGuard<'_, CriticalSectionRawMutex, BytesSlot>,
) -> &'g mut [u8; MAX_BYTES] {
    // SAFETY: as for `request_bytes`, with the RESULT guard, the only lock guarding a `BytesSlot`.
    unsafe { &mut *core::ptr::addr_of_mut!(RESULT_BYTES) }
}

/// Grants [`WORK_BYTES`] once.
#[cfg(has_app)]
static WORK_CLAIM: ClaimOnce = ClaimOnce::new();

/// Returns the work buffer to the first caller only. `file_bytes_task` claims it once, at start.
#[cfg(has_app)]
fn claim_work_bytes() -> Option<&'static mut [u8; MAX_BYTES]> {
    if !WORK_CLAIM.claim() {
        return None;
    }
    // SAFETY: WORK_CLAIM grants exactly one claim for the life of the program and nothing else
    // names WORK_BYTES, so this is the only reference that will ever exist.
    Some(unsafe { &mut *core::ptr::addr_of_mut!(WORK_BYTES) })
}

/// Drops what this channel holds for the card that is gone. Called by `deluge_card_flush` after it
/// bumps the card generation.
///
/// Runs on the interaction tier and must never wait, so every clear is `try_lock`ed and may not
/// land. That costs no correctness: the stamps are the safety property ([`admit_at_take`] for
/// requests, [`take_from`] for answers), and no answer here owns a resource.
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

/// Milliseconds `file_bytes_task` waits before each operation; see
/// [`deluge_file_bytes_set_latency_ms`].
#[cfg(feature = "host_app")]
static LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: makes every byte-channel operation wait `ms` milliseconds before it touches
/// the card, so a test can hold an operation in flight. 0, the default, means no wait.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_bytes_set_latency_ms(ms: u32) {
    LATENCY_MS.store(ms, Ordering::Relaxed);
}

/// Units of work the task has finished: one per operation published or dropped unrun, and one per
/// chunk a copy moved. Wraps.
static PROGRESS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Records one unit of finished work in [`PROGRESS`].
fn note_progress() {
    PROGRESS.fetch_add(1, Ordering::Release);
}

/// Returns a mark that moves whenever the channel finishes some work.
///
/// A caller refused or kept PENDING compares it with its previous tick's reading: a moving mark
/// means the channel is making progress; one that stands still for the caller's whole stall bound
/// means it has stopped. Compare for equality only; the mark wraps.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_bytes_progress() -> u32 {
    PROGRESS.load(Ordering::Acquire)
}

/// Milliseconds each chunk of a copy waits before it moves; see
/// [`deluge_file_bytes_set_chunk_latency_ms`].
#[cfg(feature = "host_app")]
static CHUNK_LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: makes each chunk a copy moves wait `ms` milliseconds first, so a test can
/// hold the channel with a long operation that keeps making progress. 0, the default, means no
/// wait.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_bytes_set_chunk_latency_ms(ms: u32) {
    CHUNK_LATENCY_MS.store(ms, Ordering::Relaxed);
}

/// Milliseconds each [`ByteOp::WriteFrom`] waits before it touches the card, on top of
/// [`LATENCY_MS`]; see [`deluge_file_bytes_set_write_from_latency_ms`].
#[cfg(feature = "host_app")]
static WRITE_FROM_LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: makes every document write (`write_from`) wait `ms` milliseconds before it
/// touches the card, and no other operation, so a test can hold a save's write past its stall
/// bound while the steps around it run at the card's own pace. 0, the default, means no wait.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_bytes_set_write_from_latency_ms(ms: u32) {
    WRITE_FROM_LATENCY_MS.store(ms, Ordering::Relaxed);
}

// ── The request entry points ────────────────────────────────────────────────

/// Requests a whole-file read of `path` (up to [`MAX_BYTES`]).
///
/// An unusable request (null, non-UTF-8, over-long, or a busy channel) is silently dropped: the
/// next collect reports PENDING if this read is in flight and UNKNOWN otherwise, and re-asking
/// covers it.
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
    let _ = enqueue_request(ByteRequest::Read { path }, &[]);
}

/// Requests that `len` bytes be written as the whole of `path`.
///
/// Returns `false` when refused, which the caller must act on: a dropped write is a lost change.
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
    // SAFETY: `bytes` points to at least `len` readable bytes by the caller's contract, and the
    // slice is copied into the request buffer before this returns.
    let src = unsafe { core::slice::from_raw_parts(bytes, len as usize) };
    enqueue_request(ByteRequest::Write { path, len }, src)
}

/// Requests that `from` be renamed to `to`. Returns `false` when refused.
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
    enqueue_request(ByteRequest::Rename { from, to }, &[])
}

/// Requests that `path` be deleted. Returns `false` when refused.
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
    enqueue_request(ByteRequest::Unlink { path }, &[])
}

/// Requests a file-existence check on `path`. A refused request is dropped, as for a read.
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
    let _ = enqueue_request(ByteRequest::Exists { path }, &[]);
}

/// Requests a directory-existence check on `path`. A refused request is dropped, as for a read.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_dir_exists(path: *const c_char) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return;
    };
    let _ = enqueue_request(ByteRequest::DirExists { path }, &[]);
}

/// Requests that `path` and every missing directory above it be created. Returns `false` when
/// refused.
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
    enqueue_request(ByteRequest::MkdirAll { path }, &[])
}

/// Requests that `path`'s modification time be set. Returns `false` when refused.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_set_time(
    path: *const c_char,
    timestamp: AbiTimestamp,
) -> bool {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return false;
    };
    let timestamp = FileTimestamp {
        year: timestamp.year,
        month: timestamp.month,
        day: timestamp.day,
        hour: timestamp.hour,
        minute: timestamp.minute,
        second: timestamp.second,
    };
    enqueue_request(ByteRequest::SetTime { path, timestamp }, &[])
}

/// Requests up to `len` (at most [`BLOCK_BYTES`]) bytes from `path` at `offset`. Returns `false`
/// when refused.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_read_at(
    path: *const c_char,
    offset: u32,
    len: u32,
) -> bool {
    if len as usize > BLOCK_BYTES {
        return false;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return false;
    };
    enqueue_request(ByteRequest::ReadAt { path, offset, len }, &[])
}

/// Requests that `len` (at most [`BLOCK_BYTES`]) bytes be written into `path` at `offset`. Returns
/// `false` when refused.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string, and `bytes` must point to at least `len`
/// readable bytes. Both are copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_write_at(
    path: *const c_char,
    offset: u32,
    bytes: *const u8,
    len: u32,
) -> bool {
    if bytes.is_null() || len as usize > BLOCK_BYTES {
        return false;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return false;
    };
    // SAFETY: `bytes` points to at least `len` readable bytes by the caller's contract, and the
    // slice is copied into the request buffer before this returns.
    let src = unsafe { core::slice::from_raw_parts(bytes, len as usize) };
    enqueue_request(ByteRequest::WriteAt { path, offset, len }, src)
}

/// Requests `path`'s size. A refused request is dropped, as for a read.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_stat(path: *const c_char) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return;
    };
    let _ = enqueue_request(ByteRequest::Stat { path }, &[]);
}

/// Requests that `from` be copied to `to`. Returns `false` when refused.
///
/// # Safety
///
/// `from` and `to` must be valid NUL-terminated C strings. Both are copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_copy(
    from: *const c_char,
    to: *const c_char,
) -> bool {
    // SAFETY: the caller's contract, above.
    let (Some(from), Some(to)) = (unsafe { cstr_to_path(from) }, unsafe { cstr_to_path(to) })
    else {
        return false;
    };
    enqueue_request(ByteRequest::Copy { from, to }, &[])
}

/// Requests that the whole of `path` be read into the caller's `cap`-byte buffer at `dst`. Returns
/// `false` when refused.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string; it is copied out before this returns. `dst` must
/// point at `cap` writable bytes that stay alive and untouched until the matching collect is
/// terminal and [`deluge_file_bytes_holds`] no longer names them.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_read_into(
    path: *const c_char,
    dst: *mut core::ffi::c_void,
    cap: u32,
) -> bool {
    if dst.is_null() || cap == 0 {
        return false;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return false;
    };
    enqueue_request(
        ByteRequest::ReadInto {
            path,
            dst: dst as usize,
            cap,
        },
        &[],
    )
}

/// Requests that `len` bytes at `src` be written as the whole of `path`; unless `may_overwrite`,
/// an existing file makes it fail with `EXISTS`. Returns `false` when refused.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string; it is copied out before this returns. `src` must
/// point at `len` readable bytes that stay alive and unmodified until the matching collect is
/// terminal and [`deluge_file_bytes_holds`] no longer names them.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_request_write_from(
    path: *const c_char,
    src: *const core::ffi::c_void,
    len: u32,
    may_overwrite: bool,
) -> bool {
    if src.is_null() || len == 0 {
        return false;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return false;
    };
    enqueue_request(
        ByteRequest::WriteFrom {
            path,
            src: src as usize,
            len,
            may_overwrite,
        },
        &[],
    )
}

/// Returns whether a queued or running request reads or writes through `buffer`.
///
/// A caller must not free or reuse a buffer handed to this channel while this returns true: the
/// operation cannot be cancelled. Returns true when it cannot tell (lock contended).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_bytes_holds(buffer: *const core::ffi::c_void) -> bool {
    let addr = buffer as usize;
    if addr == 0 {
        return false;
    }
    let queued = match REQUEST.try_lock() {
        Ok(request) => request
            .as_ref()
            .is_some_and(|q| q.request.caller_buffer() == addr),
        Err(_) => true,
    };
    queued || RUNNING_BUFFER.load(Ordering::Acquire) == addr
}

/// Installs the request and wakes the driver, or returns `false` if the channel is busy.
///
/// Called synchronously from C++ and must never wait, so every lock is `try_lock`ed; contention
/// refuses, the safe direction (a refused caller retries, a falsely accepted one waits forever).
///
/// A queued or in-flight operation always makes the channel busy. An uncollected answer does not,
/// or a caller that gave up mid-poll would wedge the channel for everyone: a publish displaces it,
/// and its owner reads UNKNOWN and asks again. The exception is a [`non_idempotent`] answer stamped
/// for this card, whose rerun would misreport: it refuses new requests for up to
/// [`PROTECTED_ANSWER_SPAN`].
///
/// `payload` (empty for every kind but the two buffered writes) is copied into the request buffer
/// in the same lock hold that stores the request, so the task never takes a request without its
/// bytes.
fn enqueue_request(request: ByteRequest, payload: &[u8]) -> bool {
    let Ok(mut slot) = REQUEST.try_lock() else {
        return false; // Contended; the caller retries.
    };
    let queued = slot.as_ref().map(|q| q.request.key().0);
    if !accepts(queued, request.key().0) || !in_flight_idle() {
        return false;
    }
    let generation = crate::storage::card::generation();
    // Held by an uncollected answer whose rerun would not be harmless, or contended: refused, and
    // retried.
    let admitted = RESULT
        .try_lock()
        .is_ok_and(|mut result| result.admits_new_request(generation, Instant::now()));
    if !admitted {
        return false;
    }
    let Some(into) = request_bytes(&mut slot).get_mut(..payload.len()) else {
        return false; // Cannot happen: every submit bounds its payload by MAX_BYTES.
    };
    into.copy_from_slice(payload);
    *slot = Some(Queued {
        request,
        generation,
    });
    drop(slot);
    BYTES_WAKE.signal(());
    true
}

/// Whether nothing is running right now. Contention reports not idle, so the worst case is a retry.
fn in_flight_idle() -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight.is_none(),
        Err(_) => false,
    }
}

// ── The collect entry points ────────────────────────────────────────────────

/// Collects the bytes read for `path`, if they are ready.
///
/// `out_buf` and `out_len` are written only on [`DELUGE_CHANNEL_READY`], `out_status` only on
/// [`DELUGE_CHANNEL_FAILED`]. A file larger than `buf_cap` is FAILED with `NO_MEMORY`, never
/// truncated.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string; `out_buf` must point to at least `buf_cap`
/// writable bytes; `out_len` and `out_status` must point to writable objects of their types.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_read(
    path: *const c_char,
    out_buf: *mut u8,
    buf_cap: u32,
    out_len: *mut u32,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    if out_buf.is_null() || out_len.is_null() || out_status.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    // SAFETY: non-null (checked above), and `out_buf` has at least `buf_cap` writable bytes by the
    // caller's contract. Written only through `copy_out`, and only when the whole answer fits.
    let dst = unsafe { core::slice::from_raw_parts_mut(out_buf, buf_cap as usize) };
    let probed = probe_with(ByteOp::Read, &path, |outcome, bytes| {
        let copied = match &outcome {
            ByteOutcome::Bytes(len) => Some(copy_out(bytes, *len, dst)),
            _ => None,
        };
        (outcome, copied)
    });
    match probed {
        Probe::Ready((ByteOutcome::Bytes(_), Some(CopyOut::Copied(len)))) => {
            // SAFETY: non-null (checked above); written only on READY, as the header promises.
            unsafe { *out_len = len };
            DELUGE_CHANNEL_READY
        }
        Probe::Ready((ByteOutcome::Bytes(_), Some(CopyOut::TooBig))) => {
            // Terminal: the take removed the answer, so PENDING would poll forever, and a
            // truncated read would parse with the remaining settings silently reset.
            //
            // SAFETY: non-null (checked above); written only on FAILED, as the header promises.
            unsafe { *out_status = NO_MEMORY };
            DELUGE_CHANNEL_FAILED
        }
        // An answer longer than its own buffer, which no producer can publish. UNKNOWN rather
        // than a panic on the interaction tier.
        Probe::Ready((ByteOutcome::Bytes(_), _)) => DELUGE_CHANNEL_UNKNOWN,
        // Unreachable: `bytes_io_once` produces only `Bytes` or `Failed` for a read. UNKNOWN rather
        // than an assert, so the caller never sees READY with an unwritten length.
        Probe::Ready((ByteOutcome::Done, _))
        | Probe::Ready((ByteOutcome::Exists(_), _))
        | Probe::Ready((ByteOutcome::Block(_), _))
        | Probe::Ready((ByteOutcome::Size(_), _))
        | Probe::Ready((ByteOutcome::Wrote(_), _))
        | Probe::Ready((ByteOutcome::Filled(_), _))
        | Probe::Ready((ByteOutcome::Refused { .. }, _)) => DELUGE_CHANNEL_UNKNOWN,
        Probe::Ready((ByteOutcome::Failed(status), _)) => {
            // SAFETY: as above, and written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_CHANNEL_FAILED
        }
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// Collects the outcome of the write to `path`, if it has finished.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written only on [`DELUGE_CHANNEL_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_write(
    path: *const c_char,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::Write, path, out_status) }
}

/// Collects the outcome of the rename to `to`, if it has finished.
///
/// # Safety
///
/// `to` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written only on [`DELUGE_CHANNEL_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_rename(
    to: *const c_char,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::Rename, to, out_status) }
}

/// Collects the outcome of the deletion of `path`, if it has finished.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written only on [`DELUGE_CHANNEL_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_unlink(
    path: *const c_char,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::Unlink, path, out_status) }
}

/// Collects the file-existence answer for `path`, if there is one.
///
/// READY with `false` is the answer "no"; FAILED means the question could not be answered.
/// `out_present` is written only on [`DELUGE_CHANNEL_READY`], `out_status` only on
/// [`DELUGE_CHANNEL_FAILED`].
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string; `out_present` must point to a writable `bool`
/// and `out_status` to a writable `i8`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_exists(
    path: *const c_char,
    out_present: *mut bool,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    unsafe { collect_presence(ByteOp::Exists, path, out_present, out_status) }
}

/// Collects the directory-existence answer for `path`, if there is one, as
/// [`deluge_file_bytes_collect_exists`] does.
///
/// # Safety
///
/// As [`deluge_file_bytes_collect_exists`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_dir_exists(
    path: *const c_char,
    out_present: *mut bool,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    unsafe { collect_presence(ByteOp::DirExists, path, out_present, out_status) }
}

/// The body both existence collects share: `op`'s three-state answer for `path`.
///
/// # Safety
///
/// As [`deluge_file_bytes_collect_exists`].
unsafe fn collect_presence(
    op: ByteOp,
    path: *const c_char,
    out_present: *mut bool,
    out_status: *mut i8,
) -> u8 {
    if out_present.is_null() || out_status.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    match probe(op, &path) {
        Probe::Ready(ByteOutcome::Exists(present)) => {
            // SAFETY: non-null (checked above); written only on READY, as the header promises.
            unsafe { *out_present = present };
            DELUGE_CHANNEL_READY
        }
        Probe::Ready(ByteOutcome::Failed(status)) => {
            // SAFETY: as above, and written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_CHANNEL_FAILED
        }
        // Unreachable: `bytes_io_once` produces only `Exists` or `Failed` here. UNKNOWN rather than
        // an assert, so the caller never sees READY with `out_present` unwritten.
        Probe::Ready(ByteOutcome::Bytes(_))
        | Probe::Ready(ByteOutcome::Done)
        | Probe::Ready(ByteOutcome::Block(_))
        | Probe::Ready(ByteOutcome::Size(_))
        | Probe::Ready(ByteOutcome::Wrote(_))
        | Probe::Ready(ByteOutcome::Filled(_))
        | Probe::Ready(ByteOutcome::Refused { .. }) => DELUGE_CHANNEL_UNKNOWN,
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// Collects the outcome of the directory creation for `path`, if it has finished.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written only on [`DELUGE_CHANNEL_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_mkdir_all(
    path: *const c_char,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::MkdirAll, path, out_status) }
}

/// Collects the outcome of the timestamp write for `path`, if it has finished.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`,
/// written only on [`DELUGE_CHANNEL_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_set_time(
    path: *const c_char,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::SetTime, path, out_status) }
}

/// Collects the bytes read at an offset from `path`, if they are ready. A short count means
/// end-of-file.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string; `out_buf` must point to at least `buf_cap`
/// writable bytes; `out_len` and `out_status` must point to writable objects of their types.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_read_at(
    path: *const c_char,
    out_buf: *mut u8,
    buf_cap: u32,
    out_len: *mut u32,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    if out_buf.is_null() || out_len.is_null() || out_status.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    // SAFETY: non-null (checked above), and `out_buf` has at least `buf_cap` writable bytes by the
    // caller's contract. Written only through `copy_out`, and only when the whole block fits.
    let dst = unsafe { core::slice::from_raw_parts_mut(out_buf, buf_cap as usize) };
    let probed = probe_with(ByteOp::ReadAt, &path, |outcome, bytes| {
        let copied = match &outcome {
            ByteOutcome::Block(len) => Some(copy_out(bytes, *len, dst)),
            _ => None,
        };
        (outcome, copied)
    });
    match probed {
        Probe::Ready((ByteOutcome::Block(_), Some(CopyOut::Copied(len)))) => {
            // SAFETY: non-null (checked above); written only on READY.
            unsafe { *out_len = len };
            DELUGE_CHANNEL_READY
        }
        Probe::Ready((ByteOutcome::Block(_), Some(CopyOut::TooBig))) => {
            // SAFETY: non-null (checked above); written only on FAILED.
            unsafe { *out_status = NO_MEMORY };
            DELUGE_CHANNEL_FAILED
        }
        Probe::Ready((ByteOutcome::Failed(status), _)) => {
            // SAFETY: as above, and written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_CHANNEL_FAILED
        }
        // `CopyOut::Invalid` and every outcome that cannot be filed under ReadAt.
        Probe::Ready(_) => DELUGE_CHANNEL_UNKNOWN,
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// Collects the outcome of the offset-addressed write to `path`, if it has finished.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string; `out_written` and `out_status` must point to
/// writable objects of their types.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_write_at(
    path: *const c_char,
    out_written: *mut u32,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    if out_written.is_null() || out_status.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    match probe(ByteOp::WriteAt, &path) {
        Probe::Ready(ByteOutcome::Wrote(written)) => {
            // SAFETY: non-null (checked above); written only on READY.
            unsafe { *out_written = written };
            DELUGE_CHANNEL_READY
        }
        Probe::Ready(ByteOutcome::Failed(status)) => {
            // SAFETY: as above, and written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_CHANNEL_FAILED
        }
        Probe::Ready(_) => DELUGE_CHANNEL_UNKNOWN,
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// Collects `path`'s size, if it is known.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string; `out_size` and `out_status` must point to
/// writable objects of their types.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_stat(
    path: *const c_char,
    out_size: *mut u32,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    if out_size.is_null() || out_status.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    match probe(ByteOp::Stat, &path) {
        Probe::Ready(ByteOutcome::Size(size)) => {
            // SAFETY: non-null (checked above); written only on READY.
            unsafe { *out_size = size };
            DELUGE_CHANNEL_READY
        }
        Probe::Ready(ByteOutcome::Failed(status)) => {
            // SAFETY: as above, and written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_CHANNEL_FAILED
        }
        Probe::Ready(_) => DELUGE_CHANNEL_UNKNOWN,
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// Collects the outcome of the copy to `to`, if it has finished.
///
/// # Safety
///
/// `to` must be a valid NUL-terminated C string and `out_status` must point to a writable `i8`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_copy(
    to: *const c_char,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    unsafe { collect_completion(ByteOp::Copy, to, out_status) }
}

/// Collects the whole-file read of `path` into the caller's buffer, if it has finished.
///
/// `out_len` is written only on [`DELUGE_CHANNEL_READY`]; `out_error` and `out_needed` (the file's
/// size when it did not fit, else 0) only on [`DELUGE_CHANNEL_FAILED`].
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. Each out-pointer may be null; one that is not
/// must point at a writable object of its type.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_read_into(
    path: *const c_char,
    out_len: *mut u32,
    out_error: *mut i8,
    out_needed: *mut u32,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    match probe(ByteOp::ReadInto, &path) {
        Probe::Ready(ByteOutcome::Filled(len)) => {
            // SAFETY: the caller's contract, above.
            unsafe { write_out(out_len, len) };
            DELUGE_CHANNEL_READY
        }
        Probe::Ready(ByteOutcome::Refused { status, needed }) => {
            // SAFETY: the caller's contract, above.
            unsafe {
                write_out(out_error, status);
                write_out(out_needed, needed);
            }
            DELUGE_CHANNEL_FAILED
        }
        // Unreachable: `bytes_io_once` produces only `Filled` or `Refused` for `ReadInto`.
        Probe::Ready(_) => DELUGE_CHANNEL_UNKNOWN,
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// Collects the whole-file write from the caller's buffer to `path`, if it has finished.
///
/// `out_written` is written only on [`DELUGE_CHANNEL_READY`], `out_error` only on
/// [`DELUGE_CHANNEL_FAILED`].
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string. Each out-pointer may be null; one that is not
/// must point at a writable object of its type.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_bytes_collect_write_from(
    path: *const c_char,
    out_written: *mut u32,
    out_error: *mut i8,
) -> DelugeChannelStatus {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    match probe(ByteOp::WriteFrom, &path) {
        Probe::Ready(ByteOutcome::Wrote(written)) => {
            // SAFETY: the caller's contract, above.
            unsafe { write_out(out_written, written) };
            DELUGE_CHANNEL_READY
        }
        Probe::Ready(ByteOutcome::Refused { status, .. }) => {
            // SAFETY: the caller's contract, above.
            unsafe { write_out(out_error, status) };
            DELUGE_CHANNEL_FAILED
        }
        // Unreachable: `bytes_io_once` produces only `Wrote` or `Refused` for `WriteFrom`.
        Probe::Ready(_) => DELUGE_CHANNEL_UNKNOWN,
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// Stores `value` through `out` unless it is null.
///
/// # Safety
///
/// A non-null `out` must point at a writable `T`.
unsafe fn write_out<T>(out: *mut T, value: T) {
    if !out.is_null() {
        // SAFETY: non-null, and the caller guarantees it is writable.
        unsafe { *out = value };
    }
}

/// The shared body of the completion collects, which differ only in the [`ByteOp`] they key on.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string and `out_status` a writable `i8`.
unsafe fn collect_completion(op: ByteOp, path: *const c_char, out_status: *mut i8) -> u8 {
    if out_status.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    match probe(op, &path) {
        Probe::Ready(ByteOutcome::Done) => DELUGE_CHANNEL_READY,
        Probe::Ready(ByteOutcome::Failed(status)) => {
            // SAFETY: non-null (checked above); written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_CHANNEL_FAILED
        }
        // Unreachable for a completion op; UNKNOWN rather than an assert.
        Probe::Ready(ByteOutcome::Bytes(_))
        | Probe::Ready(ByteOutcome::Exists(_))
        | Probe::Ready(ByteOutcome::Block(_))
        | Probe::Ready(ByteOutcome::Size(_))
        | Probe::Ready(ByteOutcome::Wrote(_))
        | Probe::Ready(ByteOutcome::Filled(_))
        | Probe::Ready(ByteOutcome::Refused { .. }) => DELUGE_CHANNEL_UNKNOWN,
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// What one forward pass over the channel found.
///
/// One pass, never separate "ready?" and "still coming?" passes: the driver can publish and clear
/// its in-flight mark between two passes, and the second would report UNKNOWN for work that landed.
enum Probe<R> {
    /// What the caller's `ready` closure made of the taken outcome, run with the `RESULT` guard
    /// held.
    Ready(R),
    Pending,
    Unknown,
}

/// Probes [`REQUEST`], then [`IN_FLIGHT`], then [`RESULT`], and runs `ready` on a taken answer.
///
/// An entry only moves forward through those three, and three rules make a forward pass unable to
/// miss it:
///
/// 1. `take_next_request` sets `IN_FLIGHT` while still holding the `REQUEST` lock.
/// 2. The driver publishes to `RESULT` before clearing `IN_FLIGHT`, so that handoff overlaps.
/// 3. [`is_requested`] and [`is_in_flight`] report `true` when their lock is contended: the lock,
///    not the table, covers the window in which the entry is in neither.
///
/// Probing in reverse order could report UNKNOWN for an operation that just landed, and the caller
/// would run it again.
fn probe_with<R>(
    op: ByteOp,
    path: &str,
    ready: impl FnOnce(ByteOutcome, &[u8; MAX_BYTES]) -> R,
) -> Probe<R> {
    if is_requested(op, path) || is_in_flight(op, path) {
        return Probe::Pending;
    }
    // Contended: the driver is mid-publish, so this is PENDING, never UNKNOWN.
    let Ok(mut result) = RESULT.try_lock() else {
        return Probe::Pending;
    };
    let held_someone_off = result.has_refused();
    // The generation is read here, at the take: the collector's view of the card now in the slot,
    // which decides an answer's fate whether or not the flush managed to take this lock.
    match take_from(&mut result, op, path, crate::storage::card::generation()) {
        // `ready` runs with the guard held, so it reads the bytes published with this entry.
        ByteTake::Ready(outcome) => {
            let probed = Probe::Ready(ready(outcome, result_bytes(&mut result)));
            drop(result);
            if held_someone_off {
                // The answer refused another caller, which now waits for a wake; the channel is
                // free once this returns.
                crate::app_loop::wake(crate::app_loop::WAKE_STORAGE);
            }
            probed
        }
        ByteTake::Absent => Probe::Unknown,
    }
}

/// [`probe_with`] for the collects that carry no bytes: just the outcome.
fn probe(op: ByteOp, path: &str) -> Probe<ByteOutcome> {
    probe_with(op, path, |outcome, _| outcome)
}

/// Whether `(op, path)` is the request not yet picked up.
///
/// Contention reports `true`, and that is load-bearing: the driver marks a request in flight while
/// holding this lock, so a contended lock may be a request mid-handoff. Reporting `false` would
/// make the collect answer UNKNOWN for it.
fn is_requested(op: ByteOp, path: &str) -> bool {
    match REQUEST.try_lock() {
        Ok(request) => request
            .as_ref()
            .is_some_and(|q| q.request.key() == (op, path)),
        Err(_) => true,
    }
}

/// Whether `(op, path)` is the operation running right now. Contention reports `true`, as for
/// [`is_requested`].
fn is_in_flight(op: ByteOp, path: &str) -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight
            .as_ref()
            .is_some_and(|(k, p)| *k == op && p.as_str() == path),
        Err(_) => true,
    }
}

/// Copies a C string into a [`BytesPath`]. `None` for null, non-UTF-8, or an over-long path, which
/// is refused rather than truncated: a truncated path names a different file, which might then be
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
// Compiled only where efatfs is linked: on device, and on host under `host_app`.

#[cfg(has_app)]
use crate::storage::efatfs::backend as backing;

/// The real [`ByteOps`], over efatfs's task-context file table. Stateless: every operation is keyed
/// by path.
#[cfg(has_app)]
pub struct ProdByteOps;

#[cfg(has_app)]
impl ProdByteOps {
    /// The production ops.
    pub fn new() -> Self {
        Self
    }

    /// Opens `path`, mounting first if the open fails with `NO_FILESYSTEM`.
    ///
    /// Mounting is card I/O, so it happens here on the storage tier rather than in the interaction
    /// tier's callers. `mount()` bumps `deluge_efatfs_mount_generation`, which is how the C++ side
    /// learns to run `firstCardRead()`.
    async fn open_mounted(path: &str, mode: u8) -> Result<u32, i8> {
        match backing::task_file_open(path, mode).await {
            Err(NO_FILESYSTEM) => {
                backing::mount().await.map_err(|_| NO_FILESYSTEM)?;
                backing::task_file_open(path, mode)
                    .await
                    .map_err(|status| status as i8)
            }
            other => other.map_err(|status| status as i8),
        }
    }

    /// Opens `path` as a directory, mounting first as [`Self::open_mounted`] does.
    async fn open_dir_mounted(path: &str) -> Result<u32, i8> {
        match backing::task_dir_open(path).await {
            Err(NO_FILESYSTEM) => {
                backing::mount().await.map_err(|_| NO_FILESYSTEM)?;
                backing::task_dir_open(path)
                    .await
                    .map_err(|status| status as i8)
            }
            other => other.map_err(|status| status as i8),
        }
    }

    /// Creates one directory, mounting first if needed. `EXISTS` is success: it is the commonest
    /// outcome for every segment but the last of a walk.
    async fn mkdir_one(path: &str) -> Result<(), i8> {
        match backing::task_mkdir(path).await {
            Ok(()) => Ok(()),
            Err(EXISTS) => Ok(()),
            Err(NO_FILESYSTEM) => {
                // Not mounted: mount lazily, as `open_mounted` does.
                backing::mount().await.map_err(|_| NO_FILESYSTEM)?;
                match backing::task_mkdir(path).await {
                    Ok(()) => Ok(()),
                    Err(EXISTS) => Ok(()),
                    Err(status) => Err(status as i8),
                }
            }
            Err(status) => Err(status as i8),
        }
    }

    /// Returns the parent directory of `path`, or `None` for something at the card root.
    fn parent_of(path: &str) -> Option<&str> {
        path.rfind('/').map(|cut| &path[..cut])
    }
}

#[cfg(has_app)]
impl ByteOps for ProdByteOps {
    async fn read_all(&self, path: &str, into: &mut [u8; MAX_BYTES]) -> Result<usize, i8> {
        let handle = Self::open_mounted(path, OPEN_READ).await?;
        // Every path must reach the close (a leaked handle costs a file-table slot for the rest of
        // the mount), so the result is bound rather than returned with `?`.
        let result = async {
            let size = backing::task_file_size(handle)
                .await
                .map_err(|status| status as i8)?;
            if size as usize > MAX_BYTES {
                // Never a truncated prefix: it would parse with the remaining settings reset.
                return Err(NO_MEMORY);
            }
            let read = backing::task_file_read_exact(handle, &mut into[..size as usize])
                .await
                .map_err(|status| status as i8)?;
            // Short of the size just read means the read stopped early: a failure.
            if read != size as usize {
                return Err(ERR_IO);
            }
            Ok(read)
        }
        .await;
        backing::task_file_close(handle).await;
        result
    }

    async fn write_all(&self, path: &str, bytes: &[u8]) -> Result<(), i8> {
        if let Some(parent) = Self::parent_of(path) {
            // Result ignored: EXISTS is the usual outcome, and any real failure surfaces as the
            // open failing.
            let _ = backing::task_mkdir(parent).await;
        }
        let handle = Self::open_mounted(path, OPEN_WRITE_CREATE).await?;
        let result = async {
            // Truncate first so a shorter file never keeps an older, longer file's tail. The open
            // mode's own truncation is not relied on.
            backing::task_file_truncate(handle, 0)
                .await
                .map_err(|status| status as i8)?;
            let written = backing::task_file_write(handle, bytes)
                .await
                .map_err(|status| status as i8)?;
            // A short write is a failure: a partial settings file is corrupt but would still parse.
            if written != bytes.len() {
                return Err(ERR_IO);
            }
            Ok(())
        }
        .await;
        // The close is what flushes; skipping it would leave the write unflushed and leak the slot.
        backing::task_file_close(handle).await;
        result
    }

    async fn unlink(&self, path: &str) -> Result<(), i8> {
        backing::task_unlink(path)
            .await
            .map_err(|status| status as i8)
    }

    async fn exists(&self, path: &str) -> Result<bool, i8> {
        // Open for READ, as `deluge::io::presence_from_open` does in C++. NOT_FOUND is the answer
        // "no"; every other failure is "could not tell", and the two must stay apart.
        //
        // Not a read of the bytes: this answers for song files far above `MAX_BYTES`, and a
        // `NO_MEMORY` collapsed into "absent" would get an existing song overwritten.
        match ProdByteOps::open_mounted(path, OPEN_READ).await {
            Ok(handle) => {
                backing::task_file_close(handle).await;
                Ok(true)
            }
            Err(NOT_FOUND) => Ok(false),
            Err(status) => Err(status),
        }
    }

    async fn dir_exists(&self, path: &str) -> Result<bool, i8> {
        // Open and close at once, so none of the directory table's eight handles is held past the
        // answer. NOT_FOUND is "no"; every other failure is "could not tell", as for `exists`.
        match ProdByteOps::open_dir_mounted(path).await {
            Ok(handle) => {
                backing::task_dir_close(handle).await;
                Ok(true)
            }
            Err(NOT_FOUND) => Ok(false),
            Err(status) => Err(status),
        }
    }

    async fn mkdir_all(&self, path: &str) -> Result<(), i8> {
        // Empty segments (leading or repeated separators) are skipped, not passed to mkdir.
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

    async fn read_at(&self, path: &str, offset: u32, into: &mut [u8]) -> Result<usize, i8> {
        let handle = Self::open_mounted(path, OPEN_READ).await?;
        let result = async {
            backing::task_file_seek(handle, offset)
                .await
                .map_err(|status| status as i8)?;
            // A short read is end-of-file, not a failure: the SysEx file reader detects EOF this
            // way.
            backing::task_file_read_exact(handle, into)
                .await
                .map_err(|status| status as i8)
        }
        .await;
        backing::task_file_close(handle).await;
        result
    }

    async fn write_at(&self, path: &str, offset: u32, bytes: &[u8]) -> Result<u32, i8> {
        // OPEN_READ on purpose: it maps to efatfs's `open_context`, which returns a read-write
        // handle without truncating. OPEN_WRITE_CREATE maps to `create_context`, which always
        // truncates, so a multi-block transfer would keep only its last block. Do not "fix" this.
        let handle = match Self::open_mounted(path, OPEN_READ).await {
            Ok(handle) => handle,
            Err(NOT_FOUND) => {
                // Absent: create it once (truncating an absent file is harmless), then reopen.
                let created = Self::open_mounted(path, OPEN_WRITE_CREATE).await?;
                backing::task_file_close(created).await;
                Self::open_mounted(path, OPEN_READ).await?
            }
            Err(status) => return Err(status),
        };
        let result = async {
            backing::task_file_seek(handle, offset)
                .await
                .map_err(|status| status as i8)?;
            backing::task_file_write(handle, bytes)
                .await
                .map_err(|status| status as i8)
                .map(|written| written as u32)
        }
        .await;
        // The close is what flushes.
        backing::task_file_close(handle).await;
        result
    }

    async fn stat(&self, path: &str) -> Result<u32, i8> {
        let handle = Self::open_mounted(path, OPEN_READ).await?;
        let result = backing::task_file_size(handle)
            .await
            .map_err(|status| status as i8);
        backing::task_file_close(handle).await;
        result
    }

    async fn copy_file(&self, from: &str, to: &str, scratch: &mut [u8]) -> Result<(), i8> {
        if let Some(parent) = Self::parent_of(to) {
            let _ = backing::task_mkdir(parent).await;
        }
        // Create or truncate the destination first, so a copy onto a longer file leaves no tail.
        // The one place here that wants `create_context`'s truncation.
        let dst_init = Self::open_mounted(to, OPEN_WRITE_CREATE).await?;
        backing::task_file_close(dst_init).await;

        let src = Self::open_mounted(from, OPEN_READ).await?;
        let result = async {
            let chunk_len = BLOCK_BYTES.min(scratch.len());
            let mut offset: u32 = 0;
            loop {
                let chunk = &mut scratch[..chunk_len];
                backing::task_file_seek(src, offset)
                    .await
                    .map_err(|status| status as i8)?;
                let read = backing::task_file_read_exact(src, chunk)
                    .await
                    .map_err(|status| status as i8)?;
                if read == 0 {
                    return Ok(());
                }
                #[cfg(feature = "host_app")]
                {
                    let ms = CHUNK_LATENCY_MS.load(Ordering::Relaxed);
                    if ms > 0 {
                        embassy_time::Timer::after_millis(u64::from(ms)).await;
                    }
                }
                // Non-truncating open, as in `write_at`.
                let dst = Self::open_mounted(to, OPEN_READ).await?;
                let written = async {
                    backing::task_file_seek(dst, offset)
                        .await
                        .map_err(|status| status as i8)?;
                    backing::task_file_write(dst, &chunk[..read])
                        .await
                        .map_err(|status| status as i8)
                }
                .await;
                backing::task_file_close(dst).await;
                if written? != read {
                    return Err(ERR_IO);
                }
                offset += read as u32;
                note_progress();
            }
        }
        .await;
        backing::task_file_close(src).await;
        result
    }

    async fn read_into(&self, path: &str, into: &mut [u8]) -> Result<u32, (i8, u32)> {
        let handle = match crate::channel::file_stream::open_mounted(path, STREAM_READ).await {
            Ok(handle) => handle,
            // NO_MEMORY means "did not fit" here, and the caller resizes on it, so an open that
            // failed for want of memory is reported as a card error instead.
            Err(NO_MEMORY) => return Err((ERR_IO, 0)),
            // The open's own status: NOT_FOUND for a missing document, and so on.
            Err(status) => return Err((status, 0)),
        };
        let size = backing::stream_size(handle).await.unwrap_or(0);
        if size == 0 || size as usize > into.len() {
            // Refuse rather than truncate, reporting the size needed. Close first: a leaked handle
            // would starve the stream table's eight slots.
            backing::stream_close(handle).await;
            return Err(if size == 0 {
                (ERR_IO, 0)
            } else {
                (NO_MEMORY, size)
            });
        }
        let read = backing::stream_read_at_via(handle, 0, &mut into[..size as usize]).await;
        backing::stream_close(handle).await;
        match read {
            // A short read is a failure: the parser would be handed a truncated document.
            Some(n) if n as u32 == size => Ok(size),
            _ => Err((ERR_IO, 0)),
        }
    }

    async fn write_new(&self, path: &str, bytes: &[u8], exclusive: bool) -> Result<u32, i8> {
        // The open carries the overwrite policy, so the existence check cannot race the create.
        let mode = if exclusive {
            STREAM_CREATE_NEW
        } else {
            STREAM_CREATE
        };
        // On failure, the open's own status (EXISTS for an exclusive create that found the file).
        let handle = crate::channel::file_stream::open_mounted(path, mode).await?;
        let written = backing::stream_write_at(handle, 0, bytes).await;
        backing::stream_close(handle).await;
        match written {
            // A short write is a failure: a truncated document is worse than none.
            Some(n) if n == bytes.len() => Ok(n as u32),
            _ => Err(ERR_IO),
        }
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

/// The byte channel's driver on the storage tier: wakes on [`BYTES_WAKE`], runs the outstanding
/// operation and publishes its answer.
///
/// `IN_FLIGHT` is set before the operation and cleared only after the answer is published, so a
/// running operation never looks unknown. A failure is published too: publishing nothing would
/// read as UNKNOWN ("ask again"), and the caller would retry a failing read forever.
#[cfg(has_app)]
#[embassy_executor::task]
pub async fn file_bytes_task() {
    crate::tier::scoped(crate::tier::Tier::Storage, file_bytes_task_body()).await
}

#[cfg(has_app)]
async fn file_bytes_task_body() {
    let Some(work) = claim_work_bytes() else {
        debug_assert!(false, "the byte channel's work buffer was claimed twice");
        return;
    };
    BYTES_WAKE.wait().await;
    let ops = ProdByteOps::new();
    loop {
        while let Some(taken) = take_next_request(work).await {
            let Some((request, generation)) = taken else {
                // Dropped unrun (an earlier card's request); IN_FLIGHT was never set for it, but
                // `mark_in_flight(None)` still wakes the app for this drop.
                mark_in_flight(None).await;
                note_progress();
                continue;
            };
            // After the take, so a collect during the wait answers PENDING, as for a slow card.
            #[cfg(feature = "host_app")]
            {
                let mut ms = LATENCY_MS.load(Ordering::Relaxed);
                if request.key().0 == ByteOp::WriteFrom {
                    ms = ms.saturating_add(WRITE_FROM_LATENCY_MS.load(Ordering::Relaxed));
                }
                if ms > 0 {
                    embassy_time::Timer::after_millis(u64::from(ms)).await;
                }
            }
            let outcome = bytes_io_once(&ops, &request, work).await;
            // The operation has finished with the caller's buffer, if it had one.
            RUNNING_BUFFER.store(0, Ordering::Release);
            // Publish before clearing IN_FLIGHT: briefly in both is harmless, in neither would read
            // as UNKNOWN. `generation` was sampled before the operation, so a card pulled during it
            // makes this publish a no-op.
            let (op, key) = request.key();
            publish_result(op, key, outcome, generation, work).await;
            mark_in_flight(None).await;
            note_progress();
        }
        BYTES_WAKE.wait().await;
    }
}

/// Takes the outstanding request, marks it in flight, and returns it with the card generation the
/// operation begins under. `None` when nothing is queued; `Some(None)` when the request was asked
/// under a card that has gone and was dropped unrun.
///
/// The mark happens while the [`REQUEST`] lock is still held, so a racing collect sees the request
/// queued or meets a contended lock (PENDING), never neither (UNKNOWN).
///
/// The generation is read here, before any I/O, so it names the card the operation touches.
/// Reading it at publish would see a flush's new value and defeat the check.
#[cfg(has_app)]
async fn take_next_request(work: &mut [u8; MAX_BYTES]) -> Option<Option<(ByteRequest, u32)>> {
    let mut slot = REQUEST.lock().await;
    let Queued {
        request,
        generation: asked_under,
    } = slot.take()?;
    // A request from an earlier card that survived the flush's `try_lock`.
    let Some(buffer) = admit_at_take(&request, asked_under, crate::storage::card::generation())
    else {
        return Some(None);
    };
    RUNNING_BUFFER.store(buffer, Ordering::Release);
    if let Some(len) = request.payload_len() {
        // Copied in the same lock hold as the take: once it is released, an eject may clear the
        // in-flight mark and admit a new write into the request buffer.
        let len = (len as usize).min(MAX_BYTES);
        work[..len].copy_from_slice(&request_bytes(&mut slot)[..len]);
    }
    let generation = crate::storage::card::generation();
    mark_in_flight(Some(request.key_owned())).await;
    Some(Some((request, generation)))
}

/// Records (or clears) the operation running right now. The task sets it while still holding the
/// `REQUEST` lock. Clearing it wakes the app (`DELUGE_WAKE_STORAGE`): the operation's result, or
/// its absence, is visible once this returns.
///
/// Public so the host tests, which do not run the task, can drive the collects' in-flight branch.
pub async fn mark_in_flight(entry: Option<(ByteOp, BytesPath)>) {
    let done = entry.is_none();
    *IN_FLIGHT.lock().await = entry;
    if done {
        crate::app_loop::wake(crate::app_loop::WAKE_STORAGE);
    }
}

/// Publishes one finished operation into `RESULT`, stamped with the card generation it began
/// under, unless that card is already gone.
///
/// The early drop is an optimisation, not the guarantee: with no happens-before edge from
/// `deluge_card_flush`'s bump, the check can read the old generation and publish; the entry still
/// carries the old stamp and [`take_from`] drops it. The check runs under the lock so it is at
/// least ordered against a flush that took it.
///
/// A `Bytes` or `Block` answer's bytes are copied into the result buffer in the same lock hold as
/// the entry, so no collect can pair an entry with another answer's bytes.
pub async fn publish_result(
    op: ByteOp,
    key: &str,
    outcome: ByteOutcome,
    generation: u32,
    work: &[u8; MAX_BYTES],
) {
    let mut result = RESULT.lock().await;
    if generation != crate::storage::card::generation() {
        // The card this ran against is gone. A failure is dropped too: it belongs to the old card.
        return;
    }
    if let ByteOutcome::Bytes(len) | ByteOutcome::Block(len) = outcome {
        let len = (len as usize).min(MAX_BYTES);
        result_bytes(&mut result)[..len].copy_from_slice(&work[..len]);
    }
    publish_into(&mut result, op, key, outcome, generation);
}

abi_pin!(
    deluge_file_bytes_collect_copy,
    deluge_file_bytes_collect_dir_exists,
    deluge_file_bytes_collect_exists,
    deluge_file_bytes_collect_mkdir_all,
    deluge_file_bytes_collect_read,
    deluge_file_bytes_collect_read_at,
    deluge_file_bytes_collect_rename,
    deluge_file_bytes_collect_set_time,
    deluge_file_bytes_collect_stat,
    deluge_file_bytes_collect_unlink,
    deluge_file_bytes_collect_write,
    deluge_file_bytes_collect_write_at,
    deluge_file_bytes_request_copy,
    deluge_file_bytes_request_dir_exists,
    deluge_file_bytes_request_exists,
    deluge_file_bytes_request_mkdir_all,
    deluge_file_bytes_request_read,
    deluge_file_bytes_request_read_at,
    deluge_file_bytes_request_rename,
    deluge_file_bytes_request_set_time,
    deluge_file_bytes_request_stat,
    deluge_file_bytes_request_unlink,
    deluge_file_bytes_request_write,
    deluge_file_bytes_request_write_at,
    deluge_file_bytes_request_read_into,
    deluge_file_bytes_collect_read_into,
    deluge_file_bytes_request_write_from,
    deluge_file_bytes_collect_write_from,
    deluge_file_bytes_holds,
    deluge_file_bytes_progress,
);
#[cfg(feature = "host_app")]
abi_pin!(
    deluge_file_bytes_set_latency_ms,
    deluge_file_bytes_set_chunk_latency_ms,
    deluge_file_bytes_set_write_from_latency_ms
);
