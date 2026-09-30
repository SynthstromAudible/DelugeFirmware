//! The process-wide in-RAM disk image that
//! [`FileBlockDevice`](crate::block_dev::FileBlockDevice) serves.
//!
//! Only one image is live at a time; tests that use it must serialize.
use std::sync::Mutex;

static DISK: Mutex<Vec<u8>> = Mutex::new(Vec::new());

/// Handle to the shared disk image.
pub struct RamDisk;

impl RamDisk {
    /// Loads a card image file, replacing the current image.
    pub fn load(image_path: &str) -> Self {
        Self::load_bytes(&std::fs::read(image_path).expect("read image"))
    }

    /// Loads raw image bytes, replacing the current image.
    ///
    /// Writes mutate the image in place, so a test that needs the same starting fixture twice
    /// reloads it.
    pub fn load_bytes(bytes: &[u8]) -> Self {
        *DISK.lock().unwrap() = bytes.to_vec();
        RamDisk
    }

    /// Returns a copy of the current image.
    pub fn snapshot(&self) -> Vec<u8> {
        DISK.lock().unwrap().clone()
    }

    /// Reads from byte offset `pos` into `buf`; returns the bytes copied (`0` at or past the end).
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

    /// Writes `buf` at byte offset `pos`, zero-extending the image if the write runs past its end.
    pub fn write_at(pos: u64, buf: &[u8]) {
        let mut d = DISK.lock().unwrap();
        let pos = pos as usize;
        let end = pos + buf.len();
        if end > d.len() {
            d.resize(end, 0);
        }
        d[pos..end].copy_from_slice(buf);
    }

    /// Returns the image size in bytes.
    pub fn len() -> u64 {
        DISK.lock().unwrap().len() as u64
    }
}
