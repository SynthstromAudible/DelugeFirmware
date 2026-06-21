//! flash.h — small persistent settings flash (SPI boot-flash region).
//!
//! A reserved 64 KB sector of the board's SPI boot flash (the deluge_bsp::flash
//! SETTINGS window) survives power-off and holds device settings. Offsets are
//! relative to that region. Reads are XIP — the flash is memory-mapped at
//! `spibsc::SPI_FLASH_BASE`; erase/program go through `deluge_bsp::flash::MAP`,
//! which refuses any range outside the board's writable windows. Safe to
//! command-mode the flash here because our code runs from SRAM, not XIP.
//!
//! NOTE: this is a different physical region than the original rza1 firmware's
//! settings (0x7F000) — a fresh BSP, so settings simply live where this BSP's
//! flash map places them (SETTINGS_OFFSET = 0x3C0000).

use core::ffi::c_void;

use deluge_bsp::flash;
use rza1l_hal::spibsc;

/// Memory-mapped (XIP) base of the settings region, for reads.
const SETTINGS_XIP_BASE: u32 = spibsc::SPI_FLASH_BASE + flash::SETTINGS_OFFSET;

#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_read(offset: u32, dst: *mut c_void, len: u32) {
    // SAFETY: the SPI flash is memory-mapped (XIP) at SPI_FLASH_BASE; the
    // settings sector is in range. The app passes a `len`-byte dst.
    unsafe {
        core::ptr::copy_nonoverlapping(
            (SETTINGS_XIP_BASE + offset) as *const u8,
            dst as *mut u8,
            len as usize,
        );
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_erase(offset: u32) {
    // SAFETY: erases the settings sector containing `offset`; MAP guards the
    // writable window, and we never execute from SPI flash (code is in SRAM).
    unsafe { flash::MAP.erase_sector(flash::SETTINGS_OFFSET + offset) };
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_program(offset: u32, src: *const c_void, len: u32) {
    // SAFETY: the app passes a `len`-byte src and erased the sector first; MAP
    // guards the writable window.
    let data = unsafe { core::slice::from_raw_parts(src as *const u8, len as usize) };
    unsafe { flash::MAP.program(flash::SETTINGS_OFFSET + offset, data) };
}
