//! The single-slot prefix channel: reads the first N bytes of a file on the storage tier for a
//! caller that must not wait on the card.
//!
//! The C side is `libdeluge/file_prefix.h`. Callers request with [`deluge_file_prefix_request`] and
//! poll [`deluge_file_prefix_collect`] on later ticks. The callers are the song browser's artwork
//! preview and the DX7 cartridge load.
//!
//! # One slot, not a table
//!
//! There is one browser and one cursor, so at most one prefix is wanted at a time: a newer request
//! replaces an older one, and there is no depth or eviction policy.
//!
//! # Tier contract
//!
//! The task shares the audio loader's executor and its rule: it holds the efatfs FS mutex across
//! SD awaits, so it never spins and never calls C++ (see [`audio_loader`](super::audio_loader)).
//! Its filesystem operations are the loader's [`LoadOps`], so that rule is stated in one trait.
//!
//! # Card swaps
//!
//! The slot is keyed by path, so a prefix read from card A must not be served for card B's file at
//! the same path. Each read is stamped with the [card generation](crate::storage::card::generation)
//! it began under, and [`take_from`] serves an entry only when its stamp matches the taker's
//! current generation. `deluge_card_flush`'s clears (`flush_tables`) are `try_lock`ed and may
//! fail, and a read in flight across the flush publishes afterwards, so the check at take is the
//! guarantee.
//!
//! # Build coverage
//!
//! The slot logic, the driver ([`prefix_io_once`]), the statics and the C ABI build everywhere and
//! are the only definitions. The task and its work buffer build only on device and under
//! `host_app`. The two [`PREFIX_CAPACITY`] byte buffers (`RESULT_BYTES` and `WORK_BYTES`) are in
//! `.sdram_bss` on device, because on-chip SRAM is shared with the C++ `FAST_INTERNAL` heap.

use core::ffi::{CStr, c_char};

use crate::channel::{
    DELUGE_CHANNEL_FAILED, DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_READY, DELUGE_CHANNEL_UNKNOWN,
    DELUGE_PATH_MAX, DelugeChannelStatus,
};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::{Mutex, MutexGuard};
use embassy_sync::signal::Signal;

use crate::channel::audio_loader::{LoadOps, OpenVerdict};

/// The most bytes one prefix request may ask for, and the size of the result slot;
/// `DELUGE_FILE_PREFIX_CAPACITY` in C, pinned by `abi::pins`.
///
/// Sized for the largest consumer, the DX7 cartridge load, not the song preview's smaller
/// `kSongPreviewWindow`. [`deluge_file_prefix_request`] clamps `max_bytes` to this, so a read
/// always fits the slot.
pub const PREFIX_CAPACITY: usize = 8192;

/// Longest path this channel keys on, in characters: `DELUGE_PATH_MAX` minus the NUL.
pub(crate) const MAX_PATH_CHARS: usize = DELUGE_PATH_MAX - 1;

/// One path, stored inline. Nothing in this module may touch the Rust global heap, which assumes
/// single-threaded use while this task preempts thread mode.
pub type PrefixPath = heapless::String<MAX_PATH_CHARS>;

/// How many bytes of the store are the prefix, or `None` when the read failed.
///
/// A failure is published as an entry, not left absent: absence reads as `UNKNOWN` ("request it
/// again"), so an unreadable file would be re-read every tick. It is distinct from `Some(0)` (an
/// empty file) because the caller shows an error for one and blank artwork for the other. It
/// carries no handle, so it never orphans anything.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct PrefixLen(pub Option<usize>);

impl crate::channel::Answer for PrefixLen {}

/// What [`take_from`] found in the slot for the path it was asked about.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum PrefixTake {
    /// This many bytes were copied into the taker's buffer; 0 for an empty file.
    Bytes(usize),
    /// The file would not open, could not be sized, or the read failed. Nothing was written.
    Failed,
}

/// The one prefix this channel holds, if any: an [`AnswerSlot`](crate::channel::AnswerSlot) keyed
/// by path.
///
/// Passed to [`publish_into`] and [`take_from`] by reference so the slot logic is testable without
/// the statics. It holds only the entry; the bytes live in a store passed beside it
/// (`RESULT_BYTES` in production), which can go to `.sdram_bss` whereas the slot cannot (an
/// empty `Option` is not all-zero bytes).
pub struct PrefixSlot {
    inner: crate::channel::AnswerSlot<PrefixPath, PrefixLen>,
}

impl PrefixSlot {
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

impl Default for PrefixSlot {
    fn default() -> Self {
        Self::new()
    }
}

/// Publishes `bytes` under `key` into `slot` and `store`, stamped with `generation`, replacing
/// whatever the slot held.
///
/// Both halves are written in one call, so a take never pairs this entry with another publish's
/// bytes. `bytes` longer than [`PREFIX_CAPACITY`] is a driver bug (requests are clamped) and
/// trips a debug assertion. A key too long to store clears the slot (see `new_entry`).
pub fn publish_into(
    slot: &mut PrefixSlot,
    store: &mut [u8; PREFIX_CAPACITY],
    key: &str,
    bytes: &[u8],
    generation: u32,
) {
    debug_assert!(bytes.len() <= PREFIX_CAPACITY);
    let len = bytes.len().min(PREFIX_CAPACITY);
    if new_entry(slot, key, generation, Some(len)) {
        store[..len].copy_from_slice(&bytes[..len]);
    }
}

/// Publishes "the read for `key` failed", stamped with `generation`, replacing whatever the slot
/// held. An over-long key clears the slot, as in [`publish_into`].
pub fn publish_failure_into(slot: &mut PrefixSlot, key: &str, generation: u32) {
    let _ = new_entry(slot, key, generation, None);
}

/// Installs a fresh entry under `key` and returns `true`, or clears the slot and returns `false`
/// when the key does not fit.
///
/// Unreachable through the FFI, which refuses over-long paths; clearing rather than keeping the
/// previous entry avoids leaving a stale neighbour in place of an untakeable one.
fn new_entry(slot: &mut PrefixSlot, key: &str, generation: u32, len: Option<usize>) -> bool {
    let Ok(key) = PrefixPath::try_from(key) else {
        slot.inner.drain(); // a `PrefixLen` carries no handle, so there is no orphan
        return false;
    };
    slot.inner.publish(key, PrefixLen(len), generation);
    true
}

/// Takes `path`'s entry if the slot holds one stamped `generation`, copying its bytes from `store`
/// into `out`.
///
/// Follows [`AnswerSlot::take`](crate::channel::AnswerSlot::take): a mismatched key is left alone;
/// a stale stamp is removed and reported as `None` (the collector's `UNKNOWN`, "request it
/// again").
///
/// An `out` shorter than the prefix is a caller bug (the header requires at least
/// [`PREFIX_CAPACITY`]). The entry is still consumed and `Bytes(0)` returned: not
/// [`PrefixTake::Failed`], which would show an SD error for a readable file, and not left
/// resident, which would make the caller re-request and re-read the card every tick.
pub fn take_from(
    slot: &mut PrefixSlot,
    store: &[u8; PREFIX_CAPACITY],
    path: &str,
    generation: u32,
    out: &mut [u8],
) -> Option<PrefixTake> {
    let len = match slot.inner.take(path, generation) {
        // Key mismatch or a stale stamp; a prefix has no handle to close.
        crate::channel::Take::Absent { .. } => return None,
        // A published failure, consumed like any other answer; `out` is untouched.
        crate::channel::Take::Ready(PrefixLen(None)) => return Some(PrefixTake::Failed),
        crate::channel::Take::Ready(PrefixLen(Some(len))) => len,
    };
    if out.len() < len {
        // Caller bug; terminal. See the doc comment.
        return Some(PrefixTake::Bytes(0));
    }
    out[..len].copy_from_slice(&store[..len]);
    Some(PrefixTake::Bytes(len))
}

/// One request waiting to be read.
struct PrefixRequest {
    path: PrefixPath,
    /// Already clamped to [`PREFIX_CAPACITY`]. Unread in builds without the task.
    #[allow(dead_code)]
    max_bytes: u32,
}

/// Wakes `file_prefix_task` when a prefix is requested. Signalling with no task waiting is a
/// harmless flag set.
pub static PREFIX_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// The one outstanding request, if any. A newer request replaces an older one; the dropped
/// request's caller reads `UNKNOWN` and re-requests only if it still wants it.
static REQUEST: Mutex<CriticalSectionRawMutex, Option<PrefixRequest>> = Mutex::new(None);

/// The path being read right now, if any, so [`deluge_file_prefix_collect`] answers `PENDING`
/// (not `UNKNOWN`, which would make the caller re-request running work) between a request leaving
/// [`REQUEST`] and its bytes reaching [`RESULT`].
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<PrefixPath>> = Mutex::new(None);

/// The finished prefix waiting to be collected. See [`PrefixSlot`].
static RESULT: Mutex<CriticalSectionRawMutex, PrefixSlot> = Mutex::new(PrefixSlot::new());

/// The published prefix's bytes, paired with the entry in [`RESULT`] and reached only through
/// [`result_parts`].
///
/// In `.sdram_bss` on device (NOLOAD, zeroed by `boot_mem::init_sdram_memory()` before any task
/// runs); a zeroed byte array is valid, unlike the `Mutex` slots, which stay on-chip.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static mut RESULT_BYTES: [u8; PREFIX_CAPACITY] = [0; PREFIX_CAPACITY];

/// Returns the slot and its bytes as disjoint borrows of the held [`RESULT`] guard, so the borrow
/// checker proves the lock is held for every use of the bytes.
fn result_parts<'g>(
    guard: &'g mut MutexGuard<'_, CriticalSectionRawMutex, PrefixSlot>,
) -> (&'g mut PrefixSlot, &'g mut [u8; PREFIX_CAPACITY]) {
    // SAFETY: nothing else names RESULT_BYTES, and a reference is handed out only while an
    // exclusive borrow of the RESULT guard (the only lock over a `PrefixSlot` here) is held, so
    // this is the only live reference.
    let store = unsafe { &mut *core::ptr::addr_of_mut!(RESULT_BYTES) };
    (&mut **guard, store)
}

/// The read in progress, before it is published; owned by `file_prefix_task` through
/// [`claim_work_bytes`]. Placed like [`RESULT_BYTES`].
#[cfg(has_app)]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static mut WORK_BYTES: [u8; PREFIX_CAPACITY] = [0; PREFIX_CAPACITY];

/// Grants [`WORK_BYTES`] once.
#[cfg(has_app)]
static WORK_CLAIM: crate::channel::file_bytes::ClaimOnce =
    crate::channel::file_bytes::ClaimOnce::new();

/// Returns the work buffer to the first caller only; `file_prefix_task` claims it once, at start.
#[cfg(has_app)]
fn claim_work_bytes() -> Option<&'static mut [u8; PREFIX_CAPACITY]> {
    if !WORK_CLAIM.claim() {
        return None;
    }
    // SAFETY: WORK_CLAIM grants one claim for the life of the program and nothing else names
    // WORK_BYTES, so this is the only reference that will ever exist.
    Some(unsafe { &mut *core::ptr::addr_of_mut!(WORK_BYTES) })
}

/// Clears the request, the in-flight mark and the result.
///
/// Called by `deluge_card_flush` after it bumps the card generation. It runs synchronously from
/// C++ and must never wait, so each clear is a `try_lock` that may fail harmlessly: a surviving
/// entry carries its old stamp and [`take_from`] drops it; a surviving in-flight mark is cleared
/// by its read; a surviving request has done no I/O and is stamped with whichever card it reads.
pub(crate) fn flush_tables() {
    if let Ok(mut request) = REQUEST.try_lock() {
        *request = None;
    }
    if let Ok(mut in_flight) = IN_FLIGHT.try_lock() {
        *in_flight = None;
    }
    if let Ok(mut result) = RESULT.try_lock() {
        result.inner.drain(); // a `PrefixLen` carries no handle, so there is no orphan
    }
}

/// Milliseconds `file_prefix_task` waits after marking each read in flight. See
/// [`deluge_file_prefix_set_latency_ms`].
#[cfg(feature = "host_app")]
static LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: makes every prefix read wait `ms` milliseconds after it is marked in flight,
/// so a test can hold a read while a consumer keeps collecting `PENDING`. 0 (the default) means no
/// wait.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_prefix_set_latency_ms(ms: u32) {
    LATENCY_MS.store(ms, core::sync::atomic::Ordering::Relaxed);
}

/// Requests the first `max_bytes` (clamped to [`PREFIX_CAPACITY`]) of `file_path` and wakes the
/// reader. See `file_prefix.h`.
///
/// An unusable request (NULL, non-UTF-8, over-long, or the slot contended) is dropped silently:
/// the next collect reports `UNKNOWN` and the caller re-requests.
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_prefix_request(file_path: *const c_char, max_bytes: u32) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return;
    };
    // Clamped once, here, so downstream an over-long prefix is a driver bug, not a runtime case.
    let max_bytes = max_bytes.min(PREFIX_CAPACITY as u32);
    enqueue_request(path, max_bytes);
}

/// Installs the request, replacing any not yet picked up, and wakes the reader. `try_lock`s: it
/// is called synchronously from C++ in any context and must never wait.
fn enqueue_request(path: PrefixPath, max_bytes: u32) {
    let Ok(mut request) = REQUEST.try_lock() else {
        return; // Contended; the caller re-requests on its next tick.
    };
    *request = Some(PrefixRequest { path, max_bytes });
    drop(request);
    PREFIX_WAKE.signal(());
}

/// Collects the prefix read for `file_path`, if one is ready. See `file_prefix.h`.
///
/// # Probe order
///
/// An entry only moves forward, `REQUEST` → `IN_FLIGHT` → `RESULT`, and the probe reads in
/// that order, under the audio loader's three rules (see
/// [`deluge_audio_load_collect`](super::audio_loader::deluge_audio_load_collect)):
///
/// 1. `take_next_request` sets `IN_FLIGHT` while still holding the `REQUEST` lock.
/// 2. The task publishes to `RESULT` before clearing `IN_FLIGHT`.
/// 3. `is_requested` and `is_in_flight` report `true` when their lock is contended; only the
///    held lock covers the REQUEST → IN_FLIGHT handoff, when the entry is in neither.
///
/// Reading in reverse order can report `UNKNOWN` for a read that has already landed.
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string; `out_buf` must point to at least `out_cap`
/// writable bytes and `out_len` to a writable `uint32_t`. Both outputs are written only on
/// [`DELUGE_CHANNEL_READY`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_prefix_collect(
    file_path: *const c_char,
    out_buf: *mut u8,
    out_cap: u32,
    out_len: *mut u32,
) -> DelugeChannelStatus {
    if out_buf.is_null() || out_len.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    // The header's requirement. A short buffer degrades silently in release (`take_from` reports an
    // empty prefix), so a debug build fails here, at the cause.
    debug_assert!(
        out_cap as usize >= PREFIX_CAPACITY,
        "`out_cap` must be at least DELUGE_FILE_PREFIX_CAPACITY"
    );
    // SAFETY: `out_buf` is non-null (checked above) and the caller promises `out_cap` writable
    // bytes there.
    let out = unsafe { core::slice::from_raw_parts_mut(out_buf, out_cap as usize) };

    match probe(&path, out) {
        Probe::Ready(len) => {
            // SAFETY: `out_len` is non-null (checked above) and, by the caller's contract, a
            // writable, aligned `uint32_t`. Written only on READY, as the header promises.
            unsafe { *out_len = len as u32 };
            DELUGE_CHANNEL_READY
        }
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
        Probe::Failed => DELUGE_CHANNEL_FAILED,
    }
}

/// What one forward pass over the channel found.
///
/// Produced by a single pass: separate "ready?" and "still coming?" passes would let the reader
/// publish between them and report `UNKNOWN` for a prefix that has landed.
enum Probe {
    /// `out` has been written with this many bytes. Only this case writes it: `file_prefix.h` lets
    /// a caller pass an uninitialised buffer and read it only on READY.
    Ready(usize),
    Pending,
    Unknown,
    /// The read failed; terminal (`DELUGE_CHANNEL_FAILED`), and distinct from an empty file.
    Failed,
}

/// One forward pass: REQUEST, then IN_FLIGHT, then RESULT. See [`deluge_file_prefix_collect`].
fn probe(path: &str, out: &mut [u8]) -> Probe {
    if is_requested(path) || is_in_flight(path) {
        return Probe::Pending;
    }
    // A contended slot means the reader is mid-publish: `Unknown` would make the caller re-request
    // work that is about to land.
    let Ok(mut result) = RESULT.try_lock() else {
        return Probe::Pending;
    };
    // The generation is read at the take: the collector's own view of which card is in the slot.
    let (slot, store) = result_parts(&mut result);
    match take_from(slot, store, path, crate::storage::card::generation(), out) {
        Some(PrefixTake::Bytes(len)) => Probe::Ready(len),
        Some(PrefixTake::Failed) => Probe::Failed,
        None => Probe::Unknown,
    }
}

/// Whether `path` is the request not yet picked up.
///
/// Contention reports `true`, deliberately: the reader marks a request in flight while holding
/// this lock, so `false` here would report `UNKNOWN` for a request mid-handoff.
fn is_requested(path: &str) -> bool {
    match REQUEST.try_lock() {
        Ok(request) => request.as_ref().is_some_and(|r| r.path == path),
        Err(_) => true,
    }
}

/// Whether `path` is the read running right now. Contention reports `true`, as in
/// [`is_requested`].
fn is_in_flight(path: &str) -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight.as_deref() == Some(path),
        Err(_) => true,
    }
}

/// Copies a C string into a [`PrefixPath`]. Returns `None` for NULL, non-UTF-8, or a path longer
/// than [`MAX_PATH_CHARS`]; a truncated path would name a different file.
///
/// # Safety
///
/// `ptr` must be NULL or a valid NUL-terminated C string.
unsafe fn cstr_to_path(ptr: *const c_char) -> Option<PrefixPath> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: the caller's contract, above.
    let cstr = unsafe { CStr::from_ptr(ptr) };
    PrefixPath::try_from(cstr.to_str().ok()?).ok()
}

/// Reads up to `max_bytes` of `path`'s opening bytes into `out`, returning how many landed.
///
/// `open`, `size_of`, one `read_at(0, ..)`, `close`; the handle is closed on every path. `None`
/// means the read failed (no open, no size, or a read error) and is published as
/// `DELUGE_CHANNEL_FAILED`; `Some(0)` is an empty file, published as READY with zero bytes.
pub async fn prefix_io_once<O: LoadOps>(
    ops: &O,
    path: &str,
    max_bytes: u32,
    out: &mut [u8],
) -> Option<usize> {
    let handle = match ops.open(path).await {
        OpenVerdict::Opened(handle) => handle,
        // Both publish a terminal failure. `TableFull` says nothing about this file, so a
        // readable song can show an error popup while the handle table is full (the next visit
        // re-asks). Making it transient, as the audio loader's `TOO_MANY_OPEN_STREAMS` is, would
        // need a terminal-vs-transient bit in `file_prefix.h` and a bounded retry; not built.
        OpenVerdict::Missing | OpenVerdict::TableFull => return None,
    };
    let read = read_prefix(ops, handle, max_bytes, out).await;
    ops.close(handle).await;
    read
}

/// Fills `out` from the front of the file behind `handle`. Never closes it.
async fn read_prefix<O: LoadOps>(
    ops: &O,
    handle: u32,
    max_bytes: u32,
    out: &mut [u8],
) -> Option<usize> {
    let size = ops.size_of(handle).await?;
    // Bounded by the file size too: `LoadOps::read_at` fails, rather than filling short, past the
    // end of the file.
    let want = size.min(max_bytes) as usize;
    // Cannot bite (requests are clamped to the buffer size), but clamping keeps a smaller `out`
    // from panicking on the executor.
    debug_assert!(want <= out.len());
    let len = want.min(out.len());
    // A zero-length read (an empty file) trivially succeeds.
    if !ops.read_at(handle, 0, &mut out[..len]).await {
        return None;
    }
    Some(len)
}

/// The prefix reader: wakes on [`PREFIX_WAKE`], serves the outstanding request through the audio
/// loader's [`ProdOps`](crate::channel::audio_loader::ProdOps), and publishes the result, on the
/// storage tier.
///
/// `IN_FLIGHT` is set before the I/O and cleared only after the result is published, so
/// [`deluge_file_prefix_collect`] never sees a running read as unknown. A failed read is published
/// as a failure (see [`PrefixLen`]).
#[cfg(has_app)]
#[embassy_executor::task]
pub async fn file_prefix_task() {
    crate::tier::scoped(crate::tier::Tier::Storage, file_prefix_task_body()).await
}

#[cfg(has_app)]
async fn file_prefix_task_body() {
    PREFIX_WAKE.wait().await;
    let ops = crate::channel::audio_loader::ProdOps::new();
    // A static buffer: an 8 KiB array held across the awaits would sit in this task's pool, in
    // on-chip SRAM.
    let Some(work) = claim_work_bytes() else {
        debug_assert!(
            false,
            "file_prefix_task claims its work buffer once; its pool size is 1"
        );
        return;
    };
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
            let read = prefix_io_once(&ops, &request.path, request.max_bytes, &mut work[..]).await;
            // Publish before clearing IN_FLIGHT: the reverse leaves a window in which `collect`
            // reports `UNKNOWN` for a read that just succeeded. `generation` was sampled before the
            // I/O, so a card pulled during it makes this result uncollectable.
            publish_result(&request.path, read.map(|len| &work[..len]), generation).await;
            mark_in_flight(None).await;
        }
        PREFIX_WAKE.wait().await;
    }
}

/// Takes the outstanding request, marks it in flight, and returns it with the card generation the
/// read begins under.
///
/// The mark is made while the [`REQUEST`] lock is held, so a racing collect reports `PENDING`,
/// never `UNKNOWN`. The generation is read here, before any I/O, so it names the card this read
/// reads; reading it at publish time would see the post-flush value.
#[cfg(has_app)]
async fn take_next_request() -> Option<(PrefixRequest, u32)> {
    let mut request = REQUEST.lock().await;
    let request = request.take()?;
    let generation = crate::storage::card::generation();
    mark_in_flight(Some(request.path.clone())).await;
    Some((request, generation))
}

/// Records (or clears) the path being read right now.
///
/// The task calls this while holding the `REQUEST` lock. Host tests call it directly to reach
/// [`deluge_file_prefix_collect`]'s IN_FLIGHT branch without running the task.
pub async fn mark_in_flight(path: Option<PrefixPath>) {
    *IN_FLIGHT.lock().await = path;
}

/// Publishes one finished read into the live `RESULT` slot (`Some(bytes)` for a prefix, `None`
/// for a failure), stamped with `generation`, unless that card has already gone.
///
/// The check is only an early drop: with no happens-before edge from `deluge_card_flush`'s bump,
/// it can read the pre-flush value and publish, and [`take_from`] then drops the entry. It runs
/// under the lock so it is at least ordered against a flush that took that lock.
pub async fn publish_result(key: &str, bytes: Option<&[u8]>, generation: u32) {
    let mut result = RESULT.lock().await;
    if generation != crate::storage::card::generation() {
        // Read from a card that has gone; the collector gets `UNKNOWN` and re-requests. A failure
        // is dropped too: it belongs to the old card, not to the file now at this path.
        return;
    }
    let (slot, store) = result_parts(&mut result);
    match bytes {
        Some(bytes) => publish_into(slot, store, key, bytes, generation),
        None => publish_failure_into(slot, key, generation),
    }
}

abi_pin!(deluge_file_prefix_collect, deluge_file_prefix_request);
#[cfg(feature = "host_app")]
abi_pin!(deluge_file_prefix_set_latency_ms);
