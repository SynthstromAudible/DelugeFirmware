//! Host-only no-op stubs for the peripheral C ABI (`audio_io.h`, `midi_io.h`,
//! `cv_gate.h`, `signals.h`) under `host_app`.
//!
//! These close the last of the host `deluge_app` object closure's undefined
//! symbols that the real (device-only) `audio`/`cv_gate`/`midi`/`signals`
//! modules provide on-target. Boot-and-idle never drives real peripheral I/O on
//! host (no SSI/DMA rings, no PIC-shared RSPI0, no USB stack), so inert defaults
//! are correct here. The exceptions are DIN input, which reads the note script a
//! scenario arms (`audio_host.rs`), DIN MIDI in and out, which the desktop
//! simulator's MIDI queues carry under `sim`, and audio capture, which
//! `audio_host.rs` owns.
//!
//! Return values are chosen wherever the boot path might actually read one, so a
//! "wrong zero" can't silently wedge boot — e.g. `deluge_audio_drive` returning 0
//! (no renders needed) is the documented idle case, not a failure.
//! `deluge_midi_port_count` returns 0: with zero ports the app's `0..port_count`
//! loops never touch MIDI I/O at all, the most inert option, and nothing on the
//! boot-and-idle path depends on a DIN port existing.
#![allow(unused_variables, non_snake_case)]
use crate::sys::{
    DelugeMidiPort, DelugeSignal, DelugeStatus, DelugeStatus_DELUGE_OK as DELUGE_OK,
    DelugeUsbHostEvent, DelugeUsbHostEvent_DELUGE_USB_HOST_NONE as USB_HOST_NONE,
};

/// midi_io.h's `DELUGE_MIDI_DIN`: the port the app writes DIN MIDI to.
#[cfg(feature = "sim")]
const DIN_PORT: DelugeMidiPort = 0;

macro_rules! stub_log {
    ($n:literal) => {{
        static L: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);
        if !L.swap(true, core::sync::atomic::Ordering::Relaxed) {
            log::info!(concat!("host stub: ", $n));
        }
    }};
}

// ── audio_io.h ─────────────────────────────────────────────────────────────
// `deluge_audio_drive`/`max_block_frames`/`sample_rate`/`frames_until_block_offset`
// are real (see `audio_host.rs`) — the priority-0 task actually renders via
// `deluge_app_render` instead of a no-op. The remaining audio_io.h symbols
// below (start/input_resync/stamp_to_render_offset) have no host equivalent
// (no DMA ring to (re-)anchor or resync) and stay inert stubs.

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_start() -> DelugeStatus {
    stub_log!("deluge_audio_start");
    DELUGE_OK
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_input_resync() {
    stub_log!("deluge_audio_input_resync");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_stamp_to_render_offset(stamp: u32) -> u32 {
    stub_log!("deluge_audio_stamp_to_render_offset");
    0
}

// ── cv_gate.h ──────────────────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_init(display_shares_spi: bool) {
    stub_log!("deluge_cv_init");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_set(channel: u8, value: u16) {
    stub_log!("deluge_cv_set");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_sent_count() -> u32 {
    stub_log!("deluge_cv_sent_count");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_init() {
    stub_log!("deluge_gate_init");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_set(channel: u8, on: bool) {
    stub_log!("deluge_gate_set");
}

// ── midi_io.h (+ signals.h's deluge_midi_gate_timer_pending) ────────────────

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_init() {
    stub_log!("deluge_midi_init");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_port_count() -> u8 {
    stub_log!("deluge_midi_port_count");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_port(controller: u8, device: u8) -> DelugeMidiPort {
    stub_log!("deluge_midi_usb_port");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_read_timed(
    port: DelugeMidiPort,
    dst: *mut u8,
    max: u32,
    arrival_ticks: *mut u32,
) -> u32 {
    stub_log!("deluge_midi_read_timed");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write(port: DelugeMidiPort, src: *const u8, len: u32) -> u32 {
    #[cfg(feature = "sim")]
    if let (DIN_PORT, Some(panel)) = (port, deluge_bsp::sim::panel()) {
        // SAFETY: the app passes `len` valid bytes.
        panel.push_midi_out(unsafe { core::slice::from_raw_parts(src, len as usize) });
        return len;
    }
    stub_log!("deluge_midi_write");
    // Always "accept" (silently dropped) rather than
    // reporting 0 accepted, so a caller retrying on partial acceptance can't
    // spin forever against a port that will never drain.
    len
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write_space(port: DelugeMidiPort) -> u32 {
    stub_log!("deluge_midi_write_space");
    256
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write_pending(port: DelugeMidiPort) -> u32 {
    stub_log!("deluge_midi_write_pending");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_din_read_timed(byte: *mut u8, arrival_ticks: *mut u32) -> bool {
    // DIN input is a note script a scenario arms (audio_host.rs), else the simulator's MIDI input (`sim`); nothing
    // arrives without either.
    let script = crate::audio_host::din_script_read();
    #[cfg(feature = "sim")]
    let script = script.or_else(|| deluge_bsp::sim::panel().and_then(|panel| panel.pop_midi_in()));
    let Some(b) = script else {
        return false;
    };
    // SAFETY: the app passes valid out-pointers (arrival_ticks may be null), as for the device's midi.rs.
    unsafe {
        *byte = b;
        if !arrival_ticks.is_null() {
            *arrival_ticks = embassy_time::Instant::now().as_ticks() as u32;
        }
    }
    true
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_flush(port: DelugeMidiPort) {
    stub_log!("deluge_midi_flush");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_service() {
    stub_log!("deluge_midi_service");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_is_host() -> bool {
    stub_log!("deluge_midi_usb_is_host");
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_peripheral_connected() -> bool {
    stub_log!("deluge_midi_usb_peripheral_connected");
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_poll_usb_host_event() -> DelugeUsbHostEvent {
    stub_log!("deluge_midi_poll_usb_host_event");
    USB_HOST_NONE
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_pending() -> bool {
    stub_log!("deluge_midi_gate_timer_pending");
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_arm(samples_from_now: u32) {
    stub_log!("deluge_midi_gate_timer_arm");
    // No MTU2 (or any) one-shot timer on host.
    // Reachable via `deluge_audio_drive`'s (audio_host.rs) render path, which
    // can hit AudioEngine::scheduleMidiGateOutISR and arm the gate timer.
}

// ── signals.h ──────────────────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_write(signal: DelugeSignal, on: bool) {
    stub_log!("deluge_signal_write");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_read(signal: DelugeSignal) -> bool {
    stub_log!("deluge_signal_read");
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_battery_read_raw(out: *mut u16) -> bool {
    stub_log!("deluge_battery_read_raw");
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_battery_start_conversion() {
    stub_log!("deluge_battery_start_conversion");
}
