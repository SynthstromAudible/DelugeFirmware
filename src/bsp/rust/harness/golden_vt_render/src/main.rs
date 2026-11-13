//! `golden_vt_render`: boots the real C++ app on the BSP under a virtual clock, fills the card with
//! a fixture tree, and runs one scenario deterministically.
//!
//! `GOLDEN_SCENARIO` names a row of `tests/integration/scenarios/scenario_table.cpp`: a stepped scenario, the
//! recorder round trip, or `stem_export`, which `GOLDEN_SCENARIO` selects by name or by being unset or empty: an
//! offline `StemExport` of the fixture song whose stem WAVs it copies out. Sample cluster fills drain through the BSP's
//! async `streaming_fill_task`, so a sample-backed fixture renders real audio.
//!
//! # Why a separate package
//!
//! The executor and time driver here would collide with the embassy std ones the BSP's
//! `host-runtime` feature brings (duplicate `__pender` and time-driver symbols), and Cargo unifies
//! features across a package, so this cannot be a binary of the BSP's package.
//!
//! # Executor and clock
//!
//! A `raw::Executor` with an `AtomicBool` [`pender`] and the virtual clock in [`clock`]. The loop
//! in `main` polls the executor to quiescence, jumps the clock to the next due deadline, and
//! repeats.
//!
//! Card transfers complete synchronously here. Where a gate needs storage time, the async fill
//! task models it (`deluge_streaming_set_fill_*`).
//!
//! # The card and the pacing come from the scenario's row, before any task is spawned
//!
//! Each row carries its card (`ScenarioCard`: empty, the corpus fixture, a directory an environment variable
//! names, a card manifest, or one of [`BUILDERS`]) and its pacing (`ScenarioPacing`). `main` applies both before the
//! executor exists. The BSP's host SD opens its disk once, lazily, on first access, and embassy does not order the
//! first polls of tasks spawned together, so a card installed from a task could lose to [`boot_task`]'s SD bring-up
//! and open the wrong backing, silently.
//!
//! `GOLDEN_CARD_ONLY=1` installs the scenario's card and exits before anything runs, so `GOLDEN_SD_DUMP=<path>`
//! writes out exactly the card the scenario boots with.
//!
//! A corpus-fixture card is installed only for a run that declares the corpus (`DELUGE_CORPUS_DECLARED=1`, which
//! `./dbt test` sets for an entry that requires `song-corpus`); see [`corpus_fixture`].
#![feature(impl_trait_in_assoc_type)]
#![recursion_limit = "512"]

use core::ffi::c_char;
use core::sync::atomic::{AtomicBool, Ordering};
use std::ffi::{CStr, CString};
use std::path::{Path, PathBuf};

use embassy_executor::raw;
use embassy_time::{Duration, Instant, Timer};
use embassy_time_driver::Driver as _;

mod clock;

use deluge_bsp_rust::audio::host as audio_host;
use deluge_bsp_rust::audio::render;
use deluge_bsp_rust::channel::{
    audio_loader, file_bytes, file_directory, file_prefix, file_stream,
};
use deluge_bsp_rust::deluge_bsp;
use deluge_bsp_rust::peripherals::{board, control, display};
use deluge_bsp_rust::storage::efatfs::host as efatfs_host;
use deluge_bsp_rust::storage::{card_manifest, memory_card, sd, sd_image, streaming_loader};
use deluge_bsp_rust::{app_loop, boot, sys, tier_meter};

/// How a scenario's audio is paced: `ScenarioPacing` in `scenario_table.h`, in its order.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Pacing {
    Virtual = 0,
    Realtime = 1,
    DevicePeriod = 2,
}

impl Pacing {
    /// The pacing a row's byte names. The row's field is a byte, not this enum: a value outside the enum's range
    /// would be undefined behaviour in a Rust enum.
    fn from_byte(byte: u8) -> Self {
        [Self::Virtual, Self::Realtime, Self::DevicePeriod]
            .into_iter()
            .find(|pacing| *pacing as u8 == byte)
            .unwrap_or_else(|| panic!("golden_vt_render: pacing {byte} is not a ScenarioPacing"))
    }

    /// Sets the render up for this pacing. Called before the render task is spawned.
    fn apply(self) {
        match self {
            Self::Virtual => {}
            Self::Realtime => audio_host::set_realtime_render(true),
            Self::DevicePeriod => {
                audio_host::set_realtime_render(true);
                render::set_period_override_us(2902, 0);
            }
        }
    }
}

/// `ScenarioCard::Kind` in `scenario_table.h`, in its order.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum CardKind {
    Empty = 0,
    CorpusFixture = 1,
    FromEnv = 2,
    Manifest = 3,
    Builder = 4,
}

/// `ScenarioCard`: its kind as a byte, and its argument (null for `Empty` and `CorpusFixture`).
#[repr(C)]
struct CardSpec {
    kind: u8,
    arg: *const c_char,
}

/// A row's card, decoded.
enum Card {
    Empty,
    CorpusFixture,
    FromEnv {
        var: &'static str,
        fallback: Option<&'static str>,
    },
    Manifest(&'static str),
    Builder(&'static str),
}

/// A row of `tests/integration/scenarios/scenario_table.cpp`, as `scenario_table.h` declares it.
#[repr(C)]
struct Scenario {
    name: *const c_char,
    on_app_init: Option<unsafe extern "C" fn()>,
    begin: Option<unsafe extern "C" fn()>,
    complete: Option<unsafe extern "C" fn() -> bool>,
    dispatch_step: Option<unsafe extern "C" fn()>,
    step_done: Option<unsafe extern "C" fn() -> bool>,
    failures: Option<unsafe extern "C" fn() -> i32>,
    pacing: u8,
    card: CardSpec,
}

// scenario_table.cpp asserts the same offsets.
const _: () = assert!(core::mem::offset_of!(Scenario, pacing) == 7 * core::mem::size_of::<usize>());
const _: () = assert!(core::mem::offset_of!(Scenario, card) == 8 * core::mem::size_of::<usize>());

// SAFETY: a row of a C++ `constexpr` table: immutable for the life of the process.
unsafe impl Sync for Scenario {}

/// A stepped scenario's five functions, driven by [`run_stepped_scenario`].
#[derive(Clone, Copy)]
struct Steps {
    begin: unsafe extern "C" fn(),
    complete: unsafe extern "C" fn() -> bool,
    dispatch_step: unsafe extern "C" fn(),
    step_done: unsafe extern "C" fn() -> bool,
    failures: unsafe extern "C" fn() -> i32,
}

impl Scenario {
    /// The row's steps, or `None` for a row the harness drives itself. The table asserts a row has all five or none.
    fn steps(&self) -> Option<Steps> {
        Some(Steps {
            begin: self.begin?,
            complete: self.complete?,
            dispatch_step: self.dispatch_step?,
            step_done: self.step_done?,
            failures: self.failures?,
        })
    }

    fn pacing(&self) -> Pacing {
        Pacing::from_byte(self.pacing)
    }

    fn card(&self) -> Card {
        let kind = [
            CardKind::Empty,
            CardKind::CorpusFixture,
            CardKind::FromEnv,
            CardKind::Manifest,
            CardKind::Builder,
        ]
        .into_iter()
        .find(|kind| *kind as u8 == self.card.kind)
        .unwrap_or_else(|| {
            panic!(
                "golden_vt_render: card kind {} is not a ScenarioCard::Kind",
                self.card.kind
            )
        });
        let arg = || -> &'static str {
            assert!(
                !self.card.arg.is_null(),
                "golden_vt_render: a {kind:?} card needs its argument"
            );
            // SAFETY: a string literal in a C++ `constexpr` table: NUL-terminated and static.
            unsafe { CStr::from_ptr(self.card.arg) }
                .to_str()
                .expect("golden_vt_render: a card argument is UTF-8")
        };
        match kind {
            CardKind::Empty => Card::Empty,
            CardKind::CorpusFixture => Card::CorpusFixture,
            CardKind::FromEnv => match arg().split_once('|') {
                Some((var, fallback)) => Card::FromEnv {
                    var,
                    fallback: Some(fallback),
                },
                None => Card::FromEnv {
                    var: arg(),
                    fallback: None,
                },
            },
            CardKind::Manifest => Card::Manifest(arg()),
            CardKind::Builder => Card::Builder(arg()),
        }
    }
}

/// The scenario this run plays: GOLDEN_SCENARIO, or `stem_export` when it is unset or empty.
fn scenario_name() -> String {
    std::env::var("GOLDEN_SCENARIO")
        .ok()
        .filter(|name| !name.is_empty())
        .unwrap_or_else(|| "stem_export".to_string())
}

/// The table row named `name`, if there is one.
fn table_row(name: &str) -> Option<&'static Scenario> {
    let name = CString::new(name).ok()?;
    // SAFETY: host scenario C-ABI; `name` is NUL-terminated and outlives the call, and the table the answer points
    // into is static.
    unsafe { deluge_scenario_find(name.as_ptr()).as_ref() }
}

// The host scenarios' C ABI (`tests/integration/scenarios/`), linked from the host tree; the device
// firmware never contains it. Declared by hand: these are harness entry points, not part of the
// libdeluge ABI that bindgen covers.
unsafe extern "C" {
    fn deluge_scenario_start_song_load(full_path: *const c_char);
    fn deluge_scenario_song_load_begin_done() -> bool;
    fn deluge_scenario_song_load_begin_ok() -> bool;
    fn deluge_scenario_song_listing_in_progress() -> bool;
    fn deluge_scenario_commit_song_load() -> bool;
    fn deluge_scenario_song_load_in_progress() -> bool;
    fn deluge_scenario_start_stem_export(mode: i32);
    fn deluge_scenario_stem_export_done() -> bool;
    fn deluge_scenario_copy_stems_out(out_dir: *const c_char);
    fn deluge_scenario_run_recorder_roundtrip() -> i32;
    fn deluge_scenario_recorder_cases_tick() -> i32;
    fn deluge_scenario_recorder_finalized_begin();
    fn deluge_scenario_recorder_finalized_tick() -> i32;
    fn deluge_scenario_recorder_rest_begin();
    fn deluge_scenario_recorder_rest_tick() -> i32;
    fn deluge_scenario_recorder_roundtrip_failures() -> i32;
    fn deluge_scenario_ticked_begin(mode: i32);
    fn deluge_scenario_ticked_tick() -> i32;
    fn deluge_scenario_ticked_report();

    fn deluge_unguarded_structural_edits_total() -> u32;
    fn deluge_scenario_find(name: *const c_char) -> *const Scenario;
}

/// Set once [`boot_task`] has mounted the filesystem, run `deluge_app_init` and marked boot
/// finished. Every scenario task waits on it.
static BOOT_MOUNTED: AtomicBool = AtomicBool::new(false);

/// Boots the app in the host runtime's order — PIC handshake, SD bring-up, filesystem mount,
/// `deluge_app_init`, boot marked finished — with this harness's scenario hooks, then parks.
#[embassy_executor::task]
async fn boot_task() {
    deluge_bsp::pic::wait_ready().await;
    sd::boot_init().await;

    // A failed mount does not abort boot, as on the device: streamed samples just won't load.
    match efatfs_host::mount().await {
        Ok(()) => log::info!("golden_vt_render: efatfs mounted"),
        Err(status) => {
            log::warn!(
                "golden_vt_render: efatfs mount failed ({status}) — streamed samples will not load"
            );
        }
    }

    // `stem_export`, which GOLDEN_SCENARIO selects by name or by being unset or empty, needs no boot settings, and
    // the settings prelude's reads (and its write when MIDI_FOLLOW.XML is absent) would occupy the single-slot byte
    // channel the export's folder search uses. Scenarios keep the prelude, which exercises it.
    // Must precede deluge_app_init, which registers the startup job.
    if scenario_name() == "stem_export" {
        // SAFETY: plain store to a C++ global, before deluge_app_init reads it.
        unsafe { sys::deluge_startup_skip_settings_prelude() };
    }

    log::info!("golden_vt_render: deluge_app_init() (registers the app's jobs)");
    // SAFETY: called once, before anything else touches the app state.
    unsafe { sys::deluge_app_init(board::deluge_board()) };
    // A stepped scenario's hook for this moment: boot has not been marked finished, so neither the
    // app loop nor the render has run yet.
    if let Some(on_app_init) = table_row(&scenario_name()).and_then(|scenario| scenario.on_app_init)
    {
        // SAFETY: host scenario C ABI through the table row, after deluge_app_init.
        unsafe { on_app_init() };
    }
    boot::mark_finished();
    BOOT_MOUNTED.store(true, Ordering::Release);
    log::info!("golden_vt_render: boot+mount complete; app loop running");

    // The app loop runs the app's jobs from here on.
    core::future::pending::<()>().await
}

/// Returns the card path of `fixture`'s song (the fixture tree flattens it to `SONGS/<name>.XML`),
/// or `GOLDEN_STEM_SONG` when set.
///
/// # Panics
///
/// Panics for a fixture not in the table when `GOLDEN_STEM_SONG` is unset.
fn fixture_song_path(fixture: &str) -> String {
    if let Ok(path) = std::env::var("GOLDEN_STEM_SONG") {
        return path;
    }
    match fixture {
        "cordae" => "SONGS/Cordae.XML",
        "highsiderr" => "SONGS/06 Ilove Techno Song_Final_2.XML",
        "icoustic" => "SONGS/Grunnmur 58 Nobass 14 Comm-Test.XML",
        other => panic!(
            "golden_vt_render: no known SONGS/*.XML path for fixture '{other}' — set GOLDEN_STEM_SONG"
        ),
    }
    .to_string()
}

/// Returns the `StemExportType` `GOLDEN_STEM_MODE` names, as the C++ enum's value (CLIP=0, TRACK=1,
/// DRUM=2, MIXDOWN=3 in `definitions_cxx.hpp`); defaults to TRACK.
fn stem_export_mode() -> i32 {
    match std::env::var("GOLDEN_STEM_MODE")
        .unwrap_or_default()
        .to_uppercase()
        .as_str()
    {
        "CLIP" => 0,
        "DRUM" => 2,
        "MIXDOWN" => 3,
        _ => 1, // TRACK
    }
}

/// Drives a stepped scenario (`scenario_table.h`): waits for boot, calls `begin`, then one
/// `dispatch_step` per virtual millisecond, each waited out until `step_done`, until `complete`.
/// Exits 1 on in-process failures, 2 on a wedge.
#[embassy_executor::task]
async fn run_stepped_scenario(steps: Steps, name: &'static str, done: &'static AtomicBool) {
    log::info!("golden_vt_render: {name}: awaiting boot+mount");
    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        if BOOT_MOUNTED.load(Ordering::Acquire) {
            break;
        }
        if Instant::now() >= deadline {
            log::error!("golden_vt_render: boot+mount did not complete within 30s — wedged");
            hard_exit(2);
        }
        Timer::after_millis(5).await;
    }

    // SAFETY (every call below): host scenario C ABI through the scenario's table row; boot has run
    // `deluge_app_init`.
    unsafe { (steps.begin)() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        if unsafe { (steps.complete)() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!("golden_vt_render: {name} did not complete within 600s — wedged");
            hard_exit(2);
        }
        unsafe { (steps.dispatch_step)() };
        // One virtual millisecond per step before asking whether it is done: the gates' tick
        // counts and races are paced in these milliseconds.
        Timer::after_millis(1).await;
        while !unsafe { (steps.step_done)() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    let failures = unsafe { (steps.failures)() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: {name} FAILED ({failures} in-process failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: {name} completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The default scenario: waits for boot, loads `fixture`'s song, runs an offline `StemExport`,
/// and copies the stem WAVs out to `GOLDEN_STEM_OUT` (default `golden_stems`).
///
/// The card already holds the fixture (`main` installed it); `fixture` picks the song and labels
/// the log. `StemExport::renderWait` waits pass by pass for `streaming_fill_task` to drain the
/// loader queue, so a sample-backed fixture exports real audio.
#[embassy_executor::task]
async fn run_stem_export_scenario(fixture: &'static str, done: &'static AtomicBool) {
    log::info!(
        "golden_vt_render: run_stem_export_scenario: fixture={fixture}, awaiting boot+mount"
    );
    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        if BOOT_MOUNTED.load(Ordering::Acquire) {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: boot+mount did not complete within the 30s wait budget — wedged"
            );
            hard_exit(2);
        }
        Timer::after_millis(5).await;
    }

    // StemExport exports `currentSong`, which boot leaves blank. The load is two-phase (see
    // `streaming_scenario.h`): the begin only dispatches the async directory listing.
    let song_path = fixture_song_path(fixture);
    log::info!("golden_vt_render: boot+mount confirmed for fixture={fixture}; loading {song_path}");
    let song_path_c =
        CString::new(song_path.clone()).expect("fixture_song_path never contains a NUL byte");
    // SAFETY: host scenario C ABI; boot has run `deluge_app_init`, and `song_path_c` outlives the
    // call.
    unsafe { deluge_scenario_start_song_load(song_path_c.as_ptr()) };

    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_song_load_begin_done() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: song-load dispatch did not run within the 30s wait budget — wedged"
            );
            hard_exit(2);
        }
        Timer::after_millis(5).await;
    }
    // SAFETY: as above.
    if !unsafe { deluge_scenario_song_load_begin_ok() } {
        log::error!(
            "golden_vt_render: deluge_scenario_begin_song_load('{song_path}') returned false"
        );
        hard_exit(2);
    }

    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        // SAFETY: as above.
        if !unsafe { deluge_scenario_song_listing_in_progress() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: song listing did not complete within the 30s wait budget — wedged"
            );
            hard_exit(2);
        }
        Timer::after_millis(5).await;
    }

    // SAFETY: as above.
    let committed = unsafe { deluge_scenario_commit_song_load() };
    if !committed {
        log::error!(
            "golden_vt_render: deluge_scenario_commit_song_load() returned false (the load was refused)"
        );
        hard_exit(2);
    }

    let deadline = Instant::now() + Duration::from_secs(60);
    loop {
        // SAFETY: as above.
        if !unsafe { deluge_scenario_song_load_in_progress() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: song load did not complete within the 60s wait budget — wedged"
            );
            hard_exit(2);
        }
        Timer::after_millis(5).await;
    }
    log::info!("golden_vt_render: song load complete for fixture={fixture}");

    let mode = stem_export_mode();
    log::info!("golden_vt_render: dispatching stem export mode={mode}");
    // SAFETY: host scenario C ABI; the song load has completed. This only starts the export.
    unsafe { deluge_scenario_start_stem_export(mode) };

    // Generous: this budget covers the export's render time.
    let deadline = Instant::now() + Duration::from_secs(300);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_stem_export_done() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: stem export did not complete within the 300s wait budget — wedged"
            );
            hard_exit(2);
        }
        Timer::after_millis(5).await;
    }
    log::info!("golden_vt_render: stem export complete for fixture={fixture}; copying stems out");

    let out_dir = std::env::var("GOLDEN_STEM_OUT").unwrap_or_else(|_| "golden_stems".to_string());
    match CString::new(out_dir.clone()) {
        Ok(c_out_dir) => {
            // SAFETY: as above; `c_out_dir` outlives the call.
            unsafe { deluge_scenario_copy_stems_out(c_out_dir.as_ptr()) };
        }
        Err(_) => {
            log::error!("golden_vt_render: GOLDEN_STEM_OUT ({out_dir:?}) contains a NUL byte");
            hard_exit(2);
        }
    }

    log::info!("golden_vt_render: stems copied to {out_dir}; exiting cleanly");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=recorder_roundtrip` scenario: the SampleRecorder byte-exact round-trip
/// oracle, then the ticked recording cases.
///
/// Waits for boot, sets the oracle up, then ticks its cases, the finalized probe and the tail one
/// step per await, and exits with the result. The card starts empty: the recorder only writes new
/// files, and the finalized probe reads back what it wrote.
#[embassy_executor::task]
async fn run_recorder_roundtrip_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_recorder_roundtrip_scenario: awaiting boot+mount");
    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        if BOOT_MOUNTED.load(Ordering::Acquire) {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: boot+mount did not complete within the 30s wait budget — wedged"
            );
            hard_exit(2);
        }
        Timer::after_millis(5).await;
    }

    log::info!("golden_vt_render: boot+mount confirmed; starting the recorder round-trip");
    // SAFETY: host scenario C ABI; boot has run `deluge_app_init`. Set-up only: it blocks on
    // nothing.
    unsafe { deluge_scenario_run_recorder_roundtrip() };

    // One case step per tick with a yield between, so the storage channel can answer: a caller
    // that does not return cannot be answered.
    let case_deadline = Instant::now() + Duration::from_secs(300);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_recorder_cases_tick() } != 0 {
            break;
        }
        if Instant::now() >= case_deadline {
            log::error!("golden_vt_render: recorder cases did not finish within 300s — wedged");
            hard_exit(2);
        }
        Timer::after_millis(1).await;
    }

    // The finalized multi-cluster probe, ticked the same way and for the same reason.
    // SAFETY: as above.
    unsafe { deluge_scenario_recorder_finalized_begin() };
    let fin_deadline = Instant::now() + Duration::from_secs(300);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_recorder_finalized_tick() } != 0 {
            break;
        }
        if Instant::now() >= fin_deadline {
            log::error!("golden_vt_render: finalized probe did not settle within 300s — wedged");
            hard_exit(2);
        }
        Timer::after_millis(1).await;
    }

    // The oracle's tail — the live-readback probe, the finalized probe's acquire, the summary —
    // ticked the same way: its drains are requests, answered between these calls.
    // SAFETY: as above.
    unsafe { deluge_scenario_recorder_rest_begin() };
    let rest_deadline = Instant::now() + Duration::from_secs(300);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_recorder_rest_tick() } != 0 {
            break;
        }
        if Instant::now() >= rest_deadline {
            log::error!(
                "golden_vt_render: recorder round-trip tail did not finish within 300s — wedged"
            );
            hard_exit(2);
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_recorder_roundtrip_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: recorder round-trip FAILED ({failures} case(s) — see the log above)"
        );
        hard_exit(1);
    }
    // The `ticked_` cases, driven from a task as the device drives a recorder: cluster writes go
    // through the channel.
    // SAFETY: same C-ABI contract as the calls above; called only from this task.
    unsafe { deluge_scenario_ticked_begin(0) };
    let off_deadline = Instant::now() + Duration::from_secs(300);
    let off_result = loop {
        // SAFETY: as above.
        let state = unsafe { deluge_scenario_ticked_tick() };
        if state != 0 {
            break state;
        }
        if Instant::now() >= off_deadline {
            log::error!("golden_vt_render: ticked recording did not complete within 300s — wedged");
            hard_exit(2);
        }
        // Yield so the write task can make progress on the clusters the case handed it.
        Timer::after_millis(1).await;
    };
    if off_result != 1 {
        log::error!("golden_vt_render: ticked recording FAILED — see the log above");
        hard_exit(1);
    }

    // Again through the alteration branch, as a separate run so a failure names which shape broke.
    // SAFETY: same C-ABI contract as above.
    unsafe { deluge_scenario_ticked_begin(1) };
    let alter_deadline = Instant::now() + Duration::from_secs(300);
    let alter_result = loop {
        // SAFETY: as above.
        let state = unsafe { deluge_scenario_ticked_tick() };
        if state != 0 {
            break state;
        }
        if Instant::now() >= alter_deadline {
            log::error!("golden_vt_render: ticked alterFile did not complete within 300s — wedged");
            hard_exit(2);
        }
        Timer::after_millis(1).await;
    };
    if alter_result != 1 {
        log::error!("golden_vt_render: ticked alterFile FAILED — see the log above");
        hard_exit(1);
    }

    // Once more, abandoning the recording part-way so the abort machine runs: its close must
    // complete before its unlink, which only the free-cluster count can see.
    // SAFETY: same C-ABI contract as above.
    unsafe { deluge_scenario_ticked_begin(2) };
    let abort_deadline = Instant::now() + Duration::from_secs(300);
    let abort_result = loop {
        // SAFETY: as above.
        let state = unsafe { deluge_scenario_ticked_tick() };
        if state != 0 {
            break state;
        }
        if Instant::now() >= abort_deadline {
            log::error!("golden_vt_render: ticked abort did not complete within 300s — wedged");
            hard_exit(2);
        }
        Timer::after_millis(1).await;
    };
    if abort_result != 1 {
        log::error!("golden_vt_render: ticked abort FAILED — see the log above");
        hard_exit(1);
    }

    // The give-up cases (the channel busy with another operation, a cluster write held past the
    // recorder's bound, and so on). Each prints its own line, and a failure stops neither the
    // other cases nor the tier report, so the script sees every result.
    let mut give_up_failed = false;
    // dirstall runs last: its held directory open is still in flight when it reports, and a later
    // case's folder scan would wait behind it.
    for (mode, name) in [
        (3, "busy"),
        (4, "stall"),
        (6, "abortcreate"),
        (7, "abortalter"),
        (8, "openioerr"),
        (5, "dirstall"),
    ] {
        // SAFETY: same C-ABI contract as above.
        unsafe { deluge_scenario_ticked_begin(mode) };
        let deadline = Instant::now() + Duration::from_secs(300);
        let result = loop {
            // SAFETY: as above.
            let state = unsafe { deluge_scenario_ticked_tick() };
            if state != 0 {
                break state;
            }
            if Instant::now() >= deadline {
                log::error!(
                    "golden_vt_render: ticked {name} case did not complete within 300s — wedged"
                );
                hard_exit(2);
            }
            Timer::after_millis(1).await;
        };
        if result != 1 {
            log::error!("golden_vt_render: ticked {name} case FAILED — see the log above");
            give_up_failed = true;
        }
    }

    // Over every `ticked_` case: a synchronous card operation is a violation whichever case made
    // it.
    // SAFETY: same C-ABI contract as above.
    unsafe { deluge_scenario_ticked_report() };
    if give_up_failed {
        hard_exit(1);
    }

    log::info!("golden_vt_render: recorder round-trip PASSED (0 failures); exiting cleanly");
    done.store(true, Ordering::Release);
}

/// An empty directory to stage a scenario's card tree in, named for this process so that runs of the same scenario at
/// once never share one.
fn staging_dir(name: &str) -> PathBuf {
    let root = std::env::temp_dir().join(format!("{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    root
}

/// Installs the tree staged at `root` as the card, then removes it: the card holds a copy.
fn install_staged(root: &Path) {
    memory_card::install(Some(root));
    let _ = std::fs::remove_dir_all(root);
}

/// The cards whose contents a manifest cannot describe, by the name a `Builder` row gives. song_preview_ab's copies up
/// to MAX_CORPUS_CASES songs from DELUGE_SONG_CORPUS (or DELUGE_BACKUP) at run time and derives its songs from
/// constants the C++ scenario asserts against.
const BUILDERS: &[(&str, fn(&Path))] = &[("song_preview_ab", fill_song_preview_ab_card)];

/// Installs scenario `name`'s card. Exits 2 when the card cannot be made: a required variable unset, a manifest
/// that does not parse or build, a builder name `BUILDERS` lacks.
fn install_card(name: &str, card: Card, repo_root: &Path, fixture: &str) {
    match card {
        Card::Empty => memory_card::install(None),
        Card::CorpusFixture => {
            memory_card::install(Some(&corpus_fixture(name, repo_root, fixture)))
        }
        Card::FromEnv { var, fallback } => match (std::env::var_os(var), fallback) {
            (Some(value), _) if value == "none" => memory_card::install(None),
            (Some(value), _) => memory_card::install(Some(Path::new(&value))),
            (None, Some("corpus")) => {
                memory_card::install(Some(&corpus_fixture(name, repo_root, fixture)))
            }
            (None, Some(dir)) => memory_card::install(Some(&repo_root.join(dir))),
            (None, None) => {
                eprintln!("golden_vt_render: {name} needs {var}: the directory its card holds");
                std::process::exit(2);
            }
        },
        Card::Manifest(path) => {
            let root = staging_dir(&format!("deluge-{name}-card"));
            if let Err(e) = card_manifest::build_file(repo_root, &repo_root.join(path), &root) {
                eprintln!("golden_vt_render: {name}'s card: {e}");
                std::process::exit(2);
            }
            install_staged(&root);
        }
        Card::Builder(builder) => {
            let Some((_, build)) = BUILDERS.iter().find(|(known, _)| *known == builder) else {
                eprintln!(
                    "golden_vt_render: {name} names card builder '{builder}', which BUILDERS does not hold"
                );
                std::process::exit(2);
            };
            build(repo_root);
        }
    }
}

/// Names the run as one whose registry entry requires `song-corpus`; `./dbt test` sets it to "1" for exactly those.
const CORPUS_DECLARED: &str = "DELUGE_CORPUS_DECLARED";

/// The corpus fixture's tree, for scenario `name`. Exits 2 unless the run declares the corpus.
///
/// A test that boots the corpus fixture runs only where the maintainer's card backup is, so its registry entry must
/// say so (`requires = ["song-corpus"]`, which keeps it out of CI). Refusing here makes a scenario given the corpus
/// card by mistake fail on every machine, instead of passing where the backup happens to be and failing in CI.
fn corpus_fixture(name: &str, repo_root: &Path, fixture: &str) -> PathBuf {
    if std::env::var_os(CORPUS_DECLARED).is_none_or(|value| value != "1") {
        eprintln!(
            "golden_vt_render: {name} boots the song-corpus fixture '{fixture}', but this run does not declare the \
             corpus: its entry in tests/registry.toml must require \"song-corpus\" (./dbt test then sets \
             {CORPUS_DECLARED}=1). Running the binary by hand, set {CORPUS_DECLARED}=1 yourself."
        );
        std::process::exit(2);
    }
    sd_image::golden_fixture_tree(repo_root, fixture)
}

// The song-preview A/B fixture
//
// `tests/integration/scenarios/song_preview_ab_scenario.cpp` names every file written below and
// asserts each one's artwork against these constants; keep the two in step.

/// Uniform preview channel value per synthetic song, matching the C++ scenario's constants of the
/// same names. All different, so no two songs' artwork can be mistaken for each other (the
/// card-swap assertion depends on it).
const PREVIEW_PAD_RENDER: u8 = 0x30;
const PREVIEW_PAD_JSON: u8 = 0x48;
const PREVIEW_PAD_LATE: u8 = 0x50;
const PREVIEW_PAD_SWAP_A: u8 = 0x20;
const PREVIEW_PAD_SWAP_B: u8 = 0x60;

/// Hex characters in a full preview payload: (16 + 2 sidebar) columns × 8 rows × 3 colours × 2.
/// `runPreviewWalk` reads exactly this many; a shorter payload renders a partial image.
const PREVIEW_HEX_CHARS: usize = (16 + 2) * 8 * 3 * 2;

/// Returns a preview payload whose every pad is `value` on every channel.
fn uniform_preview(value: u8) -> String {
    let pair = format!("{value:02X}");
    pair.repeat(PREVIEW_HEX_CHARS / 2)
}

/// Returns a minimal song document the real deserializer accepts: the root `song` tag with the
/// firmware attributes `openXMLFile` looks for, then `body`, then the closing tag.
///
/// Attribute order follows the corpus (`firmwareVersion`, `earliestCompatibleFirmware`,
/// `previewNumPads`, `preview`): the preview walk steps over the first two and reads the third.
fn song_xml(body: &str) -> String {
    format!(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n\tfirmwareVersion=\"c1.3.0\"\n\t\
         earliestCompatibleFirmware=\"4.1.0-alpha\"\n{body}>\n</song>\n"
    )
}

/// Returns a song whose artwork is a single uniform colour.
fn uniform_preview_song(value: u8) -> String {
    song_xml(&format!(
        "\tpreviewNumPads=\"144\"\n\tpreview=\"{}\"\n",
        uniform_preview(value)
    ))
}

/// Stages the song-preview A/B scenario's card. Each file is one shape of answer the two backings
/// must agree about:
///
/// - `SONGS/RENDER.XML` and `SONGS/JSONP.Json` are ordinary songs with artwork inside the read
///   window, one per deserializer. The JSON one is the checked-in
///   `tests/support/data/songs/JsonPreview.Json` with its payload rewritten, not hand-written, so
///   the fixture does not end up testing itself.
/// - `SONGS/NOPREV.XML` has no `preview` attribute and is shorter than the read window (a longer
///   one would legitimately differ between backings — `LATE.XML`'s job).
/// - `SONGS/CORRUPT.XML` is not a song, `SONGS/EMPTY.XML` is empty, and `SONGS/MISSING.XML` is not
///   written: three failures kept apart because the reported `Error` must match, not merely the
///   fact of failure.
/// - `SONGS/LATE.XML` pushes `preview` past the read window with a filler attribute: the one case
///   whose backings must differ (Rendered against Truncated).
/// - `SONGS/SWAP.XML` and `SONGS/SWAPB.XML` are the card-swap pair: the second is moved onto the
///   first's path once the card is marked ejected.
/// - `SONGS/CORPUS/C%02u.XML` (or `.json`) are real songs copied from `DELUGE_SONG_CORPUS` (or
///   `DELUGE_BACKUP/SONGS`) when set, with no absolute expectation; `COUNT.TXT` says how many.
///
/// # Panics
///
/// Panics if a file cannot be written, or if a named corpus stages no song.
fn fill_song_preview_ab_card(repo_root: &Path) {
    let root = staging_dir("deluge-song-preview-ab-card");
    let songs = root.join("SONGS");
    let corpus_dir = songs.join("CORPUS");
    std::fs::create_dir_all(&corpus_dir)
        .expect("creating the song-preview fixture card directories");

    let write = |path: PathBuf, text: String| {
        std::fs::write(&path, text)
            .unwrap_or_else(|e| panic!("writing the song-preview fixture {}: {e}", path.display()));
    };

    write(
        songs.join("RENDER.XML"),
        uniform_preview_song(PREVIEW_PAD_RENDER),
    );
    write(
        songs.join("SWAP.XML"),
        uniform_preview_song(PREVIEW_PAD_SWAP_A),
    );
    write(
        songs.join("SWAPB.XML"),
        uniform_preview_song(PREVIEW_PAD_SWAP_B),
    );
    // No `preview` attribute at all, and short: both backings must call this NoPreview.
    write(
        songs.join("NOPREV.XML"),
        song_xml("\tpreviewNumPads=\"144\"\n"),
    );
    // Not a song: `openXMLFile` walks to the end without finding its root tag.
    write(
        songs.join("CORRUPT.XML"),
        String::from(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<notasong version=\"1\">\n</notasong>\n",
        ),
    );
    // Empty: a different failure from a missing file, and the one that makes "READY with zero
    // bytes" unambiguous on the prefix channel.
    write(songs.join("EMPTY.XML"), String::new());
    // `preview` starts well past 4096 bytes, so the buffer backing runs out (Truncated) where the
    // file backing renders.
    write(
        songs.join("LATE.XML"),
        song_xml(&format!(
            "\tfiller=\"{}\"\n\tpreviewNumPads=\"144\"\n\tpreview=\"{}\"\n",
            "F".repeat(6000),
            uniform_preview(PREVIEW_PAD_LATE)
        )),
    );

    // The checked-in JSON song with its 864-hex-character payload replaced in place.
    let json_source = repo_root.join("tests/support/data/songs/JsonPreview.Json");
    let json = std::fs::read_to_string(&json_source).unwrap_or_else(|e| {
        panic!(
            "reading the checked-in JSON song fixture {}: {e}",
            json_source.display()
        )
    });
    let marker = "\"preview\":\"";
    let start = json
        .find(marker)
        .expect("the JSON song fixture has a preview attribute")
        + marker.len();
    let end = start + PREVIEW_HEX_CHARS;
    assert!(
        json[start..end].chars().all(|c| c.is_ascii_hexdigit()),
        "the JSON song fixture's preview payload is not {PREVIEW_HEX_CHARS} hex characters"
    );
    let mut json_song = String::with_capacity(json.len());
    json_song.push_str(&json[..start]);
    json_song.push_str(&uniform_preview(PREVIEW_PAD_JSON));
    json_song.push_str(&json[end..]);
    write(songs.join("JSONP.Json"), json_song);

    // Real songs, when a corpus is available; without one the synthetic cases still run.
    let corpus = std::env::var_os("DELUGE_SONG_CORPUS")
        .or_else(|| std::env::var_os("DELUGE_BACKUP").map(|b| Path::new(&b).join("SONGS").into()));
    let mut copied = 0usize;
    // One letter a staged song, in case order: `X` for XML, `J` for JSON.
    let mut kinds = String::new();
    let corpus_was_named = corpus.is_some();
    if let Some(corpus) = corpus {
        let mut entries: Vec<PathBuf> = std::fs::read_dir(&corpus)
            .map(|dir| {
                dir.filter_map(Result::ok)
                    .map(|e| e.path())
                    .filter(|p| {
                        p.is_file()
                            && p.extension().and_then(|e| e.to_str()).is_some_and(|e| {
                                e.eq_ignore_ascii_case("xml") || e.eq_ignore_ascii_case("json")
                            })
                    })
                    .collect()
            })
            .unwrap_or_default();
        // Sorted: `read_dir` order is not stable, and both legs and every run need the same order.
        entries.sort();
        for path in entries.iter().take(MAX_CORPUS_CASES) {
            let json = path
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| e.eq_ignore_ascii_case("json"));
            let target = corpus_dir.join(format!(
                "C{copied:02}.{}",
                if json { "json" } else { "XML" }
            ));
            if std::fs::copy(path, &target).is_ok() {
                copied += 1;
                kinds.push(if json { 'J' } else { 'X' });
            }
        }
        // A named corpus that stages nothing is a failure, not a silent fallback to the synthetic
        // cases: the run would still pass with the real songs gone.
        assert!(
            copied > 0,
            "DELUGE_SONG_CORPUS names {} but no .XML or .json song could be staged from it",
            Path::new(&corpus).display()
        );
    }
    // Always written, zero included: the C++ side reads it (`kCorpusCountPath`) and fails if it is
    // absent, rather than taking absence to mean "no corpus".
    write(corpus_dir.join("COUNT.TXT"), format!("{copied}\n{kinds}\n"));
    log::info!(
        "golden_vt_render: song-preview fixture staged {copied} corpus song(s) (corpus named: {corpus_was_named})"
    );

    install_staged(&root);
}

/// The most corpus songs the fixture stages; must equal `kMaxCorpusCases` in
/// `tests/integration/scenarios/song_preview_ab_scenario.cpp`.
///
/// The assert against its own literal makes a change take a deliberate edit on each side; the C++
/// side's check against `COUNT.TXT` catches drift at run time.
const MAX_CORPUS_CASES: usize = 16;
const _: () = assert!(MAX_CORPUS_CASES == 16);

/// Set by [`pender`]; the driver loop in `main` polls again while it is set.
static PENDED: AtomicBool = AtomicBool::new(false);

/// The executor's pender: no thread and no parking, just [`PENDED`].
#[unsafe(export_name = "__pender")]
fn pender(_context: *mut ()) {
    PENDED.store(true, Ordering::SeqCst);
}

fn main() {
    // A `--release` build compiles `log::debug!`/`trace!` away (deluge-sdk's `log` enables
    // `release_max_level_info`); use a debug build for those.
    env_logger::init();
    log::info!("golden_vt_render: Embassy-host-BSP golden renderer (boot + mount rung)");

    let fixture = std::env::var("GOLDEN_FIXTURE").unwrap_or_else(|_| "cordae".to_string());
    // A bound on virtual time, not wall time, so a wedged run fails rather than simulating
    // forever.
    let budget_ms: u64 = std::env::var("GOLDEN_VIRTUAL_BUDGET_MS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(60_000);

    // GOLDEN_SCENARIO names a row of the scenario table; unset, it is the stem export.
    let scenario = scenario_name();
    log::info!("golden_vt_render: fixture={fixture} budget_ms={budget_ms} scenario={scenario}");
    // A name the table does not hold is a mistake (a scenario not registered in scenario_table.cpp, or a typo), not
    // a request for the stem export, which would run for minutes and report nothing about the scenario.
    let Some(row) = table_row(&scenario) else {
        eprintln!(
            "golden_vt_render: no scenario named '{scenario}' (tests/integration/scenarios/scenario_table.cpp)"
        );
        std::process::exit(2);
    };
    enum Driver {
        Stepped(Steps),
        RecorderRoundtrip,
        StemExport,
    }
    let driver = match (row.steps(), scenario.as_str()) {
        (Some(steps), _) => Driver::Stepped(steps),
        (None, "recorder_roundtrip") => Driver::RecorderRoundtrip,
        (None, "stem_export") => Driver::StemExport,
        (None, other) => {
            eprintln!(
                "golden_vt_render: scenario '{other}' has no steps, and the harness has no driver of its own for it"
            );
            std::process::exit(2);
        }
    };

    // The card and the pacing must be set here, before any task is spawned (see the module doc).
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(5)
        .expect("CARGO_MANIFEST_DIR (src/bsp/rust/harness/golden_vt_render) has a repo root 5 levels up")
        .to_path_buf();
    if std::env::var_os("GOLDEN_CARD_ONLY").is_some()
        && std::env::var_os("DELUGE_SD_IMAGE").is_some()
    {
        eprintln!(
            "golden_vt_render: GOLDEN_CARD_ONLY dumps the scenario's card; DELUGE_SD_IMAGE replaces it"
        );
        std::process::exit(2);
    }
    row.pacing().apply();
    // DELUGE_SD_IMAGE, when set, names an image file to use as the card instead.
    if std::env::var_os("DELUGE_SD_IMAGE").is_none() {
        install_card(&scenario, row.card(), &repo_root, &fixture);
    }
    // GOLDEN_CARD_ONLY: stop once the card is installed, so GOLDEN_SD_DUMP holds exactly what the scenario boots
    // with and nothing it writes.
    if std::env::var_os("GOLDEN_CARD_ONLY").is_some() {
        log::info!("golden_vt_render: GOLDEN_CARD_ONLY: the card is installed; exiting");
        hard_exit(0);
    }
    // Trims the host audio drive's per-block diagnostic bookkeeping.
    // SAFETY: single-threaded here; no task or executor exists yet.
    unsafe { std::env::set_var("DELUGE_RENDER", "1") };

    // `raw::Executor` needs `&'static`; leak it.
    let executor: &'static raw::Executor =
        Box::leak(Box::new(raw::Executor::new(std::ptr::null_mut())));
    let spawner = executor.spawner();

    // The host runtime's BSP tasks, without its separate audio thread: the render task shares this
    // one executor.
    spawner.spawn(control::pic_pump().unwrap());
    spawner.spawn(control::pad_render().unwrap());
    spawner.spawn(app_loop::app_loop().unwrap());
    spawner.spawn(display::oled_render().unwrap());
    spawner.spawn(boot_task().unwrap());
    spawner.spawn(render::render_task().unwrap());
    // The async cluster-fill task, which owns the loader queue.
    spawner.spawn(streaming_loader::streaming_fill_task().unwrap());
    // The channel workers. Without each, every request on its channel waits forever.
    spawner.spawn(audio_loader::audio_load_task().unwrap());
    spawner.spawn(file_prefix::file_prefix_task().unwrap());
    spawner.spawn(file_directory::file_directory_task().unwrap());
    spawner.spawn(file_bytes::file_bytes_task().unwrap());
    spawner.spawn(file_stream::file_stream_task().unwrap());

    static DONE: AtomicBool = AtomicBool::new(false);
    match driver {
        Driver::Stepped(steps) => {
            let name: &'static str = Box::leak(scenario.clone().into_boxed_str());
            spawner.spawn(run_stepped_scenario(steps, name, &DONE).unwrap());
        }
        Driver::RecorderRoundtrip => spawner.spawn(run_recorder_roundtrip_scenario(&DONE).unwrap()),
        Driver::StemExport => {
            let fixture_static: &'static str = Box::leak(fixture.into_boxed_str());
            spawner.spawn(run_stem_export_scenario(fixture_static, &DONE).unwrap());
        }
    }

    // The discrete-event loop: poll to quiescence, then jump the virtual clock to the next due
    // deadline, until the scenario is done or the budget or wedge detection trips.
    let driver = clock::PeekableMockDriver::get();
    let budget_ticks = Duration::from_millis(budget_ms).as_ticks();
    let wall_start = std::time::Instant::now();
    let mut quiescence_passes = 0u64;
    let mut advances = 0u64;
    loop {
        loop {
            PENDED.store(false, Ordering::SeqCst);
            // SAFETY: single-threaded, never called reentrantly.
            unsafe { executor.poll() };
            quiescence_passes += 1;
            if !PENDED.load(Ordering::SeqCst) {
                break;
            }
        }

        if DONE.load(Ordering::Acquire) {
            log::info!(
                "golden_vt_render: run_stem_export_scenario completed at virtual t={}us \
                 ({quiescence_passes} quiescence passes, {advances} clock advances)",
                driver.now(),
            );
            break;
        }

        let Some(next) = driver.peek_next_deadline() else {
            log::error!(
                "golden_vt_render: no task has an outstanding timer — boot/mount wedged (deadlock)"
            );
            hard_exit(2);
        };
        if next > budget_ticks {
            log::error!(
                "golden_vt_render: virtual-time budget ({budget_ms}ms) exhausted before boot/mount \
                 completed (next deadline at {next}us > budget {budget_ticks}us) — wedged"
            );
            hard_exit(2);
        }
        let now = driver.now();
        driver.advance_ticks(next - now);
        advances += 1;
    }
    let wall_elapsed = wall_start.elapsed();
    log::info!("golden_vt_render: wall-clock elapsed: {wall_elapsed:?}");
    log::info!("golden_vt_render: boot + mount succeeded; exiting cleanly");

    hard_exit(0);
}

/// The UI script runner's clock: virtual milliseconds since boot, the clock the app's timers run on here.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_now_ms() -> u32 {
    Instant::now().as_millis() as u32
}

/// Ends the run for the UI script runner: 0 when its script quit, 2 when the script failed.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_exit(code: i32) -> ! {
    hard_exit(code)
}

/// Prints the exit reports, flushes stdout and stderr, and ends the process with `code` without
/// running C++ static destructors.
///
/// `std::process::exit` would run the app's `__cxa_atexit` destructors, whose ordering against
/// still-referenced globals can crash (SIGSEGV) after a successful run; the app never needs an
/// orderly shutdown. The flush matters because `env_logger` buffers, so a fast failure would lose
/// its log.
///
/// A run that made unguarded structural edits exits 3 instead of 0.
fn hard_exit(mut code: i32) -> ! {
    use std::io::Write as _;
    memory_card::dump_if_asked();
    // The synchronous-call inventory, on every exit: a failing run's numbers matter too.
    tier_meter::print_report();
    if audio_host::render_after_boot() {
        println!("AUDIO-RENDER blocks={}", audio_host::drive_count());
    }
    // Structural edits to the render's containers made while the render could have preempted them
    // (`util/structural_edit_probe.h`). Any fails an otherwise passing run (exit 3); nothing is
    // printed on a clean run, so its markers do not change.
    // SAFETY: a host C-ABI getter reading relaxed atomics; safe at any point.
    let unguarded = unsafe { deluge_unguarded_structural_edits_total() };
    if unguarded != 0 {
        println!("GUARD-UNGUARDED total={unguarded}");
        if code == 0 {
            code = 3;
        }
    }
    let _ = std::io::stdout().flush();
    let _ = std::io::stderr().flush();
    #[cfg(all(target_os = "linux", target_arch = "x86_64"))]
    unsafe {
        core::arch::asm!(
            "syscall",
            in("rax") 231usize, // exit_group
            in("rdi") code,
            options(noreturn, nostack)
        );
    }
    // 32-bit ARM EABI (the arm-linux/qemu golden leg): exit_group is syscall 248, number in r7,
    // argument in r0.
    #[cfg(all(target_os = "linux", target_arch = "arm"))]
    unsafe {
        core::arch::asm!(
            "svc #0",
            in("r7") 248usize, // exit_group
            in("r0") code,
            options(noreturn, nostack)
        );
    }
    #[cfg(not(all(target_os = "linux", any(target_arch = "x86_64", target_arch = "arm"))))]
    std::process::exit(code);
}
