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
    pub(super) use crate::efatfs_fs::stream_write_at;
    #[cfg(not(target_os = "none"))]
    pub(super) use crate::efatfs_host_shim::stream_write_at;
}

/// One outstanding write. `ptr` is a `usize` so this is `Send`; see the module's Safety note.
#[derive(Clone, Copy)]
struct WriteRequest {
    handle: u32,
    offset: u32,
    ptr: usize,
    len: u32,
}

/// The answer to the request for `handle`. `Ok(n)` is the true byte count written.
#[derive(Clone, Copy)]
struct WriteAnswer {
    handle: u32,
    result: Result<u32, ()>,
}

static REQUEST: Mutex<CriticalSectionRawMutex, Option<WriteRequest>> = Mutex::new(None);
/// The request the task is running right now. Kept separate from [`REQUEST`] so a collect can tell
/// "still running" (PENDING) from "never asked" (UNKNOWN) — conflating those two is what makes a
/// caller wait forever for an answer to a request nothing accepted.
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<u32>> = Mutex::new(None);
static RESULT: Mutex<CriticalSectionRawMutex, Option<WriteAnswer>> = Mutex::new(None);
static WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// C-ABI: is the driver running?
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_write_async_active() -> bool {
    cfg!(feature = "async_file_write_active")
}

/// C-ABI: ask for a write. Refuses rather than queues — see `file_write.h`.
///
/// # Safety
/// `bytes` must point at `len` readable bytes that stay alive and unmodified until the matching
/// collect is terminal.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_write_request(
    handle: u32,
    offset: u32,
    bytes: *const u8,
    len: u32,
) -> bool {
    // NOTE: handle 0 is VALID here. This table is `STREAM_WRITE_CTX`, whose slots are indexed from
    // zero -- which is exactly why `deluge::io::Stream` boxes `handle + 1` into its opaque pointer.
    // The `0 = invalid` convention belongs to `sample_stream.h`'s registry, not this one, and
    // rejecting it here silently refused every write the first recorder ever made.
    if bytes.is_null() || len == 0 || !deluge_file_write_async_active() {
        return false;
    }
    let (Ok(mut req), Ok(in_flight)) = (REQUEST.try_lock(), IN_FLIGHT.try_lock()) else {
        return false;
    };
    // Single-flight, keyed by handle. A second request while one is outstanding is REFUSED, never
    // replaced: a dropped cluster write is lost audio, so the caller must see the refusal.
    if req.is_some() || in_flight.is_some() {
        return false;
    }
    // Drop any uncollected answer for this handle before accepting a new request. Handles are slot
    // indices in `STREAM_WRITE_CTX` and are RECYCLED: a recorder that closed its stream frees slot
    // N, and the next recorder to open one gets slot N back. Without this, that second recorder's
    // first collect would consume the first recorder's stale answer and report a write as landed
    // when its own bytes had not been written at all.
    let Ok(mut result) = RESULT.try_lock() else {
        return false;
    };
    if result.is_some_and(|a| a.handle == handle) {
        *result = None;
    }
    *req = Some(WriteRequest {
        handle,
        offset,
        ptr: bytes as usize,
        len,
    });
    WAKE.signal(());
    true
}

/// C-ABI: collect the answer for `handle`.
///
/// # Safety
/// `out_written` may be null; when it is not, it must point at a writable `u32`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_write_collect(handle: u32, out_written: *mut u32) -> u8 {
    if let Ok(mut result) = RESULT.try_lock() {
        if let Some(answer) = *result {
            if answer.handle == handle {
                *result = None;
                return match answer.result {
                    Ok(n) => {
                        if !out_written.is_null() {
                            // SAFETY: non-null, and the caller guarantees it is writable.
                            unsafe { *out_written = n };
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
        .map(|r| r.is_some_and(|q| q.handle == handle))
        .unwrap_or(false);
    let running = IN_FLIGHT
        .try_lock()
        .map(|f| *f == Some(handle))
        .unwrap_or(false);
    if queued || running {
        DELUGE_FILE_WRITE_PENDING
    } else {
        // Nothing queued, nothing running, no answer waiting: this handle has no request. UNKNOWN
        // tells the caller to ask again, which is what it must do.
        DELUGE_FILE_WRITE_UNKNOWN
    }
}

/// The driver. Takes one request at a time and publishes its answer.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
#[embassy_executor::task]
pub async fn file_write_task() {
    loop {
        WAKE.wait().await;
        loop {
            let Some(req) = REQUEST.lock().await.take() else {
                break;
            };
            *IN_FLIGHT.lock().await = Some(req.handle);
            // SAFETY: the C++ contract (file_write.h) is that this buffer stays alive and unmodified
            // until the collect below is observed as terminal. `Stream::close()` drains before the
            // handle is released, so no caller can free it underneath this borrow.
            let src =
                unsafe { core::slice::from_raw_parts(req.ptr as *const u8, req.len as usize) };
            let written = backing::stream_write_at(req.handle, req.offset, src).await;
            *IN_FLIGHT.lock().await = None;
            *RESULT.lock().await = Some(WriteAnswer {
                handle: req.handle,
                result: match written {
                    // A SHORT write is a failure here, not a success with a small count: the caller
                    // treats `written != len` as a card error anyway, and reporting it as READY with
                    // a short count would let a partial cluster look written.
                    Some(n) if n as u32 == req.len => Ok(n as u32),
                    _ => Err(()),
                },
            });
        }
    }
}
