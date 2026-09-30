//! The streaming-underrun scenario: load a real song, play it, record the output, and step until
//! a target number of audio blocks have rendered, counting SD traffic and underruns.
//!
//! [`run`] issues the C-ABI calls a LOAD/PLAY/RECORD press would
//! (`tests/integration/scenarios/streaming_scenario.h`) and polls what a UI would. It spawns no
//! thread and only awaits [`embassy_time::Timer`]s, so any executor can drive it. The caller boots
//! the C++ app first. [`scenario_task`] runs it on the host app executor and hands the result to
//! the OS thread through [`take_result`].
#![cfg(all(not(target_os = "none"), feature = "host_app"))]

use core::ffi::c_char;
use std::ffi::CString;
use std::sync::Mutex;

use embassy_time::{Duration, Instant, Timer};

// The host-only scenario bridge (`streaming_scenario.{h,cpp}`, archived by the build script).
// Declared by hand: it is harness surface, not part of the libdeluge headers.
unsafe extern "C" {
    fn deluge_scenario_begin_song_load(full_path: *const c_char) -> bool;
    fn deluge_scenario_song_listing_in_progress() -> bool;
    fn deluge_scenario_commit_song_load() -> bool;
    fn deluge_scenario_song_load_in_progress() -> bool;
    fn deluge_scenario_start_playback();
    fn deluge_scenario_playback_active() -> bool;
    fn deluge_scenario_start_recording() -> bool;
}

// The harness's underrun counters (`harness/streaming_underrun.{h,cpp}`), declared by hand for
// the same reason.
unsafe extern "C" {
    fn deluge_sim_underrun_wait_count() -> u64;
    fn deluge_sim_underrun_unassign_count() -> u64;
}

/// The parameters of [`run`].
#[derive(Debug, Clone, Copy)]
pub struct ScenarioConfig {
    /// The song's path on the card, e.g. `"SONGS/Cordae.XML"`.
    pub song_full_path: &'static str,
    /// The audio blocks to render after playback starts.
    pub target_blocks: u64,
    /// The bound on each wait, so a wedged app fails the scenario rather than hanging it.
    pub step_timeout: Duration,
}

/// The outcome of [`run`].
///
/// Plain data rather than a `Result`: a step that times out leaves the later fields at their
/// defaults, and the caller decides what counts as failure.
#[derive(Debug, Default, Clone, Copy)]
pub struct ScenarioResult {
    /// Boot reached `registerTasks()`, so `currentSong` exists.
    pub boot_ready: bool,
    /// `deluge_scenario_begin_song_load` returned true.
    pub song_load_dispatched: bool,
    /// The directory listing it starts completed.
    pub listing_completed: bool,
    /// `deluge_scenario_commit_song_load` dispatched.
    pub load_committed: bool,
    /// The committed load finished.
    pub load_completed: bool,
    /// `deluge_scenario_start_playback` was called.
    pub playback_started: bool,
    /// Playback was seen active after starting it.
    pub playback_confirmed_active: bool,
    /// `AudioRecorder::beginOutputRecording` returned true.
    pub recording_started: bool,
    /// Audio blocks rendered from just before playback until the target or the timeout
    /// ([`crate::audio::host::drive_count`]).
    pub blocks_rendered: u64,
    /// All SD block reads over the same window: mostly the fill task's cluster reads, plus
    /// anything else that read the card.
    pub cluster_reads: u64,
    /// All SD block writes over the same window: mostly the recorder's.
    pub recorder_writes: u64,
    /// Underruns over the same window where the audio thread found a needed cluster not yet
    /// loaded and deferred the voice. With [`Self::underrun_unassign`], the primary underrun
    /// signal.
    pub underrun_wait: u64,
    /// Underruns over the same window where the audio thread dropped the voice outright.
    pub underrun_unassign: u64,
}

/// Polls `cond` every 5 ms until it holds or `timeout` elapses; returns whether it held.
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

/// Runs the scenario. Returns at the first step that fails or times out.
pub async fn run(cfg: ScenarioConfig) -> ScenarioResult {
    let mut result = ScenarioResult::default();

    // This may be polled before the task that calls `deluge_app_init()`, so wait for boot to
    // reach `registerTasks()` before touching app state.
    result.boot_ready = wait_for(cfg.step_timeout, || {
        crate::scheduler::registered_task_count() > 0
    })
    .await;
    if !result.boot_ready {
        return result;
    }

    let path = match CString::new(cfg.song_full_path) {
        Ok(p) => p,
        Err(_) => return result, // A NUL in the path.
    };
    result.song_load_dispatched = unsafe { deluge_scenario_begin_song_load(path.as_ptr()) };
    if !result.song_load_dispatched {
        return result;
    }

    result.listing_completed = wait_for(cfg.step_timeout, || unsafe {
        !deluge_scenario_song_listing_in_progress()
    })
    .await;
    if !result.listing_completed {
        return result;
    }

    result.load_committed = unsafe { deluge_scenario_commit_song_load() };
    if !result.load_committed {
        return result;
    }
    result.load_completed = wait_for(cfg.step_timeout, || unsafe {
        !deluge_scenario_song_load_in_progress()
    })
    .await;
    if !result.load_completed {
        return result;
    }

    // Baseline after the load, so the counts cover only streaming and recording.
    let blocks_before = crate::audio::host::drive_count();
    let reads_before = crate::storage::sd::stats::reads();
    let writes_before = crate::storage::sd::stats::writes();
    let underrun_wait_before = unsafe { deluge_sim_underrun_wait_count() };
    let underrun_unassign_before = unsafe { deluge_sim_underrun_unassign_count() };

    unsafe { deluge_scenario_start_playback() };
    result.playback_started = true;
    result.playback_confirmed_active = wait_for(cfg.step_timeout, || unsafe {
        deluge_scenario_playback_active()
    })
    .await;

    result.recording_started = unsafe { deluge_scenario_start_recording() };

    // Step until the block target or the timeout; a short count is the caller's to judge.
    wait_for(cfg.step_timeout, || {
        crate::audio::host::drive_count().saturating_sub(blocks_before) >= cfg.target_blocks
    })
    .await;

    result.blocks_rendered = crate::audio::host::drive_count().saturating_sub(blocks_before);
    result.cluster_reads = crate::storage::sd::stats::reads().saturating_sub(reads_before);
    result.recorder_writes = crate::storage::sd::stats::writes().saturating_sub(writes_before);
    result.underrun_wait =
        unsafe { deluge_sim_underrun_wait_count() }.saturating_sub(underrun_wait_before);
    result.underrun_unassign =
        unsafe { deluge_sim_underrun_unassign_count() }.saturating_sub(underrun_unassign_before);

    result
}

/// The result [`scenario_task`] hands to the OS thread.
static RESULT: Mutex<Option<ScenarioResult>> = Mutex::new(None);

/// Runs [`run`] as an embassy task, for an OS thread that cannot await it: spawn this on the host
/// app executor, then poll [`take_result`].
#[embassy_executor::task]
pub async fn scenario_task(cfg: ScenarioConfig) {
    let result = run(cfg).await;
    *RESULT.lock().unwrap() = Some(result);
}

/// Takes the result [`scenario_task`] stored, or `None` before it has finished.
pub fn take_result() -> Option<ScenarioResult> {
    RESULT.lock().unwrap().take()
}
