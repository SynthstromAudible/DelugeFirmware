//! `sim_block::block_on`: a future pending on an outside agent completes when a progress hook is
//! registered, and an unsatisfiable one trips the spin budget instead of spinning forever.

use core::future::Future;
use core::pin::Pin;
use core::sync::atomic::{AtomicU32, Ordering};
use core::task::{Context, Poll};
use std::sync::Mutex;

use deluge_bsp_rust::storage::sim_block;

use sim_block::Progress;

/// Serializes the tests: the progress hook and spin budget are process-global.
///
/// Each test takes it for its whole body and sets both hook and budget itself.
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

/// Ready only once another OS thread sets `CROSS_THREAD_DONE`; nothing the spinning thread does
/// can make it ready.
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
    // Only another OS thread can satisfy this spin; on a threaded host, yielding is progress.
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    CROSS_THREAD_DONE.store(false, Ordering::SeqCst);

    sim_block::use_threaded_host_hook();
    // Small enough to trip long before the other thread lands unless the hook counts yielding as
    // progress.
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
    // Panics while holding `TEST_LOCK`, poisoning it; every acquisition recovers from that.
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    TICKS.store(0, Ordering::SeqCst);
    sim_block::set_progress_hook(stalled_hook);
    sim_block::set_spin_budget(8);

    // Needs ticks the stalled hook will never supply.
    let _ = sim_block::block_on(NeedsTicks { needed: u32::MAX });
}
