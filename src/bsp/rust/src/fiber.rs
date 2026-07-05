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
//! Concurrency: single fiber, single-threaded executor. The fiber and the embassy
//! worker task never run at once (a switch hands control between them); the only
//! other context is IRQs, which save/restore everything they touch (incl. the VFP
//! save in the HAL `_irq_handler`), so an IRQ may preempt the fiber harmlessly.
#![allow(dead_code)] // the driver that calls start/resume/yield lands in the next step

use core::ffi::c_void;
use core::future::Future;
use core::pin::pin;
use core::sync::atomic::{AtomicBool, Ordering};
use core::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;

/// Saved callee-saved register context for a cooperative switch. A cooperative
/// switch happens at a function-call boundary, so only callee-saved state must be
/// preserved: core `r4–r11`, `sp`, `lr`, VFP `d8–d15`, and `fpscr`. Field order is
/// the exact order [`fiber_switch`] stores/loads them (sequential, via writeback) —
/// do not reorder without updating the assembly. `repr(C)` + `align(8)` so the VFP
/// block is 8-byte aligned for `vstm`/`vldm`.
#[repr(C, align(8))]
struct Ctx {
    core: [u32; 8], // r4-r11
    sp: u32,        // r13
    lr: u32,        // r14 (resume/return address; thumb bit preserved for interworking)
    vfp: [u32; 16], // d8-d15
    fpscr: u32,
}

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

unsafe extern "C" {
    fn fiber_switch(save: *mut Ctx, restore: *const Ctx);
}

/// Worker fiber stack. Sized for the deepest operation (song load is deep). In
/// SDRAM (`.sdram_bss`, zeroed at boot) to spare the tight internal SRAM.
const WORKER_STACK_SIZE: usize = 64 * 1024;
#[unsafe(link_section = ".sdram_bss")]
static mut WORKER_STACK: [u8; WORKER_STACK_SIZE] = [0; WORKER_STACK_SIZE];

// Saved contexts: MAIN = the embassy worker task; FIBER = the operation.
static mut MAIN_CTX: Ctx = Ctx::zeroed();
static mut FIBER_CTX: Ctx = Ctx::zeroed();

/// The operation currently assigned to the fiber, consumed by [`trampoline`] on
/// first entry. `extern "C"` so C++ dispatch can hand over a function + context.
static mut CURRENT_FN: Option<(extern "C" fn(*mut c_void), *mut c_void)> = None;
/// Set by [`trampoline`] when the operation returns; observed by the worker.
static mut FIBER_DONE: bool = false;
/// True while control is executing on the fiber. `yield()` reads this to decide
/// whether to suspend the fiber (on it) or fall back to a busy-wait (off it, e.g.
/// the legacy C-HAL SPI-flash/USB storage waits).
static ON_FIBER: AtomicBool = AtomicBool::new(false);

/// Is the caller running on the worker fiber? (For the `yield()` implementation.)
pub fn on_fiber() -> bool {
    ON_FIBER.load(Ordering::Relaxed)
}

/// 8-byte-aligned top of the worker stack (stacks grow down).
fn worker_stack_top() -> u32 {
    let base = core::ptr::addr_of!(WORKER_STACK) as u32;
    (base + WORKER_STACK_SIZE as u32) & !7
}

/// First-entry trampoline: runs the assigned operation, then marks the fiber done
/// and parks by switching back to main. Never returns (it sits at the base of the
/// worker stack — returning would pop garbage).
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
pub fn yield_now() {
    switch_to_main();
}

// ---------------------------------------------------------------------------
// Worker driver: a serialized queue of operations run on the fiber, plus the
// predicate-wait that backs the scheduler's yield(). Pumped by the embassy
// app_task via `worker_poll()`; ops are submitted by C++ dispatch via
// `deluge_worker_run`. While an op is suspended on a predicate the executor runs
// every other task + I/O, so the predicate (cluster drain, button release, ...)
// flips and the op resumes.
// ---------------------------------------------------------------------------

use crate::sys::RunCondition;

/// Pending operations (serialized — these are user actions, at most one active).
type Job = (extern "C" fn(*mut c_void), *mut c_void);
const QUEUE_CAP: usize = 4;
static mut QUEUE: [Option<Job>; QUEUE_CAP] = [None; QUEUE_CAP];
static mut Q_HEAD: usize = 0;
static mut Q_COUNT: usize = 0;

/// An operation is on the fiber (running or suspended) — distinct from idle.
static FIBER_BUSY: AtomicBool = AtomicBool::new(false);

/// Wakes the worker pump (`app_task`). Raised when an op is submitted, when a task
/// runner makes progress while an op is suspended (so its predicate is re-checked),
/// and by the [`block_on_fiber`] waker when an awaited future (e.g. an SD transfer)
/// completes. Lets `app_task` sleep instead of polling on a fixed tick.
pub static WORKER_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// Wake the worker unconditionally (e.g. a new op was submitted).
pub fn wake() {
    WORKER_WAKE.signal(());
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

fn now_us() -> u64 {
    embassy_time::Instant::now().as_micros()
}

/// Submit an operation to run on the worker fiber (C++ dispatch boundary). Runs
/// serialized after any already-queued operations once the worker is pumped.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_worker_run(f: extern "C" fn(*mut c_void), ctx: *mut c_void) {
    // SAFETY: single-threaded; enqueue only (no switch here).
    unsafe {
        if Q_COUNT < QUEUE_CAP {
            let tail = (Q_HEAD + Q_COUNT) % QUEUE_CAP;
            core::ptr::addr_of_mut!(QUEUE)
                .cast::<Option<Job>>()
                .add(tail)
                .write(Some((f, ctx)));
            Q_COUNT += 1;
        }
        // else: queue full (should not happen for user actions) — drop.
    }
    // Wake the pump so the op starts promptly (it may be idle-asleep).
    wake();
}

fn dequeue() -> Option<Job> {
    // SAFETY: single-threaded access to the ring.
    unsafe {
        if Q_COUNT == 0 {
            return None;
        }
        let head = Q_HEAD;
        let job = core::ptr::addr_of_mut!(QUEUE)
            .cast::<Option<Job>>()
            .add(head)
            .replace(None);
        Q_HEAD = (Q_HEAD + 1) % QUEUE_CAP;
        Q_COUNT -= 1;
        job
    }
}

fn queue_nonempty() -> bool {
    unsafe { core::ptr::addr_of!(Q_COUNT).read() > 0 }
}

/// Pump the worker. Starts the next queued op (if idle) or resumes the suspended
/// op once its predicate holds / times out. Returns true while work remains.
/// Called from the embassy `app_task` on a ~1 ms ticker.
pub fn worker_poll() -> bool {
    if !FIBER_BUSY.load(Ordering::Relaxed) {
        let Some((f, ctx)) = dequeue() else {
            return false;
        };
        FIBER_BUSY.store(true, Ordering::Relaxed);
        if start(f, ctx) {
            // Completed without ever yielding.
            FIBER_BUSY.store(false, Ordering::Relaxed);
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
        if resume() {
            FIBER_BUSY.store(false, Ordering::Relaxed);
        }
    }
    FIBER_BUSY.load(Ordering::Relaxed) || queue_nonempty()
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
