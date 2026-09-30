//! A regression gate: does a finalized multi-cluster recording read back correctly through the
//! region port?
//!
//! Finalizing must grow the sample's per-cluster table (`Sample::overviewCache_`) from its single
//! setup entry to the real cluster count, on every branch of `finalizeRecordedFile()`. The Rust
//! cursor derives the cluster count from the finalized data length on its own, so a table left
//! short lets `acquire(index >= 1)` run past it.
//!
//! This records a real `SampleRecorder` to completion
//! (`tests/integration/scenarios/recorder_readback_probe.h`), acquires region 1 through
//! `deluge_sample_region_acquire_ex`, and checks the table size and the bytes. Like
//! [`super::scenario::run`], [`run`] only awaits timers.

use std::sync::Mutex;

use embassy_time::{Duration, Instant, Timer};

// The host-only probe bridge (`recorder_readback_probe.{h,cpp}`), declared by hand.
unsafe extern "C" {
    fn deluge_harness_recorder_finalized_multicluster_probe(
        num_channels: u8,
        num_frames: u32,
        region_index: u32,
    ) -> u8;
    fn deluge_harness_recorder_finalized_multicluster_probe_poll() -> u8;
    fn deluge_harness_recorder_finalized_multicluster_probe_table_clusters() -> u32;
    fn deluge_harness_recorder_finalized_multicluster_probe_expected_clusters() -> u32;
    fn deluge_harness_recorder_finalized_multicluster_probe_bytes_ok() -> u8;
    fn deluge_harness_recorder_finalized_multicluster_probe_end();
}

/// The probe's "harness setup failed" sentinel. The other `STATE_*` values are
/// `DelugeRegionState` (`libdeluge/sample_source.h`) as bytes.
pub const STATE_HARNESS_ERROR: u8 = 0;
pub const STATE_READY: u8 = crate::sys::DelugeRegionState::Ready as u8;
pub const STATE_LOADING: u8 = crate::sys::DelugeRegionState::Loading as u8;
#[allow(dead_code)]
pub const STATE_UNAVAILABLE: u8 = crate::sys::DelugeRegionState::Unavailable as u8;

/// The outcome of [`run`].
#[derive(Debug, Default, Clone, Copy)]
pub struct RecorderFinalizeProbeResult {
    /// Boot reached `registerTasks()` within `step_timeout`.
    pub boot_ready: bool,
    /// The state the C++ side's bounded retry settled on after finalizing and the first acquire.
    pub initial_state: u8,
    /// The state after polling for up to `poll_window`, yielding between checks.
    pub final_state: u8,
    /// The poll iterations before `final_state` was read.
    pub poll_iterations: u32,
    /// The per-cluster table's entry count right after finalizing; 0 if setup failed.
    pub table_clusters: u32,
    /// The cluster count the finalized recording needs; `table_clusters` must reach it.
    pub expected_clusters: u32,
    /// The acquired region's bytes matched the recorded ramp.
    pub bytes_ok: bool,
    /// READY, a nonzero table of at least `expected_clusters`, and `bytes_ok`.
    pub passed: bool,
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
pub async fn run(step_timeout: Duration, poll_window: Duration) -> RecorderFinalizeProbeResult {
    let mut result = RecorderFinalizeProbeResult::default();

    // Touch no app state before `registerTasks()` has run.
    result.boot_ready = wait_for(step_timeout, || {
        crate::scheduler::registered_task_count() > 0
    })
    .await;
    if !result.boot_ready {
        return result;
    }

    // Mono 24-bit frames spanning four 32 KiB clusters, so region 1 is a full data cluster. Mono,
    // with no file alteration allowed, takes `finalizeRecordedFile()`'s no-alteration branch,
    // the one that regressed.
    const NUM_FRAMES: u32 = (3 * 32768 / 3) + 400;
    result.initial_state =
        unsafe { deluge_harness_recorder_finalized_multicluster_probe(1, NUM_FRAMES, 1) };

    let mut state = result.initial_state;
    let deadline = Instant::now() + poll_window;
    while state == STATE_LOADING && Instant::now() < deadline {
        Timer::after_millis(20).await;
        state = unsafe { deluge_harness_recorder_finalized_multicluster_probe_poll() };
        result.poll_iterations += 1;
    }
    result.final_state = state;

    result.table_clusters =
        unsafe { deluge_harness_recorder_finalized_multicluster_probe_table_clusters() };
    result.expected_clusters =
        unsafe { deluge_harness_recorder_finalized_multicluster_probe_expected_clusters() };
    result.bytes_ok =
        unsafe { deluge_harness_recorder_finalized_multicluster_probe_bytes_ok() } == 1;
    result.passed = state == STATE_READY
        && result.table_clusters != 0
        && result.table_clusters >= result.expected_clusters
        && result.bytes_ok;

    unsafe { deluge_harness_recorder_finalized_multicluster_probe_end() };
    result
}

/// The result [`recorder_finalize_probe_task`] hands to the OS thread.
static RESULT: Mutex<Option<RecorderFinalizeProbeResult>> = Mutex::new(None);

/// Runs [`run`] as an embassy task: spawn it on the host app executor, then poll
/// [`take_result`]. Durations are in milliseconds.
#[embassy_executor::task]
pub async fn recorder_finalize_probe_task(step_timeout_ms: u64, poll_window_ms: u64) {
    let result = run(
        Duration::from_millis(step_timeout_ms),
        Duration::from_millis(poll_window_ms),
    )
    .await;
    *RESULT.lock().unwrap() = Some(result);
}

/// Takes the result [`recorder_finalize_probe_task`] stored, or `None` before it has finished.
pub fn take_result() -> Option<RecorderFinalizeProbeResult> {
    RESULT.lock().unwrap().take()
}
