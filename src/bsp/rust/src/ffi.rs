//! No-op stubs for the libdeluge service functions this BSP does not implement.
//!
//! Each logs its first call. The implemented functions live with their hardware: `audio_io.h` in
//! `audio::device`, `block_device.h` in `storage::sd`, `control_surface.h` in
//! `peripherals::control`, `display.h` in `peripherals::display`, and `cv_gate.h`, `midi_io.h`,
//! `flash.h` and `signals.h` in the `peripherals` modules of those names.
#![allow(unused_variables, non_snake_case)]
use crate::sys::*;
use core::sync::atomic::{AtomicBool, Ordering};

macro_rules! stub_log {
    ($n:literal) => {{
        static L: AtomicBool = AtomicBool::new(false);
        if !L.swap(true, Ordering::Relaxed) {
            log::info!(concat!("stub: ", $n));
        }
    }};
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_init() {
    stub_log!("deluge_control_init");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_wait_for_flush() {
    stub_log!("deluge_control_wait_for_flush");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_setup_for_pads() {
    stub_log!("deluge_control_setup_for_pads");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_enable_oled() {
    stub_log!("deluge_control_enable_oled");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_poll_resume() -> bool {
    stub_log!("deluge_control_poll_resume");
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flash_pad(idx: u8) {
    stub_log!("deluge_control_flash_pad");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flash_pad_colour(idx: u8, colour_idx: i32) {
    stub_log!("deluge_control_flash_pad_colour");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_dimmer_interval(interval: u8) {
    stub_log!("deluge_control_set_dimmer_interval");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_write_seven_segment(digits: *const u8, count: u8) -> DelugeStatus {
    stub_log!("deluge_display_write_seven_segment");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_consume_transfer_ack() -> bool {
    stub_log!("deluge_display_consume_transfer_ack");
    false
}

abi_pin!(
    deluge_control_enable_oled,
    deluge_control_flash_pad,
    deluge_control_flash_pad_colour,
    deluge_control_init,
    deluge_control_poll_resume,
    deluge_control_set_dimmer_interval,
    deluge_control_setup_for_pads,
    deluge_control_wait_for_flush,
    deluge_display_consume_transfer_ack,
    deluge_display_write_seven_segment,
);
