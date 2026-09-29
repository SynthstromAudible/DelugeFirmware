//! Mounts the vendored `embedded-fatfs` (`crates/embedded-fatfs/`) over the
//! shared `DISK` image (`ram_disk.rs`) and reads/writes through it -- this
//! harness's whole storage backend.
//!
//! Confirmed async API, from the vendored source (not the crates.io docs,
//! not guessed -- this crate is never published and pins its own fork):
//!
//!   `embedded_fatfs::FileSystem::<IO, TP, OCC>::new<T: IntoStorage<IO>>`
//!       `(storage: T, options: FsOptions<TP, OCC>) -> Result<Self, Error<IO::Error>>`
//!       -- async, and requires `storage` to be un-seeked (asserts pos==0 after
//!       its own `seek(Start(0))`).                                    (fs.rs:377)
//!   `embedded_fatfs::FsOptions::new() -> FsOptions<DefaultTimeProvider, LossyOemCpConverter>`
//!       (fs.rs:256)
//!   `FileSystem::root_dir(&self) -> Dir<'_, IO, TP, OCC>`             -- sync (fs.rs:629)
//!   `Dir::open_file(&self, path: &str) -> Result<File<'a,IO,TP,OCC>, Error<IO::Error>>`
//!       -- async; `path` is '/'-separated, leading/trailing '/' trimmed by
//!       `split_path`, so an absolute path like "/SAMPLES/hello.txt" works.
//!                                                                       (dir.rs:307)
//!   `File` implements `embedded_io_async::Read`:
//!       `async fn read(&mut self, buf: &mut [u8]) -> Result<usize, Error<IO::Error>>`,
//!       `Ok(0)` at EOF. embedded-io-async 0.7 ships no `ReadExt::read_to_end`,
//!       so `read_file` below loops `read()` to drain the file.        (file.rs:317-368)
//!   `Dir::iter(&self) -> DirIter<'a, IO, TP, OCC>`                    -- sync (dir.rs:123)
//!   `DirIter::next(&mut self) -> Option<Result<DirEntry<'a,IO,TP,OCC>, Error<IO::Error>>>`
//!       -- async, `None` at end-of-directory.                         (dir.rs:833)
//!   `DirEntry::file_name(&self) -> String`, `::len(&self) -> u64`, `::is_dir(&self) -> bool`
//!       (dir_entry.rs:617,712,637; `file_name`/`len` need the `alloc` feature,
//!       already on in this crate's `Cargo.toml`).
//!
//! Write path, same vendored source:
//!   `Dir::create_file(&self, path: &str) -> Result<File<'a,IO,TP,OCC>, Error<IO::Error>>`
//!       -- async; opens if it already exists, creates (empty) otherwise.   (dir.rs:339)
//!   `File::truncate(&mut self) -> Result<(), Error<IO::Error>>`
//!       -- async; resets size to the file's current offset.               (file.rs:102)
//!   `File` implements `embedded_io_async::Write`: `write`/`write_all`/`flush`.
//!       `flush` MUST be called (or `File::close`) -- `update_dir_entry_after_write`
//!       only updates the in-memory entry; the on-disk directory entry (size,
//!       modified time) is written by `flush`, not by `write` itself.       (file.rs:254,371-444)
//!   `Dir::create_dir(&self, path: &str) -> Result<Self, Error<IO::Error>>`  (dir.rs:384)
//!   `Dir::remove(&self, path: &str) -> Result<(), Error<IO::Error>>`       (dir.rs:456)
//!   `Dir::rename(&self, src_path: &str, dst_dir: &Dir<'_,IO,TP,OCC>, dst_path: &str)
//!       -> Result<(), Error<IO::Error>>`                                   (dir.rs:520)
//!
//! `IO: ReadWriteSeek` is a blanket impl over any `T: embedded_io_async::{Read,
//! Write, Seek}` (fs.rs:130-132), and `IntoStorage<T> for T` is blanket too
//! (fs.rs:343), so the storage below needs only those three trait impls -- no
//! bespoke `IntoStorage`.
//!
//! That storage is the real device bridge: `block_dev::FileBlockDevice`
//! (`block_device_driver::BlockDevice<512>` over the shared `DISK`) wrapped
//! in `block_device_adapters::BufStream`, which supplies the
//! `embedded_io_async::{Read,Write,Seek}` `embedded-fatfs` needs. This is
//! the exact adapter stack (plus `#68`/`#62`, vendored in
//! `crates/block-device-adapters`) driven on-device, so the whole
//! differential corpus validates it on host too.

use crate::block_dev::FileBlockDevice;
use crate::ops::Entry;
use block_device_adapters::BufStream;
use embassy_futures::block_on;
use embedded_fatfs::{
    Date, DateTime, DefaultTimeProvider, File, FileContext, FileSystem, FsOptions,
    LossyOemCpConverter, Time,
};
use embedded_io_async::{Read, Seek, SeekFrom, Write};

/// A mounted `embedded-fatfs` volume, read + write path. Implements
/// `ops::FsOps`/`ops::FsOpsMut` (`mount`/`read_file`/`read_dir`/write ops).
pub struct EFatFs {
    fs: FileSystem<BufStream<FileBlockDevice, 512>, DefaultTimeProvider, LossyOemCpConverter>,
}

impl EFatFs {
    /// Mount the volume currently installed via `ram_disk::RamDisk::load`.
    pub fn mount() -> Self {
        let storage = BufStream::<FileBlockDevice, 512>::new(FileBlockDevice);
        let fs = block_on(FileSystem::new(storage, FsOptions::new())).expect("FileSystem::new");
        EFatFs { fs }
    }

    /// The raw mounted `FileSystem`, for tests that drive the shared
    /// `efatfs_core` handle-table logic directly over it.
    pub fn raw(
        &self,
    ) -> &FileSystem<BufStream<FileBlockDevice, 512>, DefaultTimeProvider, LossyOemCpConverter>
    {
        &self.fs
    }

    /// Read a whole file's contents.
    pub fn read_file(&self, path: &str) -> Vec<u8> {
        block_on(async {
            let root = self.fs.root_dir();
            let mut file = root.open_file(path).await.expect("open_file");
            let mut out = Vec::new();
            let mut buf = [0u8; 4096];
            loop {
                let n = file.read(&mut buf).await.expect("read");
                if n == 0 {
                    break;
                }
                out.extend_from_slice(&buf[..n]);
            }
            out
        })
    }

    /// Host analog: open `path`, then DETACH it to an opaque
    /// [`FileContext`] via `File::close()` — the exact handoff the device
    /// handle table (`efatfs_fs.rs`) stores per open sample. The context is
    /// opaque here (its fields are `pub(crate)`); the caller stores/clones the
    /// whole struct.
    pub fn open_context(&self, path: &str) -> FileContext {
        block_on(async {
            let f = self.fs.root_dir().open_file(path).await.expect("open_file");
            f.close().await.expect("close")
        })
    }

    /// Re-attach `ctx` to a fresh [`File`] (`File::new_from_context`), seek to
    /// ABSOLUTE `offset`, fill `dst` completely by looping the `Read` impl,
    /// then detach again — returning the advanced context and whether the whole
    /// buffer was filled (`false` on a short read / EOF). Mirrors the device
    /// `read_at`'s clone-out → reattach → seek → fill → write-back, minus the
    /// `HANDLES`/FS mutexes (host is single-threaded `block_on`).
    pub fn read_at_context(
        &self,
        ctx: &FileContext,
        offset: u32,
        dst: &mut [u8],
    ) -> (FileContext, bool) {
        block_on(async {
            let mut f = File::new_from_context(ctx.clone(), &self.fs)
                .await
                .expect("new_from_context");
            f.seek(SeekFrom::Start(u64::from(offset)))
                .await
                .expect("seek");
            let mut filled = 0;
            while filled < dst.len() {
                match f.read(&mut dst[filled..]).await.expect("read") {
                    0 => break, // short read / EOF
                    n => filled += n,
                }
            }
            let newctx = f.close().await.expect("close");
            (newctx, filled == dst.len())
        })
    }

    /// Host analog of `efatfs_core::create_context`: `Dir::create_file`
    /// (open-or-create) then `File::truncate` — WRITE_CREATE semantics
    /// (`exclusive == false`, starts empty even if `path` already existed) —
    /// or, with `exclusive == true`, WRITE_CREATE_NEW (fails if `path`
    /// already exists; embedded-fatfs has no atomic create-if-absent
    /// primitive, so this checks `Dir::exists` first — safe here because
    /// task-context file ops are single-threaded, both on host and on
    /// device under `efatfs_fs::with_fs`'s mutex). Panics on any FS error,
    /// INCLUDING WRITE_CREATE_NEW's "already exists" — use
    /// [`try_create_exclusive`](Self::try_create_exclusive) to observe that
    /// failure without panicking.
    pub fn create_context(&self, path: &str, exclusive: bool) -> FileContext {
        self.try_create_context(path, exclusive)
            .expect("create_context")
    }

    /// Non-panicking WRITE_CREATE_NEW: `None` if `path` already exists (or
    /// any other FS error), instead of `create_context(path, true)`'s panic.
    pub fn try_create_exclusive(&self, path: &str) -> Option<FileContext> {
        self.try_create_context(path, true)
    }

    fn try_create_context(&self, path: &str, exclusive: bool) -> Option<FileContext> {
        block_on(async {
            let root = self.fs.root_dir();
            if exclusive && root.exists(path).await.ok()? {
                return None;
            }
            let mut f = root.create_file(path).await.ok()?;
            f.truncate().await.ok()?;
            f.close().await.ok()
        })
    }

    /// Host analog of `efatfs_core::write_context`: reattach, seek to
    /// absolute `offset`, loop `Write::write` over `src`, detach. Mirrors
    /// `read_at_context`'s inlined (not `efatfs_core`-delegating) style since
    /// this library crate doesn't depend on `efatfs_core` — only the test
    /// binary `#[path]`-includes it.
    pub fn write_at_context(
        &self,
        ctx: &FileContext,
        offset: u32,
        src: &[u8],
    ) -> (FileContext, usize) {
        block_on(async {
            let mut f = File::new_from_context(ctx.clone(), &self.fs)
                .await
                .expect("new_from_context");
            f.seek(SeekFrom::Start(u64::from(offset)))
                .await
                .expect("seek");
            let mut written = 0;
            while written < src.len() {
                match f.write(&src[written..]).await.expect("write") {
                    0 => break,
                    n => written += n,
                }
            }
            let newctx = f.close().await.expect("close");
            (newctx, written)
        })
    }

    /// Host analog of `efatfs_core::write_context_noflush`:
    /// reattach, seek to absolute `offset`, loop `Write::write` over `src`,
    /// then `File::detach()` instead of `close()` — the in-memory size
    /// advances but the on-disk directory entry is NOT flushed. Pairs with
    /// [`flush_context`](Self::flush_context). Inlined (not
    /// `efatfs_core`-delegating), same rationale as
    /// [`write_at_context`](Self::write_at_context).
    pub fn write_at_via_context_noflush(
        &self,
        ctx: &FileContext,
        offset: u32,
        src: &[u8],
    ) -> (FileContext, usize) {
        block_on(async {
            let mut f = File::new_from_context(ctx.clone(), &self.fs)
                .await
                .expect("new_from_context");
            f.seek(SeekFrom::Start(u64::from(offset)))
                .await
                .expect("seek");
            let mut written = 0;
            while written < src.len() {
                match f.write(&src[written..]).await.expect("write") {
                    0 => break,
                    n => written += n,
                }
            }
            (f.detach(), written)
        })
    }

    /// Host analog of `efatfs_core::read_at_via_context`:
    /// reattach, seek to absolute `offset`, read bounded by the context's
    /// IN-MEMORY size (so this sees data from a prior
    /// [`write_at_via_context_noflush`](Self::write_at_via_context_noflush)
    /// even before it's been flushed to disk), EOF-honest short count, then
    /// `File::detach()` (no flush) — preserving the dirty write state for a
    /// later [`flush_context`](Self::flush_context).
    pub fn read_at_via_context(
        &self,
        ctx: &FileContext,
        offset: u32,
        dst: &mut [u8],
    ) -> (FileContext, usize) {
        block_on(async {
            let mut f = File::new_from_context(ctx.clone(), &self.fs)
                .await
                .expect("new_from_context");
            f.seek(SeekFrom::Start(u64::from(offset)))
                .await
                .expect("seek");
            let mut filled = 0;
            while filled < dst.len() {
                match f.read(&mut dst[filled..]).await.expect("read") {
                    0 => break,
                    n => filled += n,
                }
            }
            (f.detach(), filled)
        })
    }

    /// Host analog of `efatfs_core::flush_context`: reattach `ctx`
    /// and `File::close()` it, flushing the accumulated in-memory size/mtime
    /// edit (from one or more prior
    /// [`write_at_via_context_noflush`](Self::write_at_via_context_noflush)
    /// calls) to the on-disk directory entry.
    pub fn flush_context(&self, ctx: &FileContext) -> FileContext {
        block_on(async {
            let f = File::new_from_context(ctx.clone(), &self.fs)
                .await
                .expect("new_from_context");
            f.close().await.expect("close")
        })
    }

    /// Host analog of `efatfs_core::read_context_exact`: EOF-honest — returns
    /// the true accumulated byte count on the first short read, NO
    /// zero-padding (unlike `read_at_context`/`fill`'s streaming-read
    /// tolerance).
    pub fn read_exact_context(
        &self,
        ctx: &FileContext,
        offset: u32,
        dst: &mut [u8],
    ) -> (FileContext, usize) {
        block_on(async {
            let mut f = File::new_from_context(ctx.clone(), &self.fs)
                .await
                .expect("new_from_context");
            f.seek(SeekFrom::Start(u64::from(offset)))
                .await
                .expect("seek");
            let mut filled = 0;
            while filled < dst.len() {
                match f.read(&mut dst[filled..]).await.expect("read") {
                    0 => break,
                    n => filled += n,
                }
            }
            let newctx = f.close().await.expect("close");
            (newctx, filled)
        })
    }

    /// Host analog of `efatfs_core::size_context`: file length via
    /// `Seek(End(0))` (`File::size` is private to embedded-fatfs's own
    /// module).
    pub fn size_context(&self, ctx: &FileContext) -> (FileContext, u32) {
        block_on(async {
            let mut f = File::new_from_context(ctx.clone(), &self.fs)
                .await
                .expect("new_from_context");
            let size = f.seek(SeekFrom::End(0)).await.expect("seek end") as u32;
            let newctx = f.close().await.expect("close");
            (newctx, size)
        })
    }

    /// Host analog of `efatfs_core::truncate_context`: seek to `new_len`
    /// (embedded-fatfs's `File::truncate` truncates AT the current position),
    /// then truncate.
    pub fn truncate_context(&self, ctx: &FileContext, new_len: u32) -> (FileContext, ()) {
        block_on(async {
            let mut f = File::new_from_context(ctx.clone(), &self.fs)
                .await
                .expect("new_from_context");
            f.seek(SeekFrom::Start(u64::from(new_len)))
                .await
                .expect("seek");
            f.truncate().await.expect("truncate");
            let newctx = f.close().await.expect("close");
            (newctx, ())
        })
    }

    /// List a directory's entries, sorted by name.
    pub fn read_dir(&self, path: &str) -> Vec<Entry> {
        block_on(async {
            let root = self.fs.root_dir();
            let dir = if path.trim_matches('/').is_empty() {
                root
            } else {
                root.open_dir(path).await.expect("open_dir")
            };
            let mut iter = dir.iter();
            let mut v = Vec::new();
            while let Some(r) = iter.next().await {
                let e = r.expect("dir entry");
                let name = e.file_name();
                // HARNESS NORMALIZATION: embedded-fatfs's directory iterator
                // yields `.` and `..` pseudo-entries for non-root
                // directories, which the C++ app's own directory-listing
                // consumers never expect -- filter them out here so this
                // harness's `read_dir` presents the same logical view a
                // caller expects.
                if name == "." || name == ".." {
                    continue;
                }
                v.push(Entry {
                    name,
                    size: e.len(),
                    is_dir: e.is_dir(),
                });
            }
            v.sort_by(|a, b| a.name.cmp(&b.name));
            v
        })
    }

    /// Test helper: `read_dir` collapsed to the harness's comparison shape
    /// (`(name, is_dir, size)`), for enumeration-set equivalence tests against
    /// a hardcoded known-good expected set.
    pub fn readdir_all(&self, path: &str) -> Vec<(String, bool, u32)> {
        self.read_dir(path)
            .into_iter()
            .map(|e| (e.name, e.is_dir, e.size as u32))
            .collect()
    }

    /// True if `path` (file or directory) exists. A fresh
    /// [`mount`](Self::mount)ed `EFatFs` calling this is an independent,
    /// from-disk check: it walks the on-disk directory structure through a
    /// brand-new `FileSystem`, not any in-process cache a writer's `EFatFs`
    /// instance might hold.
    pub fn exists(&self, path: &str) -> bool {
        block_on(async { self.fs.root_dir().exists(path).await.unwrap_or(false) })
    }

    /// The packed FAT modified-date/time (`(dos_date << 16) | dos_time`,
    /// matching `set_time`'s convention) of `path`, read via `Dir::open_meta`
    /// (directory-entry metadata only, no file data touched).
    pub fn mtime(&self, path: &str) -> u32 {
        block_on(async {
            let entry = self.fs.root_dir().open_meta(path).await.expect("open_meta");
            let dt = entry.modified();
            let dos_date = (u32::from(dt.date.year - 1980) << 9)
                | (u32::from(dt.date.month) << 5)
                | u32::from(dt.date.day);
            let dos_time = (u32::from(dt.time.hour) << 11)
                | (u32::from(dt.time.min) << 5)
                | (u32::from(dt.time.sec) / 2);
            (dos_date << 16) | dos_time
        })
    }

    /// Create a directory. `path`'s parent must already exist.
    ///
    /// `&self`, not `&mut self`: `Dir::create_dir` (like every other
    /// `Dir` write op this file wraps) only needs `&self` — the actual
    /// mutable state is the shared `DISK` RAM image behind `self.fs`'s
    /// storage, not `self` itself.
    pub fn mkdir(&self, path: &str) {
        block_on(async {
            self.fs
                .root_dir()
                .create_dir(path)
                .await
                .expect("create_dir");
        });
    }

    /// Create (or truncate, if it already exists) `path` and write `bytes`
    /// as its whole contents.
    pub fn write_new(&mut self, path: &str, bytes: &[u8]) {
        block_on(async {
            let root = self.fs.root_dir();
            let mut file = root.create_file(path).await.expect("create_file");
            file.truncate().await.expect("truncate");
            file.write_all(bytes).await.expect("write_all");
            file.flush().await.expect("flush");
        });
    }

    /// Open the existing file at `path`, seek to its end, and append
    /// `bytes`.
    pub fn append(&mut self, path: &str, bytes: &[u8]) {
        block_on(async {
            let root = self.fs.root_dir();
            let mut file = root.open_file(path).await.expect("open_file");
            file.seek(SeekFrom::End(0)).await.expect("seek end");
            file.write_all(bytes).await.expect("write_all");
            file.flush().await.expect("flush");
        });
    }

    /// Delete an existing file or (empty) directory. `&mut self` to match
    /// `ops::FsOpsMut::delete`'s trait signature (the differential-fuzzing
    /// surface both backends implement).
    pub fn delete(&mut self, path: &str) {
        block_on(async {
            self.fs.root_dir().remove(path).await.expect("remove");
        });
    }

    /// Host analog of `efatfs_core::unlink` — same operation as
    /// [`delete`](Self::delete), `&self` to match the path-op suite's
    /// (mkdir/create_context/rename/unlink) signatures.
    pub fn unlink(&self, path: &str) {
        block_on(async {
            self.fs.root_dir().remove(path).await.expect("remove");
        });
    }

    /// Rename/move `from` to `to`, both absolute paths.
    ///
    /// Drives the crate's `Dir::rename` directly, with no parent-resolution
    /// workaround needed: the vendored crate fixes a bug where `Dir::rename`
    /// would silently drop a multi-component `dst_path`'s leading directory
    /// components (`dir.rs:559` passed the raw, untraversed `dst_dir`
    /// parameter to `rename_internal` instead of the traversed destination
    /// parent it had just computed; see `crates/embedded-fatfs/VENDOR.md`'s
    /// "Applied local fixes"), so a plain `root.rename(from, &root, to)`
    /// with a multi-component `to` lands in the right directory.
    ///
    /// `&self`, not `&mut self` — see [`mkdir`](Self::mkdir)'s doc comment.
    pub fn rename(&self, from: &str, to: &str) {
        block_on(async {
            let root = self.fs.root_dir();
            let from = from.trim_start_matches('/');
            let to = to.trim_start_matches('/');
            root.rename(from, &root, to).await.expect("rename");
        });
    }

    /// Host analog of `efatfs_core::set_time`: `timestamp` is the
    /// same packed FAT date/time (`(dos_date << 16) | dos_time`) that
    /// `efatfs_core::set_time` documents. Panics on any FS error or
    /// out-of-range date/time component.
    pub fn set_time(&self, path: &str, timestamp: u32) {
        block_on(async {
            let dos_date = (timestamp >> 16) as u16;
            let dos_time = timestamp as u16;
            let year = (dos_date >> 9) + 1980;
            let month = (dos_date >> 5) & 0xF;
            let day = dos_date & 0x1F;
            let hour = dos_time >> 11;
            let min = (dos_time >> 5) & 0x3F;
            let sec = (dos_time & 0x1F) * 2;
            let mut f = self.fs.root_dir().open_file(path).await.expect("open_file");
            #[allow(deprecated)]
            f.set_modified(DateTime::new(
                Date::new(year, month, day),
                Time::new(hour, min, sec, 0),
            ));
            f.close().await.expect("close");
        });
    }
}
