//! The rig harness: lets a host drive the real Deluge with level-3 UI scripts and see what its panel
//! shows, over the debug probe's RTT channel. Built only with the `rig-harness` feature; the pure
//! parts compile everywhere so the host tests can drive them.
//!
//! The app's panel writes are recorded at the libdeluge API, where `ui_harness` records them on
//! host builds, so both harnesses see the same thing.

pub mod shadow;

#[cfg(all(feature = "rig-harness", target_os = "none"))]
mod device {
    use core::cell::RefCell;

    use critical_section::Mutex;

    use super::shadow::{COLUMN_BYTES, Shadow};

    // In SDRAM: the SRAM heap has little headroom over its floor. `.sdram_bss` is zeroed at boot,
    // which is `Shadow::new()`.
    #[unsafe(link_section = ".sdram_bss")]
    static SHADOW: Mutex<RefCell<Shadow>> = Mutex::new(RefCell::new(Shadow::new()));

    pub(crate) fn with<R>(f: impl FnOnce(&mut Shadow) -> R) -> R {
        critical_section::with(|cs| f(&mut SHADOW.borrow_ref_mut(cs)))
    }

    pub(crate) fn record_oled(pixels: &[u8], width: u16, height: u16) {
        with(|s| s.record_oled(pixels, width, height));
    }

    pub(crate) fn record_columns(idx: u8, rgb: &[[u8; 3]; 16]) {
        let mut flat = [0u8; COLUMN_BYTES];
        flat.copy_from_slice(rgb.as_flattened());
        with(|s| s.record_columns(idx, &flat));
    }

    pub(crate) fn record_led(id: u8, on: bool) {
        with(|s| s.record_led(id, on));
    }

    pub(crate) fn record_ring(knob: u8, levels: [u8; 4]) {
        with(|s| s.record_ring(knob, levels));
    }

    pub(crate) fn record_flash(idx: u8, colour: u8) {
        with(|s| s.record_flash(idx, colour));
    }

    pub(crate) fn record_pic(dimmer: Option<u8>, refresh: Option<u8>) {
        with(|s| s.record_pic(dimmer, refresh));
    }

    pub(crate) fn record_scroll() {
        with(|s| s.record_scroll());
    }
}

#[cfg(all(feature = "rig-harness", target_os = "none"))]
pub(crate) use device::*;
