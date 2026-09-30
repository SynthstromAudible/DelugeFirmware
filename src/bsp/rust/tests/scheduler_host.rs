//! Host tests for the scheduler (`libdeluge/scheduler.h` on Embassy): self-blocking and stats, on
//! an embassy std executor.
//!
//! The scheduler is process-global (one task table, one spawner), so every test here shares one
//! executor thread, and the tests run one at a time under [`serial`]. Registration must happen on
//! the executor thread; [`on_executor`] runs a closure there. Each test blocks its tasks before it
//! returns so that they stop taking executor time from the next test.
#![feature(impl_trait_in_assoc_type)]

use std::sync::atomic::{AtomicI8, AtomicU32, Ordering::SeqCst};
use std::sync::{Mutex, MutexGuard, OnceLock, mpsc};
use std::time::{Duration, Instant};

use deluge_bsp_rust::scheduler;

type Job = Box<dyn FnOnce() + Send>;

/// Serializes the tests: they share the executor and the task table.
fn serial() -> MutexGuard<'static, ()> {
    static SERIAL: Mutex<()> = Mutex::new(());
    SERIAL.lock().unwrap_or_else(|e| e.into_inner())
}

/// Runs registration jobs on the executor thread.
#[embassy_executor::task]
async fn registrar(jobs: mpsc::Receiver<Job>) {
    loop {
        while let Ok(job) = jobs.try_recv() {
            job();
        }
        embassy_time::Timer::after_millis(1).await;
    }
}

/// The job queue into the shared executor thread, which is started on first use.
fn jobs() -> &'static Mutex<mpsc::Sender<Job>> {
    static JOBS: OnceLock<Mutex<mpsc::Sender<Job>>> = OnceLock::new();
    JOBS.get_or_init(|| {
        let (tx, rx) = mpsc::channel::<Job>();
        std::thread::Builder::new()
            .name("scheduler-test-executor".into())
            .spawn(move || {
                let executor: &'static mut embassy_executor::Executor =
                    Box::leak(Box::new(embassy_executor::Executor::new()));
                executor.run(move |spawner| {
                    scheduler::set_spawner(spawner);
                    spawner.spawn(registrar(rx).unwrap());
                });
            })
            .expect("spawning the scheduler test executor");
        Mutex::new(tx)
    })
}

/// Runs `f` on the executor thread and returns its result.
fn on_executor<R: Send + 'static>(f: impl FnOnce() -> R + Send + 'static) -> R {
    let (tx, rx) = mpsc::sync_channel(1);
    jobs()
        .lock()
        .unwrap()
        .send(Box::new(move || {
            let _ = tx.send(f());
        }))
        .unwrap();
    rx.recv_timeout(Duration::from_secs(5))
        .expect("the executor ran the job")
}

/// Waits until `pred` holds, panicking with `what` after `timeout`.
fn wait_until(timeout: Duration, what: &str, pred: impl Fn() -> bool) {
    let deadline = Instant::now() + timeout;
    while !pred() {
        assert!(Instant::now() < deadline, "timed out waiting for: {what}");
        std::thread::sleep(Duration::from_millis(1));
    }
}

/// Whether `pred` holds for the whole of `duration`.
fn stays(duration: Duration, pred: impl Fn() -> bool) -> bool {
    let deadline = Instant::now() + duration;
    while Instant::now() < deadline {
        if !pred() {
            return false;
        }
        std::thread::sleep(Duration::from_millis(1));
    }
    true
}

/// Registers a repeating thread-executor task on the executor thread. Times are in seconds, as in
/// the C ABI.
fn repeating(
    body: extern "C" fn(),
    period: f64,
    backoff: f64,
    name: &'static core::ffi::CStr,
) -> i8 {
    let id = on_executor(move || {
        scheduler::addRepeatingTask(
            Some(body),
            5,
            backoff,
            period,
            period * 2.0,
            name.as_ptr(),
            0,
        )
    });
    assert!(id >= 0, "registering {name:?}");
    id
}

// ---- Wakes -------------------------------------------------------------------------------------

static BLOCK_ID: AtomicI8 = AtomicI8::new(-1);
static BLOCK_RUNS: AtomicU32 = AtomicU32::new(0);
extern "C" fn self_blocking_body() {
    BLOCK_RUNS.fetch_add(1, SeqCst);
    scheduler::blockTask(BLOCK_ID.load(SeqCst));
}

/// A task that blocks itself from its own body parks until it is unblocked. The wake the block
/// leaves behind does not make it run.
#[test]
fn block_from_own_body_parks() {
    let _serial = serial();
    // Registered blocked-by-id before its first run: the id is stored before the 5 ms period ends.
    let id = repeating(self_blocking_body, 0.005, 0.0, c"self-blocking");
    BLOCK_ID.store(id, SeqCst);
    wait_until(Duration::from_millis(500), "the first run", || {
        BLOCK_RUNS.load(SeqCst) == 1
    });
    assert!(
        stays(Duration::from_millis(100), || BLOCK_RUNS.load(SeqCst) == 1),
        "blocked: no further runs"
    );
    scheduler::unblockTask(id);
    wait_until(
        Duration::from_millis(500),
        "the run after unblocking",
        || BLOCK_RUNS.load(SeqCst) == 2,
    );
    scheduler::blockTask(id);
}

// ---- Stats -------------------------------------------------------------------------------------

static STATS_RUNS: AtomicU32 = AtomicU32::new(0);
extern "C" fn stats_body() {
    // The first run is excluded and instant; every later run takes 10 ms and counts.
    if STATS_RUNS.fetch_add(1, SeqCst) == 0 {
        scheduler::ignoreForStats();
    } else {
        std::thread::sleep(Duration::from_millis(10));
    }
}

/// `ignoreForStats` excludes only the run that calls it: the runs after it still update the
/// average.
#[test]
fn ignore_for_stats_excludes_one_run() {
    let _serial = serial();
    let id = repeating(stats_body, 0.005, 0.0, c"stats");
    wait_until(Duration::from_secs(2), "six runs", || {
        STATS_RUNS.load(SeqCst) >= 6
    });
    scheduler::blockTask(id);
    let avg = scheduler::getAverageRunTimeForTask(id);
    assert!(
        avg >= 0.008,
        "average run time {avg} s: the later runs were not counted"
    );
}
