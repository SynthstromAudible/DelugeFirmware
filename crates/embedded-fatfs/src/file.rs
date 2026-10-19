use core::cmp;

use crate::dir_entry::{DirEntryEditor, DirFileEntryData};
use crate::error::Error;
use crate::fs::{FileSystem, ReadWriteSeek};
use crate::io::{IoBase, Read, Seek, SeekFrom, Write};
use crate::time::{Date, DateTime, TimeProvider};

const MAX_FILE_SIZE: u32 = u32::MAX;

/// A FAT filesystem file object used for reading and writing data.
///
/// This struct is created by the `open_file` or `create_file` methods on `Dir`.
pub struct File<'a, IO: ReadWriteSeek, TP, OCC> {
    context: FileContext,
    // file-system reference
    fs: &'a FileSystem<IO, TP, OCC>,
}

/// A context of an existing [`File`].
///
/// This is obtained by calling [`File::close`] and can be used to resume
/// operations on a [`File`] with the [`DirEntry::to_file_with_context`](crate::dir_entry::DirEntry::to_file_with_context)
/// method. This can be useful for large files, because to `Seek` to the
/// end of the file would mean scanning the whole cluster chain which
/// has `O(n)` time complexity.
#[derive(Clone)]
pub struct FileContext {
    // Note first_cluster is None if file is empty
    pub(crate) first_cluster: Option<u32>,
    // Note: if offset points between clusters current_cluster is the previous cluster
    pub(crate) current_cluster: Option<u32>,
    // current position in this file
    pub(crate) offset: u32,
    // file dir entry editor - None for root dir
    pub(crate) entry: Option<DirEntryEditor>,
}

/// Bytes in an encoded [`FileContext`]; see [`FileContext::to_bytes`].
pub const FILE_CONTEXT_BYTES: usize = 64;

/// The first byte of every encoded [`FileContext`], so a buffer that holds none is refused.
const FILE_CONTEXT_VERSION: u8 = 1;

const HAS_FIRST_CLUSTER: u8 = 1 << 0;
const HAS_CURRENT_CLUSTER: u8 = 1 << 1;
const HAS_ENTRY: u8 = 1 << 2;
const ENTRY_DIRTY: u8 = 1 << 3;

impl FileContext {
    /// Encodes the context as plain bytes, so another component can reopen the same file with
    /// [`FileContext::from_bytes`] without walking a directory. Valid only for the volume, and the
    /// mount of it, that produced it.
    pub fn to_bytes(&self) -> [u8; FILE_CONTEXT_BYTES] {
        let mut buf = [0u8; FILE_CONTEXT_BYTES];
        buf[0] = FILE_CONTEXT_VERSION;
        let mut flags = 0;
        if let Some(cluster) = self.first_cluster {
            flags |= HAS_FIRST_CLUSTER;
            buf[4..8].copy_from_slice(&cluster.to_le_bytes());
        }
        if let Some(cluster) = self.current_cluster {
            flags |= HAS_CURRENT_CLUSTER;
            buf[8..12].copy_from_slice(&cluster.to_le_bytes());
        }
        buf[12..16].copy_from_slice(&self.offset.to_le_bytes());
        if let Some(entry) = &self.entry {
            flags |= HAS_ENTRY;
            if entry.dirty() {
                flags |= ENTRY_DIRTY;
            }
            buf[16..24].copy_from_slice(&entry.pos().to_le_bytes());
            buf[24..56].copy_from_slice(&entry.inner().to_bytes());
        }
        buf[1] = flags;
        buf
    }

    /// Decodes [`FileContext::to_bytes`]' output; `None` if `bytes` holds no encoded context.
    pub fn from_bytes(bytes: &[u8; FILE_CONTEXT_BYTES]) -> Option<Self> {
        if bytes[0] != FILE_CONTEXT_VERSION {
            return None;
        }
        let flags = bytes[1];
        let u32_at = |i: usize| u32::from_le_bytes([bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]]);
        let entry = (flags & HAS_ENTRY != 0).then(|| {
            let mut pos = [0u8; 8];
            pos.copy_from_slice(&bytes[16..24]);
            let mut data = [0u8; 32];
            data.copy_from_slice(&bytes[24..56]);
            DirEntryEditor::from_parts(
                DirFileEntryData::from_bytes(&data),
                u64::from_le_bytes(pos),
                flags & ENTRY_DIRTY != 0,
            )
        });
        Some(Self {
            first_cluster: (flags & HAS_FIRST_CLUSTER != 0).then(|| u32_at(4)),
            current_cluster: (flags & HAS_CURRENT_CLUSTER != 0).then(|| u32_at(8)),
            offset: u32_at(12),
            entry,
        })
    }
}

/// An extent containing a file's data on disk.
///
/// This is created by the `extents` method on `File`, and represents
/// a byte range on the disk that contains a file's data. All values
/// are in bytes.
#[cfg_attr(feature = "defmt", derive(defmt::Format))]
#[derive(Clone, Debug)]
pub struct Extent {
    pub offset: u64,
    pub size: u32,
}

impl<'a, IO: ReadWriteSeek, TP, OCC> File<'a, IO, TP, OCC> {
    pub(crate) fn new(
        first_cluster: Option<u32>,
        entry: Option<DirEntryEditor>,
        fs: &'a FileSystem<IO, TP, OCC>,
    ) -> Self {
        File {
            context: FileContext {
                first_cluster,
                entry,
                current_cluster: None, // cluster before first one
                offset: 0,
            },
            fs,
        }
    }

    /// Create a file from a preexisting [`FileContext`] & [`FileSystem`].
    ///
    /// Returns `Error::InvalidInput` if the file has been modified, deleted,
    /// or the filesystem reformatted since the context was created.
    ///
    /// **WARNING**: Two or more concurrent write accesses to the same file
    /// will corrupt the filesystem. This is a caller invariant — the library
    /// does not track open files.
    ///
    /// The on-disk staleness check is narrowed to the short-name bytes when
    /// `context.entry` is already dirty (`DirEntryEditor::dirty()`). A dirty
    /// entry only reaches here via [`File::detach`] (`close()`'s only other
    /// public exit clears dirty by flushing first), which is how a
    /// long-lived buffered writer (the sample recorder) accumulates several
    /// no-flush writes before a single finalize flush — the in-memory
    /// size/mtime/first_cluster legitimately diverge from the still-stale
    /// on-disk bytes in that window, so a full 32-byte comparison would
    /// reject every one of the writer's own reattaches. The short name is
    /// stable across that window, so comparing just it still catches a
    /// wholly different file recycling the slot. The invariant this relies
    /// on: while a dirty context is unflushed, its holder must be the slot's
    /// sole holder — nothing else may delete, move, or replace the file, as
    /// a same-name replacement would not be caught by this narrowed check.
    /// The full 32-byte check still runs (and still guards against any
    /// concurrent external modification) for every other, non-dirty caller —
    /// the vast majority of `new_from_context` callers, whose contexts
    /// always came from a flushing `close()`.
    pub async fn new_from_context(context: FileContext, fs: &'a FileSystem<IO, TP, OCC>) -> Result<Self, Error<IO::Error>> {
        let editor = context.entry.as_ref().ok_or(Error::InvalidInput)?;

        let mut on_disk = [0u8; 32];
        {
            let mut disk = fs.disk.borrow_mut();
            disk.seek(SeekFrom::Start(editor.pos())).await?;
            disk.read_exact(&mut on_disk).await?;
        }

        if editor.dirty() {
            if editor.inner().name()[..] != on_disk[0..11] {
                return Err(Error::InvalidInput);
            }
        } else if editor.inner().to_bytes() != on_disk {
            return Err(Error::InvalidInput);
        }

        Ok(File { context, fs })
    }

    /// Truncate file in current position.
    ///
    /// # Errors
    ///
    /// `Error::Io` will be returned if the underlying storage object returned an I/O error.
    ///
    /// # Panics
    ///
    /// Will panic if this is the root directory.
    pub async fn truncate(&mut self) -> Result<(), Error<IO::Error>> {
        trace!("File::truncate");
        if let Some(ref mut e) = self.context.entry {
            e.set_size(self.context.offset);
            if self.context.offset == 0 {
                e.set_first_cluster(None, self.fs.fat_type());
            }
        } else {
            // Note: we cannot handle this case because there is no size field
            panic!("Trying to truncate a file without an entry");
        }
        if let Some(current_cluster) = self.context.current_cluster {
            // current cluster is none only if offset is 0
            debug_assert!(self.context.offset > 0);
            self.fs.truncate_cluster_chain(current_cluster).await
        } else {
            debug_assert!(self.context.offset == 0);
            if let Some(n) = self.context.first_cluster {
                self.fs.free_cluster_chain(n).await?;
                self.context.first_cluster = None;
            }
            Ok(())
        }
    }

    /// Get the extents of a file on disk.
    ///
    /// This returns an iterator over the byte ranges on-disk occupied by
    /// this file.
    // pub fn extents(&mut self) -> impl Iterator<Item = Result<Extent, Error<IO::Error>>> + 'a {
    // let fs = self.fs;
    // let cluster_size = fs.cluster_size();
    // let mut bytes_left = match self.size() {
    //     Some(s) => s,
    //     None => return None.into_iter().flatten(),
    // };
    // let first = match self.context.first_cluster {
    //     Some(f) => f,
    //     None => return None.into_iter().flatten(),
    // };

    // Some(
    //     core::iter::once(Ok(first))
    //         .chain(fs.cluster_iter(first))
    //         .map(move |cluster_err| match cluster_err {
    //             Ok(cluster) => {
    //                 let size = cluster_size.min(bytes_left);
    //                 bytes_left -= size;
    //                 Ok(Extent {
    //                     offset: fs.offset_from_cluster(cluster),
    //                     size,
    //                 })
    //             }
    //             Err(e) => Err(e),
    //         }),
    // )
    // .into_iter()
    // .flatten()
    // todo!("extents needs to be implemented using AsyncIterator");
    // }

    pub(crate) fn abs_pos(&self) -> Option<u64> {
        // Returns current position relative to filesystem start
        // Note: when between clusters it returns position after previous cluster
        match self.context.current_cluster {
            Some(n) => {
                let cluster_size = self.fs.cluster_size();
                let offset_mod_cluster_size = self.context.offset % cluster_size;
                let offset_in_cluster = if offset_mod_cluster_size == 0 {
                    // position points between clusters - we are returning previous cluster so
                    // offset must be set to the cluster size
                    cluster_size
                } else {
                    offset_mod_cluster_size
                };
                let offset_in_fs = self.fs.offset_from_cluster(n) + u64::from(offset_in_cluster);
                Some(offset_in_fs)
            }
            None => None,
        }
    }

    async fn flush_dir_entry(&mut self) -> Result<(), Error<IO::Error>> {
        if let Some(ref mut e) = self.context.entry {
            e.flush(self.fs).await?;
        }
        Ok(())
    }

    /// Sets date and time of creation for this file.
    ///
    /// Note: it is set to a value from the `TimeProvider` when creating a file.
    /// Deprecated: if needed implement a custom `TimeProvider`.
    #[deprecated]
    pub fn set_created(&mut self, date_time: DateTime) {
        if let Some(ref mut e) = self.context.entry {
            e.set_created(date_time);
        }
    }

    /// Sets date of last access for this file.
    ///
    /// Note: it is overwritten by a value from the `TimeProvider` on every file read operation.
    /// Deprecated: if needed implement a custom `TimeProvider`.
    #[deprecated]
    pub fn set_accessed(&mut self, date: Date) {
        if let Some(ref mut e) = self.context.entry {
            e.set_accessed(date);
        }
    }

    /// Sets date and time of last modification for this file.
    ///
    /// Note: it is overwritten by a value from the `TimeProvider` on every file write operation.
    /// Deprecated: if needed implement a custom `TimeProvider`.
    #[deprecated]
    pub fn set_modified(&mut self, date_time: DateTime) {
        if let Some(ref mut e) = self.context.entry {
            e.set_modified(date_time);
        }
    }

    fn size(&self) -> Option<u32> {
        match self.context.entry {
            Some(ref e) => e.inner().size(),
            None => None,
        }
    }

    fn is_dir(&self) -> bool {
        match self.context.entry {
            Some(ref e) => e.inner().is_dir(),
            None => false,
        }
    }

    fn bytes_left_in_file(&self) -> Option<usize> {
        // Note: seeking beyond end of file is not allowed so overflow is impossible
        self.size().map(|s| (s - self.context.offset) as usize)
    }

    fn set_first_cluster(&mut self, cluster: u32) {
        self.context.first_cluster = Some(cluster);
        if let Some(ref mut e) = self.context.entry {
            e.set_first_cluster(self.context.first_cluster, self.fs.fat_type());
        }
    }

    pub(crate) fn first_cluster(&self) -> Option<u32> {
        self.context.first_cluster
    }

    /// Whether this `File` is actually the FAT32 root directory's backing
    /// stream (`FileSystem::root_dir` builds it with `entry: None` -- see
    /// `FileContext::entry`'s doc comment -- unlike every other directory
    /// or file, which owns a `DirEntryEditor` into its parent).
    pub(crate) fn is_root_dir(&self) -> bool {
        self.context.entry.is_none()
    }

    async fn flush(&mut self) -> Result<(), Error<IO::Error>> {
        self.flush_dir_entry().await?;
        let mut disk = self.fs.disk.borrow_mut();
        disk.flush().await?;
        Ok(())
    }
}

impl<IO: ReadWriteSeek, TP: TimeProvider, OCC> File<'_, IO, TP, OCC> {
    fn update_dir_entry_after_write(&mut self) {
        let offset = self.context.offset;
        if let Some(ref mut e) = self.context.entry {
            let now = self.fs.options.time_provider.get_current_date_time();
            e.set_modified(now);
            if e.inner().size().map_or(false, |s| offset > s) {
                e.set_size(offset);
            }
        }
    }

    /// Manually close the file
    ///
    /// A [`FileContext`] is returned, which can be used in conjunction with the
    /// `to_file_with_context` API.
    pub async fn close(mut self) -> Result<FileContext, Error<IO::Error>> {
        self.flush().await?;
        Ok(FileContext {
            first_cluster: self.context.first_cluster,
            current_cluster: self.context.current_cluster,
            offset: self.context.offset,
            entry: self.context.entry.clone(),
        })
    }

    /// Detach the file into a [`FileContext`] WITHOUT flushing the dir entry.
    ///
    /// Unlike [`close`], this does not persist the (possibly dirty) in-memory
    /// size/mtime edit to disk — the returned context carries it for a later
    /// reattach+flush. Used by a long-lived buffered writer (the sample
    /// recorder) that flushes only once at finalize. Clears the local file's
    /// dirty flag before drop so `Drop`'s dirty-file warning/panic does not fire
    /// (the dirtiness is intentionally carried in the returned context).
    pub fn detach(mut self) -> FileContext {
        let ctx = FileContext {
            first_cluster: self.context.first_cluster,
            current_cluster: self.context.current_cluster,
            offset: self.context.offset,
            entry: self.context.entry.clone(),
        };
        if let Some(ref mut e) = self.context.entry {
            e.set_dirty(false); // silence Drop; the returned ctx keeps the dirty edit
        }
        ctx
    }
}

impl<IO: ReadWriteSeek, TP, OCC> Drop for File<'_, IO, TP, OCC> {
    fn drop(&mut self) {
        if let Some(e) = &self.context.entry {
            if e.dirty() {
                warn!("Dropping dirty file before flushing");
                #[cfg(feature = "dirty-file-panic")]
                {
                    panic!("Dropping unflushed file");
                }
            }
        }
    }
}

// Note: derive cannot be used because of invalid bounds. See: https://github.com/rust-lang/rust/issues/26925
impl<IO: ReadWriteSeek, TP, OCC> Clone for File<'_, IO, TP, OCC> {
    fn clone(&self) -> Self {
        File {
            context: self.context.clone(),
            fs: self.fs,
        }
    }
}

impl<IO: ReadWriteSeek, TP, OCC> IoBase for File<'_, IO, TP, OCC> {
    type Error = Error<IO::Error>;
}

impl<IO: ReadWriteSeek, TP: TimeProvider, OCC> Read for File<'_, IO, TP, OCC> {
    async fn read(&mut self, buf: &mut [u8]) -> Result<usize, Self::Error> {
        trace!("File::read");
        let cluster_size = self.fs.cluster_size();
        let current_cluster_opt = if self.context.offset % cluster_size == 0 {
            // next cluster
            match self.context.current_cluster {
                None => self.context.first_cluster,
                Some(n) => {
                    let r = self.fs.cluster_iter(n).next().await;
                    match r {
                        Some(Err(err)) => return Err(err),
                        Some(Ok(n)) => Some(n),
                        None => None,
                    }
                }
            }
        } else {
            self.context.current_cluster
        };
        let current_cluster = match current_cluster_opt {
            Some(n) => n,
            None => return Ok(0),
        };
        let offset_in_cluster = self.context.offset % cluster_size;
        let bytes_left_in_cluster = (cluster_size - offset_in_cluster) as usize;
        let bytes_left_in_file = self.bytes_left_in_file().unwrap_or(bytes_left_in_cluster);
        let read_size = cmp::min(cmp::min(buf.len(), bytes_left_in_cluster), bytes_left_in_file);
        if read_size == 0 {
            return Ok(0);
        }
        trace!("read {} bytes in cluster {}", read_size, current_cluster);
        let offset_in_fs = self.fs.offset_from_cluster(current_cluster) + u64::from(offset_in_cluster);
        let read_bytes = {
            let mut disk = self.fs.disk.borrow_mut();
            disk.seek(SeekFrom::Start(offset_in_fs)).await?;
            disk.read(&mut buf[..read_size]).await?
        };
        if read_bytes == 0 {
            return Ok(0);
        }
        self.context.offset += read_bytes as u32;
        self.context.current_cluster = Some(current_cluster);

        if let Some(ref mut e) = self.context.entry {
            if self.fs.options.update_accessed_date {
                let now = self.fs.options.time_provider.get_current_date();
                e.set_accessed(now);
            }
        }
        Ok(read_bytes)
    }
}

impl<IO: ReadWriteSeek, TP: TimeProvider, OCC> Write for File<'_, IO, TP, OCC> {
    async fn write(&mut self, buf: &[u8]) -> Result<usize, Self::Error> {
        trace!("File::write");
        let cluster_size = self.fs.cluster_size();
        let offset_in_cluster = self.context.offset % cluster_size;
        let bytes_left_in_cluster = (cluster_size - offset_in_cluster) as usize;
        let bytes_left_until_max_file_size = (MAX_FILE_SIZE - self.context.offset) as usize;
        let write_size = cmp::min(buf.len(), bytes_left_in_cluster);
        let write_size = cmp::min(write_size, bytes_left_until_max_file_size);
        // Exit early if we are going to write no data
        if write_size == 0 {
            if self.context.offset >= MAX_FILE_SIZE {
                warn!("File size limit reached (4GiB)");
            }
            return Ok(0);
        }
        // Mark the volume 'dirty'
        self.fs.set_dirty_flag(true).await?;
        // Get cluster for write possibly allocating new one
        let current_cluster = if self.context.offset % cluster_size == 0 {
            // next cluster
            let next_cluster = match self.context.current_cluster {
                None => self.context.first_cluster,
                Some(n) => {
                    let r = self.fs.cluster_iter(n).next().await;
                    match r {
                        Some(Err(err)) => return Err(err),
                        Some(Ok(n)) => Some(n),
                        None => None,
                    }
                }
            };
            if let Some(n) = next_cluster {
                n
            } else {
                // end of chain reached - allocate new cluster
                let new_cluster = self
                    .fs
                    .alloc_cluster(self.context.current_cluster, self.is_dir())
                    .await?;
                trace!("allocated cluster {}", new_cluster);
                if self.context.first_cluster.is_none() {
                    self.set_first_cluster(new_cluster);
                }
                new_cluster
            }
        } else {
            // self.context.current_cluster should be a valid cluster
            match self.context.current_cluster {
                Some(n) => n,
                None => panic!("Offset inside cluster but no cluster allocated"),
            }
        };
        trace!("write {} bytes in cluster {}", write_size, current_cluster);
        let offset_in_fs = self.fs.offset_from_cluster(current_cluster) + u64::from(offset_in_cluster);
        let written_bytes = {
            let mut disk = self.fs.disk.borrow_mut();
            disk.seek(SeekFrom::Start(offset_in_fs)).await?;
            disk.write(&buf[..write_size]).await?
        };
        if written_bytes == 0 {
            return Ok(0);
        }
        // some bytes were writter - update position and optionally size
        self.context.offset += written_bytes as u32;
        self.context.current_cluster = Some(current_cluster);
        self.update_dir_entry_after_write();
        Ok(written_bytes)
    }

    async fn flush(&mut self) -> Result<(), Self::Error> {
        Self::flush(self).await
    }
}

impl<IO: ReadWriteSeek, TP, OCC> Seek for File<'_, IO, TP, OCC> {
    async fn seek(&mut self, pos: SeekFrom) -> Result<u64, Self::Error> {
        trace!("File::seek");
        let size_opt = self.size();
        let new_offset_opt: Option<u32> = match pos {
            SeekFrom::Current(x) => i64::from(self.context.offset)
                .checked_add(x)
                .and_then(|n| u32::try_from(n).ok()),
            SeekFrom::Start(x) => u32::try_from(x).ok(),
            SeekFrom::End(o) => size_opt
                .and_then(|s| i64::from(s).checked_add(o))
                .and_then(|n| u32::try_from(n).ok()),
        };
        let mut new_offset = if let Some(new_offset) = new_offset_opt {
            new_offset
        } else {
            error!("Invalid seek offset");
            return Err(Error::InvalidInput);
        };
        if let Some(size) = size_opt {
            if new_offset > size {
                warn!("Seek beyond the end of the file");
                new_offset = size;
            }
        }
        trace!(
            "file seek {} -> {} - entry {:?}",
            self.context.offset,
            new_offset,
            self.context.entry
        );
        if new_offset == self.context.offset {
            // position is the same - nothing to do
            return Ok(u64::from(self.context.offset));
        }
        let new_offset_in_clusters = self.fs.clusters_from_bytes(u64::from(new_offset));
        let old_offset_in_clusters = self.fs.clusters_from_bytes(u64::from(self.context.offset));
        let new_cluster = if new_offset == 0 {
            None
        } else if new_offset_in_clusters == old_offset_in_clusters {
            self.context.current_cluster
        } else if let Some(first_cluster) = self.context.first_cluster {
            // calculate number of clusters to skip
            // return the previous cluster if the offset points to the cluster boundary
            // Note: new_offset_in_clusters cannot be 0 here because new_offset is not 0
            debug_assert!(new_offset_in_clusters > 0);
            let clusters_to_skip = new_offset_in_clusters - 1;

            // LOCAL ADDITION (SP1b): continue forward from `current_cluster` when the
            // target is ahead of the current position, instead of restarting at
            // `first_cluster`. Upstream has only the same-cluster fast path above, so
            // every cross-cluster seek — including a one-cluster step forward — was an
            // O(n) re-walk from the head of the chain.
            //
            // `old_offset_in_clusters` rounds up just like `new_offset_in_clusters`, so
            // `current_cluster` is chain index `old_offset_in_clusters - 1`, matching
            // the same "previous cluster at a boundary" convention. Only valid when the
            // old offset is non-zero (else `current_cluster` is None).
            let (mut cluster, already_skipped) = match self.context.current_cluster {
                Some(c) if old_offset_in_clusters > 0 && clusters_to_skip >= old_offset_in_clusters - 1 => {
                    (c, old_offset_in_clusters - 1)
                }
                _ => (first_cluster, 0),
            };
            let mut iter = self.fs.cluster_iter(cluster);
            for i in already_skipped..clusters_to_skip {
                cluster = if let Some(r) = iter.next().await {
                    r?
                } else {
                    // cluster chain ends before the new position - seek to the end of the last cluster
                    new_offset = self.fs.bytes_from_clusters(i + 1) as u32;
                    break;
                };
            }
            Some(cluster)
        } else {
            // empty file - always seek to 0
            new_offset = 0;
            None
        };
        self.context.offset = new_offset;
        self.context.current_cluster = new_cluster;
        Ok(u64::from(self.context.offset))
    }
}

#[cfg(test)]
mod context_bytes_tests {
    use super::{FileContext, FILE_CONTEXT_BYTES};
    use crate::dir_entry::{DirEntryEditor, DirFileEntryData};

    fn entry_bytes() -> [u8; 32] {
        let mut raw = [0u8; 32];
        raw[0..11].copy_from_slice(b"SNARE58 WAV");
        raw[11] = 0x20; // archive
        raw[20..22].copy_from_slice(&0x0001u16.to_le_bytes()); // first cluster, high half
        raw[26..28].copy_from_slice(&0x2345u16.to_le_bytes()); // first cluster, low half
        raw[28..32].copy_from_slice(&71_680u32.to_le_bytes()); // size
        raw
    }

    #[test]
    fn a_context_survives_its_bytes() {
        let ctx = FileContext {
            first_cluster: Some(0x0001_2345),
            current_cluster: Some(0x0001_2399),
            offset: 40_960,
            entry: Some(DirEntryEditor::from_parts(DirFileEntryData::from_bytes(&entry_bytes()), 0x0040_1220, true)),
        };
        let bytes = ctx.to_bytes();
        let back = FileContext::from_bytes(&bytes).expect("an encoded context decodes");
        assert_eq!(back.first_cluster, Some(0x0001_2345));
        assert_eq!(back.current_cluster, Some(0x0001_2399));
        assert_eq!(back.offset, 40_960);
        let entry = back.entry.as_ref().expect("the entry survives");
        assert_eq!(entry.pos(), 0x0040_1220);
        assert!(entry.dirty());
        assert_eq!(entry.inner().to_bytes(), entry_bytes());
        assert_eq!(back.to_bytes(), bytes);
    }

    #[test]
    fn an_empty_file_with_no_entry_survives_its_bytes() {
        let ctx = FileContext {
            first_cluster: None,
            current_cluster: None,
            offset: 0,
            entry: None,
        };
        let back = FileContext::from_bytes(&ctx.to_bytes()).expect("decodes");
        assert_eq!(back.first_cluster, None);
        assert_eq!(back.current_cluster, None);
        assert!(back.entry.is_none());
    }

    #[test]
    fn bytes_that_hold_no_context_are_refused() {
        assert!(FileContext::from_bytes(&[0u8; FILE_CONTEXT_BYTES]).is_none());
    }
}
