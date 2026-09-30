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
    // The dispatched variant of `deluge_scenario_begin_song_load`, and its completion latch.
    fn deluge_scenario_start_song_load(full_path: *const c_char);
    fn deluge_scenario_song_load_begin_done() -> bool;
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
    /// `(throughput_bytes_per_sec, command_overhead_us)` for `sd::sim_latency`, applied once the
    /// song has loaded, so it slows only the real-time streaming, not the load. Read only with
    /// the `sim_latency` feature.
    #[cfg_attr(not(feature = "sim_latency"), allow(dead_code))]
    pub post_load_sim_latency: Option<(u32, u32)>,
    /// Card contention during playback: `Some(n)` starts a listing-only browse of the song
    /// about every `n` audio blocks' worth of elapsed time (0 means as often as possible),
    /// never committing it, so the browser's listing competes with the streaming reads.
    ///
    /// It uses the dispatched `deluge_scenario_start_song_load`: the synchronous
    /// `deluge_scenario_begin_song_load` does a blocking storage read that can livelock under
    /// `sim_latency`.
    pub concurrent_listing_every_blocks: Option<u64>,
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

    #[cfg(feature = "sim_latency")]
    if let Some((throughput_bps, overhead_us)) = cfg.post_load_sim_latency {
        crate::storage::sd::sim_latency::set_throughput_bytes_per_sec(throughput_bps);
        crate::storage::sd::sim_latency::set_command_overhead_us(overhead_us);
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

    // Step until the block target or the timeout; a short count is the caller's to judge. The
    // contention knob fires its listings from this same poll loop rather than a second task.
    match cfg.concurrent_listing_every_blocks {
        None => {
            wait_for(cfg.step_timeout, || {
                crate::audio::host::drive_count().saturating_sub(blocks_before) >= cfg.target_blocks
            })
            .await;
        }
        Some(every_blocks) => {
            let every_blocks = every_blocks.max(1);

            // The cadence is in nominal block periods (128 frames at 44.1 kHz) of elapsed
            // time, not `drive_count()` deltas: that counter stops advancing on an executor with
            // no audio spawner, where post-boot renders are skipped.
            const NOMINAL_BLOCK_PERIOD_US: u64 = 128 * 1_000_000 / 44_100; // 2902 (floor)
            let interval = Duration::from_micros(every_blocks * NOMINAL_BLOCK_PERIOD_US);
            let deadline = Instant::now() + cfg.step_timeout;
            let mut next_fire_at = Instant::now() + interval;
            let mut listing_in_flight = false;
            let mut listings_fired: u64 = 0;

            // A hard cap, not a tuned rate: each dispatch's `openUI(&loadSongUI)` pushes onto
            // the 16-deep `uiNavigationHierarchy` and, never committing, nothing pops it.
            // Playback start leaves the stack at depth 1, so the 16th dispatch would index out
            // of bounds and abort the process. Capped below 15 to leave margin.
            const MAX_SAFE_LISTINGS: u64 = 10;
            let mut cap_logged = false;

            loop {
                let rendered = crate::audio::host::drive_count().saturating_sub(blocks_before);
                if rendered >= cfg.target_blocks {
                    break;
                }
                let now = Instant::now();
                if now >= deadline {
                    break;
                }
                // One listing in flight at a time, as a real browse would.
                if !listing_in_flight && now >= next_fire_at {
                    if listings_fired >= MAX_SAFE_LISTINGS {
                        if !cap_logged {
                            log::warn!(
                                "streaming-scenario: concurrent_listing_every_blocks capped at \
                                 {MAX_SAFE_LISTINGS} dispatches this run — the UI navigation \
                                 stack (uiNavigationHierarchy, capacity 16) never pops while this \
                                 knob is active (never commits), so continuing would abort the \
                                 whole process; see task-4-report.md"
                            );
                            cap_logged = true;
                        }
                    } else {
                        unsafe { deluge_scenario_start_song_load(path.as_ptr()) };
                        listing_in_flight = true;
                        listings_fired += 1;
                        next_fire_at = now + interval;
                    }
                }
                if listing_in_flight && unsafe { deluge_scenario_song_load_begin_done() } {
                    listing_in_flight = false;
                }
                Timer::after_millis(5).await;
            }
            // Never return with a listing in flight.
            if listing_in_flight {
                wait_for(cfg.step_timeout, || unsafe {
                    deluge_scenario_song_load_begin_done()
                })
                .await;
            }
            log::info!(
                "streaming-scenario: concurrent_listing_every_blocks={every_blocks} \
                 (interval={interval:?}) fired {listings_fired} overlapping listing(s) \
                 during the block-target wait"
            );
        }
    }

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
