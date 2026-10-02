//! The read and write interfaces a filesystem backend presents to the harness.
//!
//! [`FsOps`] is what [`compare_read`](crate::diff::compare_read) walks: a live [`EFatFs`] mount or
//! a [`Captured`](crate::diff::Captured) tree.
use crate::efatfs::EFatFs;

/// A directory-listing entry.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub name: String,
    pub size: u64,
    pub is_dir: bool,
}

/// Read and enumerate operations a backend presents.
pub trait FsOps {
    fn read_file(&self, path: &str) -> Vec<u8>;
    fn read_dir(&self, path: &str) -> Vec<Entry>; // sorted by name
}

impl FsOps for EFatFs {
    fn read_file(&self, path: &str) -> Vec<u8> {
        EFatFs::read_file(self, path)
    }
    fn read_dir(&self, path: &str) -> Vec<Entry> {
        EFatFs::read_dir(self, path)
    }
}

/// One write operation, covering the primitives the firmware's file I/O uses.
#[derive(Debug, Clone)]
pub enum Op {
    Mkdir(String),
    Write(String, Vec<u8>),
    Extend(String, Vec<u8>),
    Delete(String),
    Rename(String, String),
}

/// Write operations a backend presents.
pub trait FsOpsMut: FsOps {
    /// Creates a directory; its parent must exist.
    fn mkdir(&mut self, path: &str);
    /// Creates or truncates `path` and writes `bytes` as its whole contents.
    fn write_new(&mut self, path: &str, bytes: &[u8]);
    /// Appends `bytes` to the existing file at `path`.
    fn append(&mut self, path: &str, bytes: &[u8]);
    /// Deletes an existing file or empty directory.
    fn delete(&mut self, path: &str);
    /// Renames or moves `from` to `to`.
    fn rename(&mut self, from: &str, to: &str);

    /// Applies `op` through the matching method.
    fn apply(&mut self, op: &Op) {
        match op {
            Op::Mkdir(p) => self.mkdir(p),
            Op::Write(p, b) => self.write_new(p, b),
            Op::Extend(p, b) => self.append(p, b),
            Op::Delete(p) => self.delete(p),
            Op::Rename(a, b) => self.rename(a, b),
        }
    }
}

impl FsOpsMut for EFatFs {
    fn mkdir(&mut self, path: &str) {
        EFatFs::mkdir(self, path)
    }
    fn write_new(&mut self, path: &str, bytes: &[u8]) {
        EFatFs::write_new(self, path, bytes)
    }
    fn append(&mut self, path: &str, bytes: &[u8]) {
        EFatFs::append(self, path, bytes)
    }
    fn delete(&mut self, path: &str) {
        EFatFs::delete(self, path)
    }
    fn rename(&mut self, from: &str, to: &str) {
        EFatFs::rename(self, from, to)
    }
}
