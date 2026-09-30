//! Storage-generic, host-testable core of the embedded-fatfs streaming read
//! path. This module holds the load-bearing logic — the
//! file-handle table, the `FileContext` detach/reattach, the generation guard,
//! and the fill loop — with NO device dependencies: it is generic over the
//! embedded-fatfs `FileSystem`'s IO type, uses no `static`s, no fibers, and no
//! `SdBlockDevice`, so a host test can
//! drive the exact same code over a host/sim block device.
//!
//! `efatfs_fs.rs` (device-only) wraps this: it owns the mounted `FileSystem`
//! and the `HandleTable` behind two async `Mutex`es and composes the SPLIT
//! primitives below ([`HandleTable::checkout`] / [`read_context`] /
//! [`HandleTable::commit`]) under its lock discipline — HANDLES is cloned-out
//! and released BEFORE the FS mutex is taken, then re-taken to write back.
//! Holding the table borrow across the FS await (as a monolithic `&mut self`
//! op would) is what [`HandleTable::read_at_owned`] does for single-threaded
//! host/sim callers; the device path must NOT use it, or it would invert the
//! FS→HANDLES order `open` takes and deadlock.
//!
//! Handle model: the streaming read path opens each sample once, detaches it to
//! a [`FileContext`] (cheap to `Clone`, carries `current_cluster` so forward
//! reads skip re-walking the cluster chain), and re-attaches a fresh `File`
//! from that context on every read. No per-op heap allocation.

#![allow(dead_code)]

// `readdir_open`'s snapshot `Vec` needs `alloc` -- this module
// compiles both `no_std` (device) and `std` (host_app / fs_differential's
// `#[path]` include), and `extern crate` visibility is per-module in Rust
// 2018+, so this can't rely on another module's `extern crate alloc;`.
// Harmless under a `std` build too -- `alloc` is always in the sysroot there.
extern crate alloc;
use alloc::string::String;
use alloc::vec::Vec;

use embedded_fatfs::{
    Date, DateTime, File, FileContext, FileSystem, OemCpConverter, ReadWriteSeek, Time,
    TimeProvider,
};
// embedded-fatfs keeps its own `io` traits `pub(crate)`; `File`'s `Read`/`Seek`
// impls are the public `embedded_io_async` ones, so bring those into scope to
// drive the fill loop / absolute seek below.
use embedded_io_async::{Read as _, Seek as _, SeekFrom, Write as _};

// `crate::sys` is always the real bindgen output (from `include/libdeluge/*.h`)
// wherever this module is compiled: either `target_os = "none"` (device) or
// `feature = "host_app"` (host, C++ app linked in) -- see main.rs's `mod sys`
// cfg gates and this module's own `mod efatfs_core` gate. So, unlike modules
// compiled on a plain (non-`host_app`) host build, there's no hand-mirrored
// `sys_host` stand-in to keep in sync here.
use crate::sys::{
    DelugeStatus, DelugeStatus_DELUGE_ERR_EXISTS as DELUGE_ERR_EXISTS,
    DelugeStatus_DELUGE_ERR_IO as DELUGE_ERR_IO,
    DelugeStatus_DELUGE_ERR_NO_FILESYSTEM as DELUGE_ERR_NO_FILESYSTEM,
    DelugeStatus_DELUGE_ERR_NO_SPACE as DELUGE_ERR_NO_SPACE,
    DelugeStatus_DELUGE_ERR_NOT_EMPTY as DELUGE_ERR_NOT_EMPTY,
    DelugeStatus_DELUGE_ERR_NOT_FOUND as DELUGE_ERR_NOT_FOUND,
    DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM,
};

/// Max concurrent streamed-READ files (the [`HANDLES`](crate::storage::efatfs::device)
/// table). One resident [`Sample`](crate) holds one of these for its whole
/// lifetime (see `SampleStream::open_read_stream`), so this bounds how many
/// samples can be simultaneously resident in a song — 128 gives real
/// multi-track projects (e.g. a 19-sample song) comfortable headroom over the
/// previous 16, at a modest static cost (80 B/slot × 128 = 10 KiB).
pub const MAX_HANDLES: usize = 128;

/// Max concurrent persistent stream-WRITE handles (the sample recorder's
/// `STREAM_WRITE_CTX` table, `efatfs_fs.rs`/`efatfs_host_shim.rs`). The
/// recorder opens one write handle per in-progress recording; a handful of
/// concurrent recordings (this device supports at most a few audio inputs at
/// once) is the realistic ceiling, so this stays small and independent of
/// [`MAX_HANDLES`] rather than paying the same 128-slot cost for no reason.
pub const MAX_STREAM_WRITE_HANDLES: usize = 8;

/// One handle-table slot: an optional detached [`FileContext`] plus a
/// generation counter bumped on every claim (`insert`) and free (`remove`) of
/// that index, so a deferred write-back can detect the slot having been
/// recycled for a different file while its FS work was in flight.
struct Slot {
    generation: u32,
    ctx: Option<FileContext>,
}

/// A fixed-capacity table of detached [`FileContext`]s keyed by a `u32`
/// handle, generic over its slot count `N` so callers with very different
/// concurrency needs ([`MAX_HANDLES`] for the streaming read path,
/// [`MAX_STREAM_WRITE_HANDLES`] for the recorder write path) don't have to
/// share one capacity.
///
/// The device layer keeps one of these behind an async `Mutex`; host/sim
/// callers own one directly. All methods are FS-agnostic except the two that
/// take a `&FileSystem` ([`read_at_owned`] and, indirectly, the free functions
/// [`open_context`] / [`read_context`] below).
pub struct HandleTable<const N: usize> {
    slots: [Slot; N],
}

impl<const N: usize> Default for HandleTable<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> HandleTable<N> {
    pub const fn new() -> Self {
        Self {
            slots: [const {
                Slot {
                    generation: 0,
                    ctx: None,
                }
            }; N],
        }
    }

    /// Stash `ctx` in the lowest free slot, bumping that slot's generation.
    /// Returns the slot index as the handle, or `None` if the table is full.
    pub fn insert(&mut self, ctx: FileContext) -> Option<u32> {
        for (i, slot) in self.slots.iter_mut().enumerate() {
            if slot.ctx.is_none() {
                slot.ctx = Some(ctx);
                slot.generation = slot.generation.wrapping_add(1);
                return Some(i as u32);
            }
        }
        None
    }

    /// Clone the [`FileContext`] behind `handle` out of the table, together with
    /// the slot's current generation. `None` if the handle is out of range or
    /// the slot is free. Clones (not takes) so a failed read leaves the slot
    /// valid.
    pub fn checkout(&self, handle: u32) -> Option<(u32, FileContext)> {
        let slot = self.slots.get(handle as usize)?;
        let ctx = slot.ctx.clone()?;
        Some((slot.generation, ctx))
    }

    /// Write `newctx` back into `handle`'s slot ONLY if the slot is still
    /// occupied AND its generation still matches `captured_generation`. A
    /// mismatch means `remove` (and possibly a later `insert` reusing this
    /// index for a *different* file) ran while the FS work was in flight;
    /// writing back then would splice this file's advanced context onto an
    /// unrelated file's slot, and `File::new_from_context`'s validation can't
    /// catch it (the old file's directory entry is still valid on disk). The
    /// `is_some()` check is belt-and-suspenders; the generation equality is
    /// what actually prevents the identity confusion.
    pub fn commit(&mut self, handle: u32, captured_generation: u32, newctx: FileContext) {
        if let Some(slot) = self.slots.get_mut(handle as usize) {
            if slot.generation == captured_generation && slot.ctx.is_some() {
                slot.ctx = Some(newctx);
            }
        }
    }

    /// Free `handle`'s slot. No-op for an out-of-range handle. Bumps the slot's
    /// generation so any deferred write-back captured before this `remove` is
    /// detected as stale even if no subsequent `insert` reuses the index.
    pub fn remove(&mut self, handle: u32) {
        if let Some(slot) = self.slots.get_mut(handle as usize) {
            slot.ctx = None;
            slot.generation = slot.generation.wrapping_add(1);
        }
    }

    /// Single-threaded convenience: checkout → [`read_context`] → commit under
    /// one exclusive `&mut self` borrow. For host tests ONLY — the device path must compose the split primitives
    /// under its two mutexes instead (see the module doc's lock-discipline
    /// note), never this, or it would hold the table across the FS await and
    /// deadlock against `open`'s FS→table order.
    pub async fn read_at_owned<IO, TP, OCC>(
        &mut self,
        fs: &FileSystem<IO, TP, OCC>,
        handle: u32,
        byte_offset: u32,
        dst: &mut [u8],
    ) -> bool
    where
        IO: ReadWriteSeek,
        TP: TimeProvider,
        OCC: OemCpConverter,
    {
        let Some((generation, ctx)) = self.checkout(handle) else {
            return false;
        };
        let Some((newctx, filled)) = read_context(fs, ctx, byte_offset, dst).await else {
            return false;
        };
        self.commit(handle, generation, newctx);
        filled
    }
}

/// Outcome of an [`HandleTable::insert`]-backed streaming-read open
/// (`efatfs_fs::open` / `efatfs_host_shim::open`): either a fresh handle, or
/// specifically *why* it failed. Table-full is deliberately distinguishable
/// from every other open failure (bad path, unmounted FS): it means a real,
/// valid file is being silently dropped rather than that anything is
/// actually missing, so the `deluge_efatfs_open` FFI bridges surface it to
/// C++ as a dedicated `Error::TOO_MANY_OPEN_STREAMS`, not the generic
/// `Error::FILE_NOT_FOUND` a bare `Option<u32>` would collapse it into.
pub enum OpenOutcome {
    Handle(u32),
    /// The open itself failed: `DELUGE_ERR_NO_FILESYSTEM` for an unmounted FS, otherwise
    /// [`open_context`]'s status (`DELUGE_ERR_NOT_FOUND` for a missing file, ...).
    Failed(DelugeStatus),
    TableFull,
}

/// Fill `dst` completely from `f`'s current position. Returns `true` if the
/// whole buffer was filled (either by real reads, or by zero-padding a tail
/// that ran past logical EOF — see below), `false` only on an actual read
/// error. embedded-fatfs has no `read_exact`, so loop `Read` by hand.
///
/// The streaming loader's last-cluster read is sector-rounded
/// (`begin_fill` in `async_fill.cpp` computes
/// `numSectors = ceil((audioDataEnd - clusterStart) / 512)`), and
/// `audioDataEnd` is frequently NOT sector-aligned (e.g. a WAV whose `data`
/// chunk ends the file). That legitimately asks for up to ~511 bytes past the
/// file's logical size — but never past the last cluster's on-disk
/// allocation, which FAT always pads up to the cluster boundary. The retired
/// raw-sector C-FatFS reader tolerated this by construction (it read whole
/// allocated sectors, unbounded by file_size); `File::read` is a LOGICAL-file
/// reader and returns `Ok(0)` at EOF instead. Rather than fail the whole
/// fill, treat EOF as "the rest is unused cluster padding" and zero it —
/// `dst[filled..]` is deterministic and the convert/stitch pipeline never
/// consumes past the real audio-data length anyway.
///
/// CALLER CONTRACT: this makes EOF non-distinguishable from a valid short tail —
/// a read that begins at/beyond EOF (`filled == 0` on the first `Ok(0)`) also
/// returns `true` with an all-zero buffer, NOT an error. Callers must therefore
/// bound the request to within the file (at most ~1 cluster past logical EOF, as
/// `begin_fill` does via `audioDataLengthBytes` clamped to the real on-disk file
/// size in `Sample::finalizeAfterLoad`). A caller that lets `byte_offset` land
/// fully past EOF would silently read zeros as if valid data. Today the only
/// production caller is the streaming `begin_fill` path, which enforces this.
async fn fill<IO, TP, OCC>(f: &mut File<'_, IO, TP, OCC>, dst: &mut [u8]) -> bool
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
{
    let mut filled = 0;
    while filled < dst.len() {
        match f.read(&mut dst[filled..]).await {
            Ok(0) => {
                // EOF before dst is full: zero-pad the rest and report success.
                dst[filled..].fill(0);
                return true;
            }
            Ok(n) => filled += n,
            Err(_) => return false,
        }
    }
    true
}

/// Open `path` over `fs` and detach it to a [`FileContext`] (open → `close`).
/// `Err` (mapped via [`error_to_status`]) if the open failed -- e.g.
/// `DELUGE_ERR_NOT_FOUND` for a missing path.
pub async fn open_context<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    path: &str,
) -> Result<FileContext, DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let f = fs
        .root_dir()
        .open_file(path)
        .await
        .map_err(|e| error_to_status(&e))?;
    f.close().await.map_err(|e| error_to_status(&e))
}

/// Re-attach a `File` from `ctx`, seek to absolute `byte_offset`, fill `dst`,
/// and detach again. Returns the advanced [`FileContext`] plus whether the
/// whole buffer was filled; `None` on any FS error (reattach/seek/read/close).
/// Seeks absolutely every call, so `current_cluster` correctness does not depend
/// on reusing the returned context — reusing it only makes forward seeks cheap.
pub async fn read_context<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    ctx: FileContext,
    byte_offset: u32,
    dst: &mut [u8],
) -> Option<(FileContext, bool)>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let mut f = File::new_from_context(ctx, fs).await.ok()?;
    f.seek(SeekFrom::Start(u64::from(byte_offset))).await.ok()?;
    let filled = fill(&mut f, dst).await;
    let newctx = f.close().await.ok()?;
    Some((newctx, filled))
}

/// EOF-honest counterpart of [`read_context`]/[`fill`]. Loops
/// `File::read` the same way, but on the FIRST `Ok(0)` (real EOF, or a
/// request that started at/beyond it) stops and returns the true accumulated
/// byte count in `0..=dst.len()` — it does NOT zero-pad the remainder of
/// `dst` the way `fill` does for the streaming-read path's last-cluster
/// tolerance. Task-context callers (the write path's read-modify-write users,
/// size probes, etc.) need to know the real file length, not a padded one.
/// `None` only on an actual FS error (reattach/seek/read/close), same as
/// `read_context`.
pub async fn read_context_exact<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    ctx: FileContext,
    byte_offset: u32,
    dst: &mut [u8],
) -> Option<(FileContext, usize)>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let mut f = File::new_from_context(ctx, fs).await.ok()?;
    f.seek(SeekFrom::Start(u64::from(byte_offset))).await.ok()?;
    let mut filled = 0;
    while filled < dst.len() {
        match f.read(&mut dst[filled..]).await {
            Ok(0) => break,
            Ok(n) => filled += n,
            Err(_) => return None,
        }
    }
    let newctx = f.close().await.ok()?;
    Some((newctx, filled))
}

/// Re-attach a `File` from `ctx`, seek to absolute `byte_offset`, write all of
/// `src` (looping `File::write` `write_all`-style since embedded-fatfs's own
/// `write_all` returns `()` rather than a byte count), and detach again.
/// Mirrors `read_context`'s detach/reattach/seek discipline. `close` (via
/// `File`'s `flush`) is what persists the advanced size/mtime dir-entry edit
/// to disk — `File::write` alone only updates the in-memory context (see
/// `file.rs`'s `update_dir_entry_after_write`). `None` on any FS error.
pub async fn write_context<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    ctx: FileContext,
    byte_offset: u32,
    src: &[u8],
) -> Option<(FileContext, usize)>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let mut f = File::new_from_context(ctx, fs).await.ok()?;
    f.seek(SeekFrom::Start(u64::from(byte_offset))).await.ok()?;
    let mut written = 0;
    while written < src.len() {
        match f.write(&src[written..]).await {
            Ok(0) => break,
            Ok(n) => written += n,
            Err(_) => return None,
        }
    }
    let newctx = f.close().await.ok()?;
    Some((newctx, written))
}

/// Reattach `ctx`, seek to absolute `byte_offset`, write all of
/// `src` (`write_context`'s loop), then `File::detach()` instead of
/// `close()` — the in-memory size advances (`update_dir_entry_after_write`,
/// same as `write_context`) but the on-disk directory entry is NOT touched.
/// Pairs with [`flush_context`]: a long-lived buffered writer (the sample
/// recorder) calls this once per chunk and only flushes once, at finalize,
/// instead of paying a dir-entry flush on every write. `None` on any FS
/// error (reattach/seek/write).
pub async fn write_context_noflush<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    ctx: FileContext,
    byte_offset: u32,
    src: &[u8],
) -> Option<(FileContext, usize)>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let mut f = File::new_from_context(ctx, fs).await.ok()?;
    f.seek(SeekFrom::Start(u64::from(byte_offset))).await.ok()?;
    let mut written = 0;
    while written < src.len() {
        match f.write(&src[written..]).await {
            Ok(0) => break,
            Ok(n) => written += n,
            Err(_) => return None,
        }
    }
    Some((f.detach(), written))
}

/// Reattach `ctx` and `File::close()` it — flushing the
/// accumulated in-memory size/mtime edit built up by one or more prior
/// [`write_context_noflush`] calls to the on-disk directory entry. The
/// finalize half of the persistent-write-handle pair. `None` on any FS error
/// (reattach/flush).
pub async fn flush_context<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    ctx: FileContext,
) -> Option<FileContext>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let f = File::new_from_context(ctx, fs).await.ok()?;
    f.close().await.ok()
}

/// Reattach `ctx`, seek to absolute `byte_offset`, and read —
/// bounded by the context's IN-MEMORY size (`File::size`'s
/// `DirEntryEditor.size`, which [`write_context_noflush`] advances even
/// though it has not been flushed to disk), so this correctly reads back
/// data written earlier in the same unflushed session even though an
/// independent open-by-path reader would still see the stale (pre-write)
/// on-disk size. EOF-honest short count, same loop as
/// [`read_context_exact`]. Detaches (no flush) on the way out, preserving
/// the dirty write state for a later [`flush_context`]. `None` on any FS
/// error (reattach/seek/read).
pub async fn read_at_via_context<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    ctx: FileContext,
    byte_offset: u32,
    dst: &mut [u8],
) -> Option<(FileContext, usize)>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let mut f = File::new_from_context(ctx, fs).await.ok()?;
    f.seek(SeekFrom::Start(u64::from(byte_offset))).await.ok()?;
    let mut filled = 0;
    while filled < dst.len() {
        match f.read(&mut dst[filled..]).await {
            Ok(0) => break,
            Ok(n) => filled += n,
            Err(_) => return None,
        }
    }
    Some((f.detach(), filled))
}

/// File length in bytes, via `Seek(End(0))` — `File::size` is private to
/// embedded-fatfs's own `file` module, so the portable way to read a file's
/// length from outside the crate is the same trick any `Seek` consumer uses:
/// seeking to the end returns the new absolute position, which IS the length.
/// Leaves the detached context's cursor at EOF; harmless, since every
/// `read_context`/`write_context`/`read_context_exact` reattach seeks
/// absolutely before doing anything else. `None` on any FS error.
pub async fn size_context<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    ctx: FileContext,
) -> Option<(FileContext, u32)>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let mut f = File::new_from_context(ctx, fs).await.ok()?;
    let size = f.seek(SeekFrom::End(0)).await.ok()?;
    let newctx = f.close().await.ok()?;
    Some((newctx, size as u32))
}

/// Truncate the file to `new_len` bytes. embedded-fatfs's `File::truncate`
/// truncates AT THE FILE'S CURRENT POSITION (sets size = offset), so seek to
/// `new_len` first, then truncate. `None` on any FS error (including a
/// `new_len` past the current size — `Seek` clamps to EOF rather than
/// growing the file, so this primitive can only shrink, matching every task
/// context caller today).
pub async fn truncate_context<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    ctx: FileContext,
    new_len: u32,
) -> Option<(FileContext, ())>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let mut f = File::new_from_context(ctx, fs).await.ok()?;
    f.seek(SeekFrom::Start(u64::from(new_len))).await.ok()?;
    f.truncate().await.ok()?;
    let newctx = f.close().await.ok()?;
    Some((newctx, ()))
}

// --- Path ops (create/unlink/rename/mkdir/set_time) -------------
//
// Unlike the handle-based ops above, these operate directly on `fs.root_dir()`
// plus a `'/'`-separated path -- there is no open `FileContext` to reattach.
// `create_context` is the one exception: it opens/creates the file, then
// detaches it to a `FileContext` exactly like `open_context` does, so its
// caller gets a handle to install into the table. Every crate error maps to
// a granular `DelugeStatus` via `error_to_status`; the device layer
// (`efatfs_fs.rs`) surfaces that directly as the task-context result code.

/// Create `path`, returning a detached [`FileContext`] for the new file --
/// mirrors [`open_context`]'s open→`close()` detach, but via `create_file`
/// instead of `open_file`.
///
/// `exclusive == false` is WRITE_CREATE: open-or-create, then truncate to
/// empty (embedded-fatfs's own `create_file` opens an existing file
/// as-is -- see its doc comment -- so the truncate here is what gives
/// WRITE_CREATE its "starts empty" semantics, matching `EFatFs::write_new`'s
/// create+truncate pairing).
///
/// `exclusive == true` is WRITE_CREATE_NEW: `Err(DELUGE_ERR_EXISTS)` if `path` already exists.
/// embedded-fatfs has no atomic create-if-absent primitive (`Dir::create_file`
/// always opens-or-creates), so this checks `Dir::exists` first -- a
/// check-then-create race is unreachable here: task-context file ops are
/// single-threaded, both on host (this test binary's single `block_on`) and
/// on device (one task drives the mounted `FileSystem` at a time under
/// `efatfs_fs::with_fs`'s mutex).
pub async fn create_context<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    path: &str,
    exclusive: bool,
) -> Result<FileContext, DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let root = fs.root_dir();
    // Ensure the parent directory path exists (mkdir -p) FIRST — before the exclusive existence
    // check below, which itself errors (not `Ok(false)`) on a path whose parent is missing.
    // embedded-fatfs' create_file requires the parent, returning NotFound otherwise -- creating it
    // here makes write-create "just work" for a nested path (e.g. SampleRecorder writing into a
    // fresh SAMPLES/RESAMPLE folder), on device and host alike; mkdir no-ops an already-existing
    // parent.
    if let Some(slash) = path.rfind('/') {
        if slash > 0 {
            mkdir(fs, &path[..slash]).await?;
        }
    }
    if exclusive && root.exists(path).await.map_err(|e| error_to_status(&e))? {
        return Err(DELUGE_ERR_EXISTS);
    }
    let mut f = root
        .create_file(path)
        .await
        .map_err(|e| error_to_status(&e))?;
    f.truncate().await.map_err(|e| error_to_status(&e))?;
    f.close().await.map_err(|e| error_to_status(&e))
}

/// Delete the file or empty directory at `path`. `Err` (mapped via
/// [`error_to_status`]) on any FS error -- `DELUGE_ERR_NOT_FOUND` for a
/// non-existent path, `DELUGE_ERR_NOT_EMPTY` for a non-empty directory.
pub async fn unlink<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    path: &str,
) -> Result<(), DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    fs.root_dir()
        .remove(path)
        .await
        .map_err(|e| error_to_status(&e))
}

/// Free + total cluster counts of the mounted volume. `Err` (mapped via [`error_to_status`]) if
/// the query failed.
pub async fn fs_stats<IO, TP, OCC>(fs: &FileSystem<IO, TP, OCC>) -> Result<(u32, u32), DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let s = fs.stats().await.map_err(|e| error_to_status(&e))?;
    Ok((s.free_clusters(), s.total_clusters()))
}

/// The mounted volume's cluster (allocation-unit) size, in bytes. `Err` (mapped via
/// [`error_to_status`]) if the query failed.
pub async fn fs_cluster_size<IO, TP, OCC>(fs: &FileSystem<IO, TP, OCC>) -> Result<u32, DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    fs.stats()
        .await
        .map(|s| s.cluster_size())
        .map_err(|e| error_to_status(&e))
}

/// Create the directory at `path`, creating any missing parent directories too
/// (`mkdir -p` semantics). `Err` (mapped via [`error_to_status`]) on any FS
/// error, e.g. `DELUGE_ERR_EXISTS` if a FILE already occupies `path`.
///
/// `embedded-fatfs`' `create_dir` requires the immediate parent to already exist
/// (it returns `NotFound` otherwise) but is idempotent on an already-existing
/// directory, so we build the path one ancestor prefix at a time. This matches the
/// portable folder-creation contract callers rely on: a `mkdir` that reports
/// `NOT_FOUND` for a missing parent so incremental callers (e.g.
/// `SampleRecorder`, creating a nested `SAMPLES/RESAMPLE` recording folder on a
/// fresh card) can create it — but the efatfs C-ABI reports only success/failure
/// as a bool, losing that distinction. Making mkdir recursive here restores the
/// expected behaviour uniformly for every efatfs caller, device and host alike.
pub async fn mkdir<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    path: &str,
) -> Result<(), DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let root = fs.root_dir();
    // Create each ancestor prefix (the substring up to each interior '/') before the
    // full path. Skips empty components (a leading or doubled '/'); create_dir no-ops
    // an existing dir, so re-creating shared ancestors is harmless.
    for (idx, ch) in path.char_indices() {
        if ch == '/' && idx > 0 && !path[..idx].ends_with('/') {
            root.create_dir(&path[..idx])
                .await
                .map_err(|e| error_to_status(&e))?;
        }
    }
    root.create_dir(path)
        .await
        .map_err(|e| error_to_status(&e))?;
    Ok(())
}

/// Rename/move `old` to `new`, both paths relative to the volume root.
/// `Err` (mapped via [`error_to_status`]) on any FS error, including
/// `DELUGE_ERR_EXISTS` for `new` already existing.
pub async fn rename<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    old: &str,
    new: &str,
) -> Result<(), DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let root = fs.root_dir();
    root.rename(old, &root, new)
        .await
        .map_err(|e| error_to_status(&e))
}

/// Set `path`'s modified-time directory-entry field from `timestamp`, a
/// packed 32-bit FAT date/time matching the standard FatFs `get_fattime()` /
/// `FILINFO::fdate,ftime` convention: the high 16 bits are the DOS date
/// (`(year-1980)<<9 | month<<5 | day`), the low 16 bits are the DOS time
/// (`hour<<11 | min<<5 | sec/2`) -- i.e. the same halves `Date`/`Time` encode,
/// concatenated as `(date << 16) | time`.
///
/// `File::set_modified` only updates the in-memory entry (same caveat as
/// `write_context`'s doc comment); `close()`'s flush is what persists it.
/// `Err` (mapped via [`error_to_status`]) on any FS error -- `DELUGE_ERR_PARAM`
/// if `timestamp` decodes to an out-of-range
/// date/time component (`Date`/`Time` panic on out-of-range fields, so this
/// validates by hand instead of decoding blind).
pub async fn set_time<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    path: &str,
    timestamp: u32,
) -> Result<(), DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let dos_date = (timestamp >> 16) as u16;
    let dos_time = timestamp as u16;
    let year = (dos_date >> 9) + 1980;
    let month = (dos_date >> 5) & 0xF;
    let day = dos_date & 0x1F;
    let hour = dos_time >> 11;
    let min = (dos_time >> 5) & 0x3F;
    let sec = (dos_time & 0x1F) * 2;
    if !(1..=12).contains(&month) || !(1..=31).contains(&day) || hour > 23 || min > 59 || sec > 59 {
        return Err(DELUGE_ERR_PARAM);
    }

    let mut f = fs
        .root_dir()
        .open_file(path)
        .await
        .map_err(|e| error_to_status(&e))?;
    #[allow(deprecated)]
    // embedded-fatfs deprecates set_modified in favor of a custom TimeProvider;
    // task-context callers (deluge's `fileSetTimeDate`/rename-with-timestamp
    // callers) set an explicit timestamp, not "now", so there is no
    // `TimeProvider` shaped for this.
    f.set_modified(DateTime::new(
        Date::new(year, month, day),
        Time::new(hour, min, sec, 0),
    ));
    f.close().await.map_err(|e| error_to_status(&e))?;
    Ok(())
}

// --- Directory enumeration --------------------------------------------------
//
// `Dir`/`DirIter` borrow `&FileSystem` (`Dir::iter(&self) -> DirIter<'a,
// ..>`, `crates/embedded-fatfs/src/dir.rs:134`), so a live iterator can't be
// held across an `.await` boundary or returned from a function bound to a
// short-lived `&FileSystem` borrow -- the same "no borrow across the FS call"
// constraint every other primitive in this module sidesteps via detached,
// owned `FileContext`s. `readdir_open` sidesteps it the same way: it walks
// the WHOLE directory to completion under one `with_fs` call and returns an
// owned snapshot (`DirCursor`, a `Vec` + a walk index), not a live borrow.
//
// The UI identifies files by path, not by a locator handle, so `DirCursor`
// entries are plain `DirEntryInfo` -- there is no separate open-by-locator
// fast-open path here.

/// One directory entry snapshotted by [`readdir_open`]/[`readdir_next`].
///
/// `modified` is the same packed 32-bit FAT date/time [`set_time`]
/// documents: `(dos_date << 16) | dos_time`, `dos_date =
/// (year-1980)<<9 | month<<5 | day`, `dos_time = hour<<11 | min<<5 | sec/2`
/// -- matching C-FatFS's `FILINFO::fdate,ftime` convention this codebase
/// already uses elsewhere.
///
/// `attrs` is the raw FAT attribute byte
/// (`embedded_fatfs::FileAttributes::bits()`: READ_ONLY=0x01, HIDDEN=0x02,
/// SYSTEM=0x04, VOLUME_ID=0x08, DIRECTORY=0x10, ARCHIVE=0x20), bit-for-bit
/// C-FatFS's `FILINFO::fattrib` (`AM_*`).
/// `name` is an exact-sized heap `String`, NOT an inline `heapless::String<256>`.
/// A snapshot holds one of these per directory entry, so an inline 256-byte name
/// buffer set the entry cost at ~272 bytes whatever the real filename length --
/// which put the whole-directory snapshot over the FS arena at only a couple of
/// hundred entries, and the overflow surfaced as `handle_alloc_error` (a panic,
/// i.e. a dead device) rather than an error. Typical preset names are 20-30 bytes,
/// so paying for the actual length raises the ceiling several-fold.
#[derive(Clone)]
pub struct DirEntryInfo {
    pub name: String,
    pub is_dir: bool,
    pub size: u32,
    pub modified: u32,
    pub attrs: u8,
}

/// A directory snapshot collected by [`readdir_open`]: an owned `Vec` of
/// entries plus a walk index -- see this section's module comment for why
/// this can't be a live `DirIter` borrow instead.
pub struct DirCursor {
    entries: Vec<DirEntryInfo>,
    idx: usize,
}

/// Pack an embedded-fatfs `DateTime` into the `(dos_date << 16) | dos_time`
/// convention [`set_time`] documents and consumes, using `Date`/`Time`'s
/// public `year`/`month`/`day`/`hour`/`min`/`sec` fields (the crate's own
/// `encode()` doing the same math is `pub(crate)`).
fn pack_fat_datetime(dt: DateTime) -> u32 {
    let dos_date = ((dt.date.year - 1980) << 9) | (dt.date.month << 5) | dt.date.day;
    let dos_time = (dt.time.hour << 11) | (dt.time.min << 5) | (dt.time.sec / 2);
    (u32::from(dos_date) << 16) | u32::from(dos_time)
}

/// Open `path` as a directory and snapshot ALL of its entries in one pass
/// (see this section's module comment for why a snapshot, not a live
/// iterator). Skips the `.`/`..` pseudo-entries embedded-fatfs's iterator
/// yields for non-root directories -- C-FatFS's `f_readdir` never surfaces
/// those, so this matches its enumeration surface (same filter
/// `fs_differential::efatfs::EFatFs::read_dir` already applies). `path`
/// empty (or all `/`) opens the volume root. `Err` (mapped via
/// [`error_to_status`]) on any FS error, including `path` not naming a
/// directory.
pub async fn readdir_open<IO, TP, OCC>(
    fs: &FileSystem<IO, TP, OCC>,
    path: &str,
) -> Result<DirCursor, DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let root = fs.root_dir();
    let dir = if path.trim_matches('/').is_empty() {
        root
    } else {
        root.open_dir(path).await.map_err(|e| error_to_status(&e))?
    };
    let mut iter = dir.iter();
    let mut entries = Vec::new();
    while let Some(r) = iter.next().await {
        let e = r.map_err(|e| error_to_status(&e))?;
        let name = e.file_name();
        if name == "." || name == ".." {
            continue;
        }
        // EVERY allocation on this path is fallible. A snapshot is proportional to
        // the directory's size, the arena backing it is fixed (96 KiB on device),
        // and an infallible `push`/`String::from` reaches `handle_alloc_error` when
        // it runs out -- which panics, and a panic on the calling task spins
        // there forever and takes the front panel with it. A folder with too many
        // files must degrade to an error the browser can display, never to a brick.
        let mut owned = String::new();
        if owned.try_reserve_exact(name.len()).is_err() {
            return Err(DELUGE_ERR_IO);
        }
        owned.push_str(&name);
        let is_dir = e.is_dir();
        let size = if is_dir { 0 } else { e.len() as u32 };
        let info = DirEntryInfo {
            name: owned,
            is_dir,
            size,
            modified: pack_fat_datetime(e.modified()),
            attrs: e.attributes().bits(),
        };
        if entries.try_reserve(1).is_err() {
            return Err(DELUGE_ERR_IO);
        }
        entries.push(info);
    }
    Ok(DirCursor { entries, idx: 0 })
}

/// Advance `cursor` and return the next entry. Outer `Option` is an FS
/// error (`None`); inner `Option` is end-of-directory (`None`). `cursor` is
/// an owned snapshot (see [`readdir_open`]), so this never touches the FS or
/// does any FS I/O -- it deliberately takes no `&FileSystem` parameter, unlike
/// this module's other `fs`-taking primitives: routing an in-memory snapshot
/// read through the single FS mutex would serialize dir-page reads behind any
/// in-flight streaming SD read for no reason. An FS-error outer `None` cannot
/// occur for a snapshot cursor today, but the signature leaves room for a
/// future non-snapshot cursor that could fail mid-walk.
///
/// Consumes each entry as it passes: the returned name is MOVED out of the
/// snapshot, leaving an empty `String` behind. This is sound because the cursor
/// only ever walks forward (`idx` is monotonic and there is no rewind API), and it
/// matters for two reasons — cloning the name would allocate on every read, and
/// allocating is exactly what cannot be relied upon here, since a large snapshot is
/// itself what leaves the arena short; and freeing each name as it is handed over
/// lowers the peak the walk holds. Consequently a given entry can only be read once.
#[allow(clippy::unnecessary_wraps)]
pub fn readdir_next(cursor: &mut DirCursor) -> Option<Option<DirEntryInfo>> {
    let Some(slot) = cursor.entries.get_mut(cursor.idx) else {
        return Some(None); // end of directory
    };
    let info = DirEntryInfo {
        name: core::mem::take(&mut slot.name),
        is_dir: slot.is_dir,
        size: slot.size,
        modified: slot.modified,
        attrs: slot.attrs,
    };
    cursor.idx += 1;
    Some(Some(info))
}

/// Pack a decomposed timestamp into the `(dos_date << 16) | dos_time` convention
/// [`set_time`] consumes -- the encode-side counterpart of that function's own
/// decode. Shared here (rather than duplicated in `efatfs_fs.rs` AND
/// `efatfs_host_shim.rs`) because both C-ABI bridges receive the task-context
/// `DelugeTimestamp`'s decomposed fields (year/month/day/hour/minute/second, see
/// `include/libdeluge/types.h`) from `deluge_efatfs_set_time` and need the same
/// packing before calling [`set_time`]. No range validation here -- `set_time`
/// already validates the packed result before decoding it back.
pub fn pack_timestamp(year: u16, month: u8, day: u8, hour: u8, minute: u8, second: u8) -> u32 {
    let dos_date = ((year - 1980) << 9) | (u16::from(month) << 5) | u16::from(day);
    let dos_time = (u16::from(hour) << 11) | (u16::from(minute) << 5) | u16::from(second / 2);
    (u32::from(dos_date) << 16) | u32::from(dos_time)
}

// --- Task-context file-handle table + dir-cursor table ----------------------
//
// The streaming-read `HandleTable` above is deliberately NOT reused for
// task-context file I/O: task-context `deluge::io::File` callers `seek()` then
// `read()`/`write()` with no explicit byte offset (`file_io.h`'s
// `deluge_file_seek`/`_read`/`_write` contract), so each handle needs a
// PERSISTED cursor position threaded through every op -- something the
// streaming table's `Slot` (generation + `FileContext` only) has no field for
// and does not need (the streaming read path always passes an explicit
// absolute `byte_offset`). Extending the streaming `Slot`/`HandleTable` to
// carry a position would touch the already-proven streaming path for a
// field it never uses; a separate, small `TaskFileTable` keeps the two
// concerns apart. `readdir_next` is synchronous (no `.await`, no FS access --
// see the module comment above [`DirCursor`]), so [`DirHandleTable`] needs no
// generation-guarded checkout/commit dance: its slots are claimed/read/freed
// under one lock, never split across an FS-mutex await.

/// Max concurrent task-context file handles. Kept independent of
/// [`MAX_HANDLES`] — task-context file I/O (menu browsing, project
/// save/load) is not high-concurrency the way resident streamed samples are.
pub const MAX_TASK_FILES: usize = 16;

struct TaskFileSlot {
    generation: u32,
    ctx: Option<FileContext>,
    /// The handle's current byte position -- `deluge_file_seek` sets it directly
    /// (no FS access needed); every read/write reattach explicitly seeks the
    /// underlying `File` to this value before touching it (see `read_context`/
    /// `write_context`'s absolute-seek discipline), so this is authoritative
    /// regardless of whatever cursor state the detached `FileContext` itself
    /// carries.
    position: u32,
}

/// A fixed-capacity table of task-context file handles: a detached
/// [`FileContext`] plus a persisted cursor `position`, keyed by a `u32` handle.
/// Composed under the SAME checkout/`with_fs`/commit discipline [`HandleTable`]
/// documents (never held across the FS-mutex await) -- `efatfs_fs.rs`/
/// `efatfs_host_shim.rs` own one behind an async `Mutex`.
pub struct TaskFileTable {
    slots: [TaskFileSlot; MAX_TASK_FILES],
}

impl Default for TaskFileTable {
    fn default() -> Self {
        Self::new()
    }
}

impl TaskFileTable {
    pub const fn new() -> Self {
        Self {
            slots: [const {
                TaskFileSlot {
                    generation: 0,
                    ctx: None,
                    position: 0,
                }
            }; MAX_TASK_FILES],
        }
    }

    /// Stash `ctx` in the lowest free slot at position 0. Returns the slot index
    /// as the handle, or `None` if the table is full.
    pub fn insert(&mut self, ctx: FileContext) -> Option<u32> {
        for (i, slot) in self.slots.iter_mut().enumerate() {
            if slot.ctx.is_none() {
                slot.ctx = Some(ctx);
                slot.position = 0;
                slot.generation = slot.generation.wrapping_add(1);
                return Some(i as u32);
            }
        }
        None
    }

    /// Clone `handle`'s [`FileContext`] and current position out of the table,
    /// together with the slot's generation. `None` if out of range or free.
    pub fn checkout(&self, handle: u32) -> Option<(u32, FileContext, u32)> {
        let slot = self.slots.get(handle as usize)?;
        let ctx = slot.ctx.clone()?;
        Some((slot.generation, ctx, slot.position))
    }

    /// Write `newctx`/`new_position` back into `handle`'s slot, generation-gated
    /// exactly like [`HandleTable::commit`] (see its doc for the stale-write-back
    /// rationale a `remove`+`insert` recycle guards against).
    pub fn commit(
        &mut self,
        handle: u32,
        captured_generation: u32,
        newctx: FileContext,
        new_position: u32,
    ) {
        if let Some(slot) = self.slots.get_mut(handle as usize) {
            if slot.generation == captured_generation && slot.ctx.is_some() {
                slot.ctx = Some(newctx);
                slot.position = new_position;
            }
        }
    }

    /// Set `handle`'s cursor position directly -- no FS access needed. `false`
    /// if the handle is out of range or free.
    pub fn seek(&mut self, handle: u32, offset: u32) -> bool {
        match self.slots.get_mut(handle as usize) {
            Some(slot) if slot.ctx.is_some() => {
                slot.position = offset;
                true
            }
            _ => false,
        }
    }

    /// Free `handle`'s slot. No-op for an out-of-range handle. Bumps the
    /// generation, same rationale as [`HandleTable::remove`].
    pub fn remove(&mut self, handle: u32) {
        if let Some(slot) = self.slots.get_mut(handle as usize) {
            slot.ctx = None;
            slot.position = 0;
            slot.generation = slot.generation.wrapping_add(1);
        }
    }
}

/// Max concurrent open directory browses (small -- task context browses one
/// folder at a time; headroom for a nested "up one level" during navigation).
pub const MAX_DIR_HANDLES: usize = 8;

struct DirSlot {
    cursor: Option<DirCursor>,
}

/// A fixed-capacity table of open [`DirCursor`] snapshots keyed by a `u32`
/// handle. Unlike [`TaskFileTable`]/[`HandleTable`], entries need no generation
/// guard: [`readdir_next`] is synchronous and never touches the FS, so a slot
/// is claimed, read some number of times, and freed all under one lock --
/// there is no split checkout/`with_fs`/commit window for a concurrent `close`
/// to race.
pub struct DirHandleTable {
    slots: [DirSlot; MAX_DIR_HANDLES],
}

impl Default for DirHandleTable {
    fn default() -> Self {
        Self::new()
    }
}

impl DirHandleTable {
    pub const fn new() -> Self {
        Self {
            slots: [const { DirSlot { cursor: None } }; MAX_DIR_HANDLES],
        }
    }

    /// Stash `cursor` in the lowest free slot. Returns the slot index as the
    /// handle, or `None` if the table is full.
    pub fn insert(&mut self, cursor: DirCursor) -> Option<u32> {
        for (i, slot) in self.slots.iter_mut().enumerate() {
            if slot.cursor.is_none() {
                slot.cursor = Some(cursor);
                return Some(i as u32);
            }
        }
        None
    }

    /// Borrow `handle`'s cursor mutably (for [`readdir_next`]).
    /// `None` if out of range or free.
    pub fn get_mut(&mut self, handle: u32) -> Option<&mut DirCursor> {
        self.slots.get_mut(handle as usize)?.cursor.as_mut()
    }

    /// Free `handle`'s slot. No-op for an out-of-range handle.
    pub fn remove(&mut self, handle: u32) {
        if let Some(slot) = self.slots.get_mut(handle as usize) {
            slot.cursor = None;
        }
    }
}

/// Map an embedded-fatfs `Error` to the C-ABI `DelugeStatus` so the efatfs
/// wrappers can report granular NOT_FOUND/EXISTS/NOT_EMPTY/NO_SPACE instead of
/// a bare bool.
pub fn error_to_status<E>(err: &embedded_fatfs::Error<E>) -> DelugeStatus {
    use embedded_fatfs::Error;
    match err {
        Error::NotFound => DELUGE_ERR_NOT_FOUND,
        Error::AlreadyExists => DELUGE_ERR_EXISTS,
        Error::DirectoryIsNotEmpty => DELUGE_ERR_NOT_EMPTY,
        Error::NotEnoughSpace => DELUGE_ERR_NO_SPACE,
        Error::CorruptedFileSystem => DELUGE_ERR_NO_FILESYSTEM,
        Error::InvalidInput
        | Error::InvalidFileNameLength
        | Error::UnsupportedFileNameCharacter => DELUGE_ERR_PARAM,
        Error::Io(_) => DELUGE_ERR_IO,
        // `Error<T>` is `#[non_exhaustive]` and also carries `UnexpectedEof`/
        // `WriteZero` (embedded_io_async read/write plumbing, not a granular
        // FS-op failure) -- collapse those, and any future variant, to a
        // generic I/O error.
        _ => DELUGE_ERR_IO,
    }
}
