//! Regression gate: does a real, FINALIZED (`RecorderStatus::COMPLETE`) multi-cluster recording
//! read back correctly through the region port on THIS target?
//!
//! `SampleRecorder` owns private capture buffers and relies on a finalize-time guard to resize
//! `Sample::overviewCache_` (the structure this probe measures via `table_clusters`/
//! `_table_clusters()`) to the real cluster count. `finalizeRecordedFile()`'s no-alteration
//! else-branch -- the only branch `AudioClip` recording ever takes -- never resized it at all, so
//! it stayed the single entry `Sample::initialize(1)` set in `setup()`. On the Rust-cursor port
//! specifically, `num_clusters` is derived independently from the finalized
//! `audio_data_length_bytes` (`abi.rs::num_clusters_for`), so `acquire(index >= 1)` could reach
//! `cluster_construct`/`cluster_materialize` (`sample_stream.cpp`) before the backing structure
//! had grown to match.
//!
//! This module drives `scenarios/recorder_readback_probe.h`'s finalized-multicluster C-ABI (a real
//! `SampleRecorder`, fed real audio, driven all the way to `RecorderStatus::COMPLETE` so
//! `finalizeRecordedFile()` genuinely runs) through the SAME region-port entry point
//! (`deluge_sample_region_acquire_ex`) real playback uses, on THIS target, and reports the
//! finalize-time-grow sizing plus the acquired region's byte correctness.
//!
//! Thread-agnostic like [`super::scenario::run`] and [`super::recorder_probe::run`]: only
//! `.await`s [`embassy_time::Timer`], no thread spawn, no `sim_latency` dependency.
#![cfg(all(not(target_os = "none"), feature = "host_app"))]

use std::sync::Mutex;

use embassy_time::{Duration, Instant, Timer};

// Host-only C-ABI bridge (tests/integration/scenarios/recorder_readback_probe.{h,cpp}, compiled
// into the host tree's `deluge_scenarios` objects that build.rs archives; the device firmware
// never contains it). Manually declared here, same pattern as `recorder_probe.rs`'s bridge.
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

/// `DelugeRegionState` values, as bytes (`libdeluge/sample_source.h`); `0` is this probe's own
/// "harness setup failed" sentinel.
pub const STATE_HARNESS_ERROR: u8 = 0;
pub const STATE_READY: u8 = crate::sys::DelugeRegionState::Ready as u8;
pub const STATE_LOADING: u8 = crate::sys::DelugeRegionState::Loading as u8;
#[allow(dead_code)]
pub const STATE_UNAVAILABLE: u8 = crate::sys::DelugeRegionState::Unavailable as u8;

/// Outcome of [`run`]. Plain data (not a `Result`/panic), same rationale as
/// [`super::recorder_probe::RecorderProbeResult`].
#[derive(Debug, Default, Clone, Copy)]
pub struct RecorderFinalizeProbeResult {
    /// `registerTasks()` claimed a scheduler slot within `step_timeout`.
    pub boot_ready: bool,
    /// The state the C++-side bounded retry loop settled on immediately after finalize + first
    /// acquire.
    pub initial_state: u8,
    /// The state after this task's own poll loop (yielding to the executor between checks, the
    /// mechanism that actually owns the drain on `host_app`), bounded by `poll_window`.
    pub final_state: u8,
    /// How many poll iterations ran before `final_state` was read.
    pub poll_iterations: u32,
    /// The waveform overview cache's physical entry count captured right after finalize -- the
    /// direct regression assertion. 0 if the harness itself failed to set up.
    pub table_clusters: u32,
    /// The finalized recording's true required cluster count for the same geometry -- what
    /// `table_clusters` must be >= for the fix to hold.
    pub expected_clusters: u32,
    /// Whether the acquired region's payload bytes matched the recorded ramp, byte-for-byte.
    pub bytes_ok: bool,
    /// `final_state == STATE_READY && table_clusters >= expected_clusters && bytes_ok` — the
    /// plain pass/fail a caller most likely wants.
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

/// Runs the probe. `poll_window` bounds how long this waits, yielding to the executor between
/// checks, for a `STATE_LOADING` result to resolve.
pub async fn run(step_timeout: Duration, poll_window: Duration) -> RecorderFinalizeProbeResult {
    let mut result = RecorderFinalizeProbeResult::default();

    // Same rationale as scenario::run / recorder_probe::run: don't touch C++ app state
    // (currentSong et al.) before registerTasks() has actually run.
    result.boot_ready = wait_for(step_timeout, || {
        crate::scheduler::registered_task_count() > 0
    })
    .await;
    if !result.boot_ready {
        return result;
    }

    // Mono, several clusters' worth of frames (Cluster::size is 32KB; 3*32768/3 + 400 frames
    // comfortably spans 4 clusters) so region index 1 is a real, non-header cluster. Mono +
    // never setting allowFileAlterationAfter guarantees finalizeRecordedFile()'s no-alteration
    // else-branch -- the one branch the regression left completely unresized.
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

/// Cross-thread handoff for [`recorder_finalize_probe_task`] — see its doc comment.
static RESULT: Mutex<Option<RecorderFinalizeProbeResult>> = Mutex::new(None);

/// `host_app` boot-path convenience wrapper (NOT part of a reusable driver — same shape as
/// [`super::recorder_probe::recorder_probe_task`]). Spawn on the SAME executor `host_app_task`
/// and the async streaming-fill task run on, then poll [`take_result`] from
/// the OS thread. Embassy task args must be `Copy`; durations are passed as plain millis.
#[embassy_executor::task]
pub async fn recorder_finalize_probe_task(step_timeout_ms: u64, poll_window_ms: u64) {
    let result = run(
        Duration::from_millis(step_timeout_ms),
        Duration::from_millis(poll_window_ms),
    )
    .await;
    *RESULT.lock().unwrap() = Some(result);
}

/// Takes the result [`recorder_finalize_probe_task`] stashed, if it has finished. `None` before
/// completion.
pub fn take_result() -> Option<RecorderFinalizeProbeResult> {
    RESULT.lock().unwrap().take()
}
