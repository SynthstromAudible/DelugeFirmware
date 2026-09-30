//! A `BlockDevice<512>` over the shared [`RamDisk`] image.
//!
//! [`EFatFs`](crate::efatfs::EFatFs) mounts through this and the same `BufStream` adapter stack the
//! device uses, rather than a byte-granular shortcut.

use aligned::{Aligned, A4};
use block_device_driver::BlockDevice;

use crate::ram_disk::RamDisk;

/// Block size in bytes; the SD card's sector size.
const BLOCK_SIZE: usize = 512;

/// A `BlockDevice<512>` over the shared [`RamDisk`] image.
pub struct FileBlockDevice;

impl BlockDevice<BLOCK_SIZE> for FileBlockDevice {
    type Align = A4;
    type Error = core::convert::Infallible;

    async fn read(
        &mut self,
        block_address: u32,
        data: &mut [Aligned<A4, [u8; BLOCK_SIZE]>],
    ) -> Result<(), Self::Error> {
        for (i, blk) in data.iter_mut().enumerate() {
            let off = (block_address as u64 + i as u64) * BLOCK_SIZE as u64;
            RamDisk::read_at(off, &mut blk[..]);
        }
        Ok(())
    }

    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<A4, [u8; BLOCK_SIZE]>],
    ) -> Result<(), Self::Error> {
        for (i, blk) in data.iter().enumerate() {
            let off = (block_address as u64 + i as u64) * BLOCK_SIZE as u64;
            RamDisk::write_at(off, &blk[..]);
        }
        Ok(())
    }

    async fn size(&mut self) -> Result<u64, Self::Error> {
        Ok(RamDisk::len())
    }
}
