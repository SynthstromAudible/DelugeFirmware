//! Host tests for the app-loop driver: deadlines, wakes and encoder levels, on an embassy std
//! executor with a stand-in service.
//!
//! The driver's wake state is process-global, so the tests share one driver on one executor and run
//! one at a time under [`serial`].
#![feature(impl_trait_in_assoc_type)]

use std::sync::atomic::{AtomicU32, AtomicU64, Ordering::SeqCst};
use std::sync::{Mutex, MutexGuard, OnceLock};
use std::time::{Duration, Instant};

use deluge_bsp_rust::app_loop::{self, WAKE_APP, WAKE_ENCODERS, WAKE_TIMER};
use deluge_bsp_rust::deluge_bsp::encoder::{ENCODER_DELTAS, ENCODER_WAKER};

/// Every service call: (now, reasons).
static CALLS: Mutex<Vec<(u64, u32)>> = Mutex::new(Vec::new());
/// What the stand-in service returns: the next deadline, in µs, or `u64::MAX`.
static NEXT: AtomicU64 = AtomicU64::new(u64::MAX);
/// Calls left for which the stand-in service answers "now", as the app does mid-pass.
static BUSY: AtomicU32 = AtomicU32::new(0);
/// Wakes [`neighbour`] on the first busy call.
static NEIGHBOUR: embassy_sync::signal::Signal<
    embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex,
    (),
> = embassy_sync::signal::Signal::new();
/// How many service calls had been made when [`neighbour`] first ran; `u64::MAX` until it has.
static NEIGHBOUR_SAW: AtomicU64 = AtomicU64::new(u64::MAX);

/// The stand-in `deluge_app_service`. It records the call and, like the app's encoder job, takes
/// the encoder edges.
fn service(now: u64, reasons: u32) -> u64 {
    CALLS.lock().unwrap().push((now, reasons));
    for d in &ENCODER_DELTAS {
        d.swap(0, core::sync::atomic::Ordering::Relaxed);
    }
    if BUSY.load(SeqCst) > 0 {
        BUSY.fetch_sub(1, SeqCst);
        NEIGHBOUR.signal(());
        // As a job that requests another pass does: the driver's next wait has a wake pending.
        app_loop::wake(WAKE_APP);
        return now;
    }
    NEXT.load(SeqCst)
}

/// Another task on the driver's executor: records how many service calls it found the first time it
/// ran.
#[embassy_executor::task]
async fn neighbour() {
    loop {
        NEIGHBOUR.wait().await;
        let seen = CALLS.lock().unwrap().len() as u64;
        let _ = NEIGHBOUR_SAW.compare_exchange(u64::MAX, seen, SeqCst, SeqCst);
    }
}

#[embassy_executor::task]
async fn driver() {
    app_loop::drive(service).await
}

fn serial() -> MutexGuard<'static, ()> {
    static SERIAL: Mutex<()> = Mutex::new(());
    let guard = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
    static STARTED: OnceLock<()> = OnceLock::new();
    STARTED.get_or_init(|| {
        std::thread::spawn(|| {
            let executor: &'static mut embassy_executor::Executor =
                Box::leak(Box::new(embassy_executor::Executor::new()));
            executor.run(|spawner| {
                spawner.spawn(driver().unwrap());
                spawner.spawn(neighbour().unwrap());
            });
        });
        // The driver's first pass runs at once; let it happen before any test counts calls.
        wait_until(Duration::from_secs(5), "the driver's first pass", || {
            !CALLS.lock().unwrap().is_empty()
        });
    });
    NEXT.store(u64::MAX, SeqCst);
    CALLS.lock().unwrap().clear();
    guard
}

fn wait_until(timeout: Duration, what: &str, pred: impl Fn() -> bool) {
    let deadline = Instant::now() + timeout;
    while !pred() {
        assert!(Instant::now() < deadline, "timed out waiting for: {what}");
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn calls() -> Vec<(u64, u32)> {
    CALLS.lock().unwrap().clone()
}

#[test]
fn wake_from_another_thread_runs_a_pass() {
    let _s = serial();
    app_loop::wake(WAKE_APP);
    wait_until(Duration::from_millis(500), "a pass with WAKE_APP", || {
        calls().iter().any(|&(_, r)| r & WAKE_APP != 0)
    });
}

#[test]
fn returned_deadline_brings_a_timer_pass() {
    let _s = serial();
    // Ask for a pass 30 ms from now, by way of a first pass that reads NEXT.
    let now_us = embassy_time::Instant::now().as_micros();
    NEXT.store(now_us + 30_000, SeqCst);
    app_loop::wake(WAKE_APP);
    wait_until(
        Duration::from_millis(500),
        "the pass that reads the deadline",
        || !calls().is_empty(),
    );
    NEXT.store(u64::MAX, SeqCst);
    wait_until(Duration::from_millis(500), "the timer pass", || {
        calls().iter().any(|&(_, r)| r & WAKE_TIMER != 0)
    });
    let (at, _) = *calls().iter().find(|&&(_, r)| r & WAKE_TIMER != 0).unwrap();
    assert!(
        at >= now_us + 30_000,
        "the timer pass came early: {at} < {}",
        now_us + 30_000
    );
}

#[test]
fn encoder_edges_bring_an_encoder_pass() {
    let _s = serial();
    ENCODER_DELTAS[2].store(1, core::sync::atomic::Ordering::Relaxed);
    ENCODER_WAKER.wake();
    wait_until(
        Duration::from_millis(500),
        "a pass with WAKE_ENCODERS",
        || calls().iter().any(|&(_, r)| r & WAKE_ENCODERS != 0),
    );
    // The service took the edges, so the driver goes quiet: no pass in the next 50 ms.
    let n = calls().len();
    std::thread::sleep(Duration::from_millis(50));
    assert_eq!(calls().len(), n, "passes kept coming with nothing pending");
}

#[test]
fn encoders_pending_after_a_pass_trigger_another() {
    let _s = serial();
    // Edges that arrive while no waker is registered (between passes) are still seen: the driver
    // checks the level after every pass.
    ENCODER_DELTAS[1].store(-1, core::sync::atomic::Ordering::Relaxed);
    app_loop::wake(WAKE_APP);
    wait_until(
        Duration::from_millis(500),
        "a pass with WAKE_ENCODERS",
        || calls().iter().any(|&(_, r)| r & WAKE_ENCODERS != 0),
    );
}

#[test]
fn a_service_answering_now_lets_the_executor_run_before_the_next_call() {
    let _s = serial();
    NEIGHBOUR_SAW.store(u64::MAX, SeqCst);
    // Five calls answer "now", as the app does while a pass has jobs left, each with a wake pending.
    BUSY.store(5, SeqCst);
    app_loop::wake(WAKE_APP);
    wait_until(Duration::from_millis(500), "the busy calls", || {
        BUSY.load(SeqCst) == 0 && NEIGHBOUR_SAW.load(SeqCst) != u64::MAX
    });
    // The neighbour was woken on the first busy call and ran while busy calls were still to come.
    // Without a turn between calls, the driver makes all five, and the one after, first.
    let saw = NEIGHBOUR_SAW.load(SeqCst);
    assert!(
        saw < 5,
        "no executor turn between service calls: the neighbour ran after call {saw}"
    );
}
