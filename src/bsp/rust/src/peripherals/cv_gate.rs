//! `libdeluge/cv_gate.h` — CV outputs, gate outputs, and the external trigger clock.
//!
//! Gates are plain GPIO. CV outputs drive the MAX5136 DAC over RSPI0, which is shared with the
//! OLED, so CV writes funnel through [`cv_writer`], which acquires the bus asynchronously: a
//! synchronous send from an app task could deadlock against the OLED task holding the bus. The
//! runtime's start-up (`runtime::device::start`) already ran `deluge_bsp::cv_gate::init()`, so the
//! init hooks here are no-ops.
#![allow(non_upper_case_globals)]

use core::future::poll_fn;
use core::sync::atomic::{AtomicU32, AtomicUsize, Ordering};
use core::task::Poll;

use deluge_bsp::{cv_gate, trigger_clock};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;

use super::cv_latest::CvLatest;
use crate::sys::DelugeTriggerClockHandler;

// CV output

/// Each channel's latest value from [`deluge_cv_set`] until [`cv_writer`] takes it.
static CV_LATEST: CvLatest<{ cv_gate::NUM_CV_CHANNELS }> = CvLatest::new();
/// Wakes [`cv_writer`] after a [`deluge_cv_set`].
static CV_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();
static CV_SENT: AtomicU32 = AtomicU32::new(0);

/// Background task: the sole CV transmitter. Writes each channel's latest pending value to the DAC
/// over the shared RSPI0 bus, waiting out any OLED frame in progress.
#[embassy_executor::task]
pub async fn cv_writer() {
    loop {
        CV_WAKE.wait().await;
        // A set during a write leaves its channel pending, so sweep until a pass finds none.
        loop {
            let mut wrote = false;
            for ch in 0..cv_gate::NUM_CV_CHANNELS {
                if let Some(value) = CV_LATEST.take(ch) {
                    cv_gate::cv_set(ch as u8, value).await;
                    CV_SENT.fetch_add(1, Ordering::Relaxed);
                    wrote = true;
                }
            }
            if !wrote {
                break;
            }
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_init(_display_shares_spi: bool) {
    // `runtime::device::start` already ran cv_gate::init(): pins, MAX5136 linearity, zeroing.
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_set(channel: u8, value: u16) {
    // Non-blocking producer; the writer task performs the bus-arbitrated send. A channel the
    // Deluge doesn't wire is ignored, as cv_gate::cv_set would.
    if CV_LATEST.set(channel, value) {
        CV_WAKE.signal(());
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_sent_count() -> u32 {
    CV_SENT.load(Ordering::Relaxed)
}

// Gate output

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_init() {
    // Gate GPIOs were configured by cv_gate::init() in `runtime::device::start`.
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_set(channel: u8, on: bool) {
    // SAFETY: GPIO write; channel is 0..NUM_GATE_CHANNELS per the board descriptor.
    unsafe { cv_gate::gate_set(channel, on) };
}

// External trigger clock (analog clock input)

/// The app's handler for each trigger-clock rising edge, as a raw fn pointer (0 = none).
static TRIGGER_HANDLER: AtomicUsize = AtomicUsize::new(0);

#[unsafe(no_mangle)]
pub extern "C" fn deluge_trigger_clock_set_handler(handler: DelugeTriggerClockHandler) {
    TRIGGER_HANDLER.store(handler.map_or(0, |f| f as usize), Ordering::Relaxed);
}

/// Background task: calls the app's trigger-clock handler once per rising edge the
/// `deluge_bsp::trigger_clock` ISR captured, with the latest edge's capture stamp.
#[embassy_executor::task]
pub async fn trigger_clock_task() {
    let mut last = trigger_clock::EDGE_COUNT.load(Ordering::Relaxed);
    loop {
        poll_fn(|cx| {
            trigger_clock::EDGE_WAKER.register(cx.waker());
            if trigger_clock::EDGE_COUNT.load(Ordering::Relaxed) != last {
                Poll::Ready(())
            } else {
                Poll::Pending
            }
        })
        .await;

        let now = trigger_clock::EDGE_COUNT.load(Ordering::Relaxed);
        let ts = u64::from(crate::audio::capture_stamp::now_for(
            trigger_clock::LAST_EDGE_TICKS.load(Ordering::Relaxed),
        ));
        let h = TRIGGER_HANDLER.load(Ordering::Relaxed);
        while last != now {
            last = last.wrapping_add(1);
            if h != 0 {
                // SAFETY: h is a valid DelugeTriggerClockHandler set by the app.
                let f: unsafe extern "C" fn(u64) = unsafe { core::mem::transmute(h) };
                unsafe { f(ts) };
            }
        }
    }
}

abi_pin!(
    deluge_cv_init,
    deluge_cv_sent_count,
    deluge_cv_set,
    deluge_gate_init,
    deluge_gate_set,
    deluge_trigger_clock_set_handler,
);
