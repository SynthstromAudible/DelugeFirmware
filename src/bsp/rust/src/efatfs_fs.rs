//! The single-owner mounted `embedded-fatfs` `FileSystem` —
//! mounts ONE `FileSystem` over the real SD card (`SdBlockDevice`, see
//! `fat_block_device.rs`) and stores it behind an `embassy_sync` async
//! `Mutex`. [`with_fs`] is the ONLY way live code may touch the FS.
//!
//! embedded-fatfs's `FileSystem` wraps its disk in a `RefCell`, which assumes
//! *exclusive* (non-reentrant, non-concurrent) access — two fibers/tasks
//! calling into it at once would panic the `RefCell` (or worse, race the
//! underlying SD transfer). An async `Mutex` (not the `RefCell` itself, and
//! not a blocking lock) serializes all FS operations here while letting a
//! holder `.await` (yield the executor) across the underlying SD transfer,
//! rather than busy-spinning or blocking other tasks outright.
//!
//! Device-only and flag-gated: the file-handle table, the FFI boundary, and
//! the streaming read path (`streaming_loader.rs`, `main.rs`) are what
//! invoke [`mount`]/[`with_fs`]; anything in this module not reached from
//! there stays dead code by design, hence the blanket `#![allow(dead_code)]`
//! below (keeps the `efatfs_streaming` build warning-clean without
//! disturbing the default build, which doesn't compile this file at all).
#![cfg(all(target_os = "none", feature = "efatfs_streaming"))]
#![allow(dead_code)]

use aligned::{A4, Aligned};
use block_device_adapters::{BufStream, StreamSlice};
use block_device_driver::BlockDevice;
use embassy_sync::{blocking_mutex::raw::CriticalSectionRawMutex, mutex::Mutex};
use embedded_fatfs::{DefaultTimeProvider, FileSystem, FsOptions, LossyOemCpConverter};

use crate::efatfs_core::{self, DirHandleTable, HandleTable, OpenOutcome, TaskFileTable};
use crate::fat_block_device::SdBlockDevice;
// No `DELUGE_ERR_BUSY` import any more: it was only ever the "you are off the worker fiber, go
// away" status the rejection gates returned, and nothing in this module can produce it now that
// any context may do I/O. The C++ side still maps it (`io/file.cpp:66`) and two consumers still
// branch on `Status::BUSY` (`existence_policy.h`, `smsysex.cpp:77`) — those branches are simply
// unreachable from this path now, which is the point of the change rather than an oversight.
// Leaving them in place is deliberate: pruning them is a separate concern from this module.
use crate::sys::{
    DelugeStatus, DelugeStatus_DELUGE_ERR_IO as DELUGE_ERR_IO,
    DelugeStatus_DELUGE_ERR_NO_FILESYSTEM as DELUGE_ERR_NO_FILESYSTEM,
    DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM, DelugeStatus_DELUGE_OK as DELUGE_OK,
};

type Storage = StreamSlice<BufStream<SdBlockDevice, 512>>;
pub type Fs = FileSystem<Storage, DefaultTimeProvider, LossyOemCpConverter>;

// Single-owner: embedded-fatfs's RefCell disk assumes exclusive access, so ALL
// FS ops serialize here. Async mutex (not the RefCell) — holders yield on SD.
static FS: Mutex<CriticalSectionRawMutex, Option<Fs>> = Mutex::new(None);

/// Detect MBR partition (real cards) vs superfloppy, return the byte window.
/// Sector 0 is either a FAT VBR (partitionless "superfloppy" — jump
/// `EB`/`E9`) or an MBR whose partition-0
/// entry (type @ +4, start_lba @ +8, sector-count @ +12 from offset 446) gives
/// the real FAT volume's byte window. On any sector-0 read error, fall back
/// to the whole-device window rather than panicking — `mount` below still
/// surfaces the eventual `FileSystem::new` failure as `Err(())`.
async fn partition_window() -> (u64, u64) {
    let mut s0: [Aligned<A4, [u8; 512]>; 1] = [Aligned([0u8; 512])];
    if SdBlockDevice.read(0, &mut s0).await.is_err() {
        return (0, SdBlockDevice.size().await.unwrap_or(u64::MAX));
    }
    let s = &s0[0][..];
    let sig = u16::from_le_bytes([s[510], s[511]]);
    let is_fat_vbr = s[0] == 0xEB || s[0] == 0xE9;
    let plba = u32::from_le_bytes([s[454], s[455], s[456], s[457]]);
    let nsec = u32::from_le_bytes([s[458], s[459], s[460], s[461]]);
    if !is_fat_vbr && sig == 0xAA55 && plba != 0 {
        let start = plba as u64 * 512;
        (start, start + nsec as u64 * 512)
    } else {
        (0, SdBlockDevice.size().await.unwrap_or(u64::MAX))
    }
}

/// Mount the FS, storing it in [`FS`]. Idempotent: returns `Ok(())`
/// immediately if a FS is already mounted, so both the eager boot mount and a
/// later C-ABI [`deluge_efatfs_mount`] call can invoke this safely.
/// How many times the filesystem has become mounted, as a monotonic counter.
///
/// The C++ side reads this to notice an unmounted->mounted EDGE, which is what drives
/// `AudioFileManager::firstCardRead()` (and through it `cardReinserted()`, which re-checks every
/// resident sample against the new card). It used to detect that edge itself, by sampling
/// `deluge_efatfs_is_mounted()` either side of its own `deluge_efatfs_mount()` call -- which only
/// works while C++ is the only thing that ever mounts. The directory channel's driver now mounts
/// too, on the storage tier, because mounting is real card I/O and its caller is on the interaction
/// tier; a driver that mounted silently would leave `is_mounted()` reading true with the
/// notification never fired, and every later check would see "already mounted" and skip it forever.
///
/// A counter rather than a flag so no reader can consume the edge out from under another.
static MOUNT_GENERATION: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// C-ABI: the mount counter. See [`MOUNT_GENERATION`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_mount_generation() -> u32 {
    MOUNT_GENERATION.load(core::sync::atomic::Ordering::Acquire)
}

pub async fn mount() -> Result<(), ()> {
    if FS.lock().await.is_some() {
        return Ok(()); // already mounted -- idempotent
    }
    let (start, end) = partition_window().await;
    let slice = StreamSlice::new(
        BufStream::<SdBlockDevice, 512>::new(SdBlockDevice),
        start,
        end,
    )
    .await
    .map_err(|_| ())?;
    let fs = FileSystem::new(slice, FsOptions::new())
        .await
        .map_err(|_| ())?;
    *FS.lock().await = Some(fs);
    // Bumped only on the transition, with the FS installed: a reader that sees the new value can
    // rely on the filesystem being usable.
    MOUNT_GENERATION.fetch_add(1, core::sync::atomic::Ordering::Release);
    Ok(())
}

/// Reset the four handle tables ([`HANDLES`], [`TASK_FILES`], [`DIR_HANDLES`],
/// [`STREAM_WRITE_CTX`]) back to empty. Each holds detached `FileContext`-ish
/// state keyed against the FS that was mounted when the handle was opened, so
/// all four go stale the instant the underlying [`Fs`] is dropped (a card
/// swap). Re-`new()`ing each under its own lock IS the reset: every table's
/// `new()` is a plain const zero-init, so there's no drop side effect worth
/// preserving.
async fn reset_all_handle_tables() {
    *HANDLES.lock().await = HandleTable::new();
    *TASK_FILES.lock().await = TaskFileTable::new();
    *DIR_HANDLES.lock().await = DirHandleTable::new();
    *STREAM_WRITE_CTX.lock().await = HandleTable::new();
}

/// Drop the mounted FS, reset the four handle tables, and re-mount fresh.
/// For a card SWAP: gives a clean [`Fs`] against the new card and invalidates
/// every stale Rust-side handle from the old one (open streaming reads, task
/// files, directory browses, and in-progress stream-writes all become
/// invalid — callers must reopen).
/// Drop the filesystem, leaving it unmounted.
///
/// The symmetric counterpart of [`mount`], which until now had none: [`remount`] drops and re-mounts
/// in one step, so nothing could put the filesystem into the unmounted state and leave it there.
/// That is the state the directory channel's driver mounts OUT of, so without this it could not be
/// exercised at all.
pub async fn unmount() {
    *FS.lock().await = None;
    reset_all_handle_tables().await;
}

pub async fn remount() -> Result<(), ()> {
    *FS.lock().await = None; // drop the old Fs (BufStream/StreamSlice discarded with it)
    reset_all_handle_tables().await;
    mount().await // re-mount fresh against the (new) card
}

/// Run `f` with exclusive access to the mounted FS (the ONLY entry point).
/// Returns `None` if [`mount`] hasn't run (or failed) yet.
///
/// `f` is an `AsyncFnOnce` (not `FnOnce(&Fs) -> impl Future`): only the async-
/// closure form ties the returned future's lifetime to the borrowed `&Fs`, so
/// the body may `.await` while holding that borrow.
pub async fn with_fs<R, F>(f: F) -> Option<R>
where
    F: AsyncFnOnce(&Fs) -> R,
{
    let g = FS.lock().await;
    match g.as_ref() {
        Some(fs) => Some(f(fs).await),
        None => None,
    }
}

// --- File-handle table (logic lives in `efatfs_core`) -----------------------
//
// The table type, the generation guard, the `FileContext` detach/reattach, and
// the fill loop all live in the storage-generic, host-testable [`efatfs_core`]
// module. This device layer owns one [`HandleTable`] behind an async `Mutex`
// and composes the core's split primitives under the lock discipline below.
//
// Lock discipline: [`HANDLES`] is NEVER held across a [`with_fs`] (FS-mutex)
// await. `read_at` `checkout`s the context OUT of the table, drops the
// `HANDLES` lock, does its FS work under `with_fs`, then re-locks `HANDLES` to
// `commit` the result back (gated on the slot's generation, so a `close`+`open`
// recycle racing the in-flight read can't splice a stale context onto a
// different file). Acquisition is therefore never nested — `open` takes
// FS→HANDLES, `read_at` takes HANDLES→FS-then-HANDLES with the FS work OUTSIDE
// the HANDLES lock, so the two never hold both at once and can't deadlock.
// (This is why the device path must not use `HandleTable::read_at_owned`, which
// holds the table across the FS await — see its doc.)

/// `FileContext` is plain data (`DirEntryEditor` is `data`/`pos`/`dirty`, no
/// `Rc`/`RefCell`), so [`HandleTable`] is `Send` and this static compiles.
///
/// Sized at [`efatfs_core::MAX_HANDLES`] (128) — a resident [`Sample`] holds
/// one of these for its whole lifetime (see `SampleStream::open_read_stream`),
/// so this is the ceiling on simultaneously-resident streamed samples in a
/// song. The persistent stream-*write* table below ([`STREAM_WRITE_CTX`])
/// deliberately does NOT share this capacity — see its own doc.
static HANDLES: Mutex<CriticalSectionRawMutex, HandleTable<{ efatfs_core::MAX_HANDLES }>> =
    Mutex::new(HandleTable::new());

/// Open `path`, detach it to a [`FileContext`], and stash it in a free slot.
/// Returns the slot index as [`OpenOutcome::Handle`], or specifically why the
/// open failed ([`OpenOutcome::NotFound`]: the FS is unmounted or the file
/// open failed; [`OpenOutcome::TableFull`]: [`HANDLES`] has no free slot).
pub async fn open(path: &str) -> OpenOutcome {
    // Open + detach under the FS mutex only; never touch HANDLES here (keeps the
    // FS→HANDLES order that pairs deadlock-free with read_at's HANDLES→FS).
    // `open_context` now reports a granular `DelugeStatus`, but this streaming-read
    // bridge's contract is still bare success/failure (see `streaming_fill.h`), so
    // every open_context error collapses to `NotFound` here regardless of which one it was.
    let Some(Ok(ctx)) = with_fs(async |fs| efatfs_core::open_context(fs, path).await).await else {
        return OpenOutcome::NotFound;
    };
    match HANDLES.lock().await.insert(ctx) {
        Some(h) => OpenOutcome::Handle(h),
        None => OpenOutcome::TableFull,
    }
}

/// Read `dst.len()` bytes from absolute `byte_offset` of the file behind
/// `handle`. Returns `true` iff the full buffer was filled. Composes the core's
/// split primitives under the lock discipline: `checkout` (release HANDLES) →
/// `read_context` under `with_fs` → `commit` (re-lock HANDLES, generation-gated).
pub async fn read_at(handle: u32, byte_offset: u32, dst: &mut [u8]) -> bool {
    let Some((generation, ctx)) = HANDLES.lock().await.checkout(handle) else {
        return false;
    };

    let result =
        with_fs(async |fs| efatfs_core::read_context(fs, ctx, byte_offset, dst).await).await;

    match result {
        Some(Some((newctx, filled))) => {
            HANDLES.lock().await.commit(handle, generation, newctx);
            filled
        }
        // FS not mounted, or reattach/seek/read failed — slot left untouched.
        _ => false,
    }
}

/// The size, in bytes, of the file behind `handle`; `None` if the handle is
/// out of range/free, the FS is unmounted, or the size could not be read.
///
/// Deliberately reads [`HANDLES`] — the *streaming-read* handle table
/// [`open`]/[`read_at`]/[`close`] above share — and NOT the separate
/// `TASK_FILES` table `deluge_efatfs_file_size` serves. The async audio loader
/// opens a file through [`open`] and must size that same open file; routing its
/// size through the task-file table would index an unrelated (or empty) slot and
/// hand the header parser a bound belonging to a different file, which bounds
/// the parse to the wrong place rather than failing.
///
/// Same lock discipline as [`read_at`]: `checkout` (release HANDLES) →
/// `size_context` under `with_fs` → `commit` (re-lock HANDLES,
/// generation-gated). `size_context` leaves the context's cursor at EOF, which
/// is harmless — every `read_context` reattaches and seeks absolutely first.
pub async fn size_of_handle(handle: u32) -> Option<u32> {
    let (generation, ctx) = HANDLES.lock().await.checkout(handle)?;

    let result = with_fs(async |fs| efatfs_core::size_context(fs, ctx).await).await;

    match result {
        Some(Some((newctx, size))) => {
            HANDLES.lock().await.commit(handle, generation, newctx);
            Some(size)
        }
        // FS not mounted, or reattach/seek failed — slot left untouched (`checkout`
        // clones rather than takes, so the handle stays usable).
        _ => None,
    }
}

/// Close `handle`, freeing its slot. No-op for an out-of-range handle. Bumps
/// the slot's generation so any `read_at` write-back already in flight for
/// this handle (captured before this `close`) is detected as stale even if no
/// subsequent `open` reuses the index.
pub async fn close(handle: u32) {
    HANDLES.lock().await.remove(handle);
}

// --- FFI bridge --------------------------------------------------------------
//
// C++ calls these synchronously at sample-load (`open_read_stream`), but
// [`open`]/[`close`] above are async. Bridge via `crate::sim_block::block_on` — on device a
// plain non-yielding spin, whose awaited transfer completes by SDHI/DMA interrupt, so nothing
// else has to run for the future to become ready.
//
// Callable from ANY context. These entry points used to require `crate::fiber::on_fiber()` and
// reject every other caller, which is what made saving fail off-owner and what made
// `deluge_boot`'s settings read fail before the fiber pump existed. The cost of the spin is
// real and accepted: the calling context is HELD for the duration of the transfer rather than
// yielded, so thread-mode tasks (PIC pump, OLED, UI) do not run during one. Streaming is
// unaffected — it lives on its own interrupt executor. The answer to the responsiveness cost is
// a genuinely async `Owner::await`, not reinstating the yield.
use core::ffi::{CStr, c_char};

/// C-ABI: open a sample file for streaming; writes the handle to `*out_handle`.
/// Returns false (caller falls back to the C-FatFS map) if not on the worker
/// fiber, the path/pointer is null or invalid, the FS is unmounted, or the open
/// failed. `*out_table_full` is always written on a `false` return — true iff
/// the failure was specifically [`HANDLES`] having no free slot (as opposed to
/// a genuinely missing file or unmounted FS), so the caller can surface a
/// distinguishable "too many open streams" error instead of a misleading
/// "file not found". See `include/libdeluge/streaming_fill.h` for the C-side
/// contract.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_open(
    path: *const c_char,
    out_handle: *mut u32,
    out_table_full: *mut bool,
) -> bool {
    if path.is_null() || out_handle.is_null() || out_table_full.is_null() {
        return false;
    }
    // SAFETY: `path` is a NUL-terminated C string supplied by open_read_stream,
    // valid for the duration of this call.
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return false,
    };
    match crate::sim_block::block_on(open(path)) {
        OpenOutcome::Handle(h) => {
            // SAFETY: `out_handle`/`out_table_full` are non-null (checked above) and point at
            // memory the C++ caller owns for the duration of this synchronous call.
            unsafe {
                *out_handle = h;
                *out_table_full = false;
            }
            true
        }
        OpenOutcome::TableFull => {
            // SAFETY: see above.
            unsafe {
                *out_table_full = true;
            }
            false
        }
        OpenOutcome::NotFound => {
            // SAFETY: see above.
            unsafe {
                *out_table_full = false;
            }
            false
        }
    }
}

/// C-ABI: close a streaming file handle, releasing its table slot.
///
/// Closes from ANY context. This used to be wrapped in `if on_fiber()`, which silently skipped
/// the close off-fiber and leaked the slot until reuse — tolerable only because off-fiber callers
/// were rejected everywhere else, so an off-fiber close was nearly unreachable. Now that they are
/// allowed, that leak would be live, and handles are the scarce resource (a full handle table
/// drops samples silently). A spin needs no fiber, so there is nothing left to gate on.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_close(handle: u32) {
    crate::sim_block::block_on(close(handle));
}

/// C-ABI: synchronously read `count` bytes at `byte_offset` of the file behind `handle`, bridging
/// the async [`read_at`] through a non-yielding spin — valid from any context. Writes
/// `*out_read = count` and returns true iff the full buffer was filled. See
/// `include/libdeluge/streaming_fill.h` for the contract.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_read_at(
    handle: u32,
    byte_offset: u32,
    dst: *mut u8,
    count: u32,
    out_read: *mut u32,
) -> bool {
    if dst.is_null() || out_read.is_null() {
        return false;
    }
    // SAFETY: `dst` points at `count` writable bytes owned by the caller for the duration of this
    // synchronous call (the chunk's cluster payload buffer, from the reader's `fill_now`).
    let buf = unsafe { core::slice::from_raw_parts_mut(dst, count as usize) };
    if crate::sim_block::block_on(read_at(handle, byte_offset, buf)) {
        // SAFETY: `out_read` is non-null (checked above), a `u32` the caller owns.
        unsafe {
            *out_read = count;
        }
        true
    } else {
        false
    }
}

// --- Task-context file/dir tables --------------------------------------------
//
// Separate from [`HANDLES`] above -- see `efatfs_core::TaskFileTable`'s doc for
// why task-context file I/O (explicit `seek()` + position-implicit `read`/
// `write`, `file_io.h`'s contract) needs its own position-carrying table rather
// than reusing the streaming path's `Slot`. [`DIR_HANDLES`] backs
// `include/libdeluge/file_io.h`'s directory-enumeration C-ABI.

static TASK_FILES: Mutex<CriticalSectionRawMutex, TaskFileTable> = Mutex::new(TaskFileTable::new());
static DIR_HANDLES: Mutex<CriticalSectionRawMutex, DirHandleTable> =
    Mutex::new(DirHandleTable::new());

/// `DelugeFileOpenMode` mode selector matching `file_io.h`'s C enum's implicit
/// declaration-order values: 0 = READ, 1 = WRITE_CREATE, 2 = WRITE_CREATE_NEW.
/// `Err(DELUGE_ERR_NO_FILESYSTEM)` if the volume isn't mounted,
/// `Err(DELUGE_ERR_PARAM)` for an unrecognized `mode` or a full [`TASK_FILES`]
/// table, otherwise the granular open/create error from `efatfs_core`.
pub(crate) async fn task_file_open(path: &str, mode: u8) -> Result<u32, DelugeStatus> {
    let ctx = with_fs(async |fs| match mode {
        0 => efatfs_core::open_context(fs, path).await,
        1 => efatfs_core::create_context(fs, path, false).await,
        2 => efatfs_core::create_context(fs, path, true).await,
        _ => Err(DELUGE_ERR_PARAM),
    })
    .await
    .ok_or(DELUGE_ERR_NO_FILESYSTEM)??;
    TASK_FILES.lock().await.insert(ctx).ok_or(DELUGE_ERR_IO) // table full
}

/// Fill-semantics read (zero-pads a short tail at EOF, like [`read_at`]) at the
/// handle's current position, advancing it by `dst.len()` on success.
async fn task_file_read(handle: u32, dst: &mut [u8]) -> Result<(), DelugeStatus> {
    let Some((generation, ctx, pos)) = TASK_FILES.lock().await.checkout(handle) else {
        return Err(DELUGE_ERR_PARAM);
    };
    let result = with_fs(async |fs| efatfs_core::read_context(fs, ctx, pos, dst).await).await;
    match result {
        Some(Some((newctx, filled))) => {
            TASK_FILES
                .lock()
                .await
                .commit(handle, generation, newctx, pos + dst.len() as u32);
            if filled { Ok(()) } else { Err(DELUGE_ERR_IO) }
        }
        Some(None) => Err(DELUGE_ERR_IO),
        None => Err(DELUGE_ERR_NO_FILESYSTEM),
    }
}

/// EOF-honest read at the handle's current position -- the default for the
/// port's `File::read` (`file.cpp`). Returns the TRUE byte count (may be less
/// than `dst.len()` at EOF, never zero-padded), advancing the position by that
/// count.
pub(crate) async fn task_file_read_exact(
    handle: u32,
    dst: &mut [u8],
) -> Result<usize, DelugeStatus> {
    let (generation, ctx, pos) = TASK_FILES
        .lock()
        .await
        .checkout(handle)
        .ok_or(DELUGE_ERR_PARAM)?;
    let (newctx, n) = with_fs(async |fs| efatfs_core::read_context_exact(fs, ctx, pos, dst).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)?
        .ok_or(DELUGE_ERR_IO)?;
    TASK_FILES
        .lock()
        .await
        .commit(handle, generation, newctx, pos + n as u32);
    Ok(n)
}

/// Write at the handle's current position, advancing it by the bytes actually
/// written.
pub(crate) async fn task_file_write(handle: u32, src: &[u8]) -> Result<usize, DelugeStatus> {
    let (generation, ctx, pos) = TASK_FILES
        .lock()
        .await
        .checkout(handle)
        .ok_or(DELUGE_ERR_PARAM)?;
    let (newctx, n) = with_fs(async |fs| efatfs_core::write_context(fs, ctx, pos, src).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)?
        .ok_or(DELUGE_ERR_IO)?;
    TASK_FILES
        .lock()
        .await
        .commit(handle, generation, newctx, pos + n as u32);
    Ok(n)
}

/// Set the handle's cursor position directly. No FS access needed.
/// Seek to an absolute byte offset.
///
/// `pub(crate)` so `file_bytes.rs`'s driver can AWAIT it: the offset-addressed operations seek before
/// every read and write, and the C ABI beside them reaches the same future through
/// `sim_block::block_on`, which spins its caller.
pub(crate) async fn task_file_seek(handle: u32, offset: u32) -> Result<(), DelugeStatus> {
    if TASK_FILES.lock().await.seek(handle, offset) {
        Ok(())
    } else {
        Err(DELUGE_ERR_PARAM)
    }
}

/// File length in bytes. Leaves the handle's position untouched.
pub(crate) async fn task_file_size(handle: u32) -> Result<u32, DelugeStatus> {
    let (generation, ctx, pos) = TASK_FILES
        .lock()
        .await
        .checkout(handle)
        .ok_or(DELUGE_ERR_PARAM)?;
    let (newctx, size) = with_fs(async |fs| efatfs_core::size_context(fs, ctx).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)?
        .ok_or(DELUGE_ERR_IO)?;
    TASK_FILES
        .lock()
        .await
        .commit(handle, generation, newctx, pos);
    Ok(size)
}

/// Truncate to `new_len` bytes. Leaves the handle's position untouched (matches
/// POSIX `ftruncate`'s file-offset-unchanged convention -- a subsequent read at
/// a now-past-EOF position simply returns 0 bytes).
pub(crate) async fn task_file_truncate(handle: u32, new_len: u32) -> Result<(), DelugeStatus> {
    let Some((generation, ctx, pos)) = TASK_FILES.lock().await.checkout(handle) else {
        return Err(DELUGE_ERR_PARAM);
    };
    let result = with_fs(async |fs| efatfs_core::truncate_context(fs, ctx, new_len).await).await;
    match result {
        Some(Some((newctx, ()))) => {
            TASK_FILES
                .lock()
                .await
                .commit(handle, generation, newctx, pos);
            Ok(())
        }
        Some(None) => Err(DELUGE_ERR_IO),
        None => Err(DELUGE_ERR_NO_FILESYSTEM),
    }
}

/// Close a task-context file handle, freeing its slot.
pub(crate) async fn task_file_close(handle: u32) {
    TASK_FILES.lock().await.remove(handle);
}

/// Open `path` as a directory, resolving to a handle into [`DIR_HANDLES`].
///
/// `pub(crate)` so `file_directory.rs`'s driver can AWAIT this, which is the whole of that channel:
/// `deluge_efatfs_dir_open` reaches the same future through `sim_block::block_on` and spins its
/// caller until it completes.
pub(crate) async fn task_dir_open(path: &str) -> Result<u32, DelugeStatus> {
    let cursor = with_fs(async |fs| efatfs_core::readdir_open(fs, path).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)??;
    DIR_HANDLES.lock().await.insert(cursor).ok_or(DELUGE_ERR_IO) // table full
}

/// Outcome of advancing a [`DirCursor`](efatfs_core) past zero or more
/// too-long-for-the-C-buffer entries (see the module comment above) to either a
/// fitting entry or end-of-directory.
enum NextFit {
    Eof,
    Found(efatfs_core::DirEntryInfo),
}

/// Advance `handle`'s cursor to the next entry whose name fits in
/// `max_name_bytes` (the C caller's buffer, minus room for the NUL --
/// see `deluge_efatfs_dir_read`), skipping any that don't -- belt-and-suspenders
/// with `efatfs_core::readdir_open`, which already keeps ONE
/// oversized name from failing the whole snapshot: this additionally guards the
/// narrower "fits the 256-byte `heapless::String` but not the caller's
/// NUL-terminated buffer" edge (a name of exactly 256 UTF-8 bytes fits the
/// former, not the latter).
///
/// `Err(DELUGE_ERR_PARAM)`: bad handle. `Ok(None)`: end of directory --
/// `deluge_dir_read`'s existing contract ("no more entries" is not an error).
/// `Ok(Some(info))`: a fitting entry.
async fn task_dir_read(
    handle: u32,
    max_name_bytes: usize,
) -> Result<Option<efatfs_core::DirEntryInfo>, DelugeStatus> {
    let mut dir_table = DIR_HANDLES.lock().await;
    let cursor = dir_table.get_mut(handle).ok_or(DELUGE_ERR_PARAM)?;
    // `readdir_next` is FS-free (it only walks the in-memory snapshot
    // `cursor` already holds), so this does not route through `with_fs` --
    // doing so would serialize an in-memory directory-page read behind the
    // single FS mutex, and thus behind any in-flight streaming SD read, for
    // no reason.
    let next = loop {
        // `readdir_next`'s outer `None` cannot occur for a snapshot cursor today (see its
        // doc) -- IO is the generic fallback if a future non-snapshot cursor ever hits it.
        match efatfs_core::readdir_next(cursor).ok_or(DELUGE_ERR_IO)? {
            None => break NextFit::Eof,
            Some(info) => {
                if info.name.len() >= max_name_bytes {
                    continue; // doesn't fit the caller's buffer; skip, don't fail the browse
                }
                break NextFit::Found(info);
            }
        }
    };
    Ok(match next {
        NextFit::Eof => None,
        NextFit::Found(info) => Some(info),
    })
}

/// Release `handle`'s row in [`DIR_HANDLES`]. FS-free -- it touches no card.
pub(crate) async fn task_dir_close(handle: u32) {
    DIR_HANDLES.lock().await.remove(handle);
}

/// Release `handle`'s row without awaiting, for a caller that may not block.
///
/// `deluge_card_flush` runs synchronously on the interaction tier and must close whatever handle the
/// directory channel was holding, so it needs a close that cannot wait. This is FS-free like
/// [`task_dir_close`] -- the only thing it can lose is the table lock, and a contended table leaks
/// one of `MAX_DIR_HANDLES` slots until the next mount. That is bounded and rare, and strictly
/// better than blocking the interaction tier inside an eject.
///
/// Returns whether the slot was actually released.
#[allow(dead_code)] // only the directory channel calls this, and it is feature-gated
pub(crate) fn try_close_dir(handle: u32) -> bool {
    match DIR_HANDLES.try_lock() {
        Ok(mut table) => {
            table.remove(handle);
            true
        }
        Err(_) => false,
    }
}

// --- Task-context FFI bridge --------------------------------------------------
//
// Same spin-bridge discipline as the streaming bridge above — callable from any context, and
// see that header for the accepted cost. `include/libdeluge/file_io.h` has the C-side contract
// of every function below.

/// C-ABI: open a task-context file. `mode` matches `DelugeFileOpenMode`'s
/// declaration-order values (0=READ, 1=WRITE_CREATE, 2=WRITE_CREATE_NEW).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_open(
    path: *const c_char,
    mode: u8,
    out_handle: *mut u32,
) -> DelugeStatus {
    if path.is_null() || out_handle.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `path` is a NUL-terminated C string valid for the duration of this call.
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    match crate::sim_block::block_on(task_file_open(path, mode)) {
        Ok(h) => {
            // SAFETY: `out_handle` is non-null (checked above), owned by the caller.
            unsafe {
                *out_handle = h;
            }
            DELUGE_OK
        }
        Err(st) => st,
    }
}

/// C-ABI: fill-semantics read (see [`task_file_read`]) at the handle's current
/// position; always reports `*out_read = count` on success.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_read(
    handle: u32,
    dst: *mut u8,
    count: u32,
    out_read: *mut u32,
) -> DelugeStatus {
    if dst.is_null() || out_read.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `dst` points at `count` writable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts_mut(dst, count as usize) };
    match crate::sim_block::block_on(task_file_read(handle, buf)) {
        Ok(()) => {
            // SAFETY: `out_read` is non-null (checked above).
            unsafe {
                *out_read = count;
            }
            DELUGE_OK
        }
        Err(st) => st,
    }
}

/// C-ABI: EOF-honest read (see [`task_file_read_exact`]) -- the default for the
/// port's `File::read`. `*out_read` is the true byte count, `<= count`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_read_exact(
    handle: u32,
    dst: *mut u8,
    count: u32,
    out_read: *mut u32,
) -> DelugeStatus {
    if dst.is_null() || out_read.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `dst` points at `count` writable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts_mut(dst, count as usize) };
    match crate::sim_block::block_on(task_file_read_exact(handle, buf)) {
        Ok(n) => {
            // SAFETY: `out_read` is non-null (checked above).
            unsafe {
                *out_read = n as u32;
            }
            DELUGE_OK
        }
        Err(st) => st,
    }
}

/// C-ABI: write at the handle's current position. `*out_written` is the true
/// byte count written.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_write(
    handle: u32,
    src: *const u8,
    count: u32,
    out_written: *mut u32,
) -> DelugeStatus {
    if src.is_null() || out_written.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `src` points at `count` readable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts(src, count as usize) };
    match crate::sim_block::block_on(task_file_write(handle, buf)) {
        Ok(n) => {
            // SAFETY: `out_written` is non-null (checked above).
            unsafe {
                *out_written = n as u32;
            }
            DELUGE_OK
        }
        Err(st) => st,
    }
}

/// C-ABI: move the handle's cursor to an absolute byte offset.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_seek(handle: u32, offset: u32) -> DelugeStatus {
    match crate::sim_block::block_on(task_file_seek(handle, offset)) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// C-ABI: total file size in bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_size(handle: u32, out_size: *mut u32) -> DelugeStatus {
    if out_size.is_null() {
        return DELUGE_ERR_PARAM;
    }
    match crate::sim_block::block_on(task_file_size(handle)) {
        Ok(sz) => {
            // SAFETY: `out_size` is non-null (checked above).
            unsafe {
                *out_size = sz;
            }
            DELUGE_OK
        }
        Err(st) => st,
    }
}

/// C-ABI: truncate to `new_len` bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_truncate(handle: u32, new_len: u32) -> DelugeStatus {
    match crate::sim_block::block_on(task_file_truncate(handle, new_len)) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// C-ABI: close a task-context file handle, releasing its table slot. Closes from any context —
/// see [`deluge_efatfs_close`] for why the previous `on_fiber()` guard had to go.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_close(handle: u32) {
    crate::sim_block::block_on(task_file_close(handle));
}

/// C-ABI: open `path` as a directory for iteration.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_dir_open(
    path: *const c_char,
    out_handle: *mut u32,
) -> DelugeStatus {
    if path.is_null() || out_handle.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `path` is a NUL-terminated C string valid for the duration of this call.
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    match crate::sim_block::block_on(task_dir_open(path)) {
        Ok(h) => {
            // SAFETY: `out_handle` is non-null (checked above), owned by the caller.
            unsafe {
                *out_handle = h;
            }
            DELUGE_OK
        }
        Err(st) => st,
    }
}

/// C-ABI: read the next directory entry into the caller's out-params (see
/// `include/libdeluge/file_io.h`'s `DelugeDirEntry`/`deluge_dir_read` for the
/// shape this mirrors). `out_name` must point at `out_name_cap` writable bytes
/// (`DELUGE_MAX_FILENAME` in practice); an entry whose name doesn't fit is
/// skipped internally (see [`task_dir_read`]), never truncated into `out_name`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_dir_read(
    handle: u32,
    out_name: *mut c_char,
    out_name_cap: u32,
    out_is_dir: *mut bool,
    out_size: *mut u32,
    out_modified: *mut u32,
    out_attrs: *mut u8,
    out_has: *mut bool,
) -> DelugeStatus {
    if out_name.is_null()
        || out_name_cap == 0
        || out_is_dir.is_null()
        || out_size.is_null()
        || out_modified.is_null()
        || out_attrs.is_null()
        || out_has.is_null()
    {
        return DELUGE_ERR_PARAM;
    }
    match crate::sim_block::block_on(task_dir_read(handle, out_name_cap as usize)) {
        Ok(None) => {
            // SAFETY: `out_has` is non-null (checked above).
            unsafe {
                *out_has = false;
            }
            DELUGE_OK
        }
        Ok(Some(info)) => {
            let name_bytes = info.name.as_bytes();
            // SAFETY: `task_dir_read` only returns entries with
            // `name.len() < out_name_cap`, so `name_bytes.len() + 1 <= out_name_cap`;
            // every `out_*` pointer is non-null (checked above) and owned by the
            // caller for the duration of this call.
            unsafe {
                let dst =
                    core::slice::from_raw_parts_mut(out_name.cast::<u8>(), out_name_cap as usize);
                dst[..name_bytes.len()].copy_from_slice(name_bytes);
                dst[name_bytes.len()] = 0;
                *out_is_dir = info.is_dir;
                *out_size = info.size;
                *out_modified = info.modified;
                *out_attrs = info.attrs;
                *out_has = true;
            }
            DELUGE_OK
        }
        Err(st) => st,
    }
}

/// C-ABI: close a directory handle, releasing its table slot. Closes from any context — see
/// [`deluge_efatfs_close`] for why the previous `on_fiber()` guard had to go.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_dir_close(handle: u32) {
    crate::sim_block::block_on(task_dir_close(handle));
}

/// C-ABI: free + total cluster counts of the mounted volume.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stats(
    out_free_clusters: *mut u32,
    out_total_clusters: *mut u32,
) -> bool {
    if out_free_clusters.is_null() || out_total_clusters.is_null() {
        return false;
    }
    match crate::sim_block::block_on(with_fs(async |fs| efatfs_core::fs_stats(fs).await)).flatten()
    {
        Some((free, total)) => {
            // SAFETY: both out-params non-null (checked above).
            unsafe {
                *out_free_clusters = free;
                *out_total_clusters = total;
            }
            true
        }
        None => false,
    }
}

/// C-ABI: mount the FS if it isn't already mounted (idempotent -- see
/// [`mount`]). True if mounted (or already was), false on failure/off-fiber.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_mount() -> bool {
    crate::sim_block::block_on(mount()).is_ok()
}

/// C-ABI: whether the FS is currently mounted (pure state check, no I/O). Lets a caller distinguish
/// a fresh `mount()` transition from the already-mounted case, since `mount()`'s own success return
/// can't tell them apart (it's idempotent).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_is_mounted() -> bool {
    crate::sim_block::block_on(with_fs(async |_fs| ())).is_some()
}

/// C-ABI (libdeluge/storage_owner.h): is a filesystem operation in flight?
///
/// NON-BLOCKING and callable from any context — deliberately unlike
/// [`deluge_efatfs_is_mounted`], which is callable anywhere too but BLOCKS (it spins on the FS
/// mutex). This one only probes. Off-owner
/// callers asking "may I start filesystem work now?" are the primary consumer, and blocking here
/// would be the very deadlock this exists to prevent.
///
/// `try_lock()` returns `Err` when the mutex is HELD — do not invert this. On `Ok` the guard is
/// dropped immediately (it's a temporary, dropped at the end of the statement), so a successful
/// probe does not keep the lock.
///
/// Check-then-act is sound here, but narrower than "cooperative on one thread" alone would suggest:
/// the audio InterruptExecutor CAN preempt a thread-mode task_runner mid-handle (`scheduler.rs:476-478`),
/// so this is not literally single-threaded. It is sound because audio never touches the filesystem —
/// the interrupt executor has no code path that acquires `FS` — so no preemption can land inside the gap
/// between this probe and the caller's own filesystem access, and no `await` sits in that gap either.
/// That "audio never touches the FS" fact is the actual load-bearing premise, not thread-cooperation by
/// itself — state it plainly so a future reader "hardening" this into an atomic understands what would
/// have to become false first (audio starting to touch the FS), rather than assuming there is no race to
/// reason about at all.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_storage_fs_busy() -> bool {
    FS.try_lock().is_err()
}

/// C-ABI: drop the mounted FS + reset the four handle tables + re-mount fresh
/// (see [`remount`]). For a card SWAP: gives a clean FS against the new card
/// and invalidates stale Rust-side handle state from the old one.
/// C-ABI: drop the filesystem, leaving it unmounted. See [`unmount`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_unmount() {
    crate::sim_block::block_on(unmount());
}
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_remount() -> bool {
    crate::sim_block::block_on(remount()).is_ok()
}

/// C-ABI: the mounted volume's cluster (allocation-unit) size, in bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_cluster_size(out_bytes: *mut u32) -> bool {
    if out_bytes.is_null() {
        return false;
    }
    match crate::sim_block::block_on(with_fs(async |fs| efatfs_core::fs_cluster_size(fs).await))
        .flatten()
    {
        Some(bytes) => {
            // SAFETY: `out_bytes` is non-null (checked above).
            unsafe {
                *out_bytes = bytes;
            }
            true
        }
        None => false,
    }
}

/// Create a directory.
///
/// `pub(crate)` so the directory channel's driver can AWAIT it: the C ABI below reaches the same
/// future through `sim_block::block_on`, which spins its caller, and the browser's fallback reaches
/// that from the interaction tier where waiting on the card is not allowed.
pub(crate) async fn task_mkdir(path: &str) -> Result<(), DelugeStatus> {
    with_fs(async |fs| efatfs_core::mkdir(fs, path).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)?
}

/// C-ABI: create a directory.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_mkdir(path: *const c_char) -> DelugeStatus {
    if path.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    match crate::sim_block::block_on(task_mkdir(path)) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// Delete a file or empty directory.
///
/// `pub(crate)` so `file_bytes.rs`'s driver can AWAIT it: the C ABI below reaches the same future
/// through `sim_block::block_on`, which spins its caller, and the startup fail-safe reaches that from
/// a scheduler task where waiting on the card is not allowed.
pub(crate) async fn task_unlink(path: &str) -> Result<(), DelugeStatus> {
    match with_fs(async |fs| efatfs_core::unlink(fs, path).await).await {
        Some(result) => result,
        None => Err(DELUGE_ERR_NO_FILESYSTEM),
    }
}

/// C-ABI: delete a file or empty directory.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_unlink(path: *const c_char) -> DelugeStatus {
    if path.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    match crate::sim_block::block_on(task_unlink(path)) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// Rename or move a file or directory.
///
/// `pub(crate)` so `file_bytes.rs`'s driver can AWAIT it: the C ABI below reaches the same future
/// through `sim_block::block_on`, which spins its caller, and the settings family's legacy-path
/// migration reaches that from the interaction tier where waiting on the card is not allowed.
pub(crate) async fn task_rename(old: &str, new: &str) -> Result<(), DelugeStatus> {
    match with_fs(async |fs| efatfs_core::rename(fs, old, new).await).await {
        Some(result) => result,
        None => Err(DELUGE_ERR_NO_FILESYSTEM),
    }
}

/// C-ABI: rename/move a file or directory.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_rename(
    old_path: *const c_char,
    new_path: *const c_char,
) -> DelugeStatus {
    if old_path.is_null() || new_path.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let old = match unsafe { CStr::from_ptr(old_path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    let new = match unsafe { CStr::from_ptr(new_path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    match crate::sim_block::block_on(task_rename(old, new)) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// Set a file or directory's last-modified timestamp.
///
/// `pub(crate)` so `file_bytes.rs`'s driver can AWAIT it: the C ABI below reaches the same future
/// through `sim_block::block_on`, which spins its caller, and the SysEx file protocol reaches that
/// from a scheduler task where waiting on the card is not allowed.
///
/// Takes the decomposed fields rather than a packed value, because packing is `efatfs_core`'s
/// business and the boundary above carries the fields.
pub(crate) async fn task_set_time(
    path: &str,
    year: u16,
    month: u8,
    day: u8,
    hour: u8,
    minute: u8,
    second: u8,
) -> Result<(), DelugeStatus> {
    let packed = efatfs_core::pack_timestamp(year, month, day, hour, minute, second);
    match with_fs(async |fs| efatfs_core::set_time(fs, path, packed).await).await {
        Some(result) => result,
        None => Err(DELUGE_ERR_NO_FILESYSTEM),
    }
}

/// C-ABI: set a file or directory's last-modified timestamp (decomposed
/// fields, packed here via `efatfs_core::pack_timestamp` before reaching the
/// FAT DOS date/time `set_time` primitive).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_set_time(
    path: *const c_char,
    year: u16,
    month: u8,
    day: u8,
    hour: u8,
    minute: u8,
    second: u8,
) -> DelugeStatus {
    if path.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    match crate::sim_block::block_on(task_set_time(path, year, month, day, hour, minute, second)) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

// --- Persistent stream-write handle table -------------------------------------
//
// Backs the sample recorder's efatfs write path (`include/libdeluge/stream_io.h`'s
// `deluge_efatfs_stream_*`). Unlike [`HANDLES`]/[`TASK_FILES`] above, a slot here holds ONE
// `FileContext` across an ENTIRE recording: `stream_write_at` composes `efatfs_core::
// write_context_noflush` (advances the in-memory size/mtime but does NOT touch the on-disk
// directory entry -- see that primitive's doc), so a long recording pays a directory-entry flush
// only at `stream_flush`/`stream_close`, not on every chunk. Reuses [`HandleTable`] (generation-
// gated checkout/commit, just like [`HANDLES`]) rather than a bespoke type -- the table shape
// (generation + `FileContext`) is identical; only the *primitives* composed under it differ.
//
// Same lock discipline as every other table in this module: [`STREAM_WRITE_CTX`] is never held
// across a [`with_fs`] await -- checkout (release the table lock) -> FS work under `with_fs` ->
// commit (re-lock, generation-gated).
/// Sized at [`efatfs_core::MAX_STREAM_WRITE_HANDLES`] (8), deliberately much
/// smaller than [`HANDLES`]'s 128: the recorder opens one write handle per
/// in-progress recording, and this device supports at most a handful of
/// concurrent recordings — there's no reason to pay 128 slots' worth of
/// static memory for a table that only ever needs a few.
static STREAM_WRITE_CTX: Mutex<
    CriticalSectionRawMutex,
    HandleTable<{ efatfs_core::MAX_STREAM_WRITE_HANDLES }>,
> = Mutex::new(HandleTable::new());

/// `DelugeStreamMode` mode selector matching `stream_io.h`'s C enum's implicit declaration-order
/// values: 0 = READ, 1 = WRITE_CREATE, 2 = WRITE_CREATE_NEW, 3 = WRITE_APPEND.
async fn stream_open(path: &str, mode: u8) -> Option<u32> {
    // `open_context`/`create_context` now report a granular `DelugeStatus`, but
    // this persistent stream-write bridge's contract (`stream_io.h`) is still
    // bare success/failure, so `.ok()` collapses it back to `Option` here.
    let ctx = with_fs(async |fs| match mode {
        0 => efatfs_core::open_context(fs, path).await,
        1 => efatfs_core::create_context(fs, path, false).await,
        2 => efatfs_core::create_context(fs, path, true).await,
        3 => efatfs_core::open_context(fs, path).await, // append: open existing, no truncation
        _ => Err(DELUGE_ERR_PARAM),
    })
    .await?
    .ok()?;
    STREAM_WRITE_CTX.lock().await.insert(ctx)
}

/// Write `src` at absolute `byte_offset`, advancing the handle's persisted context WITHOUT
/// flushing the size/mtime edit to disk (`efatfs_core::write_context_noflush`). Returns the true
/// byte count written, or `None` on a bad handle or FS error.
async fn stream_write_at(handle: u32, byte_offset: u32, src: &[u8]) -> Option<usize> {
    let (generation, ctx) = STREAM_WRITE_CTX.lock().await.checkout(handle)?;
    let (newctx, n) =
        with_fs(async |fs| efatfs_core::write_context_noflush(fs, ctx, byte_offset, src).await)
            .await??;
    STREAM_WRITE_CTX
        .lock()
        .await
        .commit(handle, generation, newctx);
    Some(n)
}

/// EOF-honest read at absolute `byte_offset`, bounded by the handle's IN-MEMORY size
/// (`efatfs_core::read_at_via_context`) -- can read back data written earlier in the same
/// unflushed session. Returns the true byte count read, or `None` on a bad handle or FS error.
async fn stream_read_at_via(handle: u32, byte_offset: u32, dst: &mut [u8]) -> Option<usize> {
    let (generation, ctx) = STREAM_WRITE_CTX.lock().await.checkout(handle)?;
    let (newctx, n) =
        with_fs(async |fs| efatfs_core::read_at_via_context(fs, ctx, byte_offset, dst).await)
            .await??;
    STREAM_WRITE_CTX
        .lock()
        .await
        .commit(handle, generation, newctx);
    Some(n)
}

/// Persist the handle's accumulated in-memory size/mtime edit to the on-disk directory entry
/// (`efatfs_core::flush_context`). Leaves the slot occupied -- callers may keep writing after a
/// flush (e.g. a periodic mid-recording flush).
async fn stream_flush(handle: u32) -> bool {
    let Some((generation, ctx)) = STREAM_WRITE_CTX.lock().await.checkout(handle) else {
        return false;
    };
    let result = with_fs(async |fs| efatfs_core::flush_context(fs, ctx).await).await;
    match result {
        Some(Some(newctx)) => {
            STREAM_WRITE_CTX
                .lock()
                .await
                .commit(handle, generation, newctx);
            true
        }
        _ => false,
    }
}

/// Truncate the file behind `handle` to `new_len` bytes.
async fn stream_truncate(handle: u32, new_len: u32) -> bool {
    let Some((generation, ctx)) = STREAM_WRITE_CTX.lock().await.checkout(handle) else {
        return false;
    };
    let result = with_fs(async |fs| efatfs_core::truncate_context(fs, ctx, new_len).await).await;
    match result {
        Some(Some((newctx, ()))) => {
            STREAM_WRITE_CTX
                .lock()
                .await
                .commit(handle, generation, newctx);
            true
        }
        _ => false,
    }
}

/// File length in bytes.
async fn stream_size(handle: u32) -> Option<u32> {
    let (generation, ctx) = STREAM_WRITE_CTX.lock().await.checkout(handle)?;
    let (newctx, size) = with_fs(async |fs| efatfs_core::size_context(fs, ctx).await).await??;
    STREAM_WRITE_CTX
        .lock()
        .await
        .commit(handle, generation, newctx);
    Some(size)
}

/// Flush (see [`stream_flush`]) then free `handle`'s slot. Returns whether the flush succeeded;
/// the slot is freed either way (mirrors `deluge_efatfs_file_close`'s "invalid after this call
/// regardless of status" contract at the C-ABI layer).
async fn stream_close(handle: u32) -> bool {
    let flushed = stream_flush(handle).await;
    STREAM_WRITE_CTX.lock().await.remove(handle);
    flushed
}

// --- Persistent stream-write FFI bridge ---------------------------------------
//
// Same spin-bridge discipline as the task-context bridge above — callable from any context. See
// `include/libdeluge/stream_io.h` for the C-side contract of every function below.

/// C-ABI: open a persistent stream-write handle. `mode` matches `DelugeStreamMode`'s
/// declaration-order values (0=READ, 1=WRITE_CREATE, 2=WRITE_CREATE_NEW, 3=WRITE_APPEND).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_open(
    path: *const c_char,
    mode: u8,
    out_handle: *mut u32,
) -> bool {
    if path.is_null() || out_handle.is_null() {
        return false;
    }
    // SAFETY: `path` is a NUL-terminated C string valid for the duration of this call.
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return false,
    };
    match crate::sim_block::block_on(stream_open(path, mode)) {
        Some(h) => {
            // SAFETY: `out_handle` is non-null (checked above), owned by the caller.
            unsafe {
                *out_handle = h;
            }
            true
        }
        None => false,
    }
}

/// C-ABI: write at absolute `byte_offset` (no flush -- see [`stream_write_at`]). `*out_written` is
/// the true byte count written.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_write_at(
    handle: u32,
    byte_offset: u32,
    src: *const u8,
    count: u32,
    out_written: *mut u32,
) -> bool {
    if src.is_null() || out_written.is_null() {
        return false;
    }
    // SAFETY: `src` points at `count` readable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts(src, count as usize) };
    match crate::sim_block::block_on(stream_write_at(handle, byte_offset, buf)) {
        Some(n) => {
            // SAFETY: `out_written` is non-null (checked above).
            unsafe {
                *out_written = n as u32;
            }
            true
        }
        None => false,
    }
}

/// C-ABI: EOF-honest read at absolute `byte_offset` (see [`stream_read_at_via`]). `*out_read` is
/// the true byte count read, `<= count`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_read_at_via(
    handle: u32,
    byte_offset: u32,
    dst: *mut u8,
    count: u32,
    out_read: *mut u32,
) -> bool {
    if dst.is_null() || out_read.is_null() {
        return false;
    }
    // SAFETY: `dst` points at `count` writable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts_mut(dst, count as usize) };
    match crate::sim_block::block_on(stream_read_at_via(handle, byte_offset, buf)) {
        Some(n) => {
            // SAFETY: `out_read` is non-null (checked above).
            unsafe {
                *out_read = n as u32;
            }
            true
        }
        None => false,
    }
}

/// C-ABI: persist the handle's accumulated size/mtime edit to disk.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_flush(handle: u32) -> bool {
    crate::sim_block::block_on(stream_flush(handle))
}

/// C-ABI: truncate to `new_len` bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_truncate(handle: u32, new_len: u32) -> bool {
    crate::sim_block::block_on(stream_truncate(handle, new_len))
}

/// C-ABI: total size in bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_size(handle: u32, out_size: *mut u32) -> bool {
    if out_size.is_null() {
        return false;
    }
    match crate::sim_block::block_on(stream_size(handle)) {
        Some(sz) => {
            // SAFETY: `out_size` is non-null (checked above).
            unsafe {
                *out_size = sz;
            }
            true
        }
        None => false,
    }
}

/// C-ABI: flush and close a persistent stream-write handle, freeing its slot.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_close(handle: u32) -> bool {
    crate::sim_block::block_on(stream_close(handle))
}
