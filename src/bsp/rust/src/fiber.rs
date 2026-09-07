//! A single cooperative **stackful fiber** for the long, synchronous C++ operations
//! that pause via the scheduler's `yield()` (song load, stem export, grid clip
//! create). On the decomposed Embassy BSP a synchronous `yield()` can't hand the
//! CPU back to the executor, so those operations froze it (hanging
//! song-load-while-playing). A fiber suspends the *whole* call stack at `yield()` —
//! regardless of depth or return values — so the C++ stays unchanged; the embassy
//! worker (app_task) resumes it when its predicate holds, running every other task
//! and I/O in between. This is the Loom/goroutine pattern.
//!
//! This module is the load-bearing primitive: the Cortex-A9 (AArch32) cooperative
//! context switch plus a `selftest`. The full worker (op queue, predicate wait, the
//! `yield`/`deluge_worker_*` C ABI) builds on top.
//!
//! The low-level switch is the only part that differs per target: on device
//! (`target_os = "none"`) it's the ARM `global_asm!` register-save switch below;
//! on host it's [`corosensei`](https://docs.rs/corosensei)'s stackful
//! `Coroutine`/`Yielder`, chosen over `ucontext`/`swapcontext` because it has
//! built-in sanitizer support (needed for ThreadSanitizer) instead of
//! requiring hand-written `__tsan_switch_to_fiber` annotations around every
//! switch. Both sides present the same `start`/`resume`/`yield_now`/`on_fiber`
//! contract to the portable layer below.
//!
//! Concurrency: single fiber, single-threaded executor. The fiber and the embassy
//! worker task never run at once (a switch hands control between them); the only
//! other context is IRQs, which save/restore everything they touch (incl. the VFP
//! save in the HAL `_irq_handler`), so an IRQ may preempt the fiber harmlessly.
#![allow(dead_code)] // the driver that calls start/resume/yield lands in the next step

use core::ffi::c_void;
use core::future::Future;
use core::pin::pin;
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use core::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;

/// Saved callee-saved register context for a cooperative switch. A cooperative
/// switch happens at a function-call boundary, so only callee-saved state must be
/// preserved: core `r4–r11`, `sp`, `lr`, VFP `d8–d15`, and `fpscr`. Field order is
/// the exact order [`fiber_switch`] stores/loads them (sequential, via writeback) —
/// do not reorder without updating the assembly. `repr(C)` + `align(8)` so the VFP
/// block is 8-byte aligned for `vstm`/`vldm`.
#[cfg(target_os = "none")]
#[repr(C, align(8))]
struct Ctx {
    core: [u32; 8], // r4-r11
    sp: u32,        // r13
    lr: u32,        // r14 (resume/return address; thumb bit preserved for interworking)
    vfp: [u32; 16], // d8-d15
    fpscr: u32,
}

#[cfg(target_os = "none")]
impl Ctx {
    const fn zeroed() -> Self {
        Ctx {
            core: [0; 8],
            sp: 0,
            lr: 0,
            vfp: [0; 16],
            fpscr: 0,
        }
    }
}

// Cooperative context switch: save the current callee-saved context to `*save`,
// restore `*restore`, and return into the restored `lr`. ARM-encoded; reached via
// interworking `bl`/`bx`, so it is safe whether the surrounding Rust is ARM or
// Thumb (it preserves each context's `lr` thumb bit verbatim).
#[cfg(target_os = "none")]
core::arch::global_asm!(
    r#"
    .section .text.fiber_switch, "ax"
    .global fiber_switch
    .type fiber_switch, %function
    .arm
fiber_switch:
    /* r0 = save (*mut Ctx), r1 = restore (*const Ctx). r2 = scratch (caller-saved). */
    stm     r0!, {{r4-r11}}      /* save core r4-r11 */
    str     sp,  [r0], #4        /* save sp          */
    str     lr,  [r0], #4        /* save lr          */
    vstm    r0!, {{d8-d15}}      /* save VFP d8-d15  */
    vmrs    r2,  fpscr
    str     r2,  [r0]            /* save fpscr       */

    ldm     r1!, {{r4-r11}}      /* restore core     */
    ldr     sp,  [r1], #4        /* restore sp       */
    ldr     lr,  [r1], #4        /* restore lr       */
    vldm    r1!, {{d8-d15}}      /* restore VFP      */
    ldr     r2,  [r1]
    vmsr    fpscr, r2            /* restore fpscr    */
    bx      lr                   /* resume restored context */
    .size fiber_switch, . - fiber_switch
"#
);

#[cfg(target_os = "none")]
unsafe extern "C" {
    fn fiber_switch(save: *mut Ctx, restore: *const Ctx);
}

/// Worker fiber stack size. Sized for the deepest operation (song load is deep).
/// On device this backs [`WORKER_STACK`] (SDRAM); on host it sizes the
/// `corosensei` coroutine's stack (kept the same for parity, though it's a
/// regular allocation there, not a fixed static).
const WORKER_STACK_SIZE: usize = 64 * 1024;

/// Worker fiber stack. In SDRAM (`.sdram_bss`, zeroed at boot) to spare the tight
/// internal SRAM. Device-only: on host `corosensei` owns (and allocates) the
/// coroutine's stack itself.
#[cfg(target_os = "none")]
#[unsafe(link_section = ".sdram_bss")]
static mut WORKER_STACK: [u8; WORKER_STACK_SIZE] = [0; WORKER_STACK_SIZE];

// Saved contexts: MAIN = the embassy worker task; FIBER = the operation.
#[cfg(target_os = "none")]
static mut MAIN_CTX: Ctx = Ctx::zeroed();
#[cfg(target_os = "none")]
static mut FIBER_CTX: Ctx = Ctx::zeroed();

/// The operation currently assigned to the fiber, consumed by [`trampoline`] on
/// first entry. `extern "C"` so C++ dispatch can hand over a function + context.
/// Device-only: the host switch layer captures `f`/`ctx` directly in the
/// coroutine's closure instead (see the host `start` below).
#[cfg(target_os = "none")]
static mut CURRENT_FN: Option<(extern "C" fn(*mut c_void), *mut c_void)> = None;
/// Set by [`trampoline`] when the operation returns; observed by the worker.
/// Device-only: the host switch layer reads completion off `CoroutineResult`
/// instead (see the host `resume` below).
#[cfg(target_os = "none")]
static mut FIBER_DONE: bool = false;
/// True while control is executing on the fiber. `yield()` reads this to decide
/// whether to suspend the fiber (on it) or fall back to a busy-wait (off it, e.g.
/// the legacy C-HAL SPI-flash/USB storage waits).
static ON_FIBER: AtomicBool = AtomicBool::new(false);

/// Is the caller running on the worker fiber? (For the `yield()` implementation.)
pub fn on_fiber() -> bool {
    ON_FIBER.load(Ordering::Relaxed)
}

/// Latched `true` the first time [`worker_poll`] runs — i.e. the worker/owner is
/// live. Monotonic (never reset), so a plain `Relaxed` load/store is fine: this
/// is a one-way latch, not a synchronization point guarding shared data. Exists
/// so the `storage-owner-audit` diskio asserts (`sd.rs`) can tell apart the
/// boot-time FatFS mount — which runs synchronously in `deluge_boot()` *before*
/// the first `worker_poll()`, with no owner yet to run on — from a genuine
/// off-owner transfer at runtime, after the owner exists. See `sd.rs`'s asserts
/// for the full rationale.
static WORKER_STARTED: AtomicBool = AtomicBool::new(false);

/// Has the worker pump run at least once yet? `false` only during the narrow
/// pre-owner boot window (the synchronous `deluge_boot()` FatFS mount); `true`
/// for the remainder of the process's life once `app_task`/`host_app_task`
/// starts polling `worker_poll()`.
pub fn worker_started() -> bool {
    WORKER_STARTED.load(Ordering::Relaxed)
}

// ---------------------------------------------------------------------------
// Low-level switch: device (ARM `fiber_switch`/`Ctx`, above).
// ---------------------------------------------------------------------------

/// 8-byte-aligned top of the worker stack (stacks grow down).
#[cfg(target_os = "none")]
fn worker_stack_top() -> u32 {
    let base = core::ptr::addr_of!(WORKER_STACK) as u32;
    (base + WORKER_STACK_SIZE as u32) & !7
}

/// Bounds of the worker fiber's stack, `[start, end)`, for the fault reporter.
///
/// The fiber runs application code on its own stack, in a different region from the program stack.
/// The crash reporter's pointer walk only walks a stack it was told about, so without this a fault
/// on the fiber reported one address and no call chain — and storage, sample-preview and browser
/// operations all run here, which makes those the faults most worth reporting.
#[cfg(target_os = "none")]
pub fn worker_stack_bounds() -> (usize, usize) {
    let base = core::ptr::addr_of!(WORKER_STACK) as usize;
    (base, base + WORKER_STACK_SIZE)
}

/// First-entry trampoline: runs the assigned operation, then marks the fiber done
/// and parks by switching back to main. Never returns (it sits at the base of the
/// worker stack — returning would pop garbage).
#[cfg(target_os = "none")]
extern "C" fn trampoline() -> ! {
    // SAFETY: single fiber; CURRENT_FN was set by `start` before the switch in.
    let job = unsafe { core::ptr::addr_of_mut!(CURRENT_FN).read() };
    unsafe { core::ptr::addr_of_mut!(CURRENT_FN).write(None) };
    if let Some((f, ctx)) = job {
        f(ctx);
    }
    unsafe { core::ptr::addr_of_mut!(FIBER_DONE).write(true) };
    // Operation finished — hand control back to main, and keep doing so if ever
    // (erroneously) resumed before a new operation re-inits the fiber.
    loop {
        switch_to_main();
    }
}

/// Switch from the fiber back to main (called on the fiber — `yield`/completion).
#[cfg(target_os = "none")]
fn switch_to_main() {
    // SAFETY: only called while executing on the fiber; both contexts are valid.
    unsafe {
        fiber_switch(
            core::ptr::addr_of_mut!(FIBER_CTX),
            core::ptr::addr_of!(MAIN_CTX),
        )
    };
}

/// Start `f(ctx)` on the fiber (must be idle). Runs it until it yields or
/// completes, then returns to the caller (the worker). Returns `true` if the
/// operation completed, `false` if it yielded and is now suspended.
#[cfg(target_os = "none")]
pub fn start(f: extern "C" fn(*mut c_void), ctx: *mut c_void) -> bool {
    // SAFETY: single-threaded; the fiber is idle (caller's contract).
    unsafe {
        core::ptr::addr_of_mut!(CURRENT_FN).write(Some((f, ctx)));
        core::ptr::addr_of_mut!(FIBER_DONE).write(false);
        // Initialise FIBER_CTX so the first switch enters `trampoline` on a fresh
        // worker stack. Other callee-saved fields are don't-care on entry.
        let fc = &mut *core::ptr::addr_of_mut!(FIBER_CTX);
        *fc = Ctx::zeroed();
        fc.sp = worker_stack_top();
        fc.lr = trampoline as *const () as usize as u32; // raw fn address (thumb bit preserved)
    }
    resume()
}

/// Resume the suspended fiber. Returns `true` if it completed, `false` if it
/// yielded again.
#[cfg(target_os = "none")]
pub fn resume() -> bool {
    ON_FIBER.store(true, Ordering::Relaxed);
    // SAFETY: switches into the fiber; returns here when it yields/completes.
    unsafe {
        fiber_switch(
            core::ptr::addr_of_mut!(MAIN_CTX),
            core::ptr::addr_of!(FIBER_CTX),
        )
    };
    ON_FIBER.store(false, Ordering::Relaxed);
    unsafe { core::ptr::addr_of!(FIBER_DONE).read() }
}

/// Yield from the fiber back to the worker. Call only while [`on_fiber`] is true.
#[cfg(target_os = "none")]
pub fn yield_now() {
    switch_to_main();
}

// ---------------------------------------------------------------------------
// Low-level switch: host (`corosensei` stackful coroutine). Same
// `start`/`resume`/`yield_now` contract as the device impl above: `start`/
// `resume` return `true` if the operation ran to completion, `false` if it
// suspended again; `yield_now` is called from arbitrary depth inside the
// running operation.
//
// `corosensei::Coroutine<Input, Yield, Return>` is asymmetric (a coroutine
// resumed by its parent, suspending itself via a `Yielder`), unlike the
// device's symmetric `fiber_switch(save, restore)`, so the worker fiber here
// is a single `Coroutine<(), (), ()>` held in a static, rebuilt fresh by each
// `start()` (mirroring the device's re-entry into `trampoline` on a fresh
// stack). `yield_now()` is called deep inside the C++ op, not at the top of
// the coroutine body, so — exactly like `ON_FIBER` above — the currently
// running coroutine's `&Yielder` is stashed in a static for it to reach.
// ---------------------------------------------------------------------------

#[cfg(not(target_os = "none"))]
use corosensei::{Coroutine, CoroutineResult, Yielder, stack::DefaultStack};

/// The worker fiber. `None` when idle (never started, or the last operation ran
/// to completion). Rebuilt by each `start()`.
#[cfg(not(target_os = "none"))]
static mut WORKER: Option<Coroutine<(), (), ()>> = None;

/// The running coroutine's `Yielder`, stashed for [`yield_now`] to reach from
/// arbitrary call depth (`yield_now` isn't called at the coroutine's top level).
/// Valid only while [`on_fiber`] is true. Raw pointer (not a reference) since it
/// must outlive the borrow that created it across the `resume`/`suspend` switch.
#[cfg(not(target_os = "none"))]
static mut CURRENT_YIELDER: *const Yielder<(), ()> = core::ptr::null();

/// Start `f(ctx)` on the fiber (must be idle). Runs it until it yields or
/// completes, then returns to the caller (the worker). Returns `true` if the
/// operation completed, `false` if it yielded and is now suspended.
#[cfg(not(target_os = "none"))]
pub fn start(f: extern "C" fn(*mut c_void), ctx: *mut c_void) -> bool {
    // `f` (a fn pointer) and `ctx` (a raw pointer, no lifetime parameter) are
    // both trivially `'static`, satisfying `Coroutine::with_stack`'s bound.
    // SAFETY: single-threaded; the fiber is idle (caller's contract), so no
    // other reference to WORKER/CURRENT_YIELDER is live.
    let stack =
        DefaultStack::new(WORKER_STACK_SIZE).expect("fiber: failed to allocate worker stack");
    let coro = Coroutine::with_stack(stack, move |yielder: &Yielder<(), ()>, ()| {
        unsafe {
            core::ptr::addr_of_mut!(CURRENT_YIELDER).write(yielder as *const Yielder<(), ()>)
        };
        f(ctx);
    });
    // Drop-respecting assignment (not `.write()`, which would leak an old `Some`
    // coroutine's stack without running its `Drop`): the caller's contract is that
    // the fiber is idle here, so WORKER is `None` and there's nothing to drop
    // today, but this stays parity-correct with `resume()`'s `Return` arm below.
    unsafe { *core::ptr::addr_of_mut!(WORKER) = Some(coro) };
    resume()
}

/// Resume the suspended fiber. Returns `true` if it completed, `false` if it
/// yielded again.
#[cfg(not(target_os = "none"))]
pub fn resume() -> bool {
    ON_FIBER.store(true, Ordering::Relaxed);
    // SAFETY: single-threaded; WORKER was set by `start` and not yet completed
    // (caller's contract — resume is only called while an op is suspended).
    let result = unsafe {
        let worker = (*core::ptr::addr_of_mut!(WORKER))
            .as_mut()
            .expect("fiber: resume() with no active worker");
        worker.resume(())
    };
    ON_FIBER.store(false, Ordering::Relaxed);
    match result {
        CoroutineResult::Yield(()) => false,
        CoroutineResult::Return(()) => {
            // Drop-respecting assignment (not `.write(None)`, which overwrites
            // the old `Some(finished_coroutine)` without running its `Drop` —
            // silently leaking the coroutine's stack, ~4 KiB, on every completed
            // op) so a stale WORKER can't be mistakenly resumed again; the next
            // op rebuilds it.
            unsafe { *core::ptr::addr_of_mut!(WORKER) = None };
            // The stashed Yielder pointer is dangling now that the coroutine
            // (and its stack) is gone — null it out so a wayward yield_now()
            // call between here and the next start() fails the null-check
            // instead of dereferencing freed memory.
            unsafe { core::ptr::addr_of_mut!(CURRENT_YIELDER).write(core::ptr::null()) };
            true
        }
    }
}

/// Yield from the fiber back to the worker. Call only while [`on_fiber`] is true.
#[cfg(not(target_os = "none"))]
pub fn yield_now() {
    // SAFETY: only called while executing on the fiber (on_fiber() == true), so
    // CURRENT_YIELDER was stashed by the running coroutine's `start` closure and
    // is still valid (its `resume()` call is still on the stack).
    unsafe {
        let yielder = core::ptr::addr_of!(CURRENT_YIELDER).read();
        debug_assert!(!yielder.is_null(), "yield_now() called while not on_fiber");
        (*yielder).suspend(());
    }
}

// ---------------------------------------------------------------------------
// Worker driver: a serialized queue of operations run on the fiber, plus the
// predicate-wait that backs the scheduler's yield(). Pumped by the embassy
// app_task via `worker_poll()`; ops are submitted by C++ dispatch via
// `deluge_worker_run`. While an op is suspended on a predicate the executor runs
// every other task + I/O, so the predicate (cluster drain, button release, ...)
// flips and the op resumes.
// ---------------------------------------------------------------------------

#[cfg(target_os = "none")]
use crate::sys::RunCondition;
/// Host stand-in for the bindgen `RunCondition` typedef (`storage_wait.h`:
/// `typedef bool (*RunCondition)();`). `mod sys` (the bindgen output) is
/// device-only — no C++ ABI is linked on host — so mirror the C type's
/// shape directly here rather than depending on it. Same shape bindgen would
/// produce for this typedef; if a shared host-ABI `sys` module lands later this
/// can be replaced with `crate::sys::RunCondition` again.
#[cfg(not(target_os = "none"))]
pub type RunCondition = Option<unsafe extern "C" fn() -> bool>;

/// Pending operations (serialized — these are user actions, at most one active).
/// `is_sd` is the sd-routine bit: true ops hold `SD_ROUTINE_HELD` from enqueue
/// to completion (see `deluge_worker_run_sd_routine`).
type Job = (extern "C" fn(*mut c_void), *mut c_void, bool);
const QUEUE_CAP: usize = 4;

/// The ring's storage: each occupied slot pairs a `Job` with the sequence
/// number it was enqueued at (`Q_NEXT_SEQ`, monotonic). A job is written into
/// whichever slot is free at enqueue time, so the slot index carries no order;
/// the per-slot sequence gives `dequeue` the enqueue order to pick the oldest
/// job (plain FIFO — see `dequeue` below). A rotating head/tail ring would
/// serve this FIFO with less scanning; the free-slot+sequence form is a
/// leftover from the retired HIGH-priority tier (which needed out-of-order
/// removal) and could be simplified later.
static mut QUEUE: [Option<(Job, u32)>; QUEUE_CAP] = [None; QUEUE_CAP];
static mut Q_COUNT: usize = 0;
/// Monotonic (wrapping) insertion counter, stamped onto each enqueued slot.
/// `dequeue` resets this to 0 whenever the ring drains to empty (see there),
/// which bounds the span of seqs any two *resident* jobs can carry to at most
/// `QUEUE_CAP` (4) — so a wrong ordering decision at the wrap would require
/// ~4 billion enqueues within a single stretch where the ring never once goes
/// empty, which cannot happen (the queue drains far faster than that).
static mut Q_NEXT_SEQ: u32 = 0;

/// An operation is on the fiber (running or suspended) — distinct from idle.
static FIBER_BUSY: AtomicBool = AtomicBool::new(false);

/// Count of SD-routine-class ops in flight (enqueued but not yet completed).
/// Incremented synchronously by `deluge_worker_run_sd_routine` at enqueue,
/// decremented when the op completes (`complete_active_op`). Read by the
/// scheduler's RESOURCE_SD_ROUTINE gate (scheduler.rs) to hold off tasks that
/// would free an object an in-flight op is mid-way through (the recorder, freed
/// by discardRecorder). Synchronous so the hold engages before the enqueuing task
/// returns — closing the window between enqueue and the pump starting the op.
/// Currently inert until cooperative yielding is enabled on this path, but
/// correct-by-construction for when it is.
static SD_ROUTINE_HELD: AtomicU32 = AtomicU32::new(0);

/// The sd-routine bit of the op currently on the fiber (running or suspended),
/// so `complete_active_op` knows whether to release a hold. Serialized queue
/// (one active op) makes this exact.
static ACTIVE_IS_SD_ROUTINE: AtomicBool = AtomicBool::new(false);

/// True while any SD-routine-class op is in flight (enqueued or running).
pub fn sd_routine_held() -> bool {
    SD_ROUTINE_HELD.load(Ordering::Acquire) > 0
}

/// Wakes the worker pump (`app_task`). Raised when an op is submitted, when a task
/// runner makes progress while an op is suspended (so its predicate is re-checked),
/// and by the [`block_on_fiber`] waker when an awaited future (e.g. an SD transfer)
/// completes. Lets `app_task` sleep instead of polling on a fixed tick.
pub static WORKER_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// Wake the worker unconditionally (e.g. a new op was submitted).
pub fn wake() {
    WORKER_WAKE.signal(());
}

/// Worker-pump state, readable over the debug probe when the device wedges.
///
/// A device found parked in the executor's idle `wfe()` is waiting for a wakeup that never came, and
/// the question is always the same: was the pump still being scheduled, and did it think it had work?
/// The statics that answer it (`FIBER_BUSY`, `Q_COUNT`, `WAIT_PRED`, `WAIT_DEADLINE_US`) do not
/// survive optimisation — LTO folds them into their access sites, leaving nothing in the symbol
/// table to read. So the answers are mirrored here, in one `#[no_mangle]` struct with a stable
/// symbol, updated from [`worker_poll`] and [`enqueue`].
///
/// Reading it: halt the target and `p DELUGE_FIBER_DEBUG`, twice, a second apart.
/// - `polls` advancing ⇒ the pump is alive; the stall is in something it is waiting on.
/// - `polls` frozen with `fiber_busy == 1` ⇒ a suspended op the pump stopped re-polling (its 8 ms
///   fallback in `app_task` should make this impossible — if you see it, that timer is not arming).
/// - `polls` frozen with `fiber_busy == 0` and `q_count == 0` ⇒ the pump is idle-asleep on
///   `WORKER_WAKE` with no fallback timer at all, and whoever is stalled never submitted an op (or
///   submitted without waking). This is the shape a missed signal takes.
/// - `submits > resumes + completed ops` ⇒ work was queued that the pump never started.
///
/// Diagnostic only: nothing reads this on the device, and the cost is a handful of stores per poll.
#[repr(C)]
pub struct FiberDebug {
    /// Bumped on every [`worker_poll`] entry. Frozen ⇒ the pump is not being scheduled.
    pub polls: u32,
    /// Bumped whenever [`worker_poll`] resumes a suspended op.
    pub resumes: u32,
    /// Bumped whenever an op is enqueued ([`enqueue`]).
    pub submits: u32,
    /// Bumped whenever an enqueue was DROPPED because the ring was full — the op never ran.
    pub submits_dropped: u32,
    /// `FIBER_BUSY` as observed at the last poll.
    pub fiber_busy: u8,
    /// Queue depth as observed at the last poll.
    pub q_count: u8,
    _pad: [u8; 2],
    /// `WAIT_PRED` at the last poll (0 = "resume on the next poll", what `block_on_fiber` sets).
    pub wait_pred: u32,
    /// `WAIT_DEADLINE_US` at the last poll (0 = wait forever).
    pub wait_deadline_us: u64,
    /// `now_us()` at the last poll. With `polls`, shows whether the pump stopped and when.
    pub last_poll_us: u64,
}

#[unsafe(no_mangle)]
pub static mut DELUGE_FIBER_DEBUG: FiberDebug = FiberDebug {
    polls: 0,
    resumes: 0,
    submits: 0,
    submits_dropped: 0,
    fiber_busy: 0,
    q_count: 0,
    _pad: [0; 2],
    wait_pred: 0,
    wait_deadline_us: 0,
    last_poll_us: 0,
};

/// Bump one `u32` counter in [`DELUGE_FIBER_DEBUG`]. Saturating so a wrap can't be mistaken for a
/// stalled counter when reading a long-running target.
fn dbg_bump(field: impl FnOnce(&mut FiberDebug) -> &mut u32) {
    // SAFETY: single-threaded (the pump and the enqueue path both run on the one executor); this
    // struct is diagnostic-only and nothing else reads it.
    unsafe {
        let d = &mut *core::ptr::addr_of_mut!(DELUGE_FIBER_DEBUG);
        let f = field(d);
        *f = f.saturating_add(1);
    }
}

/// Wake the worker only if an op is currently on the fiber — i.e. something might
/// be waiting on the progress the caller just made. Called by task runners after
/// running a handle; a no-op (no spurious wake) when the worker is idle.
pub fn wake_if_busy() {
    if FIBER_BUSY.load(Ordering::Relaxed) {
        WORKER_WAKE.signal(());
    }
}

/// What the suspended op is waiting on. `WAIT_PRED` is a `RunCondition` as a usize
/// (0 = no predicate => ready next poll). `WAIT_MET` is handed back to `yield_until`
/// on resume (predicate met vs. timed out).
static mut WAIT_PRED: usize = 0;
static mut WAIT_DEADLINE_US: u64 = 0; // 0 = wait forever
static mut WAIT_MET: bool = false;

/// Monotonic microseconds since boot.
///
/// `pub(crate)` because `scheduler.rs`'s timeout-bearing wrappers read it too. When this file is
/// retired the clock needs a new home that is not a fiber concern -- note that the golden/lens
/// harnesses already own the module name `clock`, so it cannot simply move there.
pub(crate) fn now_us() -> u64 {
    embassy_time::Instant::now().as_micros()
}

/// Enqueue an operation on the worker ring, taking the SD-routine hold when
/// `is_sd` (synchronously, so it engages before the caller returns). Returns
/// whether it was accepted; a dropped enqueue (queue full) takes no hold.
fn enqueue(f: extern "C" fn(*mut c_void), ctx: *mut c_void, is_sd: bool) -> bool {
    // SAFETY: single-threaded; enqueue only (no switch here).
    let enqueued = unsafe {
        if Q_COUNT < QUEUE_CAP {
            let queue = core::ptr::addr_of_mut!(QUEUE).cast::<Option<(Job, u32)>>();
            let mut free: Option<usize> = None;
            for i in 0..QUEUE_CAP {
                if queue.add(i).read().is_none() {
                    free = Some(i);
                    break;
                }
            }
            let idx = free.expect("Q_COUNT < QUEUE_CAP implies a free slot");
            let seq = Q_NEXT_SEQ;
            Q_NEXT_SEQ = Q_NEXT_SEQ.wrapping_add(1);
            queue.add(idx).write(Some(((f, ctx, is_sd), seq)));
            Q_COUNT += 1;
            if is_sd {
                // Take the hold synchronously, before returning, so a RESOURCE_SD_ROUTINE
                // task can't slip in between this enqueue and the pump running the op.
                SD_ROUTINE_HELD.fetch_add(1, Ordering::AcqRel);
            }
            true
        } else {
            // Queue full — dropped, the op will NOT run. The caller learns via the
            // false return (e.g. the storage Coalescer clears its single-flight guard
            // so a later request can retry rather than wedging forever).
            false
        }
    };
    dbg_bump(|d| {
        if enqueued {
            &mut d.submits
        } else {
            &mut d.submits_dropped
        }
    });
    // Wake the pump so the op starts promptly (it may be idle-asleep).
    wake();
    enqueued
}

/// Submit an operation to run on the worker fiber (C++ dispatch boundary). Runs
/// serialized after any already-queued operations once the worker is pumped.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_worker_run(f: extern "C" fn(*mut c_void), ctx: *mut c_void) -> bool {
    enqueue(f, ctx, false)
}

/// SD-routine-class submission (see include/libdeluge/worker.h). Same enqueue as
/// `deluge_worker_run`, but takes an SD-routine hold synchronously so it is
/// visible before this returns, and holds it until the op completes — keeping
/// RESOURCE_SD_ROUTINE scheduler tasks (discardRecorder) off for the op's whole
/// in-flight window. On a dropped enqueue (queue full) NO hold is taken.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_worker_run_sd_routine(
    f: extern "C" fn(*mut c_void),
    ctx: *mut c_void,
) -> bool {
    enqueue(f, ctx, true)
}

/// C-ABI: is the current context the worker fiber? Backs `Owner::on_owner()`
/// (see include/libdeluge/worker.h). `deluge_worker_run` (above) runs ops on
/// this stackful fiber; [`on_fiber`] is true exactly while executing inside
/// such an op.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_worker_on_worker() -> bool {
    on_fiber()
}

/// C-ABI: has the worker pump run at least once yet? Backs `include/libdeluge/worker.h`'s
/// `deluge_worker_started` -- a C++ caller's way to draw the same "boot-time, before the owner
/// exists" vs. "runtime, after it does" distinction `worker_started()` already gives `sd.rs`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_worker_started() -> bool {
    worker_started()
}

/// Dequeue the next op to run: the oldest (lowest-sequence) queued job — plain
/// FIFO. `QUEUE_CAP` is small (4), so a linear scan per dequeue is cheap and
/// keeps the ring itself a plain fixed array.
fn dequeue() -> Option<Job> {
    // SAFETY: single-threaded access to the ring.
    unsafe {
        if Q_COUNT == 0 {
            return None;
        }
        let queue = core::ptr::addr_of_mut!(QUEUE).cast::<Option<(Job, u32)>>();
        let mut best: Option<(usize, u32)> = None; // (slot index, seq)
        for i in 0..QUEUE_CAP {
            if let Some((_job, seq)) = queue.add(i).read() {
                if best.is_none_or(|(_, best_seq)| seq < best_seq) {
                    best = Some((i, seq));
                }
            }
        }
        let (idx, _) = best.expect("Q_COUNT > 0 implies at least one occupied slot");
        let (job, _seq) = queue
            .add(idx)
            .replace(None)
            .expect("scanned slot was occupied");
        Q_COUNT -= 1;
        if Q_COUNT == 0 {
            // Ring just drained: reset the seq counter so the next enqueue
            // stretch starts a fresh span (see Q_NEXT_SEQ doc comment).
            Q_NEXT_SEQ = 0;
        }
        Some(job)
    }
}

fn queue_nonempty() -> bool {
    unsafe { core::ptr::addr_of!(Q_COUNT).read() > 0 }
}

/// Pump the worker. Starts the next queued op (if idle) or resumes the suspended
/// op once its predicate holds / times out. Returns true while work remains.
/// Called from the embassy `app_task` on a ~1 ms ticker.
pub fn worker_poll() -> bool {
    // Latch on first entry: the owner is live from here on (see WORKER_STARTED's
    // doc comment). Relaxed store is fine — this is a monotonic latch, not a
    // handoff of other state.
    WORKER_STARTED.store(true, Ordering::Relaxed);
    // Diagnostic snapshot — see DELUGE_FIBER_DEBUG. Taken before anything else so a target halted
    // mid-poll still shows the state this pass started from.
    // SAFETY: single-threaded; WAIT_PRED/WAIT_DEADLINE_US/Q_COUNT are only written on the fiber and
    // in `enqueue`, neither of which can be running concurrently with this.
    unsafe {
        let d = &mut *core::ptr::addr_of_mut!(DELUGE_FIBER_DEBUG);
        d.polls = d.polls.saturating_add(1);
        d.fiber_busy = FIBER_BUSY.load(Ordering::Relaxed) as u8;
        d.q_count = Q_COUNT as u8;
        d.wait_pred = core::ptr::addr_of!(WAIT_PRED).read() as u32;
        d.wait_deadline_us = core::ptr::addr_of!(WAIT_DEADLINE_US).read();
        d.last_poll_us = now_us();
    }
    if !FIBER_BUSY.load(Ordering::Relaxed) {
        let Some((f, ctx, is_sd)) = dequeue() else {
            return false;
        };
        ACTIVE_IS_SD_ROUTINE.store(is_sd, Ordering::Relaxed);
        FIBER_BUSY.store(true, Ordering::Relaxed);
        let completed = start(f, ctx);
        if completed {
            // Completed without ever yielding.
            complete_active_op();
        }
        return FIBER_BUSY.load(Ordering::Relaxed) || queue_nonempty();
    }

    // An op is suspended on a predicate — resume it once satisfied / timed out.
    // SAFETY: single-threaded; these are only written by yield_until (on the
    // fiber) and read here, never concurrently.
    let pred_word = unsafe { core::ptr::addr_of!(WAIT_PRED).read() };
    let met = if pred_word == 0 {
        true // no predicate: resume on the next poll
    } else {
        let pred: RunCondition = unsafe { core::mem::transmute::<usize, RunCondition>(pred_word) };
        pred.map_or(true, |p| unsafe { p() })
    };
    let deadline = unsafe { core::ptr::addr_of!(WAIT_DEADLINE_US).read() };
    let timed_out = deadline != 0 && now_us() >= deadline;
    if met || timed_out {
        unsafe { core::ptr::addr_of_mut!(WAIT_MET).write(met) };
        dbg_bump(|d| &mut d.resumes);
        let completed = resume();
        if completed {
            complete_active_op();
        }
    }
    FIBER_BUSY.load(Ordering::Relaxed) || queue_nonempty()
}

/// End-of-op cleanup: clear busy and release an SD-routine hold if this op held
/// one. Called at both completion points in `worker_poll` (ran-to-completion on
/// first start, and resumed-to-completion after a yield).
fn complete_active_op() {
    if ACTIVE_IS_SD_ROUTINE.swap(false, Ordering::AcqRel) {
        SD_ROUTINE_HELD.fetch_sub(1, Ordering::AcqRel);
    }
    FIBER_BUSY.store(false, Ordering::Relaxed);
}

/// Suspend the current operation until `until` holds (or `timeout_us` elapses).
/// Called from the scheduler's `yield()` family while [`on_fiber`] is true; returns
/// whether the predicate was met (vs. timed out). Switches to the worker; the
/// executor runs everything else until [`worker_poll`] resumes this op.
pub fn yield_until(until: RunCondition, timeout_us: Option<u64>) -> bool {
    // SAFETY: only called on the fiber (single context); written here, read by poll.
    unsafe {
        core::ptr::addr_of_mut!(WAIT_PRED).write(until.map_or(0, |f| f as usize));
        core::ptr::addr_of_mut!(WAIT_DEADLINE_US).write(timeout_us.map_or(0, |t| now_us() + t));
        core::ptr::addr_of_mut!(WAIT_MET).write(false);
    }
    yield_now(); // switch to the worker; returns here when poll resumes us
    unsafe { core::ptr::addr_of!(WAIT_MET).read() }
}

// A Waker whose wake raises WORKER_WAKE. Used by `block_on_fiber` so an awaited
// future's completion (e.g. the SD-transfer IRQ) wakes the pump, which re-polls
// the suspended fiber. Data-less: all state is the global signal + a static vtable.
static WORKER_WAKER_VTABLE: RawWakerVTable = RawWakerVTable::new(
    |_| RawWaker::new(core::ptr::null(), &WORKER_WAKER_VTABLE), // clone
    |_| WORKER_WAKE.signal(()),                                 // wake
    |_| WORKER_WAKE.signal(()),                                 // wake_by_ref
    |_| {},                                                     // drop
);

fn worker_waker() -> Waker {
    // SAFETY: the vtable is 'static and its fns only touch the 'static WORKER_WAKE.
    unsafe { Waker::from_raw(RawWaker::new(core::ptr::null(), &WORKER_WAKER_VTABLE)) }
}

/// Drive `fut` to completion on the worker fiber WITHOUT parking the executor:
/// poll it, and on `Pending` suspend the fiber (`yield_now`) so the executor runs
/// everything else; `worker_poll` resumes us — promptly when the future's waker
/// fires WORKER_WAKE (its completion IRQ), or on the pump's coarse fallback. The
/// fiber-aware analogue of `embassy_futures::block_on`, for I/O reached from a
/// worker op (SD transfers, `Timer` delays). Only valid while [`on_fiber`].
pub fn block_on_fiber<F: Future>(fut: F) -> F::Output {
    let mut fut = pin!(fut);
    let waker = worker_waker();
    let mut cx = Context::from_waker(&waker);
    loop {
        match fut.as_mut().poll(&mut cx) {
            Poll::Ready(v) => return v,
            Poll::Pending => {
                // No predicate: worker_poll resumes us on the next wake, and we
                // re-poll (the future re-checks its own readiness).
                // SAFETY: on the fiber; written here, read by worker_poll.
                unsafe {
                    core::ptr::addr_of_mut!(WAIT_PRED).write(0);
                    core::ptr::addr_of_mut!(WAIT_DEADLINE_US).write(0);
                }
                yield_now();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Self-test — validates the context switch in isolation: main -> fiber (start),
// fiber yields back, main resumes, fiber completes. Logs each step so a hardware
// bring-up shows the round trip. Returns true on the expected sequence.
// ---------------------------------------------------------------------------

static mut SELFTEST_STAGE: u32 = 0;

extern "C" fn selftest_body(_ctx: *mut c_void) {
    // Stage 1: we're on the fiber stack.
    unsafe { core::ptr::addr_of_mut!(SELFTEST_STAGE).write(1) };
    log::info!("fiber selftest: on fiber stack (stage 1), yielding");
    yield_now();
    // Stage 2: resumed after the yield.
    unsafe { core::ptr::addr_of_mut!(SELFTEST_STAGE).write(2) };
    log::info!("fiber selftest: resumed (stage 2), returning");
}

/// Exercise the full switch machinery once. Safe to call once at boot before the
/// worker is in use.
pub fn selftest() -> bool {
    log::info!("fiber selftest: starting");
    let done_first = start(selftest_body, core::ptr::null_mut());
    let stage_after_start = unsafe { core::ptr::addr_of!(SELFTEST_STAGE).read() };
    // Expect: ran to the yield (stage 1), not yet done.
    if done_first || stage_after_start != 1 {
        log::error!(
            "fiber selftest: FAILED at start (done={}, stage={})",
            done_first,
            stage_after_start
        );
        return false;
    }
    let done_second = resume();
    let stage_final = unsafe { core::ptr::addr_of!(SELFTEST_STAGE).read() };
    if !done_second || stage_final != 2 {
        log::error!(
            "fiber selftest: FAILED at resume (done={}, stage={})",
            done_second,
            stage_final
        );
        return false;
    }
    log::info!("fiber selftest: PASSED (round trip + yield/resume OK)");
    true
}
