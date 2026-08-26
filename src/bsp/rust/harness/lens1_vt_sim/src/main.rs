//! Lens 1 (streaming-underrun harness): a single-threaded, DETERMINISTIC
//! virtual-time discrete-event simulation of the real `host_app` (song load,
//! sustained playback, concurrent record), measuring `loaded`-miss underruns
//! (`deluge::harness::noteUnderrunWait`/`noteUnderrunUnassign`) as a function of
//! modeled SD latency / audio compute budget — reproducibly, independent of host
//! CPU speed.
//!
//! # Executor + clock
//!
//! Two `embassy_executor::raw::Executor` instances — MAIN (this file's driver loop) and
//! `HP_EXEC` (Task 2's host emulation of a device interrupt executor, see [`preempt`]) —
//! plus a custom context-routed `__pender` (two `AtomicBool` flags, one per executor, see
//! `preempt::note_pend`) and a `PeekableMockDriver` ([`clock`] — custom discrete-event shape
//! for virtual-time simulation). The driver loop: poll MAIN to quiescence,
//! draining `HP_EXEC` alongside it (`preempt::pump_hp`), peek the next due deadline, jump
//! the virtual clock to exactly that deadline, repeat. No `platform-std`/`executor-thread`
//! embassy-executor feature is enabled (see `Cargo.toml`), so there is no executor-supplied
//! `__pender` in this binary — both instances share the one below, and it is ours.
//!
//! # The boot livelock (see `sd.rs`'s `locked_read_sectors` doc comment for the
//! full mechanism and its history)
//!
//! `deluge_app_init` calls the boot-time FatFS mount SYNCHRONOUSLY, off the
//! worker fiber, via `embassy_futures::block_on` — a tight busy-spin poll loop
//! (confirmed by reading `embassy-futures` 0.1.2's actual source: `loop { if let
//! Ready(v) = fut.poll(cx) { return v } }`, no thread parking, no waker use at
//! all) that never returns control to anything else on this one OS thread. Under
//! `sim_latency`, that polled future is `sim_latency::modeled_read`, whose
//! completion depends on a separately spawned `pump()` task's
//! `Timer::after(latency).await` being POLLED by the executor — but the
//! executor's own `poll()` call is already on the stack (it's the one polling
//! the boot task, which is itself inside the synchronous `deluge_app_init` call,
//! which is inside `block_on`'s spin loop): nothing else can be polled until
//! that whole call stack unwinds. Lens 2 (`../src/main.rs`'s `host_app` block)
//! sidesteps this by running `pump` on a second real OS thread — not available
//! here (single-threaded, virtual clock; a real *thread* would reintroduce
//! wall-clock waiting and a second executor OUTSIDE this loop's quiescence
//! check, defeating determinism). A second *executor* driven from INSIDE that
//! same quiescence check, on the same OS thread, is exactly what `HP_EXEC`
//! (`preempt`) is: it never waits on anything wall-clock, and the driver loop
//! still polls it to quiescence every pass, so determinism is preserved. This
//! is why `sim_block`/`preempt` exist — see their module docs for the fuller
//! mechanism. `streaming_fill_task` lives there too, mirroring the device's
//! `STREAM_EXEC`: it holds the FS mutex across its SD await, so a MAIN-resident
//! spin has to be able to drive it to release.
//!
//! **Fix (current): the progress hook, and modeled latency now reaches EVERY transfer.**
//! Off-fiber storage no longer spins hooklessly — it goes through
//! `sim_block::block_on`, whose hook pumps `HP_EXEC` (where `sim_latency::pump` lives) and
//! advances the virtual clock on each `Pending`. So the `Timer` the boot mount waits on is
//! exactly what the hook completes, and the busy-spin has something to wait on after all.
//!
//! This replaced an earlier fix: `sd.rs` used to carry an `off_fiber_instant` escape hatch that
//! made off-fiber transfers skip modeling and take the real synchronous read. That was removed
//! once the hook made it unnecessary — verified, not assumed, by running this harness's four
//! spin selftests and a full sweep with modeled latency reaching off-fiber reads for the first
//! time, with no livelock. The gain is fidelity: task-context reads are now modeled here, where
//! they previously escaped modeling entirely. It does NOT make the margin arm any less vacuous —
//! that is the render-path issue in the charter note below, not a latency-modeling one.
//!
//! # Reused shared substrate
//!
//! `#[path]`-includes the SAME source files `deluge-bsp-rust`'s own `host_app`
//! build compiles (`fiber.rs`, `scheduler.rs`, `sd.rs`, `board.rs`, `control.rs`,
//! `display.rs`, `ffi.rs`, `ffi_extra.rs`, `flash.rs`, `services.rs`,
//! `audio_host.rs`, `scenario.rs`, `sd_image.rs`) — a second, independently
//! configured COMPILATION of the same source (this package's own `mod
//! sys`/Cargo features), not a fork: a change to any of these files is picked
//! up by both packages. The shared-file BEHAVIOR changes this file makes are
//! the small, additive, off-by-default hook called below
//! (`scheduler::set_audio_period_override_us`, documented at its definition in
//! `scheduler.rs`), plus:
//! `preempt::init` calls `sim_block::set_progress_hook` and
//! `sim_block::set_spin_budget(10_000)` (overriding that shared file's own
//! `100_000` default) — see `sim_block.rs`'s and `preempt.rs`'s module docs.
#![feature(impl_trait_in_assoc_type)]

use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

use embassy_executor::raw;
use embassy_time::Duration;
use embassy_time_driver::Driver as _;

mod clock;
mod preempt;
mod selftest;

// Link-only: the host-built C++ `deluge_app` object closure (build.rs) calls the
// deluge_resource_* residency C ABI and the deluge_{alloc,slab_*,heap_*}
// allocator C ABI. `extern crate` forces the rlib onto the link line so those
// `#[no_mangle]` symbols resolve; nothing here references them from Rust
// (mirrors `../src/main.rs`'s identical `host_app`-gated declaration).
extern crate deluge_resource;
// Link-only, same reasoning as `deluge_resource` above: the archived `deluge_app` object closure's
// `audio_file_format.cpp` calls straight into this crate's `deluge_audio_header_*` `#[no_mangle]`
// symbols, whose sole definition it is; nothing here references them from Rust.
extern crate deluge_audio_format;

// These crates export only `#[no_mangle] extern "C"` ABI symbols that the C++ app calls;
// nothing in Rust references them, so without `extern crate` rustc/lld would drop the whole
// rlib and the link would fail with undefined `deluge_sample_*`. Mirrors
// `../../golden_vt_render/src/main.rs`'s identical block.
extern crate deluge_sample_reader;
extern crate deluge_sample_source;
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
// pattern `tests/*.rs` already use for `fiber.rs`/`sd.rs` (see `HOST_HARNESS.md`).
#[path = "../../../src/audio_host.rs"]
mod audio_host;
#[path = "../../../src/board.rs"]
mod board;
#[path = "../../../src/control.rs"]
mod control;
#[path = "../../../src/display.rs"]
mod display;
/// Storage-generic core of the efatfs read path — see
/// `../../../src/main.rs`'s `mod efatfs_core` doc. Gated on `host_app` (always on
/// for this package) to mirror that file's cfg exactly, even though this
/// package's own `#[cfg(feature = "efatfs_streaming")]` is what actually
/// drives `mount()` (below, in `boot_task`).
#[cfg(feature = "host_app")]
#[path = "../../../src/efatfs_core.rs"]
mod efatfs_core;
/// Host counterpart of the device `efatfs_fs.rs` — see
/// `../../../src/main.rs`'s `mod efatfs_host_shim` doc.
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
#[path = "../../../src/scenario.rs"]
mod scenario;
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
#[path = "../../../src/sim_block.rs"]
mod sim_block;
/// Mirrors `../../../src/main.rs`'s `mod streaming_loader` — the
/// async cluster-fill task + its selector/wakeup C ABI. The selector/wakeup
/// symbols (`deluge_streaming_async_active`/`deluge_streaming_signal_fill`)
/// are always compiled so their call sites link regardless of the
/// `async_streaming_loader` feature (see that file's module doc); the actual
/// drain machinery + [`streaming_loader::streaming_fill_task`] stay gated on
/// the feature and are only spawned (below, in `main`) when it's enabled.
#[path = "../../../src/streaming_loader.rs"]
mod streaming_loader;

unsafe extern "C" {
    fn deluge_app_init(board: *const sys::DelugeBoard);
}

// Host-only harness underrun-counter readers (`harness/streaming_underrun.h`) —
// same manual-declaration pattern `scenario.rs` already uses (harness-only
// exports, not part of the bindgen'd libdeluge ABI).
unsafe extern "C" {
    fn deluge_sim_underrun_wait_count() -> u64;
    fn deluge_sim_underrun_unassign_count() -> u64;
}

/// `AudioEngine::routine_task`'s virtual per-block cadence: exactly one
/// 128-frame block's worth of real time, in microseconds (128 / 44100 s per
/// block). This is the default `scheduler::set_audio_period_override_us`
/// value (the "compute-budget" knob); overridable via `LENS1_AUDIO_BLOCK_US`
/// for a later margin sweep.
const DEFAULT_AUDIO_BLOCK_PERIOD_US: u64 = 128 * 1_000_000 / 44_100; // 2902 (floor)

/// Boot task: mirrors `deluge-bsp-rust`'s `host_app_task` exactly (PIC-ready
/// wait, SD bring-up, `deluge_app_init`, worker-fiber pump loop) — see
/// `../src/main.rs`'s `host_app_task` doc comment for the full rationale behind
/// each step. Not `#[path]`-included: `host_app_task` is defined INSIDE
/// `../src/main.rs` itself (not a separate module file), which also carries a
/// conflicting `#![no_std]`/device `fn main`/`EXECUTOR` static — there is no
/// clean way to pull just the one function in via `#[path]`. This is a direct,
/// small (~15-line) reimplementation of that sequence, not a duplicated
/// abstraction.
#[embassy_executor::task]
async fn boot_task() {
    deluge_bsp::pic::wait_ready().await;
    crate::sd::boot_init().await;

    // Mirrors `deluge-bsp-rust`'s `host_app_task` efatfs mount (see its comment). This mount
    // runs off-fiber and its transfers ARE modeled — the `off_fiber_instant` escape hatch that
    // used to exempt them is gone, because off-fiber storage now spins through
    // `sim_block::block_on` and the progress hook completes the modeled `Timer`. A failed mount
    // must NOT abort boot — see the device/host_app_task comments this mirrors.
    #[cfg(feature = "efatfs_streaming")]
    match crate::efatfs_host_shim::mount().await {
        Ok(()) => log::info!("lens1-vt-sim: efatfs mounted"),
        Err(()) => {
            log::warn!("lens1-vt-sim: efatfs mount failed — streaming falls back to C FatFS");
        }
    }

    log::info!("lens1-vt-sim: deluge_app_init() (registers + spawns task runners)");
    // SAFETY: called once, after `scheduler::set_spawner` (main, below) and
    // before anything else touches `currentSong`/scheduler state.
    unsafe { deluge_app_init(board::deluge_board()) };
    log::info!("lens1-vt-sim: scheduler running; pumping async worker");

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

/// Runs [`scenario::run`], stashes the result, and flags `done`. A thin task
/// wrapper — `run` itself is thread-agnostic (see `scenario.rs`'s module doc)
/// and makes no assumption about which executor drives it; this is just this
/// binary's way of getting its `Future` onto the one `raw::Executor` and
/// getting the result back out to `main`'s driver loop (which cannot `.await`).
#[embassy_executor::task]
async fn scenario_runner(cfg: scenario::ScenarioConfig, done: &'static AtomicBool) {
    log::info!("lens1-vt-sim: scenario_runner: starting scenario::run");
    let result = scenario::run(cfg).await;
    log::info!(
        "lens1-vt-sim: scenario result: boot_ready={} song_load_dispatched={} \
         listing_completed={} load_committed={} load_completed={} playback_started={} \
         playback_confirmed_active={} recording_started={} blocks_rendered={} \
         cluster_reads={} recorder_writes={} underrun_wait={} underrun_unassign={}",
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
    *RESULT.lock().unwrap() = Some(result);
    done.store(true, Ordering::Release);
}

static RESULT: std::sync::Mutex<Option<scenario::ScenarioResult>> = std::sync::Mutex::new(None);

/// Runs [`selftest::block_on_modeled_read_nested`] and stashes its result — the task
/// `main`'s `--selftest-block-nested` mode spawns onto MAIN specifically so it is polled
/// with `executor.poll()` already on the stack (see that mode's block, and `selftest.rs`'s
/// module doc for the mechanism this is meant to exercise). Mirrors [`scenario_runner`]'s
/// static-result-plus-`done`-flag shape, not a new mechanism.
#[embassy_executor::task]
async fn nested_selftest_task(done: &'static AtomicBool) {
    let result = selftest::block_on_modeled_read_nested();
    *NESTED_SELFTEST_RESULT.lock().unwrap() = Some(result);
    done.store(true, Ordering::Release);
}

/// See [`nested_selftest_task`].
static NESTED_SELFTEST_RESULT: std::sync::Mutex<Option<Result<(), String>>> =
    std::sync::Mutex::new(None);

/// Exists ONLY to arm a real, embassy-registered `Timer` on MAIN with a deadline strictly
/// inside `--selftest-block-nested`'s spin window (100us, vs. the modeled read's own
/// 525us at the default throughput/overhead) — see that mode's block in `main`. Without
/// this, that mode's only outstanding timer belongs to `sim_latency::pump` on `HP_EXEC`, so
/// `advance_to`'s pop-and-wake can only ever touch an HP waker — the brief's named hazard
/// ("if `advance_to` wakes a MAIN timer while a MAIN task's future is `&mut`-borrowed by
/// the outer `executor.poll()`, the resulting interleaving is untested") would go
/// completely unexercised.
///
/// This task's `Timer::after` deadline (100us) fires from INSIDE `nested_selftest_task`'s
/// own non-yielding spin — `advance_to(driver, 100)`, called from `progress_hook` deep in
/// that spin, pops-and-wakes it, which re-enqueues THIS task onto MAIN's real embassy run
/// queue via the genuine waker/`__pender` path, while that same run queue is already being
/// iterated (for `nested_selftest_task`) by the `executor.poll()` call this wake reenters.
/// Spawn ORDER matters for this to land inside the spin rather than before it:
/// `embassy_executor::raw`'s run queue is a LIFO stack (see its own `run_queue.rs` doc,
/// "batches will be iterated in reverse order as they were enqueued"), and a single
/// `executor.poll()` call drains the WHOLE queue in one batch — so this task must be
/// spawned AFTER `nested_selftest_task` (making it the most-recently-pushed, hence
/// FIRST-polled of the two) so its `Timer::after` is armed before `nested_selftest_task`'s
/// poll begins its long non-yielding run and blocks the rest of that same `poll()` batch.
///
/// Never resumed after that one wake: this mode's driver loop exits as soon as
/// `nested_selftest_task` completes (same `executor.poll()` batch or very next one), so
/// this task's second poll — which would just observe `Ready` and return — is never
/// reached. That's fine: arming the timer and taking the one wake is the entire point.
#[embassy_executor::task]
async fn nested_selftest_timer_task() {
    embassy_time::Timer::after(embassy_time::Duration::from_micros(100)).await;
}

/// Our own pender: no thread, no parking — just flags the driver loop polls itself between
/// `raw::Executor::poll()` calls. Routes by the context pointer each executor was created
/// with, so MAIN and HP pends stay distinguishable (see `preempt`).
#[unsafe(export_name = "__pender")]
fn pender(context: *mut ()) {
    preempt::note_pend(context);
}

/// Mirrors of driver-loop state, published for the watchdog thread (spawned in `main`, below)
/// AND for `preempt::progress_hook` via [`advance_to`] — module-level (not `main`-local, as
/// they were pre-Task-2) because `preempt` is a different module and needs a real path to
/// them. `progress_hook` can now ALSO move the virtual clock (Task 2's `HP_EXEC` emulation),
/// so these must be the sole source of truth for anything printed about the timeline — never
/// shadow them with a same-named local that could drift out of sync (that drift is exactly
/// what put stale numbers in the in-loop WEDGED diagnostic before this fix).
static QUIESCENCE_PASSES: AtomicU64 = AtomicU64::new(0);
static ADVANCES: AtomicU64 = AtomicU64::new(0);
static VIRTUAL_NOW_US: AtomicU64 = AtomicU64::new(0);
// `u64::MAX` is the "no outstanding timer" sentinel — never a real deadline (ticks are
// microseconds; a scenario running that long is not a case this harness needs to represent).
static NEXT_DEADLINE_US: AtomicU64 = AtomicU64::new(u64::MAX);
/// Running checksum of every `next` deadline the clock has ever advanced to, from either
/// call site (see [`advance_to`]). Without this, `NEXT_DEADLINE_US` and `VIRTUAL_NOW_US` are
/// equal by construction (both get the same `next`), so the wedge fingerprint's "five"
/// printed fields collapse to fewer genuinely independent scalars than they look like; this
/// adds a real per-step timeline checksum instead. Rotate-xor: O(1), no RNG, no wall clock,
/// and order-sensitive (unlike a plain xor/sum), so it also detects a REORDERING of advances,
/// not just a different multiset of `next` values.
static TIMELINE_HASH: AtomicU64 = AtomicU64::new(0);
/// The virtual-time budget in ticks (`main`'s `budget_ticks`, from `LENS1_VIRTUAL_BUDGET_MS`)
/// — set once in `main`, before the driver loop starts (i.e. before anything can be polled
/// and reach `progress_hook`), so [`advance_to`] can enforce the same ceiling for BOTH call
/// sites. `u64::MAX` here would mean "no budget enforced yet"; `main` always sets a real
/// value first, so this sentinel is purely defensive.
static BUDGET_TICKS: AtomicU64 = AtomicU64::new(u64::MAX);

/// Jump the virtual clock forward to `next`, enforcing the virtual-time budget and updating
/// every watchdog mirror plus the timeline checksum — the ONE place either the driver loop's
/// own advance step or `preempt::progress_hook`'s advance step may move the clock, so the two
/// call sites can never let the mirrors drift apart. `next` must be a real deadline (i.e.
/// `driver.peek_next_deadline()` returned `Some(next)`), not checked again here.
fn advance_to(driver: &clock::PeekableMockDriver, next: u64) {
    // Stored before the budget check (matching the original single-call-site behaviour) so
    // the watchdog sees the over-budget deadline even on the `hard_exit` path below.
    NEXT_DEADLINE_US.store(next, Ordering::Relaxed);
    let budget_ticks = BUDGET_TICKS.load(Ordering::Relaxed);
    if next > budget_ticks {
        if crate::preempt::in_spin() {
            log::error!(
                "lens1-vt-sim: virtual-time budget ({budget_ticks}us) exhausted from INSIDE a \
                 non-yielding sim_block::block_on spin (next deadline at {next}us). The spin's \
                 future never became ready and the clock ran out chasing it — this is a wedged \
                 spin, not a slow scenario. Check what that future is waiting on and which \
                 executor can satisfy it."
            );
        } else {
            log::error!(
                "lens1-vt-sim: virtual-time budget ({budget_ticks}us) exhausted before the \
                 scenario completed (next deadline at {next}us) — scenario wedged"
            );
        }
        hard_exit(2);
    }
    let now = driver.now();
    if next > now {
        driver.advance_ticks(next - now);
    }
    VIRTUAL_NOW_US.store(next, Ordering::Relaxed);
    let h = TIMELINE_HASH.load(Ordering::Relaxed);
    TIMELINE_HASH.store(h.rotate_left(7) ^ next, Ordering::Relaxed);
    ADVANCES.fetch_add(1, Ordering::Relaxed);
}

fn main() {
    // NOTE for anyone chasing "why did RUST_LOG=trace print nothing": `deluge-bsp`/`rza1l-hal`
    // (pulled in for their `sd`/`pic` host surface) both request `log`'s `release_max_level_off`
    // feature for the DEVICE build's benefit — Cargo unifies that feature across this whole
    // binary's dependency graph, so a `--release` build of THIS package also compiles every
    // `log::*!` call to a no-op (`log::STATIC_MAX_LEVEL` is `Off`), regardless of `RUST_LOG`.
    // `println!`/`eprintln!` are unaffected. Use a debug build (`cargo build`, no `--release`)
    // when you need the log output; `--release` is fine (faster) once you trust a config and
    // just want the final `LENS1_RESULT` line.
    env_logger::init();
    log::info!("lens1-vt-sim: Lens 1 deterministic virtual-time streaming sim");

    let fixture = std::env::var("LENS1_FIXTURE").unwrap_or_else(|_| "cordae".to_string());
    let song_path = std::env::var("LENS1_SONG").unwrap_or_else(|_| "SONGS/Cordae.XML".to_string());
    let target_blocks: u64 = std::env::var("LENS1_BLOCKS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(2000);
    let throughput_bps: u32 = std::env::var("LENS1_THROUGHPUT_BPS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(20_000_000);
    let overhead_us: u32 = std::env::var("LENS1_OVERHEAD_US")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(500);
    let audio_block_us: u64 = std::env::var("LENS1_AUDIO_BLOCK_US")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(DEFAULT_AUDIO_BLOCK_PERIOD_US);
    // Bound on VIRTUAL time simulated before giving up (NOT wall time — see
    // `drive`'s doc comment): generous, since a wedged scenario should fail
    // loudly rather than "simulate" forever.
    let budget_ms: u64 = std::env::var("LENS1_VIRTUAL_BUDGET_MS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(120_000);
    // A per-STEP VIRTUAL-time bound: `scenario.rs`'s `wait_for` compares this
    // against the mocked `embassy_time::Instant`, not the wall clock (see its
    // callers at `scenario.rs:165,182,194,224,233`). Do NOT also use this for
    // any wall-clock deadline below — the two are unrelated units that happen
    // to share a name, and conflating them (as an earlier version of this
    // guard did) makes the driver-loop watchdog's firing time depend on this
    // scenario-tuning knob instead of on real host speed.
    let step_timeout_s: u64 = std::env::var("LENS1_STEP_TIMEOUT_S")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(30);
    // R5a Phase 0 Task 4 contention knob (`ScenarioConfig::concurrent_listing_every_blocks`):
    // task-context file I/O (a load-browser listing, dispatched-and-never-committed) fired
    // every N rendered audio blocks, overlapping the sustained sample streaming this
    // scenario measures. Unset (the default) leaves the block-target wait exactly as it was
    // before this knob existed — see that field's doc comment for why `None` must be inert.
    // Same env-var-driven plumbing as every other `LENS1_*` knob above/below (no CLI parser
    // in this binary — see `post_load_sim_latency`'s `LENS1_THROUGHPUT_BPS`/`LENS1_OVERHEAD_US`
    // for the established pattern this follows).
    let concurrent_listing_every_blocks: Option<u64> =
        std::env::var("LENS1_CONCURRENT_LISTING_EVERY_BLOCKS")
            .ok()
            .and_then(|s| s.parse().ok());
    // A whole-process WALL-clock budget for the driver loop below (Steps 8-9 of
    // `task-0-brief.md`) — real time, never compared against virtual time or
    // against `step_timeout_s` above. PROVISIONAL default: 300s was picked
    // generously because no fixture we have completes yet (`cordae` wedges),
    // so there is no healthy run to measure a real ceiling against. Re-size
    // this once a fixture reaches the post-loop success path.
    let wall_timeout_s: u64 = std::env::var("LENS1_WALL_TIMEOUT_S")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(300);
    log::info!(
        "lens1-vt-sim: fixture={fixture} song={song_path} target_blocks={target_blocks} \
         throughput_bps={throughput_bps} overhead_us={overhead_us} audio_block_us={audio_block_us} \
         budget_ms={budget_ms} wall_timeout_s={wall_timeout_s} \
         concurrent_listing_every_blocks={concurrent_listing_every_blocks:?}"
    );

    // Pack (or reuse) a real FAT SD image from the golden corpus — same tooling
    // the scenario driver uses. Must run before anything below touches SD.
    if std::env::var_os("DELUGE_SD_IMAGE").is_none() {
        let repo_root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .ancestors()
            .nth(5)
            .expect("CARGO_MANIFEST_DIR (src/bsp/rust/harness/lens1_vt_sim) has a repo root 5 levels up")
            .to_path_buf();
        let img = sd_image::pack_golden_fixture(&repo_root, &fixture);
        // SAFETY: single-threaded at this point (before any task/executor exists).
        unsafe { std::env::set_var("DELUGE_SD_IMAGE", &img) };
    }

    // See this file's module doc: sidesteps the boot-time off-fiber block_on
    // livelock. Set once, before anything spawns.
    // `post_load_sim_latency`'s design applies the modeled latency ONLY after
    // load completes, deliberately sparing the essential-sample preload (load
    // has no real-time deadline). That's also WHY it can never produce a
    // WAIT-class underrun on its own: `SampleHolder::claimClusterReasons`
    // (called during load, `CLUSTER_ENQUEUE`) always leaves `clusters[0]`
    // loaded by the time playback starts (see `LoadSongUI::performLoad`'s
    // internal `yieldWithTimeout(..., 5)` gate at load_song_ui.cpp:480, which
    // waits — at the DEFAULT fast latency — comfortably within 5s). Setting
    // `LENS1_PRELOAD_LATENCY=1` instead applies the SAME throughput/overhead
    // globally, from before boot, so essential preload is ALSO starved: if a
    // single cluster fetch takes longer than that internal 5s gate, load
    // proceeds anyway with `clusters[0]->loaded == false`, and
    // `attemptLateSampleStart`'s WAIT branch should fire the instant
    // `resumePlayback`/note-on checks it. This is the harness's positive-
    // control lever for exercising that WAIT branch.
    let preload_latency = std::env::var("LENS1_PRELOAD_LATENCY")
        .ok()
        .is_some_and(|s| s == "1");
    #[cfg(feature = "sim_latency")]
    if preload_latency {
        sd::sim_latency::set_throughput_bytes_per_sec(throughput_bps);
        sd::sim_latency::set_command_overhead_us(overhead_us);
    }
    // Applied to `scenario::run` AFTER load completes (see
    // `ScenarioConfig::post_load_sim_latency`'s doc comment) — this is the
    // knob that stresses sustained real-time streaming, not load itself. When
    // `preload_latency` is set the override is already globally in effect, so
    // there's nothing left for `scenario::run` to apply post-load.
    let post_load_sim_latency = if preload_latency {
        None
    } else {
        Some((throughput_bps, overhead_us))
    };

    // Pin the audio (priority-0) task to the exact virtual block cadence — see
    // `scheduler.rs`'s `AUDIO_PERIOD_OVERRIDE_US` doc comment (the
    // "compute-budget" knob).
    scheduler::set_audio_period_override_us(audio_block_us, 0);

    // SAFETY: `raw::Executor` needs `&'static`; leaking a Box is the standard pattern for a
    // `fn main` that doesn't itself run forever (`embassy_executor::Executor::run`'s own doc
    // uses the same pattern).
    let executor: &'static raw::Executor =
        Box::leak(Box::new(raw::Executor::new(preempt::MAIN_TAG as *mut ())));
    // HP_EXEC: the host emulation of the device's preemptive interrupt executor. Hosts the
    // tasks a non-yielding spin must be able to drive — `sim_latency::pump` (timer-class) and
    // `streaming_fill_task` (mutex-class: it holds the FS mutex across its SD await).
    let hp_executor: &'static raw::Executor =
        Box::leak(Box::new(raw::Executor::new(preempt::HP_TAG as *mut ())));
    preempt::init(hp_executor);
    let spawner = executor.spawner();
    let hp_spawner = hp_executor.spawner();
    scheduler::set_spawner(spawner);

    // `--selftest-block-nested` (below) needs this before anything else is spawned: that
    // mode deliberately spawns NOTHING from the real app's task set below except
    // `sim_latency::pump` — the point of that mode is to isolate the nesting variable (a
    // task's own spin, called while MAIN's `executor.poll()` is already on the stack).
    // The decisive reason to skip `boot_task` here, not just a "cleaner measurement"
    // preference: its mount reaches SD through the HOOKLESS `embassy_futures::block_on`
    // (`sd.rs:47`'s import, used by `deluge_block_read`'s host body at `sd.rs:616`) rather
    // than `sim_block::block_on`, so nothing can pump `sim_latency::pump`'s `Timer` for THAT
    // call path: spawning `boot_task` here would not merely perturb the measurement, it would
    // reproduce the original boot livelock outright (100% CPU, virtual clock frozen) and this
    // mode would never reach its assertion at all. Note this hazard is about the HOOKLESS
    // import specifically — the hooked spin is exactly what makes the rest of this harness
    // work. A lesser,
    // secondary reason: even if that livelock didn't exist, `boot_task`'s own SD activity
    // would race the selftest's single sector-0 read on the same `SD_BUS` lock (`sd.rs`'s
    // `locked_read_sectors`) and could perturb its exact-latency assertion for a reason
    // that has nothing to do with nesting. Do NOT "restore realism" by re-adding these
    // spawns without addressing the hookless-`block_on` livelock first.
    let nested_selftest = std::env::args().any(|a| a == "--selftest-block-nested");
    // The two mutex-class modes (see `selftest`'s "The MUTEX class" section). The positive one
    // contends against an HP_EXEC-resident holder it spawns itself; the control contends
    // against a MAIN-resident one and REQUIRES that no other timer source be live, or its
    // expected wedge turns into an uncatchable `hard_exit(2)` from `advance_to` — see
    // `selftest::block_on_main_held_mutex_control`'s doc, mechanic 2.
    let mutex_selftest = std::env::args().any(|a| a == "--selftest-block-mutex");
    let mutex_control_selftest = std::env::args().any(|a| a == "--selftest-block-mutex-control");
    // Modes that must run with the app's task graph suppressed, for the reasons each documents.
    let isolate_selftest = nested_selftest || mutex_selftest || mutex_control_selftest;

    // Same BSP task set `deluge-bsp-rust`'s host_app boot block spawns (minus
    // the audio-thread split — see module doc: with `scheduler::set_audio_spawner`
    // never called, the priority-0 audio task falls back to this single spawner
    // automatically, the documented fallback path in `scheduler.rs`'s `claim`).
    // Skipped for the isolating selftest modes — see `nested_selftest`'s doc comment above.
    if !isolate_selftest {
        spawner.spawn(control::pic_pump().unwrap());
        spawner.spawn(control::pad_render().unwrap());
        spawner.spawn(control::encoder_wake_pump().unwrap());
        spawner.spawn(display::oled_render().unwrap());
    }
    // On HP_EXEC, not the main executor: a non-yielding `sim_block::block_on` must be able
    // to drive this task to completion, and it cannot re-enter the executor it is running
    // on. See `preempt`'s module doc. Needed by the real run and by the timer-class selftest
    // modes (`--selftest-block` and `--selftest-block-nested`).
    //
    // EXCEPTION — `--selftest-block-mutex-control`: `pump` re-arms a `Timer` on every request,
    // and this control's whole premise is that once the MAIN holder's single timer is consumed
    // there is NO deadline left, so `progress_hook` reports `Stalled` and `sim_block`'s wedge
    // assertion fires. With `pump` live the hook would keep returning `Advanced`, the stalled
    // counter would never accumulate, and the run would die in `advance_to`'s `hard_exit(2)`
    // instead — a raw exit_group the control cannot catch, turning a correct detection into an
    // opaque failure. The control issues no SD transfer, so it needs nothing `pump` provides.
    if !mutex_control_selftest {
        hp_spawner.spawn(sd::sim_latency::pump().unwrap());
    }
    if !isolate_selftest {
        spawner.spawn(boot_task().unwrap());
        // On HP_EXEC, not MAIN: this task holds the FS mutex across its SD await
        // (`efatfs_host_shim::read_at` -> `with_fs`), so a non-yielding
        // `sim_block::block_on` in a MAIN-resident context must be able to drive it to
        // release. The hook pumps HP_EXEC and cannot poll MAIN from inside MAIN's own
        // `poll()`, so while this task sat on MAIN it was unreachable from a spin — the
        // mutex-class case `selftest::block_on_hp_held_mutex` now covers, with
        // `block_on_main_held_mutex_control` proving the MAIN placement really was
        // unsatisfiable. Mirrors the device's `STREAM_EXEC`.
        //
        // Owns the loader queue only once `deluge_streaming_async_active()` reports true
        // (i.e. only under this feature); inert otherwise.
        #[cfg(feature = "async_streaming_loader")]
        hp_spawner.spawn(streaming_loader::streaming_fill_task().unwrap());
    }

    // Published here — before the `--selftest-block` branch below, which can reach
    // `preempt::progress_hook` (via `sim_block::block_on`) just as the driver loop further
    // down does — so `advance_to` can enforce this ceiling for BOTH callers from the very
    // first clock advance either one makes. Left at the later spot (just before the driver
    // loop) this stayed `u64::MAX` (no budget enforced) AND the wall-clock watchdog thread
    // (spawned alongside it, also further down) didn't exist yet, during the selftest's
    // entire run: today harmless (Phase 0 hosts only `sim_latency::pump` on `HP_EXEC`, which
    // arms exactly one timer per request and can't spin unboundedly), but this plan's own
    // binding constraint is "every spin must be wedge-detectable" — see `BUDGET_TICKS`'s doc.
    let budget_ticks = Duration::from_millis(budget_ms).as_ticks();
    BUDGET_TICKS.store(budget_ticks, Ordering::Relaxed);

    // Wall-clock deadline + watchdog thread, published/spawned HERE — before EITHER
    // selftest branch below, not just before the real driver loop further down — for the
    // identical reason `BUDGET_TICKS` moved up (see its own comment just above):
    // `--selftest-block-nested` runs its own driver loop (further down) that calls
    // `unsafe { executor.poll() }` just like the real one does, and until this moved here
    // that mode had NO wall-clock guard at all (`LENS1_WALL_TIMEOUT_S` had no effect on it)
    // — a MAIN/HP task pair that kept re-pending on every fast poll with no clock advance
    // and no `sim_block::block_on` spin in play would have spun at 100% CPU past this
    // process's own wall budget, bounded only by an external `timeout`. `--selftest-block`
    // (the non-nested mode) never calls `executor.poll()` at all, so this is harmless for
    // it — inert until a `poll()` call actually happens.
    let wall_start = std::time::Instant::now();
    // Wall-clock deadline for the loop below — `wall_timeout_s` (a REAL-time
    // budget for this whole loop), never `step_timeout_s` (a per-step VIRTUAL
    // bound belonging to `scenario.rs`; see its doc comment above). A task
    // pair that keeps re-pending on every `executor.poll()` never breaks out
    // of the inner quiescence loop, so neither of that loop's sibling
    // `hard_exit(2)` wedge paths (no outstanding timer / virtual budget
    // exceeded) is ever reached — this bounds REAL time spent stuck there
    // instead.
    let wall_deadline = wall_start + std::time::Duration::from_secs(wall_timeout_s);
    // `QUIESCENCE_PASSES`/`ADVANCES`/`VIRTUAL_NOW_US`/`NEXT_DEADLINE_US`/`TIMELINE_HASH`
    // (module-level, above `main`) mirror loop state for the watchdog thread spawned just
    // below: it must never touch `driver` (see that thread's doc comment) or this function's
    // stack locals, only plain `'static` atomics.
    // Set right after the real driver loop (further down) breaks on success, or right
    // before either selftest branch's own `hard_exit`, before anything that could itself
    // run long (the `RESULT` lock, final logging, `hard_exit(0)`). Without this the
    // watchdog is unconditional: on a HEALTHY run whose total wall time (booting +
    // simulating + this post-loop tail) happens to reach `wall_timeout_s`, it fires anyway
    // and reports a wedge that never happened — observed as a real failure mode of
    // `sweep.sh`'s longer fixtures before this flag was added (see the report for this fix
    // round). In practice neither selftest branch below needs to touch it explicitly: both
    // reach their own `hard_exit` within microseconds of virtual/wall time of entering this
    // function, so there is no realistic path to `wall_deadline` before they exit.
    static DISARMED: AtomicBool = AtomicBool::new(false);
    // A second, independent OS thread that unconditionally fires at
    // `wall_deadline` (unless disarmed) REGARDLESS of whether the main thread
    // ever returns to the in-loop check further down. Necessary, not just defensive:
    // this harness's own module doc (top of file, "The boot livelock") already
    // documents that a single `executor.poll()` call can itself never return —
    // `embassy_futures::block_on`'s poll loop has no waker use and no deadline
    // check of its own — in which case control never comes back to increment
    // `quiescence_passes` at all, and the in-loop check can never run.
    // Confirmed directly on the `cordae` fixture while building this guard: an
    // instrumented build showed `quiescence_passes` stall for good partway
    // through startup (a few hundred microseconds of wall time in, deep inside
    // `executor.poll()`) while the process kept burning ~100% CPU — i.e. stuck
    // inside one non-yielding call, not spinning across many fast ones. See
    // the report for the full readout and hypothesis.
    //
    // Deliberately reads ONLY the atomics above, never `driver.now()` /
    // `driver.peek_next_deadline()`: both go through
    // `critical_section::with`, i.e. a blocking `Mutex::lock()` whose
    // reentrancy allowance is a THREAD-LOCAL flag — safe for the main thread
    // to re-enter its own critical section, but a genuine cross-thread lock
    // for this watchdog thread. If a future wedge ever spun *inside* that
    // critical section (or suspended a fiber while holding it), this thread
    // would block on `lock()` forever and the guard would silently fail
    // (unbounded hang again). Today's `cordae` wedge happens not to hold that
    // lock — which is why a driver-call version of this watchdog "worked" in
    // an earlier fix round — but that was luck, not a property of the design.
    // `peek_next_deadline()` specifically also mutates the timer queue it
    // reports on (`clock.rs`'s `next_expiration` pops-and-wakes anything
    // already due), so a cross-thread caller would race the executor and can
    // destroy the very evidence — an already-due, never-polled timer — that
    // this diagnostic exists to surface.
    std::thread::spawn(move || {
        std::thread::sleep(wall_deadline.saturating_duration_since(std::time::Instant::now()));
        if DISARMED.load(Ordering::Acquire) {
            return;
        }
        // `println!`, not `log::error!`: a release build is built with
        // `log/release_max_level_off` (additive across `rza1l-hal`/
        // `deluge-bsp`), which compiles out `log::` macro bodies entirely —
        // exactly the build that hangs. Reuses the same field set as the
        // periodic progress log and the in-loop check below so all three
        // diagnostics stay comparable (`next_deadline`'s `u64::MAX` sentinel
        // prints as a large number rather than `None`/`Some(..)` here, since
        // it comes from a plain atomic rather than `Option<u64>`).
        println!(
            "lens1-vt-sim: WEDGED (watchdog) — wall-clock wall timeout ({wall_timeout_s}s) \
             exceeded; the main thread never returned to its own in-loop check (see report for \
             why): virtual t={}us quiescence_passes={} advances={} next_deadline={} \
             on_fiber_reads={} on_fiber_writes={} timeline_hash={}",
            VIRTUAL_NOW_US.load(Ordering::Relaxed),
            QUIESCENCE_PASSES.load(Ordering::Relaxed),
            ADVANCES.load(Ordering::Relaxed),
            NEXT_DEADLINE_US.load(Ordering::Relaxed),
            sd::stats::on_fiber_reads(),
            sd::stats::on_fiber_writes(),
            TIMELINE_HASH.load(Ordering::Relaxed),
        );
        // Belt-and-suspenders against the two `hard_exit` call sites racing
        // each other's `exit_group` on the success path (see `DISARMED`'s
        // doc comment): `hard_exit` below already flushes both streams
        // itself, but flushing here too means this thread's diagnostic is on
        // its way to the OS before it does anything else.
        let _ = std::io::Write::flush(&mut std::io::stdout());
        hard_exit(2);
    });

    // The MUTEX-class proofs. Both are placed here, after `BUDGET_TICKS` and the wall-clock
    // watchdog are published, so neither can spin unguarded — same discipline as the
    // timer-class modes below. Neither issues an SD transfer.
    //
    // The positive case must complete; the control must wedge. Running only the positive one
    // proves little, because a pass is also what an uncontended lock produces — see
    // `selftest`'s "The MUTEX class" section.
    if mutex_selftest {
        match selftest::block_on_hp_held_mutex(hp_spawner) {
            Ok(()) => {
                log::info!("selftest: PASSED");
                println!("lens1-vt-sim: SELFTEST-BLOCK-MUTEX PASS");
                hard_exit(0);
            }
            Err(e) => {
                log::error!("selftest: FAILED — {e}");
                println!("lens1-vt-sim: SELFTEST-BLOCK-MUTEX FAIL — {e}");
                hard_exit(2);
            }
        }
    }
    if mutex_control_selftest {
        match selftest::block_on_main_held_mutex_control(spawner, executor) {
            Ok(()) => {
                log::info!("selftest: PASSED");
                println!("lens1-vt-sim: SELFTEST-BLOCK-MUTEX-CONTROL PASS (spin correctly wedged)");
                hard_exit(0);
            }
            Err(e) => {
                log::error!("selftest: FAILED — {e}");
                println!("lens1-vt-sim: SELFTEST-BLOCK-MUTEX-CONTROL FAIL — {e}");
                hard_exit(2);
            }
        }
    }

    // Phase 0's load-bearing proof (Task 3): a modeled SD read driven by a NON-YIELDING
    // `sim_block::block_on`, completing because `preempt::progress_hook` pumps `HP_EXEC`
    // (where `sim_latency::pump` was just spawned, above) and advances the virtual clock.
    // Runs as a mode of this binary — rather than a `#[test]` — because the sim's C++
    // global state cannot be set up twice in one process (see `sweep.sh`'s doc + this
    // file's own module doc). Placed here: after `preempt::init`/the `pump` spawn, before
    // the scenario spawn (and before the driver loop below, which never gets a chance to
    // run in this mode).
    if std::env::args().any(|a| a == "--selftest-block") {
        // This selftest calls `sd::locked_read_sectors` directly from `main`, i.e. off-fiber
        // (no fiber exists yet — `boot_task` hasn't even been polled). That used to need an
        // explicit `set_off_fiber_instant(false)` here, because the harness otherwise exempted
        // off-fiber transfers from modeling and the read would have completed in zero virtual
        // time on the FIRST poll — vacuously "passing" without exercising the hook-driven spin
        // at all. The exemption no longer exists, so the read is modeled unconditionally and
        // this mode is non-vacuous by construction.
        // Let `sim_latency::pump` reach its `REQUEST.wait()` before we issue a transfer —
        // see `selftest`'s module doc ("Why `main` pumps `HP_EXEC` once before issuing the
        // transfer") for why this is good discipline even though `Signal`'s latching
        // behavior means it isn't strictly required for correctness here.
        preempt::pump_hp();
        match selftest::block_on_modeled_read() {
            Ok(()) => {
                log::info!("selftest: PASSED");
                hard_exit(0);
            }
            Err(e) => {
                log::error!("selftest: FAILED — {e}");
                hard_exit(2);
            }
        }
    }

    // Phase 0's NESTED proof (Task 3b, added after Task 3's review): the identical
    // modeled-read/assert body as `--selftest-block` above
    // (`selftest::read_and_assert_modeled_latency`, shared by both), but called from INSIDE
    // a task while MAIN's own `executor.poll()` is already on the stack — the shape every
    // Phase 2 production call site actually uses, and also the shape of the original boot
    // livelock (see `selftest.rs`'s module doc). The ONLY difference from `--selftest-block`
    // is this nesting: same read, same exact-latency assertion, same pre-`pump_hp()`
    // discipline.
    //
    // Unlike `--selftest-block`, this mode DOES have to poll MAIN's `executor` (that is the
    // whole point), so it runs its own minimal driver loop below rather than falling through
    // to the real scenario's — that loop's post-loop success path is specific to
    // `scenario::ScenarioResult` and cannot be reused for a `Result<(), String>`. The loop
    // itself mirrors the shape of the real driver loop further down (poll to quiescence,
    // pump HP_EXEC, check a `DONE` flag, else advance to the next deadline) — not a new
    // mechanism, just the same shape scoped to this mode's own task/result statics.
    if nested_selftest {
        // Same pre-pump discipline as the non-nested mode, and for the same reason — see
        // `selftest.rs`'s module doc ("Why `main` pumps `HP_EXEC` once before issuing the
        // transfer").
        preempt::pump_hp();

        static NESTED_DONE: AtomicBool = AtomicBool::new(false);
        spawner.spawn(nested_selftest_task(&NESTED_DONE).unwrap());
        // Spawned SECOND, deliberately — see [`nested_selftest_timer_task`]'s doc comment
        // for why this order (not the reverse) is what lands its `Timer::after` inside
        // `nested_selftest_task`'s spin window rather than before it starts.
        spawner.spawn(nested_selftest_timer_task().unwrap());

        let driver = clock::PeekableMockDriver::get();
        // Mirrors the real driver loop's own `quiescence_passes` (further down): without
        // this, the watchdog thread spawned above reads `QUIESCENCE_PASSES` (still at its
        // initial 0) if it ever fires during this mode, and reports `quiescence_passes=0` —
        // indistinguishable from "never polled at all", the single most diagnostic field in
        // the wedge report.
        let mut quiescence_passes = 0u64;
        loop {
            loop {
                preempt::clear_main_pended();
                // SAFETY: single-threaded, never called reentrantly. This is the stack
                // shape under test: `nested_selftest_task`'s own `sim_block::block_on` spin
                // (and the `preempt::progress_hook`/`advance_to` calls it makes on every
                // `Pending` poll) all run INSIDE this `executor.poll()` call, with this
                // task's future `&mut`-borrowed by it the whole time.
                unsafe { executor.poll() };
                quiescence_passes += 1;
                QUIESCENCE_PASSES.store(quiescence_passes, Ordering::Relaxed);
                let hp_did_work = preempt::pump_hp();
                if !preempt::main_pended() && !hp_did_work {
                    break;
                }
            }
            if NESTED_DONE.load(Ordering::Acquire) {
                break;
            }
            let Some(next) = driver.peek_next_deadline() else {
                log::error!(
                    "lens1-vt-sim: no task has an outstanding timer — nested selftest wedged \
                     (deadlock)"
                );
                hard_exit(2);
            };
            // Enforces `BUDGET_TICKS` (already published above, before either selftest
            // branch) and keeps the watchdog mirrors in sync — see `advance_to`'s doc
            // comment. This loop's own wedges are also caught by `sim_block::block_on`'s
            // own spin-budget assertion inside the nested task itself; this outer
            // `peek_next_deadline`/`advance_to` step only fires between polls of that task,
            // i.e. once it's already returned control here (e.g. after it completes, or if
            // this loop's own — not the nested spin's — deadline bookkeeping goes wrong).
            advance_to(driver, next);
        }

        match NESTED_SELFTEST_RESULT
            .lock()
            .unwrap()
            .take()
            .expect("nested selftest task set NESTED_DONE without stashing a result")
        {
            Ok(()) => {
                log::info!("selftest: PASSED (nested)");
                hard_exit(0);
            }
            Err(e) => {
                log::error!("selftest: FAILED (nested) — {e}");
                hard_exit(2);
            }
        }
    }

    let song_path_static: &'static str = Box::leak(song_path.into_boxed_str());
    let cfg = scenario::ScenarioConfig {
        song_full_path: song_path_static,
        target_blocks,
        step_timeout: Duration::from_secs(step_timeout_s),
        post_load_sim_latency,
        concurrent_listing_every_blocks,
    };
    static DONE: AtomicBool = AtomicBool::new(false);
    spawner.spawn(scenario_runner(cfg, &DONE).unwrap());

    // --- Discrete-event driver loop -----------------------------------------
    let driver = clock::PeekableMockDriver::get();
    // `BUDGET_TICKS` AND the wall-clock watchdog thread are both already live (see above,
    // before either selftest branch) — both call sites of `advance_to` (the loop below and
    // `preempt::progress_hook`) have had a real ceiling in effect, and the watchdog has been
    // running, from before anything was ever spawned or polled.
    // `quiescence_passes` stays a plain local: only this loop ever increments it (unaffected
    // by `preempt::progress_hook`), so mirroring it into `QUIESCENCE_PASSES` right below has
    // no drift risk. There is no equivalent local for `advances` — `progress_hook` can ALSO
    // advance the clock (via `advance_to`), so `ADVANCES` (module-level) is read directly
    // everywhere below instead of being shadowed by a local that could go stale.
    let mut quiescence_passes = 0u64;
    loop {
        loop {
            preempt::clear_main_pended();
            // SAFETY: single-threaded, never called reentrantly.
            unsafe { executor.poll() };
            quiescence_passes += 1;
            QUIESCENCE_PASSES.store(quiescence_passes, Ordering::Relaxed);
            // Checked every 4096 passes (not every pass) so the check itself
            // cannot dominate runtime in the hot loop. This is the cheap,
            // cooperative half of the guard — it catches a task pair that
            // re-pends on every fast `executor.poll()` call. It CANNOT catch
            // a single `poll()` call that never returns at all; that's what
            // the watchdog thread spawned above is for.
            if quiescence_passes.is_multiple_of(4096) && std::time::Instant::now() >= wall_deadline
            {
                // Unlike the watchdog thread, this runs on the SAME thread as
                // `executor.poll()`, so calling `driver.peek_next_deadline()`
                // here cannot cross-thread-deadlock — worst case it re-enters
                // a critical section this thread already holds. It still has
                // the side effect documented on `clock.rs`'s
                // `peek_next_deadline`: it pops-and-wakes any timer already
                // due, so the value below is the deadline AFTER firing those,
                // not a pure peek.
                // Reads `ADVANCES` (not a local) since `preempt::progress_hook` can also
                // have advanced the clock mid-`executor.poll()`, above — see `advance_to`'s
                // doc comment; a locally-tracked count would silently lie here.
                let advances_now = ADVANCES.load(Ordering::Relaxed);
                println!(
                    "lens1-vt-sim: WEDGED — wall-clock wall timeout ({wall_timeout_s}s) exceeded \
                     inside the quiescence loop (scenario not making progress): virtual \
                     t={}us quiescence_passes={quiescence_passes} advances={advances_now} \
                     next_deadline={:?} on_fiber_reads={} on_fiber_writes={} timeline_hash={}",
                    driver.now(),
                    driver.peek_next_deadline(),
                    sd::stats::on_fiber_reads(),
                    sd::stats::on_fiber_writes(),
                    TIMELINE_HASH.load(Ordering::Relaxed),
                );
                let _ = std::io::Write::flush(&mut std::io::stdout());
                hard_exit(2);
            }
            // Give HP_EXEC a turn too, so it reaches quiescence alongside MAIN.
            let hp_did_work = preempt::pump_hp();
            if !preempt::main_pended() && !hp_did_work {
                break;
            }
        }

        if DONE.load(Ordering::Acquire) {
            log::info!(
                "lens1-vt-sim: scenario task completed at virtual t={}us ({quiescence_passes} \
                 quiescence passes, {} clock advances)",
                driver.now(),
                ADVANCES.load(Ordering::Relaxed),
            );
            break;
        }

        let Some(next) = driver.peek_next_deadline() else {
            log::error!(
                "lens1-vt-sim: no task has an outstanding timer — scenario wedged (deadlock)"
            );
            hard_exit(2);
        };
        // `budget_ticks` enforcement + all mirror/checksum updates live in `advance_to` now
        // (shared with `preempt::progress_hook`'s own advance step) — see its doc comment.
        advance_to(driver, next);
        let advances = ADVANCES.load(Ordering::Relaxed);
        if advances.is_multiple_of(20_000) {
            log::info!(
                "lens1-vt-sim: progress: virtual t={}us advances={advances} passes={quiescence_passes} \
                 on_fiber_reads={} on_fiber_writes={}",
                driver.now(),
                sd::stats::on_fiber_reads(),
                sd::stats::on_fiber_writes(),
            );
        }
    }
    // Disarm before anything else on the success path (the `RESULT` lock,
    // final logging, `hard_exit(0)`) can itself take enough wall time to
    // reach `wall_deadline` — see `DISARMED`'s doc comment above.
    DISARMED.store(true, Ordering::Release);
    let wall_elapsed = wall_start.elapsed();
    log::info!("lens1-vt-sim: wall-clock elapsed: {wall_elapsed:?}");

    let result = RESULT.lock().unwrap().take().expect("scenario result");
    let lifetime_wait = unsafe { deluge_sim_underrun_wait_count() };
    let lifetime_unassign = unsafe { deluge_sim_underrun_unassign_count() };
    log::info!(
        "lens1-vt-sim: FINAL underrun_wait={} underrun_unassign={} (lifetime totals: \
         wait={lifetime_wait} unassign={lifetime_unassign})",
        result.underrun_wait,
        result.underrun_unassign,
    );

    let ok = result.load_completed && result.playback_confirmed_active;
    if !ok {
        log::error!("lens1-vt-sim: scenario did not reach sustained playback (see fields above)");
        hard_exit(1);
    }

    // Single machine-parseable summary line for shell-script harnesses.
    println!(
        "LENS1_RESULT blocks_rendered={} cluster_reads={} recorder_writes={} underrun_wait={} \
         underrun_unassign={}",
        result.blocks_rendered,
        result.cluster_reads,
        result.recorder_writes,
        result.underrun_wait,
        result.underrun_unassign,
    );
    hard_exit(0);
}

/// Immediate, unconditional process termination — skips libc's atexit-run C++
/// static destructors. Same rationale (and same raw `exit_group` shape) as
/// `../src/main.rs`'s `hard_exit`: the real C++ app links objects with static
/// storage duration (`AudioEngine`, `playbackHandler`, …) that register
/// `__cxa_atexit` destructors at process start. A normal `std::process::exit`
/// still runs those destructors — observed here as a SIGSEGV (exit code 139)
/// AFTER the scenario result was already logged/printed, i.e. destructor
/// ordering against some other still-referenced global, not a bug in the
/// scenario itself. This process never needs an orderly C++ shutdown (there
/// is no real device shutdown path either), so skip it.
///
/// Flushes stdout/stderr first: `log`/`env_logger` (via its `anstream` dependency)
/// buffers writes rather than flushing per line, so without this, any run that
/// fails fast — a wedged/timed-out scenario detected only a few log lines into
/// `main`, before enough volume accumulates to trigger an internal flush — would
/// otherwise exit with its diagnostic log output silently discarded (observed
/// directly while investigating this harness: a failing run produced a bare
/// `exit_group(1)` with ZERO captured bytes on either stream). This flush is why
/// the fast-fail case still reports its cause.
fn hard_exit(code: i32) -> ! {
    use std::io::Write as _;
    let _ = std::io::stdout().flush();
    let _ = std::io::stderr().flush();
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
