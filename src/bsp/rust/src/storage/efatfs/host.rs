//! The host `embedded-fatfs` mount, for `host_app` builds (harnesses and the emulator): the
//! counterpart of the device mount (`efatfs::device`), with the same handle tables, lock
//! discipline and `deluge_efatfs_*` C ABI over the same [`portable`] primitives, so host runs
//! exercise the device's read path rather than a reimplementation.
//!
//! # Block device
//!
//! [`HostSdBlockDevice`] awaits the SD_BUS-guarded `crate::storage::sd::locked_*_sectors`, as the
//! device block device does. It must not use the synchronous `deluge_block_read`/`_write` C ABI:
//! a spin nested inside the fill task holds its executor, so in a single-threaded sim nothing
//! else runs to release SD_BUS or advance the modeled latency, and the sim deadlocks. Awaiting
//! suspends instead, so modeled latency applies to these reads as well.
//!
//! # Differences from the device mount
//!
//! - No partition-window detection: host images (mtools-built in `sd_image`, formatted in
//!   `memory_card`) are a single FAT volume with no MBR.
//! - The C ABI bridges through `storage::sim_block::block_on`, as on device, but on host that spin
//!   calls a progress hook on each `Pending`: in a single-threaded sim it pumps the
//!   higher-priority executor and advances virtual time; on a threaded host it yields the thread
//!   (see `sim_block::use_threaded_host_hook`). A hookless spin would wedge once the block device
//!   awaits modeled latency.
#![cfg(feature = "host_app")]
#![allow(dead_code)]

// A device build must never also enable `host_app`: this module and the device mount would both
// compile and define every efatfs C-ABI symbol twice, which surfaces as an opaque E0428.
#[cfg(all(target_os = "none", feature = "host_app"))]
compile_error!(
    "efatfs_fs.rs (device) and efatfs_host_shim.rs (host) both define the efatfs C-ABI, \
     including deluge_storage_fs_busy -- a device build (target_os = \"none\") must never also \
     enable the `host_app` feature, or both modules compile into the same binary and duplicate \
     every efatfs_* C-ABI symbol."
);

use aligned::{A4, Aligned};
use block_device_adapters::BufStream;
use block_device_driver::BlockDevice;
use embassy_sync::{blocking_mutex::raw::CriticalSectionRawMutex, mutex::Mutex};
use embedded_fatfs::{DefaultTimeProvider, FileSystem, FsOptions, LossyOemCpConverter};

use super::portable::{self, DirHandleTable, HandleTable, OpenOutcome, TaskFileTable};
use crate::abi::handle::{from_abi, to_abi};
use crate::sys::{
    DelugeHandle, DelugeStatus, DelugeStatus_DELUGE_ERR_IO as DELUGE_ERR_IO,
    DelugeStatus_DELUGE_ERR_NO_FILESYSTEM as DELUGE_ERR_NO_FILESYSTEM,
    DelugeStatus_DELUGE_ERR_NO_MEMORY as DELUGE_ERR_NO_MEMORY,
    DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM, DelugeStatus_DELUGE_OK as DELUGE_OK,
};

/// Host `BlockDevice<512>` over the SD_BUS-guarded `crate::storage::sd::locked_*_sectors`, the
/// same path the device block device takes; see the module doc for why not the synchronous C ABI.
pub struct HostSdBlockDevice;

/// The raw `DelugeStatus` of a failed transfer; exists to satisfy `BlockDevice::Error: Debug`.
#[derive(Debug)]
pub struct HostBlockError(i8);

impl BlockDevice<512> for HostSdBlockDevice {
    type Align = A4;
    type Error = HostBlockError;

    async fn read(
        &mut self,
        block_address: u32,
        data: &mut [Aligned<A4, [u8; 512]>],
    ) -> Result<(), Self::Error> {
        let count = data.len() as u32;
        // SAFETY: `Aligned<A4, [u8; 512]>` is `#[repr(C)]` over the array plus a zero-sized
        // alignment marker, so the slice is a contiguous, padding-free run of 512-byte blocks. The
        // flat view is derived from `data`, covers the same bytes, and does not outlive it.
        let flat = unsafe {
            core::slice::from_raw_parts_mut(data.as_mut_ptr().cast::<u8>(), data.len() * 512)
        };
        crate::storage::sd::locked_read_sectors(block_address, count, flat)
            .await
            .map_err(|_| HostBlockError(-5))
    }

    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<A4, [u8; 512]>],
    ) -> Result<(), Self::Error> {
        let count = data.len() as u32;
        // SAFETY: same layout argument as `read`; shared, read-only view.
        let flat =
            unsafe { core::slice::from_raw_parts(data.as_ptr().cast::<u8>(), data.len() * 512) };
        crate::storage::sd::locked_write_sectors(block_address, count, flat)
            .await
            .map_err(|_| HostBlockError(-5))
    }

    async fn size(&mut self) -> Result<u64, Self::Error> {
        Ok(u64::from(deluge_bsp::sd::total_sectors()) * 512)
    }
}

type Storage = BufStream<HostSdBlockDevice, 512>;
pub type Fs = FileSystem<Storage, DefaultTimeProvider, LossyOemCpConverter>;

// embedded-fatfs's `RefCell` disk assumes exclusive access, so every FS op serializes here. The
// host has real contention: the audio-load task runs on its own thread under `host_app`.
static FS: Mutex<CriticalSectionRawMutex, Option<Fs>> = Mutex::new(None);

/// How many times the filesystem has been mounted; a monotonic counter.
///
/// C++ watches it for the unmounted -> mounted edge that drives
/// `AudioFileManager::firstCardRead()` (and `cardReinserted()`). Sampling the mount state around
/// its own `deluge_efatfs_mount()` call would miss mounts by the directory channel's driver, which
/// mounts on the storage tier; persisting the last-seen value catches the edge whoever mounted. A
/// counter rather than a flag, so no reader consumes the edge from another.
static MOUNT_GENERATION: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// C-ABI: the mount counter; see `MOUNT_GENERATION`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_mount_generation() -> u32 {
    MOUNT_GENERATION.load(core::sync::atomic::Ordering::Acquire)
}

/// Mounts the FS into `FS` over the whole image (no partition window). Idempotent: `Ok(())` at
/// once if already mounted, so the boot mount and [`deluge_efatfs_mount`] can both call it.
pub async fn mount() -> Result<(), DelugeStatus> {
    if FS.lock().await.is_some() {
        return Ok(()); // already mounted
    }
    let storage = BufStream::<HostSdBlockDevice, 512>::new(HostSdBlockDevice);
    let fs = FileSystem::new(storage, FsOptions::new())
        .await
        .map_err(|e| portable::error_to_status(&e))?;
    *FS.lock().await = Some(fs);
    // Bumped only after the FS is installed: a reader that sees the new value can use it.
    MOUNT_GENERATION.fetch_add(1, core::sync::atomic::Ordering::Release);
    Ok(())
}

/// Empties the four handle tables ([`HANDLES`], [`TASK_FILES`], [`DIR_HANDLES`],
/// [`STREAM_WRITE_CTX`]), whose contexts are stale once the [`Fs`] they came from is dropped.
async fn reset_all_handle_tables() {
    *HANDLES.lock().await = HandleTable::new();
    *TASK_FILES.lock().await = TaskFileTable::new();
    *DIR_HANDLES.lock().await = DirHandleTable::new();
    *STREAM_WRITE_CTX.lock().await = HandleTable::new();
}

/// Drops the filesystem and leaves it unmounted, invalidating every handle.
///
/// The directory channel's driver mounts out of this state.
pub async fn unmount() {
    *FS.lock().await = None;
    reset_all_handle_tables().await;
}

/// Drops the FS, empties the handle tables and mounts afresh, for a card swap. Every open handle
/// is invalid afterwards.
pub async fn remount() -> Result<(), DelugeStatus> {
    *FS.lock().await = None;
    reset_all_handle_tables().await;
    mount().await
}

/// Runs `f` with exclusive access to the mounted FS; `None` if nothing is mounted.
///
/// `f` is an `AsyncFnOnce` rather than `FnOnce(&Fs) -> impl Future`: only the async-closure form
/// ties the future's lifetime to the borrowed `&Fs`, so the body can await while holding it.
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

/// The streaming-read handle table, sized at [`portable::MAX_HANDLES`].
///
/// Never held across a [`with_fs`] await: ops check out, do the FS work, and commit (see
/// [`portable`]'s lock discipline).
static HANDLES: Mutex<CriticalSectionRawMutex, HandleTable<{ portable::MAX_HANDLES }>> =
    Mutex::new(HandleTable::new());

/// Opens `path` and stores its detached context in `HANDLES`. Fails with
/// [`OpenOutcome::Failed`] (unmounted, or the open's status) or [`OpenOutcome::TableFull`].
pub async fn open(path: &str) -> OpenOutcome {
    let ctx = match with_fs(async |fs| portable::open_context(fs, path).await).await {
        Some(Ok(ctx)) => ctx,
        Some(Err(status)) => return OpenOutcome::Failed(status),
        None => return OpenOutcome::Failed(DELUGE_ERR_NO_FILESYSTEM),
    };
    match HANDLES.lock().await.insert(ctx) {
        Some(h) => OpenOutcome::Handle(h),
        None => OpenOutcome::TableFull,
    }
}

/// Reads `dst.len()` bytes at absolute `byte_offset` of the file behind `handle`. Returns
/// whether the whole buffer was filled (zero-padded past EOF, see [`portable::read_context`]).
pub async fn read_at(handle: u32, byte_offset: u32, dst: &mut [u8]) -> bool {
    let Some((generation, ctx)) = HANDLES.lock().await.checkout(handle) else {
        return false;
    };

    let result = with_fs(async |fs| portable::read_context(fs, ctx, byte_offset, dst).await).await;

    match result {
        Some(Some((newctx, filled))) => {
            HANDLES.lock().await.commit(handle, generation, newctx);
            filled
        }
        _ => false,
    }
}

/// The size in bytes of the file behind a streaming-read `handle`; `None` if the handle is bad,
/// the FS is unmounted, or the size could not be read.
///
/// Reads `HANDLES`, not the task-file table `deluge_efatfs_file_size` serves: the audio loader
/// opens through [`open`] and must size that file.
pub async fn size_of_handle(handle: u32) -> Option<u32> {
    let (generation, ctx) = HANDLES.lock().await.checkout(handle)?;

    let result = with_fs(async |fs| portable::size_context(fs, ctx).await).await;

    match result {
        Some(Some((newctx, size))) => {
            HANDLES.lock().await.commit(handle, generation, newctx);
            Some(size)
        }
        _ => None,
    }
}

/// Closes `handle`, freeing its slot (no-op if out of range); the generation bump makes any
/// in-flight [`read_at`] write-back stale.
pub async fn close(handle: u32) {
    HANDLES.lock().await.remove(handle);
}

// Streaming-read C ABI. Every bridge in this module spins through `sim_block::block_on` and is
// callable from any context; see the module doc for the host hook that makes that safe.

use core::ffi::{CStr, c_char};

/// C-ABI: opens a sample file for streaming and writes the handle to `*out_handle`.
///
/// Returns `DELUGE_ERR_PARAM` for a null or non-UTF-8 argument, otherwise the open's status.
/// `*out_table_full` (written once the pointers are checked) is true only when `HANDLES` was
/// full, so C++ can report "too many open streams" rather than "file not found".
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_open(
    path: *const c_char,
    out_handle: *mut DelugeHandle,
    out_table_full: *mut bool,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::Open);
    if path.is_null() || out_handle.is_null() || out_table_full.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `path` is a NUL-terminated C string supplied by the caller, valid for the duration
    // of this call.
    let Ok(path) = unsafe { CStr::from_ptr(path) }.to_str() else {
        return DELUGE_ERR_PARAM;
    };
    let (status, table_full) = match crate::storage::sim_block::block_on(open(path)) {
        OpenOutcome::Handle(h) => {
            // SAFETY: `out_handle` is non-null (checked above) and points at memory the caller
            // owns for the duration of this synchronous call.
            unsafe { *out_handle = to_abi(h) };
            (DELUGE_OK, false)
        }
        OpenOutcome::TableFull => (DELUGE_ERR_NO_MEMORY, true),
        OpenOutcome::Failed(status) => (status, false),
    };
    // SAFETY: `out_table_full` is non-null (checked above), owned by the caller for this call.
    unsafe { *out_table_full = table_full };
    status
}

/// C-ABI: closes a streaming file handle, releasing its slot, unconditionally from any context.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_close(handle: DelugeHandle) {
    crate::tier_meter::note(crate::tier_meter::Entry::Close);
    let Some(handle) = from_abi(handle) else {
        return;
    };
    crate::storage::sim_block::block_on(close(handle));
}

/// C-ABI: reads `count` bytes at `byte_offset` of the file behind `handle` (see [`read_at`]).
/// Returns `DELUGE_OK` and `*out_read = count` only if the whole buffer was filled.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_read_at(
    handle: DelugeHandle,
    byte_offset: u32,
    dst: *mut core::ffi::c_void,
    count: u32,
    out_read: *mut u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::ReadAt);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    if dst.is_null() || out_read.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `dst` points at `count` writable bytes owned by the caller for the duration of this
    // synchronous call.
    let buf = unsafe { core::slice::from_raw_parts_mut(dst.cast::<u8>(), count as usize) };
    if !crate::storage::sim_block::block_on(read_at(handle, byte_offset, buf)) {
        return DELUGE_ERR_IO;
    }
    // SAFETY: `out_read` is non-null (checked above), a `u32` the caller owns.
    unsafe { *out_read = count };
    DELUGE_OK
}

// Task-context file and directory tables (see `portable::TaskFileTable` for why they are separate
// from `HANDLES`).

static TASK_FILES: Mutex<CriticalSectionRawMutex, TaskFileTable> = Mutex::new(TaskFileTable::new());
static DIR_HANDLES: Mutex<CriticalSectionRawMutex, DirHandleTable> =
    Mutex::new(DirHandleTable::new());

/// Opens a task-context file. `mode` is a `DelugeFileOpenMode` (0 = READ, 1 = WRITE_CREATE,
/// 2 = WRITE_CREATE_NEW).
///
/// Fails with `DELUGE_ERR_NO_FILESYSTEM` if unmounted, `DELUGE_ERR_PARAM` for an unknown `mode`,
/// `DELUGE_ERR_IO` if [`TASK_FILES`] is full, otherwise the open or create status.
pub(crate) async fn task_file_open(path: &str, mode: u8) -> Result<u32, DelugeStatus> {
    let ctx = with_fs(async |fs| match mode {
        0 => portable::open_context(fs, path).await,
        1 => portable::create_context(fs, path, false).await,
        2 => portable::create_context(fs, path, true).await,
        _ => Err(DELUGE_ERR_PARAM),
    })
    .await
    .ok_or(DELUGE_ERR_NO_FILESYSTEM)??;
    TASK_FILES.lock().await.insert(ctx).ok_or(DELUGE_ERR_IO) // table full
}

/// Reads at the handle's position, advancing it. Returns the real byte count, short at EOF and
/// never zero-padded; this backs the C++ `File::read`.
pub(crate) async fn task_file_read_exact(
    handle: u32,
    dst: &mut [u8],
) -> Result<usize, DelugeStatus> {
    let (generation, ctx, pos) = TASK_FILES
        .lock()
        .await
        .checkout(handle)
        .ok_or(DELUGE_ERR_PARAM)?;
    let (newctx, n) = with_fs(async |fs| portable::read_context_exact(fs, ctx, pos, dst).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)?
        .ok_or(DELUGE_ERR_IO)?;
    TASK_FILES
        .lock()
        .await
        .commit(handle, generation, newctx, pos + n as u32);
    Ok(n)
}

/// Writes at the handle's position, advancing it by the bytes written.
pub(crate) async fn task_file_write(handle: u32, src: &[u8]) -> Result<usize, DelugeStatus> {
    let (generation, ctx, pos) = TASK_FILES
        .lock()
        .await
        .checkout(handle)
        .ok_or(DELUGE_ERR_PARAM)?;
    let (newctx, n) = with_fs(async |fs| portable::write_context(fs, ctx, pos, src).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)?
        .ok_or(DELUGE_ERR_IO)?;
    TASK_FILES
        .lock()
        .await
        .commit(handle, generation, newctx, pos + n as u32);
    Ok(n)
}

/// Moves the handle's position to an absolute offset, without FS access.
///
/// `pub(crate)` so the byte channel's driver (`channel::file_bytes`) can await it.
pub(crate) async fn task_file_seek(handle: u32, offset: u32) -> Result<(), DelugeStatus> {
    if TASK_FILES.lock().await.seek(handle, offset) {
        Ok(())
    } else {
        Err(DELUGE_ERR_PARAM)
    }
}

/// Returns the file length in bytes, leaving the handle's position unchanged.
pub(crate) async fn task_file_size(handle: u32) -> Result<u32, DelugeStatus> {
    let (generation, ctx, pos) = TASK_FILES
        .lock()
        .await
        .checkout(handle)
        .ok_or(DELUGE_ERR_PARAM)?;
    let (newctx, size) = with_fs(async |fs| portable::size_context(fs, ctx).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)?
        .ok_or(DELUGE_ERR_IO)?;
    TASK_FILES
        .lock()
        .await
        .commit(handle, generation, newctx, pos);
    Ok(size)
}

/// Truncates to `new_len` bytes, leaving the handle's position unchanged (as POSIX `ftruncate`
/// does; a later read past the new end returns 0 bytes).
pub(crate) async fn task_file_truncate(handle: u32, new_len: u32) -> Result<(), DelugeStatus> {
    let Some((generation, ctx, pos)) = TASK_FILES.lock().await.checkout(handle) else {
        return Err(DELUGE_ERR_PARAM);
    };
    let result = with_fs(async |fs| portable::truncate_context(fs, ctx, new_len).await).await;
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

/// Closes a task-context file handle, freeing its slot.
pub(crate) async fn task_file_close(handle: u32) {
    TASK_FILES.lock().await.remove(handle);
}

/// Opens `path` as a directory and returns a handle into [`DIR_HANDLES`].
///
/// `pub(crate)` so the directory channel's driver (`channel::file_directory`) can await it.
pub(crate) async fn task_dir_open(path: &str) -> Result<u32, DelugeStatus> {
    let cursor = with_fs(async |fs| portable::readdir_open(fs, path).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)??;
    DIR_HANDLES.lock().await.insert(cursor).ok_or(DELUGE_ERR_IO) // table full
}

/// The result of skipping directory entries too long for the caller's buffer.
enum NextFit {
    Eof,
    Found(portable::DirEntryInfo),
}

/// Advances `handle`'s cursor to the next entry whose name fits `max_name_bytes` (the C buffer,
/// including its NUL), skipping any that do not.
///
/// `Err(DELUGE_ERR_PARAM)` for a bad handle; `Ok(None)` at the end of the directory, which is not
/// an error.
async fn task_dir_read(
    handle: u32,
    max_name_bytes: usize,
) -> Result<Option<portable::DirEntryInfo>, DelugeStatus> {
    let mut dir_table = DIR_HANDLES.lock().await;
    let cursor = dir_table.get_mut(handle).ok_or(DELUGE_ERR_PARAM)?;
    // No `with_fs`: `readdir_next` only walks the in-memory snapshot.
    let next = loop {
        match portable::readdir_next(cursor).ok_or(DELUGE_ERR_IO)? {
            None => break NextFit::Eof,
            Some(info) => {
                if info.name.len() >= max_name_bytes {
                    continue; // too long for the caller's buffer: skip, don't fail the browse
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

/// Frees `handle`'s row in [`DIR_HANDLES`], without FS access.
pub(crate) async fn task_dir_close(handle: u32) {
    DIR_HANDLES.lock().await.remove(handle);
}

/// Frees `handle`'s row without waiting, for a caller that may not block. Returns whether the row
/// was freed.
///
/// `deluge_card_flush` must close the directory channel's handle without blocking the interaction
/// tier; a contended lock leaks one slot until the next mount.
#[allow(dead_code)]
pub(crate) fn try_close_dir(handle: u32) -> bool {
    match DIR_HANDLES.try_lock() {
        Ok(mut table) => {
            table.remove(handle);
            true
        }
        Err(_) => false,
    }
}

// Task-context C ABI (`libdeluge/file_io.h`).

/// C-ABI: opens a task-context file; see `task_file_open` for `mode`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_open(
    path: *const c_char,
    mode: u8,
    out_handle: *mut DelugeHandle,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::FileOpen);
    if path.is_null() || out_handle.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `path` is a NUL-terminated C string valid for the duration of this call.
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    let opened = crate::storage::sim_block::block_on(task_file_open(path, mode));
    match opened {
        Ok(h) => {
            // SAFETY: `out_handle` is non-null (checked above), owned by the caller.
            unsafe {
                *out_handle = to_abi(h);
            }
            DELUGE_OK
        }
        Err(st) => st,
    }
}

/// C-ABI: EOF-honest read at the handle's position (see `task_file_read_exact`); `*out_read` is
/// the real byte count, `<= count`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_read_exact(
    handle: DelugeHandle,
    dst: *mut core::ffi::c_void,
    count: u32,
    out_read: *mut u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::FileReadExact);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    if dst.is_null() || out_read.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `dst` points at `count` writable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts_mut(dst.cast::<u8>(), count as usize) };
    let result = crate::storage::sim_block::block_on(task_file_read_exact(handle, buf));
    match result {
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

/// C-ABI: writes at the handle's position; `*out_written` is the byte count written.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_write(
    handle: DelugeHandle,
    src: *const core::ffi::c_void,
    count: u32,
    out_written: *mut u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::FileWrite);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    if src.is_null() || out_written.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `src` points at `count` readable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts(src.cast::<u8>(), count as usize) };
    let result = crate::storage::sim_block::block_on(task_file_write(handle, buf));
    match result {
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

/// C-ABI: moves the handle's position to an absolute byte offset.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_seek(handle: DelugeHandle, offset: u32) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::FileSeek);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    let result = crate::storage::sim_block::block_on(task_file_seek(handle, offset));
    match result {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// C-ABI: the file's size in bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_size(
    handle: DelugeHandle,
    out_size: *mut u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::FileSize);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    if out_size.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let result = crate::storage::sim_block::block_on(task_file_size(handle));
    match result {
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

/// C-ABI: closes a task-context file handle.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_close(handle: DelugeHandle) {
    crate::tier_meter::note(crate::tier_meter::Entry::FileClose);
    let Some(handle) = from_abi(handle) else {
        return;
    };
    crate::storage::sim_block::block_on(task_file_close(handle));
}

/// C-ABI: opens `path` as a directory for iteration.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_dir_open(
    path: *const c_char,
    out_handle: *mut DelugeHandle,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::DirOpen);
    if path.is_null() || out_handle.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `path` is a NUL-terminated C string valid for the duration of this call.
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    let opened = crate::storage::sim_block::block_on(task_dir_open(path));
    match opened {
        Ok(h) => {
            // SAFETY: `out_handle` is non-null (checked above), owned by the caller.
            unsafe {
                *out_handle = to_abi(h);
            }
            DELUGE_OK
        }
        Err(st) => st,
    }
}

/// C-ABI: reads the next directory entry into the out-params (`deluge_dir_read`'s shape).
///
/// `out_name` must point at `out_name_cap` writable bytes. An entry whose name does not fit is
/// skipped (see `task_dir_read`), never truncated. `*out_has` is false at the end.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_dir_read(
    handle: DelugeHandle,
    out_name: *mut c_char,
    out_name_cap: u32,
    out_is_dir: *mut bool,
    out_size: *mut u32,
    out_modified: *mut u32,
    out_attrs: *mut u8,
    out_has: *mut bool,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::DirRead);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
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
    let result = crate::storage::sim_block::block_on(task_dir_read(handle, out_name_cap as usize));
    match result {
        Ok(None) => {
            // SAFETY: `out_has` is non-null (checked above).
            unsafe {
                *out_has = false;
            }
            DELUGE_OK
        }
        Ok(Some(info)) => {
            let name_bytes = info.name.as_bytes();
            // SAFETY: `task_dir_read` returns only names with `len < out_name_cap`, so the name and
            // its NUL fit; every `out_*` pointer is non-null (checked above) and caller-owned.
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

/// C-ABI: closes a directory handle.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_dir_close(handle: DelugeHandle) {
    crate::tier_meter::note(crate::tier_meter::Entry::DirClose);
    let Some(handle) = from_abi(handle) else {
        return;
    };
    crate::storage::sim_block::block_on(task_dir_close(handle));
}

/// C-ABI: the mounted volume's free and total cluster counts.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stats(
    out_free_clusters: *mut u32,
    out_total_clusters: *mut u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::Stats);
    if out_free_clusters.is_null() || out_total_clusters.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let fut = with_fs(async |fs| portable::fs_stats(fs).await);
    match crate::storage::sim_block::block_on(fut) {
        Some(Ok((free, total))) => {
            // SAFETY: both out-params non-null (checked above), owned by the caller for this call.
            unsafe {
                *out_free_clusters = free;
                *out_total_clusters = total;
            }
            DELUGE_OK
        }
        Some(Err(status)) => status,
        None => DELUGE_ERR_NO_FILESYSTEM,
    }
}

/// C-ABI: mounts the FS if it is not mounted (see [`mount`]). `DELUGE_OK` if mounted, else the
/// mount's status.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_mount() -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::Mount);
    crate::storage::sim_block::block_on(mount())
        .err()
        .unwrap_or(DELUGE_OK)
}

/// C-ABI (`libdeluge/storage_busy.h`): whether an FS operation is in flight.
///
/// Non-blocking and callable from any context; `try_lock` returns `Err` when the mutex is held.
/// The answer is advisory: another executor or thread can take the FS mutex right after the
/// probe.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_storage_fs_busy() -> bool {
    FS.try_lock().is_err()
}

/// C-ABI: drops the filesystem, leaving it unmounted; see [`unmount`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_unmount() {
    crate::tier_meter::note(crate::tier_meter::Entry::Unmount);
    crate::storage::sim_block::block_on(unmount());
}

/// C-ABI: remounts for a card swap; see [`remount`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_remount() -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::Remount);
    crate::storage::sim_block::block_on(remount())
        .err()
        .unwrap_or(DELUGE_OK)
}

/// C-ABI: the mounted volume's cluster size in bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_cluster_size(out_bytes: *mut u32) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::ClusterSize);
    if out_bytes.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let fut = with_fs(async |fs| portable::fs_cluster_size(fs).await);
    match crate::storage::sim_block::block_on(fut) {
        Some(Ok(bytes)) => {
            // SAFETY: `out_bytes` is non-null (checked above), owned by the caller for this call.
            unsafe { *out_bytes = bytes };
            DELUGE_OK
        }
        Some(Err(status)) => status,
        None => DELUGE_ERR_NO_FILESYSTEM,
    }
}

/// Creates a directory and any missing parents.
///
/// `pub(crate)` so the directory channel's driver can await it: the browser reaches it from the
/// interaction tier, which may not wait on the card through the spinning C ABI.
pub(crate) async fn task_mkdir(path: &str) -> Result<(), DelugeStatus> {
    with_fs(async |fs| portable::mkdir(fs, path).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)?
}

/// C-ABI: creates a directory and any missing parents.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_mkdir(path: *const c_char) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::Mkdir);
    if path.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    match crate::storage::sim_block::block_on(task_mkdir(path)) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// Deletes a file or empty directory.
///
/// `pub(crate)` so the byte channel's driver can await it: the startup fail-safe reaches it from a
/// scheduler task, which may not wait on the card through the spinning C ABI.
pub(crate) async fn task_unlink(path: &str) -> Result<(), DelugeStatus> {
    match with_fs(async |fs| portable::unlink(fs, path).await).await {
        Some(result) => result,
        None => Err(DELUGE_ERR_NO_FILESYSTEM),
    }
}

/// C-ABI: deletes a file or empty directory.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_unlink(path: *const c_char) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::Unlink);
    if path.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    match crate::storage::sim_block::block_on(task_unlink(path)) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// Renames or moves a file or directory.
///
/// `pub(crate)` so the byte channel's driver can await it: the settings legacy-path migration
/// reaches it from the interaction tier, which may not wait on the card through the spinning C
/// ABI.
pub(crate) async fn task_rename(old: &str, new: &str) -> Result<(), DelugeStatus> {
    match with_fs(async |fs| portable::rename(fs, old, new).await).await {
        Some(result) => result,
        None => Err(DELUGE_ERR_NO_FILESYSTEM),
    }
}

/// C-ABI: renames or moves a file or directory.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_rename(
    old_path: *const c_char,
    new_path: *const c_char,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::Rename);
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
    match crate::storage::sim_block::block_on(task_rename(old, new)) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// Sets a file or directory's modification time from decomposed fields (packed by
/// [`portable::pack_timestamp`]).
///
/// `pub(crate)` so the byte channel's driver can await it: the SysEx file protocol reaches it from
/// a scheduler task, which may not wait on the card through the spinning C ABI.
pub(crate) async fn task_set_time(
    path: &str,
    year: u16,
    month: u8,
    day: u8,
    hour: u8,
    minute: u8,
    second: u8,
) -> Result<(), DelugeStatus> {
    let packed = portable::pack_timestamp(year, month, day, hour, minute, second);
    match with_fs(async |fs| portable::set_time(fs, path, packed).await).await {
        Some(result) => result,
        None => Err(DELUGE_ERR_NO_FILESYSTEM),
    }
}

/// C-ABI: sets a file or directory's modification time; see `task_set_time`.
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
    crate::tier_meter::note(crate::tier_meter::Entry::SetTime);
    if path.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return DELUGE_ERR_PARAM,
    };
    match crate::storage::sim_block::block_on(task_set_time(
        path, year, month, day, hour, minute, second,
    )) {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

// Persistent stream-write handle table (`libdeluge/stream_io.h`), for the sample recorder. A slot
// holds one `FileContext` for a whole recording; writes do not flush the directory entry until
// `stream_flush` or `stream_close`.

/// The stream-write handle table, sized at [`portable::MAX_STREAM_WRITE_HANDLES`].

static STREAM_WRITE_CTX: Mutex<
    CriticalSectionRawMutex,
    HandleTable<{ portable::MAX_STREAM_WRITE_HANDLES }>,
> = Mutex::new(HandleTable::new());

/// The volume's free cluster count: the async form of `deluge_efatfs_stats`, so the stream
/// channel can answer a space query without holding its caller.
pub(crate) async fn fs_stats_free() -> Option<u32> {
    with_fs(async |fs| portable::fs_stats(fs).await)
        .await?
        .ok()
        .map(|(free, _total)| free)
}

/// Opens `path` as a stream. `mode` is a `DelugeStreamMode` (0 = READ, 1 = WRITE_CREATE,
/// 2 = WRITE_CREATE_NEW, 3 = WRITE_APPEND; the create modes also make a missing parent folder).
///
/// Fails with `DELUGE_ERR_NO_FILESYSTEM` if unmounted, `DELUGE_ERR_IO` if the table is full,
/// otherwise the open's status (`DELUGE_ERR_NOT_FOUND` for a missing file).
pub(crate) async fn stream_open(path: &str, mode: u8) -> Result<u32, DelugeStatus> {
    let ctx = with_fs(async |fs| match mode {
        0 => portable::open_context(fs, path).await,
        1 => portable::create_context(fs, path, false).await,
        2 => portable::create_context(fs, path, true).await,
        3 => portable::open_context(fs, path).await, // append: open existing, no truncation
        _ => Err(DELUGE_ERR_PARAM),
    })
    .await
    .ok_or(DELUGE_ERR_NO_FILESYSTEM)??;
    STREAM_WRITE_CTX
        .lock()
        .await
        .insert(ctx)
        .ok_or(DELUGE_ERR_IO) // table full
}

/// Writes `src` at absolute `byte_offset` without flushing the directory entry
/// ([`portable::write_context_noflush`]). Returns the bytes written; `None` on a bad handle or FS
/// error.
pub(crate) async fn stream_write_at(handle: u32, byte_offset: u32, src: &[u8]) -> Option<usize> {
    let (generation, ctx) = STREAM_WRITE_CTX.lock().await.checkout(handle)?;
    let (newctx, n) =
        with_fs(async |fs| portable::write_context_noflush(fs, ctx, byte_offset, src).await)
            .await??;
    STREAM_WRITE_CTX
        .lock()
        .await
        .commit(handle, generation, newctx);
    Some(n)
}

/// EOF-honest read at absolute `byte_offset`, bounded by the handle's in-memory size
/// ([`portable::read_at_via_context`]), so it sees unflushed writes. Returns the bytes read; `None`
/// on a bad handle or FS error.
pub(crate) async fn stream_read_at_via(
    handle: u32,
    byte_offset: u32,
    dst: &mut [u8],
) -> Option<usize> {
    let (generation, ctx) = STREAM_WRITE_CTX.lock().await.checkout(handle)?;
    let (newctx, n) =
        with_fs(async |fs| portable::read_at_via_context(fs, ctx, byte_offset, dst).await)
            .await??;
    STREAM_WRITE_CTX
        .lock()
        .await
        .commit(handle, generation, newctx);
    Some(n)
}

/// Flushes the handle's in-memory size/mtime to the directory entry
/// ([`portable::flush_context`]). The handle stays open, so writing can continue.
async fn stream_flush(handle: u32) -> bool {
    let Some((generation, ctx)) = STREAM_WRITE_CTX.lock().await.checkout(handle) else {
        return false;
    };
    let result = with_fs(async |fs| portable::flush_context(fs, ctx).await).await;
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

/// Truncates the file behind `handle` to `new_len` bytes.
pub(crate) async fn stream_truncate(handle: u32, new_len: u32) -> bool {
    let Some((generation, ctx)) = STREAM_WRITE_CTX.lock().await.checkout(handle) else {
        return false;
    };
    let result = with_fs(async |fs| portable::truncate_context(fs, ctx, new_len).await).await;
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

/// Returns the file length in bytes.
pub(crate) async fn stream_size(handle: u32) -> Option<u32> {
    let (generation, ctx) = STREAM_WRITE_CTX.lock().await.checkout(handle)?;
    let (newctx, size) = with_fs(async |fs| portable::size_context(fs, ctx).await).await??;
    STREAM_WRITE_CTX
        .lock()
        .await
        .commit(handle, generation, newctx);
    Some(size)
}

/// Flushes (see [`stream_flush`]) and frees `handle`'s slot. Returns whether the flush succeeded;
/// the slot is freed either way.
pub(crate) async fn stream_close(handle: u32) -> bool {
    let flushed = stream_flush(handle).await;
    STREAM_WRITE_CTX.lock().await.remove(handle);
    flushed
}

// Stream-write C ABI (`libdeluge/stream_io.h`).

/// C-ABI: opens a stream handle; see `stream_open` for `mode`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_open(
    path: *const c_char,
    mode: u8,
    out_handle: *mut DelugeHandle,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::StreamOpen);
    if path.is_null() || out_handle.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `path` is a NUL-terminated C string valid for the duration of this call.
    let Ok(path) = unsafe { CStr::from_ptr(path) }.to_str() else {
        return DELUGE_ERR_PARAM;
    };
    match crate::storage::sim_block::block_on(stream_open(path, mode)) {
        Ok(h) => {
            // SAFETY: `out_handle` is non-null (checked above), owned by the caller.
            unsafe { *out_handle = to_abi(h) };
            DELUGE_OK
        }
        Err(status) => status,
    }
}

/// C-ABI: writes at absolute `byte_offset` without flushing (see `stream_write_at`);
/// `*out_written` is the byte count written.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_write_at(
    handle: DelugeHandle,
    byte_offset: u32,
    src: *const core::ffi::c_void,
    count: u32,
    out_written: *mut u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::StreamWriteAt);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    if src.is_null() || out_written.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `src` points at `count` readable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts(src.cast::<u8>(), count as usize) };
    let Some(n) = crate::storage::sim_block::block_on(stream_write_at(handle, byte_offset, buf))
    else {
        return DELUGE_ERR_IO;
    };
    // SAFETY: `out_written` is non-null (checked above).
    unsafe { *out_written = n as u32 };
    DELUGE_OK
}

/// C-ABI: EOF-honest read at absolute `byte_offset` (see `stream_read_at_via`); `*out_read` is
/// the byte count read, `<= count`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_read_at_via(
    handle: DelugeHandle,
    byte_offset: u32,
    dst: *mut core::ffi::c_void,
    count: u32,
    out_read: *mut u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::StreamReadAtVia);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    if dst.is_null() || out_read.is_null() {
        return DELUGE_ERR_PARAM;
    }
    // SAFETY: `dst` points at `count` writable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts_mut(dst.cast::<u8>(), count as usize) };
    let Some(n) = crate::storage::sim_block::block_on(stream_read_at_via(handle, byte_offset, buf))
    else {
        return DELUGE_ERR_IO;
    };
    // SAFETY: `out_read` is non-null (checked above).
    unsafe { *out_read = n as u32 };
    DELUGE_OK
}

/// C-ABI: truncates to `new_len` bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_truncate(
    handle: DelugeHandle,
    new_len: u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::StreamTruncate);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    if crate::storage::sim_block::block_on(stream_truncate(handle, new_len)) {
        DELUGE_OK
    } else {
        DELUGE_ERR_IO
    }
}

/// C-ABI: the file's size in bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_size(
    handle: DelugeHandle,
    out_size: *mut u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::StreamSize);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    if out_size.is_null() {
        return DELUGE_ERR_PARAM;
    }
    let Some(size) = crate::storage::sim_block::block_on(stream_size(handle)) else {
        return DELUGE_ERR_IO;
    };
    // SAFETY: `out_size` is non-null (checked above).
    unsafe { *out_size = size };
    DELUGE_OK
}

/// C-ABI: flushes and closes a stream handle, freeing its slot.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_stream_close(handle: DelugeHandle) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::StreamClose);
    let Some(handle) = from_abi(handle) else {
        return DELUGE_ERR_PARAM;
    };
    if crate::storage::sim_block::block_on(stream_close(handle)) {
        DELUGE_OK
    } else {
        DELUGE_ERR_IO
    }
}

abi_pin!(
    deluge_efatfs_close,
    deluge_efatfs_cluster_size,
    deluge_efatfs_dir_close,
    deluge_efatfs_dir_open,
    deluge_efatfs_dir_read,
    deluge_efatfs_file_close,
    deluge_efatfs_file_open,
    deluge_efatfs_file_read_exact,
    deluge_efatfs_file_seek,
    deluge_efatfs_file_size,
    deluge_efatfs_file_write,
    deluge_efatfs_mkdir,
    deluge_efatfs_mount,
    deluge_efatfs_mount_generation,
    deluge_efatfs_open,
    deluge_efatfs_read_at,
    deluge_efatfs_remount,
    deluge_efatfs_rename,
    deluge_efatfs_set_time,
    deluge_efatfs_stats,
    deluge_efatfs_stream_close,
    deluge_efatfs_stream_open,
    deluge_efatfs_stream_read_at_via,
    deluge_efatfs_stream_size,
    deluge_efatfs_stream_truncate,
    deluge_efatfs_stream_write_at,
    deluge_efatfs_unlink,
    deluge_efatfs_unmount,
    deluge_storage_fs_busy,
);
