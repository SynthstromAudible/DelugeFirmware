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

#[cfg(target_os = "none")]
use deluge_bsp::flash;
#[cfg(target_os = "none")]
use rza1l_hal::spibsc;

/// Memory-mapped (XIP) base of the settings region, for reads — the **uncached**
/// mirror (0x5800_0000), NOT the cached window (0x1800_0000). The flash *write*
/// path runs the SPIBSC in command mode, bypassing the CPU cache, and only flushes
/// the SPIBSC read cache — it never invalidates the CPU L1/L2 cache. Reading
/// settings through the cached window after a write therefore returns STALE data,
/// so the app sees old/invalid settings and keeps triggering a factory reset.
/// Reading through the uncached mirror is always fresh (settings reads are rare, so
/// the lost caching is irrelevant).
#[cfg(target_os = "none")]
const UNCACHED_FLASH_MIRROR: u32 = 0x4000_0000; // 0x1800_0000 (cached) → 0x5800_0000
#[cfg(target_os = "none")]
const SETTINGS_XIP_BASE: u32 =
    spibsc::SPI_FLASH_BASE + flash::DELUGE_SETTINGS_OFFSET + UNCACHED_FLASH_MIRROR;

/// Host in-memory settings store: a single 64 KiB sector, NOR-erased (0xFF) at
/// start. Mirrors the device SETTINGS window byte-for-byte at the C-ABI boundary
/// so the app's settings load/save round-trips exactly as on hardware, with no
/// SPIBSC/XIP hardware involved. Offsets are relative to the settings region, as
/// on device.
#[cfg(not(target_os = "none"))]
const HOST_SETTINGS_LEN: usize = 64 * 1024;
#[cfg(not(target_os = "none"))]
static HOST_SETTINGS: std::sync::Mutex<[u8; HOST_SETTINGS_LEN]> =
    std::sync::Mutex::new([0xFF; HOST_SETTINGS_LEN]);

#[cfg(target_os = "none")]
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

/// Host-sim implementation of `deluge_flash_read`: copies `len` bytes from the
/// in-memory settings store at `offset`. Both are clamped to the store's bounds,
/// so an out-of-range read copies nothing rather than panicking.
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
    // SAFETY: erases the settings sector containing `offset`; MAP guards the
    // writable window, and we never execute from SPI flash (code is in SRAM).
    unsafe { flash::MAP.erase_sector(flash::DELUGE_SETTINGS_OFFSET + offset) };
}

/// Host-sim implementation of `deluge_flash_erase`: resets the whole in-memory
/// settings store to 0xFF (NOR-erased state). The store is a single 64 KiB
/// sector, so any `offset` within it erases the entire region — matching device
/// sector-erase semantics, where the settings region *is* the sector.
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
    let rb_cached =
        unsafe { core::slice::from_raw_parts(cached as *const u8, len.min(8) as usize) };
    let rb_uncached =
        unsafe { core::slice::from_raw_parts(uncached as *const u8, len.min(8) as usize) };
    log::info!("flash: readback cached={rb_cached:02x?} uncached={rb_uncached:02x?}");
}

/// Host-sim implementation of `deluge_flash_program`: programs `len` bytes from
/// `src` into the in-memory settings store at `offset`, clamped to the store's
/// bounds.
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

/// Round-trip and edge-case coverage for the host in-memory flash model's
/// contract: NOR-erased (0xFF) initial/post-erase state, AND-semantics program,
/// and bounds clamping on out-of-range access.
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
        // An offset past the store must NOT panic; dst is left untouched.
        let mut dst = [0xAAu8; 4];
        deluge_flash_read(65_537, dst.as_mut_ptr() as *mut c_void, dst.len() as u32);
        assert_eq!(
            dst, [0xAA; 4],
            "out-of-range read must copy nothing, not panic"
        );
    }
}

abi_pin!(deluge_flash_erase, deluge_flash_program, deluge_flash_read);
