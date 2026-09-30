//! Request/collect over the stream-write handle table that already exists.
//!
//! `SampleRecorder` writes one cluster per tick and once blocked the caller for the whole of each
//! write. This module drives the same `stream_write_at` future on an Embassy task so
//! the recorder can issue a write, return, and collect it on a later tick.
//!
//! There is no handle table here on purpose: `efatfs_fs`'s `STREAM_WRITE_CTX` is one already, sized
//! at `MAX_STREAM_WRITE_HANDLES` (8) and documented as the recorder's own, with a generation
//! counter per slot. This module only owns the request and its answer.
//!
//! # Safety
//!
//! The request carries a POINTER into the C++ caller's buffer, not a copy of the bytes — the same
//! zero-copy shape `streaming_fill.h`'s `dest` uses in the read direction, and the reason a 32 KiB
//! cluster write needs no staging buffer on either side. The C++ contract (`file_write.h`) is that
//! the buffer stays alive and unmodified until the collect is terminal, and
//! `deluge::io::Stream::close()` enforces it by draining before the handle goes away.
//!
//! The pointer is stored as a `usize` rather than a `*const u8` so the request is `Send`; it is
//! reconstituted only inside the task, and only for the duration of one `stream_write_at`.

use core::ffi::{CStr, c_char};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

use super::file_write_slot::{AnswerSlot, OpKey, OpKind, Take, WritePath};

/// Copied from `libdeluge/file_write.h`, because this module compiles in carriers with no bindgen
/// output; pinned by `abi_pins.rs`.
pub const DELUGE_FILE_WRITE_PENDING: u8 = 0;
/// See [`DELUGE_FILE_WRITE_PENDING`].
pub const DELUGE_FILE_WRITE_READY: u8 = 1;
/// See [`DELUGE_FILE_WRITE_PENDING`].
pub const DELUGE_FILE_WRITE_UNKNOWN: u8 = 2;
/// See [`DELUGE_FILE_WRITE_PENDING`].
pub const DELUGE_FILE_WRITE_FAILED: u8 = 3;

/// The efatfs backend this driver writes through: the device module on the target, the host shim
/// under `host_app`. One alias rather than a `#[cfg]` at every call site -- the same shape
/// `file_bytes::backing` uses, and for the same reason.
#[cfg(any(target_os = "none", feature = "host_app"))]
mod backing {
    #[cfg(target_os = "none")]
    pub(super) use crate::efatfs_fs::{
        fs_stats_free, mount, stream_close, stream_open, stream_read_at_via, stream_truncate,
        stream_write_at,
    };
    #[cfg(not(target_os = "none"))]
    pub(super) use crate::efatfs_host_shim::{
        fs_stats_free, mount, stream_close, stream_open, stream_read_at_via, stream_truncate,
        stream_write_at,
    };
}

/// One outstanding operation. Pointers travel as `usize` so this is `Send`; see the module's Safety
/// note. The caller owns every buffer named here until the matching collect is terminal.
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
    /// The card generation the request was made under. Its answer is stamped with it, so an answer that finishes
    /// after an ejection is never collected as the new card's.
    generation: u32,
}

/// `op`'s key.
fn op_key(op: &StreamOp) -> Option<OpKey> {
    match *op {
        StreamOp::Open { .. } => OpKey::new(OpKind::Open, 0, ""),
        StreamOp::ReadAt { handle, .. } => OpKey::new(OpKind::ReadAt, handle, ""),
        StreamOp::WriteAt { handle, .. } => OpKey::new(OpKind::WriteAt, handle, ""),
        StreamOp::Truncate { handle, .. } => OpKey::new(OpKind::Truncate, handle, ""),
        StreamOp::Close { handle } => OpKey::new(OpKind::Close, handle, ""),
        StreamOp::Stats => OpKey::new(OpKind::Stats, 0, ""),
    }
}

static REQUEST: Mutex<CriticalSectionRawMutex, Option<Queued>> = Mutex::new(None);
/// The operation the task is running. Kept apart from [`REQUEST`] so a collect can tell "still running" (PENDING)
/// from "never asked" (UNKNOWN).
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<OpKey>> = Mutex::new(None);
static RESULT: Mutex<CriticalSectionRawMutex, AnswerSlot> = Mutex::new(AnswerSlot::new());
/// The caller-owned buffer the running operation reads or writes, as an address; 0 when nothing is running or the
/// operation has no buffer. Set under the REQUEST lock the operation is taken from, and cleared once the operation has
/// finished with the buffer, so [`deluge_file_write_holds`] never sees a gap between queued and running.
static RUNNING_BUFFER: core::sync::atomic::AtomicUsize = core::sync::atomic::AtomicUsize::new(0);

/// The caller-owned buffer `op` reads from or writes into, as an address; 0 for an operation without one.
fn op_buffer(op: &StreamOp) -> usize {
    match op {
        StreamOp::ReadAt { dst, .. } => *dst,
        StreamOp::WriteAt { src, .. } => *src,
        _ => 0,
    }
}

/// C-ABI: whether a queued or running operation reads or writes through `buffer`.
///
/// A caller must not free or reuse a buffer it handed to this channel while this answers true: the operation cannot
/// be cancelled, and it touches the buffer whenever it runs. Answers true when it cannot tell, so the caller keeps the
/// buffer a little longer rather than freeing it early.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_write_holds(buffer: *const core::ffi::c_void) -> bool {
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

/// Accept an operation if nothing is in flight.
///
/// Drops an uncollected answer with the SAME key first. `STREAM_WRITE_CTX` recycles slot indices, so without
/// this, a recorder that inherits slot N would collect the previous recorder's answer as its own.
fn accept(op: StreamOp, path: &str) -> bool {
    let (Ok(mut req), Ok(in_flight), Ok(mut result)) =
        (REQUEST.try_lock(), IN_FLIGHT.try_lock(), RESULT.try_lock())
    else {
        return false;
    };
    // Single-flight. A second request while one is outstanding is REFUSED, never replaced: for the recorder a
    // dropped write is lost audio, so the caller must see the refusal.
    if req.is_some() || in_flight.is_some() {
        return false;
    }
    let (Some(key), Ok(stored)) = (op_key(&op), WritePath::try_from(path)) else {
        return false;
    };
    result.discard(&key);
    *req = Some(Queued {
        op,
        key,
        path: stored,
        generation: deluge_card_generation::current(),
    });
    WAKE.signal(());
    true
}

/// Collect `(kind, handle)`'s answer. `out` receives the `Ok` payload on READY.
///
/// # Safety
/// `out` may be null; when it is not, it must point at a writable `u32`.
unsafe fn collect(kind: OpKind, handle: u32, out: *mut u32) -> u8 {
    let Some(key) = OpKey::new(kind, handle, "") else {
        return DELUGE_FILE_WRITE_UNKNOWN;
    };
    if let Ok(mut result) = RESULT.try_lock() {
        let taken = result.take(&key, deluge_card_generation::current());
        if result.has_orphans() {
            WAKE.signal(());
        }
        match taken {
            Take::Ready(value) => {
                if !out.is_null() {
                    // SAFETY: non-null, and the caller guarantees it is writable.
                    unsafe { *out = value };
                }
                return DELUGE_FILE_WRITE_READY;
            }
            Take::Failed => return DELUGE_FILE_WRITE_FAILED,
            Take::Absent => {}
        }
    }
    let queued = REQUEST
        .try_lock()
        .map(|r| r.as_ref().is_some_and(|q| q.key == key))
        .unwrap_or(false);
    let running = IN_FLIGHT
        .try_lock()
        .map(|f| f.as_ref() == Some(&key))
        .unwrap_or(false);
    if queued || running {
        DELUGE_FILE_WRITE_PENDING
    } else {
        // Nothing queued, nothing running, no answer for this card: ask again.
        DELUGE_FILE_WRITE_UNKNOWN
    }
}

/// Milliseconds `file_write_task` waits before each operation. Host harness only; see
/// [`deluge_file_write_set_latency_ms`].
#[cfg(feature = "host_app")]
static LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: make every write-channel operation wait `ms` milliseconds before it touches the card, so a gate
/// can hold an operation in flight while a consumer does something else. 0, the default, means no wait.
///
/// Compiled only under `host_app`: the device image has neither this function nor the static it sets.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_write_set_latency_ms(ms: u32) {
    LATENCY_MS.store(ms, core::sync::atomic::Ordering::Relaxed);
}

/// C-ABI: ask for a write.
///
/// # Safety
/// `bytes` must point at `len` readable bytes that stay alive and unmodified until the matching
/// collect is terminal.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_write_request_write_at(
    handle: u32,
    offset: u32,
    bytes: *const u8,
    len: u32,
) -> bool {
    // NOTE: handle 0 is VALID here. This table is `STREAM_WRITE_CTX`, whose slots are indexed from
    // zero -- which is exactly why `deluge::io::Stream` boxes `handle + 1` into its opaque pointer.
    // The `0 = invalid` convention belongs to `sample_stream.h`'s registry, not this one.
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

/// C-ABI: collect the write.
///
/// # Safety
/// `out_written` may be null; when it is not, it must point at a writable `u32`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_write_collect_write_at(
    handle: u32,
    out_written: *mut u32,
) -> u8 {
    // SAFETY: forwarded contract.
    unsafe { collect(OpKind::WriteAt, handle, out_written) }
}

/// C-ABI: ask for an open.
///
/// # Safety
/// `path` must be a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_write_request_open(path: *const c_char, mode: u8) -> bool {
    if path.is_null() {
        return false;
    }
    // SAFETY: caller guarantees NUL-termination.
    let Ok(p) = (unsafe { CStr::from_ptr(path) }).to_str() else {
        return false;
    };
    accept(StreamOp::Open { mode }, p)
}

/// C-ABI: collect the open.
///
/// # Safety
/// `out_handle` may be null; when it is not, it must point at a writable `u32`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_write_collect_open(out_handle: *mut u32) -> u8 {
    // SAFETY: forwarded contract.
    unsafe { collect(OpKind::Open, 0, out_handle) }
}

/// C-ABI: ask for a positional read.
///
/// # Safety
/// `dst` must point at `len` writable bytes that stay alive until the matching collect is terminal.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_write_request_read_at(
    handle: u32,
    offset: u32,
    dst: *mut u8,
    len: u32,
) -> bool {
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

/// C-ABI: collect the read.
///
/// # Safety
/// `out_read` may be null; when it is not, it must point at a writable `u32`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_write_collect_read_at(handle: u32, out_read: *mut u32) -> u8 {
    // SAFETY: forwarded contract.
    unsafe { collect(OpKind::ReadAt, handle, out_read) }
}

/// C-ABI: ask for a truncate.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_write_request_truncate(handle: u32, size: u32) -> bool {
    accept(StreamOp::Truncate { handle, size }, "")
}

/// C-ABI: collect the truncate.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_write_collect_truncate(handle: u32) -> u8 {
    // SAFETY: null `out` is explicitly allowed.
    unsafe { collect(OpKind::Truncate, handle, core::ptr::null_mut()) }
}

/// C-ABI: ask for a close.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_write_request_close(handle: u32) -> bool {
    accept(StreamOp::Close { handle }, "")
}

/// C-ABI: collect the close.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_write_collect_close(handle: u32) -> u8 {
    // SAFETY: null `out` is explicitly allowed.
    unsafe { collect(OpKind::Close, handle, core::ptr::null_mut()) }
}

/// C-ABI: ask for the free-cluster count.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_write_request_stats() -> bool {
    accept(StreamOp::Stats, "")
}

/// C-ABI: collect the free-cluster count.
///
/// # Safety
/// `out_free_clusters` may be null; when it is not, it must point at a writable `u32`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_write_collect_stats(out_free_clusters: *mut u32) -> u8 {
    // SAFETY: forwarded contract.
    unsafe { collect(OpKind::Stats, 0, out_free_clusters) }
}

/// Open `path`, mounting the card first if it turns out not to be mounted.
///
/// The same lazy shape `file_bytes::open_mounted` uses, and for the same reason: the caller may be
/// the first thing to touch a card inserted after boot, and on the CHANNEL arm nothing else will have
/// mounted it. (The synchronous arm is different -- it reaches efatfs directly and relies on
/// `StorageManager::initSD` having mounted already, which is why that call cannot simply be deleted.)
///
/// `stream_open` collapses "no filesystem" and a real failure into one `None`, so this retries once
/// on any failure rather than only on the unmounted case. `mount()` is idempotent, so a retry after a
/// genuine error costs one wasted call and still reports the error.
#[cfg(any(target_os = "none", feature = "host_app"))]
async fn open_mounted(path: &str, mode: u8) -> Option<u32> {
    if let Some(handle) = backing::stream_open(path, mode).await {
        return Some(handle);
    }
    backing::mount().await.ok()?;
    backing::stream_open(path, mode).await
}

/// The driver. Takes one operation at a time and publishes its answer.
#[cfg(any(target_os = "none", feature = "host_app"))]
#[embassy_executor::task]
pub async fn file_write_task() {
    crate::tier::scoped(crate::tier::Tier::Storage, file_write_task_body()).await
}

#[cfg(any(target_os = "none", feature = "host_app"))]
async fn close_orphans() {
    loop {
        let orphan = RESULT
            .lock()
            .await
            .take_orphan(deluge_card_generation::current());
        let Some(handle) = orphan else { break };
        backing::stream_close(handle).await;
    }
}

#[cfg(any(target_os = "none", feature = "host_app"))]
async fn file_write_task_body() {
    loop {
        WAKE.wait().await;
        loop {
            close_orphans().await;
            let Some(queued) = ({
                let mut req = REQUEST.lock().await;
                let taken = req.take();
                if let Some(q) = taken.as_ref() {
                    RUNNING_BUFFER.store(op_buffer(&q.op), core::sync::atomic::Ordering::Release);
                }
                taken
            }) else {
                break;
            };
            // A request from before the card changed that survived the flush (its `try_lock` lost the race) names
            // the old card: running it would touch the new card. Its answer could never be collected, so none is
            // published.
            if queued.generation != deluge_card_generation::current() {
                RUNNING_BUFFER.store(0, core::sync::atomic::Ordering::Release);
                continue;
            }
            *IN_FLIGHT.lock().await = Some(queued.key.clone());
            // After marking it in flight, so a collect during the wait answers PENDING, exactly as it would while
            // a slow card was working.
            #[cfg(feature = "host_app")]
            {
                let ms = LATENCY_MS.load(core::sync::atomic::Ordering::Relaxed);
                if ms > 0 {
                    embassy_time::Timer::after_millis(u64::from(ms)).await;
                }
            }
            let path = queued.path.as_str();
            let result = match queued.op {
                StreamOp::Open { mode } => open_mounted(path, mode).await.ok_or(0),
                StreamOp::ReadAt {
                    handle,
                    offset,
                    dst,
                    len,
                } => {
                    // SAFETY: the C++ contract (file_write.h) is that this buffer stays alive and
                    // unmodified until the collect below is observed as terminal.
                    let d =
                        unsafe { core::slice::from_raw_parts_mut(dst as *mut u8, len as usize) };
                    // A SHORT read is reported as READY with its TRUE count; the caller decides
                    // whether short is an error. Reporting it as FAILED would lose the distinction
                    // between "nothing happened" and "less than asked for".
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
                        // A short WRITE is a failure, not a small success: a partial cluster must
                        // never look written.
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
            *IN_FLIGHT.lock().await = None;
            RUNNING_BUFFER.store(0, core::sync::atomic::Ordering::Release);
        }
    }
}

/// Forget everything asked of the card that is gone. Called by `deluge_card_flush`.
///
/// Drops a queued request (its buffer was never touched, so `deluge_file_write_holds` rightly stops naming it)
/// and a waiting answer. Leaves the running operation alone: it is still using its buffer, and its answer arrives
/// stamped for the old card, so no collect takes it. Each clear is `try_lock`ed and may not land; the generation
/// stamp is what makes that safe.
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
    deluge_file_write_collect_close,
    deluge_file_write_collect_open,
    deluge_file_write_collect_read_at,
    deluge_file_write_collect_stats,
    deluge_file_write_collect_truncate,
    deluge_file_write_collect_write_at,
    deluge_file_write_holds,
    deluge_file_write_request_close,
    deluge_file_write_request_open,
    deluge_file_write_request_read_at,
    deluge_file_write_request_stats,
    deluge_file_write_request_truncate,
    deluge_file_write_request_write_at,
);
#[cfg(feature = "host_app")]
abi_pin!(deluge_file_write_set_latency_ms);
