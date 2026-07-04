//! Link-plumbing stubs for the still-unimplemented libdeluge services.
//! Each logs its FIRST call (stub_log!) so we can see which boundary call the
//! C++ boot wedges on. Replaced with real impls as they land.
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

// audio_io.h (deluge_audio_start/stop/drive/max_block_frames/sample_rate/
// output_latency_frames/input_resync/frames_until_block_offset/
// stamp_to_render_offset) is implemented in `audio` (SSI ring driver).

// deluge_block_sd_unit / deluge_block_poll_card_event + the FatFS disk_* diskio
// are implemented in `sd` (SD card over deluge_bsp::sd).

// deluge_control_read_boot_info is implemented in `control` (latches the PIC
// firmware-version byte; factory_reset_requested is definitively false). The old
// stub left the out-struct UNINITIALIZED → garbage factory_reset → spurious resets.

// deluge_control_poll_event is implemented in `control` (PIC pump + event queue).

// deluge_control_set_led is implemented in `control` (PIC output queue).

// deluge_control_set_indicator is implemented in `control` (PIC output queue).

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

// deluge_control_pad_output_space is implemented in `control` (queue capacity).

// deluge_control_set_pad_columns is implemented in `control` (PIC output queue).

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flash_pad(idx: u8) {
    stub_log!("deluge_control_flash_pad");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flash_pad_colour(idx: u8, colour_idx: i32) {
    stub_log!("deluge_control_flash_pad_colour");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_horizontal(flags: u8) {
    stub_log!("deluge_control_scroll_horizontal");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_vertical(
    up: bool,
    colours: *const DelugeColour,
    count: u8,
) {
    stub_log!("deluge_control_scroll_vertical");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_row(row: u8, colour: DelugeColour) {
    stub_log!("deluge_control_scroll_row");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_done() {
    stub_log!("deluge_control_scroll_done");
}

// deluge_control_set_refresh_time is implemented in `control` (PIC output queue).

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_dimmer_interval(interval: u8) {
    stub_log!("deluge_control_set_dimmer_interval");
}

// cv_gate.h (deluge_cv_init/set/sent_count, deluge_gate_init/set,
// deluge_trigger_clock_set_handler) is implemented in `cv_gate`.

// deluge_display_blit_oled / _busy / _init / _service / _timer_event / _freeze
// are implemented in `display` (OLED render task over deluge_bsp::oled).

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

// midi_io.h (deluge_midi_init/port_count/port_kind/usb_port/read[_timed]/write/
// write_space/write_pending/din_read_timed/flush/service/port_connected/
// usb_is_host/usb_peripheral_connected/poll_usb_host_event) is implemented in
// `midi` (DIN over deluge_bsp::uart; USB-MIDI deferred/disconnected).

#[unsafe(no_mangle)]
pub extern "C" fn deluge_system_quiesce() {
    stub_log!("deluge_system_quiesce");
}

// deluge_encoder_take_edges is implemented in `control` (ISR edge counters).

// deluge_flash_read/erase/program are implemented in `flash` (SPI settings flash).

// signals.h (deluge_signal_write/read, deluge_battery_read_raw/start_conversion,
// deluge_midi_gate_timer_pending/arm) is implemented in `signals`.
