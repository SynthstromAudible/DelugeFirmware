//! The stream channel (`libdeluge/file_stream.h`): request/collect over efatfs's stream handles.
//!
//! Lets `SampleRecorder` issue a cluster write (or open, read, truncate, close, free-space query),
//! return, and collect the answer on a later tick, while `file_stream_task` runs it on the storage
//! tier. The handle table is efatfs's `STREAM_WRITE_CTX` (8 slots); this module owns only the
//! request and its answer.
//!
//! # Safety
//!
//! A read or write request carries a pointer into the caller's buffer, not a copy, so a 32 KiB
//! cluster write needs no staging buffer. The caller must keep the buffer alive and unmodified
//! until the collect is terminal (`deluge::io::Stream::close()` drains to guarantee it). The
//! pointer is stored as a `usize` so the request is `Send`, and is turned back into a slice only
//! inside the task, for one operation.
//!
//! # Handles
//!
//! Everything behind the exports works in `STREAM_WRITE_CTX` slots. Only the exports convert,
//! through [`crate::abi::handle`]: a caller's handle becomes its slot before any key is built from
//! it, and an `Open`'s slot becomes a handle only as the collect writes it out, so a request's key
//! and its collect's key are always built from the same slot.

use core::ffi::{CStr, c_char};

use crate::channel::{
    DELUGE_CHANNEL_FAILED, DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_READY, DELUGE_CHANNEL_UNKNOWN,
    DelugeChannelStatus,
};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

use crate::abi::handle::{DelugeHandle, from_abi, to_abi};
use crate::channel::file_stream_slot::{AnswerSlot, OpKey, OpKind, Take, WritePath, still_coming};

#[cfg(has_app)]
use crate::storage::efatfs::backend as backing;

/// One outstanding operation. Buffers travel as `usize` addresses; see the module's Safety section.
#[derive(Clone, Copy)]
enum StreamOp {
    Open {
        mode: u8,
    },
    ReadAt {
        handle: u32,
        offset: u32,
        dst: usize,
        len: u32,
    },
    WriteAt {
        handle: u32,
        offset: u32,
        src: usize,
        len: u32,
    },
    Truncate {
        handle: u32,
        size: u32,
    },
    Close {
        handle: u32,
    },
    Stats,
}

/// A request waiting for the task.
struct Queued {
    op: StreamOp,
    key: OpKey,
    /// The file an `Open` names; empty for the others.
    path: WritePath,
    /// The card generation the request was made under. Its answer carries this stamp, so one that
    /// finishes after an ejection is never collected as the new card's.
    generation: u32,
}

/// Returns `op`'s key. An `Open` is keyed by its path: the recorder's create and its finalize's
/// reopen name different files, and each collects only its own answer.
fn op_key(op: &StreamOp, path: &str) -> Option<OpKey> {
    match *op {
        StreamOp::Open { .. } => OpKey::new(OpKind::Open, 0, path),
        StreamOp::ReadAt { handle, .. } => OpKey::new(OpKind::ReadAt, handle, ""),
        StreamOp::WriteAt { handle, .. } => OpKey::new(OpKind::WriteAt, handle, ""),
        StreamOp::Truncate { handle, .. } => OpKey::new(OpKind::Truncate, handle, ""),
        StreamOp::Close { handle } => OpKey::new(OpKind::Close, handle, ""),
        StreamOp::Stats => OpKey::new(OpKind::Stats, 0, ""),
    }
}

static REQUEST: Mutex<CriticalSectionRawMutex, Option<Queued>> = Mutex::new(None);
/// The operation the task is running. Kept apart from [`REQUEST`] so a collect can tell "still
/// running" (PENDING) from "never asked" (UNKNOWN).
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<OpKey>> = Mutex::new(None);
static RESULT: Mutex<CriticalSectionRawMutex, AnswerSlot> = Mutex::new(AnswerSlot::new());
/// The address of the caller's buffer the running operation uses; 0 when there is none. Set under
/// the [`REQUEST`] lock the operation is taken from and cleared once the operation is done with the
/// buffer, so [`deluge_file_stream_holds`] never sees a gap between queued and running.
static RUNNING_BUFFER: core::sync::atomic::AtomicUsize = core::sync::atomic::AtomicUsize::new(0);

/// Records (or clears) the operation running right now. Clearing it wakes the app
/// (`DELUGE_WAKE_STORAGE`): the operation's result is visible once this returns.
///
/// The task calls this while holding the `REQUEST` lock when marking an operation in flight.
/// Host tests call it directly to reach [`collect`]'s IN_FLIGHT branch without running the task.
pub async fn mark_in_flight(entry: Option<OpKey>) {
    let done = entry.is_none();
    *IN_FLIGHT.lock().await = entry;
    if done {
        crate::app_loop::wake(crate::app_loop::WAKE_STORAGE);
    }
}

/// Returns the address of the caller's buffer `op` uses; 0 for an operation without one.
fn op_buffer(op: &StreamOp) -> usize {
    match op {
        StreamOp::ReadAt { dst, .. } => *dst,
        StreamOp::WriteAt { src, .. } => *src,
        _ => 0,
    }
}

/// Returns whether a queued or running operation reads or writes through `buffer`.
///
/// A caller must not free or reuse a buffer handed to this channel while this returns true: the
/// operation cannot be cancelled. Returns true when it cannot tell (lock contended).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_stream_holds(buffer: *const core::ffi::c_void) -> bool {
    let addr = buffer as usize;
    if addr == 0 {
        return false;
    }
    let queued = match REQUEST.try_lock() {
        Ok(req) => req.as_ref().is_some_and(|q| op_buffer(&q.op) == addr),
        Err(_) => true,
    };
    queued || RUNNING_BUFFER.load(core::sync::atomic::Ordering::Acquire) == addr
}
static WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// Queues `op` if nothing is outstanding. Returns false if refused.
///
/// First drops any uncollected answer with the same key: slot indices are recycled, so otherwise a
/// recorder that inherits a slot would collect the previous recorder's answer as its own.
fn accept(op: StreamOp, path: &str) -> bool {
    let (Ok(mut req), Ok(in_flight), Ok(mut result)) =
        (REQUEST.try_lock(), IN_FLIGHT.try_lock(), RESULT.try_lock())
    else {
        return false;
    };
    // Single-flight: a second request is refused, never replaces the first. For the recorder a
    // dropped write is lost audio, so the caller must see the refusal.
    if req.is_some() || in_flight.is_some() {
        return false;
    }
    let (Some(key), Ok(stored)) = (op_key(&op, path), WritePath::try_from(path)) else {
        return false;
    };
    result.discard(&key);
    *req = Some(Queued {
        op,
        key,
        path: stored,
        generation: crate::storage::card::generation(),
    });
    WAKE.signal(());
    true
}

/// Collects `(kind, handle, path)`'s answer. `out` receives the `Ok` payload on READY, `out_error`
/// the `Err` payload on FAILED.
///
/// Looks at [`REQUEST`], [`IN_FLIGHT`] then [`RESULT`], the order an operation moves through them.
/// The task marks an operation in flight while still holding the `REQUEST` lock and publishes
/// before clearing `IN_FLIGHT`, so this pass always finds a moving operation in one of the three or
/// behind a contended lock (read as PENDING). Probing `RESULT` first could report UNKNOWN for an
/// operation that had just succeeded.
///
/// # Safety
///
/// Each out-pointer may be null; one that is not must point at a writable `u32`.
unsafe fn collect(kind: OpKind, handle: u32, path: &str, out: *mut u32, out_error: *mut u32) -> u8 {
    let Some(key) = OpKey::new(kind, handle, path) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    let queued = REQUEST
        .try_lock()
        .ok()
        .map(|r| r.as_ref().is_some_and(|q| q.key == key));
    let running = IN_FLIGHT.try_lock().ok().map(|f| f.as_ref() == Some(&key));
    if still_coming(queued, running) {
        return DELUGE_CHANNEL_PENDING;
    }
    // Contended: the task is publishing, and this operation's answer may be what it is publishing.
    let Ok(mut result) = RESULT.try_lock() else {
        return DELUGE_CHANNEL_PENDING;
    };
    let taken = result.take(&key, crate::storage::card::generation());
    if result.has_orphans() {
        WAKE.signal(());
    }
    match taken {
        Take::Ready(value) => {
            if !out.is_null() {
                // SAFETY: non-null, and the caller guarantees it is writable.
                unsafe { *out = value };
            }
            DELUGE_CHANNEL_READY
        }
        Take::Failed(error) => {
            if !out_error.is_null() {
                // SAFETY: non-null, and the caller guarantees it is writable.
                unsafe { *out_error = error };
            }
            DELUGE_CHANNEL_FAILED
        }
        // Nothing queued, nothing running, no answer for this card: ask again.
        Take::Absent => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// Milliseconds [`file_stream_task`] waits before each operation; see
/// [`deluge_file_stream_set_latency_ms`].
#[cfg(feature = "host_app")]
static LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: makes every stream-channel operation wait `ms` milliseconds before it touches
/// the card, so a test can hold an operation in flight. 0, the default, means no wait.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_stream_set_latency_ms(ms: u32) {
    LATENCY_MS.store(ms, core::sync::atomic::Ordering::Relaxed);
}

/// Whether this channel's opens skip the mount they would retry after `NO_FILESYSTEM`; see
/// [`deluge_file_stream_set_mount_refused`].
#[cfg(feature = "host_app")]
static MOUNT_REFUSED: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);

/// Host harness knob: while `refused`, an open this channel runs on an unmounted card answers
/// `NO_FILESYSTEM` instead of mounting, so a test can make an open fail with a status other than
/// `NOT_FOUND`. Other channels still mount.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_stream_set_mount_refused(refused: bool) {
    MOUNT_REFUSED.store(refused, core::sync::atomic::Ordering::Relaxed);
}

/// Opens `path` with [`open_mounted`], unless the host harness has refused the mount.
#[cfg(has_app)]
async fn channel_open(path: &str, mode: u8) -> Result<u32, i8> {
    #[cfg(feature = "host_app")]
    if MOUNT_REFUSED.load(core::sync::atomic::Ordering::Relaxed) {
        return backing::stream_open(path, mode).await;
    }
    open_mounted(path, mode).await
}

/// Requests a positional write of `len` bytes at `offset`. Returns false if refused.
///
/// # Safety
///
/// `bytes` must point at `len` readable bytes that stay alive and unmodified until the matching
/// collect is terminal.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_stream_request_write_at(
    handle: DelugeHandle,
    offset: u32,
    bytes: *const u8,
    len: u32,
) -> bool {
    let Some(handle) = from_abi(handle) else {
        return false;
    };
    if bytes.is_null() || len == 0 {
        return false;
    }
    accept(
        StreamOp::WriteAt {
            handle,
            offset,
            src: bytes as usize,
            len,
        },
        "",
    )
}

/// Collects the write; `out_written` receives the byte count. A short write is FAILED.
///
/// # Safety
///
/// `out_written` may be null; when it is not, it must point at a writable `u32`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_stream_collect_write_at(
    handle: DelugeHandle,
    out_written: *mut u32,
) -> DelugeChannelStatus {
    let Some(handle) = from_abi(handle) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    // SAFETY: forwarded contract.
    unsafe {
        collect(
            OpKind::WriteAt,
            handle,
            "",
            out_written,
            core::ptr::null_mut(),
        )
    }
}

/// Requests an open of `path` in `mode`. Returns false if refused.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_stream_request_open(path: *const c_char, mode: u8) -> bool {
    if path.is_null() {
        return false;
    }
    // SAFETY: caller guarantees NUL-termination.
    let Ok(p) = (unsafe { CStr::from_ptr(path) }).to_str() else {
        return false;
    };
    accept(StreamOp::Open { mode }, p)
}

/// Collects the open of `path`. `out_handle` receives the handle on READY, `out_error` the
/// `DelugeStatus` the open failed with on FAILED.
///
/// # Safety
///
/// `path` must be null or a valid NUL-terminated C string. Each out-pointer may be null; one that
/// is not must point at a writable object of its type.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_stream_collect_open(
    path: *const c_char,
    out_handle: *mut DelugeHandle,
    out_error: *mut i8,
) -> DelugeChannelStatus {
    if path.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: caller guarantees NUL-termination.
    let Ok(p) = (unsafe { CStr::from_ptr(path) }).to_str() else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    let mut slot = 0u32;
    let mut error = 0u32;
    // SAFETY: `slot` and `error` are locals.
    let status = unsafe { collect(OpKind::Open, 0, p, &mut slot, &mut error) };
    if status == DELUGE_CHANNEL_READY && !out_handle.is_null() {
        // SAFETY: non-null, and the caller guarantees it is writable.
        unsafe { *out_handle = to_abi(slot) };
    }
    if status == DELUGE_CHANNEL_FAILED && !out_error.is_null() {
        // The task published the status as its byte.
        // SAFETY: non-null, and the caller guarantees it is writable.
        unsafe { *out_error = error as u8 as i8 };
    }
    status
}

/// Requests a positional read of `len` bytes at `offset`. Returns false if refused.
///
/// # Safety
///
/// `dst` must point at `len` writable bytes that stay alive until the matching collect is terminal.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_stream_request_read_at(
    handle: DelugeHandle,
    offset: u32,
    dst: *mut u8,
    len: u32,
) -> bool {
    let Some(handle) = from_abi(handle) else {
        return false;
    };
    if dst.is_null() || len == 0 {
        return false;
    }
    accept(
        StreamOp::ReadAt {
            handle,
            offset,
            dst: dst as usize,
            len,
        },
        "",
    )
}

/// Collects the read; `out_read` receives the byte count, which may be short.
///
/// # Safety
///
/// `out_read` may be null; when it is not, it must point at a writable `u32`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_stream_collect_read_at(
    handle: DelugeHandle,
    out_read: *mut u32,
) -> DelugeChannelStatus {
    let Some(handle) = from_abi(handle) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    // SAFETY: forwarded contract.
    unsafe { collect(OpKind::ReadAt, handle, "", out_read, core::ptr::null_mut()) }
}

/// Requests a truncate to `size` bytes. Returns false if refused.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_stream_request_truncate(handle: DelugeHandle, size: u32) -> bool {
    let Some(handle) = from_abi(handle) else {
        return false;
    };
    accept(StreamOp::Truncate { handle, size }, "")
}

/// Collects the truncate.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_stream_collect_truncate(handle: DelugeHandle) -> DelugeChannelStatus {
    let Some(handle) = from_abi(handle) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    // SAFETY: null `out` is explicitly allowed.
    unsafe {
        collect(
            OpKind::Truncate,
            handle,
            "",
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        )
    }
}

/// Requests a close. Returns false if refused.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_stream_request_close(handle: DelugeHandle) -> bool {
    let Some(handle) = from_abi(handle) else {
        return false;
    };
    accept(StreamOp::Close { handle }, "")
}

/// Collects the close.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_stream_collect_close(handle: DelugeHandle) -> DelugeChannelStatus {
    let Some(handle) = from_abi(handle) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };
    // SAFETY: null `out` is explicitly allowed.
    unsafe {
        collect(
            OpKind::Close,
            handle,
            "",
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        )
    }
}

/// Requests the free-cluster count. Returns false if refused.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_stream_request_stats() -> bool {
    accept(StreamOp::Stats, "")
}

/// Collects the free-cluster count.
///
/// # Safety
///
/// `out_free_clusters` may be null; when it is not, it must point at a writable `u32`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_stream_collect_stats(
    out_free_clusters: *mut u32,
) -> DelugeChannelStatus {
    // SAFETY: forwarded contract.
    unsafe {
        collect(
            OpKind::Stats,
            0,
            "",
            out_free_clusters,
            core::ptr::null_mut(),
        )
    }
}

/// Opens `path`, mounting the card first if the open fails with `NO_FILESYSTEM`.
///
/// A channel request may be the first thing to touch a card inserted after boot, and nothing else
/// on this path mounts it (synchronous callers rely on `StorageManager::initSD` instead). Returns
/// the final open's status, never the mount's: callers act on it (the recorder retries its folder
/// only on `NOT_FOUND`). Also used by [`file_bytes`](crate::channel::file_bytes).
#[cfg(has_app)]
pub(crate) async fn open_mounted(path: &str, mode: u8) -> Result<u32, i8> {
    use crate::channel::file_bytes::NO_FILESYSTEM;
    match backing::stream_open(path, mode).await {
        Err(NO_FILESYSTEM) => {
            backing::mount().await.map_err(|_| NO_FILESYSTEM)?;
            backing::stream_open(path, mode).await
        }
        other => other,
    }
}

/// The stream channel's driver on the storage tier: runs one operation at a time and publishes its
/// answer.
#[cfg(has_app)]
#[embassy_executor::task]
pub async fn file_stream_task() {
    crate::tier::scoped(crate::tier::Tier::Storage, file_stream_task_body()).await
}

#[cfg(has_app)]
async fn close_orphans() {
    loop {
        let orphan = RESULT
            .lock()
            .await
            .take_orphan(crate::storage::card::generation());
        let Some(handle) = orphan else { break };
        backing::stream_close(handle).await;
    }
}

#[cfg(has_app)]
async fn file_stream_task_body() {
    loop {
        WAKE.wait().await;
        loop {
            close_orphans().await;
            let taken = {
                let mut req = REQUEST.lock().await;
                match req.take() {
                    None => None,
                    // A request from an earlier card that survived the flush's `try_lock` would
                    // touch the new card. Drop it unrun: its answer could never be collected.
                    Some(q) if q.generation != crate::storage::card::generation() => Some(None),
                    Some(q) => {
                        RUNNING_BUFFER
                            .store(op_buffer(&q.op), core::sync::atomic::Ordering::Release);
                        // Marked while the REQUEST lock is still held, so a collect always sees the
                        // operation queued, running, or behind a contended lock.
                        mark_in_flight(Some(q.key.clone())).await;
                        Some(Some(q))
                    }
                }
            };
            let Some(taken) = taken else {
                break;
            };
            let Some(queued) = taken else {
                continue;
            };
            // After marking it in flight, so a collect during the wait answers PENDING, as for a
            // slow card.
            #[cfg(feature = "host_app")]
            {
                let ms = LATENCY_MS.load(core::sync::atomic::Ordering::Relaxed);
                if ms > 0 {
                    embassy_time::Timer::after_millis(u64::from(ms)).await;
                }
            }
            let path = queued.path.as_str();
            let result = match queued.op {
                // The `Err` payload is the `DelugeStatus`'s byte; the collect turns it back into
                // the status.
                StreamOp::Open { mode } => channel_open(path, mode)
                    .await
                    .map_err(|status| u32::from(status as u8)),
                StreamOp::ReadAt {
                    handle,
                    offset,
                    dst,
                    len,
                } => {
                    // SAFETY: the caller keeps this buffer alive and unmodified until the collect
                    // is terminal (`libdeluge/file_stream.h`).
                    let d =
                        unsafe { core::slice::from_raw_parts_mut(dst as *mut u8, len as usize) };
                    // A short read is READY with its true count; the caller decides whether short
                    // is an error.
                    backing::stream_read_at_via(handle, offset, d)
                        .await
                        .map(|n| n as u32)
                        .ok_or(0)
                }
                StreamOp::WriteAt {
                    handle,
                    offset,
                    src,
                    len,
                } => {
                    // SAFETY: as above.
                    let s = unsafe { core::slice::from_raw_parts(src as *const u8, len as usize) };
                    match backing::stream_write_at(handle, offset, s).await {
                        // A short write is a failure: a partial cluster must never look written.
                        Some(n) if n as u32 == len => Ok(n as u32),
                        _ => Err(0),
                    }
                }
                StreamOp::Truncate { handle, size } => {
                    if backing::stream_truncate(handle, size).await {
                        Ok(0)
                    } else {
                        Err(0)
                    }
                }
                StreamOp::Close { handle } => {
                    if backing::stream_close(handle).await {
                        Ok(0)
                    } else {
                        Err(0)
                    }
                }
                StreamOp::Stats => backing::fs_stats_free().await.ok_or(0),
            };
            RESULT
                .lock()
                .await
                .publish(queued.key, result, queued.generation);
            mark_in_flight(None).await;
            RUNNING_BUFFER.store(0, core::sync::atomic::Ordering::Release);
        }
    }
}

/// Forgets everything asked of the card that is gone. Called by `deluge_card_flush`.
///
/// Drops a queued request (its buffer was never touched) and a waiting answer. The running
/// operation is left alone: it still uses its buffer, and its answer is stamped for the old card so
/// no collect takes it. Each clear is `try_lock`ed and may not land; the generation stamp is what
/// makes that safe.
pub(crate) fn flush_tables() {
    if let Ok(mut request) = REQUEST.try_lock() {
        *request = None;
    }
    if let Ok(mut result) = RESULT.try_lock() {
        result.drain();
    }
    WAKE.signal(());
}

abi_pin!(
    deluge_file_stream_collect_close,
    deluge_file_stream_collect_open,
    deluge_file_stream_collect_read_at,
    deluge_file_stream_collect_stats,
    deluge_file_stream_collect_truncate,
    deluge_file_stream_collect_write_at,
    deluge_file_stream_holds,
    deluge_file_stream_request_close,
    deluge_file_stream_request_open,
    deluge_file_stream_request_read_at,
    deluge_file_stream_request_stats,
    deluge_file_stream_request_truncate,
    deluge_file_stream_request_write_at,
);
#[cfg(feature = "host_app")]
abi_pin!(
    deluge_file_stream_set_latency_ms,
    deluge_file_stream_set_mount_refused
);
