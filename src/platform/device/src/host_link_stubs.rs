//! The host app's stand-ins for the peripheral C ABI (`audio_io.h`, `midi_io.h`, `cv_gate.h`,
//! `signals.h`, `usb_audio.h`), which the device-only peripheral modules provide on the target.
//!
//! The host has no peripherals, so most are inert, with return values chosen so the app idles
//! rather than wedges. The exceptions: DIN input reads the note script a scenario arms
//! (`audio::host`), under `sim` DIN MIDI in and out go through the simulator's queues, DIN out
//! with no simulator panel is kept for host specs to take (`deluge_host_midi_out_take`), and the
//! trigger clock keeps its handler for the harness to fire. `deluge_midi_port_count` returns 0,
//! so the app's per-port loops touch no MIDI I/O.
#![allow(unused_variables, non_snake_case)]
use crate::sys::{
    DelugeMidiPort, DelugeSignal, DelugeStatus, DelugeStatus_DELUGE_OK as DELUGE_OK,
    DelugeStereoSample, DelugeUsbHostEvent,
    DelugeUsbHostEvent_DELUGE_USB_HOST_NONE as USB_HOST_NONE,
};

/// `midi_io.h`'s `DELUGE_MIDI_DIN`: the port the app writes DIN MIDI to.
const DIN_PORT: DelugeMidiPort = 0;

/// DIN bytes written while no simulator panel takes them (host specs), oldest first; drained by
/// `deluge_host_midi_out_take`.
static HOST_MIDI_OUT: std::sync::Mutex<Vec<u8>> = std::sync::Mutex::new(Vec::new());

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

/// No play position on the host: every output goes as soon as possible.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_render_offset_to_stamp(offset_frames: u32) -> u32 {
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

/// Sends DIN bytes on the host: to the simulator's MIDI out under `sim` when a panel is up, else
/// kept for specs to take.
pub(crate) fn host_din_write(bytes: &[u8]) {
    #[cfg(feature = "sim")]
    if let Some(panel) = crate::sim_panel::panel() {
        panel.push_midi_out(bytes);
        return;
    }
    HOST_MIDI_OUT
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .extend_from_slice(bytes);
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write(port: DelugeMidiPort, src: *const u8, len: u32) -> u32 {
    stub_log!("deluge_midi_write");
    if port == DIN_PORT {
        // SAFETY: the app passes `len` valid bytes.
        host_din_write(unsafe { core::slice::from_raw_parts(src, len as usize) });
    }
    // Accept all, so a caller retrying on partial acceptance cannot spin forever.
    len
}

/// Moves up to `cap` of the kept DIN bytes into `dst`, oldest first; returns how many it moved.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_midi_out_take(dst: *mut u8, cap: u32) -> u32 {
    let mut kept = HOST_MIDI_OUT.lock().unwrap_or_else(|e| e.into_inner());
    let n = kept.len().min(cap as usize);
    // SAFETY: the caller passes `cap` writable bytes.
    unsafe { core::ptr::copy_nonoverlapping(kept.as_ptr(), dst, n) };
    kept.drain(..n);
    n as u32
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
    // A scenario's note script, then a UI script's queued input, else (under `sim`) the simulator's MIDI input.
    let script =
        crate::audio::host::din_script_read().or_else(crate::peripherals::ui_harness::midi_in_pop);
    #[cfg(feature = "sim")]
    let script = script.or_else(|| crate::sim_panel::panel().and_then(|panel| panel.pop_midi_in()));
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

// ── usb_audio.h ────────────────────────────────────────────────────────────
// No USB device: starting it does nothing, and the audio function is absent.

#[unsafe(no_mangle)]
pub extern "C" fn deluge_usb_device_start(with_audio: bool) {
    stub_log!("deluge_usb_device_start");
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_usb_audio_present() -> bool {
    false
}

abi_pin!(
    deluge_audio_input_resync,
    deluge_audio_stamp_to_render_offset,
    deluge_audio_render_offset_to_stamp,
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
    deluge_usb_audio_present,
    deluge_usb_device_start,
);
