//! Host test for the pad writer: `pad_render` drains the output queue and sends the PIC refresh
//! itself once it is empty, so no separate "PIC flush" job is needed to trigger it.
//!
//! Mirrors the spawn set `runtime::host` uses for the control tasks (`pic_pump` + `pad_render` on
//! one executor), plus a stand-in "job" task that enqueues a whole batch with no `.await` between
//! its two sends, as the app's synchronous redraw job body does. That same-executor, no-yield
//! property is what the brief's safety argument rests on: pad_render cannot observe `OUT` empty
//! until the whole batch has landed.
#![feature(impl_trait_in_assoc_type)]

use std::sync::{Mutex, MutexGuard, OnceLock};
use std::time::{Duration, Instant};

use deluge_bsp_rust::peripherals::control;
use deluge_bsp_rust::sys::DelugeColour;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;

/// Triggers [`batch_two_rows`] to enqueue its batch.
static TRIGGER: Signal<CriticalSectionRawMutex, ()> = Signal::new();

fn columns() -> [DelugeColour; 16] {
    [DelugeColour { r: 1, g: 2, b: 3 }; 16]
}

/// Stand-in for the app's redraw job: queues two pad rows (column pairs) back to back, with no
/// `.await` between them, exactly as the synchronous C++ job body does.
#[embassy_executor::task]
async fn batch_two_rows() {
    loop {
        TRIGGER.wait().await;
        let colours = columns();
        control::deluge_control_set_pad_columns(0, colours.as_ptr(), 16);
        control::deluge_control_set_pad_columns(1, colours.as_ptr(), 16);
    }
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
                spawner.spawn(control::pic_pump().unwrap());
                spawner.spawn(control::pad_render().unwrap());
                spawner.spawn(batch_two_rows().unwrap());
            });
        });
    });
    control::reset_refresh_count_for_test();
    guard
}

fn wait_until(timeout: Duration, what: &str, pred: impl Fn() -> bool) {
    let deadline = Instant::now() + timeout;
    while !pred() {
        assert!(Instant::now() < deadline, "timed out waiting for: {what}");
        std::thread::sleep(Duration::from_millis(1));
    }
}

#[test]
fn drain_sends_exactly_one_refresh_after_the_batch() {
    let _s = serial();
    TRIGGER.signal(());
    wait_until(
        Duration::from_millis(500),
        "the batch to drain and refresh",
        || control::refresh_count_for_test() >= 1,
    );
    // Give an (incorrect) per-command refresh a chance to show up before asserting "exactly one".
    std::thread::sleep(Duration::from_millis(50));
    assert_eq!(
        control::refresh_count_for_test(),
        1,
        "one refresh should follow the whole batch, not one per queued row"
    );
}

#[test]
fn an_idle_queue_sends_no_refresh() {
    let _s = serial();
    std::thread::sleep(Duration::from_millis(50));
    assert_eq!(
        control::refresh_count_for_test(),
        0,
        "an idle tick must not send a refresh"
    );
}

#[test]
fn an_oled_frame_holds_pad_output_until_it_ends() {
    let _s = serial();
    embassy_futures::block_on(control::begin_oled_frame());
    assert_eq!(
        control::deluge_control_pad_output_space(),
        0,
        "the app must see no room while an OLED frame holds the PIC UART"
    );
    TRIGGER.signal(());
    std::thread::sleep(Duration::from_millis(50));
    assert_eq!(
        control::refresh_count_for_test(),
        0,
        "pad output went out during an OLED frame"
    );

    control::end_oled_frame();
    wait_until(
        Duration::from_millis(500),
        "the held batch to drain and refresh",
        || control::refresh_count_for_test() >= 1,
    );
    assert!(control::deluge_control_pad_output_space() > 0);
}
