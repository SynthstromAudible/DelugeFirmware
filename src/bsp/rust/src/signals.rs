//! signals.h — board GPIO signals, battery sense, and the MIDI/gate timer.
//!
//! `deluge_signal_write`/`read` map `DelugeSignal` to the board's GPIO pins
//! (mirrors src/bsp/rza1/signals.c); the pins are configured in board init.
//! SPEAKER_ENABLE / CODEC_ENABLE gate the audio output path (the app turns the
//! speaker amp on here, so this is what makes rendered audio audible).
//!
//! Battery ADC and the sample-accurate MIDI/gate timer are stubbed for now: the
//! app runs without them — no battery readout, and gate/MIDI events fire at the
//! app's tick granularity rather than sample-accurately. [refine later]
#![allow(non_upper_case_globals)]

use core::sync::atomic::{AtomicBool, Ordering};

use rza1l_hal::{gpio, mtu2};

use crate::sys::{
    DelugeSignal, DelugeSignal_DELUGE_SIGNAL_BATTERY_LED as BATTERY_LED,
    DelugeSignal_DELUGE_SIGNAL_CODEC_ENABLE as CODEC_ENABLE,
    DelugeSignal_DELUGE_SIGNAL_HEADPHONE_DETECT as HEADPHONE_DETECT,
    DelugeSignal_DELUGE_SIGNAL_LINE_IN_DETECT as LINE_IN_DETECT,
    DelugeSignal_DELUGE_SIGNAL_LINE_OUT_DETECT_L as LINE_OUT_DETECT_L,
    DelugeSignal_DELUGE_SIGNAL_LINE_OUT_DETECT_R as LINE_OUT_DETECT_R,
    DelugeSignal_DELUGE_SIGNAL_MIC_DETECT as MIC_DETECT,
    DelugeSignal_DELUGE_SIGNAL_SPEAKER_ENABLE as SPEAKER_ENABLE,
    DelugeSignal_DELUGE_SIGNAL_SYNC_LED as SYNC_LED,
};

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_write(signal: DelugeSignal, on: bool) {
    // SAFETY: GPIO writes to board-owned output pins (configured in board init).
    unsafe {
        match signal {
            SYNC_LED => gpio::write(6, 7, on),
            BATTERY_LED => gpio::write(1, 1, !on), // open-drain: 0 = LED on
            SPEAKER_ENABLE => gpio::write(4, 1, on),
            CODEC_ENABLE => gpio::write(6, 12, on),
            _ => {}
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_read(signal: DelugeSignal) -> bool {
    // SAFETY: GPIO reads of board-owned input pins (configured in board init).
    unsafe {
        match signal {
            HEADPHONE_DETECT => gpio::read_pin(6, 5),
            LINE_IN_DETECT => gpio::read_pin(6, 6),
            MIC_DETECT => gpio::read_pin(7, 9),
            LINE_OUT_DETECT_L => gpio::read_pin(6, 3),
            LINE_OUT_DETECT_R => gpio::read_pin(6, 4),
            _ => false,
        }
    }
}

// ── Battery sense (ADC channel 5, via deluge_bsp::battery) ─────────────────────

#[unsafe(no_mangle)]
pub extern "C" fn deluge_battery_read_raw(out: *mut u16) -> bool {
    match deluge_bsp::battery::read_raw() {
        Some(v) => {
            // SAFETY: the app passes a valid u16 out-pointer.
            unsafe { *out = v };
            true
        }
        None => false,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_battery_start_conversion() {
    deluge_bsp::battery::start_conversion();
}

// ── Sample-accurate MIDI/gate timer (MTU2 one-shot) ───────────────────────────
//
// The app schedules MIDI-output / gate-edge events that must fire at a precise
// sample offset inside an audio render window — finer than the app tick. We use
// MTU2 channel 2 (rza1's TIMER_MIDI_GATE_OUTPUT) as a one-shot: deluge_bsp's own
// `midi_gate` module owns the same channel for pure-Rust firmwares, but its ISR
// de-asserts a gate GPIO directly; here the C++ app's `midiAndGateTimerGoneOff()`
// owns the action (MIDI + gate emission), and deluge-bsp must not reference app
// symbols, so we drive mtu2 directly and call the app from our ISR — matching
// rza1 (src/bsp/rza1/signals.c), where the handler is also ISR-driven.

/// MTU2 channel for the MIDI/gate one-shot (rza1 TIMER_MIDI_GATE_OUTPUT = 2).
const GATE_TIMER_CH: u8 = 2;
/// Prescaler P0/64 ≈ 520 kHz; 1 tick ≈ 1.92 µs, max one-shot ≈ 126 ms.
const GATE_TIMER_PRESCALER: u16 = 64;
/// GIC priority (matches deluge_bsp::midi_gate: above USB so gate timing isn't
/// delayed behind USB servicing).
const GATE_TIMER_PRIORITY: u8 = 5;

/// True while a one-shot is armed and not yet fired. The app polls this via
/// deluge_midi_gate_timer_pending() and only re-arms when it reads false.
static GATE_TIMER_PENDING: AtomicBool = AtomicBool::new(false);

unsafe extern "C" {
    /// The app's MIDI/gate routine: emits the events whose scheduled sample
    /// offset has now arrived. Declared `[isr]` by libdeluge (signals.h); we call
    /// it from the MTU2 compare-match ISR, matching rza1.
    fn midiAndGateTimerGoneOff();
}

/// MTU2 ch2 compare-match-A ISR. Clears the flag, disarms (one-shot), clears the
/// pending flag, then runs the app handler (which may re-arm for the next event).
fn gate_timer_isr() {
    // SAFETY: only this ISR + the arm path touch ch2 after setup; clear before
    // disarm so the GIC line can't re-trigger.
    unsafe {
        mtu2::clear_flag(GATE_TIMER_CH);
        mtu2::disarm(GATE_TIMER_CH);
    }
    GATE_TIMER_PENDING.store(false, Ordering::Release);
    // SAFETY: app-provided C symbol; libdeluge documents this as an ISR callback.
    unsafe { midiAndGateTimerGoneOff() };
}

/// One-time MTU2 ch2 setup. Call in the boot setup window after gic::init
/// (init_clocks) and mtu2::enable_write(), before interrupts are unmasked.
///
/// # Safety
/// Writes MTU2 + GIC registers; call once during single-threaded startup.
pub(crate) unsafe fn gate_timer_setup() {
    unsafe { mtu2::setup_one_shot(GATE_TIMER_CH, GATE_TIMER_PRESCALER, gate_timer_isr, GATE_TIMER_PRIORITY) };
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_pending() -> bool {
    GATE_TIMER_PENDING.load(Ordering::Acquire)
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_arm(samples_from_now: u32) {
    // samples → MTU2 ticks, mirroring rza1 (src/bsp/rza1/signals.c:99):
    // ticks = samples * 766245 / 2^16  (≈ samples * 515616 / 44100). u64 math
    // avoids the u32 overflow rza1 would hit past ~5600 samples; results match
    // for the small offsets the app uses. TGRA is 16-bit, so it saturates at the
    // ~126 ms one-shot ceiling.
    let ticks = (((samples_from_now as u64) * 766245) >> 16).min(u16::MAX as u64) as u16;
    GATE_TIMER_PENDING.store(true, Ordering::Release);
    // SAFETY: ch2 was configured by gate_timer_setup() at boot.
    unsafe { mtu2::arm(GATE_TIMER_CH, ticks) };
}
