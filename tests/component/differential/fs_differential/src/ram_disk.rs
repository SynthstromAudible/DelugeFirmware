//! A single Rust-owned in-RAM disk image, read/written byte-granular by
//! `block_dev::FileBlockDevice` on behalf of `efatfs::EFatFs`'s
//! `embedded-fatfs` mount.
//!
//! Only one image is live at a time (`DISK` is a single process-wide slot),
//! matching the harness's single-threaded use: load an image, mount and
//! drive `embedded-fatfs` against it, done.
use std::sync::Mutex;

static DISK: Mutex<Vec<u8>> = Mutex::new(Vec::new());

pub struct RamDisk;

impl RamDisk {
    /// Load a card image file into the shared in-RAM disk, replacing
    /// whatever was there before.
    pub fn load(image_path: &str) -> Self {
        Self::load_bytes(&std::fs::read(image_path).expect("read image"))
    }

    /// Load raw image bytes into the shared in-RAM disk, replacing whatever
    /// was there before. Tests that need a fresh, independent copy of the
    /// same starting fixture (e.g. to compare two runs against each other)
    /// reload it between them -- write ops mutate the shared `DISK` in
    /// place (see `tests/differential.rs`'s `TEST_LOCK`, which serializes
    /// the whole load-mount-read/write span against other tests).
    pub fn load_bytes(bytes: &[u8]) -> Self {
        *DISK.lock().unwrap() = bytes.to_vec();
        RamDisk
    }

    /// A snapshot copy of the current disk contents, for before/after
    /// comparisons.
    pub fn snapshot(&self) -> Vec<u8> {
        DISK.lock().unwrap().clone()
    }

    /// Byte-granular read from the shared image into `buf`, starting at byte
    /// offset `pos`. Returns the number of bytes actually copied (`0` once
    /// `pos` is at or past the end of the image -- EOF).
    pub fn read_at(pos: u64, buf: &mut [u8]) -> usize {
        let d = DISK.lock().unwrap();
        let pos = pos as usize;
        if pos >= d.len() {
            return 0;
        }
        let n = buf.len().min(d.len() - pos);
        buf[..n].copy_from_slice(&d[pos..pos + n]);
        n
    }

    /// Byte-granular write into the shared image at byte offset `pos`,
    /// growing the image (zero-filled) if the write runs past its current
    /// end. Same `DISK` mutex as `read_at`.
    pub fn write_at(pos: u64, buf: &[u8]) {
        let mut d = DISK.lock().unwrap();
        let pos = pos as usize;
        let end = pos + buf.len();
        if end > d.len() {
            d.resize(end, 0);
        }
        d[pos..end].copy_from_slice(buf);
    }

    /// Current size of the shared image, in bytes.
    pub fn len() -> u64 {
        DISK.lock().unwrap().len() as u64
    }
}
