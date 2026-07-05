//! block_device.h + FatFS diskio — SD card over `deluge_bsp::sd`.
//!
//! The app's FatFS layer calls the BSP-provided `disk_*_without_streaming_first`
//! (its `disk_read`/`disk_write` shims live in audio_file_manager.cpp), plus
//! `disk_initialize`/`disk_status`/`disk_ioctl`/`get_fattime`. We back those with
//! deluge_bsp::sd's async SDHI+DMA driver via `block_on` — SD ops complete on the
//! SDHI/DMA-completion IRQ, so `block_on` drives them to completion without
//! needing another task to run.
//!
//! NOTE: `block_on` stalls the executor (and the app tick → audio) for the
//! duration of a transfer. Fine for bring-up (loads aren't real-time); audio-
//! during-storage yielding (storage_wait.h / scheduler) is a later refinement.
#![allow(non_upper_case_globals)]

use core::sync::atomic::{AtomicBool, Ordering};

use deluge_bsp::sd;
use embassy_futures::block_on;

use crate::sys::{
    DelugeCardEvent, DelugeCardEvent_DELUGE_CARD_EVENT_EJECTED as CARD_EJECTED,
    DelugeCardEvent_DELUGE_CARD_EVENT_INSERTED as CARD_INSERTED,
    DelugeCardEvent_DELUGE_CARD_EVENT_NONE as CARD_NONE,
};

// FatFS diskio status/result codes (src/fatfs/diskio.h).
const STA_NOINIT: u8 = 0x01;
const STA_NODISK: u8 = 0x02;
const STA_PROTECT: u8 = 0x04;
const RES_OK: i32 = 0;
const RES_ERROR: i32 = 1;
const RES_WRPRT: i32 = 2;
const RES_NOTRDY: i32 = 3;
const RES_PARERR: i32 = 4;
const CTRL_SYNC: u8 = 0;
const GET_SECTOR_COUNT: u8 = 1;
const GET_SECTOR_SIZE: u8 = 2;
const GET_BLOCK_SIZE: u8 = 3;
const SECTOR_SIZE: usize = 512;

/// Set once `disk_initialize` has driven `sd::init()` at least once. Until then,
/// the controller has never muxed `SD_CD` (P7_0 → SDHI fn 3) or started `SD_CLK`,
/// so `sd::is_inserted()` reads a meaningless card-detect line. We must NOT report
/// `STA_NODISK` from that pre-init read: FatFS skips `disk_initialize` whenever the
/// disk looks absent, and `disk_initialize` is the only path that brings the detect
/// circuit to life — detection would be gated on init, and init on detection
/// (deadlock). Report "not initialized, presence unknown" instead so FatFS attempts
/// the init that makes card-detect valid.
static CONTROLLER_UP: AtomicBool = AtomicBool::new(false);

/// Bring the SD controller up from an async (Embassy task) context, once at boot.
///
/// `sd::init()` sequences card power-up with `embassy_time::Timer` delays. This BSP
/// uses the *integrated* (intrusive) timer queue, so a `Timer` only resolves when
/// polled by a real Embassy task — `embassy_futures::block_on`'s synthetic waker
/// makes `Timer` panic (`from_embassy_waker`). The sync FatFS `disk_initialize`
/// path can therefore never run `init()`. Instead we init here, from `app_task`,
/// before `deluge_app_init` drives the first C++ storage access: afterwards the card
/// reports ready, FatFS skips `disk_initialize`, and reads/writes stay on `block_on`
/// (pure SDHI/DMA-IRQ futures — no timers). Re-runnable for card-swap from async.
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

/// FatFS DSTATUS bits for the current card state.
fn status_bits() -> u8 {
    // DIAG (SD bring-up): log the raw card-detect/ready state the first few times
    // status is queried, so "No SD card present" can be traced to is_inserted()
    // vs. is_ready() vs. init failure.
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

#[unsafe(no_mangle)]
pub extern "C" fn disk_initialize(pdrv: u8) -> u8 {
    if pdrv != 0 {
        return STA_NOINIT;
    }
    // sd::init() can't run here: its embassy-time Timers panic under `block_on`
    // (integrated timer queue — see `boot_init`). It runs eagerly from `app_task`
    // (boot_init) after the PIC baud handshake (pic::wait_ready) and before
    // deluge_app_init — waiting for the PIC first avoids the handshake race that
    // corrupted the pads. So the card is ready by the time the app first calls this;
    // if somehow not, just report status (never block_on(init) here).
    if !sd::is_ready() {
        log::warn!("sd: disk_initialize before async init done");
    }
    status_bits()
}

#[unsafe(no_mangle)]
pub extern "C" fn disk_status(pdrv: u8) -> u8 {
    if pdrv != 0 {
        return STA_NOINIT;
    }
    status_bits()
}

#[unsafe(no_mangle)]
pub extern "C" fn disk_ioctl(pdrv: u8, cmd: u8, buff: *mut core::ffi::c_void) -> i32 {
    if pdrv != 0 {
        return RES_NOTRDY;
    }
    match cmd {
        // Writes complete synchronously (block_on), so nothing is pending.
        CTRL_SYNC => RES_OK,
        GET_SECTOR_COUNT => {
            if buff.is_null() {
                return RES_PARERR;
            }
            unsafe { *(buff as *mut u32) = sd::total_sectors() };
            RES_OK
        }
        GET_SECTOR_SIZE => {
            if buff.is_null() {
                return RES_PARERR;
            }
            unsafe { *(buff as *mut u16) = SECTOR_SIZE as u16 };
            RES_OK
        }
        GET_BLOCK_SIZE => {
            if buff.is_null() {
                return RES_PARERR;
            }
            unsafe { *(buff as *mut u32) = 1 };
            RES_OK
        }
        _ => RES_PARERR,
    }
}

// NOTE: SD I/O must use `block_on` (which parks the executor for
// the transfer), NOT a fiber-yielding drive. FatFS is not re-entrant and the app's
// SD-reentrancy guard (`currentlyAccessingCard`) is only set by the legacy C diskio
// (src/RZA1/diskio.c), which this BSP does not link — so on this BSP it is always 0.
// Parking during the transfer is what serializes SD access; yielding mid-transfer
// (block_on_fiber) let other tasks re-enter FatFS and corrupted it (manifested as
// "NO MORE PRESETS FOUND" on track create, and would also break song/sample loads).
// Re-introducing fiber-aware SD requires first serializing all SD access on this BSP.
#[unsafe(no_mangle)]
pub extern "C" fn disk_read_without_streaming_first(
    pdrv: u8,
    buff: *mut u8,
    sector: u32,
    count: u32,
) -> i32 {
    if pdrv != 0 || !sd::is_ready() {
        return RES_NOTRDY;
    }
    let len = count as usize * SECTOR_SIZE;
    // SAFETY: FatFS guarantees `buff` holds `count` sectors.
    let dst = unsafe { core::slice::from_raw_parts_mut(buff, len) };
    match block_on(sd::read_sectors(sector, count, dst)) {
        Ok(()) => RES_OK,
        Err(_) => RES_ERROR,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn disk_write_without_streaming_first(
    pdrv: u8,
    buff: *const u8,
    sector: u32,
    count: u32,
) -> i32 {
    if pdrv != 0 || !sd::is_ready() {
        return RES_NOTRDY;
    }
    if sd::is_write_protected() {
        return RES_WRPRT;
    }
    let len = count as usize * SECTOR_SIZE;
    // SAFETY: FatFS guarantees `buff` holds `count` sectors.
    let src = unsafe { core::slice::from_raw_parts(buff, len) };
    match block_on(sd::write_sectors(sector, count, src)) {
        Ok(()) => RES_OK,
        Err(e) => {
            // Surface the precise SdError (the C++ side only logs a generic "SD card
            // write error"). Helps diagnose the first-exercised write path.
            log::warn!("disk_write err {e:?} (sector={sector} count={count})");
            RES_ERROR
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn disk_timerproc() {
    // SD is fully event-driven (SDHI/DMA IRQs); no periodic timer work needed.
}

#[unsafe(no_mangle)]
pub extern "C" fn get_fattime() -> u32 {
    // 2024-01-01 00:00:00, FAT-packed. No RTC on this BSP yet.
    ((2024u32 - 1980) << 25) | (1 << 21) | (1 << 16)
}

// ── block_device.h: SD unit + card-detect ─────────────────────────────────────

#[unsafe(no_mangle)]
pub extern "C" fn deluge_block_sd_unit() -> u8 {
    0
}

/// Pull-based card-detect: report INSERTED/EJECTED edges of the card-present
/// state. The first poll just latches the boot state (no spurious event), so the
/// app's initial card read isn't double-triggered (mirrors the rza1 latch).
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
