//! cv_gate.h — CV outputs, gate outputs, and the external trigger clock.
//!
//! Gates are plain GPIO (deluge_bsp::cv_gate::gate_set). CV outputs drive the
//! MAX5136 DAC over RSPI0 — which is SHARED with the OLED — so CV writes funnel
//! through a writer task that acquires the bus asynchronously
//! (deluge_bsp::cv_gate::cv_set), exactly like the PIC output task: a synchronous
//! `block_on(cv_set)` from the app tick could deadlock against the OLED task
//! holding the bus guard. The board already ran `cv_gate::init()` (pins, DAC
//! linearity, gate GPIO), so the init hooks here are no-ops.
#![allow(non_upper_case_globals)]

use core::future::poll_fn;
use core::sync::atomic::{AtomicU32, AtomicUsize, Ordering};
use core::task::Poll;

use deluge_bsp::{cv_gate, trigger_clock};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;

use crate::sys::DelugeTriggerClockHandler;

// ── CV output (RSPI0, shared with OLED → writer task) ──────────────────────────

#[derive(Clone, Copy)]
struct CvWrite {
    ch: u8,
    value: u16,
}

static CV_OUT: Channel<CriticalSectionRawMutex, CvWrite, 16> = Channel::new();
static CV_SENT: AtomicU32 = AtomicU32::new(0);

/// The sole CV transmitter: drains queued writes and clocks each to the DAC over
/// the shared RSPI0 bus (awaiting any in-progress OLED frame).
#[embassy_executor::task]
pub async fn cv_writer() {
    loop {
        let w = CV_OUT.receive().await;
        cv_gate::cv_set(w.ch, w.value).await;
        CV_SENT.fetch_add(1, Ordering::Relaxed);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_init(_display_shares_spi: bool) {
    // The board's cv_gate::init() already did pins + MAX5136 linearity + zeroing.
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_set(channel: u8, value: u16) {
    // Non-blocking producer; the writer task performs the bus-arbitrated send.
    let _ = CV_OUT.try_send(CvWrite { ch: channel, value });
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_sent_count() -> u32 {
    CV_SENT.load(Ordering::Relaxed)
}

// ── Gate output (plain GPIO) ───────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_init() {
    // Gate GPIOs were configured by the board's cv_gate::init().
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_set(channel: u8, on: bool) {
    // SAFETY: GPIO write; channel is 0..NUM_GATE_CHANNELS per the board descriptor.
    unsafe { cv_gate::gate_set(channel, on) };
}

// ── External trigger clock (analog clock input) ────────────────────────────────

/// App handler called on each trigger-clock rising edge, stored as a raw fn ptr
/// (0 = none). DelugeTriggerClockHandler = Option<unsafe extern "C" fn(u64)>.
static TRIGGER_HANDLER: AtomicUsize = AtomicUsize::new(0);

#[unsafe(no_mangle)]
pub extern "C" fn deluge_trigger_clock_set_handler(handler: DelugeTriggerClockHandler) {
    TRIGGER_HANDLER.store(handler.map_or(0, |f| f as usize), Ordering::Relaxed);
}

/// Calls the app's trigger-clock handler once per rising edge captured by
/// deluge_bsp::trigger_clock's ISR (woken by EDGE_WAKER; runs between app ticks).
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
        let ts = trigger_clock::LAST_EDGE_TICKS.load(Ordering::Relaxed);
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
