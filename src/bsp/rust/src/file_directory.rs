//! The directory-open channel: `include/libdeluge/file_directory.h`'s Rust backing.
//!
//! # What it is for
//!
//! Opening a directory is the one blocking call left in the browser's listing path.
//! `deluge_efatfs_dir_open` bridges to `task_dir_open` through `sim_block::block_on`, which spins
//! the caller until the open completes -- and the caller is `Browser`, on the interaction tier,
//! which by the execution model may never block on a lower tier. This channel replaces that spin
//! with a request the caller drops off and an answer it collects on a later tick.
//!
//! # What it is NOT
//!
//! It does not enumerate. `readdir_next` is FS-free (it walks the in-memory snapshot the cursor
//! already holds -- see `efatfs_fs::task_dir_read`), so once the handle exists, walking it costs no
//! card I/O and the C++ side does it inline, to completion, in the tick that collects. Only the
//! OPEN crosses this boundary, and the open is the only part that touches the card.
//!
//! # One slot, not a table
//!
//! The browser enforces one listing at a time (`Browser::listingInProgress_`), so a newer request
//! REPLACES an older one and the caller of a dropped request reads `UNKNOWN` -- "request it
//! (again)" -- on its next tick, which it will only do if it still wants it.
//!
//! # Handles are a scarce resource, and this channel can strand them
//!
//! `efatfs_core::MAX_DIR_HANDLES` is **8**. An answer this channel publishes holds one of those
//! eight open until somebody takes it, and an answer that is refused at the take -- the card went,
//! or a newer publish displaced it -- holds one that nobody will ever ask for again. So every
//! operation that discards an entry hands the handle back to its caller to close, rather than
//! dropping it: [`take_from`], [`publish_into`] and [`drain`] all return the handle they orphaned.
//! Eight silent leaks and directory browsing stops working entirely.
//!
//! The closing itself is deliberately NOT done here. It needs `crate::efatfs_fs`, which does not
//! exist in the carriers that `#[path]`-include this file for testing, and keeping the slot logic
//! free of it is what lets every rule above be unit-tested on the host.
//!
//! # The card belt
//!
//! Every entry is stamped with the card generation the open BEGAN under, and the stamp is compared
//! at the TAKE -- never only at publish. `deluge_card_flush`'s clear is `try_lock`ed and may not
//! land at all; the take is the guarantee. See `crates/deluge_card_generation`.

use core::ffi::{CStr, c_char};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
#[cfg(feature = "async_file_directory_active")]
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

/// Longest path this channel keys on, in CHARACTERS -- `DELUGE_MAX_PATH` minus its NUL, the same
/// bound `audio_loader::LoadPath` and `file_prefix::PrefixPath` carry, and for the same reason (a C
/// buffer size counts the terminator; the text it can hold is one byte shorter).
///
/// Spelled here rather than imported so this module compiles in every carrier that
/// `#[path]`-includes it. Pinned to the same literal by the assertion below and by
/// `file_directory_abi.cpp`'s `static_assert`s, so the three cannot drift apart silently.
const MAX_PATH_CHARS: usize = 255;

/// One path, inline. **Nothing in this module may touch the Rust global heap** -- that allocator
/// assumes single-threaded use and this driver runs on a preempting executor. See
/// `audio_loader::LoadPath`, which says it at length.
pub type DirPath = heapless::String<MAX_PATH_CHARS>;

/// `deluge_file_directory_collect`: requested or in flight; ask again, do not re-request.
pub const DELUGE_FILE_DIRECTORY_PENDING: u8 = 0;
/// `deluge_file_directory_collect`: a handle was written out and removed. It is now the caller's.
pub const DELUGE_FILE_DIRECTORY_READY: u8 = 1;
/// `deluge_file_directory_collect`: nothing known about this path. Request it (again).
pub const DELUGE_FILE_DIRECTORY_UNKNOWN: u8 = 2;
/// `deluge_file_directory_collect`: the open did not happen. Terminal -- do NOT re-request.
pub const DELUGE_FILE_DIRECTORY_FAILED: u8 = 3;

/// The values this module hand-duplicates from `file_directory.h`, pinned.
///
/// Asserting a constant against its own literal looks tautological, and that is the point: it makes
/// a change take two deliberate edits instead of one, and the mirror of these numbers lives in
/// `file_directory_abi.cpp`'s `static_assert`s.
///
/// **The status numbering is the audio loader's and the prefix channel's, value for value**, and
/// that is a safety property rather than tidiness. Two of these headers will plausibly be included
/// in one translation unit -- browsing a folder wants a listing AND artwork -- where
/// `deluge_file_directory_collect(..) == DELUGE_FILE_PREFIX_READY` compiles clean and nothing
/// cross-checks it. Under a divergent numbering that expression would mean something else entirely.
/// Keep them equal.
const _: () = {
    assert!(MAX_PATH_CHARS == 255); // DELUGE_MAX_PATH - 1, as audio_loader::LoadPath
    assert!(DELUGE_FILE_DIRECTORY_PENDING == 0);
    assert!(DELUGE_FILE_DIRECTORY_READY == 1);
    assert!(DELUGE_FILE_DIRECTORY_UNKNOWN == 2);
    assert!(DELUGE_FILE_DIRECTORY_FAILED == 3);
};

/// What one directory open produced.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DirOutcome {
    /// An open efatfs directory handle, ready to enumerate. Whoever takes it owns it and must close
    /// it -- on the C++ side `deluge::io::Directory::adopt` puts it back under RAII.
    Opened(u32),
    /// The open failed, carrying the status rather than collapsing it. `onListingFailed` shows the
    /// user this, so "no such folder" and "the card is unreadable" must not become one message.
    ///
    /// An `i8` because that is `DelugeStatus`'s real underlying type (`typedef enum DelugeStatus :
    /// int8_t`), and its values are NEGATIVE -- an unsigned carrier would report every failure as
    /// some large positive status. Carried bare rather than as the bindgen enum so this module
    /// compiles in carriers with no bindings.
    Failed(i8),
}

impl DirOutcome {
    /// The handle this outcome is holding open, if any. The one place that knows a `Failed` has
    /// nothing to close -- reporting handle 0 for it would have a caller close a live slot
    /// belonging to somebody else.
    fn handle(self) -> Option<u32> {
        match self {
            Self::Opened(handle) => Some(handle),
            Self::Failed(_) => None,
        }
    }
}

/// What [`take_from`] found for the path it was asked about.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DirTake {
    /// This path's outcome, stamped with the taker's own generation. An `Opened` handle is now the
    /// taker's to close.
    Ready(DirOutcome),
    /// Nothing here for this path. Either the slot holds another path's answer (untouched), or it
    /// held this path's answer stamped for a card that has gone -- in which case the entry has been
    /// REMOVED and `orphan` names the handle that removal left open.
    Absent {
        /// A handle the slot was holding for a card that is gone. Nobody will ask for it again and
        /// it still occupies one of `MAX_DIR_HANDLES` slots, so the taker must close it.
        orphan: Option<u32>,
    },
}

/// One finished open, stamped with the card generation the open BEGAN under.
struct DirEntry {
    key: DirPath,
    generation: u32,
    outcome: DirOutcome,
}

/// The one answer this channel holds, if it holds one.
///
/// Passed to [`publish_into`]/[`take_from`]/[`drain`] by reference rather than reached for as a
/// static, so the slot logic is compiled and unit-tested on every build -- including the ones where
/// the live [`RESULT`] does not exist.
///
/// Named `DirectorySlot` rather than `DirSlot` because `efatfs_core::DirSlot` already exists and
/// means something else (one row of the handle table). Two types with one name, one holding the
/// other's handles, would be a needless trap.
#[derive(Default)]
pub struct DirectorySlot {
    entry: Option<DirEntry>,
}

impl DirectorySlot {
    pub const fn new() -> Self {
        Self { entry: None }
    }
    pub fn is_empty(&self) -> bool {
        self.entry.is_none()
    }
}

/// Publish `outcome` under `key`, stamped with `generation`, replacing whatever the slot held.
///
/// Returns the handle of any answer it displaced, which the caller must close. Single-flight makes
/// a displacement rare, not impossible: a browser that navigates away leaves its answer
/// uncollected, and the next listing's publish lands on top of it.
/// An over-long key is REFUSED and the slot cleared, exactly as `file_prefix::new_entry` does and
/// for the same reason: unreachable through the FFI (which refuses an over-long path before it ever
/// reaches the driver), but an answer nobody can key is an answer nobody can ever take, and leaving
/// a stale neighbour resident in its place is strictly worse than an empty slot. The handle the
/// refused publish was carrying comes back alongside the displaced one -- both are orphans.
pub fn publish_into(
    slot: &mut DirectorySlot,
    key: &str,
    outcome: DirOutcome,
    generation: u32,
) -> Option<u32> {
    let displaced = slot.entry.take().and_then(|entry| entry.outcome.handle());
    let Ok(key) = DirPath::try_from(key) else {
        // The slot is already cleared by the `take()` above. `outcome`'s own handle is orphaned
        // too, but only one can be reported; prefer the one the caller did not just produce, since
        // the caller still holds `outcome` and can close it itself.
        debug_assert!(false, "an over-long key cannot reach a publish");
        return displaced.or_else(|| outcome.handle());
    };
    slot.entry = Some(DirEntry {
        key,
        generation,
        outcome,
    });
    displaced
}

/// Take `path`'s outcome if the slot holds one stamped `generation`.
///
/// A key match with a stale stamp is REMOVED and its handle reported as an orphan -- removed as
/// well as withheld, because leaving it resident would let a later take at some future generation
/// find it again, and returning it is the wrong-card bug the flush exists to prevent.
///
/// **This is the safety property, and the only one:** `flush_tables`'s clear is `try_lock`ed and
/// may not land, so nothing upstream of here can be relied on to have removed a stale entry.
///
/// A key MISMATCH leaves the slot alone and reports no orphan. It is not this take's answer to
/// discard: the path it belongs to may be about to collect it.
pub fn take_from(slot: &mut DirectorySlot, path: &str, generation: u32) -> DirTake {
    let Some(entry) = slot.entry.as_ref() else {
        return DirTake::Absent { orphan: None };
    };
    if entry.key != path {
        return DirTake::Absent { orphan: None };
    }
    let outcome = entry.outcome;
    let stamped = entry.generation;
    slot.entry = None;
    if stamped == generation {
        DirTake::Ready(outcome)
    } else {
        DirTake::Absent {
            orphan: outcome.handle(),
        }
    }
}

/// Empty the slot and hand back whatever handle it was holding. What `flush_tables` uses, so an
/// ejection does not strand one of the eight.
pub fn drain(slot: &mut DirectorySlot) -> Option<u32> {
    slot.entry.take().and_then(|entry| entry.outcome.handle())
}

// ── The open driver ─────────────────────────────────────────────────────────

/// The one filesystem operation this channel needs, behind a trait so the module compiles and is
/// tested in carriers that have no `crate::efatfs_fs`.
///
/// Two operations, not one: the close is here because the DRIVER may need it (a publish that
/// displaces a live answer orphans a handle on the task side too), and routing it through the same
/// trait keeps "everything on this executor awaits, and nothing calls into C++" in one place.
#[cfg(feature = "async_file_directory")]
pub trait DirOps {
    /// Open `path` for iteration, resolving to an efatfs directory handle or the status that
    /// stopped it.
    async fn open_dir(&self, path: &str) -> Result<u32, i8>;
    /// Release a directory handle. The table holds only `MAX_DIR_HANDLES` (8) slots, so a leak here
    /// is not a slow drift -- eight of them stop directory browsing altogether.
    async fn close_dir(&self, handle: u32);
}

/// Open `path` once, mapping the result onto what the slot carries.
///
/// Deliberately trivial. It exists so the driver's shape matches `file_prefix::prefix_io_once` and
/// `audio_loader::load_io_once` -- one named function that is the whole of this channel's card
/// contact -- rather than because the mapping is subtle.
#[cfg(feature = "async_file_directory")]
pub async fn dir_io_once<O: DirOps>(ops: &O, path: &str) -> DirOutcome {
    match ops.open_dir(path).await {
        Ok(handle) => DirOutcome::Opened(handle),
        Err(status) => DirOutcome::Failed(status),
    }
}

// ── The live channel ────────────────────────────────────────────────────────

/// Raised to wake [`file_directory_task`] when a directory is newly requested. Always compiled --
/// with the task absent nobody awaits it and signalling is a harmless flag set.
pub static DIRECTORY_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// The one outstanding request, if any.
///
/// A single slot rather than a queue, and a newer request REPLACES an older one -- the single-flight
/// shape stated as storage. A browser cursor that has moved on does not want the folder it left
/// behind, and the caller of a dropped request reads `UNKNOWN` on its next tick, which it will only
/// do if it still wants it.
#[cfg(feature = "async_file_directory_active")]
static REQUEST: Mutex<CriticalSectionRawMutex, Option<DirPath>> = Mutex::new(None);

/// The path being opened right now, if any. Exists so [`deluge_file_directory_collect`] can answer
/// `PENDING` rather than `UNKNOWN` in the window between a request leaving [`REQUEST`] and its
/// handle reaching [`RESULT`] -- an `UNKNOWN` there would tell the caller to re-request work that is
/// already running, and it would do so on every tick.
#[cfg(feature = "async_file_directory_active")]
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<DirPath>> = Mutex::new(None);

/// The finished open waiting to be collected. See [`DirectorySlot`].
#[cfg(feature = "async_file_directory_active")]
static RESULT: Mutex<CriticalSectionRawMutex, DirectorySlot> = Mutex::new(DirectorySlot::new());

/// Drop what this channel is holding, closing any handle that leaves behind. The card generation's
/// bump belongs to the CALLER (`card.rs`'s `deluge_card_flush`), which performs it before calling
/// here.
///
/// Runs synchronously from C++ on the interaction tier and must never wait, so all three are
/// `try_lock`ed. **Every one of them may fail, and none of them costs correctness**: the clears are
/// housekeeping, and the safety property is the STAMP compared in [`take_from`]. A surviving entry
/// carries the generation of the card that has gone and is therefore uncollectable, not
/// stale-but-live.
///
/// The clears are not free of RESOURCE, though, which is why this one closes. A surviving entry
/// holds one of eight directory handles that nothing will ever take, so the housekeeping this
/// channel skips is a leak rather than a nuisance -- see the module doc.
#[cfg(feature = "async_file_directory_active")]
pub(crate) fn flush_tables() {
    if let Ok(mut request) = REQUEST.try_lock() {
        *request = None;
    }
    if let Ok(mut in_flight) = IN_FLIGHT.try_lock() {
        *in_flight = None;
    }
    if let Ok(mut result) = RESULT.try_lock() {
        if let Some(orphan) = drain(&mut result) {
            close_handle_now(orphan);
        }
    }
}

/// No directory channel on this build: the statics do not exist and nothing was ever published, so
/// there is nothing to discard. The generation bump still happens in the caller -- it is not this
/// channel's, and another loader on this build may well need it.
#[cfg(not(feature = "async_file_directory_active"))]
pub(crate) fn flush_tables() {}

/// Close an orphaned directory handle from a context that cannot await.
///
/// `task_dir_close` is FS-free (it only removes a row from `DIR_HANDLES`), so a `try_lock` is enough
/// and no card I/O is involved. A contended table means one of the eight slots leaks until the next
/// mount -- bounded, rare, and strictly better than the alternative, which is blocking the
/// interaction tier inside `deluge_card_flush`.
#[cfg(all(
    feature = "async_file_directory_active",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
fn close_handle_now(handle: u32) {
    backing::try_close_dir(handle);
}

/// No efatfs backing linked into this carrier, so there is no table to close against. Reached only
/// by the `#[path]` test carriers, where every handle is a number the test made up.
#[cfg(all(
    feature = "async_file_directory_active",
    not(any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    ))
))]
fn close_handle_now(_handle: u32) {}

/// Whether the Rust directory channel backs `file_directory.h` on this BSP/config -- the switch the
/// C++ side asks before choosing the async path over opening the directory itself. The symbol must
/// always exist so that call site links regardless of which config produced this image; only the
/// return value moves.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_directory_async_active() -> bool {
    cfg!(feature = "async_file_directory_active")
}

/// Request `dir_path` be opened, and wake the driver. See `file_directory.h`.
///
/// Silently drops an unusable request (NULL, non-UTF-8, over-long, or the request slot contended):
/// there is no failure the caller could act on that re-requesting does not already cover, and the
/// collect it will run next tick reports `UNKNOWN`, whose documented meaning is exactly that.
///
/// # Safety
///
/// `dir_path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_directory_request(dir_path: *const c_char) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(dir_path) }) else {
        return;
    };
    enqueue_request(path);
}

/// Install the request and wake the driver, replacing any request not yet picked up.
///
/// `try_lock` rather than a block: this is called synchronously from C++ in whatever context the
/// app happens to be in, and must never wait.
#[cfg(feature = "async_file_directory_active")]
fn enqueue_request(path: DirPath) {
    let Ok(mut request) = REQUEST.try_lock() else {
        return; // Contended; the caller re-requests on its next tick.
    };
    *request = Some(path);
    drop(request);
    DIRECTORY_WAKE.signal(());
}

/// No driver on this build: drop it, exactly as `file_directory_abi.cpp`'s weak fallback does.
/// Nothing reaches here in practice -- the C++ side gates on
/// [`deluge_file_directory_async_active`], which is false in this configuration.
#[cfg(not(feature = "async_file_directory_active"))]
fn enqueue_request(_path: DirPath) {}

/// Collect the directory opened for `dir_path`, if one is ready. See `file_directory.h`.
///
/// # Why the probe order is REQUEST -> IN_FLIGHT -> RESULT
///
/// An entry only ever moves forward: [`REQUEST`] -> [`IN_FLIGHT`] -> [`RESULT`]. Three rules make a
/// forward pass unable to miss one, and all three are load-bearing -- they are the audio loader's
/// and the prefix channel's, unchanged:
///
/// 1. [`take_next_request`] sets `IN_FLIGHT` while still holding the `REQUEST` lock.
/// 2. The driver publishes to `RESULT` *before* clearing `IN_FLIGHT`, so that handoff overlaps
///    rather than gaps.
/// 3. [`is_requested`] and [`is_in_flight`] report `true` when they cannot take their lock -- it is
///    the LOCK, not the table, that covers the REQUEST -> IN_FLIGHT window, in which the entry
///    genuinely is in neither.
///
/// Reading in the reverse order can report `UNKNOWN` for an open that has already landed, so the
/// caller re-requests work that is done -- and, here, orphans a directory handle every time it does.
///
/// # Safety
///
/// `dir_path` must be a valid NUL-terminated C string; `out_handle` must point to a writable
/// `uint32_t` and `out_status` to a writable `DelugeStatus`. `out_handle` is written ONLY on
/// [`DELUGE_FILE_DIRECTORY_READY`], `out_status` ONLY on [`DELUGE_FILE_DIRECTORY_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_directory_collect(
    dir_path: *const c_char,
    out_handle: *mut u32,
    out_status: *mut i8,
) -> u8 {
    if out_handle.is_null() || out_status.is_null() {
        return DELUGE_FILE_DIRECTORY_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(dir_path) }) else {
        return DELUGE_FILE_DIRECTORY_UNKNOWN;
    };
    match probe(&path) {
        Probe::Ready(handle) => {
            // SAFETY: `out_handle` is non-null (checked above) and the caller's contract makes it a
            // writable, aligned `uint32_t`. Written only on READY, as the header promises.
            unsafe { *out_handle = handle };
            DELUGE_FILE_DIRECTORY_READY
        }
        Probe::Pending => DELUGE_FILE_DIRECTORY_PENDING,
        Probe::Unknown => DELUGE_FILE_DIRECTORY_UNKNOWN,
        Probe::Failed(status) => {
            // SAFETY: as above, and written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_FILE_DIRECTORY_FAILED
        }
    }
}

/// What one forward pass over the channel found.
///
/// Deliberately produced by a SINGLE pass. Asking "is it ready?" and "is it still coming?" as two
/// separate passes reintroduces the race the ordering exists to close: the driver can publish and
/// clear its in-flight mark between them, and the second pass then reports `UNKNOWN` for a handle
/// the first pass merely missed -- so the caller re-requests an open that has already landed and
/// strands the handle it left behind.
enum Probe {
    /// An open directory handle, now the caller's to close. **Only this case writes `out_handle`.**
    Ready(u32),
    Pending,
    Unknown,
    /// The open did not happen, carrying the status. Terminal -- see `file_directory.h`.
    Failed(i8),
}

/// One forward pass: REQUEST, then IN_FLIGHT, then RESULT. See [`deluge_file_directory_collect`]'s
/// doc for why the order and the contention mapping are both load-bearing.
#[cfg(feature = "async_file_directory_active")]
fn probe(path: &str) -> Probe {
    if is_requested(path) || is_in_flight(path) {
        return Probe::Pending;
    }
    // A contended slot means the driver is mid-publish. `Pending` is the honest answer: never claim
    // `Unknown` on contention, or the caller re-requests work that is about to land.
    let Ok(mut result) = RESULT.try_lock() else {
        return Probe::Pending;
    };
    // The card generation is read HERE, at the take, not sampled earlier: it is the collector's own
    // view of which card is in the slot right now, and deciding from it is what makes a handle's
    // fate independent of whether the flush that invalidated it managed to take this lock.
    match take_from(&mut result, path, deluge_card_generation::current()) {
        DirTake::Ready(DirOutcome::Opened(handle)) => Probe::Ready(handle),
        DirTake::Ready(DirOutcome::Failed(status)) => Probe::Failed(status),
        DirTake::Absent { orphan } => {
            // The take refused an answer read from a card that has gone. Nothing will ever ask for
            // its handle again, and it holds one of eight slots, so close it here -- this is the
            // collect-side half of what `flush_tables` does on the eject side.
            if let Some(orphan) = orphan {
                close_handle_now(orphan);
            }
            Probe::Unknown
        }
    }
}

/// No driver on this build: nothing was ever requested, so nothing is coming. `Unknown`, never
/// `Pending` -- `Pending` would tell a caller to keep asking forever. Matches
/// `file_directory_abi.cpp`'s weak fallback.
#[cfg(not(feature = "async_file_directory_active"))]
fn probe(_path: &str) -> Probe {
    Probe::Unknown
}

/// Whether `path` is the request not yet picked up.
///
/// **Contention reports `true`, and that is load-bearing, not defensive.** The driver marks a
/// request in flight while still holding this lock, so a collect racing that handoff finds the lock
/// taken -- mapping it to `false` here would report `UNKNOWN` for a request mid-handoff, which is
/// exactly the bug the probe order exists to prevent. Do not "simplify" this branch.
#[cfg(feature = "async_file_directory_active")]
fn is_requested(path: &str) -> bool {
    match REQUEST.try_lock() {
        Ok(request) => request.as_deref() == Some(path),
        Err(_) => true,
    }
}

/// Whether `path` is the open running right now. Contention reports `true`, for the same reason as
/// [`is_requested`] -- and with the same warning.
#[cfg(feature = "async_file_directory_active")]
fn is_in_flight(path: &str) -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight.as_deref() == Some(path),
        Err(_) => true,
    }
}

/// Copy a C string into a fixed-capacity [`DirPath`]. `None` for NULL, non-UTF-8, or a path longer
/// than the bound -- a path that does not fit is refused rather than truncated, because a truncated
/// path names a *different* directory that may well exist.
///
/// # Safety
///
/// `ptr` must be NULL or a valid NUL-terminated C string.
unsafe fn cstr_to_path(ptr: *const c_char) -> Option<DirPath> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: the caller's contract, above.
    let cstr = unsafe { CStr::from_ptr(ptr) };
    DirPath::try_from(cstr.to_str().ok()?).ok()
}

// ── The production ops ──────────────────────────────────────────────────────
//
// Only compiled where the efatfs backing is actually linked: on device under `efatfs_streaming`,
// and on host under `host_app`. Kept separate from the driver above so the driver -- and every rule
// in this file -- compiles and is tested in the `#[path]` carriers, which have no `crate::`.

/// The efatfs directory table behind the production ops: the device mount on device, the host shim
/// under `host_app`. One alias rather than a `#[cfg]` at every call site -- the same shape
/// `audio_loader::prod` uses, and for the same reason.
#[cfg(all(
    feature = "async_file_directory",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
mod backing {
    #[cfg(target_os = "none")]
    pub(super) use crate::efatfs_fs::{task_dir_close, task_dir_open, try_close_dir};
    #[cfg(not(target_os = "none"))]
    pub(super) use crate::efatfs_host_shim::{task_dir_close, task_dir_open, try_close_dir};
}

/// The real [`DirOps`], wired to the efatfs directory-handle table. Holds no state: both operations
/// are keyed by path or handle.
#[cfg(all(
    feature = "async_file_directory",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
pub struct ProdDirOps;

#[cfg(all(
    feature = "async_file_directory",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
impl ProdDirOps {
    pub fn new() -> Self {
        Self
    }
}

#[cfg(all(
    feature = "async_file_directory",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
impl DirOps for ProdDirOps {
    async fn open_dir(&self, path: &str) -> Result<u32, i8> {
        // The same `task_dir_open` `deluge_efatfs_dir_open` calls -- the difference is entirely
        // that this awaits it and that one spins on it. `DelugeStatus` is `int8_t`, so the cast is
        // the identity on the wire.
        backing::task_dir_open(path)
            .await
            .map_err(|status| status as i8)
    }

    async fn close_dir(&self, handle: u32) {
        backing::task_dir_close(handle).await;
    }
}

// ── The task ────────────────────────────────────────────────────────────────

/// The directory opener: wakes on [`DIRECTORY_WAKE`], opens the outstanding request, publishes the
/// handle, then goes back to sleep.
///
/// [`IN_FLIGHT`] is set before the open and cleared only after the handle is published, so
/// [`deluge_file_directory_collect`] never sees a gap in which a running open looks unknown.
///
/// # A failed open publishes a FAILURE, and publishing it is not optional
///
/// Publishing nothing would report `UNKNOWN`, whose documented meaning is "request it (again)" --
/// so a folder that cannot be opened would be re-requested and re-opened on every single tick,
/// forever, for as long as the browser sits on it. A published failure ends that: it is terminal,
/// exactly as a published handle is, and it carries the status so `onListingFailed` can say which
/// failure it was.
#[cfg(all(
    feature = "async_file_directory_active",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
#[embassy_executor::task]
pub async fn file_directory_task() {
    DIRECTORY_WAKE.wait().await;
    let ops = ProdDirOps::new();
    loop {
        while let Some((path, generation)) = take_next_request().await {
            let outcome = dir_io_once(&ops, &path).await;
            // Publish BEFORE clearing IN_FLIGHT, never after: the entry is briefly in both tables,
            // which is harmless, whereas the reverse leaves a window in which it is in neither and
            // `collect` would report `UNKNOWN` for an open that just succeeded -- stranding the
            // handle it published.
            //
            // `generation` was sampled before the open above, so a card pulled during it is what
            // makes this publish a no-op.
            publish_result(&ops, &path, outcome, generation).await;
            mark_in_flight(None).await;
        }
        DIRECTORY_WAKE.wait().await;
    }
}

/// Take the outstanding request and mark it in flight, atomically with respect to
/// [`deluge_file_directory_collect`], and report the card generation the open begins under.
///
/// The mark happens while the [`REQUEST`] lock is still held, so there is no instant at which the
/// request has left the slot but not yet appeared as in-flight. A collect racing this either sees
/// it requested, or blocks on the `try_lock` and reports `PENDING` -- never `UNKNOWN`.
///
/// The generation is read HERE, before the first `await` that can suspend this task and therefore
/// before any I/O, because that is what makes it mean "the card this open is opening". Reading it
/// later -- at publish time -- reads the value a flush has already installed and defeats the check
/// entirely.
#[cfg(all(
    feature = "async_file_directory_active",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
async fn take_next_request() -> Option<(DirPath, u32)> {
    let mut request = REQUEST.lock().await;
    let path = request.take()?;
    let generation = deluge_card_generation::current();
    mark_in_flight(Some(path.clone())).await;
    Some((path, generation))
}

/// Record (or clear) the path being opened right now. Called by [`file_directory_task`] while it
/// still holds the [`REQUEST`] lock -- see [`take_next_request`].
///
/// Also the seam the host tests drive to exercise [`deluge_file_directory_collect`]'s IN_FLIGHT
/// branch, which no test could reach otherwise because no test runs the task.
#[cfg(feature = "async_file_directory_active")]
pub async fn mark_in_flight(path: Option<DirPath>) {
    *IN_FLIGHT.lock().await = path;
}

/// Publish one finished open into the live [`RESULT`] slot, stamped with the card generation the
/// open began under, and skip the publish entirely if that card is already gone.
///
/// Closes every handle this publish orphans -- the one it displaces, and its own if the card went
/// while it was opening. A publish is the one place where BOTH can happen at once, and neither has
/// anyone else left to close it.
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
#[cfg(all(
    feature = "async_file_directory_active",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
pub async fn publish_result<O: DirOps>(ops: &O, key: &str, outcome: DirOutcome, generation: u32) {
    let mut result = RESULT.lock().await;
    if generation != deluge_card_generation::current() {
        // The card this was opened on is gone. The handle names a snapshot of a directory that is
        // no longer in the slot, so close it here rather than publish it -- the collector would
        // refuse it at the take anyway, and this way the close happens on the task, which can
        // await, instead of through `close_handle_now`'s `try_lock`.
        //
        // A FAILED open is dropped here too, and must be: the failure belongs to the card that has
        // gone, and reporting it as terminal against the card now in the slot would put an error on
        // screen for a folder nobody has tried to open yet.
        if let Some(orphan) = outcome.handle() {
            ops.close_dir(orphan).await;
        }
        return;
    }
    if let Some(displaced) = publish_into(&mut result, key, outcome, generation) {
        // An answer nobody collected. Closing it here is what keeps a browser that navigates away
        // mid-listing from burning one of the eight handles per abandoned folder.
        ops.close_dir(displaced).await;
    }
}
