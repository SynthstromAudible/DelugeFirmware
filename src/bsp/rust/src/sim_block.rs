//! `block_on` for storage I/O futures, with a host-side progress hook.
//!
//! On device this is exactly `embassy_futures::block_on`: a non-yielding spin whose
//! awaited transfers complete by SDHI/DMA interrupt, so nothing else needs to run for the
//! future to become ready.
//!
//! On host there is no completion interrupt. A pending transfer becomes ready only when
//! some *other* context runs — the `sim_latency::pump` task advances, or the virtual clock
//! moves. A plain spin can therefore never complete (empirically confirmed: see
//! `sd.rs`'s `off_fiber_instant` doc comment). So the host spin calls a **progress hook**
//! the harness registers, which pumps its higher-priority executor and advances virtual
//! time. This emulates the device's interrupt preemption inside a cooperative,
//! single-threaded, deterministic sim.
//!
//! The hook lives here rather than in the harness so production call sites (R5a Phase 2)
//! can call `block_on` without the BSP depending on any harness crate.

use core::future::Future;

/// What a progress hook accomplished. Consecutive [`Progress::Stalled`] returns trip the
/// wedge budget, so a genuinely unsatisfiable future fails loudly instead of hanging.
#[cfg(not(target_os = "none"))]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Progress {
    /// The hook did something that could make a pending future ready.
    Advanced,
    /// Nothing left to pump or advance.
    Stalled,
}

#[cfg(not(target_os = "none"))]
mod host {
    use super::Progress;
    use core::sync::atomic::AtomicU32;

    /// Registered by the harness. `None` means "no hook" — then a `Pending` future is
    /// immediately a wedge, which is the correct behaviour for a host build with no harness.
    static HOOK_FN: std::sync::Mutex<Option<fn() -> Progress>> = std::sync::Mutex::new(None);
    /// Consecutive `Stalled` polls tolerated before declaring a wedge.
    pub(super) static BUDGET: AtomicU32 = AtomicU32::new(100_000);

    pub fn set_hook(hook: fn() -> Progress) {
        *HOOK_FN.lock().unwrap() = Some(hook);
    }

    pub fn hook() -> Option<fn() -> Progress> {
        *HOOK_FN.lock().unwrap()
    }
}

/// Register the host progress hook. Call once during harness start-up, before any
/// `block_on`. No-op on device.
#[cfg(not(target_os = "none"))]
pub fn set_progress_hook(hook: fn() -> Progress) {
    host::set_hook(hook);
}

/// Set how many consecutive stalled polls are tolerated before panicking as wedged.
#[cfg(not(target_os = "none"))]
pub fn set_spin_budget(polls: u32) {
    host::BUDGET.store(polls, core::sync::atomic::Ordering::SeqCst);
}

/// Register the progress hook appropriate to a **threaded** host: yield the OS thread and
/// report progress. Call once at start-up, before any `block_on`. No-op on device.
///
/// The default no-hook behaviour treats a `Pending` future as an immediate wedge. That is right
/// for a single-threaded cooperative sim — nothing else can run, so nothing can satisfy it — and
/// wrong for a threaded host, where a `Pending` is routinely satisfied by another thread. The
/// streaming fill task holds the FS mutex across its SD await on its own thread, so cross-thread
/// contention is the normal case, and a spin iteration is nanoseconds: the 100_000-poll default
/// budget is well under a millisecond, so a contended read would trip it spuriously.
///
/// The trade is deliberate and worth knowing: because this hook always reports
/// [`Progress::Advanced`], it does **not** detect a genuinely unsatisfiable spin. That is
/// acceptable here — wedge detection on a threaded host belongs to the wall-clock timeouts and
/// TSan, while `lens1_vt_sim` keeps the strict budget-based detector its charter assigns it (R5a
/// spec §3). **Do not install this hook in a single-threaded sim:** it would silently disarm the
/// detector there, which is the one place the detector is load-bearing.
#[cfg(not(target_os = "none"))]
pub fn use_threaded_host_hook() {
    fn hook() -> Progress {
        std::thread::yield_now();
        Progress::Advanced
    }
    host::set_hook(hook);
}

/// Device: a plain non-yielding block. Transfers complete by interrupt.
#[cfg(target_os = "none")]
pub fn block_on<F: Future>(fut: F) -> F::Output {
    embassy_futures::block_on(fut)
}

/// Host: a non-yielding block that invokes the registered progress hook on each `Pending`.
///
/// Uses the same synthetic no-op waker `embassy_futures::block_on` does, so any future
/// polled here must be synthetic-waker-safe — notably NOT `embassy_time::Timer`, whose
/// integrated queue panics on a non-embassy waker. `sim_latency`'s `AtomicWaker`-based
/// completion future is safe.
#[cfg(not(target_os = "none"))]
pub fn block_on<F: Future>(fut: F) -> F::Output {
    use core::pin::pin;
    use core::sync::atomic::Ordering;
    use core::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

    static VTABLE: RawWakerVTable = RawWakerVTable::new(
        |_| RawWaker::new(core::ptr::null(), &VTABLE),
        |_| {},
        |_| {},
        |_| {},
    );
    // SAFETY: the vtable is 'static and every fn is a no-op touching no state.
    let waker = unsafe { Waker::from_raw(RawWaker::new(core::ptr::null(), &VTABLE)) };
    let mut cx = Context::from_waker(&waker);
    let mut fut = pin!(fut);

    let mut stalled = 0u32;
    loop {
        if let Poll::Ready(v) = fut.as_mut().poll(&mut cx) {
            return v;
        }
        let budget = host::BUDGET.load(Ordering::SeqCst);
        match host::hook().map(|h| h()) {
            Some(Progress::Advanced) => stalled = 0,
            Some(Progress::Stalled) => stalled += 1,
            None => stalled += 1,
        }
        assert!(
            stalled < budget,
            "sim_block: wedged — {stalled} consecutive stalled polls with a Pending future. \
             Nothing can make it ready; check the progress hook and executor wiring."
        );
    }
}
