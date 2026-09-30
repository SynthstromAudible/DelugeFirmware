//! The interaction tier's driver (`libdeluge/app.h`'s `deluge_app_service`): one Embassy task that
//! calls the app whenever a time it asked for comes or a wake reason arrives.
//!
//! The app owns its jobs and their order (`deluge::runtime::Loop`); this task owns only the
//! waiting. Wake reasons accumulate in [`REASONS`] from any tier and are handed to the next pass.
//! The encoder reason is level-triggered, read from the edge counters after every wait, so edges
//! that arrive between passes are never lost.

use core::sync::atomic::{AtomicU32, Ordering};

use deluge_bsp::encoder;
use embassy_futures::select::select3;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;
use embassy_time::{Instant, Timer};

/// `DELUGE_WAKE_TIMER`: a time the app returned has come.
pub const WAKE_TIMER: u32 = 1;
/// `DELUGE_WAKE_ENCODERS`: an encoder has edges to take.
pub const WAKE_ENCODERS: u32 = 2;
/// `DELUGE_WAKE_APP`: the app asked for a pass.
pub const WAKE_APP: u32 = 4;

/// The reasons gathered since the last pass.
static REASONS: AtomicU32 = AtomicU32::new(0);
/// Ends the driver's wait early.
static WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// Adds `reasons` for the next pass and wakes the driver. Safe from any tier, and from an ISR.
pub fn wake(reasons: u32) {
    REASONS.fetch_or(reasons, Ordering::Release);
    WAKE.signal(());
}

fn encoders_pending() -> bool {
    encoder::ENCODER_DELTAS
        .iter()
        .any(|d| d.load(Ordering::Relaxed) != 0)
}

/// Resolves once an encoder has edges to take.
async fn encoder_edges() {
    core::future::poll_fn(|cx| {
        // Register before checking, so an edge in between still wakes us.
        encoder::ENCODER_WAKER.register(cx.waker());
        if encoders_pending() {
            core::task::Poll::Ready(())
        } else {
            core::task::Poll::Pending
        }
    })
    .await
}

/// Serves `service` forever: waits for its deadline, a [`wake`], or encoder edges, then calls it
/// with the current time in µs and the reasons, and waits for the deadline it returns
/// (`u64::MAX` for none).
///
/// The first pass runs at once. Never returns.
pub async fn drive(service: fn(u64, u32) -> u64) {
    let mut due = Instant::now();
    loop {
        select3(Timer::at(due), WAKE.wait(), encoder_edges()).await;
        let now = Instant::now();
        let mut reasons = REASONS.swap(0, Ordering::Acquire);
        if now >= due {
            reasons |= WAKE_TIMER;
        }
        if encoders_pending() {
            reasons |= WAKE_ENCODERS;
        }
        let next = service(now.as_micros(), reasons);
        due = if next == u64::MAX {
            Instant::MAX
        } else {
            Instant::from_micros(next)
        };
    }
}

/// Runs the app's loop on the thread executor, from the end of boot.
#[cfg(has_app)]
#[embassy_executor::task]
pub async fn app_loop() {
    while !crate::boot::finished() {
        Timer::after_millis(1).await;
    }
    drive(|now, reasons| {
        // SAFETY: the app's loop entry, called only from this task, on the interaction tier.
        unsafe { crate::sys::deluge_app_service(now, reasons) }
    })
    .await
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_app_wake(reasons: u32) {
    wake(reasons);
}

abi_pin!(deluge_app_wake);
abi_const!(
    WAKE_TIMER => DELUGE_WAKE_TIMER,
    WAKE_ENCODERS => DELUGE_WAKE_ENCODERS,
    WAKE_APP => DELUGE_WAKE_APP,
);
