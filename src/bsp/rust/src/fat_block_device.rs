//! Device `block_device_driver::BlockDevice<512>` over the real SD driver
//! (`deluge_bsp::sd`) — the on-device sibling of `fs_differential`'s host
//! `FileBlockDevice` (`fs_differential/src/block_dev.rs`), which mirrors this
//! shape over a file-backed `RamDisk` instead. Both feed the same
//! `BufStream`/`embedded-fatfs` stack (`crates/block-device-driver`,
//! `crates/block-device-adapters`) this crate brings up on-device.
//!
//! Unlike the host adapter, `type Error` here is `deluge_bsp::sd::SdError`,
//! not `core::convert::Infallible`: `sd::read_sectors`/`write_sectors` talk to
//! real SDHI hardware over DMA and can genuinely fail (card removed, protocol
//! error, DMA timeout, …). `Infallible` would make that failure
//! un-representable at the `BlockDevice` boundary; propagating `SdError`
//! keeps a real I/O error visible to `embedded-fatfs` instead of silently
//! discarding it (or panicking, `Infallible`'s only other option under a
//! `Result` that must never be `Err`).
//!
//! Device-only: the real `sd` driver only exists under `target_os = "none"`
//! (see `deluge_bsp::sd`'s module doc — its host stand-in is a file-backed
//! image, already exercised by `fs_differential`, so there is nothing new to
//! adapt on host).
#![cfg(target_os = "none")]

use aligned::{A4, Aligned};
use block_device_driver::BlockDevice;
use deluge_bsp::sd::{self, SdError};

/// Block size this adapter (and the vendored FatFS `ffconf.h`, `FF_MIN_SS ==
/// FF_MAX_SS == 512`) both fix.
const BLOCK_SIZE: usize = 512;

/// A `BlockDevice<512>` over the real SDHI1 driver (`deluge_bsp::sd`).
///
/// Zero-sized: `sd`'s card state lives in module statics (see `sd.rs`), so
/// this is just a handle onto the shared driver, exactly like `sd::device::
/// DelugeBlockDevice` (the existing synchronous `embedded_sdmmc::BlockDevice`
/// impl this crate's FatFS diskio shim uses) — this is the async sibling for
/// `embedded-fatfs`.
pub struct SdBlockDevice;

// B7 REGRESSION GUARD: every transfer in this impl MUST go through
// `crate::sd::locked_*`, never `deluge_bsp::sd::read_sectors`/`write_sectors`
// directly. The raw calls bypass SD_BUS, which is what keeps a transfer off the single SDHI
// controller while `sd::card_service` is re-identifying the card (see `read`'s comment below —
// the original C-FatFS rationale is dead, the lock is not). This shipped bypassed until
// 2026-07-21 (see docs/dev/known-concurrency-bugs.md B7).
impl BlockDevice<BLOCK_SIZE> for SdBlockDevice {
    type Error = SdError;
    type Align = A4;

    async fn read(
        &mut self,
        block_address: u32,
        data: &mut [Aligned<A4, [u8; BLOCK_SIZE]>],
    ) -> Result<(), SdError> {
        let count = data.len() as u32;
        // SAFETY: `Aligned<A4, [u8; BLOCK_SIZE]>` is `#[repr(C)]` over the
        // inner `[u8; BLOCK_SIZE]` array plus a zero-sized alignment marker
        // (aligned 0.4's `Aligned<A, T>` has no other fields), so it has the
        // same size (BLOCK_SIZE) and layout as a bare `[u8; BLOCK_SIZE]`, and
        // a `[Aligned<A4, [u8; BLOCK_SIZE]>]` slice is therefore a contiguous
        // run of BLOCK_SIZE-byte blocks with no inter-element padding —
        // reinterpreting it as `data.len() * BLOCK_SIZE` flat bytes is sound.
        // The pointer stays validly derived from `data` (same allocation,
        // narrower type, non-null, no aliasing introduced — `data` is
        // borrowed for this call's lifetime and nothing else reads it
        // meanwhile), and the resulting `&mut [u8]` doesn't outlive `data`.
        let flat = unsafe {
            core::slice::from_raw_parts_mut(data.as_mut_ptr().cast::<u8>(), data.len() * BLOCK_SIZE)
        };
        // Route through the SD_BUS-guarded helper, NOT the raw driver call.
        //
        // The original reason was to stop an efatfs transfer interleaving with a fiber-side
        // C-FatFS transfer on the one SDHI controller. C-FatFS is gone, but the lock is NOT
        // redundant and must not be removed: `sd::card_service` holds SD_BUS across
        // `sd::init().await` — the ~1.3 s card-identification sequence — with no FS mutex
        // involved, so SD_BUS is the only thing keeping a transfer and a card bring-up off the
        // controller at the same time. That ordering matters MORE since streaming moved to its own
        // interrupt executor, which preempts the thread mode `card_service` runs on.
        //
        // Lock order: this is reached under the efatfs FS mutex, so the order is
        // FS -> SD_BUS. Nothing takes SD_BUS and then FS, so there is no inversion.
        crate::sd::locked_read_sectors(block_address, count, flat).await
    }

    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<A4, [u8; BLOCK_SIZE]>],
    ) -> Result<(), SdError> {
        let count = data.len() as u32;
        // SAFETY: see the identical reinterpret in `read`, above — same
        // layout argument, read-only direction.
        let flat = unsafe {
            core::slice::from_raw_parts(data.as_ptr().cast::<u8>(), data.len() * BLOCK_SIZE)
        };
        // B7: SD_BUS-guarded, for the same reason as `read` above.
        crate::sd::locked_write_sectors(block_address, count, flat).await
    }

    async fn size(&mut self) -> Result<u64, SdError> {
        Ok(u64::from(sd::total_sectors()) * BLOCK_SIZE as u64)
    }
}

/// Stack-instantiation smoke: names the full device storage stack (`SdBlockDevice`
/// -> `BufStream` -> `embedded_fatfs::FileSystem`) so the type-checker fully
/// monomorphizes it for `armv7a-none-eabihf` — the decisive device-readiness
/// check for the on-device storage stack. Never called (no card mount happens here); its only
/// job is to force codegen of the whole stack. `#[allow(dead_code)]` since
/// nothing calls it — its existence, not its execution, is the point.
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
