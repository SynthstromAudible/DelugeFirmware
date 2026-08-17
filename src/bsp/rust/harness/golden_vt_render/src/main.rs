//! `golden_vt_render`: an Embassy-host-BSP golden renderer. Boots the real C++
//! `deluge_app` on this repo's Rust/Embassy host substrate (the same
//! `raw::Executor` + `PeekableMockDriver` + no-thread pender shape
//! `../lens1_vt_sim/` pioneered for the streaming-underrun harness), mounts a
//! fixture SD-card image, dispatches a real offline `StemExport` run onto the
//! storage-owner worker fiber, copies the produced stem WAV(s) out of the
//! image, and exits cleanly. Sample cluster fills drain through the real async
//! Embassy `streaming_fill_task` during the offline render (the reader/overview
//! and voice-playback paths route their fills onto that drain under
//! `async_active`), so a sample-backed fixture renders real audio, not silence.
//!
//! # Why a sibling package, not a `lens1_vt_sim` `[[bin]]`
//!
//! `embassy-time`'s `std` feature (`deluge-bsp-rust`'s own host target) and a
//! custom/`mock-driver`-shaped time driver both define `#[no_mangle]
//! _embassy_time_now`/`_embassy_time_schedule_wake` — a hard duplicate-symbol
//! compile error if both land in one package's dependency graph (Cargo unifies
//! a dependency's features across every target in one Cargo.toml; there is no
//! per-`[[bin]]` override). See `Cargo.toml`'s header comment for the full
//! rationale — identical to `../lens1_vt_sim/Cargo.toml`'s.
//!
//! # Executor + clock
//!
//! `embassy_executor::raw::Executor` + a custom `AtomicBool` `__pender` + a
//! `PeekableMockDriver` ([`clock`] — the same discrete-event shape
//! `../lens1_vt_sim/` uses). The driver loop:
//! poll the executor to quiescence, peek the next due deadline, jump the
//! virtual clock to exactly that deadline, repeat. No `platform-std`/
//! `executor-thread` embassy-executor feature is enabled (see `Cargo.toml`),
//! so there is exactly one `__pender` in this binary and it is ours.
//!
//! # The boot livelock (see `../lens1_vt_sim/src/main.rs`'s module-doc section
//! for the full mechanism and its history)
//!
//! `deluge_app_init` calls the boot-time FatFS mount SYNCHRONOUSLY, off the
//! worker fiber, via `embassy_futures::block_on` — a tight busy-spin poll loop
//! that never returns control to anything else on this one OS thread. Under
//! `sim_latency`, that polled future's completion depends on a separately
//! spawned `pump()` task's `Timer::after(latency).await` being POLLED by the
//! executor — but the executor's own `poll()` call is already on the stack, so
//! nothing else can be polled until that whole call stack unwinds.
//! This package sidesteps the whole question by not enabling `sim_latency` at all (see
//! `Cargo.toml`: a golden renderer wants bit-exact output, not latency realism), so its host SD
//! reads resolve synchronously and there is no modeled `Timer` to wait on. In `lens1_vt_sim`,
//! which does model latency, the answer is the `sim_block::block_on` progress hook.
//!
//! # SD-image packing happens BEFORE any task is spawned
//!
//! `sd_image::pack_golden_fixture` + setting `DELUGE_SD_IMAGE` run
//! synchronously in `main`, before the executor exists or any task is
//! spawned — deliberately NOT inside [`run_stem_export_scenario`] itself,
//! even though that task is conceptually "the one that packs the fixture".
//! `deluge-bsp`'s host `sd.rs` resolves `DELUGE_SD_IMAGE` lazily, exactly
//! once, behind a `OnceLock` (`disk()`) the first time anything touches the
//! backing file — and [`scenario::run`]'s own doc comment already documents
//! the relevant hazard: "embassy doesn't guarantee poll order across tasks
//! spawned in the same batch". If packing+env-var-set instead lived inside
//! [`run_stem_export_scenario`]'s task body, a poll order that reaches
//! [`boot_task`]'s `sd::init()` first would lock the `OnceLock` onto the
//! WRONG (default) backing path before the real fixture image is ready —
//! silently, not a crash. Doing it in `main` (single-threaded, before the
//! executor exists at all) has no such race, matching `../lens1_vt_sim/src/main.rs`'s
//! own proven-safe placement of the identical call.
//!
//! # Reused shared substrate
//!
//! `#[path]`-includes the SAME source files `deluge-bsp-rust`'s own `host_app`
//! build (and `../lens1_vt_sim/`) compiles (`fiber.rs`, `scheduler.rs`, `sd.rs`,
//! `board.rs`, `control.rs`, `display.rs`, `ffi.rs`, `ffi_extra.rs`, `flash.rs`,
//! `services.rs`, `audio_host.rs`, `sd_image.rs`, `streaming_loader.rs`,
//! `efatfs_core.rs`, `efatfs_host_shim.rs`) — a second, independently
//! configured COMPILATION of the same source (this package's own `mod
//! sys`/Cargo features), not a fork: a change to any of these files is picked
//! up by every package that path-includes it. Deliberately does NOT
//! `#[path]`-include `scenario.rs` (unlike `../lens1_vt_sim/`): nothing this
//! package boots reaches into it, and this package's own scenario
//! ([`run_stem_export_scenario`]) is a `StemExport` drive, not the
//! streaming-underrun harness's play/record scenario that file wraps.
#![feature(impl_trait_in_assoc_type)]

use core::ffi::c_char;
use core::sync::atomic::{AtomicBool, Ordering};
use std::ffi::CString;

use embassy_executor::raw;
use embassy_time::{Duration, Instant, Timer};
use embassy_time_driver::Driver as _;

mod clock;

// Link-only: the host-built C++ `deluge_app` object closure (build.rs) calls the
// deluge_resource_* residency C ABI and the deluge_{alloc,slab_*,heap_*}
// allocator C ABI. `extern crate` forces the rlib onto the link line so those
// `#[no_mangle]` symbols resolve; nothing here references them from Rust
// (mirrors `../lens1_vt_sim/src/main.rs`'s identical `host_app`-gated
// declaration).
extern crate deluge_resource;
// Link-only, same reasoning as `deluge_resource` above: `sample_source.cpp`/
// `sample_reader_bridge.cpp` in the archived `deluge_app` object closure call
// straight into these crates' `deluge_sample_reserve_*`/`deluge_sample_peek`/
// `deluge_sample_read`/`deluge_sample_invalidate`/`deluge_sample_reader_*`
// `#[no_mangle]` symbols; nothing here references them from Rust, so without
// this `extern crate` rustc/lld would drop the whole rlib and the link would
// fail with undefined symbols (mirrors `../src/main.rs`'s identical
// `host_app`-gated declarations for both crates).
extern crate deluge_sample_reader;
extern crate deluge_sample_source;
// Link-only, same reasoning as the pair above: `deluge_sample_stream`'s `abi.rs` (a later task)
// will define the `deluge_sample_stream_*` C ABI. Nothing here references it from Rust yet (no
// consumer — that is a later task), so without this `extern crate` rustc/lld would drop the whole
// rlib; this just proves the symbols are present and compile clean.
extern crate deluge_sample_stream;

/// Real bindgen'd libdeluge C-ABI types (see `build.rs`).
#[allow(
    non_upper_case_globals,
    non_camel_case_types,
    non_snake_case,
    dead_code
)]
mod sys {
    include!(concat!(env!("OUT_DIR"), "/libdeluge_sys.rs"));
}

// Reuse deluge-bsp-rust's own source files verbatim via `#[path]` — the same
// pattern `../lens1_vt_sim/`/`tests/*.rs` already use (see `HOST_HARNESS.md`).
#[path = "../../../src/audio_host.rs"]
mod audio_host;
#[path = "../../../src/board.rs"]
mod board;
#[path = "../../../src/control.rs"]
mod control;
#[path = "../../../src/display.rs"]
mod display;
/// Storage-generic core of the efatfs read path — see `../../../src/main.rs`'s
/// `mod efatfs_core` doc. Gated on `host_app` (always on for this package) to
/// mirror that file's cfg exactly, even though this package's own
/// `#[cfg(feature = "efatfs_streaming")]` is what actually drives `mount()`
/// (below, in `boot_task`).
#[cfg(feature = "host_app")]
#[path = "../../../src/efatfs_core.rs"]
mod efatfs_core;
/// Host counterpart of the device `efatfs_fs.rs` — see `../../../src/main.rs`'s
/// `mod efatfs_host_shim` doc.
#[cfg(feature = "host_app")]
#[path = "../../../src/efatfs_host_shim.rs"]
mod efatfs_host_shim;
#[path = "../../../src/ffi.rs"]
mod ffi;
#[path = "../../../src/ffi_extra.rs"]
mod ffi_extra;
#[path = "../../../src/fiber.rs"]
mod fiber;
#[path = "../../../src/flash.rs"]
mod flash;
#[path = "../../../src/host_link_stubs.rs"]
mod host_link_stubs;
#[path = "../../../src/scheduler.rs"]
mod scheduler;
#[path = "../../../src/sd.rs"]
mod sd;
#[path = "../../../src/sd_image.rs"]
mod sd_image;
#[path = "../../../src/services.rs"]
mod services;
/// The storage spin seam. Needed here because `efatfs_host_shim.rs` now drives every FS
/// operation through `sim_block::block_on` unconditionally, matching the device bridge.
///
/// This package registers **no progress hook**, deliberately, and that is the right choice
/// rather than an omission: it is single-threaded (one executor, one discrete-event driver
/// loop), so no other thread exists to satisfy a `Pending`, and
/// `sim_block::use_threaded_host_hook` would only disarm wedge detection. With no hook a
/// `Pending` is reported as a wedge — which for this harness is the desired outcome, since a
/// storage future that cannot complete here should fail the byte-identity gate loudly rather
/// than silently. In practice nothing pends: this package does not enable `sim_latency`, so its
/// host SD reads resolve synchronously (see `Cargo.toml`'s `[features]` note on why latency
/// realism is deliberately off for a golden renderer).
#[path = "../../../src/sim_block.rs"]
mod sim_block;
/// The async cluster-fill task + its selector/wakeup C ABI. The
/// selector/wakeup symbols (`deluge_streaming_async_active`/
/// `deluge_streaming_signal_fill`) are always compiled so their call
/// sites link regardless of the `async_streaming_loader` feature (see that
/// file's module doc); the actual drain machinery +
/// [`streaming_loader::streaming_fill_task`] stay gated on the feature and are
/// only spawned (below, in `main`) when it's enabled — on by default for this
/// package (see `Cargo.toml`).
#[path = "../../../src/streaming_loader.rs"]
mod streaming_loader;

unsafe extern "C" {
    fn deluge_app_init(board: *const sys::DelugeBoard);
}

// Host-only C-ABI bridge (src/deluge/harness/streaming_scenario.{h,cpp},
// `DELUGE_HOST`-guarded — compiled into the linked host_app `deluge_app` object
// closure, never into the ARM device firmware). Manually declared (not through
// the bindgen `sys` module, same pattern `scenario.rs` uses for the identical
// bridge): these are harness-only entry points the app exposes to the platform,
// not part of the libdeluge C-ABI the app CONSUMES.
//
// The song-load quartet (`begin_song_load`/`song_listing_in_progress`/
// `commit_song_load`/`song_load_in_progress`) pre-dates this task (added for
// the streaming-underrun harness, `scenario.rs`'s own copy of these same
// declarations) — this scenario needs its own since it does NOT `#[path]`-
// include `scenario.rs` (see the module doc). Without loading a real song
// first, `currentSong` is still the blank boot-time song `deluge_boot()`
// leaves in place, and StemExport has nothing to export.
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
    fn deluge_scenario_start_recorder_roundtrip();
    fn deluge_scenario_recorder_roundtrip_done() -> bool;
    fn deluge_scenario_recorder_roundtrip_failures() -> i32;
}

/// Set once [`boot_task`] has run the efatfs mount, `deluge_app_init` (which
/// itself runs the C FatFS mount synchronously), and is about to enter its
/// worker-fiber pump loop. [`run_stem_export_scenario`] waits on this flag
/// before dispatching the stem export.
static BOOT_MOUNTED: AtomicBool = AtomicBool::new(false);

/// Boot task: mirrors `deluge-bsp-rust`'s `host_app_task` exactly (PIC-ready
/// wait, SD bring-up, efatfs mount, `deluge_app_init`, worker-fiber pump loop)
/// — see `../lens1_vt_sim/src/main.rs`'s `boot_task` doc comment for the full
/// rationale behind each step; this is the same ~15-line reimplementation (not
/// `#[path]`-included for the same reason: `host_app_task` lives inside
/// `../src/main.rs` itself, which carries a conflicting `#![no_std]`/device
/// `fn main`/`EXECUTOR` static).
#[embassy_executor::task]
async fn boot_task() {
    deluge_bsp::pic::wait_ready().await;
    crate::sd::boot_init().await;

    // Mirrors `deluge-bsp-rust`'s `host_app_task` efatfs mount (see its comment). This mount
    // runs off-fiber; with `sim_latency` off for this package its transfers are plain
    // synchronous reads, so there is no modeled delay for an off-fiber caller to get stuck on.
    // A failed mount must NOT abort boot — see the device/host_app_task comments this mirrors.
    #[cfg(feature = "efatfs_streaming")]
    match crate::efatfs_host_shim::mount().await {
        Ok(()) => log::info!("golden_vt_render: efatfs mounted"),
        Err(()) => {
            log::warn!("golden_vt_render: efatfs mount failed — streaming falls back to C FatFS");
        }
    }

    log::info!("golden_vt_render: deluge_app_init() (registers + spawns task runners)");
    // SAFETY: called once, after `scheduler::set_spawner` (main, below) and
    // before anything else touches `currentSong`/scheduler state.
    unsafe { deluge_app_init(board::deluge_board()) };
    BOOT_MOUNTED.store(true, Ordering::Release);
    log::info!("golden_vt_render: boot+mount complete; scheduler running; pumping async worker");

    use embassy_futures::select::select;
    loop {
        let busy = fiber::worker_poll();
        if busy {
            let _ = select(
                fiber::WORKER_WAKE.wait(),
                embassy_time::Timer::after_millis(8),
            )
            .await;
        } else {
            fiber::WORKER_WAKE.wait().await;
        }
    }
}

/// In-image `SONGS/*.XML` path for `fixture` — the same `SONG_XML` values
/// `scripts/golden_mixdown.sh`'s per-fixture `case` uses (the packed image
/// flattens each fixture's song to `SONGS/<name>.XML`, dropping the
/// `problem_songs/...` subdirectory the local backup corpus nests it under —
/// see `sd_image::pack_golden_fixture`). `GOLDEN_STEM_SONG` overrides for a
/// fixture not in this table.
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

/// `StemExportType` to export — matches the C++ enum's values
/// (`definitions_cxx.hpp`: CLIP=0, TRACK=1, DRUM=2, MIXDOWN=3), carried as the
/// `int32_t` `deluge_scenario_start_stem_export` takes. `GOLDEN_STEM_MODE`
/// mirrors `deluge_render`'s own `--mode` flag (`host_render_main.cpp`'s
/// `parse_mode`); defaults to TRACK, same as that tool.
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

/// The render scenario: waits for [`boot_task`] to finish booting + mounting,
/// dispatches a real offline `StemExport` run onto the storage-owner worker
/// fiber (`deluge_scenario_start_stem_export`), polls the driver to quiescence
/// until it completes (`deluge_scenario_stem_export_done`), copies the produced
/// stem WAV(s) out of the mounted image (`deluge_scenario_copy_stems_out` —
/// mirrors `host_render_main.cpp`'s `copy_stems_out`), then declares the run
/// done. `fixture` is already packed and pointed at by `DELUGE_SD_IMAGE` before
/// this task is even spawned (see the module doc's "SD-image packing" section)
/// — it's threaded through here only so the log lines identify which fixture
/// this run is against.
///
/// Cluster fills DO drain here: `async_active` is on for this package (see
/// `Cargo.toml`'s `default` features), so `StemExport::renderWait`'s offline
/// loop drains the loader queue through the real async `streaming_fill_task`
/// (via `deluge_streaming_drain_queue_blocking`). A sample-backed fixture
/// therefore renders real audio, not silence — this task proves both the
/// plumbing (real WAV files, right count/structure, clean exit) and, with the
/// fills drained, sample-content correctness.
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

    // Load the fixture's real song first — StemExport exports whatever
    // `currentSong` currently is, and boot leaves it as the blank template
    // song otherwise. Same two-phase dispatch/poll shape `scenario.rs`'s
    // `run()` uses for the identical C-ABI (see `streaming_scenario.h`'s "Why
    // two-phase song load" doc): `begin_song_load` only DISPATCHES the async
    // directory listing.
    let song_path = fixture_song_path(fixture);
    log::info!("golden_vt_render: boot+mount confirmed for fixture={fixture}; loading {song_path}");
    let song_path_c =
        CString::new(song_path.clone()).expect("fixture_song_path never contains a NUL byte");
    // Dispatched onto the worker fiber (deluge_scenario_start_song_load), NOT
    // called inline — see that C-ABI entry's doc: called directly from this
    // off-fiber Rust task, deluge_scenario_begin_song_load()'s own
    // openUI(&loadSongUI) chain does a real storage read that can livelock the
    // process under sim_latency.
    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has already run
    // `deluge_app_init`, so `currentSong` exists. `song_path_c` outlives the call.
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
            "golden_vt_render: deluge_scenario_commit_song_load() returned false (owner queue rejected it)"
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
    // SAFETY: DELUGE_HOST harness C-ABI; the song load above has completed, so
    // `currentSong` is the fixture's real song. Fire-and-forget: this only
    // enqueues the export onto the storage-owner worker fiber (see the C++ doc
    // comment) — it does not itself run the export.
    unsafe { deluge_scenario_start_stem_export(mode) };

    // The export runs as ONE synchronous worker-fiber op once dispatched (no
    // yield points inside it yet — see the module doc's fill-drain note), so
    // this wait budget only needs to cover real wall-clock render time, not a
    // wedge in the usual "stuck forever" sense; keep it generous regardless.
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

/// The `GOLDEN_SCENARIO=recorder_roundtrip` alternative to [`run_stem_export_scenario`]:
/// runs the SampleRecorder byte-exact round-trip oracle (the ex-`deluge_recorder_roundtrip`
/// binary, now the `deluge_scenario_run_recorder_roundtrip` harness scenario) on THIS Embassy
/// substrate. Waits for boot+mount, dispatches the oracle onto the storage-owner worker fiber
/// (`deluge_scenario_start_recorder_roundtrip` — where its file I/O and the finalized probe's
/// async fill drain can block/resume), polls to quiescence until it finishes, then exits with
/// its result. Unlike the stem-export scenario, no song load and no fixture image: `main`
/// formats an EMPTY FAT image (the recorder only ever writes new files) and leaves
/// `DELUGE_SD_ROOT` unset so the finalized probe reads efatfs on the same image it wrote.
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

    log::info!(
        "golden_vt_render: boot+mount confirmed; dispatching recorder round-trip onto the worker fiber"
    );
    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_start_recorder_roundtrip() };

    let deadline = Instant::now() + Duration::from_secs(300);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_recorder_roundtrip_done() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: recorder round-trip did not complete within the 300s wait budget — wedged"
            );
            hard_exit(2);
        }
        Timer::after_millis(5).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_recorder_roundtrip_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: recorder round-trip FAILED ({failures} case(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: recorder round-trip PASSED (0 failures); exiting cleanly");
    done.store(true, Ordering::Release);
}

/// Our own pender: no thread, no parking — just a flag the driver loop polls
/// itself between `raw::Executor::poll()` calls.
static PENDED: AtomicBool = AtomicBool::new(false);

#[unsafe(export_name = "__pender")]
fn pender(_context: *mut ()) {
    PENDED.store(true, Ordering::SeqCst);
}

fn main() {
    // See `../lens1_vt_sim/src/main.rs`'s identical NOTE: a `--release` build
    // of this package also compiles every `log::*!` call to a no-op
    // (`deluge-bsp`/`rza1l-hal` request `log`'s `release_max_level_off`
    // feature for the DEVICE build's benefit, and Cargo unifies that feature
    // across this whole binary's dependency graph). Use a debug build (`cargo
    // build`, no `--release`) when you need the log output.
    env_logger::init();
    log::info!("golden_vt_render: Embassy-host-BSP golden renderer (boot + mount rung)");

    let fixture = std::env::var("GOLDEN_FIXTURE").unwrap_or_else(|_| "cordae".to_string());
    // Bound on VIRTUAL time simulated before giving up (NOT wall time — see
    // `drive`'s doc comment below): generous, since a wedged boot should fail
    // loudly rather than "simulate" forever.
    let budget_ms: u64 = std::env::var("GOLDEN_VIRTUAL_BUDGET_MS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(60_000);

    // GOLDEN_SCENARIO selects what this binary runs on the Embassy substrate: the default
    // stem-export golden render, or the SampleRecorder byte-exact round-trip oracle (the
    // ex-`deluge_recorder_roundtrip`, converged onto Embassy so the C++ sync fill drain can be
    // retired). The recorder scenario ignores GOLDEN_FIXTURE — it formats an empty image and
    // records into it rather than loading a corpus song.
    let recorder_mode = std::env::var("GOLDEN_SCENARIO").as_deref() == Ok("recorder_roundtrip");

    log::info!(
        "golden_vt_render: fixture={fixture} budget_ms={budget_ms} recorder_mode={recorder_mode}"
    );

    // Pack (or reuse) a real FAT SD image from the golden corpus — same
    // tooling `../lens1_vt_sim/`'s scenario driver uses. Must run before
    // anything below touches SD, and MUST run here (single-threaded, before
    // any task is spawned) rather than inside a task — see the module doc's
    // "SD-image packing happens BEFORE any task is spawned" section.
    let repo_root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(5)
        .expect("CARGO_MANIFEST_DIR (src/bsp/rust/harness/golden_vt_render) has a repo root 5 levels up")
        .to_path_buf();
    if std::env::var_os("DELUGE_SD_IMAGE").is_none() {
        // Recorder mode records into a fresh EMPTY image (it only ever writes new files); the
        // stem-export golden packs the fixture's corpus song + samples.
        let img = if recorder_mode {
            sd_image::format_empty_image()
        } else {
            sd_image::pack_golden_fixture(&repo_root, &fixture)
        };
        // SAFETY: single-threaded at this point (before any task/executor exists).
        unsafe { std::env::set_var("DELUGE_SD_IMAGE", &img) };
    }
    // Same flag `host_render_main.cpp`'s `main()` sets before its own
    // `deluge_main()` — `audio_host.rs`'s `deluge_audio_drive` reads it to trim
    // redundant per-block diagnostic bookkeeping during the offline export (see
    // its `render_mode()` doc comment). `host_app`/`lens1_vt_sim` never set this,
    // so this is inert everywhere but this binary.
    // SAFETY: single-threaded at this point (before any task/executor exists).
    unsafe { std::env::set_var("DELUGE_RENDER", "1") };

    // SAFETY: `raw::Executor` needs `&'static`; leaking a Box is the standard
    // pattern for a `fn main` that doesn't itself run forever
    // (`embassy_executor::Executor::run`'s own doc uses the same pattern).
    let executor: &'static raw::Executor =
        Box::leak(Box::new(raw::Executor::new(std::ptr::null_mut())));
    let spawner = executor.spawner();
    scheduler::set_spawner(spawner);

    // Same BSP task set `deluge-bsp-rust`'s host_app boot block /
    // `../lens1_vt_sim/` spawns (minus the audio-thread split — with
    // `scheduler::set_audio_spawner` never called, the priority-0 audio task
    // falls back to this single spawner automatically, the documented
    // fallback path in `scheduler.rs`'s `claim`).
    spawner.spawn(control::pic_pump().unwrap());
    spawner.spawn(control::pad_render().unwrap());
    spawner.spawn(control::encoder_wake_pump().unwrap());
    spawner.spawn(display::oled_render().unwrap());
    // Only spawned under `sim_latency` (see Cargo.toml's `default` doc): with the feature off
    // there is no modeled-latency queue for this task to pump.
    #[cfg(feature = "sim_latency")]
    spawner.spawn(sd::sim_latency::pump().unwrap());
    spawner.spawn(boot_task().unwrap());
    // The async cluster-fill task, on the SAME executor `boot_task`'s
    // worker-fiber pump loop and the C++ enqueue path run on (this binary has
    // only the one executor — see the module doc's "Executor + clock"). Owns
    // the loader queue only once `deluge_streaming_async_active()` reports
    // true (i.e. only under this feature — on by default for this package);
    // inert otherwise.
    #[cfg(feature = "async_streaming_loader")]
    spawner.spawn(streaming_loader::streaming_fill_task().unwrap());

    static DONE: AtomicBool = AtomicBool::new(false);
    if recorder_mode {
        spawner.spawn(run_recorder_roundtrip_scenario(&DONE).unwrap());
    } else {
        let fixture_static: &'static str = Box::leak(fixture.into_boxed_str());
        spawner.spawn(run_stem_export_scenario(fixture_static, &DONE).unwrap());
    }

    // --- Discrete-event driver loop -----------------------------------------
    // Same shape as `../lens1_vt_sim/src/main.rs`'s driver loop: poll to
    // quiescence, then jump the virtual clock straight to the next due
    // deadline (rather than stepping tick-by-tick) until the scenario signals
    // done or the virtual-time budget/wedge detection trips.
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

/// Immediate, unconditional process termination — skips libc's atexit-run C++
/// static destructors. Same rationale (and same raw `exit_group` shape) as
/// `../lens1_vt_sim/src/main.rs`'s `hard_exit`: the real C++ app links objects
/// with static storage duration (`AudioEngine`, `playbackHandler`, …) that
/// register `__cxa_atexit` destructors at process start. A normal
/// `std::process::exit` still runs those destructors — observed elsewhere as a
/// SIGSEGV (exit code 139) AFTER the run had already succeeded, i.e.
/// destructor ordering against some other still-referenced global, not a bug
/// in the scenario itself. This process never needs an orderly C++ shutdown
/// (there is no real device shutdown path either), so skip it.
///
/// Flushes stdout/stderr first: `log`/`env_logger` (via its `anstream`
/// dependency) buffers writes rather than flushing per line, so without this,
/// a run that fails fast could exit with its diagnostic log output silently
/// discarded.
fn hard_exit(code: i32) -> ! {
    use std::io::Write as _;
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
    // 32-bit ARM EABI (the arm-linux/qemu golden oracle): exit_group is syscall
    // 248, taken in r7, argument in r0, via `svc #0`. Same "skip C++ static
    // destructors" rationale as the x86-64 path above.
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
