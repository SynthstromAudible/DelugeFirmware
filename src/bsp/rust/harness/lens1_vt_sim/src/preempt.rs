//! Emulated interrupt preemption for the single-threaded Lens 1 sim (R5a Phase 0).
//!
//! On device, a non-yielding `block_on` in thread mode is preempted by an interrupt
//! executor, so the task holding a lock (or the `sim_latency::pump` equivalent) still runs
//! and the spin completes. Host has one OS thread and no interrupts, so this module
//! emulates that: the spin's progress hook polls a SECOND executor (`HP_EXEC`) and then
//! advances the virtual clock.
//!
//! Polling `HP_EXEC` from inside a task running on the MAIN executor is sound: they are
//! distinct `raw::Executor` instances with distinct state, the process is single-threaded,
//! and this is precisely what `embassy_executor::InterruptExecutor::on_interrupt()` does on
//! device (poll executor B while executor A's `poll()` sits on the stack). It is NOT the
//! reentrant same-executor poll that `raw::Executor::poll` forbids.
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

use embassy_executor::raw;
// Brings `PeekableMockDriver::now()` (the `embassy_time_driver::Driver` trait method used
// by `progress_hook`, below) into scope; `peek_next_deadline`/`advance_ticks` are inherent
// methods and need no import.
use embassy_time_driver::Driver as _;

use crate::clock;
use crate::sim_block::Progress;

/// Pender context tags. `__pender` receives the context pointer the executor was created
/// with, so distinct tags let one global pender route to the right flag.
pub const MAIN_TAG: usize = 1;
pub const HP_TAG: usize = 2;

static MAIN_PENDED: AtomicBool = AtomicBool::new(false);
static HP_PENDED: AtomicBool = AtomicBool::new(false);
/// Reentrancy guard for [`pump_hp`] — enforces this module's doc-comment claim that
/// `HP_EXEC` is never polled from within itself. See `pump_hp`'s doc comment.
static IN_HP: AtomicBool = AtomicBool::new(false);

static mut HP: Option<&'static raw::Executor> = None;

/// Counts every call to [`progress_hook`] — i.e. every hook-driven advance
/// (`HP_EXEC` doing work OR the virtual clock being pushed to the next deadline).
/// `Relaxed` is fine: this is diagnostic only, and the whole harness is
/// single-threaded (see this module's doc comment).
///
/// Exists so a selftest that spins on [`crate::sim_block::block_on`] can assert the
/// completion it observed actually came from the hook mechanism under test, rather
/// than some other means (e.g. a future edit that swaps `block_on` for a plain
/// `.await` under an already-running executor, which would let the surrounding
/// driver loop advance the clock to the same virtual instant with the hook never
/// invoked at all — see `selftest.rs`'s module doc for the exact failure this
/// guards against).
static HOOK_INVOCATIONS: AtomicU64 = AtomicU64::new(0);

/// Snapshot of [`HOOK_INVOCATIONS`] for callers that want to assert it advanced
/// across some span (e.g. `hook_invocations() before` ... spin ... `hook_invocations()
/// after`, asserting `after > before`).
pub fn hook_invocations() -> u64 {
    HOOK_INVOCATIONS.load(Ordering::Relaxed)
}

/// Non-zero while a [`progress_hook`] call is on the stack — i.e. while a non-yielding
/// `sim_block::block_on` spin is what is driving the clock. Read by [`crate::advance_to`] so a
/// budget exhaustion reached from inside a spin says so, instead of blaming the scenario for
/// running long.
///
/// Why this is needed at all: a spin that can never be satisfied does NOT report as a spin.
/// [`progress_hook`] returns [`Progress::Advanced`] on every clock advance, and
/// `sim_block::block_on` resets its stalled-poll counter on `Advanced`, so its wedge budget is
/// unreachable in any scenario where something keeps arming timers (audio arms one every
/// 2902us). The run instead dies in `advance_to` — pointing at the wrong thing.
///
/// A counter rather than a bool: the hook advances the clock and then pumps HP again, and a
/// future edit could nest hook calls.
static IN_SPIN: AtomicU64 = AtomicU64::new(0);

/// True while a non-yielding spin's progress hook is on the stack. See [`IN_SPIN`].
pub fn in_spin() -> bool {
    IN_SPIN.load(Ordering::Relaxed) != 0
}

/// Record a pend against the executor identified by `context`.
pub fn note_pend(context: *mut ()) {
    if context as usize == HP_TAG {
        HP_PENDED.store(true, Ordering::SeqCst);
    } else {
        MAIN_PENDED.store(true, Ordering::SeqCst);
    }
}

pub fn main_pended() -> bool {
    MAIN_PENDED.load(Ordering::SeqCst)
}

pub fn clear_main_pended() {
    MAIN_PENDED.store(false, Ordering::SeqCst);
}

/// Stash `HP_EXEC` and install the `sim_block` progress hook. Call once at start-up,
/// before any task is spawned.
pub fn init(hp: &'static raw::Executor) {
    // SAFETY: single-threaded; called once at start-up before anything is spawned or polled.
    unsafe { *core::ptr::addr_of_mut!(HP) = Some(hp) };
    crate::sim_block::set_progress_hook(progress_hook);
    // Generous but finite: a modeled transfer needs only a handful of pump/advance rounds,
    // so anything near this bound means genuinely wedged, not merely slow.
    crate::sim_block::set_spin_budget(10_000);
}

/// Poll `HP_EXEC` to quiescence. Returns whether it made any pend (i.e. did work).
///
/// # Reentrancy
///
/// Panics if called while an outer `pump_hp` call is already on the stack (via `IN_HP`).
/// This enforces the module doc's claim that `HP_EXEC` is never polled from within itself:
/// `raw::Executor::poll`'s own doc says calling it reentrantly on the SAME executor is UB
/// (the run queue is mutated non-reentrantly, and a task whose future is already
/// `&mut`-borrowed by the outer poll could be re-polled). The only way to hit this today
/// would be an `HP_EXEC`-resident task itself calling `sim_block::block_on` — which installs
/// this same `progress_hook` — while ITS OWN `hp.poll()` is already on the stack; not
/// reachable yet (Phase 0 hosts only `sim_latency::pump`, which never calls `block_on`), but
/// exactly the failure mode R5a Phase 1 introduces once `streaming_fill_task` (storage I/O,
/// the prime candidate for `block_on`) moves onto `HP_EXEC` too. A panic is more honest than
/// a silent `false`: silently returning would hide the bug this guard exists to catch.
pub fn pump_hp() -> bool {
    // SAFETY: see `init` — set once before use, read only on the single thread.
    let Some(hp) = (unsafe { *core::ptr::addr_of!(HP) }) else {
        return false;
    };
    assert!(
        !IN_HP.swap(true, Ordering::SeqCst),
        "preempt::pump_hp: reentrant HP_EXEC poll — a task running ON HP_EXEC triggered the \
         progress hook, which tried to poll HP_EXEC while HP_EXEC's own poll() was already on \
         the stack. See this function's and the module's doc comments."
    );
    let mut did_work = false;
    loop {
        // `swap`, not `store`: a pend that arrived before this call (e.g. `sim_latency::pump`
        // waking on its `Signal` right before `pump_hp` runs, then going straight to
        // `Pending` on `Timer::after` without raising a further pend) is real work this call
        // performs by polling it — `store(false)` would discard that fact and make the
        // common case under-report, biasing the underrun margin this harness measures.
        did_work |= HP_PENDED.swap(false, Ordering::SeqCst);
        // SAFETY: HP_EXEC is never polled from within itself — enforced by the `IN_HP` guard
        // above, not merely asserted in prose.
        unsafe { hp.poll() };
        if !HP_PENDED.load(Ordering::SeqCst) {
            break;
        }
        did_work = true;
    }
    IN_HP.store(false, Ordering::SeqCst);
    did_work
}

/// The `sim_block` progress hook: give the higher-priority executor a chance to run, and
/// if it had nothing to do, advance virtual time to the next scheduled deadline.
///
/// Clock advances made here go through `crate::advance_to` — the SAME helper the driver
/// loop's own advance step uses — so the watchdog mirrors, the virtual-time budget check,
/// and the timeline checksum stay correct regardless of which of the two call sites moved
/// the clock. Never advance the clock directly here.
///
/// Marks [`IN_SPIN`] for its whole duration — including the [`crate::advance_to`] call it
/// makes — so a budget exhaustion reached from within a spin is attributable to the spin.
pub fn progress_hook() -> Progress {
    IN_SPIN.fetch_add(1, Ordering::Relaxed);
    let r = progress_hook_inner();
    IN_SPIN.fetch_sub(1, Ordering::Relaxed);
    r
}

fn progress_hook_inner() -> Progress {
    // Counts the call, not just a successful outcome: `HOOK_INVOCATIONS`'s job is to prove
    // the hook mechanism itself ran, and it is invoked whether or not this particular call
    // found anything to do (see `Progress::Stalled` below) — a caller asserting it advanced
    // across a whole spin cares that the hook fired at all, not how many of those calls were
    // individually productive.
    HOOK_INVOCATIONS.fetch_add(1, Ordering::Relaxed);
    if pump_hp() {
        return Progress::Advanced;
    }
    let driver = clock::PeekableMockDriver::get();
    match driver.peek_next_deadline() {
        Some(next) => {
            let now = driver.now();
            if next <= now {
                // Cannot happen given `clock.rs`'s `peek_next_deadline` doc (it only ever
                // returns a deadline strictly greater than `now`), but if that invariant
                // ever changes, doing nothing here must not be reported as progress: an
                // unbounded spin must stay wedge-detectable via `sim_block`'s `stalled`
                // counter, not silently reset it.
                return Progress::Stalled;
            }
            crate::advance_to(driver, next);
            // Let the freshly-woken HP tasks (e.g. `sim_latency::pump`) actually run.
            pump_hp();
            Progress::Advanced
        }
        // No pending timer anywhere and HP is idle: nothing can make the future ready.
        None => Progress::Stalled,
    }
}
