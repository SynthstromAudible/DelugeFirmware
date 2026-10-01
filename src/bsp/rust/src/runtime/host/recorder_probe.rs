//! A diagnostic: does a still-recording sample read back through the async streaming fill?
//!
//! The fill reads through the fill context's file handle, and a recording's is
//! `DELUGE_HANDLE_NONE`, so such a read is expected to stay LOADING. This drives a real
//! `SampleRecorder`, never finalized (`tests/integration/scenarios/recorder_readback_probe.h`),
//! through the region port playback uses (`deluge_sample_region_acquire_ex`) and reports what
//! comes back. Like [`super::scenario::run`], [`run`] only awaits timers; [`recorder_probe_task`]
//! runs it for an OS thread that polls [`take_result`].

use std::sync::Mutex;

use embassy_time::{Duration, Instant, Timer};

// The host-only probe bridge (`recorder_readback_probe.{h,cpp}`, archived by the build script),
// declared by hand as harness surface.
unsafe extern "C" {
    fn deluge_harness_recorder_probe(num_channels: u8, num_frames: u32) -> u8;
    fn deluge_harness_recorder_probe_drain_tick();
    fn deluge_harness_recorder_probe_acquire() -> u8;
    fn deluge_harness_recorder_probe_poll() -> u8;
    fn deluge_harness_recorder_probe_abort();
    fn deluge_harness_recorder_probe_settle_tick() -> u8;
    fn deluge_harness_recorder_probe_end();
}

/// The probe's "harness setup failed" sentinel. The other `STATE_*` values are
/// `DelugeRegionState` (`libdeluge/sample_source.h`) as bytes.
pub const STATE_HARNESS_ERROR: u8 = 0;
pub const STATE_READY: u8 = crate::sys::DelugeRegionState::Ready as u8;
pub const STATE_LOADING: u8 = crate::sys::DelugeRegionState::Loading as u8;
#[allow(dead_code)]
pub const STATE_UNAVAILABLE: u8 = crate::sys::DelugeRegionState::Unavailable as u8;

/// The outcome of [`run`]. An unresolved read is a finding, not a failure, so the caller judges.
#[derive(Debug, Default, Clone, Copy)]
pub struct RecorderProbeResult {
    /// Boot finished within `step_timeout`.
    pub boot_ready: bool,
    /// The first acquire's state, after the drain ticks; expected LOADING.
    pub initial_state: u8,
    /// The state after polling for up to `poll_window`, yielding so the fill task can run.
    pub final_state: u8,
    /// The poll iterations before `final_state` was read.
    pub poll_iterations: u32,
    /// `final_state == STATE_READY`.
    pub resolved_ready: bool,
}

async fn wait_for(timeout: Duration, mut cond: impl FnMut() -> bool) -> bool {
    let deadline = Instant::now() + timeout;
    loop {
        if cond() {
            return true;
        }
        if Instant::now() >= deadline {
            return false;
        }
        Timer::after_millis(5).await;
    }
}

/// Runs the probe, waiting up to `poll_window` for a LOADING read to resolve.
pub async fn run(step_timeout: Duration, poll_window: Duration) -> RecorderProbeResult {
    let mut result = RecorderProbeResult::default();

    // Touch no app state before boot has finished.
    result.boot_ready = wait_for(step_timeout, || crate::boot::finished()).await;
    if !result.boot_ready {
        return result;
    }

    // 50 mono frames: well inside cluster 0, which is the one probed.
    let setup = unsafe { deluge_harness_recorder_probe(1, 50) };
    if setup == STATE_HARNESS_ERROR {
        result.initial_state = STATE_HARNESS_ERROR;
        return result;
    }

    // Flush to the card, yielding between ticks so the storage task can answer each request.
    for _ in 0..4 {
        unsafe { deluge_harness_recorder_probe_drain_tick() };
        Timer::after_millis(1).await;
    }

    result.initial_state = unsafe { deluge_harness_recorder_probe_acquire() };

    let mut state = result.initial_state;
    let deadline = Instant::now() + poll_window;
    while state == STATE_LOADING && Instant::now() < deadline {
        Timer::after_millis(20).await;
        state = unsafe { deluge_harness_recorder_probe_poll() };
        result.poll_iterations += 1;
    }
    result.final_state = state;
    result.resolved_ready = state == STATE_READY;

    // Settle before deleting: a request can still be in flight.
    unsafe { deluge_harness_recorder_probe_abort() };
    while unsafe { deluge_harness_recorder_probe_settle_tick() } == 0 {
        Timer::after_millis(1).await;
    }
    unsafe { deluge_harness_recorder_probe_end() };
    result
}

/// The result [`recorder_probe_task`] hands to the OS thread.
static RESULT: Mutex<Option<RecorderProbeResult>> = Mutex::new(None);

/// Runs [`run`] as an embassy task: spawn it on the host app executor, then poll
/// [`take_result`]. Durations are in milliseconds.
#[embassy_executor::task]
pub async fn recorder_probe_task(step_timeout_ms: u64, poll_window_ms: u64) {
    let result = run(
        Duration::from_millis(step_timeout_ms),
        Duration::from_millis(poll_window_ms),
    )
    .await;
    *RESULT.lock().unwrap() = Some(result);
}

/// Takes the result [`recorder_probe_task`] stored, or `None` before it has finished.
pub fn take_result() -> Option<RecorderProbeResult> {
    RESULT.lock().unwrap().take()
}
