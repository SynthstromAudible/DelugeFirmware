//! The directory-open channel: the Rust backing of `libdeluge/file_directory.h`.
//!
//! A synchronous `deluge_efatfs_dir_open` `block_on`-spins its caller until the open completes, and
//! the caller (`Browser`) is on the interaction tier, which may never block on a lower tier. This
//! channel replaces the spin with a request the caller drops off and an answer it collects on a
//! later tick. Only the open crosses the channel: enumerating the handle (`readdir_next`) walks an
//! in-memory snapshot with no card I/O, so the C++ side does it inline once it has collected.
//!
//! # One slot, not a table
//!
//! The browser runs one listing at a time, so a newer request replaces an older one; the dropped
//! request's caller reads `UNKNOWN` and re-requests only if it still wants it.
//!
//! # Directory handles are scarce
//!
//! There are only eight (`MAX_DIR_HANDLES`), and a published answer holds one open until it is
//! taken. An answer refused at the take (the card went) or displaced by a newer publish would hold
//! it forever, so [`take_from`], [`publish_into`] and [`drain`] each return the handle they
//! orphaned for the caller to close. The slot logic does not close handles itself, so it needs no
//! efatfs backing and is unit-tested on the host.
//!
//! # Card swaps
//!
//! Every entry is stamped with the [card generation](crate::storage::card::generation) the open
//! began under, and the stamp is checked at the take. `deluge_card_flush`'s clear
//! (`flush_tables`) is `try_lock`ed and may not land; the take is the guarantee.

use core::ffi::{CStr, c_char};

use crate::abi::handle::{DelugeHandle, to_abi};

use crate::channel::{
    DELUGE_CHANNEL_FAILED, DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_READY, DELUGE_CHANNEL_UNKNOWN,
    DELUGE_PATH_MAX, DelugeChannelStatus,
};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

/// Longest path this channel keys on, in characters: `DELUGE_PATH_MAX` minus the NUL.
pub(crate) const MAX_PATH_CHARS: usize = DELUGE_PATH_MAX - 1;

/// One path, stored inline. Nothing in this module may touch the Rust global heap, which assumes
/// single-threaded use while this task preempts thread mode.
pub type DirPath = heapless::String<MAX_PATH_CHARS>;

/// `DELUGE_ERR_NO_FILESYSTEM` as a bare `i8`, since the plain host build has no bindgen output.
///
/// It must stay a named constant in scope: an unknown identifier in pattern position is a fresh
/// binding, so `Err(DELUGE_ERR_NO_FILESYSTEM) =>` would compile and match every error.
pub(crate) const NO_FILESYSTEM: i8 = -11;

/// What one directory open produced.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DirOutcome {
    /// An open efatfs directory handle. Whoever takes it must close it (on the C++ side,
    /// `deluge::io::Directory::adopt` puts it under RAII).
    Opened(u32),
    /// The open failed, with its `DelugeStatus`, so `onListingFailed` can tell "no such folder"
    /// from "the card is unreadable".
    ///
    /// `i8` is `DelugeStatus`'s underlying type and its failure values are negative; it is carried
    /// bare because the plain host build has no bindgen output.
    Failed(i8),
}

impl DirOutcome {
    /// The handle this outcome holds open, if any. A `Failed` holds none (reporting handle 0 would
    /// close someone else's live slot).
    fn handle(self) -> Option<u32> {
        match self {
            Self::Opened(handle) => Some(handle),
            Self::Failed(_) => None,
        }
    }
}

impl crate::channel::Answer for DirOutcome {
    fn orphan(&self) -> Option<u32> {
        self.handle()
    }
}

/// What [`take_from`] found for the path it was asked about; mirrors
/// [`Take`](crate::channel::Take).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DirTake {
    /// This path's outcome for the current card. An `Opened` handle is now the taker's to close.
    Ready(DirOutcome),
    /// Nothing here for this path.
    Absent {
        /// A handle held for a card that has gone; the taker must close it.
        orphan: Option<u32>,
    },
}

/// The one answer this channel holds, if any: an [`AnswerSlot`](crate::channel::AnswerSlot) keyed
/// by path.
///
/// Passed to [`publish_into`], [`take_from`] and [`drain`] by reference so the slot logic is
/// testable without the statics. Not named `DirSlot`, which is a row of the efatfs handle table.
pub struct DirectorySlot {
    inner: crate::channel::AnswerSlot<DirPath, DirOutcome>,
}

impl DirectorySlot {
    /// Returns an empty slot; the `const` counterpart of [`Default`].
    pub const fn new() -> Self {
        Self {
            inner: crate::channel::AnswerSlot::new(),
        }
    }
    /// Whether the slot holds nothing.
    pub fn is_empty(&self) -> bool {
        self.inner.is_empty()
    }
}

impl Default for DirectorySlot {
    fn default() -> Self {
        Self::new()
    }
}

/// Publishes `outcome` under `key`, stamped with `generation`, replacing whatever the slot held.
///
/// Returns the handle of any answer it displaced (a browser that navigated away leaves one
/// uncollected), which the caller must close. An over-long key (unreachable through the FFI) clears
/// the slot and returns the displaced handle, or else `outcome`'s own.
pub fn publish_into(
    slot: &mut DirectorySlot,
    key: &str,
    outcome: DirOutcome,
    generation: u32,
) -> Option<u32> {
    let Ok(key) = DirPath::try_from(key) else {
        // Both handles are orphaned but only one can be returned; prefer the displaced one, since
        // the caller still holds `outcome` and can close it itself.
        debug_assert!(false, "an over-long key cannot reach a publish");
        let displaced = slot.inner.drain().map(|orphan| orphan.handle);
        return displaced.or_else(|| outcome.handle());
    };
    slot.inner
        .publish(key, outcome, generation)
        .map(|orphan| orphan.handle)
}

/// Takes `path`'s outcome if the slot holds one stamped `generation`, following
/// [`AnswerSlot::take`](crate::channel::AnswerSlot::take).
pub fn take_from(slot: &mut DirectorySlot, path: &str, generation: u32) -> DirTake {
    match slot.inner.take(path, generation) {
        crate::channel::Take::Ready(outcome) => DirTake::Ready(outcome),
        crate::channel::Take::Absent { orphan } => DirTake::Absent {
            orphan: orphan.map(|orphan| orphan.handle),
        },
    }
}

/// Empties the slot and returns the handle it held, so an ejection does not strand one.
pub fn drain(slot: &mut DirectorySlot) -> Option<u32> {
    slot.inner.drain().map(|orphan| orphan.handle)
}

/// The filesystem operations this channel needs, behind a trait so the driver is testable without
/// an efatfs backing. All `async`; none reaches C++.
pub trait DirOps {
    /// Opens `path` for iteration, returning an efatfs directory handle or the failing status.
    async fn open_dir(&self, path: &str) -> Result<u32, i8>;
    /// Creates `path`. Failure is not reported: the open that follows is the verdict, and the
    /// commonest failure, EXISTS, is success for "make sure this directory is there".
    async fn create_dir(&self, path: &str);
    /// Releases a directory handle. There are only eight, so eight leaks stop directory browsing.
    async fn close_dir(&self, handle: u32);
}

/// Opens `path` once (creating it first if `create`), mapping the result onto what the slot
/// carries. This is the whole of the channel's card I/O.
pub async fn dir_io_once<O: DirOps>(ops: &O, path: &str, create: bool) -> DirOutcome {
    if create {
        // The browser's missing-folder fallback: a card write, so it belongs on the storage tier,
        // not inline on the interaction tier.
        ops.create_dir(path).await;
    }
    match ops.open_dir(path).await {
        Ok(handle) => DirOutcome::Opened(handle),
        Err(status) => DirOutcome::Failed(status),
    }
}

/// Wakes `file_directory_task` when a directory is requested. Signalling with no task waiting is a
/// harmless flag set.
pub static DIRECTORY_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// The one outstanding request, if any; a newer request replaces an older one.
static REQUEST: Mutex<CriticalSectionRawMutex, Option<DirRequest>> = Mutex::new(None);

/// One outstanding request: which directory, and whether to create it first.
#[derive(Clone)]
struct DirRequest {
    path: DirPath,
    create: bool,
}

/// The path being opened right now, if any, so [`deluge_file_directory_collect`] answers `PENDING`
/// (not `UNKNOWN`, which would make the caller re-request running work) between a request leaving
/// [`REQUEST`] and its handle reaching [`RESULT`].
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<DirPath>> = Mutex::new(None);

/// The finished open waiting to be collected. See [`DirectorySlot`].
static RESULT: Mutex<CriticalSectionRawMutex, DirectorySlot> = Mutex::new(DirectorySlot::new());

/// Clears the request, the in-flight mark and the result, closing the result's handle.
///
/// Called by `deluge_card_flush` after it bumps the card generation. It runs synchronously from
/// C++ and must never wait, so each clear is a `try_lock`. A failed clear never costs correctness
/// (the stamp checked in [`take_from`] is the guarantee), but a surviving result keeps a directory
/// handle open until a collect refuses and closes it.
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

/// Closes an orphaned directory handle from a context that cannot await.
///
/// Closing only removes a handle-table row (no card I/O), so a `try_lock` suffices. If the table
/// is contended the handle leaks until the next mount, which beats blocking the interaction tier
/// inside `deluge_card_flush`.
#[cfg(any(target_os = "none", feature = "host_app"))]
fn close_handle_now(handle: u32) {
    backing::try_close_dir(handle);
}

// Without an efatfs backing (the plain host build) there is no handle table to close against.
#[cfg(not(any(target_os = "none", feature = "host_app")))]
fn close_handle_now(_handle: u32) {}

/// Milliseconds `file_directory_task` waits before each open. See
/// [`deluge_file_directory_set_latency_ms`].
#[cfg(feature = "host_app")]
static LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: makes every directory open wait `ms` milliseconds before it touches the
/// card, so a test can hold an open in flight while a consumer keeps ticking. 0 (the default)
/// means no wait.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_directory_set_latency_ms(ms: u32) {
    LATENCY_MS.store(ms, core::sync::atomic::Ordering::Relaxed);
}

/// Requests that `dir_path` be opened (created first if `create_if_missing`) and wakes the driver.
/// See `file_directory.h`.
///
/// An unusable request (NULL, non-UTF-8, over-long, or the slot contended) is dropped silently:
/// the next collect reports `UNKNOWN` and the caller re-requests.
///
/// # Safety
///
/// `dir_path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_directory_request(
    dir_path: *const c_char,
    create_if_missing: bool,
) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(dir_path) }) else {
        return;
    };
    enqueue_request(path, create_if_missing);
}

/// Installs the request, replacing any not yet picked up, and wakes the driver. `try_lock`s: it is
/// called synchronously from C++ in any context and must never wait.
fn enqueue_request(path: DirPath, create: bool) {
    let Ok(mut request) = REQUEST.try_lock() else {
        return; // Contended; the caller re-requests on its next tick.
    };
    *request = Some(DirRequest { path, create });
    drop(request);
    DIRECTORY_WAKE.signal(());
}

/// Collects the directory opened for `dir_path`, if one is ready. See `file_directory.h`.
///
/// # Probe order
///
/// An entry only moves forward, `REQUEST` → `IN_FLIGHT` → `RESULT`, and the probe reads in
/// that order, under the audio loader's three rules (see
/// [`deluge_audio_load_collect`](super::audio_loader::deluge_audio_load_collect)):
///
/// 1. `take_next_request` sets `IN_FLIGHT` while still holding the `REQUEST` lock.
/// 2. The driver publishes to `RESULT` before clearing `IN_FLIGHT`.
/// 3. `is_requested` and `is_in_flight` report `true` when their lock is contended; only the
///    held lock covers the REQUEST → IN_FLIGHT handoff, when the entry is in neither.
///
/// Reading in reverse order can report `UNKNOWN` for an open that has landed, and here the
/// re-request would also orphan a directory handle.
///
/// # Safety
///
/// `dir_path` must be a valid NUL-terminated C string; `out_handle` must point to a writable
/// `DelugeHandle` and `out_status` to a writable `DelugeStatus`. `out_handle` is written only on
/// [`DELUGE_CHANNEL_READY`], `out_status` only on [`DELUGE_CHANNEL_FAILED`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_directory_collect(
    dir_path: *const c_char,
    out_handle: *mut DelugeHandle,
    out_status: *mut i8,
) -> DelugeChannelStatus {
    if out_handle.is_null() || out_status.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(dir_path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    match probe(&path) {
        Probe::Ready(handle) => {
            // SAFETY: `out_handle` is non-null (checked above) and, by the caller's contract,
            // writable and aligned. Written only on READY, as the header promises.
            unsafe { *out_handle = to_abi(handle) };
            DELUGE_CHANNEL_READY
        }
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
        Probe::Failed(status) => {
            // SAFETY: as above, and written only on FAILED.
            unsafe { *out_status = status };
            DELUGE_CHANNEL_FAILED
        }
    }
}

/// What one forward pass over the channel found.
///
/// Produced by a single pass: separate "ready?" and "still coming?" passes would let the driver
/// publish between them and report `UNKNOWN` for a handle that has landed, stranding it.
enum Probe {
    /// An open directory handle, now the caller's to close. Only this case writes `out_handle`.
    Ready(u32),
    Pending,
    Unknown,
    /// The open failed, with its status; terminal.
    Failed(i8),
}

/// One forward pass: REQUEST, then IN_FLIGHT, then RESULT. See [`deluge_file_directory_collect`].
fn probe(path: &str) -> Probe {
    if is_requested(path) || is_in_flight(path) {
        return Probe::Pending;
    }
    // A contended slot means the driver is mid-publish: `Unknown` would make the caller re-request
    // work that is about to land.
    let Ok(mut result) = RESULT.try_lock() else {
        return Probe::Pending;
    };
    // The generation is read at the take: the collector's own view of which card is in the slot.
    match take_from(&mut result, path, crate::storage::card::generation()) {
        DirTake::Ready(DirOutcome::Opened(handle)) => Probe::Ready(handle),
        DirTake::Ready(DirOutcome::Failed(status)) => Probe::Failed(status),
        DirTake::Absent { orphan } => {
            // A refused answer from a card that has gone: nothing will ask for its handle again.
            if let Some(orphan) = orphan {
                close_handle_now(orphan);
            }
            Probe::Unknown
        }
    }
}

/// Whether `path` is the request not yet picked up.
///
/// Contention reports `true`, deliberately: the driver marks a request in flight while holding
/// this lock, so `false` here would report `UNKNOWN` for a request mid-handoff.
fn is_requested(path: &str) -> bool {
    match REQUEST.try_lock() {
        Ok(request) => request.as_ref().is_some_and(|r| r.path == path),
        Err(_) => true,
    }
}

/// Whether `path` is the open running right now. Contention reports `true`, as in
/// [`is_requested`].
fn is_in_flight(path: &str) -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight.as_deref() == Some(path),
        Err(_) => true,
    }
}

/// Copies a C string into a [`DirPath`]. Returns `None` for NULL, non-UTF-8, or a path longer than
/// [`MAX_PATH_CHARS`]; a truncated path would name a different directory.
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

// Production wiring: only where the efatfs backing is linked (device, and host under `host_app`).

/// The efatfs directory table: the device mount on device, the host shim under `host_app`.
#[cfg(any(target_os = "none", feature = "host_app"))]
mod backing {
    #[cfg(target_os = "none")]
    pub(super) use crate::storage::efatfs::device::{
        mount, task_dir_close, task_dir_open, task_mkdir, try_close_dir,
    };
    #[cfg(not(target_os = "none"))]
    pub(super) use crate::storage::efatfs::host::{
        mount, task_dir_close, task_dir_open, task_mkdir, try_close_dir,
    };
}

/// The production [`DirOps`], over the efatfs directory-handle table. Stateless: every operation
/// is keyed by path or handle.
#[cfg(any(target_os = "none", feature = "host_app"))]
pub struct ProdDirOps;

#[cfg(any(target_os = "none", feature = "host_app"))]
impl ProdDirOps {
    pub fn new() -> Self {
        Self
    }
}

#[cfg(any(target_os = "none", feature = "host_app"))]
impl DirOps for ProdDirOps {
    async fn open_dir(&self, path: &str) -> Result<u32, i8> {
        // The `task_dir_open` that `deluge_efatfs_dir_open` spins on, awaited instead.
        // `DelugeStatus` is `int8_t`, so the cast is the identity on the wire.
        match backing::task_dir_open(path).await {
            Err(NO_FILESYSTEM) => {
                // Not mounted. Mounting is card I/O, so it happens here, lazily, rather than in
                // `StorageManager::initSD()` on the interaction tier. `mount()` bumps
                // `deluge_efatfs_mount_generation`, which tells the C++ side to run
                // `firstCardRead()`.
                backing::mount().await.map_err(|_| NO_FILESYSTEM)?;
                backing::task_dir_open(path)
                    .await
                    .map_err(|status| status as i8)
            }
            other => other.map_err(|status| status as i8),
        }
    }

    async fn close_dir(&self, handle: u32) {
        backing::task_dir_close(handle).await;
    }

    async fn create_dir(&self, path: &str) {
        // Discarded: the open that follows is the verdict (see `DirOps::create_dir`).
        let _ = backing::task_mkdir(path).await;
    }
}

/// The directory opener: wakes on [`DIRECTORY_WAKE`], opens the outstanding request, and publishes
/// the outcome, on the storage tier.
///
/// `IN_FLIGHT` is set before the open and cleared only after the outcome is published, so
/// [`deluge_file_directory_collect`] never sees a running open as unknown. A failed open is
/// published as a terminal failure; publishing nothing would read as `UNKNOWN` and be re-requested
/// every tick.
#[cfg(any(target_os = "none", feature = "host_app"))]
#[embassy_executor::task]
pub async fn file_directory_task() {
    crate::tier::scoped(crate::tier::Tier::Storage, file_directory_task_body()).await
}

#[cfg(any(target_os = "none", feature = "host_app"))]
async fn file_directory_task_body() {
    DIRECTORY_WAKE.wait().await;
    let ops = ProdDirOps::new();
    loop {
        while let Some((request, generation)) = take_next_request().await {
            // After the in-flight mark, so a collect during the hold answers PENDING, as it would
            // for a slow card.
            #[cfg(feature = "host_app")]
            {
                let ms = LATENCY_MS.load(core::sync::atomic::Ordering::Relaxed);
                if ms > 0 {
                    embassy_time::Timer::after_millis(u64::from(ms)).await;
                }
            }
            let outcome = dir_io_once(&ops, &request.path, request.create).await;
            // Publish before clearing IN_FLIGHT: the reverse leaves a window in which `collect`
            // reports `UNKNOWN` for an open that just succeeded, stranding its handle. `generation`
            // was sampled before the open, so a card pulled during it makes this publish a no-op.
            publish_result(&ops, &request.path, outcome, generation).await;
            mark_in_flight(None).await;
        }
        DIRECTORY_WAKE.wait().await;
    }
}

/// Takes the outstanding request, marks it in flight, and returns it with the card generation the
/// open begins under.
///
/// The mark is made while the [`REQUEST`] lock is held, so a racing collect reports `PENDING`,
/// never `UNKNOWN`. The generation is read here, before any I/O, so it names the card this open
/// reads; reading it at publish time would see the post-flush value.
#[cfg(any(target_os = "none", feature = "host_app"))]
async fn take_next_request() -> Option<(DirRequest, u32)> {
    let mut request = REQUEST.lock().await;
    let request = request.take()?;
    let generation = crate::storage::card::generation();
    mark_in_flight(Some(request.path.clone())).await;
    Some((request, generation))
}

/// Records (or clears) the path being opened right now.
///
/// The task calls this while holding the `REQUEST` lock. Host tests call it directly to reach
/// [`deluge_file_directory_collect`]'s IN_FLIGHT branch without running the task.
pub async fn mark_in_flight(path: Option<DirPath>) {
    *IN_FLIGHT.lock().await = path;
}

/// Publishes one finished open into the live `RESULT` slot, stamped with `generation`, unless
/// that card has already gone; closes every handle this orphans (the displaced answer's, and its
/// own if the card went).
///
/// The generation check is only an early drop: with no happens-before edge from
/// `deluge_card_flush`'s bump, it can read the pre-flush value and publish, and [`take_from`] then
/// refuses the entry. It runs under the lock so it is at least ordered against a flush that took
/// that lock.
#[cfg(any(target_os = "none", feature = "host_app"))]
pub async fn publish_result<O: DirOps>(ops: &O, key: &str, outcome: DirOutcome, generation: u32) {
    let mut result = RESULT.lock().await;
    if generation != crate::storage::card::generation() {
        // Opened on a card that has gone: close the handle here, where it can be awaited. A
        // failure is dropped too: it belongs to the old card, not to the folder now at this path.
        if let Some(orphan) = outcome.handle() {
            ops.close_dir(orphan).await;
        }
        return;
    }
    if let Some(displaced) = publish_into(&mut result, key, outcome, generation) {
        // An uncollected answer (the browser navigated away mid-listing).
        ops.close_dir(displaced).await;
    }
}

abi_pin!(deluge_file_directory_collect, deluge_file_directory_request);
#[cfg(feature = "host_app")]
abi_pin!(deluge_file_directory_set_latency_ms);
