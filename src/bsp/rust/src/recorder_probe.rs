//! `host_app` diagnostic: does reading a STILL-RECORDING sample back resolve on the target where
//! `async_streaming_loader` is on by default?
//!
//! Device/`host_app` route the fill onto the Rust async task (`streaming_loader.rs`), which reads
//! via `efatfs_fs::read_at(handle=0, ...)` — and a recording's `efatfs_handle == 0`, so a
//! still-recording sample resolves LOADING (re-queued, never READY) uniformly on every target, sim
//! included. This module turns that into a measurement: it drives
//! `harness/recorder_readback_probe.h`'s C-ABI (a real `SampleRecorder`, fed real audio, never
//! finalized) through the SAME region-port entry point (`deluge_sample_region_acquire_ex`) real
//! playback uses, on THIS target, and reports what actually comes back.
//!
//! Thread-agnostic like [`crate::scenario::run`]: only `.await`s [`embassy_time::Timer`], no thread
//! spawn, no `sim_latency` dependency. [`recorder_probe_task`] is the same not-reusable
//! `host_app`-boot convenience wrapper `scenario::scenario_task` is (spawn on the host-app
//! executor, poll [`take_result`] from the OS thread).
#![cfg(all(not(target_os = "none"), feature = "host_app"))]

use std::sync::Mutex;

use embassy_time::{Duration, Instant, Timer};

// Host-only C-ABI bridge (src/deluge/harness/recorder_readback_probe.{h,cpp}, `DELUGE_HOST`-guarded
// — compiled into the linked host_app `deluge_app` object closure, never into the ARM device
// firmware). Manually declared here, same pattern as `scenario.rs`'s bridge: harness-only entry
// points the app exposes to the platform, not part of the libdeluge C-ABI the app CONSUMES.
unsafe extern "C" {
    fn deluge_harness_recorder_probe(
        num_channels: u8,
        num_frames: u32,
        pump_drain_ticks: u32,
    ) -> u8;
    fn deluge_harness_recorder_probe_drain_tick();
    fn deluge_harness_recorder_probe_acquire() -> u8;
    fn deluge_harness_recorder_probe_poll() -> u8;
    fn deluge_harness_recorder_probe_end();
}

/// `DelugeRegionState` values, mirrored here so the caller doesn't need the C header —
/// `libdeluge/sample_source.h` is the source of truth (`DELUGE_REGION_READY = 1`, `_LOADING = 2`,
/// `_UNAVAILABLE = 3`); `0` is this probe's own "harness setup failed" sentinel (see
/// `recorder_readback_probe.cpp`).
pub const STATE_HARNESS_ERROR: u8 = 0;
pub const STATE_READY: u8 = 1;
pub const STATE_LOADING: u8 = 2;
/// Documented for completeness (mirrors `libdeluge/sample_source.h`'s full state enum) even though
/// no code branches on it directly — a caller reads `final_state`/`resolved_ready` instead.
#[allow(dead_code)]
pub const STATE_UNAVAILABLE: u8 = 3;

/// Outcome of [`run`]. Plain data (not a `Result`/panic), same rationale as
/// [`crate::scenario::ScenarioResult`]: an unresolved probe is itself the finding, not a harness
/// failure, so the caller decides what to make of it.
#[derive(Debug, Default, Clone, Copy)]
pub struct RecorderProbeResult {
    /// `registerTasks()` claimed a scheduler slot within `step_timeout` (same boot-readiness
    /// signal `scenario::ScenarioResult::boot_ready` uses).
    pub boot_ready: bool,
    /// The state `deluge_harness_recorder_probe()`'s own bounded C++-side retry loop settled on
    /// (see that function's doc: with `RecordingReadSource` deleted this is expected to read
    /// LOADING uniformly, on sim as well as `host_app`).
    pub initial_state: u8,
    /// The state after this task's own poll loop (which lets the REAL async fill task run between
    /// checks, via `Timer::after` yields — the mechanism that actually owns the drain on
    /// `host_app`), bounded by `poll_window`.
    pub final_state: u8,
    /// How many `Timer::after`-yielding poll iterations ran before `final_state` was read.
    pub poll_iterations: u32,
    /// `final_state == STATE_READY` — the plain pass/fail a caller most likely wants.
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

/// Runs the probe. `poll_window` bounds how long this waits, yielding to the executor between
/// checks, for a `STATE_LOADING` result to resolve — long enough to give a background async fill
/// task (if one is actually working the request) a real chance, short enough that a genuinely
/// stuck fill (the routing-divergence finding) doesn't hang the caller forever.
pub async fn run(step_timeout: Duration, poll_window: Duration) -> RecorderProbeResult {
    let mut result = RecorderProbeResult::default();

    // Same rationale as scenario::run: don't touch C++ app state (currentSong et al.) before
    // registerTasks() has actually run.
    result.boot_ready = wait_for(step_timeout, || {
        crate::scheduler::registered_task_count() > 0
    })
    .await;
    if !result.boot_ready {
        return result;
    }

    // Small ramp (50 frames, mono), comfortably inside the first cluster (Cluster::size is 32KB;
    // 50 frames * 3 bytes = 150 bytes) so probing cluster index 0 is meaningful, and a handful of
    // C++-side drain ticks to get it flushed to the SD file before probing (mirrors the sim
    // round-trip harness's own pump pattern).
    // Set up and feed, but drain NOTHING here (0 ticks): the loop below does it with a yield between
    // ticks, which is the whole point.
    let setup = unsafe { deluge_harness_recorder_probe(1, 50, 0) };
    if setup == STATE_HARNESS_ERROR {
        result.initial_state = STATE_HARNESS_ERROR;
        return result;
    }

    // The drain, one tick per YIELD. It used to be a tight C++ loop, which only ever worked on the
    // recorder's synchronous arm: with the channel arm the storage driver gets no chance to run
    // between ticks, so nothing is answered and nothing lands. Awaiting here is what lets the op
    // complete, and it is what unblocks `Owner::run` going inline.
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

    unsafe { deluge_harness_recorder_probe_end() };
    result
}

/// Cross-thread handoff for [`recorder_probe_task`] — see its doc comment.
static RESULT: Mutex<Option<RecorderProbeResult>> = Mutex::new(None);

/// `host_app` boot-path convenience wrapper (NOT part of a reusable driver — same shape as
/// `scenario::scenario_task`). Spawn on the SAME executor `host_app_task`'s worker-fiber pump loop
/// and the async streaming-fill task run on, then poll [`take_result`] from the OS thread.
/// Embassy task args must be `Copy`; durations are passed as plain millis.
#[embassy_executor::task]
pub async fn recorder_probe_task(step_timeout_ms: u64, poll_window_ms: u64) {
    let result = run(
        Duration::from_millis(step_timeout_ms),
        Duration::from_millis(poll_window_ms),
    )
    .await;
    *RESULT.lock().unwrap() = Some(result);
}

/// Takes the result [`recorder_probe_task`] stashed, if it has finished. `None` before completion.
pub fn take_result() -> Option<RecorderProbeResult> {
    RESULT.lock().unwrap().take()
}
