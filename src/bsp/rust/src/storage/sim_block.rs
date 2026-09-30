//! `block_on` for storage I/O futures, with a host-side progress hook.
//!
//! On device this is `embassy_futures::block_on`: transfers complete by SDHI/DMA interrupt, so a
//! plain spin suffices.
//!
//! On host a pending transfer becomes ready only when another context runs (the
//! `sim_latency::pump` task, or the virtual clock). The host spin therefore calls a progress hook,
//! registered by the harness, that pumps the higher-priority executor and advances virtual time —
//! standing in for the device's interrupt preemption in a single-threaded, deterministic sim.

use core::future::Future;

/// What a progress hook accomplished.
///
/// Consecutive [`Progress::Stalled`] returns count against the spin budget, so an unsatisfiable
/// future panics instead of hanging.
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

    /// The harness's hook. With none, every `Pending` poll counts as stalled.
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

/// Registers the host progress hook; call once at harness start-up, before any `block_on`.
#[cfg(not(target_os = "none"))]
pub fn set_progress_hook(hook: fn() -> Progress) {
    host::set_hook(hook);
}

/// Sets how many consecutive stalled polls `block_on` tolerates before panicking as wedged.
#[cfg(not(target_os = "none"))]
pub fn set_spin_budget(polls: u32) {
    host::BUDGET.store(polls, core::sync::atomic::Ordering::SeqCst);
}

/// Registers a hook for a threaded host, which yields the OS thread and reports progress; call
/// once at start-up, before any `block_on`.
///
/// On a threaded host a `Pending` is routinely satisfied by another thread (the streaming fill task
/// holds the FS mutex across its SD await), and the default budget elapses in well under a
/// millisecond of spinning. This hook always reports [`Progress::Advanced`], so it disables wedge
/// detection; that is left to wall-clock timeouts and TSan.
///
/// Do not install it in a single-threaded sim, where the budget is the only wedge detector.
#[cfg(not(target_os = "none"))]
pub fn use_threaded_host_hook() {
    fn hook() -> Progress {
        std::thread::yield_now();
        Progress::Advanced
    }
    host::set_hook(hook);
}

/// Polls `fut` to completion, spinning without yielding.
#[cfg(target_os = "none")]
pub fn block_on<F: Future>(fut: F) -> F::Output {
    embassy_futures::block_on(fut)
}

/// Polls `fut` to completion, calling the progress hook after each `Pending`.
///
/// Polls with a no-op waker, so `fut` must not await an `embassy_time::Timer` (the integrated
/// timer queue panics on a non-Embassy waker).
///
/// # Panics
///
/// Panics when the hook reports [`Progress::Stalled`] (or no hook is set) for the spin budget's
/// worth of consecutive polls.
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
