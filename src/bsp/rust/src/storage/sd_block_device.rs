//! `block_device_driver::BlockDevice<512>` over the real SD driver (`deluge_bsp::sd`), the device
//! block device under the `BufStream`/`embedded-fatfs` stack.
//!
//! `type Error` is `SdError`, not `Infallible` as in `fs_differential`'s file-backed
//! `FileBlockDevice`: SDHI transfers can fail (card removed, protocol error, DMA timeout), and the
//! error has to reach `embedded-fatfs` rather than be discarded or panic.
#![cfg(target_os = "none")]

use aligned::{A4, Aligned};
use block_device_driver::BlockDevice;
use deluge_bsp::sd::{self, SdError};

/// Block size fixed by this adapter and the vendored FatFS `ffconf.h` (`FF_MIN_SS == FF_MAX_SS ==
/// 512`).
const BLOCK_SIZE: usize = 512;

/// A `BlockDevice<512>` over the SDHI1 driver.
///
/// Zero-sized: the card state lives in `deluge_bsp::sd`'s statics. The device efatfs mount is
/// built over this type.
pub struct SdBlockDevice;

// Every transfer must go through `crate::storage::sd::locked_*`, never the raw
// `deluge_bsp::sd::read_sectors`/`write_sectors`: the raw calls bypass SD_BUS, which keeps a
// transfer off the SDHI controller while `sd::card_service` re-identifies the card.
impl BlockDevice<BLOCK_SIZE> for SdBlockDevice {
    type Error = SdError;
    type Align = A4;

    async fn read(
        &mut self,
        block_address: u32,
        data: &mut [Aligned<A4, [u8; BLOCK_SIZE]>],
    ) -> Result<(), SdError> {
        let count = data.len() as u32;
        // SAFETY: `Aligned<A4, [u8; BLOCK_SIZE]>` is `#[repr(C)]` over the array plus a zero-sized
        // alignment marker, so the slice is a contiguous, padding-free run of BLOCK_SIZE-byte
        // blocks. The flat view is derived from `data`, covers the same bytes, and does not
        // outlive the exclusive borrow.
        let flat = unsafe {
            core::slice::from_raw_parts_mut(data.as_mut_ptr().cast::<u8>(), data.len() * BLOCK_SIZE)
        };
        // SD_BUS is not redundant here: `sd::card_service` holds it across `sd::init().await`
        // (about 1.3 s of card identification) without the FS mutex, and the streaming executor
        // preempts the thread-mode executor `card_service` runs on.
        //
        // Lock order is FS mutex -> SD_BUS; nothing takes SD_BUS and then the FS mutex.
        crate::storage::sd::locked_read_sectors(block_address, count, flat).await
    }

    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<A4, [u8; BLOCK_SIZE]>],
    ) -> Result<(), SdError> {
        let count = data.len() as u32;
        // SAFETY: same layout argument as in `read`; shared, read-only view.
        let flat = unsafe {
            core::slice::from_raw_parts(data.as_ptr().cast::<u8>(), data.len() * BLOCK_SIZE)
        };
        crate::storage::sd::locked_write_sectors(block_address, count, flat).await
    }

    async fn size(&mut self) -> Result<u64, SdError> {
        Ok(u64::from(sd::total_sectors()) * BLOCK_SIZE as u64)
    }
}

/// Forces the whole device storage stack (`SdBlockDevice` -> `BufStream` ->
/// `embedded_fatfs::FileSystem`) to monomorphize for the device target.
///
/// Never called; its existence is the check.
#[allow(dead_code)]
async fn stack_instantiation_smoke() {
    use block_device_adapters::BufStream;
    use embedded_fatfs::{FileSystem, FsOptions};

    let storage = BufStream::<SdBlockDevice, BLOCK_SIZE>::new(SdBlockDevice);
    if let Ok(fs) = FileSystem::new(storage, FsOptions::new()).await {
        let root = fs.root_dir();
        let mut iter = root.iter();
        let _ = iter.next().await;
    }
}
