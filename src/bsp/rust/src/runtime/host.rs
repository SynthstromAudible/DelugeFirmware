//! The host entry: a boot smoke without the C++ app, the threaded host app under `host_app`, and
//! the emulator under `sim`.

#[cfg(all(feature = "host_app", not(feature = "sim")))]
use crate::audio::host as audio_host;
use crate::peripherals::{control, display};
#[cfg(feature = "sim")]
use crate::storage::memory_card;
#[cfg(not(feature = "host_app"))]
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

/// Boots the C++ app on the host app executor, in the device `app_task`'s order: wait for the
/// PIC handshake, bring SD up, mount the filesystem, then `deluge_app_init`.
///
/// It then marks boot finished and parks: the app loop (`crate::app_loop`) runs the app's jobs
/// from then on.
#[cfg(feature = "host_app")]
#[embassy_executor::task]
async fn host_app_task() {
    // The PIC handshake is synthetic on host.
    deluge_bsp::pic::wait_ready().await;
    crate::storage::sd::boot_init().await;

    // Mount before `deluge_app_init`, so the app's first sample load can open a file. A failed
    // mount does not abort boot, as on the device: streamed samples just won't load.
    match crate::storage::efatfs::host::mount().await {
        Ok(()) => log::info!("efatfs: mounted (host)"),
        Err(status) => {
            log::warn!("efatfs: host mount failed ({status}) — streamed samples will not load")
        }
    }

    log::info!("deluge-bsp-rust: host deluge_app_init() (registers the app's jobs)");
    unsafe { sys::deluge_app_init(board::deluge_board()) };
    boot::mark_finished();
    log::info!("deluge-bsp-rust: host boot finished");

    core::future::pending::<()>().await
}

/// The host binary's entry.
///
/// Without `host_app`: an SD round trip through the block C ABI, then a boot smoke of the control
/// and display tasks. With `host_app`: boots the threaded C++ app and runs the harness mode an
/// environment variable selects (a streaming scenario, a recorder probe), else a boot smoke and a
/// short concurrent soak. With `sim`: runs the emulator until its window closes.
pub fn main() {
    env_logger::init();
    log::info!("deluge-bsp-rust: HOST harness");

    // Round-trips sector 1 through `deluge_block_write`/`deluge_block_read`, synchronously, before
    // any executor exists. Skipped under `host_app`, where `DELUGE_SD_IMAGE` may name a real FAT
    // image: the pattern would land on FAT32's FSInfo sector, which embedded-fatfs then refuses to
    // mount. The app's own boot exercises the same ABI there.
    #[cfg(feature = "host_app")]
    log::info!("deluge-bsp-rust: sd round-trip skipped under host_app");
    #[cfg(not(feature = "host_app"))]
    {
        const TEST_SECTOR: u32 = 1;
        let mut pattern = [0u8; 512];
        for (i, b) in pattern.iter_mut().enumerate() {
            *b = (i as u8).wrapping_mul(31).wrapping_add(7);
        }
        // SAFETY: `pattern` holds one 512-byte sector.
        let status = unsafe { sd::deluge_block_write(0, pattern.as_ptr(), TEST_SECTOR, 1) };
        assert_eq!(
            status, 0,
            "sd round-trip: deluge_block_write failed (status={status})"
        );
        let mut readback = [0u8; 512];
        // SAFETY: `readback` holds one 512-byte sector.
        let status = unsafe { sd::deluge_block_read(0, readback.as_mut_ptr(), TEST_SECTOR, 1) };
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

    // The emulator: the simulator owns the main thread and makes the panel and the audio bridge;
    // `deluge_bsp`'s host arms drive the panel where the device's drive the PIC and the OLED. The
    // app is still running when the window closes, hence `hard_exit`.
    #[cfg(feature = "sim")]
    {
        let card = insert_sim_card();
        deluge_simulator::run_brain(|panel, audio| {
            crate::sim_panel::install(panel, audio);
            start_threaded_host(|_| {});
        });
        if card.is_some_and(|path| path.is_dir()) {
            memory_card::write_back();
        }
        hard_exit(0);
    }

    // Boot smoke without the C++ app: the device's control and display tasks on one executor.
    // With no input each initialises and parks; `oled_render` sends one blank frame, which proves
    // boot. The deadline turns a wedged init into a failure rather than a hang.
    #[cfg(not(feature = "host_app"))]
    {
        use embassy_executor::{Executor, Spawner};
        use std::time::{Duration, Instant};

        std::thread::Builder::new()
            .name("deluge-bsp-boot".into())
            .spawn(|| {
                let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
                executor.run(|spawner: Spawner| {
                    spawner.spawn(control::pic_pump().unwrap());
                    spawner.spawn(control::pad_render().unwrap());
                    spawner.spawn(display::oled_render().unwrap());
                });
            })
            .expect("spawning the host BSP executor thread");

        // Wait (bounded) for oled_render to capture its first (blank) frame.
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
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

    // The threaded host app. Boot is proven by boot finishing (`deluge_app_init` returning), with
    // the app's first OLED frame as a fallback signal.
    #[cfg(all(feature = "host_app", not(feature = "sim")))]
    {
        use std::time::{Duration, Instant};

        // Streaming scenario, opted into by DELUGE_STREAMING_SCENARIO_SONG (e.g.
        // "SONGS/Cordae.XML"): load the song, play and record it, and step until
        // DELUGE_STREAMING_SCENARIO_BLOCKS blocks (default 500) have rendered. The card is a
        // private copy of DELUGE_SD_IMAGE if set, else of a FAT image packed from the golden
        // corpus, so every run starts from the same card whatever earlier runs recorded or saved.
        // This must run before anything touches SD: `host_app_task` mounts the card as soon as it
        // starts.
        let scenario_cfg = std::env::var("DELUGE_STREAMING_SCENARIO_SONG")
            .ok()
            .map(|song| {
                let template = std::env::var_os("DELUGE_SD_IMAGE")
                    .map(std::path::PathBuf::from)
                    .unwrap_or_else(|| {
                        let fixture = std::env::var("DELUGE_STREAMING_SCENARIO_FIXTURE")
                            .unwrap_or_else(|_| "cordae".to_string());
                        let repo_root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                            .ancestors()
                            .nth(3)
                            .expect("CARGO_MANIFEST_DIR (src/bsp/rust) has a repo root 3 levels up")
                            .to_path_buf();
                        crate::storage::sd_image::pack_golden_fixture(&repo_root, &fixture)
                    });
                let card =
                    crate::storage::sd_image_path::private_copy(&template).unwrap_or_else(|e| {
                        panic!("copying the card image {} failed: {e}", template.display())
                    });
                log::info!(
                    "streaming-scenario: card is a private copy of {} at {}",
                    template.display(),
                    card.display()
                );
                // SAFETY: no other thread exists yet, so nothing reads the environment
                // concurrently.
                unsafe { std::env::set_var("DELUGE_SD_IMAGE", &card) };
                let target_blocks = std::env::var("DELUGE_STREAMING_SCENARIO_BLOCKS")
                    .ok()
                    .and_then(|s| s.parse().ok())
                    .unwrap_or(500u64);
                // A wider per-step budget, for a very long manual run.
                let step_timeout_secs = std::env::var("DELUGE_STREAMING_SCENARIO_STEP_TIMEOUT_S")
                    .ok()
                    .and_then(|s| s.parse().ok())
                    .unwrap_or(20u64);
                scenario::ScenarioConfig {
                    song_full_path: Box::leak(song.into_boxed_str()),
                    target_blocks,
                    step_timeout: embassy_time::Duration::from_secs(step_timeout_secs),
                }
            });

        // DELUGE_RECORDER_PROBE=1: the recorder live-readback diagnostic (see
        // `recorder_probe`).
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

        // DELUGE_RECORDER_FINALIZE_PROBE=1: the finalized-recording regression gate (see
        // `recorder_finalize_probe`).
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
            // On the app executor, so their C-ABI calls interleave with the app's own tasks as
            // an input handler's would.
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

        // Boot must not hang: a generous bound for a first-time walk of much of the BSP.
        let deadline = Instant::now() + Duration::from_secs(20);
        let mut boot_ok = false;
        loop {
            if crate::boot::finished() {
                log::info!("deluge-bsp-rust: HOST APP boot OK — deluge_app_init returned");
                boot_ok = true;
            }
            if deluge_bsp::oled::boot_frame_captured() {
                log::info!("deluge-bsp-rust: HOST APP boot OK — first OLED frame captured");
                boot_ok = true;
            }
            if boot_ok {
                break;
            }
            if Instant::now() >= deadline {
                panic!(
                    "host app boot: deluge_app_init had not returned within 20s (wedged or crashed during boot)"
                );
            }
            std::thread::sleep(Duration::from_millis(5));
        }

        // Boot does not show the render task runs on the audio thread: wait for a
        // `deluge_audio_drive` call seen there. `deluge_boot`'s direct render call runs on the app
        // thread and does not count.
        let audio_deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if audio_host::audio_thread_render_seen() {
                log::info!(
                    "deluge-bsp-rust: HOST APP audio-thread routing OK — render task rendered on \"deluge-audio\""
                );
                break;
            }
            if Instant::now() >= audio_deadline {
                panic!(
                    "host app boot: no deluge_audio_drive call was observed on the \"deluge-audio\" \
                     thread within 5s after boot — the render task did not run on the audio \
                     executor"
                );
            }
            std::thread::sleep(Duration::from_millis(5));
        }

        // A scenario replaces the soak below: report its outcome and exit. The deadline makes a
        // wedged scenario fail rather than hang.
        if let Some(cfg) = scenario_cfg {
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

        // The live-readback probe is a diagnostic: every read outcome is a finding, and it fails
        // only if the harness itself did.
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

        // The finalize probe is a gate: a finalized recording's residency table must be sized for
        // all its clusters, and region 1 must read back correctly.
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

        // Soak: keep the app's task graph and the audio render running together for a while, so
        // a TSan run sees the cross-thread races. Bounded by wall-clock time and by rendered
        // blocks, whichever comes first.
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
        hard_exit(0);
    }

    #[cfg(not(feature = "host_app"))]
    log::info!("deluge-bsp-rust: HOST harness OK");
}

/// Inserts the emulator's card from `DELUGE_SIM_CARD` and returns the path given.
///
/// An image file is read and written in place. A directory is loaded into a memory disk, which
/// [`memory_card::write_back`] mirrors back to it when the emulator closes. Unset, the card is an
/// empty formatted memory disk. Must run before boot, while nothing has opened the card.
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

/// Starts the threaded host app: the audio, streaming and app executors, each on its own OS thread,
/// in the device's bring-up order.
///
/// The app executor spawns the control and display tasks, [`host_app_task`] and the app loop, then
/// runs `extra` for the caller's own tasks. Returns once all three threads are running.
#[cfg(feature = "host_app")]
fn start_threaded_host(extra: impl FnOnce(embassy_executor::Spawner) + Send + 'static) {
    use embassy_executor::{Executor, Spawner};
    use std::sync::{Arc, Barrier};

    // Here a pending storage future is satisfied by another thread (the fill task holds the FS
    // mutex across its SD await), so contention is normal. Install the yielding hook before any
    // transfer, or `sim_block::block_on`'s default would call a contended read a wedge.
    crate::storage::sim_block::use_threaded_host_hook();

    // The audio thread: the host's stand-in for the preemptive `AUDIO_EXEC`.
    let audio_executor_ready = Arc::new(Barrier::new(2));
    let audio_thread_barrier = Arc::clone(&audio_executor_ready);
    std::thread::Builder::new()
        .name("deluge-audio".into())
        .spawn(move || {
            let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
            executor.run(|spawner: Spawner| {
                crate::audio::host::set_audio_executor();
                spawner.spawn(crate::audio::render::render_task().unwrap());
                log::info!(
                    "deluge-bsp-rust: host audio executor up on thread {:?} — render task spawned",
                    std::thread::current().name()
                );
                audio_thread_barrier.wait();
            });
        })
        .expect("spawning the host audio executor thread");

    // `set_audio_executor` must precede `deluge_app_init`: `deluge_audio_await_block` reads it.
    audio_executor_ready.wait();

    // The storage thread: the host's stand-in for `STREAM_EXEC`. The tasks here hold the FS mutex
    // across SD awaits, so a non-yielding storage spin on the app thread must not starve them.
    // `deluge_in_audio_context()` is false on host, so every thread masks and the global
    // critical-section mutex serializes all three.
    //
    // The barrier gives the loader queue an owner before `deluge_app_init`, or the enqueues boot
    // performs sit undrained. It buys only that: the fill task waits on `FILL_WAKE` before
    // constructing `ProdOps`, which reads app state `deluge_app_init` is still writing.
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
                spawner.spawn(control::pic_pump().unwrap());
                spawner.spawn(control::pad_render().unwrap());
                spawner.spawn(display::oled_render().unwrap());
                spawner.spawn(host_app_task().unwrap());
                spawner.spawn(crate::app_loop::app_loop().unwrap());
                extra(spawner);
            });
        })
        .expect("spawning the host app executor thread");
}

/// Ends the process at once with `_exit`, skipping libc's atexit handlers.
///
/// A normal exit runs the C++ app's static destructors (`AudioEngine`, `midiEngine`, …) while the
/// app and audio threads are still calling into those objects, which ends in `pure virtual method
/// called`. The device never returns from `main` either, so a hard exit is the right model.
#[cfg(feature = "host_app")]
fn hard_exit(code: i32) -> ! {
    unsafe extern "C" {
        fn _exit(code: i32) -> !;
    }
    // SAFETY: `_exit` ends the process without running atexit handlers; it takes any status.
    unsafe { _exit(code) }
}
