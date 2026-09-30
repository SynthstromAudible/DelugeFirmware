//! The `libdeluge/block_device.h` SD-card entry points and the shared SD bus.
//!
//! Both targets drive `deluge_bsp::sd`, which has the same async API on device (the SDHI1+DMA
//! driver, completing on the transfer IRQ) and host (a file-backed disk image whose transfers never
//! suspend).
//!
//! The app's filesystem does not use `deluge_block_read`/`deluge_block_write`: the device and host
//! block devices (`storage::sd_block_device`, `storage::efatfs::host`) await
//! [`locked_read_sectors`]/[`locked_write_sectors`] directly. The C-ABI transfer pair is called
//! only by the plain-host bootstrap round-trip in `runtime::host` and by
//! `tests/sim_latency_host.rs`. The app does call `deluge_block_ready` (`storage_manager.cpp`'s
//! card-present checks).
//!
//! Each C-ABI symbol has one definition per target configuration, selected by `cfg`.

#![allow(non_upper_case_globals)]

use core::sync::atomic::{AtomicBool, Ordering};

use deluge_bsp::sd;
#[cfg(any(target_os = "none", not(feature = "sim_latency")))]
use embassy_futures::block_on;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;

/// Serializes access to the one SDHI controller between filesystem transfers, streaming reads and
/// card identification.
pub static SD_BUS: Mutex<CriticalSectionRawMutex, ()> = Mutex::new(());

#[cfg(target_os = "none")]
use crate::sys::{
    DelugeCardEvent, DelugeCardEvent_DELUGE_CARD_EVENT_EJECTED as CARD_EJECTED,
    DelugeCardEvent_DELUGE_CARD_EVENT_INSERTED as CARD_INSERTED,
    DelugeCardEvent_DELUGE_CARD_EVENT_NONE as CARD_NONE, DelugeStatus,
    DelugeStatus_DELUGE_ERR_IO as DELUGE_ERR_IO, DelugeStatus_DELUGE_ERR_NODEV as DELUGE_ERR_NODEV,
    DelugeStatus_DELUGE_ERR_WRITE_PROTECTED as DELUGE_ERR_WRITE_PROTECTED,
    DelugeStatus_DELUGE_OK as DELUGE_OK,
};

// Host copies of the `libdeluge/types.h` / `libdeluge/block_device.h` types, which the plain host
// build has no bindgen output for: `enum DelugeStatus : int8_t` and `enum DelugeCardEvent :
// uint8_t`. Values must match the headers; under `host_app` they are pinned by `abi::pins`.
#[cfg(not(target_os = "none"))]
pub(crate) type DelugeStatus = i8;
#[cfg(not(target_os = "none"))]
pub(crate) const DELUGE_OK: DelugeStatus = 0;
#[cfg(not(target_os = "none"))]
pub(crate) const DELUGE_ERR_IO: DelugeStatus = -5;
#[cfg(not(target_os = "none"))]
pub(crate) const DELUGE_ERR_NODEV: DelugeStatus = -6;
#[cfg(not(target_os = "none"))]
pub(crate) const DELUGE_ERR_WRITE_PROTECTED: DelugeStatus = -12;

#[cfg(not(target_os = "none"))]
pub(crate) type DelugeCardEvent = u8;
#[cfg(not(target_os = "none"))]
pub(crate) const CARD_NONE: DelugeCardEvent = 0;
#[cfg(not(target_os = "none"))]
pub(crate) const CARD_INSERTED: DelugeCardEvent = 1;
#[cfg(not(target_os = "none"))]
pub(crate) const CARD_EJECTED: DelugeCardEvent = 2;

// DSTATUS bits in ChaN FatFs's `diskio.h` encoding, composed by `status_bits()`.
#[cfg(target_os = "none")]
const STA_NOINIT: u8 = 0x01;
#[cfg(target_os = "none")]
const STA_NODISK: u8 = 0x02;
#[cfg(target_os = "none")]
const STA_PROTECT: u8 = 0x04;
/// Bytes per sector.
const SECTOR_SIZE: usize = 512;

/// Set once `boot_init` has run `sd::init()`.
///
/// Before that, `SD_CD` (P7_0 → SDHI fn 3) is not muxed and `SD_CLK` is not running, so
/// `sd::is_inserted()` is meaningless; `status_bits()` reports "not initialised, presence unknown"
/// rather than `STA_NODISK` until this is set.
#[cfg(target_os = "none")]
static CONTROLLER_UP: AtomicBool = AtomicBool::new(false);

/// Brings the SD controller and card up; called once from the device `app_task` before
/// `deluge_app_init`.
///
/// `sd::init()` paces power-up with `embassy_time::Timer`s, which with the integrated timer queue
/// resolve only under a real Embassy task waker and panic under `block_on`. So initialisation must
/// run here, on a task; the transfers themselves use no timers and are safe under `block_on`.
#[cfg(target_os = "none")]
pub async fn boot_init() {
    let r = sd::init().await;
    // The pin mux + SDHI bring-up has run, so card-detect is meaningful now.
    CONTROLLER_UP.store(true, Ordering::Release);
    log::info!(
        "sd: boot_init — init={:?} inserted={} wp={}",
        r.is_ok(),
        sd::is_inserted(),
        sd::is_write_protected(),
    );
}

/// Request to re-identify the card, raised by `deluge_block_poll_card_event` on an insertion and
/// served by `card_service`.
///
/// Coalescing several insertions into one init is correct: the task only needs to know that an
/// unidentified card is present.
#[cfg(target_os = "none")]
static CARD_INIT_REQ: embassy_sync::signal::Signal<CriticalSectionRawMutex, ()> =
    embassy_sync::signal::Signal::new();

/// Re-identifies the card after a swap.
///
/// Runs on a task because `sd::init()` needs timers (see `boot_init`). Nothing waits on it: the
/// result is published through the card's ready state, which `deluge_block_ready()` reports and the
/// app's `AudioFileManager::reinitEjectedCard` polls, re-mounting once it passes.
///
/// Holds [`SD_BUS`] across the init so identification cannot interleave with another transfer.
/// Storage-tier transfers wait and then run on the new card; interaction-tier transfers are refused
/// while the card is not ready (see `acquire_bus`).
#[cfg(target_os = "none")]
#[embassy_executor::task]
pub async fn card_service() {
    loop {
        CARD_INIT_REQ.wait().await;
        let _guard = SD_BUS.lock().await;
        // Re-check presence under the lock: the card may have been pulled again
        // between the edge and here, in which case there is nothing to identify
        // and the next insertion edge will raise a fresh request.
        if !sd::is_inserted() {
            log::info!("sd: card gone again before re-init; waiting for the next insertion");
            continue;
        }
        match sd::init().await {
            Ok(()) => log::info!("sd: card re-identified after swap"),
            // Not fatal, and not retried here: an unreadable or half-seated card
            // leaves the app in its "ejected" state, still polling, and any later
            // reseat raises another insertion edge.
            Err(e) => log::warn!("sd: re-init after swap failed: {:?}", e),
        }
    }
}

/// Brings the host SD image up before `deluge_app_init`, mirroring the device boot sequence.
///
/// Not strictly required: the host image opens lazily on first access. Idempotent.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
pub async fn boot_init() {
    let r = sd::init().await;
    log::info!(
        "sd: boot_init (host) — init={:?} inserted={} wp={}",
        r.is_ok(),
        sd::is_inserted(),
        sd::is_write_protected(),
    );
}

/// Takes [`SD_BUS`] for one transfer.
///
/// An interaction-tier caller fails fast with `NotInitialized` while the card is not ready: it
/// spins synchronously on the thread executor, the same executor `card_service` needs to finish
/// the identification it holds the bus for, so waiting would deadlock. Readiness is re-checked on
/// every poll, and every identification follows an `invalidate()`, so such a caller is refused at
/// once.
///
/// Other tiers wait: a storage-tier read runs on its own executor and succeeds on the re-identified
/// card, rather than failing a re-inserted card's samples.
async fn acquire_bus()
-> Result<embassy_sync::mutex::MutexGuard<'static, CriticalSectionRawMutex, ()>, sd::SdError> {
    let mut lock = core::pin::pin!(SD_BUS.lock());
    core::future::poll_fn(move |cx| {
        if crate::tier::current() == crate::tier::Tier::Interaction && !sd::is_ready() {
            return core::task::Poll::Ready(Err(sd::SdError::NotInitialized));
        }
        core::future::Future::poll(lock.as_mut(), cx).map(Ok)
    })
    .await
}

/// Reads `count` sectors from `lba` into `buf`, holding [`SD_BUS`].
///
/// The transfer chokepoint for the filesystem block devices and the streaming loader. Under
/// `sim_latency` the read goes through `sim_latency::modeled_read`, which does not take the bus
/// itself.
///
/// # Errors
///
/// Returns `NotInitialized` for an interaction-tier caller while the card is not ready (see
/// `acquire_bus`), or the driver's error.
pub async fn locked_read_sectors(lba: u32, count: u32, buf: &mut [u8]) -> Result<(), sd::SdError> {
    let _guard = acquire_bus().await?;
    // Counted here, once per transfer; the `sim_latency` `deluge_block_read` bypasses this function
    // and counts its own.
    #[cfg(not(target_os = "none"))]
    stats::note_read();
    #[cfg(all(not(target_os = "none"), feature = "sim_latency"))]
    {
        // Every transfer is modeled: blocking callers spin through `sim_block::block_on`, whose
        // progress hook pumps `pump`'s executor and advances the virtual clock, so the modeled
        // delay always completes.
        sim_latency::modeled_read(lba, count, buf).await
    }
    #[cfg(not(all(not(target_os = "none"), feature = "sim_latency")))]
    {
        sd::read_sectors(lba, count, buf).await
    }
}

/// Writes `count` sectors from `buf` to `lba`, holding [`SD_BUS`]; the write counterpart of
/// [`locked_read_sectors`].
///
/// # Errors
///
/// As for [`locked_read_sectors`].
pub async fn locked_write_sectors(lba: u32, count: u32, buf: &[u8]) -> Result<(), sd::SdError> {
    let _guard = acquire_bus().await?;
    #[cfg(not(target_os = "none"))]
    stats::note_write();
    #[cfg(all(not(target_os = "none"), feature = "sim_latency"))]
    {
        sim_latency::modeled_write(lba, count, buf).await
    }
    #[cfg(not(all(not(target_os = "none"), feature = "sim_latency")))]
    {
        sd::write_sectors(lba, count, buf).await
    }
}

/// DSTATUS bits for the current card state.
#[cfg(target_os = "none")]
fn status_bits() -> u8 {
    // Log the raw card state on the first query, so "No SD card present" can be traced to
    // card-detect, readiness or an init failure.
    {
        static DIAG_LEFT: AtomicBool = AtomicBool::new(true);
        if DIAG_LEFT.swap(false, Ordering::Relaxed) {
            log::info!(
                "DIAG sd: status_bits — inserted={} ready={} wp={}",
                sd::is_inserted(),
                sd::is_ready(),
                sd::is_write_protected(),
            );
        }
    }
    if !sd::is_ready() {
        // Only trust the card-detect read once the controller has been brought up.
        let absent = CONTROLLER_UP.load(Ordering::Acquire) && !sd::is_inserted();
        return STA_NOINIT | if absent { STA_NODISK } else { 0 };
    }
    if sd::is_write_protected() {
        STA_PROTECT
    } else {
        0
    }
}

/// Returns whether the card in `unit` is present and initialised.
///
/// Stricter than card-detect alone; the app's `initSD` needs full readiness for the mount anyway.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_block_ready(unit: u8) -> bool {
    if unit != 0 {
        return false;
    }
    status_bits() & (STA_NOINIT | STA_NODISK) == 0
}

/// Returns whether `unit` is the SD unit; the host disk image is always ready.
#[cfg(not(target_os = "none"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_block_ready(unit: u8) -> bool {
    unit == 0
}

/// Reads `count` sectors starting at `sector` into `dst`, blocking until done.
///
/// Nothing in the app calls this on device (see the module doc); it serializes on [`SD_BUS`] like
/// every other transfer.
///
/// # Safety
///
/// `dst` must be valid for writes of `count * 512` bytes.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_block_read(
    unit: u8,
    dst: *mut u8,
    sector: u32,
    count: u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::BlockRead);
    if unit != 0 || !sd::is_ready() {
        return DELUGE_ERR_NODEV;
    }
    let len = count as usize * SECTOR_SIZE;
    // SAFETY: caller guarantees `dst` holds `count` sectors.
    let out = unsafe { core::slice::from_raw_parts_mut(dst, len) };
    let fut = locked_read_sectors(sector, count, out);
    match block_on(fut) {
        Ok(()) => DELUGE_OK,
        Err(_) => DELUGE_ERR_IO,
    }
}

/// Writes `count` sectors from `src` starting at `sector`, blocking until done.
///
/// # Safety
///
/// `src` must be valid for reads of `count * 512` bytes.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_block_write(
    unit: u8,
    src: *const u8,
    sector: u32,
    count: u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::BlockWrite);
    if unit != 0 || !sd::is_ready() {
        return DELUGE_ERR_NODEV;
    }
    if sd::is_write_protected() {
        return DELUGE_ERR_WRITE_PROTECTED;
    }
    let len = count as usize * SECTOR_SIZE;
    // SAFETY: caller guarantees `src` holds `count` sectors.
    let data = unsafe { core::slice::from_raw_parts(src, len) };
    let fut = locked_write_sectors(sector, count, data);
    match block_on(fut) {
        Ok(()) => DELUGE_OK,
        Err(e) => {
            log::warn!("deluge_block_write err {e:?} (sector={sector} count={count})");
            DELUGE_ERR_IO
        }
    }
}

/// Host counters of SD block reads and writes, for the streaming harnesses.
///
/// Each transfer is counted exactly once: in [`locked_read_sectors`]/[`locked_write_sectors`], or
/// in the `sim_latency` `deluge_block_*` variants, which bypass them.
#[cfg(not(target_os = "none"))]
pub(crate) mod stats {
    use core::sync::atomic::{AtomicU64, Ordering};

    static READS: AtomicU64 = AtomicU64::new(0);
    static WRITES: AtomicU64 = AtomicU64::new(0);

    pub(crate) fn note_read() {
        READS.fetch_add(1, Ordering::Relaxed);
    }

    pub(crate) fn note_write() {
        WRITES.fetch_add(1, Ordering::Relaxed);
    }

    /// Returns the total number of SD block reads.
    pub fn reads() -> u64 {
        READS.load(Ordering::Relaxed)
    }

    /// Returns the total number of SD block writes.
    #[cfg(all(feature = "host-runtime", not(feature = "sim")))]
    pub fn writes() -> u64 {
        WRITES.load(Ordering::Relaxed)
    }

    /// Returns [`reads`] to C++ scenarios.
    #[unsafe(no_mangle)]
    pub extern "C" fn deluge_sim_card_reads() -> u64 {
        reads()
    }
}

/// Reads `count` sectors starting at `sector` into `dst` from the host disk image.
///
/// Does not gate on `sd::is_ready()`: the image opens lazily on first access, and the bootstrap
/// round-trip in `runtime::host` calls this before any bring-up.
///
/// # Safety
///
/// `dst` must be valid for writes of `count * 512` bytes.
#[cfg(all(not(target_os = "none"), not(feature = "sim_latency")))]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_block_read(
    unit: u8,
    dst: *mut u8,
    sector: u32,
    count: u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::BlockRead);
    if unit != 0 {
        return DELUGE_ERR_NODEV;
    }
    let len = count as usize * SECTOR_SIZE;
    // SAFETY: caller guarantees `dst` holds `count` sectors.
    let out = unsafe { core::slice::from_raw_parts_mut(dst, len) };
    match block_on(locked_read_sectors(sector, count, out)) {
        Ok(()) => DELUGE_OK,
        Err(e) => {
            log::warn!("deluge_block_read(host) err {e:?} (sector={sector} count={count})");
            DELUGE_ERR_IO
        }
    }
}

/// Writes `count` sectors from `src` starting at `sector` to the host disk image.
///
/// # Safety
///
/// `src` must be valid for reads of `count * 512` bytes.
#[cfg(all(not(target_os = "none"), not(feature = "sim_latency")))]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_block_write(
    unit: u8,
    src: *const u8,
    sector: u32,
    count: u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::BlockWrite);
    if unit != 0 {
        return DELUGE_ERR_NODEV;
    }
    if sd::is_write_protected() {
        return DELUGE_ERR_WRITE_PROTECTED;
    }
    let len = count as usize * SECTOR_SIZE;
    // SAFETY: caller guarantees `src` holds `count` sectors.
    let data = unsafe { core::slice::from_raw_parts(src, len) };
    match block_on(locked_write_sectors(sector, count, data)) {
        Ok(()) => DELUGE_OK,
        Err(e) => {
            log::warn!("deluge_block_write(host) err {e:?} (sector={sector} count={count})");
            DELUGE_ERR_IO
        }
    }
}

/// Reads `count` sectors starting at `sector` into `dst` from the host disk image, after the
/// modeled SD latency.
///
/// Blocks through [`crate::storage::sim_block::block_on`], whose progress hook lets
/// [`sim_latency::pump`] complete the modeled delay.
///
/// # Safety
///
/// `dst` must be valid for writes of `count * 512` bytes.
#[cfg(all(not(target_os = "none"), feature = "sim_latency"))]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_block_read(
    unit: u8,
    dst: *mut u8,
    sector: u32,
    count: u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::BlockRead);
    if unit != 0 {
        return DELUGE_ERR_NODEV;
    }
    stats::note_read();
    let len = count as usize * SECTOR_SIZE;
    // SAFETY: caller guarantees `dst` holds `count` sectors.
    let out = unsafe { core::slice::from_raw_parts_mut(dst, len) };
    // Takes SD_BUS itself rather than going through `locked_read_sectors`, so the transfer is
    // counted and modeled exactly once.
    let result = crate::storage::sim_block::block_on(async {
        let _guard = SD_BUS.lock().await;
        sim_latency::modeled_read(sector, count, out).await
    });
    match result {
        Ok(()) => DELUGE_OK,
        Err(e) => {
            log::warn!(
                "deluge_block_read(host, sim_latency) err {e:?} (sector={sector} count={count})"
            );
            DELUGE_ERR_IO
        }
    }
}

/// Writes `count` sectors from `src` starting at `sector` to the host disk image, after the
/// modeled SD latency.
///
/// # Safety
///
/// `src` must be valid for reads of `count * 512` bytes.
#[cfg(all(not(target_os = "none"), feature = "sim_latency"))]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_block_write(
    unit: u8,
    src: *const u8,
    sector: u32,
    count: u32,
) -> DelugeStatus {
    crate::tier_meter::note(crate::tier_meter::Entry::BlockWrite);
    if unit != 0 {
        return DELUGE_ERR_NODEV;
    }
    if sd::is_write_protected() {
        return DELUGE_ERR_WRITE_PROTECTED;
    }
    stats::note_write();
    let len = count as usize * SECTOR_SIZE;
    // SAFETY: caller guarantees `src` holds `count` sectors.
    let data = unsafe { core::slice::from_raw_parts(src, len) };
    // As in `deluge_block_read`: takes SD_BUS itself so the transfer is counted and modeled once.
    let result = crate::storage::sim_block::block_on(async {
        let _guard = SD_BUS.lock().await;
        sim_latency::modeled_write(sector, count, data).await
    });
    match result {
        Ok(()) => DELUGE_OK,
        Err(e) => {
            log::warn!(
                "deluge_block_write(host, sim_latency) err {e:?} (sector={sector} count={count})"
            );
            DELUGE_ERR_IO
        }
    }
}

/// Modeled SD latency for host transfers (`sim_latency` feature, harnesses only).
///
/// The delay cannot be a `Timer` awaited inside the transfer future: transfers are driven by a
/// `block_on`, whose synthetic waker makes the integrated timer queue panic. Instead
/// [`pump`](sim_latency::pump), a spawned Embassy task, awaits the `Timer`; `delay` hands it the
/// duration over a `Signal` and waits on an `AtomicWaker`, which any waker may poll.
///
/// Every modeled transfer holds [`SD_BUS`], so at most one is in flight and a single
/// request/completion slot suffices.
#[cfg(all(not(target_os = "none"), feature = "sim_latency"))]
pub mod sim_latency {
    use core::future::Future;
    use core::pin::Pin;
    use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};
    use core::task::{Context, Poll};

    use deluge_bsp::sd;
    use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
    use embassy_sync::signal::Signal;
    use embassy_sync::waitqueue::AtomicWaker;
    use embassy_time::{Duration, Timer};

    use super::SECTOR_SIZE;

    /// Modeled sustained throughput, bytes per second; set at runtime by harness sweeps.
    static THROUGHPUT_BYTES_PER_SEC: AtomicU32 = AtomicU32::new(20_000_000);
    /// Modeled fixed per-command overhead, microseconds.
    static COMMAND_OVERHEAD_US: AtomicU32 = AtomicU32::new(500);

    /// Sets the modeled throughput in bytes per second, clamped to at least 1.
    pub fn set_throughput_bytes_per_sec(bytes_per_sec: u32) {
        THROUGHPUT_BYTES_PER_SEC.store(bytes_per_sec.max(1), Ordering::Relaxed);
    }

    /// Sets the modeled per-command overhead in microseconds.
    pub fn set_command_overhead_us(us: u32) {
        COMMAND_OVERHEAD_US.store(us, Ordering::Relaxed);
    }

    /// Returns the modeled latency of a `bytes`-byte transfer: command overhead plus
    /// `bytes / throughput`.
    pub fn latency_for(bytes: usize) -> Duration {
        let throughput_bps = THROUGHPUT_BYTES_PER_SEC.load(Ordering::Relaxed) as u64;
        let overhead_us = COMMAND_OVERHEAD_US.load(Ordering::Relaxed) as u64;
        let transfer_us = (bytes as u64 * 1_000_000) / throughput_bps;
        Duration::from_micros(overhead_us + transfer_us)
    }

    /// The in-flight transfer's delay, handed to [`pump`].
    static REQUEST: Signal<CriticalSectionRawMutex, Duration> = Signal::new();
    /// Set by [`pump`] when the current delay has elapsed; consumed by [`DelayFuture`].
    static DONE: AtomicBool = AtomicBool::new(false);
    /// Woken by [`pump`] on completion; registered by [`DelayFuture`].
    static WAKER: AtomicWaker = AtomicWaker::new();

    /// Serves modeled delays one at a time; the only place this module awaits a `Timer`.
    ///
    /// Must be spawned once, before the first `sim_latency` transfer.
    #[embassy_executor::task]
    pub async fn pump() {
        loop {
            let dur = REQUEST.wait().await;
            Timer::after(dur).await;
            DONE.store(true, Ordering::SeqCst);
            WAKER.wake();
        }
    }

    /// Completion of the current delay; safe to poll from any waker.
    ///
    /// Registers with [`WAKER`] before checking [`DONE`], so a completion between the two is not
    /// lost.
    struct DelayFuture;
    impl Future for DelayFuture {
        type Output = ();
        fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<()> {
            WAKER.register(cx.waker());
            if DONE.swap(false, Ordering::SeqCst) {
                Poll::Ready(())
            } else {
                Poll::Pending
            }
        }
    }

    /// Waits out the modeled latency of a `bytes`-byte transfer.
    async fn delay(bytes: usize) {
        REQUEST.signal(latency_for(bytes));
        DelayFuture.await;
    }

    /// Reads from the host disk image after the modeled delay; only the timing is modeled.
    ///
    /// # Errors
    ///
    /// Returns the driver's error.
    pub async fn modeled_read(lba: u32, count: u32, buf: &mut [u8]) -> Result<(), sd::SdError> {
        delay(count as usize * SECTOR_SIZE).await;
        sd::read_sectors(lba, count, buf).await
    }

    /// Writes to the host disk image after the modeled delay.
    ///
    /// # Errors
    ///
    /// Returns the driver's error.
    pub async fn modeled_write(lba: u32, count: u32, buf: &[u8]) -> Result<(), sd::SdError> {
        delay(count as usize * SECTOR_SIZE).await;
        sd::write_sectors(lba, count, buf).await
    }
}

/// Returns the SD card's block-device unit number.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_block_sd_unit() -> u8 {
    0
}

/// Returns the next card insertion/ejection event, one per call.
///
/// Reads the SDHI's card-detect edge latches rather than comparing card-present levels between
/// polls: a card swapped between two polls reads present both times, so a level comparison misses
/// the swap entirely.
///
/// A removal reports before an insertion. When both latches are set (a swap), this returns
/// `CARD_EJECTED` now and `CARD_INSERTED` on the next call: only the app's `setCardEjected()` arms
/// the re-mount in `AudioFileManager::reinitEjectedCard`.
///
/// A removal runs `sd::invalidate()` so no transfer targets the departed card; an insertion asks
/// `card_service` to identify the new one.
#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_block_poll_card_event(unit: u8) -> DelugeCardEvent {
    if unit != 0 {
        return CARD_NONE;
    }
    // An insertion latched but not yet reported, e.g. the second half of a swap.
    static INSERTION_PENDING: AtomicBool = AtomicBool::new(false);

    let (removed, inserted) = sd::take_card_detect_events();
    if inserted {
        INSERTION_PENDING.store(true, Ordering::Relaxed);
    }

    if removed {
        // Before the report, so the app never acts on the ejection while the driver would still
        // accept a transfer against the old card.
        sd::invalidate();
        log::info!("sd: card removed");
        return CARD_EJECTED;
    }

    // Level backstop for a lost removal edge: `sd::init()` consumes pending edges as it starts, so
    // a card pulled during an init can leave `is_ready()` true with nothing latched.
    if sd::is_ready() && !sd::is_inserted() {
        sd::invalidate();
        log::info!("sd: card absent (level check)");
        return CARD_EJECTED;
    }

    // Report an insertion only if the card is still in the slot; the pending flag can outlive a
    // card pulled straight back out.
    if sd::is_inserted() && INSERTION_PENDING.swap(false, Ordering::Relaxed) {
        // The app observes `card_service`'s completion as `deluge_block_ready()` turning true.
        CARD_INIT_REQ.signal(());
        log::info!("sd: card inserted; re-init requested");
        return CARD_INSERTED;
    }

    CARD_NONE
}

/// Returns the next card insertion/ejection event by comparing card-present levels between polls.
///
/// The host image is always present, so this never reports an event; it is a comparison so a
/// harness that models ejection has a place to hook in.
#[cfg(not(target_os = "none"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_block_poll_card_event(unit: u8) -> DelugeCardEvent {
    if unit != 0 {
        return CARD_NONE;
    }
    static INITIALISED: AtomicBool = AtomicBool::new(false);
    static LAST_INSERTED: AtomicBool = AtomicBool::new(false);
    let now = sd::is_inserted();
    if !INITIALISED.swap(true, Ordering::Relaxed) {
        LAST_INSERTED.store(now, Ordering::Relaxed);
        return CARD_NONE;
    }
    let was = LAST_INSERTED.swap(now, Ordering::Relaxed);
    if now == was {
        CARD_NONE
    } else if now {
        CARD_INSERTED
    } else {
        CARD_EJECTED
    }
}

abi_pin!(
    deluge_block_poll_card_event,
    deluge_block_read,
    deluge_block_ready,
    deluge_block_sd_unit,
    deluge_block_write
);
