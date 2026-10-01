//! The task scheduler (`libdeluge/scheduler.h`) on Embassy.
//!
//! Each registered Deluge task becomes its own Embassy task: `addRepeatingTask` claims a
//! `TaskSlot` and spawns a `task_runner`, which waits on a timer (or polls its condition),
//! runs the synchronous C++ body to completion, and loops. The executors are the scheduler loop.
//! Ready tasks round-robin; there is no deadline heuristic. Every task runs on the thread
//! executor, paced by `targetTimeBetweenCalls`; the render is not a task (see `audio::render`).
//!
//! Registration runs on the thread executor. The slot fields are atomics because the C-ABI control
//! functions may be called from any tier.
#![allow(non_snake_case)]

use core::sync::atomic::{AtomicBool, AtomicI8, AtomicU64, AtomicUsize, Ordering};

use embassy_executor::Spawner;
use embassy_futures::select::{Either, select};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;
use embassy_time::{Duration, Instant, Timer};

/// `libdeluge/scheduler.h`'s `RunCondition`: a task's predicate, `None` for none.
pub type RunCondition = Option<unsafe extern "C" fn() -> bool>;

// The header's types, defined here rather than taken from `sys` because the plain host build has
// no bindgen output; the `abi_pin!`s at the end check them against the header wherever there is.

/// `libdeluge/scheduler.h`'s `TaskHandle`, nullable as bindgen declares it; registration refuses
/// a null one.
type TaskHandle = Option<TaskBody>;
/// A registered task body, the non-null [`TaskHandle`].
type TaskBody = unsafe extern "C" fn();
/// The `int8_t` task id: the [`SLOTS`] index.
type TaskID = i8;
/// The `uint32_t` resource bitmask (`RESOURCE_SD`, `RESOURCE_USB`, `RESOURCE_SD_ROUTINE`).
type ResourceID = u32;

const RESOURCE_NONE: ResourceID = 0;
// No `RESOURCE_SD`: storage serializes itself (see [`USB_GATE`]).
/// `RESOURCE_USB`: the USB transport.
const RESOURCE_USB: ResourceID = 2;

// The widths must match the header's typedefs.
const _: () = {
    assert!(core::mem::size_of::<TaskID>() == 1, "TaskID must be int8_t");
    assert!(
        core::mem::size_of::<ResourceID>() == 4,
        "ResourceID must be uint32_t"
    );
    assert!(
        core::mem::size_of::<TaskBody>() == core::mem::size_of::<usize>(),
        "TaskBody must be pointer-sized"
    );
};

/// The most tasks live at once. Must equal the `task_runner` `pool_size` literal (the task
/// macro wants a literal), which the assert below guards.
const MAX_TASKS: usize = 32;
const _: () = assert!(
    MAX_TASKS == 32,
    "task_runner pool_size literal must equal MAX_TASKS"
);

/// How often a conditional task re-evaluates its `RunCondition` while waiting.
const COND_POLL: Duration = Duration::from_millis(4);

/// A task's control block. One [`task_runner`] owns each slot in use; the C-ABI control
/// functions set its fields and wake the runner through `wake`.
struct TaskSlot {
    /// Claimed by `add*Task`, released when the runner returns.
    used: AtomicBool,
    /// The `TaskBody` as a `usize`; 0 only before first use.
    handle: AtomicUsize,
    /// The `RunCondition` as a `usize`, or 0 for a repeating task.
    condition: AtomicUsize,
    /// The target time between calls, in µs. Conditional tasks poll `condition` instead.
    period_us: AtomicU64,
    /// The minimum time from completion to the next call, in µs.
    backoff_us: AtomicU64,
    /// The resource bitmask gating the run; only `RESOURCE_USB` has a gate.
    resource: AtomicU64,
    /// Run once, then free the slot. Set only by conditional tasks.
    once: AtomicBool,
    /// Cleared by `blockTask`; while false the runner parks on `wake`.
    enabled: AtomicBool,
    /// Whether runs count towards `avg_run_us`; cleared by `ignoreForStats`.
    count_stats: AtomicBool,
    /// The running-average run time in µs: `avg = (avg + sample) / 2`.
    avg_run_us: AtomicU64,
    /// Wakes the runner early (`runTask`, `blockTask`, `unblockTask`).
    wake: Signal<CriticalSectionRawMutex, ()>,
}

impl TaskSlot {
    const fn new() -> Self {
        TaskSlot {
            used: AtomicBool::new(false),
            handle: AtomicUsize::new(0),
            condition: AtomicUsize::new(0),
            period_us: AtomicU64::new(0),
            backoff_us: AtomicU64::new(0),
            resource: AtomicU64::new(RESOURCE_NONE as u64),
            once: AtomicBool::new(false),
            enabled: AtomicBool::new(true),
            count_stats: AtomicBool::new(true),
            avg_run_us: AtomicU64::new(0),
            wake: Signal::new(),
        }
    }

    fn handle_fn(&self) -> TaskBody {
        // SAFETY: written only by `claim` from a real `TaskBody`; the runner
        // never calls a slot whose handle is unset.
        unsafe { core::mem::transmute::<usize, TaskBody>(self.handle.load(Ordering::Relaxed)) }
    }

    /// The stored predicate, or `None` for a repeating task (0 transmutes to `None`).
    fn condition_fn(&self) -> RunCondition {
        // SAFETY: written only by `claim` from a real `RunCondition` (or 0 = None).
        unsafe {
            core::mem::transmute::<usize, RunCondition>(self.condition.load(Ordering::Relaxed))
        }
    }

    /// The wait before the next run, in µs.
    fn next_wait_us(&self) -> u64 {
        self.period_us.load(Ordering::Relaxed)
    }

    fn record(&self, run_us: u64) {
        if !self.count_stats.load(Ordering::Relaxed) {
            return;
        }
        let avg = self.avg_run_us.load(Ordering::Relaxed);
        self.avg_run_us.store((avg + run_us) / 2, Ordering::Relaxed);
    }

    fn reset_for_reuse(&self) {
        self.condition.store(0, Ordering::Relaxed);
        self.period_us.store(0, Ordering::Relaxed);
        self.backoff_us.store(0, Ordering::Relaxed);
        self.resource.store(RESOURCE_NONE as u64, Ordering::Relaxed);
        self.once.store(false, Ordering::Relaxed);
        self.enabled.store(true, Ordering::Relaxed);
        self.count_stats.store(true, Ordering::Relaxed);
        self.avg_run_us.store(0, Ordering::Relaxed);
        // Drop any stale wake, so a newly claimed slot doesn't fire at once.
        self.wake.reset();
    }
}

/// The task table. Every field is interior-mutable, so runners can hold `&'static TaskSlot`.
static SLOTS: [TaskSlot; MAX_TASKS] = [const { TaskSlot::new() }; MAX_TASKS];

/// The slot whose body is running, for `ignoreForStats`; -1 when none is.
static CURRENT: AtomicI8 = AtomicI8::new(-1);

/// The `RESOURCE_USB` gate, held across a USB task's body so two such tasks never overlap.
///
/// There is no `RESOURCE_SD` gate: storage serializes itself (the filesystem mutex and the SD bus
/// lock). `RESOURCE_SD_ROUTINE` needs none either: no SD-routine work runs across a yield.
static USB_GATE: Mutex<CriticalSectionRawMutex, ()> = Mutex::new(());

// Set by [`set_spawner`] before `deluge_app_init` and read only on the thread executor.
static mut SPAWNER: Option<Spawner> = None;

/// Sets the thread executor's spawner, which the `add*Task` functions spawn runners with.
///
/// Call once, from inside that executor, before `deluge_app_init`.
pub fn set_spawner(spawner: Spawner) {
    // SAFETY: called once at startup, before any task runs, and read only on the same thread.
    unsafe { *core::ptr::addr_of_mut!(SPAWNER) = Some(spawner) };
}

fn spawner() -> Option<Spawner> {
    // SAFETY: see `set_spawner`; reads happen only on the executor thread.
    unsafe { *core::ptr::addr_of!(SPAWNER) }
}

/// Claims a free slot, fills it, and spawns its runner.
///
/// Returns the slot index as the `TaskID`, or -1 if the table or the runner pool is full or no
/// spawner is set.
fn claim(
    name: *const core::ffi::c_char,
    handle: TaskBody,
    condition: RunCondition,
    period_us: u64,
    backoff_us: u64,
    resource: ResourceID,
    once: bool,
    enabled: bool,
) -> TaskID {
    let Some(spawner) = spawner() else {
        log::error!(
            "scheduler: task {:?} registered before the executor runs; it will never run",
            task_name(name)
        );
        return -1;
    };
    for (i, slot) in SLOTS.iter().enumerate() {
        if slot
            .used
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Relaxed)
            .is_ok()
        {
            slot.reset_for_reuse();
            slot.handle.store(handle as usize, Ordering::Relaxed);
            slot.condition
                .store(condition.map_or(0, |c| c as usize), Ordering::Relaxed);
            slot.period_us.store(period_us, Ordering::Relaxed);
            slot.backoff_us.store(backoff_us, Ordering::Relaxed);
            slot.resource.store(resource as u64, Ordering::Relaxed);
            slot.once.store(once, Ordering::Relaxed);
            slot.enabled.store(enabled, Ordering::Relaxed);
            // `Err` means the runner pool is exhausted.
            let Ok(token) = task_runner(slot) else {
                slot.used.store(false, Ordering::Release);
                log::error!(
                    "scheduler: no runner left for task {:?}; it will never run",
                    task_name(name)
                );
                return -1;
            };
            spawner.spawn(token);
            return i as TaskID;
        }
    }
    // A caller that ignores the -1 loses the task silently, so name it.
    log::error!(
        "scheduler: the task table is full ({MAX_TASKS} slots); task {:?} will never run",
        task_name(name)
    );
    -1
}

/// A task's registered name, for the scheduler's errors.
fn task_name(name: *const core::ffi::c_char) -> &'static str {
    if name.is_null() {
        return "<unnamed>";
    }
    // SAFETY: the app registers every task with a NUL-terminated string literal.
    unsafe { core::ffi::CStr::from_ptr(name) }
        .to_str()
        .unwrap_or("<not UTF-8>")
}

fn slot_of(id: TaskID) -> Option<&'static TaskSlot> {
    let i = id as isize;
    if i < 0 || i as usize >= MAX_TASKS {
        return None;
    }
    let slot = &SLOTS[i as usize];
    slot.used.load(Ordering::Acquire).then_some(slot)
}

fn current_slot() -> Option<&'static TaskSlot> {
    slot_of(CURRENT.load(Ordering::Relaxed))
}

/// Runs one Deluge task: waits per the slot's schedule or condition, runs the C++ body to
/// completion, and loops. A once task exits after its first run.
#[embassy_executor::task(pool_size = 32)]
async fn task_runner(slot: &'static TaskSlot) {
    // Pairs with `claim`'s stores, so the slot's fields are visible.
    while !slot.used.load(Ordering::Acquire) {
        embassy_futures::yield_now().await;
    }

    loop {
        if !slot.enabled.load(Ordering::Relaxed) {
            slot.wake.wait().await;
            continue;
        }

        match slot.condition_fn() {
            Some(cond) => {
                // SAFETY: `cond` is the C++ app's registered predicate, which takes no arguments.
                while !unsafe { cond() } {
                    Timer::after(COND_POLL).await;
                }
            }
            None => {
                // Wait the interval, or less if signalled.
                let wait = Duration::from_micros(slot.next_wait_us());
                if let Either::Second(()) = select(Timer::after(wait), slot.wake.wait()).await {
                    // Re-check `enabled` first.
                    continue;
                }
            }
        }

        if !slot.enabled.load(Ordering::Relaxed) {
            continue;
        }

        let resource = slot.resource.load(Ordering::Relaxed) as ResourceID;

        let run_us = {
            let _usb = if resource & RESOURCE_USB != 0 {
                Some(USB_GATE.lock().await)
            } else {
                None
            };

            // Save and restore CURRENT, so the id outside this body is intact after it.
            let idx = SLOTS.iter().position(|s| core::ptr::eq(s, slot)).unwrap() as TaskID;
            let prev = CURRENT.swap(idx, Ordering::Relaxed);
            // `ignoreForStats` excludes one run, so every run starts counted.
            slot.count_stats.store(true, Ordering::Relaxed);
            let t0 = Instant::now();
            // SAFETY: a task body the C++ app registered, which takes no arguments and may run on
            // this tier.
            unsafe { (slot.handle_fn())() };
            let run_us = t0.elapsed().as_micros();
            CURRENT.store(prev, Ordering::Relaxed);
            run_us
            // The USB gate is released here, before the back-off.
        };
        slot.record(run_us);

        if slot.once.load(Ordering::Relaxed) {
            break;
        }

        let backoff = slot.backoff_us.load(Ordering::Relaxed);
        if backoff != 0 {
            Timer::after(Duration::from_micros(backoff)).await;
        }
    }

    slot.used.store(false, Ordering::Release);
}

// The C ABI speaks `double` seconds.

fn secs_to_us(seconds: f64) -> u64 {
    if seconds <= 0.0 {
        0
    } else {
        (seconds * 1_000_000.0) as u64
    }
}

fn us_to_secs(us: u64) -> f64 {
    us as f64 / 1_000_000.0
}

// The `libdeluge/scheduler.h` C ABI.

#[unsafe(no_mangle)]
pub extern "C" fn addRepeatingTask(
    task: TaskHandle,
    _priority: u8,
    backOffTime: f64,
    targetTimeBetweenCalls: f64,
    _maxTimeBetweenCalls: f64,
    name: *const core::ffi::c_char,
    resource: ResourceID,
) -> TaskID {
    let Some(task) = task else {
        return -1;
    };
    claim(
        name,
        task,
        None,
        secs_to_us(targetTimeBetweenCalls),
        secs_to_us(backOffTime),
        resource,
        false,
        true,
    )
}

#[unsafe(no_mangle)]
pub extern "C" fn addConditionalTask(
    task: TaskHandle,
    _priority: u8,
    condition: RunCondition,
    name: *const core::ffi::c_char,
    resource: ResourceID,
) -> TaskID {
    let Some(task) = task else {
        return -1;
    };
    claim(name, task, condition, 0, 0, resource, true, true)
}

#[unsafe(no_mangle)]
pub extern "C" fn blockTask(id: TaskID) {
    if let Some(slot) = slot_of(id) {
        slot.enabled.store(false, Ordering::Relaxed);
        slot.wake.signal(());
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn unblockTask(id: TaskID) {
    if let Some(slot) = slot_of(id) {
        slot.enabled.store(true, Ordering::Relaxed);
        slot.wake.signal(());
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn runTask(id: TaskID) {
    if let Some(slot) = slot_of(id) {
        slot.wake.signal(());
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ignoreForStats() {
    if let Some(slot) = current_slot() {
        slot.count_stats.store(false, Ordering::Relaxed);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn getAverageRunTimeForTask(id: TaskID) -> f64 {
    slot_of(id).map_or(0.0, |s| us_to_secs(s.avg_run_us.load(Ordering::Relaxed)))
}

#[unsafe(no_mangle)]
pub extern "C" fn getSystemTime() -> f64 {
    us_to_secs(Instant::now().as_micros())
}

#[unsafe(no_mangle)]
pub extern "C" fn isSDRoutineActive() -> bool {
    // No task body ever runs inside an SD busy-wait on this BSP.
    false
}

// `yield` suspends nothing: the operations that call it are stepped by their callers, so it
// evaluates the predicate once and returns.
#[unsafe(export_name = "yield")]
pub extern "C" fn r#yield(until: RunCondition) {
    // SAFETY: `until` is the C++ caller's predicate, which takes no arguments.
    let _ = until.map_or(true, |p| unsafe { p() });
}

abi_pin!(
    addConditionalTask,
    addRepeatingTask,
    blockTask,
    getAverageRunTimeForTask,
    getSystemTime,
    ignoreForStats,
    isSDRoutineActive,
    runTask,
    unblockTask,
);
// bindgen spells `yield` as `yield_`: it is a Rust keyword.
abi_pin!(yield_ => r#yield);
