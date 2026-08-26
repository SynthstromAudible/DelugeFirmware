//! The shared scheduler/fiber exercise body, driving the REAL `scheduler.rs`
//! + `fiber.rs` (via `crate::scheduler`/`crate::fiber`, declared by whichever
//! crate root includes this file with `#[path]`).
//!
//! Two crate roots include this file:
//! - `tests/scheduler_host.rs` — the normal `cargo test` entry point (plain,
//!   unsanitized).
//! - `examples/scheduler_host_tsan.rs` — the ThreadSanitizer entry point. It
//!   exists because `cargo test`'s `--test` harness needs `libtest`, and
//!   building `libtest` for a custom `-Zbuild-std` sanitized target hits a
//!   reproducible nightly Cargo bug (`E0152: duplicate lang item ... sized`,
//!   `core` built twice for the same unit) — a plain `fn main()` binary
//!   (`cargo run --example`) needs no `libtest` and sidesteps it. See
//!   `HOST_HARNESS.md` for the full writeup.
//!
//! This crate is bin-only (no `[lib]` target — see `Cargo.toml`), so neither
//! root can `use deluge_bsp_rust::...`; both instead declare the same `mod
//! fiber; mod scheduler;` tree `main.rs` does, via `#[path]` pointing at the
//! real sources, recompiling those two files unmodified rather than copying
//! them. `scheduler.rs`/`fiber.rs` logic is never duplicated or altered, and
//! every symbol used below (the `scheduler_api.h` C-ABI fns,
//! `fiber::deluge_worker_run`, `fiber::worker_poll`, `fiber::WORKER_WAKE`) is
//! `pub` in those modules.

use std::sync::atomic::{AtomicBool, AtomicI8, AtomicU32, Ordering};
use std::time::{Duration, Instant};

use embassy_executor::{Executor, Spawner};

// ---------------------------------------------------------------------------
// Synthetic task bodies. Captureless `extern "C" fn`s mutating shared atomics,
// exactly the shape the real C++ app hands the scheduler through the same ABI.
// ---------------------------------------------------------------------------

static REPEAT_A_COUNT: AtomicU32 = AtomicU32::new(0);
extern "C" fn repeat_a() {
    REPEAT_A_COUNT.fetch_add(1, Ordering::SeqCst);
}

static REPEAT_B_COUNT: AtomicU32 = AtomicU32::new(0);
extern "C" fn repeat_b() {
    REPEAT_B_COUNT.fetch_add(1, Ordering::SeqCst);
}

static BLOCKED_COUNT: AtomicU32 = AtomicU32::new(0);
extern "C" fn blocked_task() {
    BLOCKED_COUNT.fetch_add(1, Ordering::SeqCst);
}

static ONCE_RAN: AtomicBool = AtomicBool::new(false);
extern "C" fn once_task() {
    ONCE_RAN.store(true, Ordering::SeqCst);
}

/// Gate for the conditional task's `RunCondition` predicate — false until the
/// exercise flips it, so `cond_task` is provably NOT run until then.
static COND_GATE: AtomicBool = AtomicBool::new(false);
static COND_RAN: AtomicBool = AtomicBool::new(false);
unsafe extern "C" fn cond_predicate() -> bool {
    COND_GATE.load(Ordering::SeqCst)
}
extern "C" fn cond_task() {
    COND_RAN.store(true, Ordering::SeqCst);
}

/// The fiber op: runs ON the worker fiber (via `deluge_worker_run`), and
/// suspends in `scheduler::yield()` (== `fiber::yield_until`) until
/// `FIBER_YIELD_GATE` flips — exercising the corosensei stack switch under the
/// executor, not just the queue plumbing around it.
static FIBER_OP_STARTED: AtomicBool = AtomicBool::new(false);
static FIBER_OP_DONE: AtomicBool = AtomicBool::new(false);
static FIBER_YIELD_GATE: AtomicBool = AtomicBool::new(false);
unsafe extern "C" fn fiber_yield_predicate() -> bool {
    FIBER_YIELD_GATE.load(Ordering::SeqCst)
}
extern "C" fn fiber_op(_ctx: *mut core::ffi::c_void) {
    FIBER_OP_STARTED.store(true, Ordering::SeqCst);
    // scheduler_api.h `yield()`: suspends the fiber until the predicate holds.
    crate::scheduler::r#yield(Some(fiber_yield_predicate));
    FIBER_OP_DONE.store(true, Ordering::SeqCst);
}

/// A once-task whose body is what a real C++ dispatch site looks like:
/// submit work to the worker fiber via `deluge_worker_run`.
static FIBER_SUBMIT_RUNS: AtomicU32 = AtomicU32::new(0);
extern "C" fn fiber_submit_task() {
    FIBER_SUBMIT_RUNS.fetch_add(1, Ordering::SeqCst);
    crate::fiber::deluge_worker_run(fiber_op, core::ptr::null_mut());
}

// ---------------------------------------------------------------------------
// SD-routine exclusion gate. Proves the `RESOURCE_SD_ROUTINE` task gate
// in `scheduler.rs` actually defers a tagged task while an SD-routine op holds
// `SD_ROUTINE_HELD` — the mechanism that keeps discardRecorder off a mid-flight
// recorder cardRoutine. Inert-today (run-to-completion), so it must be
// proven here, not just at the counter's engage/release (owner_host does that).
// ---------------------------------------------------------------------------

/// An SD-routine op (submitted via `deluge_worker_run_sd_routine`) that parks on
/// `SD_HOLD_GATE`, holding `SD_ROUTINE_HELD` > 0 for a controlled window.
static SD_HOLD_OP_STARTED: AtomicBool = AtomicBool::new(false);
static SD_HOLD_OP_DONE: AtomicBool = AtomicBool::new(false);
static SD_HOLD_GATE: AtomicBool = AtomicBool::new(false);
unsafe extern "C" fn sd_hold_predicate() -> bool {
    SD_HOLD_GATE.load(Ordering::SeqCst)
}
extern "C" fn sd_hold_op(_ctx: *mut core::ffi::c_void) {
    SD_HOLD_OP_STARTED.store(true, Ordering::SeqCst);
    crate::scheduler::r#yield(Some(sd_hold_predicate));
    SD_HOLD_OP_DONE.store(true, Ordering::SeqCst);
}

/// Conditional task (fires when `SD_SUBMIT_GATE` opens) that submits `sd_hold_op`
/// from the executor thread — the only context allowed to call the worker C ABI.
static SD_SUBMIT_GATE: AtomicBool = AtomicBool::new(false);
unsafe extern "C" fn sd_submit_predicate() -> bool {
    SD_SUBMIT_GATE.load(Ordering::SeqCst)
}
extern "C" fn sd_hold_submit_task() {
    crate::fiber::deluge_worker_run_sd_routine(sd_hold_op, core::ptr::null_mut());
}

/// A `RESOURCE_SD_ROUTINE`-tagged repeating task. Must NOT tick while an
/// SD-routine op holds the counter; must resume once it clears.
static SD_GATED_COUNT: AtomicU32 = AtomicU32::new(0);
extern "C" fn sd_gated_task() {
    SD_GATED_COUNT.fetch_add(1, Ordering::SeqCst);
}

// IDs assigned by the registrar task (executor thread), read by the driving
// thread once `REGISTERED` is set.
static REPEAT_A_ID: AtomicI8 = AtomicI8::new(-1);
static BLOCKED_ID: AtomicI8 = AtomicI8::new(-1);
static REGISTERED: AtomicBool = AtomicBool::new(false);

// ---------------------------------------------------------------------------
// The worker pump: the host-exercise analogue of `main.rs`'s device
// `app_task` loop. Drives `fiber::worker_poll()` so a submitted op actually
// starts/resumes; without this nothing would ever run the fiber.
// ---------------------------------------------------------------------------

#[embassy_executor::task]
async fn worker_pump() {
    use embassy_futures::select::select;
    loop {
        let busy = crate::fiber::worker_poll();
        if busy {
            let _ = select(
                crate::fiber::WORKER_WAKE.wait(),
                embassy_time::Timer::after_millis(2),
            )
            .await;
        } else {
            crate::fiber::WORKER_WAKE.wait().await;
        }
    }
}

/// Registers the synthetic tasks through the real `scheduler_api.h` C ABI.
/// Runs as an embassy task (on the executor thread) so it observes the same
/// "only the executor thread touches `SPAWNER`" invariant `scheduler.rs`
/// documents for the real `registerTasks()` call on device.
#[embassy_executor::task]
async fn registrar() {
    let id_a =
        crate::scheduler::addRepeatingTask(repeat_a, 10, 0.0, 0.001, 0.01, core::ptr::null(), 0);
    assert!(id_a >= 0, "addRepeatingTask(repeat_a) failed");
    let id_b =
        crate::scheduler::addRepeatingTask(repeat_b, 10, 0.0, 0.001, 0.01, core::ptr::null(), 0);
    assert!(id_b >= 0, "addRepeatingTask(repeat_b) failed");
    let id_blocked = crate::scheduler::addRepeatingTask(
        blocked_task,
        10,
        0.0,
        0.001,
        0.01,
        core::ptr::null(),
        0,
    );
    assert!(id_blocked >= 0, "addRepeatingTask(blocked_task) failed");
    let id_once = crate::scheduler::addOnceTask(once_task, 10, 0.0, core::ptr::null(), 0);
    assert!(id_once >= 0, "addOnceTask(once_task) failed");
    let id_cond = crate::scheduler::addConditionalTask(
        cond_task,
        10,
        Some(cond_predicate),
        core::ptr::null(),
        0,
    );
    assert!(id_cond >= 0, "addConditionalTask(cond_task) failed");
    let id_fiber = crate::scheduler::addOnceTask(fiber_submit_task, 10, 0.0, core::ptr::null(), 0);
    assert!(id_fiber >= 0, "addOnceTask(fiber_submit_task) failed");

    // SD-routine exclusion gate: a RESOURCE_SD_ROUTINE (== 4) repeating task that
    // must defer while an SD-routine op holds the counter, plus the conditional
    // submitter that parks that op (fired late, from run(), via SD_SUBMIT_GATE).
    let id_sd_gated = crate::scheduler::addRepeatingTask(
        sd_gated_task,
        10,
        0.0,
        0.001,
        0.01,
        core::ptr::null(),
        4,
    );
    assert!(id_sd_gated >= 0, "addRepeatingTask(sd_gated_task) failed");
    let id_sd_submit = crate::scheduler::addConditionalTask(
        sd_hold_submit_task,
        10,
        Some(sd_submit_predicate),
        core::ptr::null(),
        0,
    );
    assert!(
        id_sd_submit >= 0,
        "addConditionalTask(sd_hold_submit_task) failed"
    );

    REPEAT_A_ID.store(id_a, Ordering::SeqCst);
    BLOCKED_ID.store(id_blocked, Ordering::SeqCst);
    REGISTERED.store(true, Ordering::SeqCst);
}

/// Poll `cond` until true, sleeping in short increments; panics with `what` if
/// `deadline` passes first. This is the "deadlock watchdog": no assertion in
/// this exercise can hang forever.
fn wait_until(deadline: Instant, what: &str, mut cond: impl FnMut() -> bool) {
    loop {
        if cond() {
            return;
        }
        if Instant::now() >= deadline {
            panic!("scheduler_host: timed out waiting for: {what}");
        }
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// Runs the full exercise: bring up the host executor, register synthetic
/// tasks through the real C-ABI, drive block/unblock + the fiber yield from
/// this (non-executor) thread, and assert progress within a wall-clock
/// deadline. Panics (i.e. fails the caller, whether that's a `#[test]` or a
/// plain `fn main()`) on any assertion failure or timeout.
pub fn run() {
    let _ = env_logger::builder().is_test(true).try_init();

    // Bring up the host Embassy executor on its own thread — the same shape
    // as main.rs's device `executor.run(...)` and the deluge-sdk host::run
    // template (a std::thread running `Executor::new().run(...)`).
    std::thread::Builder::new()
        .name("deluge-schedule".into())
        .spawn(|| {
            let executor: &'static mut Executor = Box::leak(Box::new(Executor::new()));
            executor.run(|spawner: Spawner| {
                // Stash the spawner so the add*Task C-ABI entry points can
                // spawn task runners — mirrors main.rs's set_spawner call.
                crate::scheduler::set_spawner(spawner);
                spawner.spawn(worker_pump().unwrap());
                spawner.spawn(registrar().unwrap());
            });
        })
        .expect("spawning the host executor thread");

    let deadline = Instant::now() + Duration::from_secs(15);

    // --- registration ---
    wait_until(deadline, "task registration to complete", || {
        REGISTERED.load(Ordering::SeqCst)
    });
    let repeat_a_id = REPEAT_A_ID.load(Ordering::SeqCst);
    let blocked_id = BLOCKED_ID.load(Ordering::SeqCst);

    // --- repeating tasks make progress ---
    wait_until(deadline, "repeat_a/repeat_b to tick >= 5 times", || {
        REPEAT_A_COUNT.load(Ordering::SeqCst) >= 5 && REPEAT_B_COUNT.load(Ordering::SeqCst) >= 5
    });

    // --- once task ran exactly once ---
    wait_until(deadline, "once_task to run", || {
        ONCE_RAN.load(Ordering::SeqCst)
    });

    // --- conditional task: must NOT have run before the gate opens ---
    wait_until(deadline, "fiber_submit_task to run", || {
        FIBER_SUBMIT_RUNS.load(Ordering::SeqCst) >= 1
    });
    assert!(
        !COND_RAN.load(Ordering::SeqCst),
        "conditional task ran before its predicate was satisfied"
    );
    COND_GATE.store(true, Ordering::SeqCst);
    wait_until(deadline, "cond_task to run after its gate opened", || {
        COND_RAN.load(Ordering::SeqCst)
    });

    // --- fiber op: started and suspended on its own yield_until, not done yet ---
    wait_until(deadline, "fiber_op to start (on the fiber)", || {
        FIBER_OP_STARTED.load(Ordering::SeqCst)
    });
    std::thread::sleep(Duration::from_millis(50));
    assert!(
        !FIBER_OP_DONE.load(Ordering::SeqCst),
        "fiber op completed before its yield_until predicate was satisfied \
         (corosensei suspend/resume did not actually suspend)"
    );
    FIBER_YIELD_GATE.store(true, Ordering::SeqCst);
    wait_until(
        deadline,
        "fiber_op to complete after its yield gate opened",
        || FIBER_OP_DONE.load(Ordering::SeqCst),
    );

    // --- blockTask/unblockTask cycle, driven from this (non-executor) thread:
    // real cross-thread calls into the scheduler's C ABI, concurrent with the
    // executor thread running task_runner — exactly what TSan is here to check.
    crate::scheduler::blockTask(blocked_id);
    // Let a few would-be intervals pass; the task must NOT progress while blocked.
    std::thread::sleep(Duration::from_millis(30));
    let snapshot = BLOCKED_COUNT.load(Ordering::SeqCst);
    std::thread::sleep(Duration::from_millis(30));
    assert_eq!(
        BLOCKED_COUNT.load(Ordering::SeqCst),
        snapshot,
        "blocked_task made progress while blocked"
    );
    crate::scheduler::unblockTask(blocked_id);
    wait_until(deadline, "blocked_task to resume after unblockTask", || {
        BLOCKED_COUNT.load(Ordering::SeqCst) > snapshot
    });

    // --- runTask: force an immediate extra run, cross-thread as above ---
    let before = REPEAT_A_COUNT.load(Ordering::SeqCst);
    crate::scheduler::runTask(repeat_a_id);
    wait_until(deadline, "repeat_a to tick again after runTask", || {
        REPEAT_A_COUNT.load(Ordering::SeqCst) > before
    });

    // --- SD-routine exclusion gate: a RESOURCE_SD_ROUTINE task must
    // defer while an SD-routine op holds SD_ROUTINE_HELD, and resume once it
    // clears. This exercises the scheduler.rs gate itself — owner_host only covers
    // the counter's engage/release. The fiber is idle now (fiber_op completed).
    assert!(
        !crate::fiber::sd_routine_held(),
        "sd_routine_held() true before the SD-routine op was submitted"
    );
    // Fire the conditional submitter; the executor thread submits sd_hold_op,
    // which parks on its gate and holds the counter.
    SD_SUBMIT_GATE.store(true, Ordering::SeqCst);
    wait_until(
        deadline,
        "sd_hold_op to start (parked on the fiber)",
        || SD_HOLD_OP_STARTED.load(Ordering::SeqCst),
    );
    wait_until(
        deadline,
        "SD_ROUTINE_HELD engaged while the op is parked",
        || crate::fiber::sd_routine_held(),
    );
    // While the hold is engaged, the RESOURCE_SD_ROUTINE task must NOT tick.
    std::thread::sleep(Duration::from_millis(30));
    let sd_snapshot = SD_GATED_COUNT.load(Ordering::SeqCst);
    std::thread::sleep(Duration::from_millis(30));
    assert_eq!(
        SD_GATED_COUNT.load(Ordering::SeqCst),
        sd_snapshot,
        "RESOURCE_SD_ROUTINE task ticked while an SD-routine op held the counter \
         (scheduler.rs gate did not defer it)"
    );
    // Release the op; the hold clears and the gated task must resume.
    SD_HOLD_GATE.store(true, Ordering::SeqCst);
    wait_until(deadline, "sd_hold_op to complete", || {
        SD_HOLD_OP_DONE.load(Ordering::SeqCst)
    });
    wait_until(
        deadline,
        "SD_ROUTINE_HELD released after completion",
        || !crate::fiber::sd_routine_held(),
    );
    wait_until(
        deadline,
        "RESOURCE_SD_ROUTINE task to resume after the hold cleared",
        || SD_GATED_COUNT.load(Ordering::SeqCst) > sd_snapshot,
    );

    // --- isSDRoutineActive: cheap coverage of the remaining listed C ABI ---
    assert!(
        !crate::scheduler::isSDRoutineActive(),
        "isSDRoutineActive() should always be false on this BSP"
    );
}
