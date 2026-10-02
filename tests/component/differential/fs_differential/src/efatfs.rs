//! [`EFatFs`]: the vendored `embedded-fatfs` mounted over the shared
//! [`RamDisk`](crate::ram_disk::RamDisk) image, the harness's storage backend.
//!
//! Storage is [`FileBlockDevice`] wrapped in `block_device_adapters::BufStream`, the same adapter
//! stack the device's SD path uses, so the differential corpus exercises it on host too.
//!
//! The `*_context` methods are host counterparts of the BSP's `storage::efatfs::portable`
//! functions: they reattach a detached `FileContext`, do one operation, and detach again. They are
//! written out here because this library does not depend on the BSP.
//!
//! `embedded-fatfs` facts the code relies on:
//! - `FileSystem::new` requires the storage to be at position 0.
//! - Writes update only the in-memory directory entry; `flush` or `File::close` writes the size and
//!   modified time to disk. `File::detach` does neither.
//! - `File::truncate` truncates at the current position.
//! - The directory iterator yields `.` and `..` in non-root directories.

use crate::block_dev::FileBlockDevice;
use crate::ops::Entry;
use block_device_adapters::BufStream;
use embassy_futures::block_on;
use embedded_fatfs::{
    Date, DateTime, DefaultTimeProvider, File, FileContext, FileSystem, FsOptions,
    LossyOemCpConverter, Time,
};
use embedded_io_async::{Read, Seek, SeekFrom, Write};

/// A mounted `embedded-fatfs` volume; implements [`FsOps`](crate::ops::FsOps) and
/// [`FsOpsMut`](crate::ops::FsOpsMut).
pub struct EFatFs {
    fs: FileSystem<BufStream<FileBlockDevice, 512>, DefaultTimeProvider, LossyOemCpConverter>,
}

impl EFatFs {
    /// Mounts the volume in the current [`RamDisk`](crate::ram_disk::RamDisk) image.
    pub fn mount() -> Self {
        let storage = BufStream::<FileBlockDevice, 512>::new(FileBlockDevice);
        let fs = block_on(FileSystem::new(storage, FsOptions::new())).expect("FileSystem::new");
        EFatFs { fs }
    }

    /// Returns the mounted `FileSystem`, for tests that drive the BSP's `portable` core over it.
    pub fn raw(
        &self,
    ) -> &FileSystem<BufStream<FileBlockDevice, 512>, DefaultTimeProvider, LossyOemCpConverter>
    {
        &self.fs
    }

    /// Reads a whole file.
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

    /// Opens `path` and closes it to a detached [`FileContext`], the form the device handle table
    /// stores per open file.
    pub fn open_context(&self, path: &str) -> FileContext {
        block_on(async {
            let f = self.fs.root_dir().open_file(path).await.expect("open_file");
            f.close().await.expect("close")
        })
    }

    /// Reads `dst.len()` bytes at absolute `offset` through `ctx`, then closes.
    ///
    /// Returns the advanced context and whether `dst` was filled (`false` on a short read).
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
                    0 => break, // EOF
                    n => filled += n,
                }
            }
            let newctx = f.close().await.expect("close");
            (newctx, filled == dst.len())
        })
    }

    /// Counterpart of `portable::create_context`: creates `path` empty and returns its context.
    ///
    /// Without `exclusive` an existing file is truncated; with it, an existing file is an error.
    /// `embedded-fatfs` has no atomic create-if-absent, so exclusivity is an `exists` check first,
    /// which is sound because file operations are single-threaded.
    ///
    /// # Panics
    ///
    /// Panics on any filesystem error, including an exclusive create of an existing file; use
    /// [`try_create_exclusive`](Self::try_create_exclusive) to observe that case.
    pub fn create_context(&self, path: &str, exclusive: bool) -> FileContext {
        self.try_create_context(path, exclusive)
            .expect("create_context")
    }

    /// Creates `path` exclusively; returns `None` if it exists or on any other error.
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

    /// Counterpart of `portable::write_context`: writes `src` at absolute `offset`, then closes
    /// (flushing the directory entry). Returns the context and the bytes written.
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

    /// Counterpart of `portable::write_context_noflush`: writes `src` at absolute `offset`, then
    /// detaches without flushing, so only the context's in-memory size advances.
    ///
    /// Pair with [`flush_context`](Self::flush_context).
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

    /// Counterpart of `portable::read_at_via_context`: reads at absolute `offset`, bounded by the
    /// context's in-memory size, then detaches without flushing.
    ///
    /// Sees unflushed data from
    /// [`write_at_via_context_noflush`](Self::write_at_via_context_noflush). Returns the true
    /// byte count.
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

    /// Counterpart of `portable::flush_context`: writes the context's in-memory size and modified
    /// time to the on-disk directory entry.
    pub fn flush_context(&self, ctx: &FileContext) -> FileContext {
        block_on(async {
            let f = File::new_from_context(ctx.clone(), &self.fs)
                .await
                .expect("new_from_context");
            f.close().await.expect("close")
        })
    }

    /// Counterpart of `portable::read_context_exact`: reads at absolute `offset` and returns the
    /// true byte count, with no zero-padding on a short read.
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

    /// Counterpart of `portable::size_context`: the file length, via a seek to the end
    /// (`File::size` is private).
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

    /// Counterpart of `portable::truncate_context`: truncates the file to `new_len` bytes.
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

    /// Lists a directory's entries, sorted by name, without `.` and `..`.
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
                // The app's directory-listing consumers never expect `.` and `..`.
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

    /// Returns [`read_dir`](Self::read_dir) as `(name, is_dir, size)` tuples.
    pub fn readdir_all(&self, path: &str) -> Vec<(String, bool, u32)> {
        self.read_dir(path)
            .into_iter()
            .map(|e| (e.name, e.is_dir, e.size as u32))
            .collect()
    }

    /// Returns whether `path` (file or directory) exists.
    ///
    /// Called on a freshly [`mount`](Self::mount)ed instance, this checks the disk rather than any
    /// state a writer's instance holds.
    pub fn exists(&self, path: &str) -> bool {
        block_on(async { self.fs.root_dir().exists(path).await.unwrap_or(false) })
    }

    /// Returns `path`'s modified time as packed FAT date/time, `(dos_date << 16) | dos_time`.
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

    /// Creates a directory; its parent must exist.
    pub fn mkdir(&self, path: &str) {
        block_on(async {
            self.fs
                .root_dir()
                .create_dir(path)
                .await
                .expect("create_dir");
        });
    }

    /// Creates or truncates `path` and writes `bytes` as its whole contents.
    pub fn write_new(&mut self, path: &str, bytes: &[u8]) {
        block_on(async {
            let root = self.fs.root_dir();
            let mut file = root.create_file(path).await.expect("create_file");
            file.truncate().await.expect("truncate");
            file.write_all(bytes).await.expect("write_all");
            file.flush().await.expect("flush");
        });
    }

    /// Appends `bytes` to the existing file at `path`.
    pub fn append(&mut self, path: &str, bytes: &[u8]) {
        block_on(async {
            let root = self.fs.root_dir();
            let mut file = root.open_file(path).await.expect("open_file");
            file.seek(SeekFrom::End(0)).await.expect("seek end");
            file.write_all(bytes).await.expect("write_all");
            file.flush().await.expect("flush");
        });
    }

    /// Deletes an existing file or empty directory.
    pub fn delete(&mut self, path: &str) {
        block_on(async {
            self.fs.root_dir().remove(path).await.expect("remove");
        });
    }

    /// Counterpart of `portable::unlink`; the same operation as [`delete`](Self::delete), taking
    /// `&self`.
    pub fn unlink(&self, path: &str) {
        block_on(async {
            self.fs.root_dir().remove(path).await.expect("remove");
        });
    }

    /// Renames or moves `from` to `to`, both absolute paths.
    ///
    /// Relies on the vendored crate's fix for multi-component destination paths in `Dir::rename`
    /// (see `crates/embedded-fatfs/VENDOR.md`); upstream drops the destination's directories.
    pub fn rename(&self, from: &str, to: &str) {
        block_on(async {
            let root = self.fs.root_dir();
            let from = from.trim_start_matches('/');
            let to = to.trim_start_matches('/');
            root.rename(from, &root, to).await.expect("rename");
        });
    }

    /// Counterpart of `portable::set_time`: sets `path`'s modified time from packed FAT date/time,
    /// `(dos_date << 16) | dos_time`.
    ///
    /// # Panics
    ///
    /// Panics on any filesystem error or out-of-range date/time component.
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
