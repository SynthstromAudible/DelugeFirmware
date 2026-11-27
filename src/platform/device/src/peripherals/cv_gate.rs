//! `libdeluge/cv_gate.h` — the CV and gate outputs' set-up, and the external trigger clock. The
//! outputs themselves are driven by the timed output service (`crate::timed_output`). The runtime's
//! start-up (`runtime::device::start`) already ran `deluge_bsp::cv_gate::init()`, so the init hooks
//! here are no-ops.
#![allow(non_upper_case_globals)]

use core::future::poll_fn;
use core::sync::atomic::{AtomicUsize, Ordering};
use core::task::Poll;

use deluge_bsp::trigger_clock;

use crate::sys::DelugeTriggerClockHandler;

#[unsafe(no_mangle)]
pub extern "C" fn deluge_cv_init(_display_shares_spi: bool) {
    // `runtime::device::start` already ran cv_gate::init(): pins, MAX5136 linearity, zeroing.
}

// Gate output

#[unsafe(no_mangle)]
pub extern "C" fn deluge_gate_init() {
    // Gate GPIOs were configured by cv_gate::init() in `runtime::device::start`.
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
    deluge_gate_init,
    deluge_trigger_clock_set_handler,
);
