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
/// Compiled unconditionally: the loader has no switch, and the file-prefix channel below reuses its
/// `LoadOps`/`ProdOps`.
#[path = "../../../src/audio_loader.rs"]
mod audio_loader;
#[path = "../../../src/board.rs"]
mod board;
/// `deluge_card_flush` — the ejection hook that bumps the shared card generation and then clears
/// every loader's tables. Compiled unconditionally, like the loaders it clears.
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
/// The single-slot whole-file byte channel and its request/collect C ABI. Compiled UNCONDITIONALLY:
/// the channel has no feature switch, and `deluge_card_flush` names it. A build without it would let
/// the C++ weak fallback resolve `deluge_file_bytes_async_active` to false, and every settings read
/// and write would quietly take the inline arm while the gate reported a green run -- which is what
/// this file did on the first channel-on run of the settings gate, and `SC-CHANNEL served=0` is what
/// caught it.
#[path = "../../../src/file_bytes.rs"]
mod file_bytes;
/// The single-slot directory channel and its request/collect C ABI. Compiled UNCONDITIONALLY, like `file_bytes`
/// above: the channel has no feature switch, and a build without it would let the C++ weak fallback answer every
/// listing's request with UNKNOWN forever. `deluge_card_flush` names it too, so `card` cannot be compiled without
/// it; the reverse does not hold, and a build without `card` has no caller for its `flush_tables`.
#[path = "../../../src/file_directory.rs"]
mod file_directory;
/// The single-slot file-prefix channel and its request/collect C ABI. Compiled unconditionally: the
/// channel has no switch, and `deluge_card_flush` names it, so `card.rs` does not compile without it.
#[path = "../../../src/file_prefix.rs"]
mod file_prefix;
/// The recorder's async stream-write driver. Included here for the same reason every module above
/// is: this binary is a separate package, so a module missing from this list resolves to the C++
/// weak fallback and every write quietly takes the synchronous arm — which is exactly how the
/// settings channel first shipped a green run reporting nothing served.
#[path = "../../../src/file_write.rs"]
mod file_write;
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
    fn deluge_scenario_start_recorder_roundtrip_rest();
    fn deluge_scenario_recorder_cases_tick() -> i32;
    fn deluge_scenario_recorder_roundtrip_done() -> bool;
    fn deluge_scenario_recorder_roundtrip_failures() -> i32;
    fn deluge_scenario_offowner_begin(mode: i32);
    fn deluge_scenario_offowner_tick() -> i32;
    fn deluge_scenario_offowner_report();
    fn deluge_scenario_audio_load_ab_begin();
    fn deluge_scenario_audio_load_ab_complete() -> bool;
    fn deluge_scenario_audio_load_ab_dispatch_step();
    fn deluge_scenario_audio_load_ab_step_done() -> bool;
    fn deluge_scenario_audio_load_ab_failures() -> i32;

    fn deluge_scenario_song_save_channel_begin();
    fn deluge_scenario_song_save_channel_complete() -> bool;
    fn deluge_scenario_song_save_channel_dispatch_step();
    fn deluge_scenario_song_save_channel_step_done() -> bool;
    fn deluge_scenario_song_save_channel_failures() -> i32;
    fn deluge_scenario_sample_collect_begin();
    fn deluge_scenario_sample_collect_complete() -> bool;
    fn deluge_scenario_sample_collect_dispatch_step();
    fn deluge_scenario_sample_collect_step_done() -> bool;
    fn deluge_scenario_sample_collect_failures() -> i32;
    fn deluge_scenario_document_save_channel_begin();
    fn deluge_scenario_document_save_channel_complete() -> bool;
    fn deluge_scenario_document_save_channel_dispatch_step();
    fn deluge_scenario_document_save_channel_step_done() -> bool;
    fn deluge_scenario_document_save_channel_failures() -> i32;
    fn deluge_scenario_song_load_channel_begin();
    fn deluge_scenario_song_load_channel_complete() -> bool;
    fn deluge_scenario_song_load_channel_dispatch_step();
    fn deluge_scenario_song_load_channel_step_done() -> bool;
    fn deluge_scenario_song_load_channel_failures() -> i32;
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

    fn deluge_scenario_favourites_channel_begin();
    fn deluge_scenario_favourites_channel_complete() -> bool;
    fn deluge_scenario_favourites_channel_dispatch_step();
    fn deluge_scenario_favourites_channel_step_done() -> bool;
    fn deluge_scenario_favourites_channel_failures() -> i32;

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
    fn deluge_scenario_card_reinsert_begin();
    fn deluge_scenario_card_reinsert_complete() -> bool;
    fn deluge_scenario_card_reinsert_dispatch_step();
    fn deluge_scenario_card_reinsert_step_done() -> bool;
    fn deluge_scenario_card_reinsert_failures() -> i32;
    fn deluge_scenario_sample_load_tree_begin();
    fn deluge_scenario_sample_load_tree_complete() -> bool;
    fn deluge_scenario_sample_load_tree_dispatch_step();
    fn deluge_scenario_sample_load_tree_step_done() -> bool;
    fn deluge_scenario_sample_load_tree_failures() -> i32;
    fn deluge_scenario_sample_claim_begin();
    fn deluge_scenario_sample_claim_complete() -> bool;
    fn deluge_scenario_sample_claim_dispatch_step();
    fn deluge_scenario_sample_claim_step_done() -> bool;
    fn deluge_scenario_sample_claim_failures() -> i32;
    fn deluge_scenario_clear_song_undo_begin();
    fn deluge_scenario_clear_song_undo_complete() -> bool;
    fn deluge_scenario_clear_song_undo_dispatch_step();
    fn deluge_scenario_clear_song_undo_step_done() -> bool;
    fn deluge_scenario_clear_song_undo_failures() -> i32;
    fn deluge_scenario_preset_load_begin();
    fn deluge_scenario_preset_load_complete() -> bool;
    fn deluge_scenario_preset_load_dispatch_step();
    fn deluge_scenario_preset_load_step_done() -> bool;
    fn deluge_scenario_preset_load_failures() -> i32;
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

    // The CASES, driven from HERE -- one step per tick with a yield between, so the storage driver can
    // answer. They used to run inside the dispatch above, in a 200000-iteration loop that never
    // yielded; that only ever worked because the fiber blocks and resumes. Off the fiber, on the
    // channel arm, a caller that does not return cannot be answered -- which is the whole reason the
    // fiber could not be deleted.
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

    // The oracle's tail: the two probes and the summary, back on the fiber.
    // SAFETY: as above.
    unsafe { deluge_scenario_start_recorder_roundtrip_rest() };
    let rest_deadline = Instant::now() + Duration::from_secs(300);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_recorder_roundtrip_done() } {
            break;
        }
        if Instant::now() >= rest_deadline {
            log::error!(
                "golden_vt_render: recorder round-trip tail did not finish within 300s — wedged"
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
    // The off-owner case. Driven from HERE -- this task is not the fiber (it dispatches INTO it
    // above), so `on_owner()` is false and the recorder takes the path the device actually takes:
    // the cluster write goes through the channel, and the blocking ticks dispatch back to the owner.
    // The eleven cases above cannot cover either, because the oracle runs wholesale on the fiber.
    // SAFETY: same C-ABI contract as the calls above; called only from this task.
    unsafe { deluge_scenario_offowner_begin(0) };
    let off_deadline = Instant::now() + Duration::from_secs(300);
    let off_result = loop {
        // SAFETY: as above.
        let state = unsafe { deluge_scenario_offowner_tick() };
        if state != 0 {
            break state;
        }
        if Instant::now() >= off_deadline {
            log::error!(
                "golden_vt_render: off-owner recording did not complete within 300s — wedged"
            );
            hard_exit(2);
        }
        // Yield so the storage owner can run the ticks the case handed it and the write task can
        // make progress on the clusters it has been given. Without this the case cannot advance.
        Timer::after_millis(1).await;
    };
    if off_result != 1 {
        log::error!("golden_vt_render: off-owner recording FAILED — see the log above");
        hard_exit(1);
    }

    // Again, this time through the alteration branch. Separate run rather than a flag inside the
    // first, so a failure names which shape broke.
    // SAFETY: same C-ABI contract as above.
    unsafe { deluge_scenario_offowner_begin(1) };
    let alter_deadline = Instant::now() + Duration::from_secs(300);
    let alter_result = loop {
        // SAFETY: as above.
        let state = unsafe { deluge_scenario_offowner_tick() };
        if state != 0 {
            break state;
        }
        if Instant::now() >= alter_deadline {
            log::error!(
                "golden_vt_render: off-owner alterFile did not complete within 300s — wedged"
            );
            hard_exit(2);
        }
        Timer::after_millis(1).await;
    };
    if alter_result != 1 {
        log::error!("golden_vt_render: off-owner alterFile FAILED — see the log above");
        hard_exit(1);
    }

    // And once more, abandoning the recording part-way, so the abort machine runs: its close must
    // complete before its unlink, and the free-cluster count is the only thing that can see it.
    // SAFETY: same C-ABI contract as above.
    unsafe { deluge_scenario_offowner_begin(2) };
    let abort_deadline = Instant::now() + Duration::from_secs(300);
    let abort_result = loop {
        // SAFETY: as above.
        let state = unsafe { deluge_scenario_offowner_tick() };
        if state != 0 {
            break state;
        }
        if Instant::now() >= abort_deadline {
            log::error!("golden_vt_render: off-owner abort did not complete within 300s — wedged");
            hard_exit(2);
        }
        Timer::after_millis(1).await;
    };
    if abort_result != 1 {
        log::error!("golden_vt_render: off-owner abort FAILED — see the log above");
        hard_exit(1);
    }

    // The give-up cases: the channel busy with someone else's operation, then a cluster write held past
    // the recorder's bound. Each prints its own line, and a failure stops neither the other case nor the
    // tier report, so the script sees all three.
    let mut give_up_failed = false;
    for (mode, name) in [(3, "busy"), (4, "stall"), (5, "dirstall")] {
        // SAFETY: same C-ABI contract as above.
        unsafe { deluge_scenario_offowner_begin(mode) };
        let deadline = Instant::now() + Duration::from_secs(300);
        let result = loop {
            // SAFETY: as above.
            let state = unsafe { deluge_scenario_offowner_tick() };
            if state != 0 {
                break state;
            }
            if Instant::now() >= deadline {
                log::error!(
                    "golden_vt_render: off-owner {name} case did not complete within 300s — wedged"
                );
                hard_exit(2);
            }
            Timer::after_millis(1).await;
        };
        if result != 1 {
            log::error!("golden_vt_render: off-owner {name} case FAILED — see the log above");
            give_up_failed = true;
        }
    }

    // Over every off-owner case: a synchronous card operation is a violation whichever case made it.
    // SAFETY: same C-ABI contract as above.
    unsafe { deluge_scenario_offowner_report() };
    if give_up_failed {
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

/// Stage the sample-collect scenario's card: two small, genuinely parseable WAVs under `SAMPLES/`,
/// with different lengths and fills so a copy that fetched the wrong one is visible in the checksum.
///
/// Deliberately small. This gate is about WHICH ARM performed the transfers and whether the audio
/// file list survives them, not about moving a lot of bytes.
/// Stage the sample-collect scenario's card and pack it into a FAT image.
///
/// Two of these four files exist to make the ORDERING check able to fail at all.
///
/// A collect-media save rewrites `filePath` for exactly one shape of file: a recording, whose name
/// is `REC` plus five characters plus `.WAV`, and which is given a random `_XXXXX` suffix so that
/// re-saving cannot collide with the copy a previous save made. Every other file keeps its path.
/// A fixture of ordinary samples therefore never moves anything in the audio-file vector, and the
/// sorted-order check passes no matter how the code mutates it -- which is a check that cannot fail.
///
/// `REC12345.WAV` is that recording. `REC123450.WAV` is thirteen characters, so it is NOT renamed,
/// and it is positioned deliberately: case-insensitively `REC12345.WAV` sorts before it and
/// `REC12345_XXXXX.WAV` sorts after it (`.` < `0` < `_`). So the rename must move the recording
/// ACROSS it. Rewrite the path without removing and reinserting the entry and the vector is left
/// out of order at that exact index, which is what the gate reads.
/// Stage the song-save scenario's card: an empty SONGS directory and nothing else.
///
/// The fixture that matters is not on the card. It is the SONG, which the scenario inflates in
/// memory until its document outgrows the staging buffer -- a card full of files would not change
/// what this gate measures.
fn pack_song_save_image() -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-song-save-card");
    let _ = std::fs::remove_dir_all(&root);
    std::fs::create_dir_all(root.join("SONGS")).expect("creating the fixture SONGS directory");
    sd_image::pack_image(&root)
}

fn pack_sample_collect_image() -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-sample-collect-card");
    let _ = std::fs::remove_dir_all(&root);
    let samples = root.join("SAMPLES");
    let recordings = samples.join("RECORD");
    std::fs::create_dir_all(&recordings).expect("creating the sample-collect fixture directories");
    std::fs::create_dir_all(root.join("SONGS")).expect("creating the fixture SONGS directory");
    write_test_wav(&samples.join("COLL1.WAV"), 1, 16, 512, 0x41);
    write_test_wav(&samples.join("COLL2.WAV"), 2, 16, 1024, 0x42);
    write_test_wav(&recordings.join("REC12345.WAV"), 1, 16, 768, 0x43);
    write_test_wav(&recordings.join("REC123450.WAV"), 1, 16, 640, 0x44);
    sd_image::pack_image(&root)
}

/// Stage the sample-load tree scenario's card and pack it into a FAT image.
///
/// - `SAMPLES/SLTSYN.WAV`, `SLTWT.WAV`, `SLTKIT.WAV` and `SLTAUD.WAV` are the song's samples: the synth's sample
///   source, its wavetable source (2048 samples, one wavetable-length cycle), the kit drum and the audio clip. The
///   scenario collects them into the song's folder when it saves, then removes the first three from here, so the
///   song load can only find their collected copies.
/// - `SYNTHS/SLTPSYN/SLTPSYN.WAV` and `KITS/SLTPKIT/SLTPKA.WAV` / `SLTPKB.WAV` are the presets' samples. Each preset
///   names its sample under `SAMPLES/`, where no such file exists, so the load has to find it in the preset's own
///   folder and rewrite the holder's path there.
/// - `SAMPLES/SLTCKA.WAV`, `SLTCKB.WAV`, `SLTCKC.WAV`, `SLTCSB.WAV` and `SLTCAB.WAV` belong to the crucial-only load's
///   song: a second kit's three drums, a second synth and a second audio clip. The scenario collects them into that
///   song's folder too, and removes them from here before loading it.
/// Stage the card-reinsert scenario's card: three samples, one per fate the walk distinguishes.
///
/// `CRLOST.WAV` is REMOVED by the scenario between the ejection and the re-insertion, which is what
/// its resident Sample must then be marked unloadable for. The other two are never touched on the
/// card; their fates are decided by whether a project still holds them.
fn pack_card_reinsert_image() -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-card-reinsert-card");
    let _ = std::fs::remove_dir_all(&root);
    let samples = root.join("SAMPLES");
    for dir in [&samples, &root.join("SONGS")] {
        std::fs::create_dir_all(dir).expect("creating the card-reinsert fixture directories");
    }
    write_test_wav(&samples.join("CRSURV.WAV"), 1, 16, 4096, 0x71);
    write_test_wav(&samples.join("CRLOST.WAV"), 1, 16, 3072, 0x72);
    write_test_wav(&samples.join("CREVICT.WAV"), 1, 16, 2048, 0x73);
    sd_image::pack_image(&root)
}

fn pack_sample_load_tree_image() -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-sample-load-tree-card");
    let _ = std::fs::remove_dir_all(&root);
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
    sd_image::pack_image(&root)
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
///   has to find, and the one whose miss promotes the load's alternate-dir status for every later case.
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
/// - `SAMPLES/STALL.WAV` belongs to the stall stage: it is previewed while the loader holds every
///   load far past the browser's bound, and the preview must give up without loading or drawing it.
/// - `SAMPLES/SWAP.WAV`, `SAMPLES/SWAPB.WAV` and `SAMPLES/WITNESS.WAV` belong to the card-ejection
///   stage: the first is parsed and left uncollected, the second is moved onto its path once the
///   card has been marked ejected, and the third is what tells the stage the first has finished
///   parsing. Their geometries are deliberately unlike each other.
///
/// The fixture is rewritten on every process start: `sd_image_path::fingerprint_dir` hashes
/// path/length/mtime, not bytes, so every run packs its own 2.5 GB sparse image.
/// `scripts/audio_load_ab.sh` removes it afterwards — an accumulation of those under
/// `/tmp/deluge-streaming-scenario-*.img` is a known way to wedge an unrelated stem export on this
/// project.
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
    // the load context's `alternateDir` is its own directory and the bare-name alternate
    // candidate stringifies to exactly the nominal path. Only a positional reading of
    // "which candidate won" calls that an alternate hit -- see the `selfdir` entry in
    // `audio_load_ab_scenario.cpp`'s kCases. There is deliberately NO flattened
    // `ALT/ALT_SELFDIR.WAV` companion: the flattened candidate must miss so the
    // ambiguous bare-name one is the one that wins.
    write_test_wav(&alt.join("SELFDIR.WAV"), 1, 16, 1280, 0xBB);
    // The preview stage's file (see the same scenario's kPreviewPath): regular path
    // only, and asked for by no case above, so it is never resident when that stage
    // begins and its load necessarily goes through the loader.
    write_test_wav(&samples.join("PREVIEW.WAV"), 1, 16, 2816, 0xCC);
    // The cancellation stage's file (kCancelPath): asked for once and then abandoned before the
    // storage owner runs the load. It is a real, loadable file on purpose -- the stage asserts that
    // NOTHING loaded or drew it, and a file that could not have loaded anyway would let that pass
    // for the wrong reason.
    write_test_wav(&samples.join("CANCEL.WAV"), 1, 16, 3328, 0xDD);
    // The stall stage's file (kStallPath): previewed while the loader holds every load, so it must
    // never load or draw. A real, loadable file for the same reason as `CANCEL.WAV`.
    write_test_wav(&samples.join("STALL.WAV"), 1, 16, 3840, 0xDE);

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

/// The `GOLDEN_SCENARIO=favourites_channel` scenario: drive the favourites bank through its real
/// entry points and require that no bank is ever written with less than it held -- while counting
/// every favourites card operation that began on the interaction tier.
///
/// Stepped for the same reason as the settings gate: a bank operation is only QUEUED when its entry
/// point runs, so each check must read its result on a later tick.
#[embassy_executor::task]
async fn run_favourites_channel_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_favourites_channel_scenario: awaiting boot+mount");
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
    unsafe { deluge_scenario_favourites_channel_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_favourites_channel_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the favourites channel gate did not complete within the 600s wait budget — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_favourites_channel_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_favourites_channel_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_favourites_channel_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the favourites channel gate FAILED ({failures} in-process failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: favourites channel gate completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=song_save_channel` scenario: inflate the song until its document outgrows
/// the staging buffer, save it through the real ENTER gesture, and require that what landed loads
/// back as a song and that overwriting it leaves a song behind either way.
#[embassy_executor::task]
async fn run_song_save_channel_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_song_save_channel_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_song_save_channel_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_song_save_channel_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the song-save run did not complete within 600s — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_song_save_channel_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_song_save_channel_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_song_save_channel_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the song-save run FAILED ({failures} in-process failure(s))"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: song-save run completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=sample_collect` scenario: save a song that references audio files with
/// collect-media on, and report whether the samples arrived, whether the audio-file list survived,
/// and which arm performed the transfers.
#[embassy_executor::task]
async fn run_sample_collect_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_sample_collect_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_sample_collect_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_sample_collect_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the sample-collect run did not complete within 600s — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_sample_collect_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_sample_collect_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_sample_collect_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the sample-collect run FAILED ({failures} in-process failure(s))"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: sample-collect run completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// Write a 16-bit PCM WAV of a sine at `freq_hz`, `frames` frames long, the same on every channel.
///
/// The claim's pitch detection needs a periodic signal to name a note; a constant fill names none.
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

/// Stage the sample-claim scenario's card and pack it into a FAT image.
///
/// Every file is asked for by one leg only, so a holder naming it can only be that leg's doing:
/// - `SAMPLES/SCSYN.WAV`: a 440 Hz sine, claimed into the boot song's synth, whose pitch detection names its note.
/// - `SAMPLES/SCCYCLE.WAV`: one 512-frame cycle of a sine, claimed as a wavetable from the context menu.
/// - `SAMPLES/SCSTER.WAV`: stereo, claimed into a wavetable oscillator, which cannot take it and falls back to a
///   sample.
/// - `SAMPLES/SCGATE.WAV` and `SAMPLES/SCSTALL.WAV`: claimed while the loader holds their loads, for the gate and the
///   stall bound.
/// - `SAMPLES/SCKITD.WAV`: the sample the scenario's one-drum kit preset names, so the kit clip starts as a sample kit.
/// - `SAMPLES/SCKICK.WAV`: claimed into that kit's drum. Its name holds "KICK", which the claim reads as a kick.
/// - `SAMPLES/SCAUD.WAV`: claimed into an audio clip.
/// - `SAMPLES/SCSLICE.WAV`: one second long, sliced into four regions of 250 ms each.
/// - `SAMPLES/RD/RD1.WAV`, `RD2.WAV` and `RD3.WAV`: the Drum Randomizer's folder. Its kit preset names the first, and
///   the randomizer picks from the folder a drum's sample sits in, so these three are reachable by no other leg.
/// - `SAMPLES/RDG/G1.WAV`..`G3.WAV`: the randomizer gate leg's folder, loaded by nothing before it, so its load reaches
///   the loader's hold.
/// - `SAMPLES/RDK/K1.WAV`: the randomizer stall leg's first row, alone in its folder and loaded before the hold.
/// - `SAMPLES/RDS/S1.WAV`..`S3.WAV`: the stall leg's second row, loaded by nothing, so its load is held past the stall
///   bound.
/// - `SAMPLES/IMK/IMK_HAT.WAV`, `IMK_KICK.WAV` and `IMK_SNARE.WAV`: the kit folder import's folder, whose shared prefix
///   the drum names drop.
/// - `SAMPLES/IMG/IMG_A.WAV`..`IMG_C.WAV`: the import gate leg's folder, loaded by nothing before it.
/// - `SAMPLES/IMS/IMS_A.WAV`..`IMS_C.WAV`: the import stall leg's folder; the first and last are loaded in its
///   preparation, and the middle one is held.
/// - `SYNTHS/`: empty; the multisample import leg writes its blank synth preset there.
/// - `SAMPLES/IMM/IMM_A2.WAV`, `IMM_A3.WAV` and `IMM_A4.WAV`: the multisample folder import's folder, one-second sines
///   at 110, 220 and 440 Hz.
fn pack_sample_claim_image() -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-sample-claim-card");
    let _ = std::fs::remove_dir_all(&root);
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
    write_sine_wav(&samples.join("SCCYCLE.WAV"), 1, 512, 44_100.0 / 512.0);
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
    sd_image::pack_image(&root)
}

/// The `GOLDEN_SCENARIO=sample_claim` scenario: claim files from the real sample browser and report what each claim
/// bound.
#[embassy_executor::task]
async fn run_sample_claim_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_sample_claim_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_sample_claim_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_sample_claim_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the sample-claim run did not complete within 600s — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_sample_claim_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_sample_claim_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_sample_claim_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the sample-claim run FAILED ({failures} in-process failure(s))"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: sample-claim run completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// Stage the preset-load scenario's card and pack it into a FAT image.
///
/// Every sample a preset names sits in `SAMPLES/PL/`; the scenario writes the presets themselves into `SYNTHS/` and
/// `KITS/` through the firmware's own serialisers, so both folders start empty:
/// - `PLSYN.WAV`: the synth leg's preset's sample; `PLROW.WAV` the kit row leg's; `PLCLN.WAV` the clone leg's.
/// - `PLKA.WAV`, `PLKB.WAV`, `PLKC.WAV`: the kit leg's preset's three drums.
/// - `PLBASE.WAV`: the one drum of the kit clip's own preset, the one the new-kit-track gesture picks up.
/// - `PLRACE.WAV`: the race leg's preset's sample.
/// - `SAMPLES/PLH/PLHA..PLHC.WAV`: the supersession leg's heavy kit, loaded by nothing before it;
///   `SAMPLES/PLL/PLLITE.WAV` its light kit.
/// - `SAMPLES/PLG/PLGATE.WAV` and `SAMPLES/PLS/PLSTALL.WAV`: the gate and stall legs' held samples.
/// - `SAMPLES/AQ/AQSA.WAV`, `AQKA..AQKD.WAV` and `AQKL.WAV`: the acquisition legs' synth, navigated kits and light kit;
///   `SAMPLES/AQH/AQHA..AQHC.WAV` their heavy kit, and `SAMPLES/AQS/PLSTB.WAV` the acquisition stall leg's held sample.
fn pack_preset_load_image() -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-preset-load-card");
    let _ = std::fs::remove_dir_all(&root);
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
    sd_image::pack_image(&root)
}

/// The card the `GOLDEN_SCENARIO=clear_song_undo` scenario runs on: the sample its preset binds, and the empty
/// SYNTHS folder the scenario writes that preset into through the firmware.
fn pack_clear_song_undo_image() -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-clear-song-undo-card");
    let _ = std::fs::remove_dir_all(&root);
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
    // The two an AudioClip's sample change moves between, and the one the missing-file leg deletes before undoing.
    write_sine_wav(&samples.join("CSUNA.WAV"), 1, 22_050, 330.0);
    write_sine_wav(&samples.join("CSUNB.WAV"), 1, 22_050, 550.0);
    write_sine_wav(&samples.join("CSGONE.WAV"), 1, 22_050, 660.0);
    sd_image::pack_image(&root)
}

/// The `GOLDEN_SCENARIO=clear_song_undo` scenario: clear the song the way the context menu does, and report where the
/// work ran and what the new song landed on.
#[embassy_executor::task]
async fn run_clear_song_undo_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_clear_song_undo_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_clear_song_undo_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_clear_song_undo_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the clear-song-undo run did not complete within 600s — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_clear_song_undo_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_clear_song_undo_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_clear_song_undo_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the clear-song-undo run FAILED ({failures} in-process failure(s))"
        );
        done.store(true, Ordering::Release);
        hard_exit(1);
    }
    log::info!("golden_vt_render: the clear-song-undo run passed");
    done.store(true, Ordering::Release);
    hard_exit(0);
}

/// The `GOLDEN_SCENARIO=preset_load` scenario: load synth and kit presets through the real preset browser and report
/// what each load bound.
#[embassy_executor::task]
async fn run_preset_load_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_preset_load_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_preset_load_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_preset_load_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the preset-load run did not complete within 600s — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_preset_load_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_preset_load_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_preset_load_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the preset-load run FAILED ({failures} in-process failure(s))"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: preset-load run completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=card_reinsert` scenario: load samples, eject the card, remove one of their
/// files, re-insert, and report what `cardReinserted()`'s walk did to each resident.
#[embassy_executor::task]
async fn run_card_reinsert_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_card_reinsert_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_card_reinsert_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_card_reinsert_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the card-reinsert run did not complete within 600s — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_card_reinsert_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_card_reinsert_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_card_reinsert_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the card-reinsert run FAILED ({failures} in-process failure(s))"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: card-reinsert run completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=sample_load_tree` scenario: build a song, save it with collected media and load it back, then
/// load a synth preset and a kit preset, and report where every holder's file came from.
#[embassy_executor::task]
async fn run_sample_load_tree_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_sample_load_tree_scenario: awaiting boot+mount");
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

    // SAFETY: DELUGE_HOST harness C-ABI; boot (confirmed above) has run `deluge_app_init`.
    unsafe { deluge_scenario_sample_load_tree_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_sample_load_tree_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the sample-load tree run did not complete within 600s — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_sample_load_tree_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_sample_load_tree_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_sample_load_tree_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the sample-load tree run FAILED ({failures} in-process failure(s))"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: sample-load tree run completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The `GOLDEN_SCENARIO=document_save_channel` scenario: save a preset through the real ENTER
/// gesture, require the written document to be whole, and report whether the storage channel served
/// the write off the owner.
///
/// Two legs are built from this one scenario -- with and without the channel features -- because
/// `write_file` begins life with no harness building it, which is precisely the position the
/// whole-file read was in when it turned out to contain two defects.
#[embassy_executor::task]
async fn run_document_save_channel_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_document_save_channel_scenario: awaiting boot+mount");
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
    unsafe { deluge_scenario_document_save_channel_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_document_save_channel_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the document-save channel run did not complete within the 600s \
                 wait budget — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_document_save_channel_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_document_save_channel_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_document_save_channel_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the document-save channel run FAILED ({failures} in-process \
             failure(s) — see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: document-save channel run completed (0 in-process failures)");
    done.store(true, Ordering::Release);
}

/// The card the song-load gate boots on: somewhere to save, and the one sample its song carries. It packs an
/// image of its own rather than the golden fixture because the gate saves and loads a song it writes itself, and
/// the fixture corpus has no sample this gate can name.
fn pack_song_load_channel_image() -> std::path::PathBuf {
    let root = std::env::temp_dir().join("deluge-song-load-channel-card");
    let _ = std::fs::remove_dir_all(&root);
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
    sd_image::pack_image(&root)
}

/// The `GOLDEN_SCENARIO=song_load_channel` scenario: save a song carrying a distinctive value, load
/// it back through the real `LoadSongUI`, and report both whether the value returned and whether the
/// storage channel served the read off the owner.
///
/// Until this gate existed, `recorder_write.sh` was the only script that built the write channel, so
/// the whole-file staging read had never executed under any harness at all.
#[embassy_executor::task]
async fn run_song_load_channel_scenario(done: &'static AtomicBool) {
    log::info!("golden_vt_render: run_song_load_channel_scenario: awaiting boot+mount");
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
    unsafe { deluge_scenario_song_load_channel_begin() };

    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        // SAFETY: as above.
        if unsafe { deluge_scenario_song_load_channel_complete() } {
            break;
        }
        if Instant::now() >= deadline {
            log::error!(
                "golden_vt_render: the song-load channel run did not complete within the 600s wait \
                 budget — wedged"
            );
            hard_exit(2);
        }
        // SAFETY: as above.
        unsafe { deluge_scenario_song_load_channel_dispatch_step() };
        // SAFETY: as above.
        while !unsafe { deluge_scenario_song_load_channel_step_done() } {
            Timer::after_millis(1).await;
        }
        Timer::after_millis(1).await;
    }

    // SAFETY: as above.
    let failures = unsafe { deluge_scenario_song_load_channel_failures() };
    if failures != 0 {
        log::error!(
            "golden_vt_render: the song-load channel run FAILED ({failures} in-process failure(s) — \
             see the log above)"
        );
        hard_exit(1);
    }
    log::info!("golden_vt_render: song-load channel run completed (0 in-process failures)");
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
/// The previews go through the prefix channel, with `file_prefix_task` spawned in `main` below, and
/// the scenario holds each one to the same file decoded the synchronous way on the storage owner.
/// `scripts/song_preview_ab.sh` builds and runs it once.
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
/// One build, one run: every case loads twice in process, first through the synchronous card path as
/// the reference and then through the Rust `audio_load_task` spawned in `main` below, and the scenario
/// requires the two to agree. `scripts/audio_load_ab.sh` checks its markers.
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
    let song_load_channel_mode = scenario == "song_load_channel";
    let document_save_channel_mode = scenario == "document_save_channel";
    let sample_collect_mode = scenario == "sample_collect";
    let song_save_mode = scenario == "song_save_channel";
    let settings_channel_mode = scenario == "settings_channel";
    let favourites_channel_mode = scenario == "favourites_channel";
    let startup_failsafe_mode = scenario == "startup_failsafe";
    let path_operations_mode = scenario == "path_operations";
    let offset_io_mode = scenario == "offset_io";
    let card_reinsert_mode = scenario == "card_reinsert";
    let sample_load_tree_mode = scenario == "sample_load_tree";
    let sample_claim_mode = scenario == "sample_claim";
    let preset_load_mode = scenario == "preset_load";
    let clear_song_undo_mode = scenario == "clear_song_undo";

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
        } else if song_save_mode {
            pack_song_save_image()
        } else if sample_collect_mode {
            pack_sample_collect_image()
        } else if card_reinsert_mode {
            pack_card_reinsert_image()
        } else if sample_load_tree_mode {
            pack_sample_load_tree_image()
        } else if sample_claim_mode {
            pack_sample_claim_image()
        } else if preset_load_mode {
            pack_preset_load_image()
        } else if clear_song_undo_mode {
            pack_clear_song_undo_image()
        } else if song_load_channel_mode {
            pack_song_load_channel_image()
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
    // The audio loader, always: without this spawn every sample load that opts in waits on a request
    // nothing serves.
    spawner.spawn(audio_loader::audio_load_task().unwrap());
    // The prefix reader, on the same executor, always. Without this spawn every preview and cartridge
    // load waits on a request nothing serves.
    spawner.spawn(file_prefix::file_prefix_task().unwrap());
    // The directory opener, always: every listing off the storage owner opens through it, and without this spawn
    // each one waits on a request nothing serves.
    spawner.spawn(file_directory::file_directory_task().unwrap());
    // The whole-file worker, always. Without this the channel's collects answer UNKNOWN forever and
    // every settings operation would stall -- `SC-CHANNEL served=` is what tells the difference.
    spawner.spawn(file_bytes::file_bytes_task().unwrap());
    // And the write channel's worker, always: without this spawn its collects answer UNKNOWN forever.
    spawner.spawn(file_write::file_write_task().unwrap());

    static DONE: AtomicBool = AtomicBool::new(false);
    if audio_load_ab_mode {
        spawner.spawn(run_audio_load_ab_scenario(&DONE).unwrap());
    } else if song_preview_ab_mode {
        spawner.spawn(run_song_preview_ab_scenario(&DONE).unwrap());
    } else if save_roundtrip_mode {
        spawner.spawn(run_save_roundtrip_scenario(&DONE).unwrap());
    } else if song_load_channel_mode {
        spawner.spawn(run_song_load_channel_scenario(&DONE).unwrap());
    } else if document_save_channel_mode {
        spawner.spawn(run_document_save_channel_scenario(&DONE).unwrap());
    } else if song_save_mode {
        spawner.spawn(run_song_save_channel_scenario(&DONE).unwrap());
    } else if sample_collect_mode {
        spawner.spawn(run_sample_collect_scenario(&DONE).unwrap());
    } else if settings_channel_mode {
        spawner.spawn(run_settings_channel_scenario(&DONE).unwrap());
    } else if favourites_channel_mode {
        spawner.spawn(run_favourites_channel_scenario(&DONE).unwrap());
    } else if startup_failsafe_mode {
        spawner.spawn(run_startup_failsafe_scenario(&DONE).unwrap());
    } else if path_operations_mode {
        spawner.spawn(run_path_operations_scenario(&DONE).unwrap());
    } else if offset_io_mode {
        spawner.spawn(run_offset_io_scenario(&DONE).unwrap());
    } else if card_reinsert_mode {
        spawner.spawn(run_card_reinsert_scenario(&DONE).unwrap());
    } else if sample_load_tree_mode {
        spawner.spawn(run_sample_load_tree_scenario(&DONE).unwrap());
    } else if sample_claim_mode {
        spawner.spawn(run_sample_claim_scenario(&DONE).unwrap());
    } else if preset_load_mode {
        spawner.spawn(run_preset_load_scenario(&DONE).unwrap());
    } else if clear_song_undo_mode {
        spawner.spawn(run_clear_song_undo_scenario(&DONE).unwrap());
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
