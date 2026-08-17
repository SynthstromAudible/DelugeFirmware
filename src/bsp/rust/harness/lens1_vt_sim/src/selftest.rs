//! Phase 0's proof: a modeled SD read, driven by a NON-YIELDING `sim_block::block_on`,
//! completes — because the progress hook pumps `HP_EXEC` (where `sim_latency::pump` lives)
//! and advances the virtual clock.
//!
//! Before Phase 0 this shape was the documented livelock: `embassy_futures::block_on` over
//! `sim_latency::modeled_read` spun at 100% CPU with the virtual clock frozen, because
//! nothing could poll `pump` (see `sd.rs`'s `off_fiber_instant` doc comment).
//!
//! Two modes share the identical read-and-assert body ([`read_and_assert_modeled_latency`]),
//! so the ONLY variable between them is the calling context:
//!
//! - [`block_on_modeled_read`] is called directly from `main`, with no `executor.poll()` on
//!   the stack — Task 3's proof.
//! - [`block_on_modeled_read_nested`] is called from INSIDE a task while MAIN's
//!   `executor.poll()` is already on the stack — the shape every Phase 2 production call
//!   site actually uses, and also the shape of the original boot livelock
//!   (`deluge_app_init` → `boot_task` → `executor.poll()`; see `sd.rs:211-231`). Two
//!   mechanisms differ here and could break it: `progress_hook` advancing the virtual clock
//!   while a MAIN task's future is `&mut`-borrowed by the outer `executor.poll()`, and
//!   `clock.rs`'s `peek_next_deadline` popping-and-waking every due timer away from the
//!   quiescence point its own doc comment says it needs. See `main.rs`'s
//!   `--selftest-block-nested` mode for how this gets onto that stack.
//!
//! # Why `main` pumps `HP_EXEC` once before issuing the transfer
//!
//! `main` calls `preempt::pump_hp()` before either mode above so that `sim_latency::pump`'s
//! `REQUEST.wait()` is registered ahead of the first transfer. This matters less than it
//! sounds: `REQUEST` is an `embassy_sync::signal::Signal`, which LATCHES — it stores
//! `Signaled` regardless of whether anything is waiting yet, and a later `poll_wait` sees
//! that latched state and returns `Ready` immediately. `pump` is also already sitting in
//! `HP_EXEC`'s run queue the moment it's spawned. So skipping the pre-pump would not lose
//! the signal; it would only cost one extra `sim_block::block_on` spin iteration (the first
//! poll of `locked_read_sectors` would find `pump` not yet polled, get `Pending`, and the
//! progress hook's own `pump_hp()` would pick it up on the very next iteration). It's still
//! correct discipline to pump first — it makes the ordering explicit rather than relying on
//! `Signal`'s latching behavior, and it is the right call if `REQUEST` is ever swapped for
//! something non-latching. The nested mode follows the same discipline (see its
//! `--selftest-block-nested` call site in `main.rs`): `main` pumps `HP_EXEC` once before
//! spawning the nested task, for the identical reason.
use core::sync::atomic::{AtomicBool, Ordering};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_time::{Duration, Instant};

use crate::preempt;
use crate::{sd, sim_block};

/// Read one sector under a non-yielding block and assert that the modeled read took
/// EXACTLY the virtual latency `sim_latency::latency_for` predicts for that transfer —
/// not merely a non-zero amount. An exact comparison (rather than a `!= 0` check) proves
/// the completion came from the modeled path specifically, not from some other timer
/// firing to end the spin; it also stays correct when the model itself predicts zero
/// (e.g. `LENS1_OVERHEAD_US=0` with a high enough throughput override), where a `!= 0`
/// check would misreport a genuine pass as "the latency model was skipped".
///
/// Shared by both [`block_on_modeled_read`] and [`block_on_modeled_read_nested`] — see this
/// module's doc comment — so the two modes cannot drift apart on anything but the calling
/// context. Returns `(elapsed, bytes)` rather than just `elapsed` so callers can log the
/// ACTUAL transfer size their `sim_latency::latency_for` comparison was made against,
/// rather than a hardcoded literal that would silently go stale if this function's buffer
/// size ever changed.
fn read_and_assert_modeled_latency() -> Result<(Duration, usize), String> {
    let mut buf = [0u8; 512];
    let expected = sd::sim_latency::latency_for(buf.len());
    let t0 = Instant::now();
    // Snapshot BEFORE the spin, not just checked for non-zero after: see the
    // `hooks_before == hooks_after` check below for why a plain "ran at least once
    // ever" read (with no baseline) would not catch the failure this guards against.
    let hooks_before = preempt::hook_invocations();

    sim_block::block_on(async { sd::locked_read_sectors(0, 1, &mut buf).await })
        .map_err(|e| format!("modeled read failed: {e:?}"))?;

    let elapsed = Instant::now() - t0;
    let hooks_after = preempt::hook_invocations();
    // Proves the completion actually came from the hook-driven spin under test, not merely
    // that the virtual clock landed on the right instant by some other means (e.g. a future
    // edit that swaps `sim_block::block_on` for a plain `.await` under an already-polling
    // executor: the outer driver loop would still advance the clock to 525us and the elapsed
    // check below would still pass, with `preempt::progress_hook` never invoked at all).
    if hooks_after <= hooks_before {
        return Err(format!(
            "modeled read completed in {}us virtual time WITHOUT the progress hook ever firing \
             (hook_invocations stayed at {hooks_before}) — the non-yielding spin completed by \
             some other means, so this selftest is no longer proving what it claims. Check that \
             the read still goes through `sim_block::block_on` rather than a plain `.await`.",
            elapsed.as_micros(),
        ));
    }
    if elapsed != expected {
        return Err(format!(
            "modeled read completed in {}us virtual time, but `sim_latency::latency_for({})` \
             predicts {}us — the completion did not come from the modeled path this selftest \
             exists to prove. Check that `sim_latency` is enabled and that `off_fiber_instant` \
             is not exempting this path.",
            elapsed.as_micros(),
            buf.len(),
            expected.as_micros(),
        ));
    }
    Ok((elapsed, buf.len()))
}

/// Read one sector under a non-yielding [`sim_block::block_on`] called directly from `main`
/// — no `executor.poll()` on the stack. See this module's doc comment.
pub fn block_on_modeled_read() -> Result<(), String> {
    let (elapsed, bytes) = read_and_assert_modeled_latency()?;
    log::info!(
        "selftest: non-yielding block_on over a modeled read, called directly from `main` \
         (no executor.poll() on the stack), completed in {}us virtual time (matches \
         sim_latency::latency_for({bytes}) exactly)",
        elapsed.as_micros(),
    );
    Ok(())
}

/// Read one sector under a non-yielding [`sim_block::block_on`] called from INSIDE a task
/// while MAIN's `executor.poll()` is already on the stack — the nested sibling of
/// [`block_on_modeled_read`]. See this module's doc comment for why this shape matters and
/// what could break it; see `main.rs`'s `--selftest-block-nested` mode for how the caller
/// gets this call onto that stack (a MAIN task, polled by the driver loop).
pub fn block_on_modeled_read_nested() -> Result<(), String> {
    let (elapsed, bytes) = read_and_assert_modeled_latency()?;
    log::info!(
        "selftest: non-yielding block_on over a modeled read, called from INSIDE a task \
         under MAIN's executor.poll(), completed in {}us virtual time (matches \
         sim_latency::latency_for({bytes}) exactly)",
        elapsed.as_micros(),
    );
    Ok(())
}

// ── The MUTEX class ─────────────────────────────────────────────────────────────
//
// Everything above proves the TIMER class: the spin's future became ready because the virtual
// clock moved. The two functions below prove the other class, which Phase 0 explicitly did
// not: the spin's future becomes ready only because ANOTHER TASK RAN TO COMPLETION and
// dropped a guard.
//
// The distinction is load-bearing. `progress_hook` pumps `HP_EXEC` and CANNOT poll MAIN from
// inside MAIN's own `poll()`, so a guard held by a MAIN-resident task is never released while
// a MAIN-resident spin waits for it. That is the shape the production code had before the
// streaming fill task moved to `HP_EXEC`: it holds the filesystem mutex across its SD await
// (`efatfs_fs::read_at` -> `with_fs`), so once the fiber's storage waits become non-yielding
// spins, a fiber spin would wait forever on a lock only MAIN could release.
//
// [`block_on_hp_held_mutex`] is the positive case (holder on `HP_EXEC`: must complete).
// [`block_on_main_held_mutex_control`] is the negative control (holder on MAIN: must NOT
// complete). The control is what makes the positive case mean anything — without it, a pass
// could equally be an uncontended lock acquired on the first poll.

/// The guard both mutex-class modes contend for. `u32` payload so the positive case can also
/// assert it observed the holder's write, not merely that it acquired the lock.
static LOCK: Mutex<CriticalSectionRawMutex, u32> = Mutex::new(0);

/// Set by [`hp_mutex_holder`] once it actually holds [`LOCK`]. Read before the spin starts, so
/// a vacuous run (holder never scheduled, lock free) fails loudly instead of passing.
static HP_HOLDER_STARTED: AtomicBool = AtomicBool::new(false);
/// Set by [`hp_mutex_holder`] after its await returns and it has written through the guard.
static HP_HOLDER_DONE: AtomicBool = AtomicBool::new(false);

/// Hold [`LOCK`] across an await on `HP_EXEC` — the same shape `efatfs_fs::read_at` has when it
/// holds the FS mutex across an SD transfer. Only the progress hook pumping `HP_EXEC` can get
/// this task past its await, which is exactly what the positive mutex-class case proves.
#[embassy_executor::task]
async fn hp_mutex_holder() {
    let mut g = LOCK.lock().await;
    HP_HOLDER_STARTED.store(true, Ordering::SeqCst);
    embassy_time::Timer::after_micros(500).await;
    *g += 1;
    HP_HOLDER_DONE.store(true, Ordering::SeqCst);
}

/// Positive mutex-class case: a MAIN-resident non-yielding spin on a guard held across an
/// await by an `HP_EXEC`-resident task.
///
/// Asserts three things, because any one of them alone can pass for the wrong reason:
///   1. the spin returns at all — it is not wedged;
///   2. the holder was genuinely mid-await when the spin started, and had finished by the time
///      the spin acquired (so the lock really was contended across the await);
///   3. `preempt::hook_invocations()` advanced across the spin — the completion came through
///      the mechanism under test, not because some enclosing loop advanced the clock. This is
///      the same false-pass this module's doc warns about for the timer-class modes.
pub fn block_on_hp_held_mutex(hp_spawner: embassy_executor::Spawner) -> Result<(), String> {
    // The task macro yields the pool allocation as a `Result`; `spawn` itself is infallible.
    let token = hp_mutex_holder()
        .map_err(|e| format!("could not allocate the HP_EXEC mutex-holder task: {e:?}"))?;
    hp_spawner.spawn(token);

    // Let the holder take the lock before the spin starts, so the spin is genuinely contended
    // rather than acquiring a free mutex on its first poll. Same pre-pump discipline as the
    // timer-class modes — see this module's doc comment.
    preempt::pump_hp();
    if !HP_HOLDER_STARTED.load(Ordering::SeqCst) {
        return Err(
            "vacuous: the HP_EXEC holder never took the lock, so the spin below would have \
             acquired an uncontended mutex and proved nothing about mutex-class spins"
                .to_string(),
        );
    }

    let hooks_before = preempt::hook_invocations();
    let observed = sim_block::block_on(async { *LOCK.lock().await });
    let hooks_after = preempt::hook_invocations();

    if !HP_HOLDER_DONE.load(Ordering::SeqCst) {
        return Err(
            "the spin acquired the lock before the HP_EXEC holder finished with it — the guard \
             was not actually held across the await, so this proves nothing about mutex-class \
             spins"
                .to_string(),
        );
    }
    if observed != 1 {
        return Err(format!(
            "expected to observe the holder's increment (1) once the spin acquired the lock, \
             saw {observed} — the guard was not held across the write"
        ));
    }
    if hooks_after <= hooks_before {
        return Err(format!(
            "the spin completed WITHOUT the progress hook ever firing (hook_invocations stayed \
             at {hooks_before}) — something other than the mechanism under test satisfied it"
        ));
    }

    log::info!(
        "selftest: non-yielding block_on on a guard held across an await by an HP_EXEC task \
         completed (hook_invocations {hooks_before} -> {hooks_after}, holder's write observed)"
    );
    Ok(())
}

/// Set by [`main_mutex_holder`] once it holds [`LOCK_CONTROL`].
static MAIN_HOLDER_STARTED: AtomicBool = AtomicBool::new(false);
/// The control's own guard — separate from [`LOCK`] so the two modes cannot interfere if both
/// ever run in one process.
static LOCK_CONTROL: Mutex<CriticalSectionRawMutex, u32> = Mutex::new(0);

/// The control's holder: identical to [`hp_mutex_holder`] but spawned on MAIN. A distinct task
/// item, because each `#[embassy_executor::task]` gets its own pool and sharing one would
/// couple the two modes.
#[embassy_executor::task]
async fn main_mutex_holder() {
    let mut g = LOCK_CONTROL.lock().await;
    MAIN_HOLDER_STARTED.store(true, Ordering::SeqCst);
    embassy_time::Timer::after_micros(500).await;
    *g += 1;
}

/// Negative control for [`block_on_hp_held_mutex`]: the identical spin, with the holder on MAIN
/// instead of `HP_EXEC`. The hook pumps `HP_EXEC` and cannot poll MAIN from inside MAIN's own
/// `poll()`, so a MAIN-held guard is never released and the spin must NOT complete.
///
/// This is what makes the positive case meaningful. Three mechanics separate a real control
/// from a vacuous one, and all three are enforced here or by the caller:
///
/// 1. **MAIN is polled once before the spin** (`main_exec`), or the holder never runs, the lock
///    is free, and the spin trivially acquires it.
/// 2. **No other timer source may be live** — the caller must not spawn `sim_latency::pump`.
///    Any task that keeps arming timers makes `progress_hook` return `Advanced` forever, the
///    stalled counter never accumulates, and the run dies in `advance_to` via `hard_exit(2)`,
///    which is a raw `exit_group` syscall and so NOT catchable here.
/// 3. **A small spin budget**, so `sim_block`'s own wedge assertion arrives in bounded time.
///
/// The expected wedge arrives as a panic from inside `sim_block::block_on`. Its own hook is
/// suppressed for the duration so a PASSING control does not print an alarming backtrace.
pub fn block_on_main_held_mutex_control(
    main_spawner: embassy_executor::Spawner,
    main_exec: &'static embassy_executor::raw::Executor,
) -> Result<(), String> {
    // The task macro yields the pool allocation as a `Result`; `spawn` itself is infallible.
    let token = main_mutex_holder()
        .map_err(|e| format!("could not allocate the MAIN mutex-holder task: {e:?}"))?;
    main_spawner.spawn(token);

    // SAFETY: single-threaded harness, and this is the only poll of MAIN on the stack — the
    // spin below reaches only `progress_hook`, which polls HP_EXEC, never MAIN.
    unsafe { main_exec.poll() };
    if !MAIN_HOLDER_STARTED.load(Ordering::SeqCst) {
        return Err(
            "vacuous control: the MAIN holder never took the lock, so the spin below would have \
             acquired a free mutex and the control would 'detect' nothing"
                .to_string(),
        );
    }

    sim_block::set_spin_budget(200);
    let prev_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        sim_block::block_on(async { *LOCK_CONTROL.lock().await })
    }));
    std::panic::set_hook(prev_hook);

    match outcome {
        Err(_) => {
            log::info!(
                "selftest: a non-yielding block_on waiting on a MAIN-held guard wedged, as it \
                 must — the positive mutex-class case is therefore not vacuous"
            );
            Ok(())
        }
        Ok(observed) => Err(format!(
            "NEGATIVE CONTROL FAILED: the spin acquired a MAIN-held guard (observed {observed}). \
             Either the holder released it (check it really awaits while holding) or something \
             is polling MAIN from inside the spin — either way the positive mutex-class \
             selftest proves nothing."
        )),
    }
}
