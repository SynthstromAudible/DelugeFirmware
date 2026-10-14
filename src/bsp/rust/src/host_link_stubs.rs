//! The host app's stand-ins for the peripheral C ABI (`audio_io.h`, `midi_io.h`, `cv_gate.h`,
//! `signals.h`), which the device-only peripheral modules provide on the target.
//!
//! The host has no peripherals, so most are inert, with return values chosen so the app idles
//! rather than wedges. The exceptions: DIN input reads the note script a scenario arms
//! (`audio::host`), under `sim` DIN MIDI in and out go through the simulator's queues, and the
//! trigger clock keeps its handler for the harness to fire. `deluge_midi_port_count` returns 0,
//! so the app's per-port loops touch no MIDI I/O.
#![allow(unused_variables, non_snake_case)]
use crate::sys::{
    DelugeMidiPort, DelugeSignal, DelugeStatus, DelugeStatus_DELUGE_OK as DELUGE_OK,
    DelugeUsbHostEvent, DelugeUsbHostEvent_DELUGE_USB_HOST_NONE as USB_HOST_NONE,
};

/// `midi_io.h`'s `DELUGE_MIDI_DIN`: the port the app writes DIN MIDI to.
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
// The rendering half is real, in `audio::host`; these have no DMA ring to act on.

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
    // No DAC to wait for: the write has gone out as soon as it is made.
    CV_SENT_COUNT.fetch_add(1, core::sync::atomic::Ordering::Relaxed);
}

/// How many CV writes have gone out: every one, at once, on the host.
static CV_SENT_COUNT: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_sent_count() -> u32 {
    stub_log!("deluge_cv_sent_count");
    CV_SENT_COUNT.load(core::sync::atomic::Ordering::Relaxed)
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_init() {
    stub_log!("deluge_gate_init");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_set(channel: u8, on: bool) {
    stub_log!("deluge_gate_set");
}

/// The app's trigger-clock handler, or 0 for none.
static TRIGGER_HANDLER: core::sync::atomic::AtomicUsize = core::sync::atomic::AtomicUsize::new(0);

#[unsafe(no_mangle)]
pub extern "C" fn deluge_trigger_clock_set_handler(handler: crate::sys::DelugeTriggerClockHandler) {
    TRIGGER_HANDLER.store(
        handler.map_or(0, |f| f as usize),
        core::sync::atomic::Ordering::Relaxed,
    );
}

/// Delivers one trigger-clock edge to the installed handler, for host harnesses.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_trigger_clock_host_edge(stamp: u64) {
    let h = TRIGGER_HANDLER.load(core::sync::atomic::Ordering::Relaxed);
    if h != 0 {
        // SAFETY: h was stored from a valid DelugeTriggerClockHandler by the setter above.
        let f: unsafe extern "C" fn(u64) = unsafe { core::mem::transmute(h) };
        // SAFETY: the app's handler takes any stamp.
        unsafe { f(stamp) };
    }
}

// ── midi_io.h (+ signals.h's MIDI/gate output timer) ──────────────────────────

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
    // Accept and drop, so a caller retrying on partial acceptance cannot spin forever.
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
pub extern "C" fn deluge_midi_din_read_timed(byte: *mut u8, arrival_stamp: *mut u32) -> bool {
    // A scenario's note script, else (under `sim`) the simulator's MIDI input.
    let script = crate::audio::host::din_script_read();
    #[cfg(feature = "sim")]
    let script = script.or_else(|| deluge_bsp::sim::panel().and_then(|panel| panel.pop_midi_in()));
    let Some(b) = script else {
        return false;
    };
    // SAFETY: the app passes valid out-pointers; `arrival_stamp` may be null.
    unsafe {
        *byte = b;
        if !arrival_stamp.is_null() {
            *arrival_stamp = crate::audio::capture_stamp::now_for(0);
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

/// The output timer's armed deadline on the `deluge_clock_now()` clock, or [`TIMER_DISARMED`].
static GATE_TIMER_DEADLINE: core::sync::atomic::AtomicU64 =
    core::sync::atomic::AtomicU64::new(TIMER_DISARMED);
const TIMER_DISARMED: u64 = u64::MAX;

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_arm_at(deadline_us: u64) {
    // A deadline of u64::MAX would read as disarmed; it is unreachable anyway.
    GATE_TIMER_DEADLINE.store(
        deadline_us.min(TIMER_DISARMED - 1),
        core::sync::atomic::Ordering::Relaxed,
    );
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_cancel() {
    GATE_TIMER_DEADLINE.store(TIMER_DISARMED, core::sync::atomic::Ordering::Relaxed);
}

/// The host's stand-in for the output timer's interrupt: fires `midiAndGateTimerGoneOff()` once
/// the armed deadline has passed. The render driver calls it after each block, on the render
/// thread, so the app sees it at block granularity rather than mid-block.
pub(crate) fn fire_midi_gate_timer_if_due() {
    use core::sync::atomic::Ordering;
    let deadline = GATE_TIMER_DEADLINE.load(Ordering::Relaxed);
    if deadline == TIMER_DISARMED || embassy_time::Instant::now().as_micros() < deadline {
        return;
    }
    // One-shot: disarmed before the handler runs, which arms again if anything is still to come.
    GATE_TIMER_DEADLINE.store(TIMER_DISARMED, Ordering::Relaxed);
    // SAFETY: an app-provided C symbol that `libdeluge/signals.h` documents as the timer callback.
    unsafe { crate::sys::midiAndGateTimerGoneOff() };
}

// ── signals.h ──────────────────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_write(signal: DelugeSignal, on: bool) {
    stub_log!("deluge_signal_write");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_read(signal: DelugeSignal) -> bool {
    stub_log!("deluge_signal_read");
    // Headphones are the one input a harness plugs in (`audio::host::deluge_host_headphones`).
    signal == crate::sys::DelugeSignal_DELUGE_SIGNAL_HEADPHONE_DETECT
        && crate::audio::host::headphones()
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

abi_pin!(
    deluge_audio_input_resync,
    deluge_audio_stamp_to_render_offset,
    deluge_audio_start,
    deluge_battery_read_raw,
    deluge_battery_start_conversion,
    deluge_cv_init,
    deluge_cv_sent_count,
    deluge_cv_set,
    deluge_gate_init,
    deluge_gate_set,
    deluge_midi_din_read_timed,
    deluge_midi_flush,
    deluge_midi_gate_timer_arm_at,
    deluge_midi_gate_timer_cancel,
    deluge_midi_init,
    deluge_midi_poll_usb_host_event,
    deluge_midi_port_count,
    deluge_midi_read_timed,
    deluge_midi_service,
    deluge_midi_usb_is_host,
    deluge_midi_usb_peripheral_connected,
    deluge_midi_usb_port,
    deluge_midi_write,
    deluge_midi_write_pending,
    deluge_midi_write_space,
    deluge_signal_read,
    deluge_signal_write,
    deluge_trigger_clock_host_edge,
    deluge_trigger_clock_set_handler,
);
