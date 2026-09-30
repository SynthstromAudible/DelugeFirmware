//! `libdeluge/signals.h` — board GPIO signals, battery sense, and the MIDI/gate timer.
//!
//! [`deluge_signal_write`] and [`deluge_signal_read`] map `DelugeSignal` to the GPIO pins board
//! init configured. `SPEAKER_ENABLE` and `CODEC_ENABLE` gate the audio output path: rendered audio
//! is inaudible until the app turns them on.
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
// The app schedules MIDI-output and gate-edge events at sample offsets finer than its tick. The
// one-shot's ISR calls the app's `midiAndGateTimerGoneOff()`. `deluge_bsp::midi_gate` drives the
// same channel for pure-Rust firmware, but deluge-bsp must not reference app symbols, so this
// module drives `mtu2` directly.

/// MTU2 channel for the MIDI/gate one-shot (the app's `TIMER_MIDI_GATE_OUTPUT`).
const GATE_TIMER_CH: u8 = 2;
/// Prescaler P0/64 ≈ 520 kHz: 1 tick ≈ 1.92 µs, longest one-shot ≈ 126 ms.
const GATE_TIMER_PRESCALER: u16 = 64;
/// GIC priority, above USB so gate timing is not delayed behind USB servicing.
const GATE_TIMER_PRIORITY: u8 = 5;

/// True while a one-shot is armed and has not fired; the app re-arms only when it reads false.
static GATE_TIMER_PENDING: AtomicBool = AtomicBool::new(false);

/// MTU2 channel 2 compare-match-A ISR: clears and disarms the timer, clears
/// [`GATE_TIMER_PENDING`], then runs the app handler (which may re-arm).
fn gate_timer_isr() {
    // SAFETY: only this ISR and the arm path touch channel 2 after setup. Clearing before
    // disarming keeps the GIC line from re-triggering.
    unsafe {
        mtu2::clear_flag(GATE_TIMER_CH);
        mtu2::disarm(GATE_TIMER_CH);
    }
    GATE_TIMER_PENDING.store(false, Ordering::Release);
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

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_pending() -> bool {
    GATE_TIMER_PENDING.load(Ordering::Acquire)
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_arm(samples_from_now: u32) {
    // ticks = samples × 766245 / 2^16 (≈ samples × 515616 / 44100), in u64 so it cannot overflow.
    // TGRA is 16-bit, so it saturates at the ~126 ms one-shot ceiling.
    let ticks = (((samples_from_now as u64) * 766245) >> 16).min(u16::MAX as u64) as u16;
    GATE_TIMER_PENDING.store(true, Ordering::Release);
    // SAFETY: ch2 was configured by gate_timer_setup() at boot.
    unsafe { mtu2::arm(GATE_TIMER_CH, ticks) };
}

abi_pin!(
    deluge_battery_read_raw,
    deluge_battery_start_conversion,
    deluge_midi_gate_timer_arm,
    deluge_midi_gate_timer_pending,
    deluge_signal_read,
    deluge_signal_write,
);
