//! The `libdeluge/block_device.h` SD-card entry points and the shared SD bus.
//!
//! Both targets drive `deluge_bsp::sd`, which has the same async API on device (the SDHI1+DMA
//! driver, completing on the transfer IRQ) and host (a file-backed disk image whose transfers never
//! suspend).
//!
//! The app's filesystem does not use `deluge_block_read`/`deluge_block_write`: the device and host
//! block devices (`storage::sd_block_device`, `storage::efatfs::host`) await
//! [`locked_read_sectors`]/[`locked_write_sectors`] directly. The C-ABI transfer pair is called
//! only by the plain-host bootstrap round-trip in `runtime::host`. The app does call `deluge_block_ready` (`storage_manager.cpp`'s
//! card-present checks).
//!
//! Each C-ABI symbol has one definition per target configuration, selected by `cfg`.

#![allow(non_upper_case_globals)]

use core::sync::atomic::{AtomicBool, Ordering};

use deluge_bsp::sd;
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
/// Set while [`card_service`] identifies a card, so the not-ready level check in
/// `deluge_block_poll_card_event` doesn't ask for another init meanwhile.
#[cfg(target_os = "none")]
static INIT_IN_FLIGHT: AtomicBool = AtomicBool::new(false);

/// Whether a card the SDK stopped reporting ready should be identified again. A dropped transfer
/// makes the SDK reset the controller and report not ready until `sd::init` runs, and with the card
/// still in the slot no insertion edge comes, so the change decides: ready at the last poll
/// (`was_ready`), inserted and not ready now. Not while an init is already running, which also
/// reports not ready until it finishes. A card that was never ready, or whose init failed, waits
/// for the next insertion edge as before, so an unreadable card isn't retried in a loop.
#[cfg(any(target_os = "none", test))]
pub(crate) fn needs_reinit(inserted: bool, ready: bool, was_ready: bool, in_flight: bool) -> bool {
    inserted && !ready && was_ready && !in_flight
}

#[cfg(target_os = "none")]
#[embassy_executor::task]
pub async fn card_service() {
    loop {
        CARD_INIT_REQ.wait().await;
        INIT_IN_FLIGHT.store(true, Ordering::Release);
        let _guard = SD_BUS.lock().await;
        // Re-check presence under the lock: the card may have been pulled again
        // between the edge and here, in which case there is nothing to identify
        // and the next insertion edge will raise a fresh request.
        if !sd::is_inserted() {
            log::info!("sd: card gone again before re-init; waiting for the next insertion");
            INIT_IN_FLIGHT.store(false, Ordering::Release);
            continue;
        }
        match sd::init().await {
            Ok(()) => log::info!("sd: card re-identified"),
            // Not fatal, and not retried here: an unreadable or half-seated card
            // leaves the app in its "ejected" state, still polling, and any later
            // reseat raises another insertion edge (or, still seated, the level
            // check asks again, at most once a second).
            Err(e) => log::warn!("sd: re-init failed: {:?}", e),
        }
        INIT_IN_FLIGHT.store(false, Ordering::Release);
    }
}

/// Brings the host SD image up before `deluge_app_init`, mirroring the device boot sequence.
///
/// Not strictly required: the host image opens lazily on first access. Idempotent.
#[cfg(feature = "host_app")]
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
///
/// Every caller holds back while the streaming fill waits ([`URGENT_WAITING`]), so the fill waits
/// at most one transfer.
async fn acquire_bus()
-> Result<embassy_sync::mutex::MutexGuard<'static, CriticalSectionRawMutex, ()>, sd::SdError> {
    // A Timer resolves by its own deadline on every poll, so this works under the interaction
    // tier's no-op-waker spin as well as on an executor.
    while URGENT_WAITING.load(Ordering::Acquire) != 0 {
        embassy_time::Timer::after_micros(YIELD_TO_URGENT_US).await;
    }
    let mut lock = core::pin::pin!(SD_BUS.lock());
    core::future::poll_fn(move |cx| {
        if crate::tier::current() == crate::tier::Tier::Interaction && !sd::is_ready() {
            return core::task::Poll::Ready(Err(sd::SdError::NotInitialized));
        }
        core::future::Future::poll(lock.as_mut(), cx).map(Ok)
    })
    .await
}

/// Streaming fills waiting for [`SD_BUS`]; see [`locked_read_sectors_urgent`].
static URGENT_WAITING: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// How often a caller holding back for a waiting fill looks again.
const YIELD_TO_URGENT_US: u64 = 100;

/// How often a waiting fill tries the bus.
const URGENT_POLL_US: u64 = 50;

/// One fill counted in [`URGENT_WAITING`] until it has the bus, or until its future is dropped
/// while waiting.
struct UrgentWait;

impl UrgentWait {
    fn begin() -> Self {
        URGENT_WAITING.fetch_add(1, Ordering::AcqRel);
        Self
    }
}

impl Drop for UrgentWait {
    fn drop(&mut self) {
        URGENT_WAITING.fetch_sub(1, Ordering::AcqRel);
    }
}

/// [`locked_read_sectors`] for the streaming fill, which every other caller holds back for.
/// Returns when it took the bus, for the fill trace, with the read's result.
///
/// It polls for the bus rather than waiting on the mutex: the mutex keeps one waker, and an
/// interaction-tier caller spinning on it displaces the fill's, which then sleeps through the
/// release.
///
/// # Errors
///
/// As for [`locked_read_sectors`].
pub async fn locked_read_sectors_urgent(
    lba: u32,
    count: u32,
    buf: &mut [u8],
) -> (embassy_time::Instant, Result<(), sd::SdError>) {
    let waiting = UrgentWait::begin();
    let guard = loop {
        if let Ok(guard) = SD_BUS.try_lock() {
            break guard;
        }
        embassy_time::Timer::after_micros(URGENT_POLL_US).await;
    };
    drop(waiting);
    let acquired = embassy_time::Instant::now();
    #[cfg(not(target_os = "none"))]
    stats::note_read();
    let result = sd::read_sectors(lba, count, buf).await;
    drop(guard);
    (acquired, result)
}

/// Reads `count` sectors from `lba` into `buf`, holding [`SD_BUS`].
///
/// The transfer chokepoint for the filesystem block devices and the streaming loader.
///
/// # Errors
///
/// Returns `NotInitialized` for an interaction-tier caller while the card is not ready (see
/// `acquire_bus`), or the driver's error.
pub async fn locked_read_sectors(lba: u32, count: u32, buf: &mut [u8]) -> Result<(), sd::SdError> {
    let _guard = acquire_bus().await?;
    #[cfg(not(target_os = "none"))]
    stats::note_read();
    sd::read_sectors(lba, count, buf).await
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
    sd::write_sectors(lba, count, buf).await
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
/// Each transfer is counted once, in [`locked_read_sectors`]/[`locked_write_sectors`].
#[cfg(not(target_os = "none"))]
pub mod stats {
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
#[cfg(not(target_os = "none"))]
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
#[cfg(not(target_os = "none"))]
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

    // Still seated, ready at the last poll and not now, with no edge: the SDK reset the controller
    // after a dropped transfer. Identify it again, quietly: it's the same card, so the app's mount
    // stays valid.
    static WAS_READY: AtomicBool = AtomicBool::new(false);
    let ready = sd::is_ready();
    let was_ready = WAS_READY.swap(ready, Ordering::Relaxed);
    if needs_reinit(
        sd::is_inserted(),
        ready,
        was_ready,
        INIT_IN_FLIGHT.load(Ordering::Acquire),
    ) {
        CARD_INIT_REQ.signal(());
        log::info!("sd: card seated but no longer ready; re-init requested");
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

#[cfg(test)]
mod reinit_tests {
    use super::needs_reinit;

    /// The SDK resets the controller after a dropped transfer and reports the card not ready until
    /// it is identified again; with the card still in the slot no insertion edge comes, so the
    /// level decides.
    #[test]
    fn card_service_reinits_an_inserted_card_that_is_not_ready() {
        // (inserted, ready, was ready at the last poll, init in flight)
        assert!(
            needs_reinit(true, false, true, false),
            "was ready, now not, card still in: re-init"
        );
        assert!(
            !needs_reinit(true, true, true, false),
            "ready: nothing to do"
        );
        assert!(
            !needs_reinit(false, false, true, false),
            "no card: nothing to identify"
        );
        assert!(
            !needs_reinit(true, false, true, true),
            "an init already running"
        );
        // Never ready, or a failed init: wait for the next insertion edge rather than retry forever.
        assert!(
            !needs_reinit(true, false, false, false),
            "not ready before either: no retry loop"
        );
    }
}
