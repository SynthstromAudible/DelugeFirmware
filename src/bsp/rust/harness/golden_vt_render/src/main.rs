//! `golden_vt_render`: boots the real C++ app on the BSP under a virtual clock, fills the card with
//! a fixture tree, and runs one scenario deterministically.
//!
//! By default the scenario is an offline `StemExport` of the fixture song, whose stem WAVs it
//! copies out; otherwise it is the scenario `GOLDEN_SCENARIO` names — the recorder round-trip, or a
//! stepped scenario from `tests/integration/scenarios/scenario_table.cpp`. Sample cluster fills
//! drain through the BSP's async `streaming_fill_task`, so a sample-backed fixture renders real
//! audio.
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
//! # The card is chosen before any task is spawned
//!
//! The BSP's host SD opens its disk once, lazily, on first access, and embassy does not order the
//! first polls of tasks spawned together. So `main` installs the memory card before the executor
//! exists; installed from a task, a poll order that reached [`boot_task`]'s SD bring-up first
//! would open the wrong backing, silently.
#![feature(impl_trait_in_assoc_type)]
#![recursion_limit = "512"]

use core::ffi::c_char;
use core::sync::atomic::{AtomicBool, Ordering};
use std::ffi::CString;

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
use deluge_bsp_rust::storage::{memory_card, sd, sd_image, streaming_loader};
use deluge_bsp_rust::{app_loop, boot, sys, tier_meter};

/// A stepped scenario, as `tests/integration/scenarios/scenario_table.h` declares it: found by
/// name, then driven by [`run_stepped_scenario`]: `begin` once, then `dispatch_step` and
/// `step_done` until `complete`.
#[repr(C)]
struct Scenario {
    name: *const c_char,
    on_app_init: Option<unsafe extern "C" fn()>,
    begin: unsafe extern "C" fn(),
    complete: unsafe extern "C" fn() -> bool,
    dispatch_step: unsafe extern "C" fn(),
    step_done: unsafe extern "C" fn() -> bool,
    failures: unsafe extern "C" fn() -> i32,
}

// SAFETY: a row of a C++ `constexpr` table: immutable for the life of the process.
unsafe impl Sync for Scenario {}

/// The stepped scenario `GOLDEN_SCENARIO` names, if it names one.
fn stepped_scenario(name: &str) -> Option<&'static Scenario> {
    let name = std::ffi::CString::new(name).ok()?;
    // SAFETY: host scenario C-ABI; `name` is NUL-terminated and outlives the call, and the table
    // the answer points into is static.
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

    // The stem export (GOLDEN_SCENARIO unset) needs no boot settings, and the settings prelude's
    // reads (and its write when MIDI_FOLLOW.XML is absent) would occupy the single-slot byte
    // channel the export's folder search uses. Scenarios keep the prelude, which exercises it.
    // Must precede deluge_app_init, which registers the startup job.
    if std::env::var("GOLDEN_SCENARIO")
        .unwrap_or_default()
        .is_empty()
    {
        // SAFETY: plain store to a C++ global, before deluge_app_init reads it.
        unsafe { sys::deluge_startup_skip_settings_prelude() };
    }

    log::info!("golden_vt_render: deluge_app_init() (registers the app's jobs)");
    // SAFETY: called once, before anything else touches the app state.
    unsafe { sys::deluge_app_init(board::deluge_board()) };
    // A stepped scenario's hook for this moment: boot has not been marked finished, so neither the
    // app loop nor the render has run yet.
    if let Some(on_app_init) =
        stepped_scenario(&std::env::var("GOLDEN_SCENARIO").unwrap_or_default())
            .and_then(|scenario| scenario.on_app_init)
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
async fn run_stepped_scenario(
    scenario: &'static Scenario,
    name: &'static str,
    done: &'static AtomicBool,
) {
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
    unsafe { (scenario.begin)() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        if unsafe { (scenario.complete)() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!("golden_vt_render: {name} did not complete within 600s — wedged");
            hard_exit(2);
        }
        unsafe { (scenario.dispatch_step)() };
        // One virtual millisecond per step before asking whether it is done: the gates' tick
        // counts and races are paced in these milliseconds.
        Timer::after_millis(1).await;
        while !unsafe { (scenario.step_done)() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    let failures = unsafe { (scenario.failures)() };
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

/// Writes a minimal parseable PCM WAV: the canonical 44-byte header, then `data_bytes` bytes all
/// equal to `fill`.
///
/// The fill tells apart files that agree on every geometry field (such as `SAMPLES/BOTH.WAV` and
/// `SAMPLES/ALT/BOTH.WAV`) when the C++ side fingerprints the bytes it reads back.
///
/// # Panics
///
/// Panics if the file cannot be written.
fn write_test_wav(path: &std::path::Path, channels: u16, bits: u16, data_bytes: u32, fill: u8) {
    const SAMPLE_RATE: u32 = 44_100;
    let block_align = channels * (bits / 8);
    let mut bytes: Vec<u8> = Vec::with_capacity(44 + data_bytes as usize);
    bytes.extend_from_slice(b"RIFF");
    bytes.extend_from_slice(&(36 + data_bytes).to_le_bytes());
    bytes.extend_from_slice(b"WAVE");
    bytes.extend_from_slice(b"fmt ");
    bytes.extend_from_slice(&16u32.to_le_bytes());
    bytes.extend_from_slice(&1u16.to_le_bytes()); // PCM
    bytes.extend_from_slice(&channels.to_le_bytes());
    bytes.extend_from_slice(&SAMPLE_RATE.to_le_bytes());
    bytes.extend_from_slice(&(SAMPLE_RATE * u32::from(block_align)).to_le_bytes());
    bytes.extend_from_slice(&block_align.to_le_bytes());
    bytes.extend_from_slice(&bits.to_le_bytes());
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&data_bytes.to_le_bytes());
    bytes.resize(44 + data_bytes as usize, fill);
    std::fs::write(path, &bytes)
        .unwrap_or_else(|e| panic!("writing the A/B fixture WAV {}: {e}", path.display()));
}

/// An empty directory to stage a scenario's card tree in, named for this process so that runs of the same scenario at
/// once never share one.
fn staging_dir(name: &str) -> std::path::PathBuf {
    let root = std::env::temp_dir().join(format!("{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    root
}

/// Installs the tree staged at `root` as the card, then removes it: the card holds a copy.
fn install_staged(root: &std::path::Path) {
    memory_card::install(Some(root));
    let _ = std::fs::remove_dir_all(root);
}

/// Stages the song-save scenario's card: an empty `SONGS` directory.
///
/// The fixture that matters is the song, which the scenario inflates in memory until its document
/// outgrows the staging buffer.
fn fill_song_save_card() {
    let root = staging_dir("deluge-song-save-card");
    std::fs::create_dir_all(root.join("SONGS")).expect("creating the fixture SONGS directory");
    install_staged(&root);
}

/// Stages the sample-collect scenario's card: small WAVs with different lengths and fills, so a
/// copy that fetched the wrong file shows in the checksum.
///
/// The two `RECORD/` files make the ordering check able to fail. A collect-media save renames only
/// a recording (`REC` + five characters + `.WAV`), appending a random `_XXXXX`; ordinary samples
/// never move in the audio-file vector. `REC12345.WAV` is that recording. `REC123450.WAV` is not
/// renamed, and sorts (case-insensitively) after `REC12345.WAV` and before `REC12345_XXXXX.WAV`
/// (`.` < `0` < `_`), so the rename must move the recording across it: rewriting the path without
/// removing and reinserting the entry leaves the vector out of order at that index.
fn fill_sample_collect_card() {
    let root = staging_dir("deluge-sample-collect-card");
    let samples = root.join("SAMPLES");
    let recordings = samples.join("RECORD");
    std::fs::create_dir_all(&recordings).expect("creating the sample-collect fixture directories");
    std::fs::create_dir_all(root.join("SONGS")).expect("creating the fixture SONGS directory");
    write_test_wav(&samples.join("COLL1.WAV"), 1, 16, 512, 0x41);
    write_test_wav(&samples.join("COLL2.WAV"), 2, 16, 1024, 0x42);
    write_test_wav(&recordings.join("REC12345.WAV"), 1, 16, 768, 0x43);
    write_test_wav(&recordings.join("REC123450.WAV"), 1, 16, 640, 0x44);
    install_staged(&root);
}

/// Stages the card-reinsert scenario's card: three samples, one per fate the reinsert walk
/// distinguishes.
///
/// The scenario removes `CRLOST.WAV` between ejection and re-insertion, so its resident Sample must
/// be marked unloadable. The other two stay on the card; their fates depend on whether a project
/// still holds them.
fn fill_card_reinsert_card() {
    let root = staging_dir("deluge-card-reinsert-card");
    let samples = root.join("SAMPLES");
    for dir in [&samples, &root.join("SONGS")] {
        std::fs::create_dir_all(dir).expect("creating the card-reinsert fixture directories");
    }
    write_test_wav(&samples.join("CRSURV.WAV"), 1, 16, 4096, 0x71);
    write_test_wav(&samples.join("CRLOST.WAV"), 1, 16, 3072, 0x72);
    write_test_wav(&samples.join("CREVICT.WAV"), 1, 16, 2048, 0x73);
    install_staged(&root);
}

/// Stages the sample-load tree scenario's card.
///
/// - `SAMPLES/SLTSYN.WAV`, `SLTWT.WAV`, `SLTKIT.WAV` and `SLTAUD.WAV` are the song's samples: the
///   synth's sample source, its wavetable source (one 2048-sample cycle), the kit drum and the
///   audio clip. The scenario collects them into the song's folder when it saves, then removes the
///   first three from here, so the song load can only find their collected copies.
/// - `SYNTHS/SLTPSYN/SLTPSYN.WAV` and `KITS/SLTPKIT/SLTPKA.WAV` / `SLTPKB.WAV` are the presets'
///   samples. Each preset names its sample under `SAMPLES/`, where no such file exists, so the load
///   must find it in the preset's own folder and rewrite the holder's path.
/// - `SAMPLES/SLTCKA.WAV`, `SLTCKB.WAV`, `SLTCKC.WAV`, `SLTCSB.WAV` and `SLTCAB.WAV` belong to the
///   crucial-only load's song (a second kit's three drums, a second synth, a second audio clip),
///   collected and removed the same way.
fn fill_sample_load_tree_card() {
    let root = staging_dir("deluge-sample-load-tree-card");
    let samples = root.join("SAMPLES");
    let synth_folder = root.join("SYNTHS").join("SLTPSYN");
    let kit_folder = root.join("KITS").join("SLTPKIT");
    for dir in [&samples, &synth_folder, &kit_folder, &root.join("SONGS")] {
        std::fs::create_dir_all(dir).expect("creating the sample-load tree fixture directories");
    }
    write_test_wav(&samples.join("SLTSYN.WAV"), 1, 16, 4096, 0x51);
    write_test_wav(&samples.join("SLTWT.WAV"), 1, 16, 4096, 0x52);
    write_test_wav(&samples.join("SLTKIT.WAV"), 1, 16, 3072, 0x53);
    write_test_wav(&samples.join("SLTAUD.WAV"), 1, 16, 2048, 0x54);
    write_test_wav(&samples.join("SLTCKA.WAV"), 1, 16, 2048, 0x61);
    write_test_wav(&samples.join("SLTCKB.WAV"), 1, 16, 2048, 0x62);
    write_test_wav(&samples.join("SLTCKC.WAV"), 1, 16, 2048, 0x63);
    write_test_wav(&samples.join("SLTCSB.WAV"), 1, 16, 2048, 0x64);
    write_test_wav(&samples.join("SLTCAB.WAV"), 1, 16, 2048, 0x65);
    write_test_wav(&synth_folder.join("SLTPSYN.WAV"), 1, 16, 2048, 0x55);
    write_test_wav(&kit_folder.join("SLTPKA.WAV"), 1, 16, 2048, 0x56);
    write_test_wav(&kit_folder.join("SLTPKB.WAV"), 1, 16, 2048, 0x57);
    install_staged(&root);
}

/// Stages the audio-load A/B scenario's card. Every file is load-bearing:
///
/// - `SAMPLES/BOTH.WAV` is in the regular directory and in `SAMPLES/ALT`, with a different length
///   and fill in each, so a wrong candidate order moves the geometry, the recorded path and the
///   stream fingerprint together.
/// - `SAMPLES/BOTH2.WAV` has the same shape but is asked for after the alternate directory is
///   probed, with both candidates live (`BOTH.WAV`'s request carries one). It is the file that
///   tests the candidate order itself.
/// - `SAMPLES/ONLYALT.WAV` is only in `SAMPLES/ALT`: the probe must find it, and its regular-path
///   miss promotes the load's alternate-dir status for every later case.
/// - `SAMPLES/ONLYREG.WAV` and `SAMPLES/STEREO24.WAV` come after that promotion and see the changed
///   try order; `STEREO24.WAV` (2 channels, 24-bit) differs in both fields compared first.
/// - `SAMPLES/BADREG.WAV` is garbage at the regular path with a good copy in `SAMPLES/ALT`, asked
///   for while the alternate dir is unprobed. A parse verdict is not a miss: the load must fail as
///   `FILE_UNSUPPORTED` without trying the alternate.
/// - `SAMPLES/GARBAGE.WAV` opens and fails to parse; `SAMPLES/EMPTY.WAV` is empty;
///   `SAMPLES/MISSING.WAV` is never written. The backings must fail with the same `Error`, not
///   merely both fail.
/// - `SAMPLES/ALT/SELFDIR.WAV` sits inside the alternate directory, the one shape where the
///   bare-name alternate candidate equals the regular path, so "an alternate won" must be decided
///   by candidate position.
/// - `SAMPLES/PREVIEW.WAV` is previewed once and must then load and draw with only per-tick calls
///   driving it.
/// - `SAMPLES/CANCEL.WAV` is asked for and abandoned before its load runs, and must never load or
///   draw.
/// - `SAMPLES/STALL.WAV` is previewed while the loader holds every load past the browser's bound;
///   the preview must give up without loading or drawing it.
/// - `SAMPLES/DRAWSTALL.WAV` is previewed while the streaming loader holds every cluster fill; the
///   redraw must give up.
/// - `SAMPLES/SWAP.WAV`, `SWAPB.WAV` and `WITNESS.WAV` serve the card-ejection stage (see the
///   comment at their writes).
///
/// The staging tree is deleted once the card holds it: a full `/tmp` can wedge an unrelated stem
/// export.
fn fill_audio_load_ab_card() {
    let root = staging_dir("deluge-audio-load-ab-card");
    let samples = root.join("SAMPLES");
    let alt = samples.join("ALT");
    let kit = samples.join("KIT");
    std::fs::create_dir_all(&alt).expect("creating the A/B fixture card directories");
    std::fs::create_dir_all(&kit).expect("creating the A/B fixture card directories");

    write_test_wav(&samples.join("BOTH.WAV"), 1, 16, 2048, 0x11);
    write_test_wav(&alt.join("BOTH.WAV"), 1, 16, 4096, 0x22);
    write_test_wav(&alt.join("ONLYALT.WAV"), 1, 16, 1536, 0x33);
    write_test_wav(&samples.join("BOTH2.WAV"), 1, 16, 1792, 0x66);
    write_test_wav(&alt.join("BOTH2.WAV"), 1, 16, 3584, 0x77);
    write_test_wav(&samples.join("ONLYREG.WAV"), 1, 16, 2560, 0x44);
    write_test_wav(&samples.join("STEREO24.WAV"), 2, 24, 3072, 0x55);
    // The nested case: three locations, three lengths. Only a nested regular path makes the
    // flattened alternate candidate (`ALT/KIT_SNARE.WAV`) and the bare-name one (`ALT/SNARE.WAV`)
    // distinct.
    write_test_wav(&kit.join("SNARE.WAV"), 1, 16, 1024, 0x88);
    write_test_wav(&alt.join("KIT_SNARE.WAV"), 1, 16, 2304, 0x99);
    write_test_wav(&alt.join("SNARE.WAV"), 1, 16, 3072, 0xAA);
    // The self-directory case (`selfdir` in `audio_load_ab_scenario.cpp`'s kCases). There is
    // deliberately no flattened `ALT/ALT_SELFDIR.WAV`: that candidate must miss so the ambiguous
    // bare-name one wins.
    write_test_wav(&alt.join("SELFDIR.WAV"), 1, 16, 1280, 0xBB);
    // kPreviewPath: asked for by no case above, so it is not resident when the stage begins and
    // its load goes through the loader.
    write_test_wav(&samples.join("PREVIEW.WAV"), 1, 16, 2816, 0xCC);
    // kCancelPath: loadable on purpose, so "nothing loaded or drew it" cannot pass because the file
    // could not load.
    write_test_wav(&samples.join("CANCEL.WAV"), 1, 16, 3328, 0xDD);
    // kStallPath: loadable, for the same reason as CANCEL.WAV.
    write_test_wav(&samples.join("STALL.WAV"), 1, 16, 3840, 0xDE);
    // The preview-draw stall file: six 32 KB clusters, so each view column covers less than two and
    // reads its cluster directly, which a held fill leaves non-resident. At 16 clusters or more
    // each column spans whole clusters, which the draw takes from the overview without waiting.
    write_test_wav(&samples.join("DRAWSTALL.WAV"), 1, 16, 196_608, 0xDF);

    // The card-ejection stage (`audio_load_ab_scenario.cpp`'s kSwapPath): the loader parses
    // SWAP.WAV before the ejection; SWAPB.WAV, the "other card's" file, is moved onto its path once
    // the card is marked gone. They differ in every asserted field, so a header parsed from one and
    // applied to the other cannot pass. WITNESS.WAV is queued behind SWAP.WAV, so its completion
    // tells the stage SWAP.WAV's result is published.
    write_test_wav(&samples.join("SWAP.WAV"), 1, 16, 2048, 0xE1);
    write_test_wav(&samples.join("SWAPB.WAV"), 2, 24, 6144, 0xE2);
    write_test_wav(&samples.join("WITNESS.WAV"), 1, 16, 1536, 0xE3);

    std::fs::write(samples.join("BADREG.WAV"), vec![0x7Eu8; 512])
        .expect("writing the A/B fixture's garbage-at-the-regular-path file");
    write_test_wav(&alt.join("BADREG.WAV"), 1, 16, 2048, 0xBB);
    std::fs::write(samples.join("GARBAGE.WAV"), vec![0x7Eu8; 512])
        .expect("writing the A/B fixture's unparseable file");
    // A third failure shape: the file opens and there is nothing in it.
    std::fs::write(samples.join("EMPTY.WAV"), Vec::<u8>::new())
        .expect("writing the A/B fixture's zero-length file");

    install_staged(&root);
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
fn fill_song_preview_ab_card(repo_root: &std::path::Path) {
    let root = staging_dir("deluge-song-preview-ab-card");
    let songs = root.join("SONGS");
    let corpus_dir = songs.join("CORPUS");
    std::fs::create_dir_all(&corpus_dir)
        .expect("creating the song-preview fixture card directories");

    let write = |path: std::path::PathBuf, text: String| {
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
    let corpus = std::env::var_os("DELUGE_SONG_CORPUS").or_else(|| {
        std::env::var_os("DELUGE_BACKUP").map(|b| std::path::Path::new(&b).join("SONGS").into())
    });
    let mut copied = 0usize;
    // One letter a staged song, in case order: `X` for XML, `J` for JSON.
    let mut kinds = String::new();
    let corpus_was_named = corpus.is_some();
    if let Some(corpus) = corpus {
        let mut entries: Vec<std::path::PathBuf> = std::fs::read_dir(&corpus)
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
            std::path::Path::new(&corpus).display()
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

/// Writes a 16-bit PCM WAV of a sine at `freq_hz`, `frames` frames long, the same on every channel.
///
/// Pitch detection needs a periodic signal to name a note; a constant fill names none.
///
/// # Panics
///
/// Panics if the file cannot be written.
fn write_sine_wav(path: &std::path::Path, channels: u16, frames: u32, freq_hz: f64) {
    const SAMPLE_RATE: u32 = 44_100;
    let data_bytes = frames * u32::from(channels) * 2;
    let block_align = channels * 2;
    let mut bytes: Vec<u8> = Vec::with_capacity(44 + data_bytes as usize);
    bytes.extend_from_slice(b"RIFF");
    bytes.extend_from_slice(&(36 + data_bytes).to_le_bytes());
    bytes.extend_from_slice(b"WAVE");
    bytes.extend_from_slice(b"fmt ");
    bytes.extend_from_slice(&16u32.to_le_bytes());
    bytes.extend_from_slice(&1u16.to_le_bytes()); // PCM
    bytes.extend_from_slice(&channels.to_le_bytes());
    bytes.extend_from_slice(&SAMPLE_RATE.to_le_bytes());
    bytes.extend_from_slice(&(SAMPLE_RATE * u32::from(block_align)).to_le_bytes());
    bytes.extend_from_slice(&block_align.to_le_bytes());
    bytes.extend_from_slice(&16u16.to_le_bytes());
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&data_bytes.to_le_bytes());
    for n in 0..frames {
        let phase = f64::from(n) * freq_hz * std::f64::consts::TAU / f64::from(SAMPLE_RATE);
        let value = (phase.sin() * 16_000.0) as i16;
        for _ in 0..channels {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
    }
    std::fs::write(path, &bytes).unwrap_or_else(|e| {
        panic!(
            "writing the sample-claim fixture WAV {}: {e}",
            path.display()
        )
    });
}

/// Stages the sample-claim scenario's card.
///
/// Every file is asked for by one leg only, so a holder naming it can only be that leg's doing:
/// - `SAMPLES/SCSYN.WAV`: a 440 Hz sine, claimed into the boot song's synth, whose pitch detection
///   names its note.
/// - `SAMPLES/SCPITCH.WAV`, `SCSUPA.WAV`, `SCSUPB.WAV`, `SCPSTALL.WAV`: SCSYN.WAV's sine under
///   paths of their own, for the legs that must catch a pitch detection waiting for a cluster: the
///   resume leg, the two supersede legs and the pitch stall leg.
/// - `SAMPLES/SCWTBIG.WAV`: 64 wavetable-length cycles, previewed and then claimed as a wavetable:
///   a resident multi-cluster Sample most of whose clusters are not cached, so it is built from the
///   card.
/// - `SAMPLES/SCWTFULL.WAV`: 16 wavetable-length cycles, made fully resident as a Sample and then
///   claimed as a wavetable, which converts it from memory. `SAMPLES/SCWTFUL2.WAV` holds the same
///   bytes and is only ever built from the card: the reference the conversion is compared against.
/// - `SAMPLES/SCCYCLE.WAV`: one 512-frame cycle of a sine, claimed as a wavetable from the context
///   menu.
/// - `SAMPLES/SCSTER.WAV`: stereo, claimed into a wavetable oscillator, which cannot take it and
///   falls back to a sample.
/// - `SAMPLES/SCGATE.WAV` and `SAMPLES/SCSTALL.WAV`: claimed while the loader holds their loads,
///   for the gate and the stall bound.
/// - `SAMPLES/SCKITD.WAV`: the sample the scenario's one-drum kit preset names, so the kit clip
///   starts as a sample kit.
/// - `SAMPLES/SCKICK.WAV`: claimed into that kit's drum. Its name holds "KICK", which the claim
///   reads as a kick.
/// - `SAMPLES/SCAUD.WAV`: claimed into an audio clip.
/// - `SAMPLES/SCSLICE.WAV`: one second long, sliced into four regions of 250 ms each.
/// - `SAMPLES/RD/RD1.WAV`, `RD2.WAV` and `RD3.WAV`: the Drum Randomizer's folder. Its kit preset
///   names the first, and the randomizer picks from the folder a drum's sample sits in, so these
///   three are reachable by no other leg.
/// - `SAMPLES/RDG/G1.WAV`..`G3.WAV`: the randomizer gate leg's folder, loaded by nothing before it,
///   so its load reaches the loader's hold.
/// - `SAMPLES/RDK/K1.WAV`: the randomizer stall leg's first row, alone in its folder and loaded
///   before the hold.
/// - `SAMPLES/RDS/S1.WAV`..`S3.WAV`: the stall leg's second row, loaded by nothing, so its load is
///   held past the stall bound.
/// - `SAMPLES/IMK/IMK_HAT.WAV`, `IMK_KICK.WAV` and `IMK_SNARE.WAV`: the kit folder import's folder,
///   whose shared prefix the drum names drop.
/// - `SAMPLES/IMG/IMG_A.WAV`..`IMG_C.WAV`: the import gate leg's folder, loaded by nothing before
///   it.
/// - `SAMPLES/IMS/IMS_A.WAV`..`IMS_C.WAV`: the import stall leg's folder; the first and last are
///   loaded in its preparation, and the middle one is held.
/// - `SYNTHS/`: empty; the multisample import leg writes its blank synth preset there.
/// - `SAMPLES/IMM/IMM_A2.WAV`, `IMM_A3.WAV` and `IMM_A4.WAV`: the multisample folder import's
///   folder, one-second sines at 110, 220 and 440 Hz.
fn fill_sample_claim_card() {
    let root = staging_dir("deluge-sample-claim-card");
    let samples = root.join("SAMPLES");
    let randomize = samples.join("RD");
    let randomize_gate = samples.join("RDG");
    let randomize_kept = samples.join("RDK");
    let randomize_stall = samples.join("RDS");
    let import_kit = samples.join("IMK");
    let import_multi = samples.join("IMM");
    let import_gate = samples.join("IMG");
    let import_stall = samples.join("IMS");
    for dir in [
        &samples,
        &randomize,
        &randomize_gate,
        &randomize_kept,
        &randomize_stall,
        &import_kit,
        &import_multi,
        &import_gate,
        &import_stall,
        &root.join("KITS"),
        &root.join("SYNTHS"),
    ] {
        std::fs::create_dir_all(dir).expect("creating the sample-claim fixture directories");
    }
    write_sine_wav(&samples.join("SCSYN.WAV"), 1, 22_050, 440.0);
    // The pitch legs' files: SCSYN.WAV's sine under paths no other leg reads, so their clusters are
    // not resident when detection starts (a file read once is never missed again).
    write_sine_wav(&samples.join("SCPITCH.WAV"), 1, 22_050, 440.0);
    write_sine_wav(&samples.join("SCSUPA.WAV"), 1, 22_050, 440.0);
    write_sine_wav(&samples.join("SCSUPB.WAV"), 1, 22_050, 440.0);
    write_sine_wav(&samples.join("SCPSTALL.WAV"), 1, 22_050, 440.0);
    write_sine_wav(&samples.join("SCCYCLE.WAV"), 1, 512, 44_100.0 / 512.0);
    // 64 wavetable-length cycles (131072 frames, 8 clusters): resident as a Sample with most
    // clusters uncached, so the wavetable claim builds it from the card.
    write_sine_wav(&samples.join("SCWTBIG.WAV"), 1, 131_072, 44_100.0 / 2048.0);
    // 16 wavetable-length cycles (32768 frames, three clusters with the header), fully resident, so
    // the wavetable claim converts it from memory. Its twin is never previewed: the card build the
    // conversion is compared against.
    write_sine_wav(&samples.join("SCWTFULL.WAV"), 1, 32_768, 44_100.0 / 2048.0);
    write_sine_wav(&samples.join("SCWTFUL2.WAV"), 1, 32_768, 44_100.0 / 2048.0);
    write_test_wav(&samples.join("SCSTER.WAV"), 2, 16, 8192, 0x36);
    write_test_wav(&samples.join("SCGATE.WAV"), 1, 16, 4096, 0x37);
    write_test_wav(&samples.join("SCSTALL.WAV"), 1, 16, 4096, 0x38);
    write_test_wav(&samples.join("SCKITD.WAV"), 1, 16, 2048, 0x32);
    write_test_wav(&samples.join("SCKICK.WAV"), 1, 16, 4096, 0x33);
    write_test_wav(&samples.join("SCAUD.WAV"), 1, 16, 16384, 0x34);
    write_test_wav(&samples.join("SCSLICE.WAV"), 1, 16, 88200, 0x35);
    write_test_wav(&randomize.join("RD1.WAV"), 1, 16, 2048, 0x71);
    write_test_wav(&randomize.join("RD2.WAV"), 1, 16, 2560, 0x72);
    write_test_wav(&randomize.join("RD3.WAV"), 1, 16, 3072, 0x73);
    write_test_wav(&randomize_gate.join("G1.WAV"), 1, 16, 2048, 0x74);
    write_test_wav(&randomize_gate.join("G2.WAV"), 1, 16, 2560, 0x75);
    write_test_wav(&randomize_gate.join("G3.WAV"), 1, 16, 3072, 0x76);
    write_test_wav(&randomize_kept.join("K1.WAV"), 1, 16, 2048, 0x77);
    write_test_wav(&randomize_stall.join("S1.WAV"), 1, 16, 2048, 0x78);
    write_test_wav(&randomize_stall.join("S2.WAV"), 1, 16, 2560, 0x79);
    write_test_wav(&randomize_stall.join("S3.WAV"), 1, 16, 3072, 0x7a);
    write_test_wav(&import_kit.join("IMK_HAT.WAV"), 1, 16, 2048, 0x81);
    write_test_wav(&import_kit.join("IMK_KICK.WAV"), 1, 16, 2560, 0x82);
    write_test_wav(&import_kit.join("IMK_SNARE.WAV"), 1, 16, 3072, 0x83);
    write_sine_wav(&import_multi.join("IMM_A2.WAV"), 1, 44_100, 110.0);
    write_sine_wav(&import_multi.join("IMM_A3.WAV"), 1, 44_100, 220.0);
    write_sine_wav(&import_multi.join("IMM_A4.WAV"), 1, 44_100, 440.0);
    write_test_wav(&import_gate.join("IMG_A.WAV"), 1, 16, 2048, 0x84);
    write_test_wav(&import_gate.join("IMG_B.WAV"), 1, 16, 2560, 0x85);
    write_test_wav(&import_gate.join("IMG_C.WAV"), 1, 16, 3072, 0x86);
    write_sine_wav(&import_stall.join("IMS_A.WAV"), 1, 44_100, 110.0);
    write_sine_wav(&import_stall.join("IMS_B.WAV"), 1, 44_100, 220.0);
    write_sine_wav(&import_stall.join("IMS_C.WAV"), 1, 44_100, 440.0);
    install_staged(&root);
}

/// Stages `load_policy`'s card: the one sample its `LP-DEFAULT` loads ask for.
fn fill_load_policy_card() {
    let root = staging_dir("deluge-load-policy-card");
    let samples = root.join("SAMPLES");
    std::fs::create_dir_all(&samples).expect("load_policy card: creating SAMPLES");
    write_test_wav(&samples.join("DEFAULT.WAV"), 1, 16, 2000, 0x42);
    install_staged(&root);
}

/// Stages the preset-load scenario's card.
///
/// The scenario writes the presets into `SYNTHS/` and `KITS/` through the firmware's serialisers,
/// so both start empty (apart from the `SYNTHS/WALK/` folders). The samples:
/// - `SAMPLES/PL/`: `PLSYN.WAV` (synth leg), `PLROW.WAV` (kit row leg), `PLCLN.WAV` (clone leg),
///   `PLKA..PLKC.WAV` (kit leg's drums), `PLBASE.WAV` (the kit clip's own preset's drum, which the
///   new-kit-track gesture picks up), `PLRACE.WAV` (race leg) and `PLNEST.WAV` (nested preset).
/// - `SAMPLES/PLH/PLHA..PLHC.WAV`: the supersession leg's heavy kit, loaded by nothing before it;
///   `SAMPLES/PLL/PLLITE.WAV` its light kit.
/// - `SAMPLES/PLG/PLGATE.WAV` and `SAMPLES/PLS/PLSTALL.WAV`: the gate and stall legs' held samples.
/// - `SAMPLES/AQ/AQSA.WAV`, `AQKA..AQKD.WAV` and `AQKL.WAV`: the acquisition legs' synth, navigated
///   kits and light kit; `SAMPLES/AQH/AQHA..AQHC.WAV` their heavy kit; `SAMPLES/AQS/PLSTB.WAV` the
///   acquisition stall leg's held sample.
fn fill_preset_load_card() {
    let root = staging_dir("deluge-preset-load-card");
    let samples = root.join("SAMPLES").join("PL");
    let heavy = root.join("SAMPLES").join("PLH");
    let lite = root.join("SAMPLES").join("PLL");
    let gate = root.join("SAMPLES").join("PLG");
    let stall = root.join("SAMPLES").join("PLS");
    let aq = root.join("SAMPLES").join("AQ");
    let aq_heavy = root.join("SAMPLES").join("AQH");
    let aq_stall = root.join("SAMPLES").join("AQS");
    for dir in [
        &samples,
        &heavy,
        &lite,
        &gate,
        &stall,
        &aq,
        &aq_heavy,
        &aq_stall,
        &root.join("SYNTHS"),
        &root.join("KITS"),
    ] {
        std::fs::create_dir_all(dir).expect("creating the preset-load fixture directories");
    }
    write_sine_wav(&samples.join("PLSYN.WAV"), 1, 22_050, 440.0);
    write_sine_wav(&samples.join("PLROW.WAV"), 1, 22_050, 330.0);
    write_sine_wav(&samples.join("PLCLN.WAV"), 1, 22_050, 550.0);
    write_test_wav(&samples.join("PLKA.WAV"), 1, 16, 2048, 0x91);
    write_test_wav(&samples.join("PLKB.WAV"), 1, 16, 2560, 0x92);
    write_test_wav(&samples.join("PLKC.WAV"), 1, 16, 3072, 0x93);
    write_test_wav(&samples.join("PLBASE.WAV"), 1, 16, 2048, 0x94);
    write_sine_wav(&samples.join("PLRACE.WAV"), 1, 22_050, 660.0);
    // The nested preset's sample. Its preset lives in a subfolder of SYNTHS, reachable only by
    // descending: no other gate covers the walk's recursion.
    write_sine_wav(&samples.join("PLNEST.WAV"), 1, 22_050, 770.0);
    // 24 empty subfolders plus the one holding the preset: more than FILE_ITEMS_MAX_NUM_ELEMENTS
    // (20), so the folder pass culls and the walk must page to see the last entry. ZNEST sorts
    // after every W##, so the walk first returns from 24 empty descents — the path a change to the
    // walk is most likely to break.
    for i in 1..=24 {
        std::fs::create_dir_all(root.join("SYNTHS").join("WALK").join(format!("W{i:02}")))
            .expect("creating the preset-walk filler folders");
    }
    std::fs::create_dir_all(root.join("SYNTHS").join("WALK").join("ZNEST"))
        .expect("creating the preset-walk target folder");
    write_test_wav(&heavy.join("PLHA.WAV"), 1, 16, 8192, 0x95);
    write_test_wav(&heavy.join("PLHB.WAV"), 1, 16, 8192, 0x96);
    write_test_wav(&heavy.join("PLHC.WAV"), 1, 16, 8192, 0x97);
    write_test_wav(&lite.join("PLLITE.WAV"), 1, 16, 2048, 0x98);
    write_sine_wav(&gate.join("PLGATE.WAV"), 1, 22_050, 220.0);
    write_sine_wav(&stall.join("PLSTALL.WAV"), 1, 22_050, 110.0);
    write_sine_wav(&aq.join("AQSA.WAV"), 1, 22_050, 880.0);
    write_test_wav(&aq.join("AQKA.WAV"), 1, 16, 2048, 0xA1);
    write_test_wav(&aq.join("AQKB.WAV"), 1, 16, 2048, 0xA2);
    write_test_wav(&aq.join("AQKC.WAV"), 1, 16, 2048, 0xA3);
    write_test_wav(&aq.join("AQKD.WAV"), 1, 16, 2048, 0xA4);
    write_test_wav(&aq.join("AQKL.WAV"), 1, 16, 2048, 0xA5);
    write_test_wav(&aq_heavy.join("AQHA.WAV"), 1, 16, 8192, 0xA6);
    write_test_wav(&aq_heavy.join("AQHB.WAV"), 1, 16, 8192, 0xA7);
    write_test_wav(&aq_heavy.join("AQHC.WAV"), 1, 16, 8192, 0xA8);
    write_sine_wav(&aq_stall.join("PLSTB.WAV"), 1, 22_050, 990.0);
    install_staged(&root);
}

/// Stages the load-cancel scenario's card: the samples its six synth presets name, in `SAMPLES/LC/`, and an empty
/// `SYNTHS/` the scenario writes the presets into through the firmware's serialisers.
fn fill_load_cancel_card() {
    let root = staging_dir("deluge-load-cancel-card");
    let samples = root.join("SAMPLES").join("LC");
    for dir in [&samples, &root.join("SYNTHS")] {
        std::fs::create_dir_all(dir).expect("creating the load-cancel fixture directories");
    }
    write_sine_wav(&samples.join("LCA.WAV"), 1, 22_050, 440.0);
    write_sine_wav(&samples.join("LCB.WAV"), 1, 22_050, 330.0);
    write_sine_wav(&samples.join("LCC.WAV"), 1, 22_050, 550.0);
    write_sine_wav(&samples.join("LCD.WAV"), 1, 22_050, 660.0);
    write_sine_wav(&samples.join("LCE.WAV"), 1, 22_050, 770.0);
    write_sine_wav(&samples.join("LCF.WAV"), 1, 22_050, 880.0);
    install_staged(&root);
}

/// Stages the `clear_song_undo` scenario's card: the samples its preset and audio clip use, and the
/// empty folders the scenario writes into through the firmware.
fn fill_clear_song_undo_card() {
    let root = staging_dir("deluge-clear-song-undo-card");
    let samples = root.join("SAMPLES").join("CS");
    for dir in [
        &samples,
        &root.join("SYNTHS"),
        &root.join("KITS"),
        &root.join("SONGS"),
    ] {
        std::fs::create_dir_all(dir).expect("creating the clear-song-undo fixture directories");
    }
    write_sine_wav(&samples.join("CSSYN.WAV"), 1, 22_050, 440.0);
    // The two an AudioClip's sample change moves between, and the one the missing-file leg deletes
    // before undoing.
    write_sine_wav(&samples.join("CSUNA.WAV"), 1, 22_050, 330.0);
    write_sine_wav(&samples.join("CSUNB.WAV"), 1, 22_050, 550.0);
    write_sine_wav(&samples.join("CSGONE.WAV"), 1, 22_050, 660.0);
    install_staged(&root);
}

/// Stages the song-load channel scenario's card: folders to save into, and the one sample its song
/// carries. The gate saves and loads a song it writes itself, so it needs no golden fixture.
fn fill_song_load_channel_card() {
    let root = staging_dir("deluge-song-load-channel-card");
    let samples = root.join("SAMPLES").join("SL");
    for dir in [
        &samples,
        &root.join("SYNTHS"),
        &root.join("KITS"),
        &root.join("SONGS"),
    ] {
        std::fs::create_dir_all(dir).expect("creating the song-load-channel fixture directories");
    }
    write_sine_wav(&samples.join("SLSONG.WAV"), 1, 22_050, 440.0);
    install_staged(&root);
}

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

    // GOLDEN_SCENARIO selects the scenario: unset for the stem export, `recorder_roundtrip`, or a
    // stepped scenario by name.
    let scenario = std::env::var("GOLDEN_SCENARIO").unwrap_or_default();
    let recorder_mode = scenario == "recorder_roundtrip";
    let stepped = stepped_scenario(&scenario);
    log::info!(
        "golden_vt_render: fixture={fixture} budget_ms={budget_ms} scenario={}",
        if scenario.is_empty() {
            "stem_export"
        } else {
            scenario.as_str()
        }
    );
    // A name the table does not hold is a mistake (a scenario not registered in scenario_table.cpp,
    // or a typo), not a request for the stem export, which would run for minutes and report nothing
    // about the scenario.
    if !scenario.is_empty() && !recorder_mode && stepped.is_none() {
        eprintln!(
            "golden_vt_render: no scenario named '{scenario}' (tests/integration/scenarios/scenario_table.cpp)"
        );
        std::process::exit(2);
    }

    // The card must be chosen here, before any task is spawned (see the module doc).
    let repo_root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(5)
        .expect("CARGO_MANIFEST_DIR (src/bsp/rust/harness/golden_vt_render) has a repo root 5 levels up")
        .to_path_buf();
    // render_song and render_preset play as the device does: every block rendered on the audio
    // tier after boot, one 128-frame block per 2,902 virtual microseconds, so their timing checks
    // compare against the device's audio period. Set before the render task is spawned.
    if scenario == "render_song" || scenario == "render_preset" {
        audio_host::set_realtime_render(true);
        render::set_period_override_us(2902, 0);
    }
    // clock_input's analog-clock transport times its input ticks by AudioEngine::audioSampleTimer,
    // which only real rendering advances. It has no golden timing, so no period override.
    if scenario == "clock_input" {
        audio_host::set_realtime_render(true);
    }
    // ui_timer_deadline compares UI timers, which run on AudioEngine::audioSampleTimer, with the app
    // loop's virtual clock, so the sample clock keeps the device's pace: one block per 2,902 µs.
    // idle_job_rates counts the ui routine's runs, which follow the same timers, so it keeps that pace too.
    // recorder_take records the test input signal and plays a DIN script during the take, both of which follow the
    // render, so it renders every block at the device's pace too. stale_ui plays a DIN script and waits on an armed
    // song swap, which the playing song's loop launches, and on the UI timers it sets while the swap waits.
    if scenario == "ui_timer_deadline"
        || scenario == "idle_job_rates"
        || scenario == "recorder_take"
        || scenario == "stale_ui"
        || scenario == "ui_script"
    {
        audio_host::set_realtime_render(true);
        render::set_period_override_us(2902, 0);
    }
    // The card: a memory disk holding the scenario's fixture tree, unless DELUGE_SD_IMAGE names an
    // image file.
    if std::env::var_os("DELUGE_SD_IMAGE").is_none() {
        match scenario.as_str() {
            "recorder_roundtrip" | "sound_lifetime" | "clip_lifetime" | "container_guard"
            | "audio_header_dump" | "ui_timer_deadline" | "idle_job_rates" | "recorder_take"
            | "stale_ui" | "menu_dump" => memory_card::install(None),
            "audio_load_ab" => fill_audio_load_ab_card(),
            "song_preview_ab" => fill_song_preview_ab_card(&repo_root),
            "song_save_channel" => fill_song_save_card(),
            "sample_collect" => fill_sample_collect_card(),
            "card_reinsert" => fill_card_reinsert_card(),
            "sample_load_tree" => fill_sample_load_tree_card(),
            "sample_claim" => fill_sample_claim_card(),
            "preset_load" => fill_preset_load_card(),
            "load_cancel" => fill_load_cancel_card(),
            "load_policy" => fill_load_policy_card(),
            "clear_song_undo" => fill_clear_song_undo_card(),
            "song_load_channel" => fill_song_load_channel_card(),
            // The card tree to play from: RENDER_TREE, or the GOLDEN_FIXTURE corpus fixture's.
            "render_song" => {
                memory_card::install(Some(&std::env::var_os("RENDER_TREE").map_or_else(
                    || sd_image::golden_fixture_tree(&repo_root, &fixture),
                    std::path::PathBuf::from,
                )))
            }
            // The card tree run.py stages for one preset case; there is no fixture to fall back on.
            // A scenes spec file's card: SCENES_SPEC_CARD ("none" for an empty card), or the fixture.
            "scenes_spec" => match std::env::var("SCENES_SPEC_CARD").as_deref() {
                Ok("none") => memory_card::install(None),
                Ok(dir) => memory_card::install(Some(std::path::Path::new(dir))),
                Err(_) => memory_card::install(Some(&repo_root.join("tests/fixtures/card"))),
            },
            // The card a UI script plays on: UI_CARD, or the scripts' own fixture.
            "ui_script" => memory_card::install(Some(&std::env::var_os("UI_CARD").map_or_else(
                || repo_root.join("tests/fixtures/card"),
                std::path::PathBuf::from,
            ))),
            "render_preset" => memory_card::install(Some(&std::path::PathBuf::from(
                std::env::var_os("RENDER_TREE").expect("render_preset needs RENDER_TREE"),
            ))),
            _ => memory_card::install(Some(&sd_image::golden_fixture_tree(&repo_root, &fixture))),
        }
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
    if let Some(stepped) = stepped {
        let name: &'static str = Box::leak(scenario.clone().into_boxed_str());
        spawner.spawn(run_stepped_scenario(stepped, name, &DONE).unwrap());
    } else if recorder_mode {
        spawner.spawn(run_recorder_roundtrip_scenario(&DONE).unwrap());
    } else {
        let fixture_static: &'static str = Box::leak(fixture.into_boxed_str());
        spawner.spawn(run_stem_export_scenario(fixture_static, &DONE).unwrap());
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
