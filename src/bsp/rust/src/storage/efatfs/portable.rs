//! The storage-generic core of the `embedded-fatfs` read and write paths: the handle tables, the
//! `FileContext` detach/reattach primitives, the generation guard, and the fill loop.
//!
//! Generic over the `FileSystem`'s IO type, with no statics and no device dependencies, so the
//! same code runs under the device mount (`efatfs::device`), the host mount (`efatfs::host`), and
//! directly in host tests.
//!
//! # Lock discipline
//!
//! The mount layers keep the `FileSystem` and each handle table behind separate async mutexes and
//! compose the split primitives ([`HandleTable::checkout`], [`read_context`],
//! [`HandleTable::commit`]): the table lock is released before the FS mutex is taken, and re-taken
//! to write back. [`HandleTable::read_at_owned`] holds the table across the FS await and is for
//! single-threaded tests only; on device it would invert the FS -> table order `open` takes and
//! deadlock.
//!
//! # Handle model
//!
//! A file is opened once and detached to a [`FileContext`] (cheap to clone; its `current_cluster`
//! lets forward seeks skip re-walking the cluster chain). Every op re-attaches a `File` from the
//! context and detaches again, with no per-op heap allocation.

// `alloc` for `readdir_open`'s snapshot; this module also builds `no_std` on device.
extern crate alloc;
use alloc::string::String;
use alloc::vec::Vec;

use embedded_fatfs::{
    Date, DateTime, File, FileContext, FileSystem, OemCpConverter, ReadWriteSeek, Time,
    TimeProvider,
};
// embedded-fatfs keeps its own `io` traits `pub(crate)`; `File` implements the public
// `embedded_io_async` ones.
use embedded_io_async::{Read as _, Seek as _, SeekFrom, Write as _};

// `crate::sys` is the bindgen output on device and under `host_app`, and the hand-written
// `abi/sys_host.rs` on the plain host build, which must define these constants too.
use crate::sys::{
    DelugeStatus, DelugeStatus_DELUGE_ERR_EXISTS as DELUGE_ERR_EXISTS,
    DelugeStatus_DELUGE_ERR_IO as DELUGE_ERR_IO,
    DelugeStatus_DELUGE_ERR_NO_FILESYSTEM as DELUGE_ERR_NO_FILESYSTEM,
    DelugeStatus_DELUGE_ERR_NO_SPACE as DELUGE_ERR_NO_SPACE,
    DelugeStatus_DELUGE_ERR_NOT_EMPTY as DELUGE_ERR_NOT_EMPTY,
    DelugeStatus_DELUGE_ERR_NOT_FOUND as DELUGE_ERR_NOT_FOUND,
    DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM,
};

pub use deluge_sample_fill::FileLocation;

const _: () =
    assert!(deluge_sample_fill::FILE_LOCATION_BYTES == embedded_fatfs::FILE_CONTEXT_BYTES);

/// The location of the open file `ctx`, found on mount `mount_generation`, for the backends'
/// `open_at` to open again with no directory walk.
pub fn location_of_context(ctx: &FileContext, mount_generation: u32) -> FileLocation {
    FileLocation {
        context: ctx.to_bytes(),
        mount_generation,
        valid: 1,
    }
}

/// The file's context, if `location` names a file found on mount `mount_generation`.
pub fn context_at(location: &FileLocation, mount_generation: u32) -> Option<FileContext> {
    if location.valid == 0 || location.mount_generation != mount_generation {
        return None;
    }
    FileContext::from_bytes(&location.context)
}

/// Capacity of the streaming-read handle table.
///
/// Each resident sample holds one handle for its lifetime (`SampleStream::open_read_stream`), so
/// this bounds how many samples can be resident at once, which while one song plays and the next
/// loads is both songs' samples. A full table sends every load through a reclaim. A slot costs about
/// 80 bytes; the sample stream registry (`deluge_sample_stream`) has the same capacity.
pub const MAX_HANDLES: usize = 256;

/// Capacity of the sample recorder's persistent stream-write table (`STREAM_WRITE_CTX`): one
/// handle per in-progress recording, and the device has only a few audio inputs.
pub const MAX_STREAM_WRITE_HANDLES: usize = 8;

/// One handle-table slot: an optional detached [`FileContext`] and a generation bumped on every
/// claim and free, so a deferred write-back can detect that the slot was recycled.
struct Slot {
    generation: u32,
    ctx: Option<FileContext>,
}

/// A fixed-capacity table of detached [`FileContext`]s keyed by a `u32` handle (the slot index).
///
/// `N` is [`MAX_HANDLES`] for the streaming read path and [`MAX_STREAM_WRITE_HANDLES`] for the
/// recorder. Only [`Self::read_at_owned`] touches the FS; see the module doc's lock discipline.
pub struct HandleTable<const N: usize> {
    slots: [Slot; N],
}

impl<const N: usize> Default for HandleTable<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> HandleTable<N> {
    /// Returns an empty table.
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

    /// Stores `ctx` in the lowest free slot, bumping its generation. Returns the handle, or `None`
    /// if the table is full.
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

    /// Clones the [`FileContext`] behind `handle` out with the slot's generation; `None` if the
    /// handle is out of range or free.
    ///
    /// Clones rather than takes, so a failed read leaves the slot valid.
    pub fn checkout(&self, handle: u32) -> Option<(u32, FileContext)> {
        let slot = self.slots.get(handle as usize)?;
        let ctx = slot.ctx.clone()?;
        Some((slot.generation, ctx))
    }

    /// Writes `newctx` back into `handle`'s slot only if the slot is occupied and its generation
    /// still equals `captured_generation`.
    ///
    /// A mismatch means the handle was removed (and possibly the index reused for another file)
    /// while the FS work was in flight. `File::new_from_context` cannot catch that splice, because
    /// the old file's directory entry is still valid on disk.
    pub fn commit(&mut self, handle: u32, captured_generation: u32, newctx: FileContext) {
        if let Some(slot) = self.slots.get_mut(handle as usize) {
            if slot.generation == captured_generation && slot.ctx.is_some() {
                slot.ctx = Some(newctx);
            }
        }
    }

    /// Frees `handle`'s slot (no-op if out of range), bumping the generation so a write-back
    /// captured earlier is stale even if the index is never reused.
    pub fn remove(&mut self, handle: u32) {
        if let Some(slot) = self.slots.get_mut(handle as usize) {
            slot.ctx = None;
            slot.generation = slot.generation.wrapping_add(1);
        }
    }

    /// Reads `dst` from `handle` at `byte_offset`: checkout, [`read_context`] and commit under one
    /// `&mut self` borrow. Returns whether `dst` was filled; `false` on a bad handle or FS error.
    ///
    /// For single-threaded tests only: it holds the table across the FS await (see the module
    /// doc).
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

/// Result of opening a streamed-read handle.
///
/// [`OpenOutcome::TableFull`] is kept apart from other failures because a valid file is being
/// dropped: the `deluge_efatfs_open` bridges flag it, and C++ reports
/// `Error::TOO_MANY_OPEN_STREAMS` rather than `Error::FILE_NOT_FOUND`.
pub enum OpenOutcome {
    /// The new handle.
    Handle(u32),
    /// The open failed: `DELUGE_ERR_NO_FILESYSTEM` if unmounted, otherwise [`open_context`]'s
    /// status (`DELUGE_ERR_NOT_FOUND` for a missing file, ...).
    Failed(DelugeStatus),
    /// Every slot is in use.
    TableFull,
}

/// Fills `dst` from `f`'s current position, zero-padding past logical EOF. Returns `false` only on
/// a read error.
///
/// The streaming fill (`async_fill.cpp`) reads whole sectors, so its last-cluster read can ask for
/// up to 511 bytes past the file's logical size, still inside the cluster's allocation.
/// `File::read` stops at logical EOF, so the tail is zeroed; the audio pipeline never consumes
/// past the real data length.
///
/// A read that starts at or past EOF also returns `true`, with an all-zero buffer. Callers must
/// bound requests to the file (at most one cluster past EOF), as the streaming path does via
/// `audioDataLengthBytes`, clamped to the on-disk size in `Sample::finalizeAfterLoad`.
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

/// Opens `path` and detaches it to a [`FileContext`]. Errors map through [`error_to_status`]
/// (`DELUGE_ERR_NOT_FOUND` for a missing path).
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

/// Reattaches `ctx`, seeks to absolute `byte_offset`, fills `dst` (zero-padding past EOF, see
/// `fill`) and detaches. Returns the advanced context and whether `dst` was filled; `None` on any
/// FS error.
///
/// The absolute seek makes the result independent of the context's cursor; reusing the returned
/// context only makes forward seeks cheap.
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

/// Like [`read_context`] but EOF-honest: stops at EOF and returns the real byte count
/// (`0..=dst.len()`) without zero-padding, for task-context callers that need the true length.
/// `None` on any FS error.
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

/// Reattaches `ctx`, seeks to absolute `byte_offset`, writes `src`, and closes. Returns the
/// advanced context and the bytes written; `None` on any FS error.
///
/// The close persists the size/mtime directory-entry update; `File::write` alone changes only the
/// in-memory context.
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

/// Like [`write_context`] but detaches without flushing: the in-memory size advances, the on-disk
/// directory entry does not. `None` on any FS error.
///
/// For a long-lived writer (the sample recorder) that writes chunk by chunk and flushes once, at
/// finalize, with [`flush_context`].
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

/// Reattaches `ctx` and closes it, flushing the size/mtime accumulated by
/// [`write_context_noflush`] to the directory entry. `None` on any FS error.
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

/// Reads from `ctx` at absolute `byte_offset`, bounded by the context's in-memory size, then
/// detaches without flushing. Returns the EOF-honest byte count; `None` on any FS error.
///
/// Sees data from an unflushed [`write_context_noflush`] session, which an open by path would
/// not (its on-disk size is stale), and keeps the dirty state for [`flush_context`].
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

/// Returns the file length in bytes, via `Seek(End(0))` since `File::size` is private to
/// embedded-fatfs. `None` on any FS error.
///
/// Leaves the context's cursor at EOF; harmless, since every reattaching op seeks absolutely.
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

/// Truncates the file to `new_len` bytes. `None` on any FS error.
///
/// `File::truncate` cuts at the current position, so this seeks to `new_len` first. It can only
/// shrink: `Seek` clamps a `new_len` past the end to EOF.
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

// Path ops. These work on `fs.root_dir()` and a `/`-separated path, with no open context;
// `create_context` also detaches the new file for the caller's handle table. Errors map through
// `error_to_status`.

/// Creates `path` (and any missing parent directories) and detaches it to a [`FileContext`].
///
/// `exclusive == false` is WRITE_CREATE: open or create, then truncate to empty (embedded-fatfs's
/// `create_file` opens an existing file as-is). `exclusive == true` is WRITE_CREATE_NEW:
/// `Err(DELUGE_ERR_EXISTS)` if `path` exists. embedded-fatfs has no atomic create-if-absent, so
/// this checks `Dir::exists` first; the check-then-create cannot race because one task drives the
/// mounted `FileSystem` at a time, under the FS mutex.
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
    // Create the parent first (mkdir -p): `create_file` returns NotFound without it, and the
    // exclusive `exists` check below errors on a missing parent rather than returning false. This
    // lets a write-create into a fresh nested folder (e.g. SAMPLES/RESAMPLE) succeed.
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

/// Deletes the file or empty directory at `path`. Errors map through [`error_to_status`]
/// (`DELUGE_ERR_NOT_FOUND`, `DELUGE_ERR_NOT_EMPTY`, ...).
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

/// Returns the volume's free and total cluster counts. Errors map through [`error_to_status`].
pub async fn fs_stats<IO, TP, OCC>(fs: &FileSystem<IO, TP, OCC>) -> Result<(u32, u32), DelugeStatus>
where
    IO: ReadWriteSeek,
    TP: TimeProvider,
    OCC: OemCpConverter,
{
    let s = fs.stats().await.map_err(|e| error_to_status(&e))?;
    Ok((s.free_clusters(), s.total_clusters()))
}

/// Returns the volume's cluster size in bytes. Errors map through [`error_to_status`].
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

/// Creates the directory at `path` and any missing parents (`mkdir -p`). Errors map through
/// [`error_to_status`], e.g. `DELUGE_ERR_EXISTS` if a file occupies `path`.
///
/// `create_dir` needs the parent to exist but no-ops on an existing directory, so this creates
/// each ancestor in turn. It is recursive because the efatfs C ABI reports only success or
/// failure, so a caller such as `SampleRecorder` cannot see a missing parent and create it.
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
    // Create each ancestor prefix, skipping empty components (a leading or doubled '/').
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

/// Renames or moves `old` to `new` (both root-relative). Errors map through [`error_to_status`],
/// including `DELUGE_ERR_EXISTS` if `new` exists.
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

/// Sets `path`'s modification time from a packed FAT timestamp, `(dos_date << 16) | dos_time`
/// with `dos_date = (year - 1980) << 9 | month << 5 | day` and
/// `dos_time = hour << 11 | min << 5 | sec / 2` (the FatFs `get_fattime()` layout).
///
/// Errors map through [`error_to_status`]; `DELUGE_ERR_PARAM` if a field is out of range (checked
/// here because `Date::new`/`Time::new` panic on it). The close persists the change.
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
    // Deprecated in favour of a `TimeProvider`, but callers set an explicit time, not "now".
    f.set_modified(DateTime::new(
        Date::new(year, month, day),
        Time::new(hour, min, sec, 0),
    ));
    f.close().await.map_err(|e| error_to_status(&e))?;
    Ok(())
}

// Directory enumeration. `DirIter` borrows the `FileSystem`, so it cannot outlive one FS call.
// `readdir_open` walks the whole directory in one call and returns an owned snapshot
// (`DirCursor`) instead.

/// One directory entry in a [`DirCursor`] snapshot.
///
/// `modified` is the packed FAT timestamp [`set_time`] takes. `attrs` is the raw FAT attribute
/// byte (`FileAttributes::bits()`, identical to FatFs `AM_*`: READ_ONLY 0x01, HIDDEN 0x02,
/// SYSTEM 0x04, VOLUME_ID 0x08, DIRECTORY 0x10, ARCHIVE 0x20).
///
/// `name` is an exact-sized heap `String`, not an inline 256-byte buffer: a snapshot holds one
/// entry per file in a fixed arena, and inline names capped a folder at a couple of hundred
/// entries.
#[derive(Clone)]
pub struct DirEntryInfo {
    pub name: String,
    pub is_dir: bool,
    pub size: u32,
    pub modified: u32,
    pub attrs: u8,
}

/// An owned directory snapshot from [`readdir_open`], plus the walk position.
pub struct DirCursor {
    entries: Vec<DirEntryInfo>,
    idx: usize,
}

/// Packs a `DateTime` into [`set_time`]'s layout (the crate's own `encode()` is `pub(crate)`).
fn pack_fat_datetime(dt: DateTime) -> u32 {
    let dos_date = ((dt.date.year - 1980) << 9) | (dt.date.month << 5) | dt.date.day;
    let dos_time = (dt.time.hour << 11) | (dt.time.min << 5) | (dt.time.sec / 2);
    (u32::from(dos_date) << 16) | u32::from(dos_time)
}

/// Opens `path` as a directory and snapshots all its entries; an empty (or all-`/`) path is the
/// root. Errors map through [`error_to_status`], including `path` not naming a directory.
///
/// Skips the `.`/`..` entries embedded-fatfs yields for subdirectories, as FatFs `f_readdir`
/// does.
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
        // Every allocation here is fallible: the snapshot grows with the directory, the arena
        // behind it is fixed (96 KiB on device), and an allocation failure panics the calling
        // task and hangs the front panel. A huge folder must give the browser an error instead.
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

/// Returns the next entry of `cursor`, or `Some(None)` at the end. The outer `None` (an FS error)
/// cannot happen for a snapshot; it is reserved for a cursor that reads the FS.
///
/// Takes no `FileSystem`, so a directory walk never queues behind streaming reads on the FS mutex.
///
/// Moves each name out of the snapshot, so an entry can be read only once. That avoids an
/// allocation per entry, when the arena may already be short, and frees names as the walk goes.
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

/// Packs decomposed `DelugeTimestamp` fields (`libdeluge/types.h`) into [`set_time`]'s layout,
/// for the `deluge_efatfs_set_time` bridges. Unvalidated; [`set_time`] checks the result.
pub fn pack_timestamp(year: u16, month: u8, day: u8, hour: u8, minute: u8, second: u8) -> u32 {
    let dos_date = ((year - 1980) << 9) | (u16::from(month) << 5) | u16::from(day);
    let dos_time = (u16::from(hour) << 11) | (u16::from(minute) << 5) | u16::from(second / 2);
    (u32::from(dos_date) << 16) | u32::from(dos_time)
}

// Task-context file and directory handle tables.
//
// Task-context files do not use the streaming `HandleTable`: `deluge_file_read`/`_write` use an
// implicit cursor set by `deluge_file_seek` (`file_io.h`), so each handle carries a position,
// which the streaming path (always an absolute offset) does not need. `readdir_next` never
// touches the FS, so `DirHandleTable` slots need no generation guard.

/// Capacity of the task-context file table (menu browsing, project save and load).
pub const MAX_TASK_FILES: usize = 16;

struct TaskFileSlot {
    generation: u32,
    ctx: Option<FileContext>,
    /// The handle's byte position, set by `deluge_file_seek` without FS access. Authoritative:
    /// every read and write seeks the reattached `File` to it, whatever cursor the context
    /// carries.
    position: u32,
}

/// A fixed-capacity table of task-context file handles: a detached [`FileContext`] and a cursor
/// position per `u32` handle.
///
/// Used with the same checkout / FS work / commit discipline as [`HandleTable`].
pub struct TaskFileTable {
    slots: [TaskFileSlot; MAX_TASK_FILES],
}

impl Default for TaskFileTable {
    fn default() -> Self {
        Self::new()
    }
}

impl TaskFileTable {
    /// Returns an empty table.
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

    /// Stores `ctx` in the lowest free slot at position 0. Returns the handle, or `None` if the
    /// table is full.
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

    /// Clones `handle`'s context and position out with the slot's generation; `None` if out of
    /// range or free.
    pub fn checkout(&self, handle: u32) -> Option<(u32, FileContext, u32)> {
        let slot = self.slots.get(handle as usize)?;
        let ctx = slot.ctx.clone()?;
        Some((slot.generation, ctx, slot.position))
    }

    /// Writes the context and position back, gated on the generation as in
    /// [`HandleTable::commit`].
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

    /// Sets `handle`'s position without FS access. Returns `false` if the handle is out of range
    /// or free.
    pub fn seek(&mut self, handle: u32, offset: u32) -> bool {
        match self.slots.get_mut(handle as usize) {
            Some(slot) if slot.ctx.is_some() => {
                slot.position = offset;
                true
            }
            _ => false,
        }
    }

    /// Frees `handle`'s slot (no-op if out of range) and bumps the generation, as in
    /// [`HandleTable::remove`].
    pub fn remove(&mut self, handle: u32) {
        if let Some(slot) = self.slots.get_mut(handle as usize) {
            slot.ctx = None;
            slot.position = 0;
            slot.generation = slot.generation.wrapping_add(1);
        }
    }
}

/// Capacity of the directory handle table: one folder browsed at a time, plus headroom for
/// navigating up a level.
pub const MAX_DIR_HANDLES: usize = 8;

struct DirSlot {
    cursor: Option<DirCursor>,
}

/// A fixed-capacity table of open [`DirCursor`] snapshots keyed by a `u32` handle.
///
/// No generation guard: [`readdir_next`] is synchronous and FS-free, so a slot is claimed, read
/// and freed under the table lock with no FS await in between for a concurrent close to race.
pub struct DirHandleTable {
    slots: [DirSlot; MAX_DIR_HANDLES],
}

impl Default for DirHandleTable {
    fn default() -> Self {
        Self::new()
    }
}

impl DirHandleTable {
    /// Returns an empty table.
    pub const fn new() -> Self {
        Self {
            slots: [const { DirSlot { cursor: None } }; MAX_DIR_HANDLES],
        }
    }

    /// Stores `cursor` in the lowest free slot. Returns the handle, or `None` if the table is
    /// full.
    pub fn insert(&mut self, cursor: DirCursor) -> Option<u32> {
        for (i, slot) in self.slots.iter_mut().enumerate() {
            if slot.cursor.is_none() {
                slot.cursor = Some(cursor);
                return Some(i as u32);
            }
        }
        None
    }

    /// Returns `handle`'s cursor for [`readdir_next`]; `None` if out of range or free.
    pub fn get_mut(&mut self, handle: u32) -> Option<&mut DirCursor> {
        self.slots.get_mut(handle as usize)?.cursor.as_mut()
    }

    /// Frees `handle`'s slot. No-op for an out-of-range handle.
    pub fn remove(&mut self, handle: u32) {
        if let Some(slot) = self.slots.get_mut(handle as usize) {
            slot.cursor = None;
        }
    }
}

/// Maps an embedded-fatfs error to the C-ABI `DelugeStatus`.
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
        // `Error` is `#[non_exhaustive]`; `UnexpectedEof`, `WriteZero` and any future variant
        // count as I/O errors.
        _ => DELUGE_ERR_IO,
    }
}
