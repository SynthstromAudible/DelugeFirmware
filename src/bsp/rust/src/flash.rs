//! flash.h — small persistent settings flash (SPI boot-flash region).
//!
//! A reserved 64 KB sector of the board's SPI boot flash (the deluge_bsp::flash
//! SETTINGS window) survives power-off and holds device settings. Offsets are
//! relative to that region. Reads are XIP — the flash is memory-mapped at
//! `spibsc::SPI_FLASH_BASE`; erase/program go through `deluge_bsp::flash::MAP`,
//! which refuses any range outside the board's writable windows. Safe to
//! command-mode the flash here because our code runs from SRAM, not XIP.
//!
//! The Deluge app's settings live at the CANONICAL `flash::DELUGE_SETTINGS_OFFSET`
//! (0x7F000) — the same physical address the original rza1 firmware uses — NOT the
//! app-loader's own `flash::SETTINGS_OFFSET` (0x3C0000). The app and the app-loader
//! must keep their settings in separate sectors; sharing one made each clobber the
//! other every boot (→ perpetual factory reset). Settings are now BSP-independent.

use core::ffi::c_void;

use deluge_bsp::flash;
use rza1l_hal::spibsc;

/// Memory-mapped (XIP) base of the settings region, for reads — the **uncached**
/// mirror (0x5800_0000), NOT the cached window (0x1800_0000). The flash *write*
/// path runs the SPIBSC in command mode, bypassing the CPU cache, and only flushes
/// the SPIBSC read cache — it never invalidates the CPU L1/L2 cache. Reading
/// settings through the cached window after a write therefore returns STALE data,
/// so the app sees old/invalid settings and keeps triggering a factory reset.
/// Reading through the uncached mirror is always fresh (settings reads are rare, so
/// the lost caching is irrelevant).
const UNCACHED_FLASH_MIRROR: u32 = 0x4000_0000; // 0x1800_0000 (cached) → 0x5800_0000
const SETTINGS_XIP_BASE: u32 =
    spibsc::SPI_FLASH_BASE + flash::DELUGE_SETTINGS_OFFSET + UNCACHED_FLASH_MIRROR;

#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_read(offset: u32, dst: *mut c_void, len: u32) {
    // SAFETY: reads the settings sector via the uncached flash mirror (always sees
    // freshly-programmed data). The app passes a `len`-byte dst.
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
    log::info!(
        "flash: erase settings off={offset:#x} (abs={:#x})",
        flash::DELUGE_SETTINGS_OFFSET + offset
    );
    // SAFETY: erases the settings sector containing `offset`; MAP guards the
    // writable window, and we never execute from SPI flash (code is in SRAM).
    unsafe { flash::MAP.erase_sector(flash::DELUGE_SETTINGS_OFFSET + offset) };
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_program(offset: u32, src: *const c_void, len: u32) {
    // SAFETY: the app passes a `len`-byte src and erased the sector first; MAP
    // guards the writable window.
    let data = unsafe { core::slice::from_raw_parts(src as *const u8, len as usize) };
    log::info!(
        "flash: program settings off={offset:#x} len={len} first8={:02x?}",
        &data[..data.len().min(8)]
    );
    unsafe { flash::MAP.program(flash::DELUGE_SETTINGS_OFFSET + offset, data) };
    // Read-back verification through both windows to pin down the "overwrite".
    let cached = spibsc::SPI_FLASH_BASE + flash::DELUGE_SETTINGS_OFFSET + offset;
    let uncached = cached + UNCACHED_FLASH_MIRROR;
    let rb_cached = unsafe { core::slice::from_raw_parts(cached as *const u8, len.min(8) as usize) };
    let rb_uncached =
        unsafe { core::slice::from_raw_parts(uncached as *const u8, len.min(8) as usize) };
    log::info!("flash: readback cached={rb_cached:02x?} uncached={rb_uncached:02x?}");
}
