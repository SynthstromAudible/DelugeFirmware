//! Request/collect over the stream-write handle table that already exists.
//!
//! `SampleRecorder` writes one cluster per tick and, until now, blocked the storage owner for the
//! whole of each write. This module drives the same `stream_write_at` future on an Embassy task so
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

#![cfg(feature = "async_file_write")]

use core::ffi::{CStr, c_char};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

/// Hand-duplicated from `libdeluge/file_write.h`; pinned against it by `static_assert` in
/// `file_write_abi.cpp`, because bindgen's allowlist covers types and not macros.
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
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
mod backing {
    #[cfg(target_os = "none")]
    pub(super) use crate::efatfs_fs::{
        fs_stats_free, stream_close, stream_open, stream_read_at_via, stream_truncate,
        stream_write_at,
    };
    #[cfg(not(target_os = "none"))]
    pub(super) use crate::efatfs_host_shim::{
        fs_stats_free, stream_close, stream_open, stream_read_at_via, stream_truncate,
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

/// What a collect matches against. Keyed by KIND as well as handle, so a read answer can never be
/// mistaken for a write answer on the same file -- the discipline `file_bytes.rs` applies with its
/// `(ByteOp, path)` key.
#[derive(Clone, Copy, PartialEq, Eq)]
enum OpKind {
    Open,
    ReadAt,
    WriteAt,
    Truncate,
    Close,
    Stats,
}

/// The answer to one operation. The `Ok` payload is the handle for `Open`, the byte count for
/// `ReadAt`/`WriteAt`, the free-cluster count for `Stats`, and 0 for `Truncate`/`Close`.
#[derive(Clone, Copy)]
struct Answer {
    kind: OpKind,
    handle: u32,
    result: Result<u32, ()>,
}

/// Path storage for the one path-keyed operation. Declared here rather than borrowed from
/// `file_bytes`: that module is behind its own cargo feature, and a build that carries this driver
/// without it must still compile. Same bound as every other channel's, for the reason
/// `file_bytes.h` gives -- a path one channel can name but another cannot would be a trap.
const MAX_PATH_CHARS: usize = 255;
type WritePath = heapless::String<MAX_PATH_CHARS>;

static REQUEST: Mutex<CriticalSectionRawMutex, Option<(StreamOp, WritePath)>> = Mutex::new(None);
/// The operation the task is running right now. Kept separate from [`REQUEST`] so a collect can tell
/// "still running" (PENDING) from "never asked" (UNKNOWN) -- conflating those makes a caller wait
/// forever for an answer to a request nothing accepted.
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<(OpKind, u32)>> = Mutex::new(None);
static RESULT: Mutex<CriticalSectionRawMutex, Option<Answer>> = Mutex::new(None);
static WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

fn op_key(op: &StreamOp) -> (OpKind, u32) {
    match *op {
        StreamOp::Open { .. } => (OpKind::Open, 0),
        StreamOp::ReadAt { handle, .. } => (OpKind::ReadAt, handle),
        StreamOp::WriteAt { handle, .. } => (OpKind::WriteAt, handle),
        StreamOp::Truncate { handle, .. } => (OpKind::Truncate, handle),
        StreamOp::Close { handle } => (OpKind::Close, handle),
        StreamOp::Stats => (OpKind::Stats, 0),
    }
}

/// Accept an operation if nothing is in flight.
///
/// Drops any uncollected answer with the SAME key first. `STREAM_WRITE_CTX` recycles slot indices,
/// so without this a second recorder that inherits slot N collects the first recorder's stale answer
/// and reports its own work as done -- a bug this channel has already shipped once, caught only by a
/// byte-level readback.
fn accept(op: StreamOp, path: &str) -> bool {
    if !deluge_file_write_async_active() {
        return false;
    }
    let (Ok(mut req), Ok(in_flight), Ok(mut result)) =
        (REQUEST.try_lock(), IN_FLIGHT.try_lock(), RESULT.try_lock())
    else {
        return false;
    };
    // Single-flight. A second request while one is outstanding is REFUSED, never replaced: for the
    // recorder a dropped write is lost audio, so the caller must see the refusal.
    if req.is_some() || in_flight.is_some() {
        return false;
    }
    let (kind, handle) = op_key(&op);
    if result.is_some_and(|a| a.kind == kind && a.handle == handle) {
        *result = None;
    }
    let mut p = WritePath::new();
    if p.push_str(path).is_err() {
        return false;
    }
    *req = Some((op, p));
    WAKE.signal(());
    true
}

/// Collect the answer for `(kind, handle)`. `out` receives the `Ok` payload on READY.
///
/// # Safety
/// `out` may be null; when it is not, it must point at a writable `u32`.
unsafe fn collect(kind: OpKind, handle: u32, out: *mut u32) -> u8 {
    if let Ok(mut result) = RESULT.try_lock() {
        if let Some(a) = *result {
            if a.kind == kind && a.handle == handle {
                *result = None;
                return match a.result {
                    Ok(v) => {
                        if !out.is_null() {
                            // SAFETY: non-null, and the caller guarantees it is writable.
                            unsafe { *out = v };
                        }
                        DELUGE_FILE_WRITE_READY
                    }
                    Err(()) => DELUGE_FILE_WRITE_FAILED,
                };
            }
        }
    }
    let queued = REQUEST
        .try_lock()
        .map(|r| {
            r.as_ref()
                .is_some_and(|(op, _)| op_key(op) == (kind, handle))
        })
        .unwrap_or(false);
    let running = IN_FLIGHT
        .try_lock()
        .map(|f| *f == Some((kind, handle)))
        .unwrap_or(false);
    if queued || running {
        DELUGE_FILE_WRITE_PENDING
    } else {
        // Nothing queued, nothing running, no answer waiting: ask again.
        DELUGE_FILE_WRITE_UNKNOWN
    }
}

/// C-ABI: is the driver running?
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_write_async_active() -> bool {
    cfg!(feature = "async_file_write_active")
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

/// The driver. Takes one operation at a time and publishes its answer.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
#[embassy_executor::task]
pub async fn file_write_task() {
    loop {
        WAKE.wait().await;
        loop {
            let Some((op, path)) = REQUEST.lock().await.take() else {
                break;
            };
            let (kind, handle) = op_key(&op);
            *IN_FLIGHT.lock().await = Some((kind, handle));
            let result = match op {
                StreamOp::Open { mode } => {
                    backing::stream_open(path.as_str(), mode).await.ok_or(())
                }
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
                        .ok_or(())
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
                        _ => Err(()),
                    }
                }
                StreamOp::Truncate { handle, size } => {
                    if backing::stream_truncate(handle, size).await {
                        Ok(0)
                    } else {
                        Err(())
                    }
                }
                StreamOp::Close { handle } => {
                    if backing::stream_close(handle).await {
                        Ok(0)
                    } else {
                        Err(())
                    }
                }
                StreamOp::Stats => backing::fs_stats_free().await.ok_or(()),
            };
            *IN_FLIGHT.lock().await = None;
            *RESULT.lock().await = Some(Answer {
                kind,
                handle,
                result,
            });
        }
    }
}
