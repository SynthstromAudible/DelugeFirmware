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
#![recursion_limit = "512"]

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
// Link-only, same reasoning as `deluge_resource` above: the archived `deluge_app` object closure's
// `audio_file_format.cpp` calls straight into this crate's `deluge_audio_header_*` `#[no_mangle]`
// symbols, whose sole definition it is; nothing here references them from Rust.
extern crate deluge_audio_format;
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
/// The async audio-file load task and its request/collect C ABI: the AWAITED file I/O of a
/// card sample's load (open a candidate, size it, walk its header), publishing a result the
/// C++ collector picks up on its own tier.
///
/// Gated on `async_audio_load` rather than compiled unconditionally the way `../src/main.rs`
/// compiles it, so the default build of this package — the golden stem-export/recorder gate —
/// is unchanged by this module's existence. See `Cargo.toml`'s feature comment.
#[cfg(feature = "async_audio_load")]
#[path = "../../../src/audio_loader.rs"]
mod audio_loader;
#[path = "../../../src/board.rs"]
mod board;
/// `deluge_card_flush` — the ejection hook that bumps the shared card generation and then clears
/// the loader's tables. Gated with `audio_loader` above rather than with `../src/main.rs`'s
/// device/`host_app` pair, because it calls into that module: without the loader compiled there is
/// nothing here to clear, and `card_abi.cpp`'s weak fallback in the `deluge_app` object closure
/// resolves the symbol instead.
#[cfg(feature = "async_audio_load")]
#[path = "../../../src/card.rs"]
mod card;
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
/// The single-slot whole-file byte channel and its request/collect C ABI. Gated like
/// `file_directory` below, for the identical structural reason: `deluge_card_flush` names every
/// module it flushes, so a build that compiles `card.rs` without this one does not compile.
///
/// Also on `async_file_bytes` and not just `async_audio_load`, or a leg that enables ONLY the byte
/// channel would silently test nothing: the module would not be compiled, the C++ weak fallback
/// would resolve `deluge_file_bytes_async_active` to false, and every settings read and write would
/// quietly take the synchronous arm while the leg reported a green run. That is not hypothetical --
/// it is what this file did on the first channel-on run of the settings gate, and `SC-CHANNEL
/// served=0` is what caught it.
#[cfg(any(feature = "async_audio_load", feature = "async_file_bytes"))]
#[path = "../../../src/file_bytes.rs"]
mod file_bytes;
/// The single-slot directory channel and its request/collect C ABI. Gated with `card` above and
/// `file_prefix` below, for the identical structural reason: `deluge_card_flush` names all three
/// of the modules it flushes, so a build that compiles `card.rs` without this one does not compile.
/// Its own two features stay off here, so all that lands in the golden binary is the slot logic and
/// the C ABI over an empty channel.
/// Also on `async_file_directory` and not just `async_audio_load`, or a leg that enables ONLY the
/// directory channel would silently test nothing: the module would not be compiled, the C++ weak
/// fallback would resolve `deluge_file_directory_async_active` to false, and every listing would
/// quietly take the synchronous arm while the leg reported a green run.
#[cfg(any(feature = "async_audio_load", feature = "async_file_directory"))]
#[path = "../../../src/file_directory.rs"]
mod file_directory;
/// The single-slot file-prefix channel and its request/collect C ABI. Gated with `card` above
/// rather than on a feature of its own, and that is structural, not a preference:
/// `deluge_card_flush` names BOTH loaders it flushes, so a build that compiles `card.rs` without
/// this module does not compile at all. Its own two features stay off here, so all that lands in
/// the golden binary is the slot logic and the C ABI over an empty channel.
#[cfg(feature = "async_audio_load")]
#[path = "../../../src/file_prefix.rs"]
mod file_prefix;
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
/// `sd_image`'s content-addressed image naming/reuse policy (same module the parent crate
/// registers) — `sd_image.rs` reaches it as `crate::sd_image_path`, so every crate that
/// `#[path]`-includes `sd_image.rs` must include this alongside it.
#[path = "../../../src/sd_image_path.rs"]
mod sd_image_path;
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
    fn deluge_scenario_audio_load_ab_begin();
    fn deluge_scenario_audio_load_ab_complete() -> bool;
    fn deluge_scenario_audio_load_ab_dispatch_step();
    fn deluge_scenario_audio_load_ab_step_done() -> bool;
    fn deluge_scenario_audio_load_ab_failures() -> i32;

    fn deluge_scenario_save_roundtrip_begin();
    fn deluge_scenario_save_roundtrip_complete() -> bool;
    fn deluge_scenario_save_roundtrip_dispatch_step();
    fn deluge_scenario_save_roundtrip_step_done() -> bool;
    fn deluge_scenario_save_roundtrip_failures() -> i32;

    fn deluge_scenario_settings_channel_begin();
    fn deluge_scenario_settings_channel_complete() -> bool;
    fn deluge_scenario_settings_channel_dispatch_step();
    fn deluge_scenario_settings_channel_step_done() -> bool;
    fn deluge_scenario_settings_channel_failures() -> i32;

    fn deluge_scenario_startup_failsafe_begin();
    fn deluge_scenario_startup_failsafe_complete() -> bool;
    fn deluge_scenario_startup_failsafe_dispatch_step();
    fn deluge_scenario_startup_failsafe_step_done() -> bool;
    fn deluge_scenario_startup_failsafe_failures() -> i32;

    fn deluge_scenario_path_operations_begin();
    fn deluge_scenario_path_operations_complete() -> bool;
    fn deluge_scenario_path_operations_dispatch_step();
    fn deluge_scenario_path_operations_step_done() -> bool;
    fn deluge_scenario_path_operations_failures() -> i32;

    fn deluge_scenario_offset_io_begin();
    fn deluge_scenario_offset_io_complete() -> bool;
    fn deluge_scenario_offset_io_dispatch_step();
    fn deluge_scenario_offset_io_step_done() -> bool;
    fn deluge_scenario_offset_io_failures() -> i32;
    fn deluge_scenario_song_preview_ab_begin();
    fn deluge_scenario_song_preview_ab_complete() -> bool;
    fn deluge_scenario_song_preview_ab_dispatch_step();
    fn deluge_scenario_song_preview_ab_step_done() -> bool;
    fn deluge_scenario_song_preview_ab_failures() -> i32;
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

/// Write a minimal, genuinely parseable WAV: canonical 44-byte header, then `data_bytes` bytes of
/// data all equal to `fill`.
///
/// The fill is what makes two files that agree on every geometry field still tell each other apart
/// when the C++ side fingerprints the bytes it reads back through the Sample's own stream — which is
/// how `SAMPLES/BOTH.WAV` and `SAMPLES/ALT/BOTH.WAV` are distinguished at all.
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

/// Stage the A/B scenario's card and pack it into a FAT image.
///
/// The layout is the fixture, and every part of it is load-bearing:
///
/// - `SAMPLES/BOTH.WAV` exists in the regular directory AND in `SAMPLES/ALT`, with a different
///   length and a different data fill in each. A backing that tries the candidates in the wrong
///   order picks the other one and the geometry, the recorded path and the stream fingerprint all
///   move together.
/// - `SAMPLES/BOTH2.WAV` is the same two-locations shape, and exists because `BOTH.WAV` alone does
///   not actually test the candidate ORDER: it is asked for before the alternate directory has been
///   probed, so its request carries one candidate and any order picks it. `BOTH2.WAV` is asked for
///   after the probe, with both candidates live. Reversing the loader's candidate order changes
///   this file and only this file — which is how the gate was shown to be able to fail on file
///   identity rather than only on geometry.
/// - `SAMPLES/ONLYALT.WAV` exists ONLY in `SAMPLES/ALT`. It is the file that the alternate-dir probe
///   has to find, and the one whose miss promotes `alternateLoadDirStatus` for every later case.
/// - `SAMPLES/ONLYREG.WAV` and `SAMPLES/STEREO24.WAV` come after that promotion, so they are the
///   cases that see the changed try order — and `STEREO24.WAV`'s geometry (2 channels, 24-bit)
///   differs in both fields the comparison names first.
/// - `SAMPLES/GARBAGE.WAV` opens and then fails to parse; `SAMPLES/MISSING.WAV` is never written.
///   Between them they pin that the two backings fail with the SAME `Error` identity, not merely
///   that they both fail.
/// - `SAMPLES/ALT/SELFDIR.WAV` sits INSIDE the alternate directory, which is the only shape where
///   the bare-name alternate candidate and the regular path are the same string — so "an alternate
///   won" can only be decided by candidate position, never by comparing paths.
/// - `SAMPLES/PREVIEW.WAV` belongs to the preview re-drive stage rather than the case list: it is
///   the file `SampleBrowser` is asked to preview once, and must then load and draw with nothing
///   but per-tick calls driving it.
/// - `SAMPLES/CANCEL.WAV` belongs to the cancellation stage, which asserts the converse: it is
///   asked for and then abandoned before its load runs, and must never load or draw at all.
/// - `SAMPLES/SWAP.WAV`, `SAMPLES/SWAPB.WAV` and `SAMPLES/WITNESS.WAV` belong to the card-ejection
///   stage: the first is parsed and left uncollected, the second is moved onto its path once the
///   card has been marked ejected, and the third is what tells the stage the first has finished
///   parsing. Their geometries are deliberately unlike each other.
///
/// Both legs stage byte-IDENTICAL content, which is what makes the comparison sound. They do NOT
/// share one image: `sd_image_path::fingerprint_dir` hashes path/length/mtime, not bytes, and this
/// function rewrites every file on each process start, so the two legs necessarily fingerprint
/// differently and each packs its own 2.5 GB sparse image. `scripts/audio_load_ab.sh` removes both
/// afterwards — an accumulation of those under `/tmp/deluge-streaming-scenario-*.img` is a known way
/// to wedge an unrelated stem export on this project.
fn pack_audio_load_ab_image() -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-audio-load-ab-card");
    let _ = std::fs::remove_dir_all(&root);
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
    // The nested case: three DIFFERENT locations, three different lengths. The regular path is
    // nested, which is the only shape for which the flattened alternate candidate
    // (`ALT/KIT_SNARE.WAV`) and the bare-name one (`ALT/SNARE.WAV`) are distinct strings at all.
    write_test_wav(&kit.join("SNARE.WAV"), 1, 16, 1024, 0x88);
    write_test_wav(&alt.join("KIT_SNARE.WAV"), 1, 16, 2304, 0x99);
    write_test_wav(&alt.join("SNARE.WAV"), 1, 16, 3072, 0xAA);
    // The self-directory case: the file lives IN the alternate directory, so
    // `alternateAudioFileLoadPath` is its own directory and the bare-name alternate
    // candidate stringifies to exactly the nominal path. Only a positional reading of
    // "which candidate won" calls that an alternate hit -- see the `selfdir` entry in
    // `audio_load_ab_scenario.cpp`'s kCases. There is deliberately NO flattened
    // `ALT/ALT_SELFDIR.WAV` companion: the flattened candidate must miss so the
    // ambiguous bare-name one is the one that wins.
    write_test_wav(&alt.join("SELFDIR.WAV"), 1, 16, 1280, 0xBB);
    // The preview stage's file (see the same scenario's kPreviewPath): regular path
    // only, and asked for by no case above, so it is never resident when that stage
    // begins and its load necessarily goes through the loader on the async leg.
    write_test_wav(&samples.join("PREVIEW.WAV"), 1, 16, 2816, 0xCC);
    // The cancellation stage's file (kCancelPath): asked for once and then abandoned before the
    // storage owner runs the load. It is a real, loadable file on purpose -- the stage asserts that
    // NOTHING loaded or drew it, and a file that could not have loaded anyway would let that pass
    // for the wrong reason.
    write_test_wav(&samples.join("CANCEL.WAV"), 1, 16, 3328, 0xDD);

    // The card-ejection stage's three files (see `audio_load_ab_scenario.cpp`'s kSwapPath block
    // comment). `SWAP.WAV` is the file the loader parses BEFORE the ejection; `SWAPB.WAV` is the
    // "other card's" file, moved onto that same path once the card has been marked gone. They differ
    // in every field the stage asserts — 1 vs 2 channels, 16- vs 24-bit, 2048 vs 6144 data bytes —
    // so a header parsed from the first and applied to the second cannot coincide with a correct
    // load. `WITNESS.WAV` is queued behind `SWAP.WAV` and drained after it, which is what lets the
    // stage know the swap file's result has been published without guessing at a delay.
    write_test_wav(&samples.join("SWAP.WAV"), 1, 16, 2048, 0xE1);
    write_test_wav(&samples.join("SWAPB.WAV"), 2, 24, 6144, 0xE2);
    write_test_wav(&samples.join("WITNESS.WAV"), 1, 16, 1536, 0xE3);

    std::fs::write(samples.join("GARBAGE.WAV"), vec![0x7Eu8; 512])
        .expect("writing the A/B fixture's unparseable file");
    // A third failure shape: the file opens and there is nothing in it.
    std::fs::write(samples.join("EMPTY.WAV"), Vec::<u8>::new())
        .expect("writing the A/B fixture's zero-length file");

    let img = sd_image::pack_image(&root);
    // The staging tree has served its purpose once the image exists, and leaving it in the system
    // temp dir is the same kind of litter as leaving the images themselves (see
    // scripts/audio_load_ab.sh's cleanup): this harness runs on machines where a full /tmp is a
    // known way to wedge an unrelated stem export.
    let _ = std::fs::remove_dir_all(&root);
    img
}

// ── The song-preview A/B fixture ────────────────────────────────────────────
//
// `src/deluge/harness/song_preview_ab_scenario.cpp` names every file written below, and asserts the
// artwork each one must produce against these very constants. The two must be read together.

/// Uniform preview channel value per synthetic song, mirroring the C++ scenario's constants of the
/// same names. All different, so no two songs' artwork can be mistaken for each other -- which is
/// the entire content of the card-swap assertion.
const PREVIEW_PAD_RENDER: u8 = 0x30;
const PREVIEW_PAD_JSON: u8 = 0x48;
const PREVIEW_PAD_LATE: u8 = 0x50;
const PREVIEW_PAD_SWAP_A: u8 = 0x20;
const PREVIEW_PAD_SWAP_B: u8 = 0x60;

/// Hex characters one full preview payload needs: (16 + 2 sidebar) columns x 8 rows x 3 colours x 2
/// characters. `runPreviewWalk` reads exactly this and stops; a shorter payload renders a partial
/// image, which is a different fixture from the one the C++ side asserts against.
const PREVIEW_HEX_CHARS: usize = (16 + 2) * 8 * 3 * 2;

/// A preview payload whose every pad is `value` on every channel.
fn uniform_preview(value: u8) -> String {
    let pair = format!("{value:02X}");
    pair.repeat(PREVIEW_HEX_CHARS / 2)
}

/// A minimal song document the real deserializer accepts: the root `song` tag with the firmware
/// attributes `openXMLFile` looks for, then whatever `body` adds, then the closing tag.
///
/// Modelled attribute-for-attribute on the corpus (`firmwareVersion` then
/// `earliestCompatibleFirmware` then `previewNumPads` then `preview`), because the preview walk
/// steps over the first two and READS the third, so their presence and order are part of what the
/// decode is being asked to handle.
fn song_xml(body: &str) -> String {
    format!(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n\tfirmwareVersion=\"c1.3.0\"\n\t\
         earliestCompatibleFirmware=\"4.1.0-alpha\"\n{body}>\n</song>\n"
    )
}

/// A song whose artwork is a single uniform colour.
fn uniform_preview_song(value: u8) -> String {
    song_xml(&format!(
        "\tpreviewNumPads=\"144\"\n\tpreview=\"{}\"\n",
        uniform_preview(value)
    ))
}

/// Stage the song-preview A/B scenario's card and pack it into a FAT image.
///
/// Every file is one SHAPE of answer the two backings must agree about, and the mix is the whole
/// fixture:
///
/// - `SONGS/RENDER.XML` and `SONGS/JSONP.Json` are ordinary songs with artwork inside the read
///   window, one through each deserializer. The JSON one is the checked-in
///   `tests/data/songs/JsonPreview.Json` with its payload rewritten to a uniform colour, rather than
///   hand-written here: that file is the only JSON song this project has, and reproducing its exact
///   structure by hand is how a fixture ends up testing the fixture.
/// - `SONGS/NOPREV.XML` has no `preview` attribute and is SHORTER than the read window. The
///   shortness is load-bearing: a longer one would legitimately report Truncated on the buffer
///   backing and NoPreview on the file backing, which is `LATE.XML`'s job rather than a bug.
/// - `SONGS/CORRUPT.XML` opens and is not a song; `SONGS/EMPTY.XML` opens and has nothing in it;
///   and `SONGS/MISSING.XML` is deliberately NOT written. Three different ways to fail, kept apart
///   because what has to match is the reported `Error` IDENTITY, not the mere fact of failure.
///   `MISSING.XML` is the one the two backings used to disagree about in silence.
/// - `SONGS/LATE.XML` carries a filler attribute long enough to push `preview` past the read
///   window, which is the one case whose two backings must legitimately differ (Rendered against
///   Truncated).
/// - `SONGS/SWAP.XML` and `SONGS/SWAPB.XML` are the card-swap pair: the second is moved onto the
///   first's path once the card has been marked ejected, and their artwork differs.
/// - `SONGS/CORPUS/C%02u.XML` are real songs copied from `DELUGE_SONG_CORPUS` when it is set. They
///   carry no absolute expectation -- their artwork is whatever their author made -- and what they
///   add is real documents: real attribute orders, real firmware versions, real preview offsets.
///   Contiguous from zero, because the C++ side probes for them by index and stops at the first gap.
fn pack_song_preview_ab_image(repo_root: &std::path::Path) -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-song-preview-ab-card");
    let _ = std::fs::remove_dir_all(&root);
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
    // Opens, is not a song. `openXMLFile` walks to the end without finding its root tag.
    write(
        songs.join("CORRUPT.XML"),
        String::from(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<notasong version=\"1\">\n</notasong>\n",
        ),
    );
    // Opens and has nothing in it -- a DIFFERENT failure from a file that is not there, and the one
    // that makes "READY with zero bytes" mean exactly one thing on the prefix channel.
    write(songs.join("EMPTY.XML"), String::new());
    // The read-window case: filler long enough that `preview` starts well past 4096 bytes, so the
    // buffer backing runs out of bytes (Truncated) where the file backing renders.
    write(
        songs.join("LATE.XML"),
        song_xml(&format!(
            "\tfiller=\"{}\"\n\tpreviewNumPads=\"144\"\n\tpreview=\"{}\"\n",
            "F".repeat(6000),
            uniform_preview(PREVIEW_PAD_LATE)
        )),
    );

    // The JSON song, taken from the checked-in fixture with its payload rewritten. Its 864-character
    // preview is a run of hex, so the substitution is a straight slice replacement.
    let json_source = repo_root.join("tests/data/songs/JsonPreview.Json");
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

    // Real songs, when the maintainer's corpus is mounted. Absent, the synthetic cases above still
    // run and the gate still gates -- what is lost is coverage of documents nobody here wrote.
    let corpus = std::env::var_os("DELUGE_SONG_CORPUS").or_else(|| {
        std::env::var_os("DELUGE_BACKUP").map(|b| std::path::Path::new(&b).join("SONGS").into())
    });
    let mut copied = 0usize;
    let corpus_was_named = corpus.is_some();
    if let Some(corpus) = corpus {
        let mut entries: Vec<std::path::PathBuf> = std::fs::read_dir(&corpus)
            .map(|dir| {
                dir.filter_map(Result::ok)
                    .map(|e| e.path())
                    .filter(|p| {
                        p.is_file()
                            && p.extension()
                                .and_then(|e| e.to_str())
                                .is_some_and(|e| e.eq_ignore_ascii_case("xml"))
                    })
                    .collect()
            })
            .unwrap_or_default();
        // Sorted, so the same corpus directory produces the same case order on both legs and across
        // runs. `read_dir` order is the filesystem's and is not stable.
        entries.sort();
        for path in entries.iter().take(MAX_CORPUS_CASES) {
            let target = corpus_dir.join(format!("C{copied:02}.XML"));
            if std::fs::copy(path, &target).is_ok() {
                copied += 1;
            }
        }
        // Naming a corpus and getting none of it staged is a failure, not a quiet fallback to the
        // six synthetic cases. It is the same species of hole as a coverage check that can cover
        // nothing: the run would still print PASS, and the only trace that fourteen real songs had
        // silently left the gate would be a count in a log line nobody reads.
        assert!(
            copied > 0,
            "DELUGE_SONG_CORPUS names {} but no .XML song could be staged from it",
            std::path::Path::new(&corpus).display()
        );
    }
    // Written UNCONDITIONALLY, zero included: the C++ side reads this rather than probing for songs
    // by index, so its absence has to be a failure over there rather than something that silently
    // means "no corpus". See `song_preview_ab_scenario.cpp`'s `kCorpusCountPath`.
    write(corpus_dir.join("COUNT.TXT"), format!("{copied}\n"));
    log::info!(
        "golden_vt_render: song-preview fixture staged {copied} corpus song(s) (corpus named: {corpus_was_named})"
    );

    let img = sd_image::pack_image(&root);
    // The staging tree has served its purpose once the image exists; leaving it in the system temp
    // dir is the same litter as leaving the images themselves.
    let _ = std::fs::remove_dir_all(&root);
    img
}

/// How many corpus songs the fixture stages, hand-duplicated with `kMaxCorpusCases` in
/// `src/deluge/harness/song_preview_ab_scenario.cpp`.
///
/// Asserted against its own literal, which makes a change take two deliberate edits, one on each
/// side -- the same pinning `DELUGE_FILE_PREFIX_CAPACITY` gets at both ends of that boundary. What
/// actually catches drift is the C++ side's runtime check against `COUNT.TXT`: a fixture that staged
/// more than the scenario can hold fails there by name.
const MAX_CORPUS_CASES: usize = 16;
const _: () = assert!(MAX_CORPUS_CASES == 16);

/// The `GOLDEN_SCENARIO=offset_io` scenario: drive a real multi-block SysEx file transfer and a
/// whole-file copy, and require the bytes on the card to be the ones that were written.
///
/// Stepped rather than one blocking call for the same reason its neighbours are: the SysEx handlers
/// are resumable, so replies arrive on later ticks.
#[embassy_executor::task]
async fn run_offset_io_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_offset_io_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_offset_io_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_offset_io_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the offset io gate did not complete within the 600s wait budget — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_offset_io_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_offset_io_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_offset_io_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the offset io gate FAILED ({failures} in-process failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: offset io gate completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=path_operations` scenario: drive real SysEx path requests and the browser's
/// folder creation, and require that the protocol still replies and that no folder creation begins on
/// the interaction tier.
///
/// Stepped rather than one blocking call for the same reason the two gates beside it are: the
/// operations are only ENQUEUED when the entry point runs, and the SysEx handler becomes resumable,
/// so replies arrive on later ticks.
#[embassy_executor::task]
async fn run_path_operations_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_path_operations_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_path_operations_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_path_operations_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the path operations gate did not complete within the 600s wait budget — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_path_operations_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_path_operations_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_path_operations_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the path operations gate FAILED ({failures} in-process failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: path operations gate completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=startup_failsafe` scenario: drive the REAL startup fail-safe and require
/// that none of its file I/O begins on the interaction tier.
///
/// Stepped rather than one blocking call for the same reason the settings gate is: the fail-safe's
/// operations are only ENQUEUED when the entry point runs, so the tier count must be read on a later
/// tick.
#[embassy_executor::task]
async fn run_startup_failsafe_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_startup_failsafe_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_startup_failsafe_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_startup_failsafe_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the startup fail-safe gate did not complete within the 600s wait budget — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_startup_failsafe_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_startup_failsafe_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_startup_failsafe_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the startup fail-safe gate FAILED ({failures} in-process failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: startup fail-safe gate completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=settings_channel` scenario: write a settings file through the real consumer
/// entry point, read it back, and require the value to survive -- while counting every settings
/// operation that began on the interaction tier.
///
/// Stepped rather than one blocking call for the same reason the save round-trip is: the operation
/// is only ENQUEUED when the entry point runs, so each check must read its result on a later tick.
#[embassy_executor::task]
async fn run_settings_channel_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_settings_channel_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_settings_channel_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_settings_channel_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the settings channel gate did not complete within the 600s wait budget — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_settings_channel_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_settings_channel_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_settings_channel_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the settings channel gate FAILED ({failures} in-process failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: settings channel gate completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// Drives the save round-trip gate: save a song through the real SaveSongUI gesture, load it back,
/// overwrite it, and require the two written files to be identical.
///
/// Stepped rather than one blocking call for the same reason the song-preview A/B is: saving is
/// dispatched onto the storage owner, so an op is only ENQUEUED when the gesture runs and each check
/// must read its result on a later tick.
#[embassy_executor::task]
async fn run_save_roundtrip_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_save_roundtrip_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_save_roundtrip_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_save_roundtrip_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the save round-trip did not complete within the 600s wait budget — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_save_roundtrip_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_save_roundtrip_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_save_roundtrip_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the save round-trip FAILED ({failures} in-process failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: save round-trip completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=song_preview_ab` scenario: draw every fixture song's artwork through the
/// real `LoadSongUI`, printing each one's image fingerprint and decode outcome as a diffable
/// `SP-CASE` line.
///
/// This binary is ONE leg. Which leg it is depends on how it was built -- `async_file_prefix` alone
/// (the file backing, which is what ships) or `async_file_prefix_active` (the Rust prefix channel,
/// with `file_prefix_task` spawned in `main` below). `scripts/song_preview_ab.sh` builds both and
/// diffs their `SP-CASE` lines.
///
/// The step machine is not ceremony: a preview whose prefix has not arrived is answered on a later
/// tick, and on this single-executor substrate the C++ worker fiber is what runs while the executor
/// is not polling, so a C++-side retry spin would starve the reader task it is waiting for.
#[embassy_executor::task]
async fn run_song_preview_ab_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_song_preview_ab_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_song_preview_ab_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_song_preview_ab_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the song-preview A/B did not complete within the 600s wait budget — wedged"
            );
            hard_exit(2);
        }
        // One tick on the UI tier (with a marker op behind it on the storage owner)...
        // SAFETY: as above.
        unsafe { deluge_scenario_song_preview_ab_dispatch_step() };
        // ...then wait for it to come back, letting the executor poll `file_prefix_task` (and the
        // fiber pump that runs the owner ops) in between.
        // SAFETY: as above.
        while !unsafe { deluge_scenario_song_preview_ab_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_song_preview_ab_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the song-preview A/B leg FAILED ({failures} in-process failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: song-preview A/B leg completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=audio_load_ab` scenario: run one whole song-shaped load of the fixture's
/// sample list through `AudioFileManager::getAudioFileFromFilename`, printing each file's resolved
/// identity and geometry as a diffable `AB-CASE` line.
///
/// This binary is ONE leg. Which leg it is depends on how it was built — `async_audio_load` alone
/// (synchronous) or `async_audio_load_active` (the Rust `audio_load_task` spawned in `main` below,
/// serving every load through the request/collect ABI). `scripts/audio_load_ab.sh` builds both and
/// diffs their `AB-CASE` lines; the two builds' `AB-SUMMARY` lines are expected to differ and are
/// not diffed.
///
/// The step machine is not ceremony. A deferred load answers `Error::LOAD_IN_PROGRESS` and its
/// caller must RETURN to its own loop; on this single-executor substrate the C++ worker fiber is
/// what runs while the executor is not polling, so a C++-side retry spin would starve the very task
/// it is waiting for. One attempt per dispatch, with an `await` in between, is what gives
/// `audio_load_task` the polls it needs to make progress.
#[embassy_executor::task]
async fn run_audio_load_ab_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_audio_load_ab_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_audio_load_ab_begin() };

    let deadline = Instant::now() + Duration::from_secs(300);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_audio_load_ab_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the audio-load A/B did not complete within the 300s wait budget — wedged"
            );
            hard_exit(2);
        }
        // One attempt on the worker fiber...
        // SAFETY: as above.
        unsafe { deluge_scenario_audio_load_ab_dispatch_step() };
        // ...then wait for it to come back, letting the executor poll `audio_load_task` (and the
        // fiber pump that runs the attempt itself) in between.
        // SAFETY: as above.
        while !unsafe { deluge_scenario_audio_load_ab_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_audio_load_ab_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the audio-load A/B leg FAILED ({failures} in-process failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: audio-load A/B leg completed (0 in-process failures)");
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
    let scenario = std::env::var("GOLDEN_SCENARIO").unwrap_or_default();
    let recorder_mode = scenario == "recorder_roundtrip";
    let audio_load_ab_mode = scenario == "audio_load_ab";
    let song_preview_ab_mode = scenario == "song_preview_ab";
    let save_roundtrip_mode = scenario == "save_roundtrip";
    let settings_channel_mode = scenario == "settings_channel";
    let startup_failsafe_mode = scenario == "startup_failsafe";
    let path_operations_mode = scenario == "path_operations";
    let offset_io_mode = scenario == "offset_io";
    if song_preview_ab_mode && !cfg!(feature = "async_file_prefix") {
        // Refusing beats running, for the reason the audio-load check below gives: without
        // `async_file_prefix` this binary has no prefix channel compiled at all, so the "file
        // backing leg" it would produce is built from a DIFFERENT configuration than the leg it gets
        // diffed against.
        eprintln!(
            "golden_vt_render: GOLDEN_SCENARIO=song_preview_ab needs --features async_file_prefix \
             (file-backed leg) or --features async_file_prefix_active (prefix-channel leg)"
        );
        std::process::exit(2);
    }
    if audio_load_ab_mode && !cfg!(feature = "async_audio_load") {
        // Refusing beats running: without `async_audio_load` this binary has no `audio_loader`
        // module at all, so the "synchronous leg" it would produce is one built from a DIFFERENT
        // configuration than the async leg it gets diffed against — the two would differ in more
        // than the switch under test, which is precisely the vacuous comparison this gate exists
        // to avoid.
        eprintln!(
            "golden_vt_render: GOLDEN_SCENARIO=audio_load_ab needs --features async_audio_load \
             (synchronous leg) or --features async_audio_load_active (async leg)"
        );
        std::process::exit(2);
    }

    log::info!(
        "golden_vt_render: fixture={fixture} budget_ms={budget_ms} recorder_mode={recorder_mode} \
         audio_load_ab_mode={audio_load_ab_mode} song_preview_ab_mode={song_preview_ab_mode}"
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
        } else if audio_load_ab_mode {
            pack_audio_load_ab_image()
        } else if song_preview_ab_mode {
            pack_song_preview_ab_image(&repo_root)
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
    #[cfg(feature = "async_audio_load_active")]
    spawner.spawn(audio_loader::audio_load_task().unwrap());
    // The prefix reader, on the same executor. Owns the channel only once
    // `deluge_file_prefix_async_active()` reports true (i.e. only under this feature); inert
    // otherwise, since with the feature off the module has no statics for it to serve.
    #[cfg(feature = "async_file_prefix_active")]
    spawner.spawn(file_prefix::file_prefix_task().unwrap());
    // The directory opener, likewise. Without this the channel's collect answers UNKNOWN forever and
    // every listing that consults it defers on every tick and never completes — which is exactly the
    // failure the `dir` leg of `scripts/song_preview_ab.sh` exists to catch, so it must be able to
    // pass when the code is right.
    #[cfg(feature = "async_file_directory_active")]
    spawner.spawn(file_directory::file_directory_task().unwrap());
    // The whole-file worker, likewise. Without this the channel's collects answer UNKNOWN forever
    // and every settings operation falls back to the owner arm — which is CORRECT and SILENT, so the
    // gate would still pass. `SC-CHANNEL served=` is the only thing that can tell the difference.
    #[cfg(feature = "async_file_bytes_active")]
    spawner.spawn(file_bytes::file_bytes_task().unwrap());

    static DONE: AtomicBool = AtomicBool::new(false);
    if audio_load_ab_mode {
        spawner.spawn(run_audio_load_ab_scenario(&DONE).unwrap());
    } else if song_preview_ab_mode {
        spawner.spawn(run_song_preview_ab_scenario(&DONE).unwrap());
    } else if save_roundtrip_mode {
        spawner.spawn(run_save_roundtrip_scenario(&DONE).unwrap());
    } else if settings_channel_mode {
        spawner.spawn(run_settings_channel_scenario(&DONE).unwrap());
    } else if startup_failsafe_mode {
        spawner.spawn(run_startup_failsafe_scenario(&DONE).unwrap());
    } else if path_operations_mode {
        spawner.spawn(run_path_operations_scenario(&DONE).unwrap());
    } else if offset_io_mode {
        spawner.spawn(run_offset_io_scenario(&DONE).unwrap());
    } else if recorder_mode {
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
