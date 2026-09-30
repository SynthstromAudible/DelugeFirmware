//! The streaming-underrun harness's reusable scenario driver: load a real song, start
//! real-time playback, start a concurrent output recording, then step until a target
//! number of audio blocks have rendered — the substrate two verification approaches assert
//! over: a deterministic virtual-time simulation and a preemptive-audio ThreadSanitizer
//! check.
//!
//! [`run`] is deliberately THREAD-AGNOSTIC: it never spawns an OS thread, never touches
//! [`crate::storage::sd::sim_latency::pump`] (that mechanism needs to spawn an escape-hatch OS
//! thread, which isn't available to a single-threaded custom executor — the shape the
//! deterministic virtual-time simulation runs on), and makes no assumption about which
//! embassy executor is driving it (`platform-std`, a future single-threaded custom
//! `raw::Executor`+`MockDriver`, ...) — it only `.await`s [`embassy_time::Timer`]s in a
//! plain poll loop, which every embassy executor variant supports identically. The caller
//! is responsible for having already booted the real C++ app (`deluge_app_init` returned)
//! on whatever executor is driving it; this module just issues the same C-ABI calls a
//! LOAD/PLAY/RECORD button press would (`scenarios/streaming_scenario.h`/`.cpp` on the C++
//! side) and polls the same observable state a UI would (`LoadSongUI::isLoadingSong()`,
//! `PlaybackHandler::isEitherClockActive()`).
//!
//! [`scenario_task`] is a thin, NOT-reusable convenience wrapper used only by this crate's
//! own `host_app` boot path (`main.rs`): it spawns [`run`] as an embassy task and stashes
//! the result in a static so the OS thread that owns that executor (which cannot `.await`
//! directly) can poll for completion. Other harnesses are expected to call [`run`]
//! directly from their own executor's task graph instead of reusing this wrapper.
#![cfg(all(not(target_os = "none"), feature = "host_app"))]

use core::ffi::c_char;
use std::ffi::CString;
use std::sync::Mutex;

use embassy_time::{Duration, Instant, Timer};

// Host-only C-ABI bridge (tests/integration/scenarios/streaming_scenario.{h,cpp}, compiled
// into the host tree's `deluge_scenarios` objects that build.rs archives; the device firmware
// never contains it). Manually declared here (not through the bindgen `sys` module, same
// pattern as `deluge_app_init` in `main.rs`) since these are harness-only entry points the
// app exposes to the platform, not part of the libdeluge C-ABI the app CONSUMES.
unsafe extern "C" {
    fn deluge_scenario_begin_song_load(full_path: *const c_char) -> bool;
    fn deluge_scenario_song_listing_in_progress() -> bool;
    fn deluge_scenario_commit_song_load() -> bool;
    fn deluge_scenario_song_load_in_progress() -> bool;
    fn deluge_scenario_start_playback();
    fn deluge_scenario_playback_active() -> bool;
    fn deluge_scenario_start_recording() -> bool;
    // Task-context contention knob: the DISPATCHED variant of
    // `deluge_scenario_begin_song_load`, plus its completion latch. See
    // `ScenarioConfig::concurrent_listing_every_blocks`'s doc comment for why the plain
    // (synchronous) `deluge_scenario_begin_song_load` isn't safe to reuse for this
    // repeated-during-playback call pattern.
    fn deluge_scenario_start_song_load(full_path: *const c_char);
    fn deluge_scenario_song_load_begin_done() -> bool;
}

// Streaming-underrun harness: C-ABI reader for `harness/streaming_underrun.{h,cpp}`'s
// sim-only `loaded`-miss counters — the harness's PRIMARY underrun signal (the audio thread
// discovering a needed sample cluster isn't loaded yet, on a play-needed path, and deferring
// or dropping the voice as a result). Same manual-declaration pattern as the block above:
// harness-only entry points the app exposes to the platform, not part of the libdeluge C-ABI
// the app CONSUMES, so not run through the bindgen `sys` module.
unsafe extern "C" {
    fn deluge_sim_underrun_wait_count() -> u64;
    fn deluge_sim_underrun_unassign_count() -> u64;
}

/// Parameters for [`run`]. `Copy` so [`scenario_task`] can take it by value (embassy task
/// arguments must be owned).
#[derive(Debug, Clone, Copy)]
pub struct ScenarioConfig {
    /// Song path within the card image, e.g. `"SONGS/Cordae.XML"` (same shape
    /// `Song::setSongFullPath` expects).
    pub song_full_path: &'static str,
    /// How many ADDITIONAL audio blocks to render (beyond the baseline sampled right
    /// before playback starts) before the scenario is considered done.
    pub target_blocks: u64,
    /// Bound on each polling wait (listing / load-commit / playback-start / block target)
    /// so a wedged app fails the scenario instead of hanging the caller forever.
    pub step_timeout: Duration,
    /// Sanity-check / negative-control knob: `(throughput_bytes_per_sec,
    /// command_overhead_us)` applied to `sd::sim_latency` right after `load_completed`
    /// succeeds and BEFORE the pre-playback baseline is snapshotted — i.e. it stresses only
    /// the phase the underrun counters care about (sustained real-time streaming), not the
    /// song LOAD's own essential-sample reads (which have no real-time deadline and would
    /// otherwise need an implausibly large `step_timeout` to survive an "absurd" value).
    /// `None` leaves whatever `sim_latency` throughput/overhead was already in effect
    /// untouched. No effect unless the `sim_latency` feature is enabled (the field still
    /// exists without it, so `ScenarioConfig` doesn't need a feature-gated shape — it's
    /// simply never read).
    #[cfg_attr(not(feature = "sim_latency"), allow(dead_code))]
    pub post_load_sim_latency: Option<(u32, u32)>,
    /// Task-context contention knob: task-context file I/O overlapping sustained sample
    /// streaming, so a caller can compare `run()`'s behaviour with and without that
    /// contention. `None` (the default) leaves the final block-target wait a plain
    /// `wait_for`, with no concurrent listing.
    ///
    /// `Some(n)` fires a listing-only browse of [`ScenarioConfig::song_full_path`] roughly
    /// every `n` audio blocks' worth of elapsed time during that same wait, via the
    /// DISPATCHED `deluge_scenario_start_song_load`/`deluge_scenario_song_load_begin_done()`
    /// pair — never `deluge_scenario_commit_song_load()` — so the load browser's async
    /// directory listing repeatedly contends for the card with the real
    /// sample-streaming reads, without ever replacing the song this scenario is
    /// playing back (committing would end the very playback the block-target wait is
    /// measuring). This mirrors a real hazard — loading a preset while a sample streams —
    /// rather than a synthetic probe: `deluge_scenario_begin_song_load`
    /// itself (the plain, synchronous entry point the pre-existing steps above use) is NOT
    /// reused here — its `openUI(&loadSongUI)` -> `opened()` chain does a real blocking storage
    /// read, which under `sim_latency` can livelock its caller (see `streaming_scenario.h`'s doc comment on
    /// `deluge_scenario_start_song_load`).
    ///
    /// "Roughly `n` blocks" — NOT `n` [`crate::audio::host::drive_count`] deltas, unlike
    /// `target_blocks` above: see the call site's comment for why (that counter is a
    /// permanently-flat "sim constant" on a single-executor harness that skips post-boot
    /// renders, making literal block-count gating fire zero listings there). Measured
    /// instead as `n` nominal audio-block periods of elapsed virtual time.
    ///
    /// `n == 0` is treated as "fire as fast as possible" (never as "never fire") — see the
    /// call site.
    pub concurrent_listing_every_blocks: Option<u64>,
}

/// Outcome of [`run`]. Deliberately plain data, not a `Result`/panic: a step that times
/// out just leaves the later fields at their default and `run` returns early — the caller
/// decides what counts as pass/fail (matters for NEGATIVE-control scenarios, which need to
/// observe "the harness correctly detects failure", not just "it panicked").
#[derive(Debug, Default, Clone, Copy)]
pub struct ScenarioResult {
    /// The app finished enough of boot (`registerTasks()` claimed at least one scheduler
    /// slot — the same signal `main.rs`'s own boot-OK check uses) for `currentSong` to
    /// exist. `run` does not itself require the caller to have already confirmed boot;
    /// this field records that `run` waited it out itself.
    pub boot_ready: bool,
    /// `deluge_scenario_begin_song_load` returned true (currentSong existed to target).
    pub song_load_dispatched: bool,
    /// The async directory listing it triggers completed within `step_timeout`.
    pub listing_completed: bool,
    /// `deluge_scenario_commit_song_load` dispatched.
    pub load_committed: bool,
    /// The committed load (`LoadSongUI::performLoad`) finished within `step_timeout`.
    pub load_completed: bool,
    /// `deluge_scenario_start_playback` was called.
    pub playback_started: bool,
    /// Playback was observed active (`PlaybackHandler::isEitherClockActive`) within
    /// `step_timeout` of starting it.
    pub playback_confirmed_active: bool,
    /// `AudioRecorder::beginOutputRecording` returned true.
    pub recording_started: bool,
    /// Audio blocks rendered from just before playback started to when the target was
    /// reached (or the timeout gave up) — see `crate::audio::host::drive_count()`.
    pub blocks_rendered: u64,
    /// ALL SD block reads observed over the same window (`crate::storage::sd::stats::reads()`):
    /// mostly the async fill task's cluster reads for this song's samples, plus anything
    /// else that read the card meanwhile (e.g. the `concurrent_listing_every_blocks`
    /// browses).
    pub cluster_reads: u64,
    /// ALL SD block writes observed over the same window (`crate::storage::sd::stats::writes()`),
    /// mostly the recorder's card writes.
    pub recorder_writes: u64,
    /// WAIT-class underrun misses over the same window: the audio thread found a
    /// needed sample cluster not loaded yet on a play-needed path and deferred the voice
    /// rather than dropping it (`deluge_sim_underrun_wait_count()`, baseline-subtracted).
    /// This — together with `underrun_unassign` — is the harness's PRIMARY underrun signal,
    /// the one both the timing simulation and the concurrency check assert on.
    pub underrun_wait: u64,
    /// UNASSIGN-class underrun misses over the same window: the audio thread found a
    /// needed sample cluster not loaded and dropped the voice outright
    /// (`deluge_sim_underrun_unassign_count()`, baseline-subtracted) — a harder failure than a
    /// WAIT deferral.
    pub underrun_unassign: u64,
}

/// Polls `cond` every 5ms (yielding to the executor between polls via
/// [`embassy_time::Timer`] — thread-agnostic, works under any embassy executor) until it
/// returns true or `timeout` elapses. Returns whether `cond` was satisfied.
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

/// Runs the scenario against `cfg`. See the module doc for the thread-agnostic contract.
/// Returns as soon as a step fails/times out (with the fields up to that point populated
/// honestly) rather than pressing on into a state the harness can no longer trust.
pub async fn run(cfg: ScenarioConfig) -> ScenarioResult {
    let mut result = ScenarioResult::default();

    // `run` may be spawned onto the same executor as (and concurrently with, from the
    // scheduler's point of view) the task that itself calls `deluge_app_init()` — embassy
    // doesn't guarantee poll order across tasks spawned in the same batch, so wait for
    // boot to actually reach `registerTasks()` (same signal `main.rs`'s own boot-OK check
    // uses) before touching any C++ app state (`currentSong` doesn't exist before then).
    result.boot_ready = wait_for(cfg.step_timeout, || {
        crate::scheduler::registered_task_count() > 0
    })
    .await;
    if !result.boot_ready {
        return result;
    }

    let path = match CString::new(cfg.song_full_path) {
        Ok(p) => p,
        Err(_) => return result, // NUL in the path — caller bug, report honestly rather than UB
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

    // Sanity-check / negative-control knob: apply the requested `sim_latency`
    // override HERE — load just finished (under whatever latency was already in effect), so
    // this stresses only the sustained real-time streaming the underrun counters care about,
    // not load's own essential-sample reads (see `ScenarioConfig::post_load_sim_latency`'s
    // doc comment).
    #[cfg(feature = "sim_latency")]
    if let Some((throughput_bps, overhead_us)) = cfg.post_load_sim_latency {
        crate::storage::sd::sim_latency::set_throughput_bytes_per_sec(throughput_bps);
        crate::storage::sd::sim_latency::set_command_overhead_us(overhead_us);
    }

    // Baseline right before starting playback/recording — everything counted from here on
    // is genuinely attributable to THIS run's streaming/recording, not the song load
    // itself (which also issues SD reads for its essential-sample clusters).
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

    // Step until the target block count or the timeout — either way, report exactly what
    // happened (the caller decides whether a short-of-target count is a failure).
    //
    // `concurrent_listing_every_blocks == None` takes the ORIGINAL single `wait_for` path,
    // unchanged — required so the knob is byte-identical to before it existed (see the
    // field's doc comment). `Some(n)` takes a loop that both checks the block target and,
    // on its own cadence (see the `Some` arm below for what that cadence actually is and
    // why), fires an overlapping listing-only browse in-loop (chosen over a
    // separately-spawned concurrent task: `run` is deliberately thread/executor-agnostic
    // per the module doc, and folding the listing into this same poll loop keeps that
    // property — no second task, no extra spawn-cleanup path — while still firing WHILE
    // the block-target wait is in flight, which is what "overlap" requires).
    match cfg.concurrent_listing_every_blocks {
        None => {
            wait_for(cfg.step_timeout, || {
                crate::audio::host::drive_count().saturating_sub(blocks_before) >= cfg.target_blocks
            })
            .await;
        }
        Some(every_blocks) => {
            // 0 means "every block", not "never" — a caller passing 0 almost certainly
            // wants maximum contention, not the knob silently degrading to off.
            let every_blocks = every_blocks.max(1);

            // DEVIATION FROM THE LITERAL BRIEF, recorded here (and in task-4-report.md)
            // rather than silently: the brief's own wording gates firing on
            // `audio_host::drive_count()` deltas ("rendered" blocks) — the SAME signal
            // `target_blocks` itself uses just above. Empirically, that counter is a
            // permanently-flat "sim constant" on a single-executor harness: `audio_host.rs`'s
            // own module doc says host_app's SEPARATE "deluge-audio" OS thread is what
            // `scheduler::set_audio_spawner` needs for `should_skip_render()` to stop
            // discarding renders, and a single-executor harness never calls that -- every
            // render after boot finishes is
            // silently discarded (returns 0, no `DRIVE_COUNT` increment). Verified live:
            // across every configuration tried, `drive_count()` reaches exactly 1 (the
            // one pre-registration render `audio_host.rs:235-241` logs) and never
            // advances again for the rest of the process. Gating firing on it here would
            // make this knob fire ZERO listings, unconditionally, under the only harness
            // wired to turn it on — dead on arrival for the very sweep this exists to
            // feed. So "N blocks" is instead measured as N nominal Deluge audio block
            // periods (128 frames @ 44.1kHz ~= 2902us) of
            // ELAPSED VIRTUAL TIME since this wait began — preserving the "every N
            // blocks' worth of streaming time" cadence intent without depending on a
            // counter that's dead in this harness. `target_blocks` itself (the loop's
            // exit condition, above/below) is UNCHANGED — still `drive_count()`-based,
            // per the brief and the pre-existing `None` path, so it inherits that same
            // pre-existing timeout-not-satisfied behaviour (see the report).
            const NOMINAL_BLOCK_PERIOD_US: u64 = 128 * 1_000_000 / 44_100; // 2902 (floor)
            let interval = Duration::from_micros(every_blocks * NOMINAL_BLOCK_PERIOD_US);
            let deadline = Instant::now() + cfg.step_timeout;
            let mut next_fire_at = Instant::now() + interval;
            let mut listing_in_flight = false;
            let mut listings_fired: u64 = 0;

            // HARD SAFETY CAP — a REAL, DELIBERATELY-SURFACED finding (see task-4-report.md),
            // not a tuned rate: `deluge_scenario_begin_song_load` -> `openUI(&loadSongUI)`
            // (`ui.cpp`) unconditionally PUSHES onto `uiNavigationHierarchy`
            // (`std::array<UI*, 16>`, `ui.cpp:60`) with no idempotency check, and this
            // knob's whole design (Kate's own call: reuse begin_song_load, NEVER commit)
            // means nothing ever pops it back off. `deluge_scenario_start_playback` resets
            // the stack to depth 1 (`changeRootUI`, `ui.cpp:106-109`) right before this
            // wait begins, so this knob gets a HARD, per-run ceiling of `16 - 1 = 15` total
            // dispatches before the 16th indexes the array out of bounds and the WHOLE
            // PROCESS aborts (`std::array::operator[]`'s bounds assertion) — verified live:
            // every configuration tried that fired > 15 listings crashed mid-run, producing
            // no result line at all (worse than an honest short-of-target report).
            // Capped well under that ceiling (not at 15) so a stray extra push from
            // elsewhere in the UI stack still can't tip it over.
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
                // Never fire a second listing while one is still in flight — this
                // scenario measures contention from a realistic browse cadence, not from
                // an unbounded pile-up of dispatches the card couldn't have
                // served anyway.
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
            // Never leave a listing in flight when `run` returns (required property) —
            // await the final dispatch's completion, bounded by the same `step_timeout`
            // every other step in this function uses.
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

/// Cross-thread handoff for [`scenario_task`] — see its doc comment.
static RESULT: Mutex<Option<ScenarioResult>> = Mutex::new(None);

/// NOT part of the reusable driver (see module doc) — a convenience wrapper for THIS
/// crate's own `host_app` boot path (`main.rs`), which owns the host-app executor from a
/// plain OS thread and can't `.await` [`run`] directly. Spawn this as an embassy task on
/// the SAME executor `host_app_task` runs on, then poll
/// [`take_result`] from the OS thread.
#[embassy_executor::task]
pub async fn scenario_task(cfg: ScenarioConfig) {
    let result = run(cfg).await;
    *RESULT.lock().unwrap() = Some(result);
}

/// Takes the result [`scenario_task`] stashed, if it has finished. `None` before
/// completion.
pub fn take_result() -> Option<ScenarioResult> {
    RESULT.lock().unwrap().take()
}
