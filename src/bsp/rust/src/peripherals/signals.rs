//! `libdeluge/signals.h` — board GPIO signals, battery sense, and the MIDI/gate timer.
//!
//! [`deluge_signal_write`] and [`deluge_signal_read`] map `DelugeSignal` to the GPIO pins board
//! init configured. `SPEAKER_ENABLE` and `CODEC_ENABLE` gate the audio output path: rendered audio
//! is inaudible until the app turns them on.
#![allow(non_upper_case_globals)]

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

// Battery sense (ADC channel 5, via `deluge_bsp::battery`)

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

// Sample-accurate MIDI/gate timer (MTU2 channel 2 one-shot)
//
// The app keeps its timed MIDI and gate output in a queue and arms this one-shot for the earliest
// deadline, on the `deluge_clock_now()` clock. The one-shot's ISR calls the app's
// `midiAndGateTimerGoneOff()`, which sends what is due and arms again. `deluge_bsp::midi_gate`
// drives the same channel for pure-Rust firmware, but deluge-bsp must not reference app symbols,
// so this module drives `mtu2` directly.

/// MTU2 channel for the MIDI/gate one-shot (the C firmware's `TIMER_MIDI_GATE_OUTPUT`).
const GATE_TIMER_CH: u8 = 2;
/// Prescaler P0/64 ≈ 516 kHz: 1 tick ≈ 1.94 µs, longest one-shot ≈ 127 ms.
const GATE_TIMER_PRESCALER: u16 = 64;
/// Timer ticks per second at [`GATE_TIMER_PRESCALER`]: P0 (33 MHz) / 64.
const GATE_TIMER_TICKS_PER_SEC: u64 = 515_616;
/// GIC priority, above USB so gate timing is not delayed behind USB servicing.
const GATE_TIMER_PRIORITY: u8 = 5;

/// MTU2 channel 2 compare-match-A ISR: clears and disarms the timer, then runs the app handler
/// (which arms again if anything is still to come).
fn gate_timer_isr() {
    // SAFETY: only this ISR and the arm/cancel paths touch channel 2 after setup. Clearing
    // before disarming keeps the GIC line from re-triggering.
    unsafe {
        mtu2::clear_flag(GATE_TIMER_CH);
        mtu2::disarm(GATE_TIMER_CH);
    }
    // SAFETY: an app-provided C symbol that `libdeluge/signals.h` documents as an ISR callback.
    unsafe { crate::sys::midiAndGateTimerGoneOff() };
}

/// One-time setup of MTU2 channel 2.
///
/// # Safety
///
/// Writes MTU2 and GIC registers: call once during single-threaded startup, after `gic::init` and
/// `mtu2::enable_write()`, before interrupts are unmasked.
pub(crate) unsafe fn gate_timer_setup() {
    unsafe {
        mtu2::setup_one_shot(
            GATE_TIMER_CH,
            GATE_TIMER_PRESCALER,
            gate_timer_isr,
            GATE_TIMER_PRIORITY,
        )
    };
}

/// Timer ticks until `deadline_us`, at least one, saturating at the 16-bit compare register: a
/// deadline beyond it fires early and the app arms again.
fn ticks_until(deadline_us: u64, now_us: u64) -> u16 {
    let us = deadline_us.saturating_sub(now_us);
    (us.saturating_mul(GATE_TIMER_TICKS_PER_SEC) / 1_000_000).clamp(1, u16::MAX as u64) as u16
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_arm_at(deadline_us: u64) {
    critical_section::with(|_| {
        let ticks = ticks_until(deadline_us, embassy_time::Instant::now().as_micros());
        // SAFETY: ch2 was configured by gate_timer_setup() at boot. `arm` stops the channel and
        // clears its flag before loading the new compare value, so it replaces an armed deadline.
        unsafe { mtu2::arm(GATE_TIMER_CH, ticks) };
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_cancel() {
    critical_section::with(|_| {
        // SAFETY: ch2 was configured by gate_timer_setup() at boot.
        unsafe {
            mtu2::disarm(GATE_TIMER_CH);
            mtu2::clear_flag(GATE_TIMER_CH);
        }
    });
}

abi_pin!(
    deluge_battery_read_raw,
    deluge_battery_start_conversion,
    deluge_midi_gate_timer_arm_at,
    deluge_midi_gate_timer_cancel,
    deluge_signal_read,
    deluge_signal_write,
);
