# R5a Phase 0 — evidence document (not a margin baseline)

Measured at commit `443c33934` (branch `feat/r5a-phase0-residue`, off `next` @ `f6b88948f`).

## Why this file exists, and what it is NOT

The Phase 0 plan (`docs/superpowers/plans/2026-08-07-r5a-phase0-gate-credibility.md`) originally
intended this file to hold two things: a Task 5 margin-sweep baseline (the numeric threshold
table Phase 2 would diff against) and a Task 6 "0d spike" — converting the R5a design doc's §2
deadlock argument from a claim into an executed observation.

**Task 5 never ran. There is no margin baseline in this file, and there cannot be one from Lens 1
today.** An investigation earlier in this run (recorded in
`.superpowers/sdd/2026-08-07-r5a-phase0-gate-credibility/progress.md`) established that
`lens1_vt_sim` structurally never renders audio: `deluge_app_render` fires exactly once, from
`deluge_boot()`, before `currentSong`/voices exist, and `AudioEngine::routine_task`'s
`should_skip_render()` discards every subsequent render call because Lens 1 never calls
`set_audio_spawner` (it is deliberately single-threaded virtual-time, unlike the real BSP's
audio thread). Consequently `blocks_rendered` is a structural `0` in every Lens 1 run, and the
underrun counters (`noteUnderrunWait`/`noteUnderrunUnassign`) are only reached from
`VoiceSample::render`/`SampleLowLevelReader` code on that unreachable render path. A margin sweep
over a counter that can never move is vacuous — confirmed independently by Control A already
failing at base (`fast=0 slow=0`, expected `slow>0`). **Do not read this file as the Phase 2
comparison artefact the plan originally intended.** That artefact does not exist yet; producing
it needs a new Phase 0 task ("drive Lens 1 render on the fiber at the virtual cadence"), not a
tweak to `sweep.sh`.

What follows is Task 6 only: the deadlock-evidence spike ("0d").

---

## 0d — deadlock evidence (Task 6)

### What this tests

`docs/superpowers/specs/2026-08-07-r5a-fiber-io-retirement-design.md` §2 argues that naively
collapsing `block_on_fiber` → `embassy_futures::block_on` (i.e. deleting the fiber's cooperative
yield without first moving `streaming_fill_task` off the thread/MAIN executor) deadlocks the
device: `streaming_fill_task` holds the FS mutex across an SD-transfer await while parked on
MAIN; a collapsed task-context caller elsewhere spins on that same mutex without yielding; MAIN
never gets polled again, so the fill task can never release the mutex it holds. §3 independently
re-derives the same conclusion from the host-sim emulation's own limits (`progress_hook` can pump
`HP_EXEC` and the virtual clock, but can never poll MAIN from inside MAIN's own `poll()`).

This spike does **not** need Lens 1 to render — it is testing for a WEDGE (a spin that cannot
make progress), not a margin. That is what makes it valid despite the Task 5 blocker above.

**Configuration under test:** task-context FS I/O (browser listing + the #4460 waveform
pre-scan, `AudioFileManager::slowRoutine` → `backgroundWaveformOverviewScan`, priority 21) versus
`streaming_fill_task` — still on the MAIN executor, the pre-Phase-1 configuration §2 describes.
This is **not** contention with playback: Lens 1 never renders (see above), so nothing here is
playback-demanded. `cluster_reads` is the pre-scan's counter; the async fill task's own activity
is separately visible as `on_fiber_reads` in the harness's progress log line.

### The throwaway patch

`src/bsp/rust/src/efatfs_host_shim.rs`, `deluge_efatfs_file_open` (the task-context file-open
C-ABI, the entry point the plan names). Its normal dispatch is:

```rust
    let opened = if crate::fiber::on_fiber() {
        crate::fiber::block_on_fiber(task_file_open(path, mode))
    } else {
        embassy_futures::block_on(task_file_open(path, mode))
    };
```

Patched to force every call — on-fiber or not — through the non-yielding spin unconditionally,
using `sim_block::block_on` (not raw `embassy_futures::block_on`) so a genuine wedge prints
`sim_block`'s informative panic message instead of spinning silently forever:

```rust
    // THROWAWAY (R5a Phase 0 step 0d) — do NOT commit.
    let opened = crate::sim_block::block_on(task_file_open(path, mode));
```

This reproduces the naive-collapse shape: no cooperative yield back to the fiber/executor is
ever available to a task-context FS call, exactly what a literal `block_on_fiber` →
`embassy_futures::block_on` find-and-replace would produce.

### The contention scenario

Built on Task 4's knob (`ScenarioConfig::concurrent_listing_every_blocks`, plumbed through
`LENS1_CONCURRENT_LISTING_EVERY_BLOCKS`): during the post-load wait, repeatedly dispatches
`deluge_scenario_start_song_load` + polls `deluge_scenario_song_load_begin_done()`, **never**
calling `deluge_scenario_commit_song_load()` — real task-context directory/file opens, capped at
10 dispatches per run (Task 4 found and documented a hard, unrelated 15-dispatch UI-stack-abort
ceiling; the cap avoids it, at the cost of a short burst rather than sustained contention — see
Task 4's report, `.superpowers/sdd/2026-08-07-r5a-phase0-gate-credibility/task-4-report.md`).

The plan's literal Step 2 command (`--scenario load-while-streaming`) does not exist in this
codebase — `scenario.rs` has no name-based scenario selector, only a `ScenarioConfig` +
env-var-driven `lens1_vt_sim` binary (see the ledger's "PLAN DRIFT" entry). The commands below are
the real equivalent, reusing `sweep.sh`'s shared-image discipline (its own header comments,
lines 84–104): the real scenario image is `sd_image::pack_golden_fixture`'s ~2.5GB FAT32 image,
which leaks a fresh per-PID copy (~300–570MB real) on every run that doesn't pin
`DELUGE_SD_IMAGE`; Task 4 already hit an ~11GiB `/tmp` incident this way. One pristine golden
image is kept at `/tmp/deluge-lens1-task4-shared.img`; every run below `cp --sparse=always`s a
disposable working copy first.

### Run 1 — default features (`efatfs_streaming`/`async_streaming_loader` OFF)

```
cd src/bsp/rust/harness/lens1_vt_sim
cargo build --release
cp --sparse=always /tmp/deluge-lens1-task4-shared.img /tmp/deluge-lens1-task6-work.img
timeout 180 env DELUGE_SD_IMAGE=/tmp/deluge-lens1-task6-work.img LENS1_FIXTURE=cordae \
  LENS1_BLOCKS=20000 LENS1_STEP_TIMEOUT_S=120 LENS1_VIRTUAL_BUDGET_MS=600000 \
  LENS1_CONCURRENT_LISTING_EVERY_BLOCKS=50 ./target/release/lens1-vt-sim
```

Output (verbatim, only line):

```
LENS1_RESULT blocks_rendered=0 cluster_reads=233 recorder_writes=3 underrun_wait=0 underrun_unassign=0
```

Exit code 0. **No wedge.** Instrumented with a temporary `eprintln!` inside the patched function
(also reverted, not committed) to confirm it was actually exercised rather than dead in this run:
**263 invocations**, spanning the #4460 pre-scan's `.wav` opens (`SAMPLES/...`) and the
concurrent-listing knob's song/settings/favourites XML opens (`SONGS/....XML`,
`SETTINGS/FAVOURITES/SONG_Bank0.xml`, etc.) — real, repeated, sustained task-context opens all
forced through the unconditional non-yielding spin, none of them wedging.

**Caveat found while investigating this result:** `lens1_vt_sim`'s `Cargo.toml` default
features are `["host_app", "sim_latency"]` only — `efatfs_streaming`/`async_streaming_loader`
(which spawns `streaming_loader::streaming_fill_task` at all — `main.rs:578-579`,
`#[cfg(feature = "async_streaming_loader")]`) is **not** on by default. So this first run's
`streaming_fill_task` was never spawned; there was no second party to contend with the forced
spin over the FS mutex. This makes Run 1 alone weak evidence — the patched entry point was
genuinely exercised, but not against the mechanism §2 describes.

### Run 2 — `--features efatfs_streaming` (spawns `streaming_fill_task` for real)

```
cargo build --release --features efatfs_streaming
cp --sparse=always /tmp/deluge-lens1-task4-shared.img /tmp/deluge-lens1-task6-work3.img
timeout 180 env DELUGE_SD_IMAGE=/tmp/deluge-lens1-task6-work3.img LENS1_FIXTURE=cordae \
  LENS1_BLOCKS=20000 LENS1_STEP_TIMEOUT_S=120 LENS1_VIRTUAL_BUDGET_MS=600000 \
  LENS1_CONCURRENT_LISTING_EVERY_BLOCKS=50 ./target/release/lens1-vt-sim
```

Output (verbatim, only line — identical to Run 1):

```
LENS1_RESULT blocks_rendered=0 cluster_reads=233 recorder_writes=3 underrun_wait=0 underrun_unassign=0
```

Exit code 0. **No wedge.** To confirm `streaming_fill_task` genuinely ran (not just compiled in),
a short debug build with `RUST_LOG=info` was also run (release strips `log::*!` to no-ops via
`release_max_level_off`, unified across the whole dependency graph — this is a documented `NOTE`
at `lens1_vt_sim/src/main.rs:389-396`, not new information):

```
cargo build --features efatfs_streaming
cp --sparse=always /tmp/deluge-lens1-task4-shared.img /tmp/deluge-lens1-task6-dbg.img
timeout 90 env RUST_LOG=info DELUGE_SD_IMAGE=/tmp/deluge-lens1-task6-dbg.img LENS1_FIXTURE=cordae \
  LENS1_BLOCKS=2000 LENS1_STEP_TIMEOUT_S=30 LENS1_VIRTUAL_BUDGET_MS=60000 \
  LENS1_CONCURRENT_LISTING_EVERY_BLOCKS=50 ./target/debug/lens1-vt-sim
```

Relevant log lines (verbatim):

```
[...] INFO  lens1_vt_sim] lens1-vt-sim: efatfs mounted
[...] INFO  lens1_vt_sim] lens1-vt-sim: progress: virtual t=2548728us advances=20000 passes=34815 on_fiber_reads=4587 on_fiber_writes=0
[...] WARN  lens1_vt_sim::scenario] streaming-scenario: concurrent_listing_every_blocks capped at 10 dispatches this run — the UI navigation stack (uiNavigationHierarchy, capacity 16) never pops while this knob is active (never commits), so continuing would abort the whole process; see task-4-report.md
[...] INFO  lens1_vt_sim] lens1-vt-sim: progress: virtual t=5187871us advances=40000 passes=66991 on_fiber_reads=8951 on_fiber_writes=3
[...] INFO  lens1_vt_sim] lens1-vt-sim: progress: virtual t=7756702us advances=60000 passes=87541 on_fiber_reads=9047 on_fiber_writes=3
[...] INFO  lens1_vt_sim] lens1-vt-sim: progress: virtual t=33316121us advances=260000 passes=291927 on_fiber_reads=9047 on_fiber_writes=3
[...] INFO  lens1_vt_sim::scenario] streaming-scenario: concurrent_listing_every_blocks=50 (interval=Duration { ticks: 145100 }) fired 10 overlapping listing(s) during the block-target wait
[...] INFO  lens1_vt_sim] lens1-vt-sim: scenario result: boot_ready=true song_load_dispatched=true listing_completed=true load_committed=true load_completed=true playback_started=true playback_confirmed_active=true recording_started=true blocks_rendered=0 cluster_reads=233 recorder_writes=3 underrun_wait=0 underrun_unassign=0
LENS1_RESULT blocks_rendered=0 cluster_reads=233 recorder_writes=3 underrun_wait=0 underrun_unassign=0
```

`on_fiber_reads` (the async fill task's genuine on-fiber `deluge_efatfs_read_at` count) climbs
from 4587 to a plateau of 9047 by virtual t≈5.19M µs, then never moves again for the remaining
~30M µs of the run. So `streaming_fill_task` did real, substantial work early — real evidence it
was live, not idle — but went idle for the rest of the run once its initial fill demand was
satisfied (consistent with `blocks_rendered=0`: nothing ever depletes a buffer to re-trigger a
fill after the front-loaded initial one). The concurrent-listing knob's 10 dispatches (interval
145100 ticks ≈ 145ms virtual each, so all 10 complete within roughly the first 1.5s virtual of
the wait) plausibly overlapped, at least in part, with that same early window of active streaming
reads. Still no wedge.

### Conclusion

Neither run wedged, including the corrected run where `streaming_fill_task` was confirmed
genuinely active (9047 real on-fiber reads) concurrently with a task-context spin forced
unconditionally through the non-yielding path, sustained over 263 forced calls in Run 1's
equivalent traffic pattern. Read narrowly, that is exactly the "if it completes, that is
important negative evidence" case the task brief anticipated.

**But it should be read narrowly, not as disproof of §2.** Two structural gaps in Lens 1 blunt
what this spike can show:

1. **Lens 1 never renders** (see the top of this file). `streaming_fill_task`'s fill demand is
   therefore front-loaded and self-limiting — it does an initial burst of reads and then has
   nothing left to do, because nothing ever consumes the buffered audio to ask for more. §2's
   scenario ("loading a preset while a sample streams") implies *sustained* concurrent demand
   from an actively-playing voice; Lens 1 structurally cannot generate that, so this spike could
   only ever probe a short overlap window, not the sustained contention §2 is really about.
2. This spike cannot rule out that the specific race — a task-context spin arriving *while* the
   fill task is mid-transfer holding the FS mutex, not merely near it in virtual time — simply
   did not land inside the ~1.5s overlap window in these particular runs. Nothing here proves the
   mutex-hold and the spin were ever simultaneously in flight at the same virtual instant; it
   only shows both kinds of activity happened in the same run without an observed wedge.

**§2's mechanism itself is not undermined by this result.** It is derived from confirmed code
facts independent of Lens 1's limitations: `streaming_fill_task` is spawned on the thread/MAIN
executor (`main.rs`, both device and `lens1_vt_sim`) and holds the FS mutex across an SD-transfer
await (`with_fs`, `efatfs_fs.rs`); a collapsed non-yielding spin has no mechanism to let MAIN run
again while parked inside its own `poll()`. §3 independently reaches the identical conclusion
from the host-sim emulation's own confirmed limits (`progress_hook` cannot poll MAIN). This
spike neither confirms nor refutes that reasoning — it is **inconclusive**, not negative, because
Lens 1's structural non-rendering (the same root cause blocking Task 5) also caps how much
sustained contention this harness can ever generate.

**Recommendation:** Phase 1's `STREAM_EXEC` move should still be treated as a correctness
prerequisite, on the strength of §2/§3's independent code-level argument — this spike did not
find a wedge, but it was not capable of mounting the sustained-contention test that would make a
clean completion actually persuasive. A real empirical answer needs either Lens 1 driving genuine
render-demanded streaming (the same missing piece Task 5 needs) or a hardware test.

### Throwaway reverted

```
git checkout -- src/bsp/rust/src/efatfs_host_shim.rs
git diff --stat src/bsp/rust/src/efatfs_host_shim.rs   # empty
git status --short                                      # clean
```

Confirmed clean (empty diff, empty status) after every run above, including the temporary
`eprintln!` instrumentation used to confirm call counts — none of it is in the tree. This file
is the only change this task commits.

### Verification

- `ctest --test-dir build-tests`: 38/38 passed.
- `grep -rn "sim_block::block_on" src/bsp/rust/src | grep -v sim_block.rs`: 0 hits (no production
  site routes through `sim_block` yet — the Phase 0 exit criterion).
