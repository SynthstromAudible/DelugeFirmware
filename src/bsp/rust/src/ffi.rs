//! M0 link-plumbing stubs for the libdeluge C-ABI services the C++ app calls.
//! GENERATED from the bindgen signatures; replaced with real impls per milestone.
#![allow(unused_variables, non_snake_case)]
use crate::sys::*;

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_start() -> DelugeStatus { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_drive() -> u32 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_input_resync() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_frames_until_block_offset(offset_frames: u32, elapsed_frames_out: *mut u32,) -> u32 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_stamp_to_render_offset(stamp: u32) -> u32 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_block_sd_unit() -> u8 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_block_poll_card_event(unit: u8) -> DelugeCardEvent { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_probe_oled() -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_early(have_oled: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_audio() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_init_storage() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_board_unlock_data_cache() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_now() -> u64 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_monotonic() -> u64 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_delay_us(us: u32) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_clock_delay_ms(ms: u32) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_read_boot_info(out: *mut DelugeBootInfo) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_poll_event(out: *mut DelugeInputEvent) -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_led(led: u8, on: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_indicator(which: u8, levels: *const u8, count: u8) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flush() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_init() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_wait_for_flush() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_setup_for_pads() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_enable_oled() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_poll_resume() -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_pad_output_space() -> u32 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_pad_columns(idx: u8, colours: *const DelugeColour, count: u8) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flash_pad(idx: u8) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flash_pad_colour(idx: u8, colour_idx: i32) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_horizontal(flags: u8) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_vertical(up: bool, colours: *const DelugeColour, count: u8) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_row(row: u8, colour: DelugeColour) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_done() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_refresh_time(ms: u8) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_dimmer_interval(interval: u8) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_init(display_shares_spi: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_set(channel: u8, value: u16) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_sent_count() -> u32 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_init() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_set(channel: u8, on: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_blit_oled(pixels: *const u8, width: u16, height: u16) -> DelugeStatus { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_write_seven_segment(digits: *const u8, count: u8) -> DelugeStatus { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_init() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_service() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_timer_event() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_consume_transfer_ack() -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_freeze(pixels: *const u8) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_external_end() -> usize { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_internal_begin() -> usize { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_memory_scratch() -> *mut ::core::ffi::c_void { core::ptr::null_mut() }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_init() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_port_count() -> u8 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_port(controller: u8, device: u8) -> DelugeMidiPort { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_read_timed(port: DelugeMidiPort, dst: *mut u8, max: u32, arrival_ticks: *mut u32,) -> u32 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write(port: DelugeMidiPort, src: *const u8, len: u32) -> u32 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write_space(port: DelugeMidiPort) -> u32 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write_pending(port: DelugeMidiPort) -> u32 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_din_read_timed(byte: *mut u8, arrival_ticks: *mut u32) -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_flush(port: DelugeMidiPort) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_service() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_is_host() -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_peripheral_connected() -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_poll_usb_host_event() -> DelugeUsbHostEvent { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_system_quiesce() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_log(text: *const ::core::ffi::c_char) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_in_interrupt() -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn ENTER_CRITICAL_SECTION() {}

#[unsafe(no_mangle)]
pub extern "C" fn EXIT_CRITICAL_SECTION() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_write(signal: DelugeSignal, on: bool) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_read(signal: DelugeSignal) -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_battery_read_raw(out: *mut u16) -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_battery_start_conversion() {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_pending() -> bool { false }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_arm(samples_from_now: u32) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_encoder_io_init(wake_task: i8) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_encoder_take_edges(encoder: u8) -> i8 { 0 }

#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_read(offset: u32, dst: *mut ::core::ffi::c_void, len: u32) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_erase(offset: u32) {}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_flash_program(offset: u32, src: *const ::core::ffi::c_void, len: u32) {}
