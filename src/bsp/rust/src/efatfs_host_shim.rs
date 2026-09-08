//! The host counterpart of `efatfs_fs.rs` — mounts the SAME vendored
//! `embedded-fatfs` over a host block device and reuses the storage-generic
//! `efatfs_core` (`HandleTable` + `open_context`/`read_context`) UNCHANGED, so
//! a host harness measures the *real* read path, not a parallel
//! reimplementation. See `docs/superpowers/specs/2026-07-22-r0-harness-enablement-design.md`
//! §4.1 (the shim) and Appendix A (the mount-strategy spike this file's block
//! device follows exactly).
//!
//! ## The block device: AWAIT `crate::sd::locked_*_sectors`, exactly like the device
//!
//! [`HostSdBlockDevice`] `.await`s the SD_BUS-guarded async helpers
//! `crate::sd::locked_read_sectors`/`locked_write_sectors` — a byte-for-byte
//! mirror of the device `fat_block_device::SdBlockDevice`. It does NOT call the
//! synchronous `deluge_block_read`/`deluge_block_write` C-ABI — see below for
//! why that path is unsound here.
//!
//! ### Why the sync `deluge_block_read` path was WRONG (the Lens 1 deadlock)
//!
//! `deluge_block_read`/`_write` are *synchronous* C-ABI functions: internally
//! they drive the transfer with `block_on_fiber` (on the worker fiber) or
//! `embassy_futures::block_on` (off it) — `sd.rs` keeps that dispatch, which is why this
//! section still describes it. The streaming READ path runs on the
//! async fill task (`streaming_loader::streaming_fill_task`), which is OFF the
//! worker fiber — so a shim read reached that way took the off-fiber
//! `block_on` branch: a NON-yielding busy spin. When a recorder card-write on
//! the fiber had suspended mid-transfer holding `SD_BUS` (`block_on_fiber`
//! yields the fiber but keeps the guard), the fill task's `block_on(SD_BUS
//! .lock())` could never acquire it AND never returned control to the executor
//! — so the write's fiber could never resume to release `SD_BUS`. On Lens 1's
//! single-threaded discrete-event driver that is a hard deadlock inside
//! `executor.poll()` (100% CPU, virtual clock frozen); Lens 2 dodged it only by
//! having a second OS thread. The device `SdBlockDevice` never had the bug
//! because it `.await`s (yields) instead of nesting a `block_on`. This shim now
//! does the same: on the fill task the read genuinely suspends, letting the
//! executor advance the clock, fire the `sim_latency` pump `Timer`, resume the
//! fiber, finish the write, and release `SD_BUS`. Modeled latency now applies
//! to efatfs streaming reads exactly as it does to the C-FatFS path, so Lens 1
//! measures a real margin for this read path.
//!
//! ## Host vs. device differences from `efatfs_fs.rs`
//!
//! - No partition-window detection: the harness's SD images (mtools
//!   `mformat`/`mcopy`, no MBR — see `sd_image.rs`) are always a single FAT
//!   volume spanning the whole raw image, exactly like `fs_differential`'s
//!   `EFatFs::mount()`. The device path needs `partition_window()` because a
//!   real card may carry an MBR; the host image never does.
//! - Same `embassy_sync` `Mutex<CriticalSectionRawMutex, _>` shape as the
//!   device (not a bespoke `RefCell`): `embassy-sync` + `critical-section`
//!   (`std` feature) are already cross-target deps, so reusing the identical
//!   lock discipline here is zero extra risk and keeps this a faithful mirror
//!   of `efatfs_fs.rs` rather than a new design. Host is still logically
//!   single-threaded (one `block_on`/one executor thread), so contention never
//!   actually happens — the `Mutex` is here for shape-fidelity, not for a real
//!   concurrency need.
//! - The FFI bridge below uses ONE unconditional `crate::sim_block::block_on` — no
//!   `on_fiber()` dispatch — exactly matching `efatfs_fs.rs` after the I/O-class collapse.
//!   The symmetry is the point: `efatfs_fs.rs` is `#![cfg(target_os = "none")]`, so no host
//!   build compiles a line of it, and if this mirror kept the old branch then every host gate
//!   (Lens 1's wedge detector, Lens 2 TSan, the goldens) would be exercising a shape the
//!   device no longer runs.
//!
//!   An earlier version of this doc warned that "always falling back to
//!   `embassy_futures::block_on` (a non-yielding spin) would be unsafe once the block device
//!   awaits modeled latency". That warning was correct and still is — but it is about the
//!   HOOKLESS spin. `sim_block::block_on` is not that: on host it invokes a registered
//!   progress hook on every `Pending`, which in `lens1_vt_sim` pumps the higher-priority
//!   executor (where `sim_latency::pump` lives) and advances the virtual clock. That is the
//!   whole reason `sim_block` exists, and the case is proven, not assumed —
//!   `--selftest-block-nested` drives exactly this shape (a spin inside a task with MAIN's
//!   `executor.poll()` already on the stack) and completes at the modeled latency. On a
//!   threaded host the hook yields instead, since there a `Pending` is satisfied by another
//!   thread. See `sim_block`'s module doc and `use_threaded_host_hook`.
//!
//! Test infrastructure only (host_app-gated) — no device path touched.
#![cfg(feature = "host_app")]
#![allow(dead_code)]

// Structural guard, not just documentation: this file's own inclusion (see this `#![cfg]` and
// main.rs's matching `mod` attribute) depends ONLY on `feature = "host_app"`, with no
// `not(target_os = "none")` -- so nothing stops a device build from also turning on `host_app`.
// `efatfs_fs.rs` (the device efatfs impl) is included independently on `target_os = "none"` --
// see its own `#![cfg]`. If both conditions were ever true at once, both files would compile into
// the same binary and doubly define the whole efatfs C-ABI, including `deluge_storage_fs_busy`,
// which would surface as an opaque `E0428` with no hint why. This file compiles on every build
// that could trigger the overlap (it's the `host_app` half of the pair), so it's the right place
// to fail loudly with the real explanation instead.
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

use crate::efatfs_core::{self, DirHandleTable, HandleTable, OpenOutcome, TaskFileTable};
use crate::sys::{
    DelugeStatus, DelugeStatus_DELUGE_ERR_IO as DELUGE_ERR_IO,
    DelugeStatus_DELUGE_ERR_NO_FILESYSTEM as DELUGE_ERR_NO_FILESYSTEM,
    DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM, DelugeStatus_DELUGE_OK as DELUGE_OK,
};

/// Host `block_device_driver::BlockDevice<512>` that routes every read/write
/// through the SD_BUS-guarded async helpers `crate::sd::locked_read_sectors`/
/// `locked_write_sectors` — the same async dispatch the device
/// `fat_block_device::SdBlockDevice` uses. See this module's doc for why this
/// (not the synchronous `deluge_block_read`/`deluge_block_write` C-ABI, nor a
/// bespoke `RamDisk`) is the load-bearing design choice.
pub struct HostSdBlockDevice;

/// Wraps `deluge_block_read`/`_write`'s raw `DelugeStatus` (`i8`) code, just to
/// satisfy `BlockDevice::Error: Debug` — no richer handling needed on this
/// harness-only path.
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
        // AWAIT the SD_BUS-guarded async helper, exactly like the device
        // `fat_block_device::SdBlockDevice::read` — NOT the synchronous
        // `deluge_block_read` C-ABI. See this module's doc for why the sync
        // path deadlocks Lens 1's single-threaded virtual clock.
        //
        // SAFETY: `Aligned<A4, [u8; 512]>` is `#[repr(C)]` over the inner
        // `[u8; 512]` plus a zero-sized alignment marker, so a
        // `[Aligned<A4, [u8; 512]>]` slice is a contiguous run of 512-byte
        // blocks with no inter-element padding; reinterpreting it as
        // `data.len() * 512` flat bytes is sound, and the resulting `&mut [u8]`
        // is validly derived from `data` and does not outlive it. Identical
        // reinterpret to the device `SdBlockDevice`.
        let flat = unsafe {
            core::slice::from_raw_parts_mut(data.as_mut_ptr().cast::<u8>(), data.len() * 512)
        };
        crate::sd::locked_read_sectors(block_address, count, flat)
            .await
            .map_err(|_| HostBlockError(-5))
    }

    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<A4, [u8; 512]>],
    ) -> Result<(), Self::Error> {
        let count = data.len() as u32;
        // See `read` above — AWAIT the SD_BUS-guarded async helper, mirroring
        // the device `SdBlockDevice::write`, not the sync `deluge_block_write`.
        // SAFETY: same layout argument as `read`, read-only direction.
        let flat =
            unsafe { core::slice::from_raw_parts(data.as_ptr().cast::<u8>(), data.len() * 512) };
        crate::sd::locked_write_sectors(block_address, count, flat)
            .await
            .map_err(|_| HostBlockError(-5))
    }

    async fn size(&mut self) -> Result<u64, Self::Error> {
        Ok(u64::from(deluge_bsp::sd::total_sectors()) * 512)
    }
}

type Storage = BufStream<HostSdBlockDevice, 512>;
pub type Fs = FileSystem<Storage, DefaultTimeProvider, LossyOemCpConverter>;

// Same single-owner shape as the device `efatfs_fs.rs` (see its doc) — host is
// logically single-threaded, so contention never actually occurs, but reusing
// the identical `Mutex` shape costs nothing and keeps this a faithful mirror.
static FS: Mutex<CriticalSectionRawMutex, Option<Fs>> = Mutex::new(None);

/// Mount the FS, storing it in [`FS`]. Idempotent: returns `Ok(())`
/// immediately if a FS is already mounted, so both the eager boot mount and a
/// later C-ABI [`deluge_efatfs_mount`] call can invoke this safely. No
/// partition-window detection (see module doc) — the harness image is always
/// a single raw FAT volume. Called (`.await`ed) from the host_app/lens boot
/// task, a genuine async context: the mount's block reads go through
/// [`HostSdBlockDevice`], which now `.await`s `crate::sd::locked_*_sectors`,
/// so they suspend and resume normally on the executor — no
/// `set_off_fiber_instant` wrap or special-casing needed here.
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
    let storage = BufStream::<HostSdBlockDevice, 512>::new(HostSdBlockDevice);
    let fs = FileSystem::new(storage, FsOptions::new())
        .await
        .map_err(|_| ())?;
    *FS.lock().await = Some(fs);
    // Bumped only on the transition, with the FS installed: a reader that sees the new value can
    // rely on the filesystem being usable.
    MOUNT_GENERATION.fetch_add(1, core::sync::atomic::Ordering::Release);
    Ok(())
}

/// Reset the four handle tables ([`HANDLES`], [`TASK_FILES`], [`DIR_HANDLES`],
/// [`STREAM_WRITE_CTX`]) back to empty. Same rationale as the device
/// `efatfs_fs::reset_all_handle_tables` -- re-`new()`ing each under its own
/// lock IS the reset.
async fn reset_all_handle_tables() {
    *HANDLES.lock().await = HandleTable::new();
    *TASK_FILES.lock().await = TaskFileTable::new();
    *DIR_HANDLES.lock().await = DirHandleTable::new();
    *STREAM_WRITE_CTX.lock().await = HandleTable::new();
}

/// Drop the mounted FS, reset the four handle tables, and re-mount fresh. See
/// the device `efatfs_fs::remount` for the full card-swap rationale.
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
    *FS.lock().await = None; // drop the old Fs
    reset_all_handle_tables().await;
    mount().await // re-mount fresh
}

/// Run `f` with exclusive access to the mounted FS (the ONLY entry point).
/// Returns `None` if [`mount`] hasn't run (or failed) yet. See the device
/// `efatfs_fs::with_fs` for why `f` must be an `AsyncFnOnce`.
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

// --- File-handle table — composes `efatfs_core`'s split primitives under the
// SAME lock discipline as the device `efatfs_fs.rs` (HANDLES never held across
// a `with_fs` await; see that file's doc for the full rationale). Deliberately
// NOT `HandleTable::read_at_owned` (that convenience holds the table across
// the FS await) — mirroring the device composition keeps this a faithful
// shape-for-shape port, even though host's single executor thread means the
// two orderings are equally deadlock-free here.
static HANDLES: Mutex<CriticalSectionRawMutex, HandleTable<{ efatfs_core::MAX_HANDLES }>> =
    Mutex::new(HandleTable::new());

/// Open `path`, detach it to a [`embedded_fatfs::FileContext`], and stash it in
/// a free slot. Returns the slot index as [`OpenOutcome::Handle`], or
/// specifically why the open failed — see the device `efatfs_fs::open`'s doc
/// (this is a faithful mirror).
pub async fn open(path: &str) -> OpenOutcome {
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
/// `handle`. Returns `true` iff the full buffer was filled. This is what
/// `ProdOps::read` calls on host (matching the device `efatfs_fs::read_at`
/// signature).
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
        _ => false,
    }
}

/// The size, in bytes, of the file behind `handle`; `None` if the handle is out
/// of range/free, the FS is unmounted, or the size could not be read. This is
/// what `ProdOps::size_of` calls on host (a faithful mirror of the device
/// `efatfs_fs::size_of_handle`, including its use of the streaming-read
/// [`HANDLES`] table rather than the separate task-file table — see that
/// function's doc).
pub async fn size_of_handle(handle: u32) -> Option<u32> {
    let (generation, ctx) = HANDLES.lock().await.checkout(handle)?;

    let result = with_fs(async |fs| efatfs_core::size_context(fs, ctx).await).await;

    match result {
        Some(Some((newctx, size))) => {
            HANDLES.lock().await.commit(handle, generation, newctx);
            Some(size)
        }
        _ => None,
    }
}

/// Close `handle`, freeing its slot. No-op for an out-of-range handle.
pub async fn close(handle: u32) {
    HANDLES.lock().await.remove(handle);
}

// --- FFI bridge --------------------------------------------------------------
//
// Host sibling of `efatfs_fs.rs`'s FFI bridge, and now shape-identical to it: one
// unconditional `crate::sim_block::block_on`, callable from any context, no `on_fiber()`
// dispatch and no rejection. See this module's doc for why the symmetry is load-bearing (the
// device file is device-only, so this mirror is what every host gate actually runs) and why a
// hook-driven spin is safe here where a hookless one would not be.

use core::ffi::{CStr, c_char};

/// C-ABI: open a sample file for streaming; writes the handle to `*out_handle`.
/// Returns false if the path/pointer is null or invalid, the FS is unmounted,
/// or the open failed. `*out_table_full` is always written on a `false`
/// return — true iff the failure was specifically [`HANDLES`] being full (see
/// the device `deluge_efatfs_open`'s doc, which this mirrors).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_open(
    path: *const c_char,
    out_handle: *mut u32,
    out_table_full: *mut bool,
) -> bool {
    if path.is_null() || out_handle.is_null() || out_table_full.is_null() {
        return false;
    }
    // SAFETY: `path` is a NUL-terminated C string supplied by the caller,
    // valid for the duration of this call (same contract as the device FFI).
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(p) => p,
        Err(_) => return false,
    };
    // Drive the async open the SAME way the device bridge (`efatfs_fs.rs`) does: one
    // non-yielding spin, whatever the calling context. What makes that safe on a
    // single-threaded harness is that this is `sim_block::block_on`, not
    // `embassy_futures::block_on` — the registered progress hook pumps the higher-priority
    // executor and advances the virtual clock on every `Pending`, so an open-path read that
    // needs the executor to advance (pump `Timer`, release `SD_BUS`) still completes. A
    // HOOKLESS spin here would livelock, which is the deadlock this module's `read` fix
    // addresses; see this module's doc.
    let opened = crate::sim_block::block_on(open(path));
    match opened {
        OpenOutcome::Handle(h) => {
            // SAFETY: `out_handle`/`out_table_full` are non-null (checked above) and point at
            // memory the caller owns for the duration of this synchronous call.
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

/// C-ABI: close a streaming file handle. Same unconditional spin as
/// [`deluge_efatfs_open`] — though `close` issues no SD reads (it only frees a [`HANDLES`]
/// slot), so it can barely pend at all.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_close(handle: u32) {
    crate::sim_block::block_on(close(handle));
}

/// C-ABI: host sibling of `efatfs_fs::deluge_efatfs_read_at`. Same unconditional
/// `sim_block::block_on` spin as [`deluge_efatfs_open`] — the progress hook is what lets an SD
/// read pend without livelocking the single-threaded harness (see this module's doc).
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
    // SAFETY: `dst` points at `count` writable bytes owned by the caller for this call.
    let buf = unsafe { core::slice::from_raw_parts_mut(dst, count as usize) };
    let filled = crate::sim_block::block_on(read_at(handle, byte_offset, buf));
    if filled {
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
// Host sibling of `efatfs_fs.rs`'s task-context tables/bridge -- same split
// rationale (a separate position-carrying [`TaskFileTable`], distinct from the
// streaming [`HANDLES`]) and the same unconditional `sim_block::block_on` spin every function
// in this module now uses. See that
// file's doc comments for the design; comments here focus on host-specific
// differences only.

static TASK_FILES: Mutex<CriticalSectionRawMutex, TaskFileTable> = Mutex::new(TaskFileTable::new());
static DIR_HANDLES: Mutex<CriticalSectionRawMutex, DirHandleTable> =
    Mutex::new(DirHandleTable::new());

/// `DelugeFileOpenMode` mode selector matching `file_io.h`'s C enum's implicit
/// declaration-order values: 0 = READ, 1 = WRITE_CREATE, 2 = WRITE_CREATE_NEW.
/// See `efatfs_fs.rs::task_file_open` for the granular-error rationale (identical here).
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

/// Open `path` as a directory. `pub(crate)` for the same reason as `efatfs_fs.rs`'s: the directory
/// channel AWAITS this, where the blocking C ABI below spins on it.
pub(crate) async fn task_dir_open(path: &str) -> Result<u32, DelugeStatus> {
    let cursor = with_fs(async |fs| efatfs_core::readdir_open(fs, path).await)
        .await
        .ok_or(DELUGE_ERR_NO_FILESYSTEM)??;
    DIR_HANDLES.lock().await.insert(cursor).ok_or(DELUGE_ERR_IO) // table full
}

/// Outcome of advancing a `DirCursor` (`efatfs_core`) past zero or more
/// too-long-for-the-C-buffer entries to either a fitting entry or
/// end-of-directory. See `efatfs_fs.rs`'s identical type for the rationale.
enum NextFit {
    Eof,
    Found(efatfs_core::DirEntryInfo),
}

/// Advance `handle`'s cursor to the next entry whose name fits in
/// `max_name_bytes`, skipping any that don't. See `efatfs_fs.rs::task_dir_read`
/// for the full rationale (identical here).
async fn task_dir_read(
    handle: u32,
    max_name_bytes: usize,
) -> Result<Option<efatfs_core::DirEntryInfo>, DelugeStatus> {
    let mut dir_table = DIR_HANDLES.lock().await;
    let cursor = dir_table.get_mut(handle).ok_or(DELUGE_ERR_PARAM)?;
    // `readdir_next` is FS-free (it only walks the in-memory snapshot
    // `cursor` already holds), so this does not route through `with_fs` --
    // see `efatfs_fs.rs::task_dir_read` for the full rationale (identical
    // here).
    let next = loop {
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

/// Release `handle`'s row in [`DIR_HANDLES`]. FS-free -- it touches no image.
pub(crate) async fn task_dir_close(handle: u32) {
    DIR_HANDLES.lock().await.remove(handle);
}

/// Release `handle`'s row without awaiting, for a caller that may not block. Host counterpart of
/// `efatfs_fs.rs`'s `try_close_dir` -- see that one for why an eject needs a non-waiting close.
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
// Same unconditional `sim_block::block_on` spin as [`deluge_efatfs_open`] above. See
// `include/libdeluge/file_io.h` for the C-side contract of every function below.

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
    let opened = crate::sim_block::block_on(task_file_open(path, mode));
    match opened {
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
    let result = crate::sim_block::block_on(task_file_read(handle, buf));
    match result {
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
    let result = crate::sim_block::block_on(task_file_read_exact(handle, buf));
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
    let result = crate::sim_block::block_on(task_file_write(handle, buf));
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

/// C-ABI: move the handle's cursor to an absolute byte offset.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_seek(handle: u32, offset: u32) -> DelugeStatus {
    let result = crate::sim_block::block_on(task_file_seek(handle, offset));
    match result {
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
    let result = crate::sim_block::block_on(task_file_size(handle));
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

/// C-ABI: truncate to `new_len` bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_file_truncate(handle: u32, new_len: u32) -> DelugeStatus {
    let result = crate::sim_block::block_on(task_file_truncate(handle, new_len));
    match result {
        Ok(()) => DELUGE_OK,
        Err(st) => st,
    }
}

/// C-ABI: close a task-context file handle.
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
    let opened = crate::sim_block::block_on(task_dir_open(path));
    match opened {
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

/// C-ABI: read the next directory entry. See `efatfs_fs.rs`'s identical
/// function for the full contract this mirrors.
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
    let result = crate::sim_block::block_on(task_dir_read(handle, out_name_cap as usize));
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

/// C-ABI: close a directory handle.
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
    let fut = with_fs(async |fs| efatfs_core::fs_stats(fs).await);
    let result = crate::sim_block::block_on(fut).flatten();
    match result {
        Some((free, total)) => {
            // SAFETY: both out-params non-null (checked above), owned by the caller for this call.
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
/// [`mount`]). True if mounted (or already was), false on failure.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_mount() -> bool {
    let fut = mount();
    crate::sim_block::block_on(fut).is_ok()
}

/// C-ABI: whether the FS is currently mounted (pure state check, no I/O). Lets a caller distinguish
/// a fresh `mount()` transition from the already-mounted case, since `mount()`'s own success return
/// can't tell them apart (it's idempotent).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_is_mounted() -> bool {
    let fut = with_fs(async |_fs| ());
    crate::sim_block::block_on(fut).is_some()
}

/// C-ABI (libdeluge/storage_owner.h): is a filesystem operation in flight? Host mirror of the
/// device impl in `efatfs_fs.rs` — see that file's `deluge_storage_fs_busy` for the full
/// rationale (non-blocking, callable from any context, `Err` means held, check-then-act is sound
/// here because there's no `await` between this probe and the caller's own FS access — do not
/// "harden" this into an atomic).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_storage_fs_busy() -> bool {
    FS.try_lock().is_err()
}

/// C-ABI: drop the mounted FS + reset the four handle tables + re-mount fresh
/// (see [`remount`]).
/// C-ABI: drop the filesystem, leaving it unmounted. See [`unmount`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_unmount() {
    crate::sim_block::block_on(unmount());
}
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_remount() -> bool {
    let fut = remount();
    crate::sim_block::block_on(fut).is_ok()
}

/// C-ABI: the mounted volume's cluster (allocation-unit) size, in bytes.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_efatfs_cluster_size(out_bytes: *mut u32) -> bool {
    if out_bytes.is_null() {
        return false;
    }
    let fut = with_fs(async |fs| efatfs_core::fs_cluster_size(fs).await);
    let result = crate::sim_block::block_on(fut).flatten();
    match result {
        Some(bytes) => {
            // SAFETY: `out_bytes` is non-null (checked above), owned by the caller for this call.
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
/// fields, packed here via `efatfs_core::pack_timestamp`).
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
// Host sibling of `efatfs_fs.rs`'s persistent stream-write table/bridge -- same rationale (a
// single `FileContext` held across an entire recording, advanced via `write_context_noflush` and
// only persisted to the on-disk directory entry on flush/close) and the same unconditional
// `sim_block::block_on` spin every function in this module now uses. See that
// file's doc comments for the design; comments here focus on host-specific differences only.

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
pub(crate) async fn stream_write_at(handle: u32, byte_offset: u32, src: &[u8]) -> Option<usize> {
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
/// the slot is freed either way.
async fn stream_close(handle: u32) -> bool {
    let flushed = stream_flush(handle).await;
    STREAM_WRITE_CTX.lock().await.remove(handle);
    flushed
}

// --- Persistent stream-write FFI bridge ---------------------------------------
//
// Same unconditional `sim_block::block_on` spin as [`deluge_efatfs_open`] above. See
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
    let opened = crate::sim_block::block_on(stream_open(path, mode));
    match opened {
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
    let result = crate::sim_block::block_on(stream_write_at(handle, byte_offset, buf));
    match result {
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
    let result = crate::sim_block::block_on(stream_read_at_via(handle, byte_offset, buf));
    match result {
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
    let result = crate::sim_block::block_on(stream_size(handle));
    match result {
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
