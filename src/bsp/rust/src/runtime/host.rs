//! The host entry: a boot smoke without the C++ app, the threaded host app under `host_app`, and
//! the emulator under `sim`.

#[cfg(all(feature = "host_app", not(feature = "sim")))]
use crate::audio::host as audio_host;
use crate::peripherals::{control, display};
#[cfg(feature = "sim")]
use crate::storage::memory_card;
#[cfg(not(any(feature = "sim_latency", feature = "host_app")))]
use crate::storage::sd;
#[cfg(feature = "host_app")]
use crate::{
    boot,
    channel::{audio_loader, file_bytes, file_directory, file_prefix, file_stream},
    peripherals::board,
    storage::streaming_loader,
    sys,
};

// The harness modes of the threaded host app, each opted into by an environment variable.
#[cfg(all(feature = "host_app", not(feature = "sim")))]
mod recorder_finalize_probe;
#[cfg(all(feature = "host_app", not(feature = "sim")))]
mod recorder_probe;
#[cfg(all(feature = "host_app", not(feature = "sim")))]
mod scenario;

/// `host_app` feature: host sibling of [`app_task`] above — the real C++ app's
/// one-time bring-up, run on the host executor spawned by `fn main`'s
/// `host_app` boot path. Mirrors the device sequencing exactly (wait for the
/// PIC handshake, bring SD up, then `deluge_app_init`), minus the device-only
/// SYNC LED blink (no GPIO on host).
///
/// After init it marks boot finished and parks, as the device [`app_task`] does:
/// the scheduler's task runners, spawned by `deluge_app_init`, own the app's work.
#[cfg(feature = "host_app")]
#[embassy_executor::task]
async fn host_app_task() {
    // Mirrors the device `app_task`: wait for the PIC's (host: synthetic)
    // ready handshake, then bring SD up, before the app's first storage access.
    deluge_bsp::pic::wait_ready().await;
    crate::storage::sd::boot_init().await;

    // Host counterpart of `app_task`'s efatfs mount (see its comment) —
    // mount the shim's embedded-fatfs `FileSystem` BEFORE `deluge_app_init` so
    // the app's first sample-load can open an efatfs handle. No FS_ALLOCATOR
    // arena needed here (unlike the device): `efatfs_host_shim.rs`'s module doc
    // notes embedded-fatfs's `alloc` feature just uses the host's implicit std
    // allocator. A failed mount must NOT abort boot — but the streaming read
    // is efatfs-only, so a failed mount means streamed samples won't load
    // rather than reverting to C-FatFS, exactly like the device. This mount's
    // block device awaits the SD_BUS-guarded async helpers directly (see the
    // shim's module doc), and in any case `sim_latency::pump` runs on the
    // dedicated audio OS thread (spawned above in `main`, before this task),
    // so this task's `block_on` busy-spin never starves it (see Appendix A of
    // the R0 design doc for why a single-threaded sim's shape is different).
    match crate::storage::efatfs::host::mount().await {
        Ok(()) => log::info!("efatfs: mounted (host)"),
        Err(status) => {
            log::warn!("efatfs: host mount failed ({status}) — streamed samples will not load")
        }
    }

    log::info!("deluge-bsp-rust: host deluge_app_init() (registers + spawns task runners)");
    // deluge_app_init → registerTasks() spawns the per-task runners onto this
    // executor via scheduler::set_spawner's stashed spawner. They begin running
    // as soon as we yield below.
    unsafe { sys::deluge_app_init(board::deluge_board()) };
    boot::mark_finished();
    log::info!("deluge-bsp-rust: host scheduler running");

    // Nothing left to pump: the scheduler's task runners were spawned by `deluge_app_init`.
    core::future::pending::<()>().await
}

/// Host harness entry (`cargo build`/`cargo test` off-target, no `target_os =
/// "none"`). Exercises the core host modules (`scheduler`, `sd`, `services`) on
/// std without any device BSP/HAL — no MMU/GIC/SDRAM bring-up. Proves the host
/// binary actually links and runs: the `sd.rs` file-backed block-device shim
/// round-trips real bytes through the same `deluge_block_read`/
/// `deluge_block_write` C ABI the app would call. With `host_app` off, no C++
/// app is linked (see build.rs); with it on, this also boots the real C++ app
/// (see the `host_app`-gated blocks below).
pub fn main() {
    env_logger::init();
    log::info!("deluge-bsp-rust: HOST harness");

    // sd.rs host shim round-trip: write a known sector via the C-ABI
    // deluge_block_write, read it back via deluge_block_read, and verify the
    // bytes survive — proves the file-backed shim actually persists data, not
    // just that it links. Sector 1 (not 0): leaves a notional boot sector alone.
    //
    // Deliberately called here, synchronously, before any executor exists — it
    // is a bootstrap-time smoke test of the raw ABI shim itself, not an app
    // FatFS access, so it runs unconditionally in both configs.
    //
    // `sim_latency`-on only: SKIPPED here instead. Under `sim_latency`,
    // `deluge_block_write`/`deluge_block_read` route through
    // `sim_latency::modeled_write`/`modeled_read` (see sd.rs's module doc),
    // which pends on `sim_latency::pump` — a genuinely-spawned Embassy task —
    // to resolve the modeled delay. No executor exists yet at this point in
    // `fn main()`, so nothing could ever spawn `pump`, and the
    // `block_on` below would busy-spin forever waiting on a modeled transfer
    // nobody services. The same round trip (write + read, byte-for-byte data
    // assertion) stays covered under `sim_latency` by
    // `tests/sim_latency_host.rs`'s exercise, which brings up a real executor
    // with `sim_latency::pump` running before issuing any transfer.
    //
    // Also SKIPPED whenever `host_app` is on. This write is destructive
    // (it clobbers sector 1 of WHATEVER image `DELUGE_SD_IMAGE` names) and, on
    // a `host_app` build, that can be a real, externally-supplied FAT image —
    // e.g. `preemptive_race_tsan/run.sh`'s own documented, recommended
    // `DELUGE_SD_IMAGE=<cached image>` workflow (its header comment: reusing an
    // already-packed image is "100% reliable", vs. packing fresh in-process).
    // This self-test's synthetic byte pattern lands on FAT32's FSInfo sector,
    // which C-FatFS's `f_mount`
    // tolerates (it just re-derives the free-cluster count) but
    // `embedded-fatfs`'s stricter mount validation rejects outright
    // (`CorruptedFileSystem`). Under `host_app`, the real app's own boot (FatFS/efatfs mount
    // + whatever scenario runs) already exercises the same
    // `deluge_block_write`/`deluge_block_read` ABI end-to-end with real data,
    // making this synthetic smoke test both redundant and unsafe there.
    #[cfg(any(feature = "sim_latency", feature = "host_app"))]
    log::info!(
        "deluge-bsp-rust: sd round-trip SKIPPED ({})",
        if cfg!(feature = "host_app") {
            "host_app is on — this write is destructive and DELUGE_SD_IMAGE may name a \
             real caller-supplied FAT image; the app's own boot exercises the same ABI"
        } else {
            "sim_latency has no executor/pump yet at this bootstrap point — see \
             tests/sim_latency_host.rs for the covered equivalent"
        }
    );
    #[cfg(not(any(feature = "sim_latency", feature = "host_app")))]
    {
        const TEST_SECTOR: u32 = 1;
        let mut pattern = [0u8; 512];
        for (i, b) in pattern.iter_mut().enumerate() {
            *b = (i as u8).wrapping_mul(31).wrapping_add(7);
        }
        let status = sd::deluge_block_write(0, pattern.as_ptr(), TEST_SECTOR, 1);
        assert_eq!(
            status, 0,
            "sd round-trip: deluge_block_write failed (status={status})"
        );
        let mut readback = [0u8; 512];
        let status = sd::deluge_block_read(0, readback.as_mut_ptr(), TEST_SECTOR, 1);
        assert_eq!(
            status, 0,
            "sd round-trip: deluge_block_read failed (status={status})"
        );
        assert_eq!(
            pattern, readback,
            "sd round-trip: readback did not match what was written"
        );
        log::info!("deluge-bsp-rust: sd round-trip OK (sector {TEST_SECTOR}, 512 bytes)");
    }

    // --- The emulator (`sim`) ---------------------------------------------
    // The threaded host app as the brain of deluge-sdk's desktop simulator, in this process. The simulator makes
    // the panel and the audio bridge and owns the main thread; the brain hands them to deluge_bsp, whose host arms
    // then drive the panel exactly where the device's drive the PIC and the OLED, and starts the same three
    // executor threads the boot smoke below starts. When the window closes the app is still running, so the
    // process ends through `hard_exit` for the reason it does below.
    #[cfg(feature = "sim")]
    {
        let card = insert_sim_card();
        deluge_simulator::run_brain(|panel, audio| {
            deluge_bsp::sim::install(panel, audio);
            start_threaded_host(|_| {});
        });
        if card.is_some_and(|path| path.is_dir()) {
            memory_card::write_back();
        }
        hard_exit(0);
    }

    // --- Whole-BSP host boot smoke (no C++ app; `host_app` OFF) ------------
    // Bring up a host Embassy executor (platform-std) and spawn the four
    // control/display tasks — the same ones main.rs spawns on device (minus the
    // app/audio/cv/usb tasks, which need the C++ app or real peripherals). With
    // no app and no input, each runs its init sequence then parks:
    //   pic_pump       → pic::init(); loops on pic::read_byte() (host: parks)
    //   pad_render     → pic::wait_ready(); OUT.receive() (parks, nothing queued)
    //   encoder_wake_pump → parks on ENCODER_WAKER (host: never fires)
    //   oled_render    → pic::wait_ready(); oled::init(); sends ONE blank frame
    //                    (captured by the oled host sim), then parks on wait_redraw
    // Boot is proven by the captured blank frame; the watchdog bounds it so a
    // regression (init that blocks/crashes) fails instead of hanging forever.
    #[cfg(not(feature = "host_app"))]
    {
        use embassy_executor::{Executor, Spawner};
        use std::time::{Duration, Instant};

        std::thread::Builder::new()
            .name("deluge-bsp-boot".into())
            .spawn(|| {
                let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
                executor.run(|spawner: Spawner| {
                    crate::scheduler::set_spawner(spawner);
                    spawner.spawn(control::pic_pump().unwrap());
                    spawner.spawn(control::pad_render().unwrap());
                    spawner.spawn(control::encoder_wake_pump().unwrap());
                    spawner.spawn(display::oled_render().unwrap());
                });
            })
            .expect("spawning the host BSP executor thread");

        // Wait (bounded) for oled_render to capture its first (blank) frame.
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            // A blank frame is all-zero; captured_frame() returns the last frame the
            // oled sim received. Before the first send it is the sim's initial state;
            // we detect "boot reached first send" via a distinct signal below.
            if deluge_bsp::oled::boot_frame_captured() {
                break;
            }
            if Instant::now() >= deadline {
                panic!("host BSP boot: oled_render did not capture its blank frame in time");
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        log::info!("deluge-bsp-rust: whole-BSP host boot OK (control/display tasks quiescent)");
    }

    // --- Host app boot-and-idle smoke (`host_app` ON) ----------------------
    // Same host executor-on-a-thread shape as the whole-BSP smoke above, but this time
    // also spawn a host `app_task` that runs the REAL C++ app's one-time
    // bring-up (`deluge_app_init` → `deluge_boot` + `registerTasks()` +
    // `encoders::init()`, mirroring the device `app_task` in `main`, above).
    // `registerTasks()` calls `addRepeatingTask`/`addConditionalTask` a couple
    // dozen times, spawning one Embassy task runner per Deluge task via
    // `scheduler::set_spawner`'s stashed spawner — so "at least one scheduler
    // slot claimed" is the strongest cheap, real-app-driven signal that boot
    // reached the scheduler (not just BSP init); `oled_render`'s captured first
    // frame (same signal the whole-BSP smoke above uses) is the fallback.
    #[cfg(all(feature = "host_app", not(feature = "sim")))]
    {
        use std::time::{Duration, Instant};

        // --- Streaming-underrun harness: opt-in scenario mode --------------
        // Off by default (env var unset) — the boot-and-idle smoke below is byte-for-
        // byte unchanged from before this task. Set DELUGE_STREAMING_SCENARIO_SONG
        // (e.g. "SONGS/Cordae.XML") to switch this run into the scenario: pack a real
        // FAT SD image from the golden harness's corpus (unless DELUGE_SD_IMAGE is
        // already set, in which case that image is used as-is — must already contain
        // the requested song), load it, start real-time playback + a concurrent
        // output recording, and step until DELUGE_STREAMING_SCENARIO_BLOCKS audio
        // blocks (default 500) have rendered. This MUST run before anything below
        // touches SD (the audio thread spawn is SD-inert, but `host_app_task` mounts
        // the card as soon as it starts) — see sd_image.rs's module doc.
        let scenario_cfg = std::env::var("DELUGE_STREAMING_SCENARIO_SONG")
            .ok()
            .map(|song| {
                if std::env::var_os("DELUGE_SD_IMAGE").is_none() {
                    let fixture = std::env::var("DELUGE_STREAMING_SCENARIO_FIXTURE")
                        .unwrap_or_else(|_| "cordae".to_string());
                    let repo_root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                        .ancestors()
                        .nth(3)
                        .expect("CARGO_MANIFEST_DIR (src/bsp/rust) has a repo root 3 levels up")
                        .to_path_buf();
                    let img = crate::storage::sd_image::pack_golden_fixture(&repo_root, &fixture);
                    // SAFETY: called before any thread below is spawned (no concurrent
                    // env access yet) — same precondition tests/sim_latency_host.rs
                    // documents for its own DELUGE_SD_IMAGE set_var.
                    unsafe { std::env::set_var("DELUGE_SD_IMAGE", &img) };
                }
                let target_blocks = std::env::var("DELUGE_STREAMING_SCENARIO_BLOCKS")
                    .ok()
                    .and_then(|s| s.parse().ok())
                    .unwrap_or(500u64);
                // Widen the per-step poll budget for a manual demo run (e.g. a very long
                // `DELUGE_STREAMING_SCENARIO_BLOCKS` window), default unchanged.
                let step_timeout_secs = std::env::var("DELUGE_STREAMING_SCENARIO_STEP_TIMEOUT_S")
                    .ok()
                    .and_then(|s| s.parse().ok())
                    .unwrap_or(20u64);
                // Optional `sd::sim_latency` override, applied by `scenario::run` right
                // after song load completes (see `ScenarioConfig::post_load_sim_latency`'s
                // doc comment for why post-load, not pre-load) — lets a manual `cargo run`
                // dial the modeled SD latency from "trivially fast" (unset — no effect) to
                // "absurdly slow" without a rebuild, to demonstrate the
                // `deluge_sim_underrun_*_count()` counters are wired to real fill-vs-drain
                // behaviour. No effect unless the `sim_latency` feature is enabled.
                #[cfg(feature = "sim_latency")]
                let post_load_sim_latency = {
                    let bps = std::env::var("DELUGE_SIM_LATENCY_THROUGHPUT_BPS")
                        .ok()
                        .and_then(|s| s.parse::<u32>().ok());
                    let us = std::env::var("DELUGE_SIM_LATENCY_OVERHEAD_US")
                        .ok()
                        .and_then(|s| s.parse::<u32>().ok());
                    match (bps, us) {
                        (None, None) => None,
                        (b, u) => {
                            // Defaults mirror `sd::sim_latency`'s own un-overridden constants
                            // (20MB/s throughput, 500us overhead) so setting only one of the
                            // two env vars still produces a sane pair.
                            let bps = b.unwrap_or(20_000_000);
                            let us = u.unwrap_or(500);
                            log::info!(
                                "deluge-bsp-rust: post-load sim_latency override = {bps} bytes/sec, {us} us overhead"
                            );
                            Some((bps, us))
                        }
                    }
                };
                #[cfg(not(feature = "sim_latency"))]
                let post_load_sim_latency = None;
                scenario::ScenarioConfig {
                    song_full_path: Box::leak(song.into_boxed_str()),
                    target_blocks,
                    step_timeout: embassy_time::Duration::from_secs(step_timeout_secs),
                    post_load_sim_latency,
                    // R5a Phase 0 Task 4's contention knob — not wired to an env var here.
                    // No overlapping listings on this manual `cargo run` scenario path.
                    concurrent_listing_every_blocks: None,
                }
            });

        // --- Opt-in recorder live-readback probe ----------------------------
        // Off by default. Set DELUGE_RECORDER_PROBE=1 to switch this run into the diagnostic:
        // construct a real SampleRecorder, feed it audio, and probe whether a still-recording
        // sample's data can be read back through the region port on THIS target — see
        // recorder_probe.rs's module doc. Mutually exclusive with the streaming-underrun
        // scenario above in practice (both would work concurrently, but nothing exercises them
        // together) — this task only needs the boot-ready signal, not a loaded song.
        let recorder_probe_requested = std::env::var("DELUGE_RECORDER_PROBE").as_deref() == Ok("1");
        let recorder_probe_step_timeout_ms: u64 =
            std::env::var("DELUGE_RECORDER_PROBE_STEP_TIMEOUT_MS")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(20_000);
        let recorder_probe_poll_window_ms: u64 =
            std::env::var("DELUGE_RECORDER_PROBE_POLL_WINDOW_MS")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(5_000);

        // --- Opt-in finalized multi-cluster regression probe -----------------
        // Off by default. Set DELUGE_RECORDER_FINALIZE_PROBE=1 to switch this run into the
        // regression gate: construct a real SampleRecorder, drive it to RecorderStatus::COMPLETE,
        // and confirm the residency table was sized correctly + region index 1 reads back
        // correctly through the region port on THIS target — see recorder_finalize_probe.rs's
        // module doc. Mutually exclusive with the other opt-in probes/scenarios in practice.
        let recorder_finalize_probe_requested =
            std::env::var("DELUGE_RECORDER_FINALIZE_PROBE").as_deref() == Ok("1");
        let recorder_finalize_probe_step_timeout_ms: u64 =
            std::env::var("DELUGE_RECORDER_FINALIZE_PROBE_STEP_TIMEOUT_MS")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(20_000);
        let recorder_finalize_probe_poll_window_ms: u64 =
            std::env::var("DELUGE_RECORDER_FINALIZE_PROBE_POLL_WINDOW_MS")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(5_000);

        start_threaded_host(move |spawner| {
            // Streaming-underrun harness: spawned on THIS executor —
            // the same one `host_app_task` runs on —
            // so `scenario::run`'s C-ABI calls interleave cooperatively with
            // the real app's own task graph, exactly like a real HID event
            // handler would (see scenario.rs's module doc: it never spawns a
            // thread itself).
            if let Some(cfg) = scenario_cfg {
                spawner.spawn(scenario::scenario_task(cfg).unwrap());
            }
            if recorder_probe_requested {
                spawner.spawn(
                    recorder_probe::recorder_probe_task(
                        recorder_probe_step_timeout_ms,
                        recorder_probe_poll_window_ms,
                    )
                    .unwrap(),
                );
            }
            if recorder_finalize_probe_requested {
                spawner.spawn(
                    recorder_finalize_probe::recorder_finalize_probe_task(
                        recorder_finalize_probe_step_timeout_ms,
                        recorder_finalize_probe_poll_window_ms,
                    )
                    .unwrap(),
                );
            }
        });

        // Bounded watchdog: boot must never hang forever. ~20s is generous for
        // a host process (no real hardware waits), but the real app's boot
        // sequence (deluge_boot + registerTasks + encoders::init) touches a lot
        // of BSP surface for the first time on host, so give it room.
        let deadline = Instant::now() + Duration::from_secs(20);
        let mut boot_ok = false;
        loop {
            let tasks = crate::scheduler::registered_task_count();
            if tasks > 0 {
                log::info!(
                    "deluge-bsp-rust: HOST APP boot OK — registerTasks() claimed {tasks} scheduler slot(s)"
                );
                boot_ok = true;
            }
            // Fallback signal: the app rendered its first real OLED frame (only
            // reachable once deluge_boot/registerTasks got far enough to drive
            // display output), in case task-count observation somehow races past
            // a transient zero.
            if deluge_bsp::oled::boot_frame_captured() {
                log::info!("deluge-bsp-rust: HOST APP boot OK — first OLED frame captured");
                boot_ok = true;
            }
            if boot_ok {
                break;
            }
            if Instant::now() >= deadline {
                panic!(
                    "host app boot: registerTasks() had not claimed any scheduler slot within 20s \
                     (deluge_app_init likely wedged or crashed before reaching registerTasks())"
                );
            }
            std::thread::sleep(Duration::from_millis(5));
        }

        // Boot alone (a scheduler slot claimed / first OLED frame) only
        // shows registerTasks() ran — it doesn't show the
        // priority-0 (audio) task actually executes on the second executor
        // thread. Wait (bounded) for at least one `deluge_audio_drive` call
        // observed on the "deluge-audio" thread — the scheduled render, not
        // `AudioEngine::runRoutine()`'s pre-registration direct call from
        // `deluge_boot` (see `audio_host.rs`'s doc comment), which necessarily
        // runs on this (host-app executor) thread and doesn't count.
        let audio_deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if audio_host::audio_thread_render_seen() {
                log::info!(
                    "deluge-bsp-rust: HOST APP audio-thread routing OK — priority-0 task rendered on \"deluge-audio\""
                );
                break;
            }
            if Instant::now() >= audio_deadline {
                panic!(
                    "host app boot: no deluge_audio_drive call was observed on the \"deluge-audio\" \
                     thread within 5s after boot — the priority-0 task did not route to the audio \
                     executor (scheduler::set_audio_spawner wiring regressed?)"
                );
            }
            std::thread::sleep(Duration::from_millis(5));
        }

        // Streaming-underrun harness: if scenario mode was requested, wait
        // for `scenario::scenario_task` (spawned above, on the host-app executor) to
        // finish, report its outcome, and exit — this REPLACES the generic soak below
        // (the scenario's own block-count step already keeps both executors running
        // concurrently for the requested window; a soak on top would just be dead
        // time). Bounded by a generous overall deadline so a wedged scenario still
        // exits nonzero instead of hanging the process forever.
        if let Some(cfg) = scenario_cfg {
            // std::time::Duration, not embassy_time::Duration (cfg.step_timeout's type) —
            // this loop runs on the plain OS thread, same as every other polling loop in
            // this block.
            let watchdog = Duration::from_millis(cfg.step_timeout.as_millis() * 8);
            let deadline = Instant::now() + watchdog;
            let result = loop {
                if let Some(r) = scenario::take_result() {
                    break r;
                }
                if Instant::now() >= deadline {
                    log::error!(
                        "deluge-bsp-rust: HOST APP scenario TIMED OUT after {watchdog:?} with no \
                         result (scenario_task wedged?)"
                    );
                    hard_exit(1);
                }
                std::thread::sleep(Duration::from_millis(20));
            };
            log::info!(
                "deluge-bsp-rust: HOST APP scenario result: song='{}' boot_ready={} \
                 load_dispatched={} listing_completed={} load_committed={} load_completed={} \
                 playback_started={} playback_confirmed_active={} recording_started={} \
                 blocks_rendered={} cluster_reads={} recorder_writes={} underrun_wait={} \
                 underrun_unassign={}",
                cfg.song_full_path,
                result.boot_ready,
                result.song_load_dispatched,
                result.listing_completed,
                result.load_committed,
                result.load_completed,
                result.playback_started,
                result.playback_confirmed_active,
                result.recording_started,
                result.blocks_rendered,
                result.cluster_reads,
                result.recorder_writes,
                result.underrun_wait,
                result.underrun_unassign,
            );
            let ok = result.load_completed
                && result.playback_confirmed_active
                && result.recording_started
                && result.blocks_rendered >= cfg.target_blocks
                && result.cluster_reads > 0
                && result.recorder_writes > 0;
            if ok {
                log::info!(
                    "deluge-bsp-rust: HOST APP scenario PASSED — song streamed + recorded, \
                     real dispatch drained (cluster_reads={}, recorder_writes={})",
                    result.cluster_reads,
                    result.recorder_writes
                );
                hard_exit(0);
            } else {
                log::error!("deluge-bsp-rust: HOST APP scenario FAILED (see fields above)");
                hard_exit(1);
            }
        }

        // If the recorder live-readback probe was requested, wait for it
        // (spawned above, on the host-app executor) to finish, report the finding, and exit —
        // same shape as the scenario block above. A diagnostic, not a normative gate: any
        // outcome (READY, LOADING-forever, UNAVAILABLE) is a valid, reportable finding, so this
        // only hard_exit(1)s if the HARNESS itself failed (couldn't even set up the recorder /
        // never reached boot), not on an unresolved read.
        if recorder_probe_requested {
            let watchdog = Duration::from_millis(
                recorder_probe_step_timeout_ms + recorder_probe_poll_window_ms + 10_000,
            );
            let deadline = Instant::now() + watchdog;
            let result = loop {
                if let Some(r) = recorder_probe::take_result() {
                    break r;
                }
                if Instant::now() >= deadline {
                    log::error!(
                        "deluge-bsp-rust: HOST APP recorder probe TIMED OUT after {watchdog:?} \
                         with no result (recorder_probe_task wedged?)"
                    );
                    hard_exit(1);
                }
                std::thread::sleep(Duration::from_millis(20));
            };
            log::info!(
                "deluge-bsp-rust: HOST APP recorder live-readback probe result: boot_ready={} \
                 initial_state={} final_state={} poll_iterations={} resolved_ready={} \
                 (state: 0=harness-error 1=READY 2=LOADING 3=UNAVAILABLE)",
                result.boot_ready,
                result.initial_state,
                result.final_state,
                result.poll_iterations,
                result.resolved_ready,
            );
            if !result.boot_ready || result.initial_state == recorder_probe::STATE_HARNESS_ERROR {
                log::error!(
                    "deluge-bsp-rust: HOST APP recorder probe HARNESS FAILURE (see fields above)"
                );
                hard_exit(1);
            }
            if result.resolved_ready {
                log::info!(
                    "deluge-bsp-rust: HOST APP recorder probe FINDING — a still-recording \
                     sample's data DID resolve READY on this target."
                );
            } else {
                log::warn!(
                    "deluge-bsp-rust: HOST APP recorder probe FINDING — a still-recording \
                     sample's data did NOT resolve READY on this target within the poll window \
                     (final_state={}) — matches the SR3b routing spike's prediction that the \
                     async loader cannot read a handle-less recording.",
                    result.final_state,
                );
            }
            hard_exit(0);
        }

        // If the finalized multi-cluster regression probe was requested, wait for it
        // (spawned above, on the host-app executor) to finish, report the finding, and exit. This
        // one IS a normative gate (unlike the live-readback probe above): a finalized recording's
        // residency table MUST be sized correctly and region index 1+ MUST read back correctly, on
        // every target, or this is a critical regression.
        if recorder_finalize_probe_requested {
            let watchdog = Duration::from_millis(
                recorder_finalize_probe_step_timeout_ms
                    + recorder_finalize_probe_poll_window_ms
                    + 10_000,
            );
            let deadline = Instant::now() + watchdog;
            let result = loop {
                if let Some(r) = recorder_finalize_probe::take_result() {
                    break r;
                }
                if Instant::now() >= deadline {
                    log::error!(
                        "deluge-bsp-rust: HOST APP recorder finalize probe TIMED OUT after \
                         {watchdog:?} with no result (recorder_finalize_probe_task wedged?)"
                    );
                    hard_exit(1);
                }
                std::thread::sleep(Duration::from_millis(20));
            };
            log::info!(
                "deluge-bsp-rust: HOST APP recorder finalize probe result: boot_ready={} \
                 initial_state={} final_state={} poll_iterations={} table_clusters={} \
                 expected_clusters={} bytes_ok={} passed={} \
                 (state: 0=harness-error 1=READY 2=LOADING 3=UNAVAILABLE)",
                result.boot_ready,
                result.initial_state,
                result.final_state,
                result.poll_iterations,
                result.table_clusters,
                result.expected_clusters,
                result.bytes_ok,
                result.passed,
            );
            if !result.boot_ready
                || result.initial_state == recorder_finalize_probe::STATE_HARNESS_ERROR
            {
                log::error!(
                    "deluge-bsp-rust: HOST APP recorder finalize probe HARNESS FAILURE (see fields above)"
                );
                hard_exit(1);
            }
            if result.passed {
                log::info!(
                    "deluge-bsp-rust: HOST APP recorder finalize probe PASSED — finalized \
                     multi-cluster recording's residency table sized correctly \
                     (table_clusters={} >= expected_clusters={}), region 1 read back READY with \
                     correct bytes.",
                    result.table_clusters,
                    result.expected_clusters,
                );
                hard_exit(0);
            } else {
                log::error!(
                    "deluge-bsp-rust: HOST APP recorder finalize probe FAILED — SR3b Task 3 \
                     regression present (see fields above)"
                );
                hard_exit(1);
            }
        }

        // --- Widen the concurrent window before exit ------------------------
        // The boot-OK / audio-routing-OK checks above only prove the two
        // executors are both alive and correctly wired — they fire within
        // ~1-2 render cycles of boot, which leaves the host-app and audio
        // threads almost no time to actually overlap. The real races
        // (`AudioEngine::audioRoutineLocked`, `audioSampleTimer`) come from
        // the *whole* task graph `registerTasks()` spawns (17 slots, incl.
        // `UITimerManager::routine`) ticking on the host-app executor
        // CONCURRENTLY with the audio thread's ongoing renders — so, instead
        // of exiting the instant boot-proof is established, keep both
        // executors running for a bounded soak so the repeating main-thread
        // tasks and the audio render overlap heavily before exit. This is
        // what lets a TSan run actually see the cross-thread C++ race
        // surface.
        //
        // Bounded two ways — wall-clock (`SOAK_DURATION`) and render-cycle
        // count (`SOAK_MAX_RENDER_BLOCKS`, via `audio_host::drive_count()`),
        // whichever is hit first — so this can never hang: it's a fixed
        // sleep-and-poll loop, nothing here waits on anything unbounded.
        const SOAK_DURATION: Duration = Duration::from_secs(4);
        const SOAK_MAX_RENDER_BLOCKS: u64 = 4_000;
        let soak_start = Instant::now();
        let soak_start_blocks = audio_host::drive_count();
        let soak_deadline = soak_start + SOAK_DURATION;
        loop {
            let rendered = audio_host::drive_count().saturating_sub(soak_start_blocks);
            if Instant::now() >= soak_deadline || rendered >= SOAK_MAX_RENDER_BLOCKS {
                log::info!(
                    "deluge-bsp-rust: HOST APP soak done after {:?} — {rendered} render \
                     block(s) rendered concurrently with the main-thread task graph",
                    soak_start.elapsed()
                );
                break;
            }
            std::thread::sleep(Duration::from_millis(20));
        }

        log::info!("deluge-bsp-rust: HOST harness OK");
        // See `hard_exit`'s doc comment: a normal return here (or a plain
        // `std::process::exit`) races the still-live host-app/audio executor
        // threads against libc's atexit-run C++ static destructors.
        hard_exit(0);
    }

    #[cfg(not(feature = "host_app"))]
    log::info!("deluge-bsp-rust: HOST harness OK");
}

/// The emulator's card, from `DELUGE_SIM_CARD`: an image file, read and written in place, or a directory, loaded
/// into a memory disk that [`memory_card::write_back`] mirrors back to it when the emulator closes. Unset, the card is
/// an empty formatted memory disk. Returns the path given. Runs before boot, while nothing has opened the card.
#[cfg(feature = "sim")]
fn insert_sim_card() -> Option<std::path::PathBuf> {
    let card = std::env::var_os("DELUGE_SIM_CARD").map(std::path::PathBuf::from);
    match &card {
        Some(path) if path.is_dir() => memory_card::install(Some(path)),
        Some(path) if path.is_file() => {
            // SAFETY: before boot, on the only thread: nothing reads the environment concurrently.
            unsafe { std::env::set_var("DELUGE_SD_IMAGE", path) };
        }
        Some(path) => {
            eprintln!(
                "DELUGE_SIM_CARD {}: no such file or directory",
                path.display()
            );
            std::process::exit(2);
        }
        None => memory_card::install(None),
    }
    card
}

/// Start the threaded host app: the audio, streaming and app executors, each on its own OS thread, brought up in
/// the order the device brings up its executors (each thread's comment says why its order matters). The app
/// executor spawns the control and display tasks and `host_app_task`, which boots the C++ app, then runs `extra`
/// for the caller's own tasks. Returns once all three threads are running.
#[cfg(feature = "host_app")]
fn start_threaded_host(extra: impl FnOnce(embassy_executor::Spawner) + Send + 'static) {
    use embassy_executor::{Executor, Spawner};
    use std::sync::{Arc, Barrier};

    // --- Second host executor thread for the audio task ----------------
    // Device routes the priority-0 (audio) task onto `AUDIO_EXEC`, a
    // preemptive GIC-SGI interrupt-executor (see `main`, above), so it runs
    // concurrently with — and can preempt — the main thread executor. Host
    // has no interrupt context to stand in for that, so a dedicated
    // `std::thread` running its own platform-std `Executor` is the host
    // analogue: real OS-thread preemption instead of an SGI, but the same
    // "audio is not cooperatively scheduled alongside everything else"
    // property (races enumerated under TSan — this task only needs a
    // functional two-thread boot).
    //
    // Ordering barrier: exactly like the device (`set_audio_spawner` is
    // called *before* the main executor's closure spawns `app_task`, which
    // reaches `registerTasks()`), the main thread here must not let
    // `host_app_task` call `deluge_app_init` until `scheduler::AUDIO_SPAWNER`
    // is `Some` — otherwise `scheduler::claim`'s `use_audio` check races
    // `registerTasks()`'s `addRepeatingTask(priority 0)` and the audio task
    // silently falls back to the cooperative main-thread spawner instead of
    // routing to this thread. A two-party `Barrier` makes the rendezvous
    // synchronous: the audio thread reaches its side immediately after
    // `set_audio_spawner`, the main thread reaches its side immediately
    // before spawning the host-app executor thread below.
    // This host runs audio, streaming and the app on three real OS threads, so a `Pending`
    // storage future here is satisfied by ANOTHER THREAD rather than by nothing —
    // `streaming_fill_task` holds the FS mutex across its SD await on its own thread, so
    // cross-thread contention is the normal case. Install the yielding hook before any
    // thread can issue a transfer, or `sim_block::block_on`'s default no-hook verdict would
    // call a normally-contended read a wedge (its budget is under a millisecond of
    // spinning). See `sim_block::use_threaded_host_hook` for the trade this accepts.
    crate::storage::sim_block::use_threaded_host_hook();

    let audio_spawner_ready = Arc::new(Barrier::new(2));
    let audio_thread_barrier = Arc::clone(&audio_spawner_ready);
    std::thread::Builder::new()
        .name("deluge-audio".into())
        .spawn(move || {
            let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
            executor.run(|spawner: Spawner| {
                // sim_latency harness only: spawned HERE — on this separate OS
                // thread's executor, not the host-app executor's — deliberately.
                // `deluge_app_init` (called synchronously from
                // `host_app_task` below) can itself issue a `sim_latency`-modeled
                // transfer (e.g. the boot-time FatFS mount read) via a plain
                // `block_on`, which hijacks its OS thread with a busy poll loop
                // and never yields back to that thread's executor — so a `pump`
                // spawned on the SAME (host-app) executor would never get polled
                // and the transfer would spin forever. `pump` only touches
                // cross-thread-safe primitives (`Signal`/`AtomicWaker` over
                // `CriticalSectionRawMutex`, `embassy_time::Timer` off the shared
                // std time driver — see sd.rs's module doc), so running it on this
                // independent thread lets it keep making progress while the
                // host-app thread is busy-spinning.
                #[cfg(feature = "sim_latency")]
                spawner.spawn(crate::storage::sd::sim_latency::pump().unwrap());
                crate::scheduler::set_audio_spawner(spawner.make_send());
                log::info!(
                    "deluge-bsp-rust: host audio executor up on thread {:?} — set_audio_spawner done",
                    std::thread::current().name()
                );
                // Release the main thread, which was waiting on this before
                // proceeding to deluge_app_init. `Executor::run`'s closure
                // then returns and the executor blocks polling forever —
                // once `registerTasks()` spawns the priority-0 task onto the
                // stashed `SendSpawner` it runs right here, on this thread.
                audio_thread_barrier.wait();
            });
        })
        .expect("spawning the host audio executor thread");

    // Do not proceed to spawn the host-app executor (whose `host_app_task`
    // calls `deluge_app_init`) until the audio thread has stashed its
    // spawner.
    audio_spawner_ready.wait();

    // --- Third host executor thread for the streaming fill task ------------
    // Device runs the fill task on `STREAM_EXEC`, a preemptive interrupt executor at a
    // priority between audio and thread mode. Host has no interrupt context, so a
    // dedicated `std::thread` with its own executor is the analogue — real OS-thread
    // preemption instead of an SGI, with the property that matters preserved: a
    // non-yielding storage spin on the app thread cannot starve the fill task, which
    // holds the FS mutex across its SD await.
    //
    // Mutual exclusion needs nothing new. `deluge_in_audio_context()` is always false on
    // host, so every thread — this one included — masks, and the global
    // critical-section mutex serializes all three (see `deluge_resource::sync`'s module
    // doc).
    //
    // Same barrier rationale as the audio thread above: the loader queue must have an
    // owner before `host_app_task` reaches `deluge_app_init`, or the enqueues its boot
    // performs sit undrained until some later `FILL_WAKE` happens to arrive.
    //
    // "Owner exists" is all the barrier buys, and all it should: the fill task's first act
    // is to wait on `FILL_WAKE`, deliberately BEFORE constructing `ProdOps`, because that
    // constructor reads app state `deluge_app_init` is still writing. See
    // `streaming_fill_task`'s doc — constructing it any earlier races `Cluster::set_size`
    // exactly that way, and TSan reports it.
    //
    // The audio-file load task shares this thread for the same reason it shares
    // `STREAM_EXEC` on device: it too holds the FS mutex across an SD await, so it must not
    // live where a non-yielding storage spin could starve it. See `audio_loader`'s module
    // doc.
    {
        let stream_ready = Arc::new(Barrier::new(2));
        let stream_barrier = Arc::clone(&stream_ready);
        std::thread::Builder::new()
            .name("deluge-stream".into())
            .spawn(move || {
                let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
                executor.run(|spawner: Spawner| {
                    spawner.spawn(streaming_loader::streaming_fill_task().unwrap());
                    spawner.spawn(audio_loader::audio_load_task().unwrap());
                    spawner.spawn(file_prefix::file_prefix_task().unwrap());
                    spawner.spawn(file_directory::file_directory_task().unwrap());
                    spawner.spawn(file_bytes::file_bytes_task().unwrap());
                    spawner.spawn(file_stream::file_stream_task().unwrap());
                    log::info!(
                        "deluge-bsp-rust: host streaming executor up on thread {:?}",
                        std::thread::current().name()
                    );
                    stream_barrier.wait();
                });
            })
            .expect("spawning the host streaming executor thread");
        stream_ready.wait();
    }

    std::thread::Builder::new()
        .name("deluge-bsp-host-app".into())
        .spawn(move || {
            let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
            executor.run(move |spawner: Spawner| {
                crate::scheduler::set_spawner(spawner);
                spawner.spawn(control::pic_pump().unwrap());
                spawner.spawn(control::pad_render().unwrap());
                spawner.spawn(control::encoder_wake_pump().unwrap());
                spawner.spawn(display::oled_render().unwrap());
                // NOTE: `sim_latency::pump` is deliberately NOT spawned on this
                // executor — see the audio-thread executor closure above for why
                // (this thread's `host_app_task` calls `deluge_app_init`
                // synchronously, which can itself busy-spin a
                // `block_on`'d sim_latency transfer and would starve a
                // same-thread `pump`).
                spawner.spawn(host_app_task().unwrap());
                // The async cluster-fill task is not spawned here: it owns the
                // `"deluge-stream"` thread above, mirroring the device's STREAM_EXEC. A
                // non-yielding storage spin on THIS thread — the one `host_app_task` and
                // the C++ enqueue path run on — must not be able to starve it.
                // The caller's own tasks (a scenario, a probe), on this executor beside the app's.
                extra(spawner);
            });
        })
        .expect("spawning the host app executor thread");
}

/// Immediate, unconditional process termination for the `host_app` boot smoke
/// — skips libc's atexit-run destructors entirely (a raw Linux `exit_group(2)`
/// syscall, not `std::process::exit`/a normal return from `fn main`).
///
/// Needed because the real C++ app links genuine C++ objects with static
/// storage duration (`AudioEngine`, `midiEngine`, `playbackHandler`,
/// `cvEngine`, …) that register destructors via `__cxa_atexit` when
/// constructed at process start (`.init_array`). A normal return from `fn
/// main` (or `std::process::exit`, which still calls libc `exit()`) runs
/// those destructors on the main thread while the host-app executor thread —
/// still alive, still ticking the real scheduler task runners spawned by
/// `registerTasks()` — keeps calling virtual methods on those same objects
/// concurrently. Found running the host_app boot smoke under ThreadSanitizer:
/// the unsanitized build is fast enough to usually win this exit race (the
/// process was gone before a background tick landed mid-destructor), but
/// TSan's instrumentation overhead reliably loses it — a background task's
/// `deluge_display_consume_transfer_ack`/`deluge_audio_input_resync` tick fired
/// after a static's destructor had already reset its vtable, so the next
/// virtual call landed on the pure-virtual stub: `pure virtual method called`
/// / `terminate called without an active exception` (SIGABRT), not a TSan
/// `WARNING: data race` — unsurprising, since the racing code (the prebuilt
/// C++ app + libc's exit path) is outside TSan's instrumentation (see
/// HOST_HARNESS.md's scoping note); TSan only widened the timing window that
/// exposed a real lifecycle bug it can't itself see. This is exactly the
/// "process never returns" shape of the real device's `main -> !` anyway —
/// there is no orderly app shutdown on hardware either — so a hard kill after
/// the boot-OK observation is the *correct* model for this smoke, not a
/// workaround.
#[cfg(feature = "host_app")]
fn hard_exit(code: i32) -> ! {
    #[cfg(target_os = "linux")]
    unsafe {
        core::arch::asm!(
            "syscall",
            in("rax") 231usize, // exit_group
            in("rdi") code,
            options(noreturn, nostack)
        );
    }
    #[cfg(not(target_os = "linux"))]
    std::process::exit(code);
}
