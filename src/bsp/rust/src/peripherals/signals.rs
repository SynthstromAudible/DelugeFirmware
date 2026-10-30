//! `libdeluge/signals.h` — board GPIO signals, battery sense, and the MIDI/gate timer.
//!
//! [`deluge_signal_write`] and [`deluge_signal_read`] map `DelugeSignal` to the board's pins: the
//! LEDs this module owns ([`leds_init`]) and `deluge_bsp::jacks`' detects and speaker enable.
//! `SPEAKER_ENABLE` gates the audio output path: rendered audio is inaudible until the app turns it
//! on. `CODEC_ENABLE` is a no-op: `deluge_bsp::audio::init` powers the codec itself.
#![allow(non_upper_case_globals)]

use core::cell::RefCell;

use critical_section::Mutex;
use deluge_bsp::board::GateTimer;
use deluge_bsp::jacks::{self, Jack};
use embassy_sync::once_lock::OnceLock;
use rza1_hal::Peri;
use rza1_hal::gpio::{Level, Output};
use rza1_hal::interrupt::{Handler, typelevel};
use rza1_hal::mtu2::OneShot;
use rza1_hal::peripherals::{P1_1, P6_7};

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

/// The SYNC LED (P6_7) and the battery LED (P1_1, open-drain: low lights it).
struct Leds {
    sync: Output<'static>,
    battery: Output<'static>,
}

static LEDS: Mutex<RefCell<Option<Leds>>> = Mutex::new(RefCell::new(None));

/// Take the two LEDs, both off. Once, at startup.
pub(crate) fn leds_init(sync: Peri<'static, P6_7>, battery: Peri<'static, P1_1>) {
    let leds = Leds {
        sync: Output::new(sync, Level::Low),
        battery: Output::new(battery, Level::High),
    };
    critical_section::with(|cs| LEDS.borrow_ref_mut(cs).replace(leds));
}

/// Light or clear the SYNC LED. Does nothing before [`leds_init`].
pub(crate) fn set_sync_led(on: bool) {
    critical_section::with(|cs| {
        if let Some(l) = LEDS.borrow_ref_mut(cs).as_mut() {
            l.sync.set_level(on.into());
        }
    });
}

/// Light or clear the battery LED (open-drain, so on drives the pin low). Does nothing before
/// [`leds_init`].
fn set_battery_led(on: bool) {
    critical_section::with(|cs| {
        if let Some(l) = LEDS.borrow_ref_mut(cs).as_mut() {
            l.battery.set_level((!on).into());
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_write(signal: DelugeSignal, on: bool) {
    match signal {
        SYNC_LED => set_sync_led(on),
        BATTERY_LED => set_battery_led(on),
        SPEAKER_ENABLE => jacks::set_speaker_enable(on),
        // `deluge_bsp::audio::init` powers the codec (P6_12) when the app brings audio up, and the
        // app only ever asks for it on.
        CODEC_ENABLE => {}
        _ => {}
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_signal_read(signal: DelugeSignal) -> bool {
    let jack = match signal {
        HEADPHONE_DETECT => Jack::Headphone,
        LINE_IN_DETECT => Jack::LineIn,
        MIC_DETECT => Jack::Mic,
        LINE_OUT_DETECT_L => Jack::LineOutL,
        LINE_OUT_DETECT_R => Jack::LineOutR,
        _ => return false,
    };
    jacks::is_inserted(jack)
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
// so this module builds its own `mtu2::OneShot` on the board's MTU2 channel 2.

/// Prescaler P0/64 ≈ 516 kHz: 1 tick ≈ 1.94 µs, longest one-shot ≈ 127 ms.
const GATE_TIMER_PRESCALER: u16 = 64;
/// Timer ticks per second at [`GATE_TIMER_PRESCALER`]: P0 (33 MHz) / 64.
const GATE_TIMER_TICKS_PER_SEC: u64 = 515_616;

/// The MIDI/gate one-shot (MTU2 channel 2, the C firmware's `TIMER_MIDI_GATE_OUTPUT`), once
/// [`gate_timer_setup`] has run.
static GATE_TIMER: OnceLock<OneShot<'static>> = OnceLock::new();

/// MTU2 channel 2's compare match: clears and disarms the timer, then runs the app handler
/// (which arms again if anything is still to come).
struct GateTimerHandler;

impl Handler<typelevel::TGI2A> for GateTimerHandler {
    unsafe fn on_interrupt() {
        // Filled before the source can be unmasked: only `arm` unmasks it.
        if let Some(timer) = GATE_TIMER.try_get() {
            // Clearing before disarming keeps the GIC line from re-triggering.
            timer.clear_flag();
            timer.disarm();
        }
        // SAFETY: an app-provided C symbol that `libdeluge/signals.h` documents as an ISR callback.
        unsafe { crate::sys::midiAndGateTimerGoneOff() };
    }
}

rza1_hal::bind_interrupts!(struct GateTimerIrqs {
    TGI2A => GateTimerHandler;
});

/// One-time setup of the gate timer on the board's MTU2 channel 2, stopped until armed.
pub(crate) fn gate_timer_setup(timer: Peri<'static, GateTimer>) {
    // Its GIC priority, P5 (above USB, so gate timing is not delayed behind USB servicing), is
    // the board's: `deluge_bsp::Board::new` sets it.
    let timer = OneShot::new(timer, GATE_TIMER_PRESCALER, GateTimerIrqs);
    if GATE_TIMER.init(timer).is_err() {
        unreachable!("MTU2 channel 2 is owned once");
    }
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
        // `arm` stops the channel and clears its flag before loading the new compare value, so it
        // replaces an armed deadline. Before setup there is no timer to arm.
        if let Some(timer) = GATE_TIMER.try_get() {
            timer.arm(ticks);
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_gate_timer_cancel() {
    critical_section::with(|_| {
        if let Some(timer) = GATE_TIMER.try_get() {
            timer.disarm();
            timer.clear_flag();
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
