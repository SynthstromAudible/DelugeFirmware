# Storage-yield relocation (libdeluge)

Status: **landed (build-green; awaiting hardware test)** on
`feat/libdeluge-storage-yield`. See the Outcome section at the end for what shipped
and how decision 3 was refined for the Embassy target. This is the *last* remaining
upward coupling in the libdeluge separation — after it, `app → BSP → HAL →
foundation` has no upward edges (the BSP is already clean; the HAL's only real
remaining edge is this). Like the CV and MIDI inversions it rewrites a real-time
path (the SD / SPI-flash / USB busy-waits), so it is **behaviour-sensitive** — audio
glitching and SD reliability under load — and must be flashed and tested on
hardware, not just built green. Do it on this branch.

## The problem: the HAL pumps the app while it waits

While the HAL busy-waits on slow storage (SD), SPI flash, or USB, it calls **up**
into the application to keep audio + UI alive:

```
routineForSD()                    sd_trns.c:84, sd_read.c:140, spibsc_ioset_drv.c:201,
                                  spibsc_flash_userdef.c:180, r_usb_rz_mcu.c:301/318,
                                  sd_dev_low.c:1146 (#else — dead under USE_TASK_MANAGER)
yieldingRoutineWithTimeoutForSD() sd_dev_low.c:1116 (the live USE_TASK_MANAGER path)
loadAnyEnqueuedClustersRoutine()  diskio.c:83, 259
```

**Compile-time, the HAL is already clean**: it gets these through the boundary
contracts `<libdeluge/storage_wait.h>` (the three `…ForSD` hooks) and
`<libdeluge/block_device.h>` (`loadAnyEnqueuedClustersRoutine`). The remaining
problem is purely **runtime control flow**: those contracts are *implemented in the
app* (`deluge.cpp`), and the implementations pump app code. This is the same shape
as the CV/MIDI work — invert it so the HAL yields *down* to the scheduler
foundation, never up into the app.

(One literal straggler: `sd_dev_low.c` still `#include`s `OSLikeStuff/scheduler_api.h`
directly, but only uses `yieldingRoutineWithTimeoutForSD` from `storage_wait.h` — the
include is **dead**, a trivial removal.)

## Anatomy of the three implementations (`deluge.cpp`)

```c
yieldingRoutineWithTimeoutForSD(until, timeout):
    if intc_func_active: return false              // (a) don't yield inside an ISR
    if sdRoutineLock: busy-wait on until w/ timeout // (b) re-entrant: spin, don't recurse
    sdRoutineLock = true; r = yieldWithTimeout(until, timeout); sdRoutineLock = false; return r

yieldingRoutineForSD(until):                        // same (a)+(b), then yield(until)

routineForSD():                                     // the "pump once" form, no condition
    if intc_func_active: return
    if sdRoutineLock: return
    sdRoutineLock = true
    AudioEngine::runRoutine()                        // <-- app
    one of { oledRoutine()+PIC::flush() | interpretEncoders() | readButtonsAndPads() }  // <-- app, round-robin
    sdRoutineLock = false
```

So there are two distinct things:
- **The yielding wrappers** (`yieldingRoutineForSD` / `…WithTimeout`) — already yield
  to the **scheduler** (`yield` / `yieldWithTimeout`) under `USE_TASK_MANAGER` (always
  on). They name no app code; they are pure concurrency logic (an ISR guard, a
  reentrancy lock, a yield). These can move **down** into the scheduler foundation.
- **The manual pump** (`routineForSD`) — explicitly runs `AudioEngine::runRoutine()`
  and the UI round-robin. This is the legacy non-task-manager cooperative loop. With
  the task manager on it is **redundant**: `yield()` runs the audio + UI as registered
  tasks. The live bare `routineForSD()` call sites should become scheduler yields.

`yieldToIdle(until)` (= `taskManager.yield(until, 0, true)`) is documented in
`scheduler_api.h` as "use if you're yielding in a loop such as the sd routine" — it
is the intended replacement for the bare `routineForSD()` busy-loops.

## Facts / gotchas (verified)

- **`sdRoutineLock` is app-visible** — declared in `deluge/extern.h`, read by ~10 UI
  files (context menus, sound_editor, …) to gate user actions while an SD routine is
  in flight. It is *not* private to the yield logic, so it cannot simply move into a
  dep-free foundation; whoever owns the reentrancy flag must keep exposing it to the
  app (the scheduler can set it; the app reads it), or the UI-gating must be reworked.
- **`intc_func_active`** is HAL state (`RZA1/intc/intc_userdef.c`, decl in
  `devdrv_intc.h`): the active interrupt id, 0 outside an ISR. The "don't yield in an
  ISR" guard reads it — fine, it is below the app.
- **Live vs dead `routineForSD()`**: the calls in `sd_trns/sd_read/spibsc_*/r_usb_rz_mcu`
  are bare (live manual-pump even with the task manager); `sd_dev_low.c`'s is in the
  `#else` (dead). `r_usb_rz_mcu.c` calling `routineForSD()` means the USB stack also
  pumps the SD/app loop during its own waits — watch this cross-coupling.
- **`loadAnyEnqueuedClustersRoutine`** (decl in `block_device.h`, impl `deluge.cpp` →
  `audioFileManager.loadAnyEnqueuedClusters()`) is *also* registered as a repeating
  scheduler task (`deluge.cpp:536`); `diskio` calls it directly at the top of
  `disk_read` to fulfil SD streaming before a new read (a priority/ordering thing).
- `USE_TASK_MANAGER` is unconditionally defined.

## Target architecture

```
HAL SD / SPI-flash / USB busy-wait
   │  yield to the scheduler  (deluge_storage_yield… / yieldToIdle)
   ▼
<libdeluge/scheduler.h>  (re-exports the scheduler C API; OSLikeStuff today,
   │                      Embassy executor later)
   ▼
scheduler foundation  — runs the registered audio/UI/USB tasks; owns the
                        reentrancy + ISR guards. No app code named by the HAL.
```

The HAL stops naming `routineForSD`/app pumping; it yields to a scheduler primitive.
The wrapper logic (ISR guard + reentrancy + yield) lives in the scheduler/runtime,
not in `deluge.cpp`. The app's only role becomes: register its audio/UI/USB/cluster
work as tasks (it already does) and read `sdRoutineLock` for UI gating.

## Design decisions to settle first

1. **Where the wrapper impl lives.** Move `yieldingRoutineForSD/…WithTimeout` out of
   `deluge.cpp` into the scheduler foundation (`OSLikeStuff/task_scheduler`), keeping
   `<libdeluge/storage_wait.h>` as the contract. They depend only on the scheduler +
   `intc_func_active` (HAL) + `sdRoutineLock`.
2. **`sdRoutineLock` ownership.** Simplest: the relocated wrappers keep setting the
   existing app-visible `sdRoutineLock`; the UI keeps reading it. (Cleaner long-term:
   expose "is a storage routine running" via the scheduler, but that touches ~10 UI
   files — probably out of scope here.). Do the cleaner long-term fix.
3. **The manual pump → yield.** ~~Replace each bare `routineForSD()` with
   `yieldToIdle(condition)`.~~ **Refined for the Embassy target (see Outcome):** each
   site declares *what it is waiting for* instead of pumping. Embassy expresses every
   storage wait as `poll_fn`-on-an-ISR-waker or `Timer::after`; a conditionless "pump"
   has no async analog, so the conditionless `routineForSD()` is *eliminated*, not kept.
   Sites become the existing guarded `yieldingRoutineForSD(until)` /
   `yieldingRoutineWithTimeoutForSD` hooks (which already carry the ISR + reentrancy
   guards). `RunCondition` is captureless, but its job is to read a hardware register —
   the C mirror of an Embassy poll closure. This is the behaviour-sensitive heart of the
   change (pump cadence → scheduler-yield cadence); flash-test under load.
4. **`loadAnyEnqueuedClustersRoutine`.** Options: (a) leave it (it is already a
   boundary contract + a registered task; `diskio` calling it is a priority hint), or
   (b) invert — the app services its cluster queue before issuing reads rather than
   the HAL calling up. (a) is lower-risk; (b) is cleaner. Decide before touching it.
   Decision: B.

## Phased plan (each phase build-green; only the whole is flash-testable)

1. **Trivial cleanup:** remove the dead `OSLikeStuff/scheduler_api.h` include from
   `sd_dev_low.c`.
2. **Relocate the yield wrappers** down into the scheduler foundation (from
   `deluge.cpp`), unchanged in behaviour; `storage_wait.h` stays the contract;
   `sdRoutineLock` per decision 2. No call-site changes yet — pure move. Flash-test
   (should be identical).
3. **Convert the bare `routineForSD()` busy-loops** to `yieldToIdle(condition)`,
   one call site per commit (`sd_read`, `sd_trns`, `spibsc_ioset_drv`,
   `spibsc_flash_userdef`, `r_usb_rz_mcu`). Flash-test SD streaming + the relevant
   path after **each**.
4. **Retire the manual pump:** once no live caller remains, delete `routineForSD`'s
   app-pumping body (and the dead `#else` legacy paths). Keep a thin
   `routineForSD()` only if some path still needs a conditionless service tick;
   otherwise drop it from `storage_wait.h`.
5. **`loadAnyEnqueuedClustersRoutine`** per decision 4.
6. **Verify:** `grep` shows the HAL has no `routineForSD`/`yieldingRoutine…`/
   `loadAnyEnqueued…`/`scheduler_api`/app-header edges; the platform-boundary lint and
   the full `app → BSP → HAL → foundation` stack are clean. Optionally remove the now-
   meaningless dead `#if defined(USB_CFG_HHID_USE)` HID includes for a tidy finish.

## Risks / hardware test matrix

- **SD streaming under load:** play multiple samples streamed from the SD card; no
  audio dropouts, clicks, or underruns while reading.
- **Long blocking storage ops:** load/save a large song, browse big sample folders;
  UI stays responsive, no hangs, encoders/pads keep working.
- **SPI flash:** firmware/settings read-write paths still complete.
- **USB during SD activity:** USB MIDI traffic while streaming from SD (the
  `r_usb_rz_mcu` `routineForSD` cross-call) — no stalls in either.
- **Reentrancy:** the SD → audio → USB → SD nesting the lock guards against must not
  deadlock or double-enter; exercise simultaneous SD + USB + audio.
- **Timing change:** compare UI cadence / responsiveness during long reads before vs
  after — the manual-pump round-robin → scheduler yield is the main behavioural risk.

## Outcome (what landed)

Done on `feat/libdeluge-storage-yield`, one commit per logical step, each build-green.
**Not yet hardware-tested** — the whole change is behaviour-sensitive and the matrix
above must be run on a Deluge before merge.

Decision 3 was refined around the strategic target (an eventual Embassy-backed
HAL/BSP, see `deluge-embassy`). Embassy expresses a storage wait as either
`poll_fn` awaiting an ISR-set waker ("wait until this hardware predicate") or
`Timer::after` ("sleep this long") — never a "pump the app" / "drain the scheduler"
call, because the executor runs other work while you `.await`. So the cleanest
long-term shape is to make every site state its *intent* and to **eliminate** the
conditionless pump (it has no async analog), rather than keep a `yieldToIdle`-style
service tick. Each site then ports to Embassy mechanically.

What shipped, by phase:
1. Dead `OSLikeStuff/scheduler_api.h` include removed from `sd_dev_low.c`.
2. `yieldingRoutineForSD` / `…WithTimeoutForSD` relocated from `deluge.cpp` into the
   scheduler foundation (`task_scheduler_c_api.cpp`); they carry the ISR
   (`intc_func_active`) and reentrancy guards. `<libdeluge/storage_wait.h>` stays the
   contract.
3. Each `routineForSD()` site converted to declare its wait:
   `spibsc_wait_tend` / `Userdef_SFLASH_Busy_Wait` → `yieldingRoutineForSD(hw_ready)`
   (→ Embassy `poll_fn`); `usb_cpu_delay_1us/_xms` → keep the ISR-safe timer loop, swap
   the pump for `yieldingRoutineForSD(timer_done)` (→ Embassy `Timer::after`);
   `sd_read`/`sd_trns` pre-pumps deleted (redundant — the transfer's own waits yield);
   the app's `FileWriter` pump → `yieldToIdle`. (Captured state lives in file statics,
   the C mirror of a poll closure — the pattern `sd_dev_low.c` already used.)
4. `routineForSD()` retired: body, the `UIStage` enum, the `storage_wait.h` decl, and
   the dead non-`USE_TASK_MANAGER` `#else` legacy paths all deleted. The boundary now
   exposes only condition/timeout yields.
5. Decision 2 (cleaner fix): the reentrancy flag is now scheduler-owned, exposed as
   `isSDRoutineActive()` in the scheduler C API; the ~25 UI/app readers query it, the
   global `sdRoutineLock` is gone from `deluge.cpp`/`extern.h`, and `resource_checker.h`
   dropped its `<extern.h>` include (a removed foundation→app edge).
6. Decision 4 = B: the FatFs `disk_read`/`disk_write` porting symbols moved from
   `RZA1/diskio.c` into `audio_file_manager.cpp` — they service the cluster-streaming
   queue then call *down* into the plain `disk_*_without_streaming_first` sector I/O.
   The `loadAnyEnqueuedClustersRoutine()` upcall is deleted. Behaviour-identical (same
   stream-then-read order); only the definition's layer moved, flipping app→HAL.

Result: the HAL/BSP names no app code — its only storage-concurrency edge is the
`<libdeluge/storage_wait.h>` boundary (two condition/timeout yields). The
platform-boundary lint passes with no new violations; `app → BSP → HAL → foundation`
has no upward edges left on this path. (One sanctioned downward edge: the relocated
wrappers in the scheduler foundation read `intc_func_active` — HAL state — for the ISR
guard, as intended.)
