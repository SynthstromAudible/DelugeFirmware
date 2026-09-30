//! `libdeluge/flash.h` — the persistent settings sector of the SPI boot flash.
//!
//! Offsets are relative to the settings region. Reads are memory-mapped (XIP); erase and program
//! go through `deluge_bsp::flash::MAP`, which refuses ranges outside the writable windows.
//! Command-mode access is safe because the code runs from SRAM, not XIP.
//!
//! The app's settings live at `flash::DELUGE_SETTINGS_OFFSET` (0x7F000, where the original
//! firmware keeps them), not the app-loader's `flash::SETTINGS_OFFSET`: sharing a sector makes
//! each clobber the other on every boot, forcing a factory reset.
//!
//! On host the region is an in-memory 64 KiB NOR model.

use core::ffi::c_void;

#[cfg(target_os = "none")]
use deluge_bsp::flash;
#[cfg(target_os = "none")]
use rza1l_hal::spibsc;

/// Offset from the cached flash window (0x1800_0000) to its uncached mirror (0x5800_0000).
///
/// Settings reads must use the mirror: a write goes through SPIBSC command mode and never
/// invalidates the CPU caches, so a cached read after a write returns stale settings (and the app
/// factory-resets).
#[cfg(target_os = "none")]
const UNCACHED_FLASH_MIRROR: u32 = 0x4000_0000; // 0x1800_0000 (cached) → 0x5800_0000
#[cfg(target_os = "none")]
const SETTINGS_XIP_BASE: u32 =
    spibsc::SPI_FLASH_BASE + flash::DELUGE_SETTINGS_OFFSET + UNCACHED_FLASH_MIRROR;

/// Size of the host settings store: one 64 KiB sector, NOR-erased (0xFF) at start, so the app's
/// settings round-trip as on hardware.
#[cfg(not(target_os = "none"))]
const HOST_SETTINGS_LEN: usize = 64 * 1024;
#[cfg(not(target_os = "none"))]
static HOST_SETTINGS: std::sync::Mutex<[u8; HOST_SETTINGS_LEN]> =
    std::sync::Mutex::new([0xFF; HOST_SETTINGS_LEN]);

#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_read(offset: u32, dst: *mut c_void, len: u32) {
    // SAFETY: reads the settings sector through the uncached mirror; the app passes a `len`-byte
    // dst.
    unsafe {
        core::ptr::copy_nonoverlapping(
            (SETTINGS_XIP_BASE + offset) as *const u8,
            dst as *mut u8,
            len as usize,
        );
    }
}

/// Host: copies `len` bytes at `offset` from the in-memory store, clamped to its bounds (an
/// out-of-range read copies nothing).
#[cfg(not(target_os = "none"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_read(offset: u32, dst: *mut c_void, len: u32) {
    let store = HOST_SETTINGS.lock().unwrap();
    let start = (offset as usize).min(HOST_SETTINGS_LEN);
    let end = start.saturating_add(len as usize).min(HOST_SETTINGS_LEN);
    let n = end.saturating_sub(start);
    // SAFETY: the app passes a `len`-byte dst; we copy the in-bounds prefix.
    unsafe {
        core::ptr::copy_nonoverlapping(store[start..end].as_ptr(), dst as *mut u8, n);
    }
}

#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_erase(offset: u32) {
    log::info!(
        "flash: erase settings off={offset:#x} (abs={:#x})",
        flash::DELUGE_SETTINGS_OFFSET + offset
    );
    // SAFETY: MAP guards the writable window, and no code executes from SPI flash.
    unsafe { flash::MAP.erase_sector(flash::DELUGE_SETTINGS_OFFSET + offset) };
}

/// Host: erases the whole store to 0xFF. It is one sector, so any `offset` erases all of it, as a
/// device sector erase does.
#[cfg(not(target_os = "none"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_erase(offset: u32) {
    let mut store = HOST_SETTINGS.lock().unwrap();
    store.fill(0xFF);
    log::info!("flash(host): erase settings off={offset:#x} (whole sector → 0xFF)");
}

#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_program(offset: u32, src: *const c_void, len: u32) {
    // SAFETY: the app passes a `len`-byte src and erased the sector first; MAP guards the
    // writable window.
    let data = unsafe { core::slice::from_raw_parts(src as *const u8, len as usize) };
    log::info!(
        "flash: program settings off={offset:#x} len={len} first8={:02x?}",
        &data[..data.len().min(8)]
    );
    unsafe { flash::MAP.program(flash::DELUGE_SETTINGS_OFFSET + offset, data) };
    // Diagnostic: log the read-back through both windows.
    let cached = spibsc::SPI_FLASH_BASE + flash::DELUGE_SETTINGS_OFFSET + offset;
    let uncached = cached + UNCACHED_FLASH_MIRROR;
    let rb_cached =
        unsafe { core::slice::from_raw_parts(cached as *const u8, len.min(8) as usize) };
    let rb_uncached =
        unsafe { core::slice::from_raw_parts(uncached as *const u8, len.min(8) as usize) };
    log::info!("flash: readback cached={rb_cached:02x?} uncached={rb_uncached:02x?}");
}

/// Host: programs `len` bytes from `src` at `offset`, clamped to the store's bounds.
#[cfg(not(target_os = "none"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_program(offset: u32, src: *const c_void, len: u32) {
    let data = unsafe { core::slice::from_raw_parts(src as *const u8, len as usize) };
    let mut store = HOST_SETTINGS.lock().unwrap();
    let start = (offset as usize).min(HOST_SETTINGS_LEN);
    let end = start.saturating_add(data.len()).min(HOST_SETTINGS_LEN);
    let n = end.saturating_sub(start);
    // NOR program clears bits (AND), matching hardware: program only after erase.
    for (dst, s) in store[start..end].iter_mut().zip(&data[..n]) {
        *dst &= *s;
    }
    log::info!(
        "flash(host): program settings off={offset:#x} len={len} first8={:02x?}",
        &data[..data.len().min(8)]
    );
}

/// Tests the host flash model: erased reads are 0xFF, program round-trips, out-of-range access
/// clamps.
#[cfg(all(test, not(target_os = "none")))]
mod host_tests {
    use super::*;

    #[test]
    fn host_flash_program_then_read_round_trips() {
        let off = 0x40u32;
        let src = [0xDEu8, 0xAD, 0xBE, 0xEF];
        deluge_flash_erase(off);
        deluge_flash_program(off, src.as_ptr() as *const c_void, src.len() as u32);
        let mut dst = [0u8; 4];
        deluge_flash_read(off, dst.as_mut_ptr() as *mut c_void, dst.len() as u32);
        assert_eq!(dst, src, "host flash store did not round-trip program→read");
    }

    #[test]
    fn host_flash_read_of_erased_region_is_0xff() {
        let off = 0x80u32;
        deluge_flash_erase(off);
        let mut dst = [0u8; 4];
        deluge_flash_read(off, dst.as_mut_ptr() as *mut c_void, dst.len() as u32);
        assert_eq!(dst, [0xFF; 4], "erased NOR flash must read back 0xFF");
    }

    #[test]
    fn host_flash_read_out_of_range_offset_is_a_safe_noop() {
        // An offset past the store must not panic; dst is left untouched.
        let mut dst = [0xAAu8; 4];
        deluge_flash_read(65_537, dst.as_mut_ptr() as *mut c_void, dst.len() as u32);
        assert_eq!(
            dst, [0xAA; 4],
            "out-of-range read must copy nothing, not panic"
        );
    }
}

abi_pin!(deluge_flash_erase, deluge_flash_program, deluge_flash_read);
