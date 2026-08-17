# Collapsing the I/O-class fiber waits and deleting the off-fiber rejection gates

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop requiring the worker fiber for filesystem I/O — delete the 27 `on_fiber()`
rejection gates and turn the device's synchronous FS C-ABI into non-yielding spins — so
storage works from any thread-mode context and the fiber's I/O role is genuinely gone.

**Architecture:** The device's sync-to-async FS bridge (`efatfs_fs.rs`, 27 wrappers) currently
refuses any off-fiber caller and otherwise suspends the fiber. Both go: the rejection is
deleted and `block_on_fiber` becomes `sim_block::block_on`, a non-yielding spin whose transfers
complete by SDHI/DMA interrupt. The host mirror (`efatfs_host_shim.rs`) is collapsed the same
way so the host gates exercise the shape the device will actually run — without that symmetry
every host gate would be testing the old shape. Two supporting pieces make that safe: a
progress hook for threaded hosts, and an orphan filter the golden harness needs before any
`.cpp` moves.

**Tech Stack:** Rust (embassy-executor / embassy-futures / embassy-sync), the `deluge_efatfs_*`
C ABI, `./dbt` for builds and harnesses, `cargo device --release` for the device link.

**Spec:** `docs/superpowers/specs/2026-08-07-r5a-fiber-io-retirement-design.md` §4 Phase 2
(and §2 for why Phase 1 had to come first). Ladder context:
`docs/superpowers/specs/2026-08-08-storage-execution-model-end-state-design.md` §3 (the bugs
this unblocks) and §5.

## Global Constraints

- **Branch first.** Work on a new branch. Never commit to `main`.
- **Never `git commit --amend`.** The clang-format/rustfmt pre-commit hooks reformat files and
  fail the commit; recover with a **fresh** commit.
- **No `Co-Authored-By` / "Generated with Claude"** trailers.
- **No process labels in commit subjects** — no "Task N", "Phase N", "rung".
- **Never pass `update` to any golden script.** `scripts/golden_embassy_diff.sh` runs with
  `check` only.
- **Device build is `cargo device --release` from `src/bsp/rust`.** `./dbt rust` is a host build
  and does not prove the device links. `./dbt build Debug` currently fails its device link on a
  **pre-existing** `.ARM.exidx` / `.rtt_cached_reserve` overlap — do not chase it, and do not
  treat it as caused by this work.
- **The device build needs the local `[patch]`** added in `26bd2b98f` (deluge-bsp + rza1l-hal
  redirected to the sibling deluge-sdk checkout). It is marked local-only in the manifest; leave
  it in place, do not "clean it up".
- **Firmware C++ is `./dbt build Debug`** (or `cmake --build build --target deluge_app` for just
  the app library), unit tests `./dbt test`, harnesses `./dbt harness run <name>`. Never run
  cargo by hand inside a harness directory — use the harness's own entry script.
- **Decision already taken (Kate, 2026-08-16): full collapse.** The device wrappers become
  non-yielding spins, not `if on_fiber() { block_on_fiber } else { block_on }`. The cost is
  accepted and must be stated, not hidden: every task-context FS transfer now holds thread mode
  for its duration, so PIC/OLED/UI tasks stall during a save, load, or directory read. Streaming
  is unaffected (it is on `STREAM_EXEC` since Phase 1). The answer to the responsiveness cost is
  R5b's `Owner::await`, **not** reinstating the yield.

---

## What this rung actually touches (read before Task 1)

The spec says "classify all 66 `block_on_fiber` sites". Having classified them, the production
change is narrower and the split is not what the site count suggests:

| Where | Sites | Class | Action |
|---|---|---|---|
| `efatfs_fs.rs` (device-only, `#![cfg(target_os = "none")]`) | 30 | I/O-class, all behind an `on_fiber()` rejection | **collapse** (Task 3) |
| `efatfs_host_shim.rs` (host mirror) | 30 | I/O-class, **already** `if on_fiber() { block_on_fiber } else { block_on }`, no rejection gate | **collapse for symmetry** (Task 4) |
| `sd.rs` | 4 | I/O-class block transfers, already on_fiber-gated dispatch | leave — they sit *below* the FS layer and their dispatch is already correct |
| `streaming_loader.rs` | 2 | **progress-class** (`WaitChunkLoaded`, `WaitQueueDrained`) | leave — R5b's inheritance, as the spec says |

**Two corrections to carry into the docs (Task 6).**

1. **"The 29 gates" over-counts by two.** The 29 `!on_fiber()` matches are **27** rejection
   gates in `efatfs_fs.rs` plus **2** in `sd.rs` that are a different thing entirely —
   `!on_fiber() && sim_latency::off_fiber_instant()`, a host-sim latency exemption, not a
   rejection. Phase 2 deletes 27. Phase 3 deletes the other two along with
   `off_fiber_instant` itself.
2. **This rung fixes a second tracked bug the spec does not credit it with.**
   `deluge.cpp:741 runtimeFeatureSettings.readSettingsFromFile()` runs inside `deluge_boot`,
   which `deluge_app_init` calls synchronously **before** the worker-fiber pump exists — so on
   the device that SD read is off-fiber, hits a rejection gate, and silently returns failure.
   That is the `boot-settings-silently-lost-on-embassy` bug (recorded as LIVE). Deleting the
   gates fixes it, and the boot context is single-threaded so a spin there is safe.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/bsp/rust/harness/golden_vt_render/build.rs` | Orphan-filtered object archiving + the build-dir provenance guard it requires | 1 |
| `src/bsp/rust/src/sim_block.rs` | A named threaded-host progress hook, plus its doc rationale | 2 |
| `src/bsp/rust/tests/sim_block_host.rs` | Test that the threaded-host hook makes a cross-thread-satisfied spin complete | 2 |
| `src/bsp/rust/src/main.rs` (host block) | Register the threaded-host hook | 2 |
| `src/bsp/rust/src/efatfs_fs.rs` | 27 gates deleted; 30 `block_on_fiber` → `sim_block::block_on` | 3 |
| `src/bsp/rust/src/efatfs_host_shim.rs` | 30 sites collapsed to match the device shape | 4 |
| `src/bsp/rust/harness/lens1_vt_sim/sweep.sh` + the two specs + memory | The rung's evidence and the two corrections | 5, 6 |

---

## Task 1: Orphan-filter the golden harness's object archiving

Scheduled before any collapse because Tasks 3 and 4 will move code between files, and this
harness silently archives stale `.o` files whose `.cpp` no longer exists (ninja never removes
them). The spec's Known-uncovered lists it as owed *before* Phase 2 for exactly this reason.

**Files:**
- Modify: `src/bsp/rust/harness/golden_vt_render/build.rs:73-74` (the `collect_objs` call) and
  its `fn collect_objs` definition

**Interfaces:**
- Produces: `collect_objs(base, dir, repo_root, out, skipped)` and
  `assert_build_dir_matches_repo_root(build_dir, repo_root)` in that build script — same
  signatures as `harness/lens1_vt_sim/build.rs:243` and `:285`, deliberately, so the two copies
  stay diffable.

- [ ] **Step 1: Read the reference implementation**

`harness/lens1_vt_sim/build.rs` already has both functions — `collect_objs` at line 243 and
`assert_build_dir_matches_repo_root` at line 285. Read both in full before editing. They are a
pair: the orphan filter resolves each object's derived source path against `repo_root`, so if
`repo_root` is wrong it treats the *entire* closure as orphaned and archives nothing, surfacing
as a wall of undefined symbols with no obvious cause. That is what the provenance guard exists
to prevent. Do not port one without the other.

- [ ] **Step 2: Prove the current build script has the defect**

Create a fake orphan object and confirm it gets archived today:

```bash
cd /home/kate/GitHub/DelugeFirmware
OBJS=$(find build-embassy-hostapp -type d -name 'deluge_app.dir' | head -1)
touch "$OBJS/zz_orphan_probe.cpp.o"
cd src/bsp/rust/harness/golden_vt_render && cargo build 2>&1 | grep -c 'zz_orphan_probe'
```

Expected: `0` — no warning, because the unfiltered `collect_objs` archives it silently. That
silence is the defect. (`ar` tolerates a zero-byte member, so this probe does not break the
build; remove it in Step 5.)

- [ ] **Step 3: Port both functions**

Replace `golden_vt_render/build.rs`'s `fn collect_objs` with lens1's five-argument version,
changing only the `cargo:warning=` crate label from `lens1_vt_sim:` to `golden_vt_render:`. Add
`assert_build_dir_matches_repo_root` verbatim. Update the call site at line 74 to pass the new
arguments and to call the guard first, mirroring lens1's own call site — read how lens1 invokes
them rather than inventing an order.

Update the file's header comment (lines 1-11), which currently advertises itself as "a trimmed
copy of `../build.rs`'s … `collect_objs`" — it is now a copy of *lens1's* orphan-filtered
version, and the header should say so and say why (Phase 2 moves `.cpp` files).

- [ ] **Step 4: Verify the filter now fires**

```bash
cd src/bsp/rust/harness/golden_vt_render && cargo build 2>&1 | grep 'zz_orphan_probe'
```

Expected: a `cargo:warning=golden_vt_render: skipping orphan object …` line naming the probe.

- [ ] **Step 5: Remove the probe and confirm the goldens are unchanged**

```bash
cd /home/kate/GitHub/DelugeFirmware
rm -f $(find build-embassy-hostapp -name 'zz_orphan_probe.cpp.o')
./dbt harness run golden-embassy-diff 2>&1 | tail -3
scripts/golden_embassy_diff.sh icoustic check 2>&1 | tail -2
```

Expected: `PASS — cordae MIXDOWN matches golden (db63b128be9748ed…)` and
`PASS — icoustic MIXDOWN matches golden (a565391b293e9d88…)`. A build-script change must not
move the output; if either diverges, the orphan filter is dropping a live object — check the
provenance guard's `repo_root` before anything else.

- [ ] **Step 6: Commit**

```bash
git add src/bsp/rust/harness/golden_vt_render/build.rs
git commit -m "build(golden): skip orphan objects when archiving the host app closure

The golden harness archived every .o under deluge_app.dir, including ones whose
.cpp had been deleted — ninja never removes them. That is harmless until source
files start moving, at which point a stale object silently wins the link.

Port lens1's orphan filter, which derives each object's source path and skips
what no longer exists, together with the build-dir provenance guard it depends
on: resolved against the wrong tree the filter would treat the whole closure as
orphaned and archive nothing. Both goldens still byte-identical."
```

---

## Task 2: A progress hook for threaded hosts

Needed **before** Task 4. Once the host shim spins through `sim_block::block_on`, the no-hook
path applies: `sim_block.rs:38` documents `None` as "a `Pending` future is immediately a wedge,
which is the correct behaviour for a host build with no harness", and the spin asserts after
100 000 consecutive stalled polls. On a *threaded* host that verdict is wrong — a `Pending` there
is routinely satisfied by another OS thread, and since Phase 1 the fill task is on its own
thread holding the FS mutex across awaits, so cross-thread contention is now normal. A spin
iteration is nanoseconds, so 100 000 of them is well under a millisecond: a contended file read
would trip the budget spuriously.

**Files:**
- Modify: `src/bsp/rust/src/sim_block.rs` (add the hook + doc)
- Modify: `src/bsp/rust/tests/sim_block_host.rs` (the test)
- Modify: `src/bsp/rust/src/main.rs` (host block — register it)

**Interfaces:**
- Consumes: `sim_block::set_progress_hook(hook: fn() -> Progress)` and
  `sim_block::Progress::{Advanced, Stalled}` (existing).
- Produces: `sim_block::use_threaded_host_hook()` — registers a hook that yields the OS thread
  and reports `Progress::Advanced`. Called once at start-up by any host build whose spins are
  satisfied by other threads.

- [ ] **Step 1: Write the failing test**

Add to `src/bsp/rust/tests/sim_block_host.rs`. Note the existing file serialises hook mutation
behind a lock (see its comment at line 22) — take the same lock this test file already uses for
`block_on_completes_when_hook_makes_progress`, or the tests stomp on each other's hook.

```rust
#[test]
fn threaded_host_hook_completes_a_spin_another_thread_satisfies() {
    // The shape every host-shim spin has after the I/O-class collapse: this thread spins on a
    // flag only ANOTHER OS thread can set. Without a hook, `block_on` treats every Pending as
    // a stall and trips its budget; the threaded-host hook yields instead, which on a threaded
    // host is real progress.
    let _guard = hook_lock();
    static DONE: AtomicBool = AtomicBool::new(false);
    DONE.store(false, Ordering::SeqCst);

    sim_block::use_threaded_host_hook();
    // Deliberately small: if the hook did NOT count as progress, this budget would trip long
    // before the sleeping thread lands, so a pass means the hook is what carried the spin.
    sim_block::set_spin_budget(50);

    let h = std::thread::spawn(|| {
        std::thread::sleep(std::time::Duration::from_millis(50));
        DONE.store(true, Ordering::SeqCst);
    });

    sim_block::block_on(core::future::poll_fn(|_cx| {
        if DONE.load(Ordering::SeqCst) {
            core::task::Poll::Ready(())
        } else {
            core::task::Poll::Pending
        }
    }));

    h.join().unwrap();
    assert!(DONE.load(Ordering::SeqCst));
}
```

If `hook_lock()` is not the existing helper's name, use whatever that file already uses; read it
first rather than adding a second lock.

- [ ] **Step 2: Run it to verify it fails**

Run: `cd src/bsp/rust && cargo test --test sim_block_host 2>&1 | tail -15`
Expected: FAIL to compile — `cannot find function 'use_threaded_host_hook' in module 'sim_block'`.

- [ ] **Step 3: Add the hook**

In `src/bsp/rust/src/sim_block.rs`, after `set_spin_budget`:

```rust
/// Register the progress hook appropriate to a **threaded** host: yield the OS thread and
/// report progress. Call once at start-up. No-op on device.
///
/// The default no-hook behaviour treats a `Pending` future as an immediate wedge, which is
/// right for a single-threaded cooperative sim (nothing else can run, so nothing can satisfy
/// it) and wrong for a threaded host, where a `Pending` is routinely satisfied by another
/// thread — the streaming fill task holds the FS mutex across its SD await on its own thread,
/// so cross-thread contention is the normal case. A spin iteration is nanoseconds, so the
/// 100_000-poll default budget is under a millisecond and a contended read would trip it
/// spuriously.
///
/// The trade is deliberate and worth knowing: because this hook always reports `Advanced`, it
/// does NOT detect a genuinely unsatisfiable spin. That is fine — wedge detection on a threaded
/// host belongs to the wall-clock timeouts and TSan, and `lens1_vt_sim` keeps the strict
/// budget-based detector (its charter, see the R5a spec §3). Do not install this hook in a
/// single-threaded sim: it would silently disarm the detector there.
#[cfg(not(target_os = "none"))]
pub fn use_threaded_host_hook() {
    fn hook() -> Progress {
        std::thread::yield_now();
        Progress::Advanced
    }
    host::set_hook(hook);
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cd src/bsp/rust && cargo test --test sim_block_host 2>&1 | tail -10`
Expected: PASS, and the two pre-existing tests in that file still pass.

- [ ] **Step 5: Register it in both threaded-host entry points**

In `src/bsp/rust/src/main.rs`'s host block, immediately before the `"deluge-audio"` thread is
spawned (so it is in place before any thread can issue a transfer):

```rust
        // This host runs audio, streaming and the app on three real OS threads, so a Pending
        // future here is satisfied by another thread rather than by nothing. Install the
        // yielding hook, or `sim_block::block_on`'s default no-hook verdict would call a
        // normally-contended read a wedge. See `sim_block::use_threaded_host_hook`.
        crate::sim_block::use_threaded_host_hook();
```

**CORRECTION (made during execution, 2026-08-16): do NOT install the hook in
`harness/golden_vt_render/src/main.rs`.** The plan originally called for it on the grounds that
`SD_BUS` could be contended by the fill task "on its own thread in the BSP it links". That is
wrong: `golden_vt_render` has its own `main` and does **not** use the BSP's threaded host
start-up — it spawns `boot_task`, `sim_latency::pump` and `streaming_fill_task` on a single
executor driven by its own discrete-event loop. It is genuinely single-threaded, so no other
thread exists to satisfy a `Pending`, and yielding would accomplish nothing. Installing the hook
there would do exactly what `use_threaded_host_hook`'s own doc forbids — silently disarm wedge
detection in a single-threaded sim.

So `golden_vt_render` keeps the **no-hook default**, which is the case that default was designed
for: if its FS path ever does hit an unsatisfiable spin, it should fail loudly with
`sim_block`'s wedge message rather than hide it. That is a visible gate failure, which is the
outcome we want.

The residual risk this leaves is real and is checked, not assumed: `golden_vt_render` today
reaches storage through the shim's off-fiber `embassy_futures::block_on`, which never wedges
however long it spins. Task 4 routes that through `sim_block::block_on`, which does wedge. If any
of its FS futures pend even once, Task 4 turns a working spin into a panic. Task 4 Step 4 runs
both goldens precisely to find out; a wedge there is a finding to report and reconsider, not to
paper over with a hook.

- [ ] **Step 6: Verify every host build still passes**

```bash
cd src/bsp/rust && cargo test 2>&1 | grep -E 'test result: FAILED|^error' | head
cd /home/kate/GitHub/DelugeFirmware && ./dbt harness run golden-embassy-diff 2>&1 | tail -2
```
Expected: no failures; cordae still `PASS … db63b128be9748ed…`.

- [ ] **Step 7: Commit**

```bash
git add src/bsp/rust/src/sim_block.rs src/bsp/rust/tests/sim_block_host.rs \
        src/bsp/rust/src/main.rs
git commit -m "feat(bsp): give threaded hosts a yielding progress hook for storage spins

sim_block's no-hook default calls any Pending future a wedge. That is right for a
single-threaded cooperative sim, where nothing else can run, and wrong for a host
with three real OS threads — where the streaming fill task holds the FS mutex
across its SD await, so cross-thread contention is the normal case and the
100k-poll budget is under a millisecond of spinning.

Add an explicit yielding hook for threaded hosts and install it in the one such
entry point (the threaded host BSP). golden_vt_render is single-threaded and keeps the
no-hook default deliberately. It reports progress unconditionally and so does not detect a truly
unsatisfiable spin; that is the intended trade, because wedge detection there
belongs to the wall-clock timeouts and TSan, while lens1 keeps the strict
budget-based detector its charter assigns it."
```

---

## Task 3: Collapse the device FS bridge

**Files:**
- Modify: `src/bsp/rust/src/efatfs_fs.rs` — 27 rejection gates deleted, 30 `block_on_fiber`
  calls replaced

**Interfaces:**
- Consumes: `crate::sim_block::block_on` (existing; on device it is exactly
  `embassy_futures::block_on`).
- Produces: no signature changes — every `deluge_efatfs_*` C-ABI entry point keeps its exact
  signature and return contract. What changes is that off-fiber callers now succeed.

- [ ] **Step 1: Enumerate the sites so none is missed**

```bash
cd /home/kate/GitHub/DelugeFirmware/src/bsp/rust/src
grep -n 'if !crate::fiber::on_fiber()' efatfs_fs.rs | wc -l      # expect 27
grep -n 'crate::fiber::block_on_fiber(' efatfs_fs.rs | wc -l     # expect 30
```

Record both numbers. They differ by exactly three, and the three are worth understanding before
you touch anything:

| Site | Function |
|---|---|
| `:282` | `deluge_efatfs_close(handle: u32)` |
| `:694` | `deluge_efatfs_file_close(handle: u32)` |
| `:791` | `deluge_efatfs_dir_close(handle: u32)` |

All three return `()`, so there was no failure value to reject an off-fiber caller with, and they
were left **ungated** — they are the one way an off-fiber caller can already reach
`block_on_fiber` on the device today. **That is a latent pre-existing hazard, not a harmless
asymmetry:** their futures take the handle-table mutex and can return `Pending`, at which point
`block_on_fiber` calls `yield_now()`, whose own SAFETY comment (`fiber.rs:349`) states it is
"only called while executing on the fiber (on_fiber() == true)" — off-fiber it switches to a
context that isn't suspended (device) or reads a stale `CURRENT_YIELDER` (host). Reachable by any
C++ path that opens on-fiber and closes off-fiber.

The collapse removes that hazard as a side effect, since a spin is safe from any context. Say so
in the commit message — it is a real fix, not a refactor.

Do not assume a 1:1 gate-to-`block_on_fiber` pairing, and re-run both counts after editing.

- [ ] **Step 2: Delete only the `on_fiber` term of each gate**

The gates are **compound**. Nine of them also null-check out-parameters, e.g.
`efatfs_fs.rs:301`:

```rust
    if !crate::fiber::on_fiber() || dst.is_null() || out_read.is_null() {
        return false;
    }
```

must become

```rust
    if dst.is_null() || out_read.is_null() {
        return false;
    }
```

Losing a null check here is a device memory-safety bug in a C-ABI boundary, so treat this as
the risky edit it is: do it one site at a time, and where deleting the `on_fiber` term leaves
the condition empty (the 18 gates that check nothing else, e.g. `:535`, `:567`, `:596`), delete
the whole `if` block rather than leaving `if true`/`if false`.

- [ ] **Step 3: Verify no null check was lost**

```bash
cd /home/kate/GitHub/DelugeFirmware
git diff -U0 src/bsp/rust/src/efatfs_fs.rs | grep '^-' | grep -c 'is_null'
git diff -U0 src/bsp/rust/src/efatfs_fs.rs | grep '^+' | grep -c 'is_null'
```
Expected: the two counts are **equal**. Every `is_null` that leaves a removed line must reappear
on an added line. If the `+` count is lower, a null check was dropped — find it before going on.

- [ ] **Step 4: Replace the fiber waits with a spin**

Replace all 30 `crate::fiber::block_on_fiber(` with `crate::sim_block::block_on(` in this file:

```bash
cd /home/kate/GitHub/DelugeFirmware/src/bsp/rust/src
sed -i 's/crate::fiber::block_on_fiber(/crate::sim_block::block_on(/g' efatfs_fs.rs
grep -c 'crate::sim_block::block_on(' efatfs_fs.rs   # expect 30
grep -c 'block_on_fiber' efatfs_fs.rs                # expect 0 outside comments
```

Then update this file's module doc (lines 1-19) and every doc comment that still promises the
old contract. At minimum the phrase pattern "requires `crate::fiber::on_fiber()`" and any
"must be called on the worker fiber" wording is now false. Replace it with what is true: these
entry points may be called from any thread-mode context; the transfer completes by SDHI/DMA
interrupt; the caller's context is held (not yielded) for its duration, so thread-mode tasks do
not run during a transfer — and that is the accepted cost, whose answer is `Owner::await`, not
reinstating the yield.

- [ ] **Step 5: Build for the device**

Run: `cd src/bsp/rust && cargo device --release 2>&1 | grep -E '^(error|warning: unused)' -A 4 | head -30`

Expected: no errors. Expect possible new `unused` warnings — if `crate::fiber` or `on_fiber` is
now unused *in this file*, remove that import, but do not delete anything from `fiber.rs`:
`block_on_fiber` is still used by `sd.rs` (4) and `streaming_loader.rs` (2), and `yield_until` is
R5b's.

- [ ] **Step 6: Confirm the C++ side and the specs are unaffected**

```bash
cd /home/kate/GitHub/DelugeFirmware
cmake --build build --target deluge_app 2>&1 | grep -cE '^\s*error'   # expect 0
./dbt test 2>&1 | tail -3                                             # expect 39/39
```

This file is device-only, so neither of these exercises the change — they are here to catch an
accidental edit outside the `target_os = "none"` cfg. Say so when reporting; do not present them
as evidence the collapse works.

- [ ] **Step 7: Commit**

```bash
git add src/bsp/rust/src/efatfs_fs.rs
git commit -m "fix(storage): let any context do file I/O, and stop suspending the fiber for it

Every synchronous FS entry point rejected off-fiber callers outright and otherwise
suspended the worker fiber mid-transfer. The rejection is why saving cannot
succeed off-owner on this BSP, and why runtimeFeatureSettings.readSettingsFromFile()
silently fails at boot — deluge_boot runs before the fiber pump exists, so that
read was refused rather than performed.

Delete the 27 rejections and make the wait a non-yielding spin whose transfer
completes by SDHI/DMA interrupt. Streaming is unaffected: it runs on its own
interrupt executor. The cost is real and accepted — thread mode is held for the
duration of a transfer, so PIC/OLED/UI tasks do not run during a save, load or
directory read. The answer to that is a genuinely async Owner::await, not
reinstating the yield.

Compound gates kept their null checks; only the on_fiber term was removed.

Also closes a latent hazard: the three void-returning close entry points had no
gate to reject an off-fiber caller with, so they could already reach
block_on_fiber off-fiber, where yield_now's own contract does not hold. A spin is
safe from any context."
```

---

## Task 4: Collapse the host mirror to match

Without this, every host gate exercises the *old* shape and proves nothing about Task 3.
`efatfs_fs.rs` is `#![cfg(target_os = "none")]`, so no host harness compiles a single line of
it; `efatfs_host_shim.rs` is what lens1, `golden_vt_render`, TSan and `streaming_fill_host`
actually run.

**Files:**
- Modify: `src/bsp/rust/src/efatfs_host_shim.rs` — 30 sites

**Interfaces:**
- Consumes: `crate::sim_block::block_on` and `sim_block::use_threaded_host_hook` (Task 2).
- Produces: no signature changes.

- [ ] **Step 1: Read one site and confirm the current shape**

`efatfs_host_shim.rs:317` is representative:

```rust
    let opened = if crate::fiber::on_fiber() {
        crate::fiber::block_on_fiber(open(path))
    } else {
        embassy_futures::block_on(open(path))
    };
```

Note what is already true here and unlike the device: there is **no rejection gate**, and the
off-fiber branch already spins. So this task removes a branch rather than a gate.

- [ ] **Step 2: Collapse each site to the single spin**

Each becomes:

```rust
    let opened = crate::sim_block::block_on(open(path));
```

Two things to get right, and they are why this is not a blind `sed`:
- the `if`/`else` is an **expression** at some sites and a **statement** at others (e.g. the
  close path at `:355`); collapsing an expression site needs the `let … =` kept, a statement
  site does not.
- `embassy_futures::block_on` must become `crate::sim_block::block_on`, not stay as-is —
  otherwise the host keeps using the hookless spin and Task 2's hook never applies.

Work site by site:

```bash
cd /home/kate/GitHub/DelugeFirmware/src/bsp/rust/src
grep -n 'crate::fiber::on_fiber()' efatfs_host_shim.rs   # the branch sites to collapse
grep -n 'embassy_futures::block_on' efatfs_host_shim.rs  # must all become sim_block::block_on
```

- [ ] **Step 3: Verify the collapse is complete and consistent**

```bash
cd /home/kate/GitHub/DelugeFirmware/src/bsp/rust/src
grep -c 'block_on_fiber' efatfs_host_shim.rs            # expect 0 outside comments
grep -c 'embassy_futures::block_on' efatfs_host_shim.rs # expect 0
grep -c 'crate::sim_block::block_on' efatfs_host_shim.rs # expect 30
```

Then update the module doc (lines 20-60), which currently describes the "`on_fiber()`-gated
`block_on_fiber`/`block_on` dispatch" as the design — it is now a single unconditional spin, and
the doc should say that the point of the change is that this file and `efatfs_fs.rs` run the
*same* shape so the host gates mean something.

- [ ] **Step 4: Run every host gate**

```bash
cd /home/kate/GitHub/DelugeFirmware/src/bsp/rust && cargo test 2>&1 | grep -E 'test result: FAILED|^error' | head
cd harness/lens1_vt_sim && ./sweep.sh selftest 2>&1 | grep -E 'PASS|FAIL|SELFTEST'
cd /home/kate/GitHub/DelugeFirmware
./dbt harness run golden-embassy-diff 2>&1 | tail -2
scripts/golden_embassy_diff.sh icoustic check 2>&1 | tail -2
./dbt harness run preemptive-race-tsan 2>&1 | grep -E 'open_findings|UNCATALOGUED' | tail -6
./dbt harness run fs-differential 2>&1 | tail -2
```

Expected: BSP suite green; all four spin selftests PASS; both goldens byte-identical
(cordae `db63b128be9748ed…`, icoustic `a565391b293e9d88…` — read the hashes off the output rather
than trusting these); `fs-differential` green.

On TSan, expect `open_findings=0` and **one known uncatalogued site**:
`deluge_debug_now_frames` (`resource_clock.cpp:31`) racing `deluge_app_render` over
`AudioEngine::audioSampleTimer`. That one is pre-existing, diagnostic-only and benign on device.
**Anything else uncatalogued is a finding from this task** — report it with both stacks and
treat it as blocking; do not catalogue it to make the gate green.

- [ ] **Step 5: Run the full Lens 1 sweep, which now means something**

Run: `cd /home/kate/GitHub/DelugeFirmware && ./dbt harness run lens1-vt-sim 2>&1 | tail -25`

This is the first rung where Lens 1 has **production** spins going through `sim_block::block_on`
— before this, its own merge note recorded "0 production sites through `sim_block`". Its job is
wedge detection: the run must reach a `LENS1_RESULT` line without wedging, and no
"exhausted from INSIDE a non-yielding spin" message may appear.

Expect the run to still exit non-zero on `CONTROL_A: FAIL — fast=0 slow=0`. That is the
documented structural vacuity of the margin arm, not a regression: `audio_host.rs:163-171`
skips render for any off-fiber caller not on a `"deluge-audio"` thread, and lens1 has no such
thread, so the underrun counters are unreachable at any throughput. Report the selftest and
wedge results, and report the margin arm as vacuous — do not report a margin number.

- [ ] **Step 6: Commit**

```bash
git add src/bsp/rust/src/efatfs_host_shim.rs
git commit -m "refactor(bsp): run one storage-wait shape on host and device

The host shim branched on on_fiber() and yielded the fiber when it could, while
the device bridge is now an unconditional spin. Left alone, every host gate —
lens1's wedge detector, TSan, the goldens — would exercise a shape the device no
longer runs, and prove nothing about the collapse.

Collapse the shim the same way and route it through sim_block::block_on rather
than embassy_futures::block_on, so the threaded-host hook applies. Lens 1 now has
production spins passing through sim_block for the first time, which is what makes
its wedge detection load-bearing here."
```

---

## Task 5: Prove the payoff, honestly

The two bugs this rung exists to fix are **device-only**, because the gates were device-only.
This task establishes exactly what can and cannot be shown without hardware, so nobody later
mistakes a green host suite for a fixed bug.

**Files:**
- Modify: `docs/dev/r5a_phase0_baseline.md` (append a Phase 2 evidence section) — or create
  `docs/dev/r5a_phase2_evidence.md` if that file's scope reads as Phase-0-specific once you see it

**Interfaces:**
- Consumes: the gate results from Tasks 3 and 4.
- Produces: a written evidence record the hardware pass can be checked against.

- [ ] **Step 1: Establish, from code, that both bug paths are now open**

Do this as a reading exercise with cited lines, not an assertion:

```bash
cd /home/kate/GitHub/DelugeFirmware
grep -n 'readSettingsFromFile' src/deluge/deluge.cpp                     # the boot read (:741)
grep -rn 'performSave' --include='*.cpp' src/deluge/gui/ui/save | head -3 # the save path
grep -c 'if !crate::fiber::on_fiber()' src/bsp/rust/src/efatfs_fs.rs      # expect 0
```

Write down: which C++ call sites reached a rejection gate before, which C-ABI function each
lands on, and that the gate count is now zero. That chain is the argument; the hardware pass is
the proof.

- [ ] **Step 2: Write the evidence section**

It must state plainly:
- **What is proven on host:** the collapsed shape does not wedge, race, or change rendered
  output — lens1's four selftests, TSan `open_findings=0`, both goldens byte-identical,
  `fs-differential` green, BSP host suite green.
- **What is NOT proven on host, and why:** `efatfs_fs.rs` is `#![cfg(target_os = "none")]`, so
  no host build compiles the code that actually changed. The host evidence covers the *shape*
  (via the shim, Task 4), not the device file.
- **The two device checks owed:** (a) save a song to a name that already exists and confirm the
  overwrite prompt appears and the save completes — the `save-overwrite-prompt-broken-again`
  bug; (b) set a runtime feature setting, power-cycle, and confirm it persisted — the
  `boot-settings-silently-lost-on-embassy` bug, which fails today because `deluge_boot` reads
  settings off-fiber.
- **The responsiveness change to listen for:** UI/LED/OLED stalling during a save, load or
  directory read, which is expected now and is R5b's to fix. Note that "expected" is not
  "acceptable at any magnitude" — if it is bad enough to feel broken, that is worth knowing
  before R5b.

- [ ] **Step 3: Commit**

```bash
git add docs/dev/
git commit -m "docs(storage): record what the I/O-class collapse proves and what it owes

The gates were device-only, so the bugs they caused are device-only and no host
suite can show them fixed. Write down the split explicitly: host gates cover the
collapsed shape via the shim, hardware covers the two bug paths — off-owner save
with an overwrite prompt, and a runtime setting surviving a power cycle. Also
names the responsiveness change to listen for, since thread mode is now held for
the duration of every transfer."
```

---

## Task 6: Update the specs and correct the gate count

**Files:**
- Modify: `docs/superpowers/specs/2026-08-07-r5a-fiber-io-retirement-design.md` §4 Phase 2
  (untracked/gitignored — edit on disk; the durable record is the tracked spec below)
- Modify: `docs/superpowers/specs/2026-08-08-storage-execution-model-end-state-design.md` §3,
  §5, §6

- [ ] **Step 1: Correct "the 29 gates" wherever it appears**

Both specs say 29. It is 27 rejection gates in `efatfs_fs.rs` plus 2 in `sd.rs` that are
`!on_fiber() && sim_latency::off_fiber_instant()` — a host-sim latency exemption, not a
rejection, and Phase 3's to delete along with `off_fiber_instant`. Fix the figure in §3's
mechanism column, §5's Phase 2 row, and §6's exit criteria, and add a one-line note saying what
the two leftovers are so the count is not "corrected" back later.

- [ ] **Step 2: Credit the boot-settings bug**

§3 lists two symptoms and Task 5 established a third that the specs do not connect to this
rung: `deluge.cpp:741 runtimeFeatureSettings.readSettingsFromFile()` runs inside `deluge_boot`,
before the fiber pump exists, so on device it was refused rather than performed — the
`boot-settings-silently-lost-on-embassy` bug. Add it to §3's table with its mechanism, and note
that the boot context is single-threaded so a spin there is safe by construction.

- [ ] **Step 3: Mark Phase 2 done in the ladder and record the collapse decision**

In the end-state spec §5, mark the Phase 2 row done with: the 27-site device collapse, the
symmetric host-shim collapse and *why* it was required (device file is
`#![cfg(target_os = "none")]`, so host gates would otherwise test the old shape), the
threaded-host hook, and the accepted responsiveness cost with `Owner::await` named as its
answer. Update §6's exit criteria: "zero `on_fiber()` rejection gates" is now satisfiable —
check it, and say the remaining `on_fiber()` uses are `sd.rs`'s dispatch and the two
progress-class waits.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-08-08-storage-execution-model-end-state-design.md
git commit -m "docs(storage): correct the gate count and credit the boot-settings bug

'The 29 gates' counted 27 off-fiber rejections in the device FS bridge together
with 2 sim-latency exemptions in sd.rs that are a different mechanism and belong
to the next rung. Say 27, and say what the other two are so the figure is not
restored later.

Also connects a third tracked bug to this rung: deluge_boot reads runtime feature
settings off-fiber, before the fiber pump exists, so the rejection made that read
fail silently rather than perform it."
```

---

## Gates for the whole rung

| Gate | Command | What it proves |
|---|---|---|
| Device link | `cd src/bsp/rust && cargo device --release` | the collapsed device bridge compiles and links |
| Firmware C++ | `cmake --build build --target deluge_app` | no accidental edit outside the device cfg |
| Unit specs | `./dbt test` | 39/39, no CppSpec regression |
| BSP host suite | `cd src/bsp/rust && cargo test` | the shim collapse and the new hook |
| Lens 1 wedge detection | `./dbt harness run lens1-vt-sim` | **first rung with production spins through `sim_block`** — no wedge, no in-spin budget exhaustion |
| Lens 2 races | `./dbt harness run preemptive-race-tsan` | `open_findings=0`; only the known `deluge_debug_now_frames` site uncatalogued |
| Goldens | `./dbt harness run golden-embassy-diff` + `scripts/golden_embassy_diff.sh icoustic check` | rendered output unchanged through the collapse |
| FS correctness | `./dbt harness run fs-differential` | the FS layer itself is unaffected |
| **Device: off-owner save** | hardware | the `save-overwrite-prompt-broken-again` bug |
| **Device: settings persist a power cycle** | hardware | the `boot-settings-silently-lost-on-embassy` bug |
| **Device: responsiveness during save/load/browse** | hardware | the accepted cost's actual magnitude |

**The payoff is hardware-only.** Every host gate here covers the *shape*, not the device file
that changed. Report the rung as shape-verified and bug-unverified until it has been flashed.

---

## Self-review notes

- **Spec coverage.** §4 Phase 2 asks for: classify all sites (done in "What this rung actually
  touches" — and the classification changed the plan, since 36 of the 66 need no production
  change), collapse I/O-class to a spin (Task 3 device, Task 4 host), leave the two
  progress-class waits documented as R5b's (stated in the same table, recorded in Task 6), and
  re-run the sweep and compare (Task 4 Step 5, with the honest caveat that the margin arm is
  vacuous by charter). The Known-uncovered `collect_objs` prerequisite is Task 1.
- **Two things the spec does not have**, both from reading the code: the host-shim symmetry
  requirement (without it the rung has no host gate at all) and the threaded-host hook (without
  it the shim's spins trip a wedge budget meant for single-threaded sims).
- **Deliberately out of scope.** `SD_BUS`, `off_fiber_instant`, `RESOURCE_SD`, the stale C++
  comments and `deluge_storage_on_owner` (all Phase 3); `yield_until`, `Owner::run`/`await`, the
  two progress-class waits and deleting `fiber.rs` (all R5b); the `deluge-bsp` pin resolution
  and the `.ARM.exidx` Debug-link overflow (pre-existing, tracked in the end-state spec's
  Known-uncovered).
- **Riskiest steps, flagged rather than smoothed over.** Task 3 Step 2 (compound gates — losing
  a null check is a C-ABI memory-safety bug, hence the counting check in Step 3), Task 4 Step 2
  (expression-vs-statement sites make a blind `sed` wrong), and Task 4 Step 4 (a new
  uncatalogued TSan site is blocking, not something to catalogue away).
