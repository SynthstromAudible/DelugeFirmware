//! Host tests for the `sim_block` seam (R5a Phase 0). Verifies that a future which is
//! Pending until an external agent acts can still complete under a non-yielding block,
//! provided a progress hook is registered — and that an unsatisfiable future trips the
//! wedge budget instead of spinning forever.
#![cfg(not(target_os = "none"))]

use core::future::Future;
use core::pin::Pin;
use core::sync::atomic::{AtomicU32, Ordering};
use core::task::{Context, Poll};
use std::sync::Mutex;

// No `deluge_bsp_rust::` path exists (bin-only crate) — re-include the module the same way
// `tests/owner_host.rs:15-18` re-includes `fiber`/`sd`.
#[path = "../src/sim_block.rs"]
mod sim_block;

use sim_block::Progress;

/// `sim_block`'s progress hook and spin budget are process-global statics.
/// `cargo test` runs `#[test]`s in parallel threads by default, so without
/// this lock one test's `set_progress_hook`/`set_spin_budget` can stomp on
/// another's mid-spin (see `fs_differential/tests/differential.rs:9-18` for
/// the same idiom against a different shared global). Every test here takes
/// this lock first, for its whole body, instead of requiring
/// `--test-threads=1`. It does NOT reset the hook/budget between tests —
/// each test already sets both explicitly at the top, which is what gives
/// it a clean slate.
static TEST_LOCK: Mutex<()> = Mutex::new(());

/// Ready only once `TICKS` has been bumped `n` times by the progress hook.
struct NeedsTicks {
    needed: u32,
}
static TICKS: AtomicU32 = AtomicU32::new(0);

impl Future for NeedsTicks {
    type Output = u32;
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<u32> {
        let seen = TICKS.load(Ordering::SeqCst);
        if seen >= self.needed {
            Poll::Ready(seen)
        } else {
            Poll::Pending
        }
    }
}

fn bump_hook() -> Progress {
    TICKS.fetch_add(1, Ordering::SeqCst);
    Progress::Advanced
}

#[test]
fn block_on_completes_when_hook_makes_progress() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    TICKS.store(0, Ordering::SeqCst);
    sim_block::set_progress_hook(bump_hook);
    sim_block::set_spin_budget(1000);

    let got = sim_block::block_on(NeedsTicks { needed: 5 });

    assert_eq!(
        got, 5,
        "future should complete once the hook has ticked enough"
    );
}

/// Pending until another OS thread sets `CROSS_THREAD_DONE` — the shape every host-shim
/// storage spin has once the I/O-class waits collapse. Unlike [`NeedsTicks`], nothing the
/// spinning thread itself does can make this ready.
struct NeedsOtherThread;
static CROSS_THREAD_DONE: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

impl Future for NeedsOtherThread {
    type Output = ();
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<()> {
        if CROSS_THREAD_DONE.load(Ordering::SeqCst) {
            Poll::Ready(())
        } else {
            Poll::Pending
        }
    }
}

#[test]
fn threaded_host_hook_completes_a_spin_another_thread_satisfies() {
    // The threaded host's real case: this thread spins on state only ANOTHER OS thread can
    // set. With no hook (or a Stalled one) `block_on` calls that a wedge and trips its budget;
    // on a threaded host yielding IS progress, which is what the threaded-host hook encodes.
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    CROSS_THREAD_DONE.store(false, Ordering::SeqCst);

    sim_block::use_threaded_host_hook();
    // Deliberately small. If yielding did NOT count as progress, this budget would trip long
    // before the sleeping thread lands — so a pass means the hook is what carried the spin,
    // not that the spin happened to be short.
    sim_block::set_spin_budget(50);

    let h = std::thread::spawn(|| {
        std::thread::sleep(std::time::Duration::from_millis(50));
        CROSS_THREAD_DONE.store(true, Ordering::SeqCst);
    });

    sim_block::block_on(NeedsOtherThread);

    h.join().unwrap();
    assert!(
        CROSS_THREAD_DONE.load(Ordering::SeqCst),
        "the spin returned before the other thread set the flag — it did not actually wait"
    );
}

fn stalled_hook() -> Progress {
    Progress::Stalled
}

#[test]
#[should_panic(expected = "sim_block: wedged")]
fn block_on_wedges_instead_of_spinning_forever() {
    // This test panics on purpose while holding `TEST_LOCK`, poisoning it —
    // hence the `unwrap_or_else(|e| e.into_inner())` recovery on every
    // acquisition above and below, rather than a bare `.unwrap()`.
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    TICKS.store(0, Ordering::SeqCst);
    sim_block::set_progress_hook(stalled_hook);
    sim_block::set_spin_budget(8);

    // Needs ticks the stalled hook will never supply.
    let _ = sim_block::block_on(NeedsTicks { needed: u32::MAX });
}
