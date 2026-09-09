# `sd_busy()` / `deluge_storage_fs_busy()` consumer audit

## Why this document exists

`deluge::sync::sd_busy()` used to be backed by `currentlyAccessingCard`, a flag with no writer since
the legacy diskio was deleted — so it was permanently `false`, and every one of its call sites had a
branch that had **never executed**, on host or on Embassy, since the flag went dead. This plan retargets
`sd_busy()` onto `deluge_storage_fs_busy()`, a libdeluge C-ABI seam that the Rust BSPs back with the real
efatfs FS-mutex state (`Mutex::try_lock().is_err()`). On Embassy that answer is now genuinely `true` for
the duration of one filesystem operation.

That flip activates nine dormant deferral/assertion branches at once. This document exercises the one
whose correctness the whole plan turns on (does the waveform overview scan still function, or did fixing
the deadlock just trade it for a silently dead feature?), then walks the other eight and classifies each.

**Scope note, stated once up front:** `deluge_storage_fs_busy()` is hardwired `false` on the
cooperative/C-host BSP (`task_scheduler_c_api.cpp`) — FatFS runs inline on the caller there, so there is
no FS mutex to report on. Every site below therefore behaves **exactly as it did before this change** on
that BSP. Everything in this document — the activation, the risk, the classifications — is Embassy-only.

## Step 0: does the overview scan still function?

### Why the existing gate cannot answer this

Task 3 fixed the deadlock by making `WaveformRenderer::advanceOverviewScan` defer (`return true`)
instead of issuing a synchronous read when `sd_busy()` is true. Task 3's own Lens 1 gate
(`cordae` fixture) went from `WEDGED` to `exit=0` with an unchanged `LENS1_RESULT` line. That is real
evidence the deadlock is gone, but it is **not** evidence the scan still works, because every field of
`LENS1_RESULT` is structurally blind to it:

- `scenario.rs:239-240` sets `cluster_reads`/`recorder_writes` from
  `sd::stats::on_fiber_reads()`/`on_fiber_writes()`.
- `sd.rs:548-558` (`note_read`/`note_write`) increments those counters **only if `on_fiber`** is true.
- The overview scan runs off-fiber by definition — that off-fiber-ness is the entire premise of the
  original deadlock — so any read it issues increments nothing these counters can see, whether the scan
  runs perfectly, stalls, or never runs at all.
- `blocks_rendered` is `audio_host::drive_count()`, a sim constant regardless.
- `underrun_wait`/`underrun_unassign` are audio-path counters, orthogonal to the scan.

So "deadlock fixed" and "feature silently dead" would produce byte-identical `LENS1_RESULT` output. The
structural argument (the guard's `return true` returns before `overviewScanAllDone = true` is reached,
and `overviewScanNextCluster` is untouched on that path, so the same cluster is simply retried with no
lost place) says deferral is lossless — but that is reading the code, not evidence it actually runs.

### The probe

Picked the brief's combined option: a temporary counter **and** a live check of
`sample->overviewCacheEntry(clusterIndex).investigated` at the moment of each successful
`investigateWholeCluster` call, printed to stderr. Added directly after the existing
`if (!investigateWholeCluster(sample, clusterIndex)) { return true; }` check in
`WaveformRenderer::advanceOverviewScan` (`src/deluge/gui/waveform/waveform_renderer.cpp`):

```cpp
// TEMPORARY (Task 6 Step 0 probe) -- remove before commit.
static uint32_t probeOverviewScanHits = 0;
std::fprintf(stderr, "PROBE_OVERVIEW_SCAN_HIT cluster=%d investigated=%d count=%u\n", clusterIndex,
             (int)sample->overviewCacheEntry(clusterIndex).investigated, ++probeOverviewScanHits);
```

(plus a temporary `#include <cstdio>`.) Rebuilt the full target list —

```
ninja -C build-embassy-hostapp deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler deluge_foundation deluge_midi
```

— then rebuilt and ran the Lens 1 harness (`src/bsp/rust/harness/lens1_vt_sim`) under a hard timeout, `stdout`
and `stderr` combined so the probe lines and the `LENS1_RESULT` line land in the same capture:

```
timeout 120 env LENS1_FIXTURE=cordae LENS1_BLOCKS=500 LENS1_WALL_TIMEOUT_S=45 \
  LENS1_VIRTUAL_BUDGET_MS=600000 ./target/debug/lens1-vt-sim > probe_run.log 2>&1
echo "exit=$?"
```

### Verbatim output

```
exit=0
```

First 5 and last 6 lines of `probe_run.log` (172 `PROBE_OVERVIEW_SCAN_HIT` lines total, all landing
before the final `LENS1_RESULT` line):

```
PROBE_OVERVIEW_SCAN_HIT cluster=0 investigated=1 count=1
PROBE_OVERVIEW_SCAN_HIT cluster=0 investigated=1 count=2
PROBE_OVERVIEW_SCAN_HIT cluster=0 investigated=1 count=3
PROBE_OVERVIEW_SCAN_HIT cluster=0 investigated=1 count=4
PROBE_OVERVIEW_SCAN_HIT cluster=0 investigated=1 count=5
...
PROBE_OVERVIEW_SCAN_HIT cluster=14 investigated=1 count=168
PROBE_OVERVIEW_SCAN_HIT cluster=14 investigated=1 count=169
PROBE_OVERVIEW_SCAN_HIT cluster=14 investigated=1 count=170
PROBE_OVERVIEW_SCAN_HIT cluster=14 investigated=1 count=171
PROBE_OVERVIEW_SCAN_HIT cluster=14 investigated=1 count=172
LENS1_RESULT blocks_rendered=0 cluster_reads=27 recorder_writes=144 underrun_wait=0 underrun_unassign=0
```

Distribution by `clusterIndex` (172 hits total, monotonically non-increasing as the index rises):

```
    19 cluster=0
    19 cluster=1
    16 cluster=2
    14 cluster=3
    13 cluster=4
    11 cluster=5
    11 cluster=6
    11 cluster=7
    10 cluster=8
     8 cluster=9
     8 cluster=10
     8 cluster=11
     8 cluster=12
     8 cluster=13
     8 cluster=14
```

Every single hit printed `investigated=1` — zero `investigated=0` lines (checked with
`grep -c "investigated=0"`, returned `0`).

### What this proves

- The scan **ran**, off-fiber, 172 separate times over the course of one Lens 1 run, and every one of
  those 172 calls **succeeded** — `investigateWholeCluster` returned `true` and the cache entry it wrote
  was observed `investigated == true` immediately afterward. This is the strongest available signal
  short of instrumenting the byte contents of the overview cache itself: it shows the cache entry the
  scan is responsible for populating actually flipped, at the moment the scan flipped it, for the exact
  cluster the scan was working on.
- The scan made **forward progress across many distinct clusters** (0 through 14) rather than getting
  stuck retrying one index. The monotonic decrease in hit-count as the cluster index rises is the
  signature of several audio files of different lengths being round-robined by
  `AudioFileManager::backgroundWaveformOverviewScan` — shorter files finish (drop out of the "still has
  work" rotation) before longer ones, which is exactly the shape you'd expect from `kOverviewScanClustersPerCall
  = 1` scanning several files of varying length one cluster at a time. **The "~19 files" figure is
  INFERRED from the cluster=0 hit count (19), not observed** — the probe printed only `clusterIndex`, never
  a sample identity, so it cannot distinguish "19 different files each hit once at cluster 0" from any
  other combination that happens to sum to 19; the inference rests on `kOverviewScanClustersPerCall = 1`
  meaning at most one file advances per call, which makes "N distinct files" the natural reading of a
  count of N hits at the lowest cluster index, but that is a reading of the shape, not a direct
  observation. Either way, the shape itself — decreasing, not flat or erratic — is evidence of a
  functioning round-robin, not an artifact of a stuck cursor (a stuck cursor would show one cluster index
  with a runaway count and nothing after it).
- Zero `investigated=0` observations rules out the failure mode where the guard's deferral path is
  somehow taken *after* a call that looked successful but didn't actually persist the flag.
- This run took the synchronous `fill_now` branch, not the `async_streaming_loader` degrade
  (`streaming_loader.rs:172-181`) — `lens1_vt_sim` leaves that feature off, and a scan that never filled
  anything would show either zero probe lines or (if the guard were the culprit) probe lines that never
  advance past `cluster=0`. Neither happened, so the pre-existing device-side degrade is confirmed **not**
  live in this harness and did not confound this result, as the brief anticipated.
- **The guard genuinely fires in this run, and the evidence for that is the WEDGED→`exit=0` transition
  itself, not this probe.** Task 3's report shows the *identical* fixture (`cordae`, same block/timeout
  budget) reproducibly `WEDGED` before the guard existed and reproducibly reached `exit=0` after it. A
  wedge only happens if the scan's synchronous read actually raced the storage worker for the FS mutex;
  the fact that it no longer does is only explained by `sd_busy()` returning `true` at the moment that
  race would otherwise have occurred, and the guard deferring on that `true`. This probe's job is
  therefore narrower than "prove the guard fires" — it is "prove the scan still gets its work done, on
  top of a guard Task 3 already showed fires." (An earlier draft of this document instead argued from this
  run's silence — "these `sd_busy()`-true windows apparently didn't coincide with an in-progress scan
  attempt" — but that reasoning is circular: if the guard never coincided with a scan attempt, the pre-fix
  run could not have wedged in the first place. Corrected here.)
- Combined with the structural argument from Task 3 (the guard's `return true` is taken *before*
  `investigateWholeCluster` is called, so a deferral never touches `overviewScanNextCluster` and never
  produces a false `investigated=0`), this closes the loop: the code-reading argument said deferral is
  lossless, Task 3's WEDGED→`exit=0` transition is evidence the guard fires, and this run is direct
  execution evidence that, guard or not, the scan completes real work and keeps making progress across the
  whole fixture.

### What this does NOT prove

- It does not prove the scan reaches full completion (`overviewScanAllDone = true`) for every sample in
  the fixture — the run only needed to reach the target block count, not necessarily scan every cluster
  of every file to exhaustion. 172 successful investigations reaching at least cluster 14 (across an
  inferred, not observed, ~19 files — see above) is strong partial-completion evidence, not a proof of
  exhaustive completion.
- **The probe only instrumented the success path, not the deferral path.** It counts successful
  `investigateWholeCluster` calls; it does not count how many times `advanceOverviewScan` took the
  `sd_busy()`-true `return true` branch. A one-line counter on that branch (deferrals-N) alongside the
  172 successes would have shown both "the guard fires" and "the guard does not starve the scan" from the
  same run, closing the loop directly rather than leaning on Task 3's separate WEDGED→`exit=0` result for
  the first half. That counter was not added in this run — this document states the gap rather than
  claiming stronger coverage than was measured. As it stands: this run is direct evidence the scan makes
  progress, and Task 3's before/after gate is the evidence the guard fires; no single run in this plan
  has yet shown both signals simultaneously with dedicated instrumentation for each.
- It says nothing about hardware timing, real SD-card latency, or the on-device experience — this is a
  virtual-time host harness.

### Headline finding

**The scan still functions.** This is not a regression. 172 successful, cache-flipping investigations
across 15 distinct cluster indices (and, per the inference above, roughly 19 audio files) were observed
in one run, ruling out the "deadlock fixed by starving the feature" failure mode this task exists to
check for.

### Instrumentation removed

The temporary counter, the `fprintf` probe line, and the temporary `#include <cstdio>` were all removed
from `src/deluge/gui/waveform/waveform_renderer.cpp` before rebuilding and committing. `git diff` against
that file shows no residual change from this task (confirmed with `git status --short` after reverting,
and by rebuilding `deluge_app`/`deluge_loadcheck` clean afterward).

## Step 1–2: the nine sites

**Summary, as shipped by this task and corrected during final review (see below): 5 IMPROVED / 4
NEUTRAL / 0 REGRESSION.**

- IMPROVED: sites 1, 3, 4, 5, 6.
- NEUTRAL: site 2 (its `RESOURCE_SD` ceiling is not linked into any BSP this repo currently builds —
  forward-looking only, see the correction below); site 7 (its code path is compiled out by default);
  sites 8 and 9 (both were REGRESSIONs as activated by the flip alone — false `FREEZE_WITH_ERROR`s on
  legitimate concurrency, worse than the eternal no-op they replaced — and both were downgraded to
  diagnostic logs in this task before they could ship).

Stated the more important way: **as activated by the flip alone, before this task's judgement, two of
the nine (`audio_engine.cpp` and `save_song_ui.cpp`) would have been REGRESSIONs.** Catching that before
it ships is exactly this task's job, not a side effect of it — see Step 3 below for both. With those two
fixes applied, the shipped tally has zero regressions.

**A final-review correction to sites 2, 3, and 6, made after the original tally above was written.**
That tally classified site 2 IMPROVED and sites 3/6 NEUTRAL-as-redundant-with-site-2. Verified from this
branch's own device ELF (`arm-none-eabi-nm -C target/armv7a-none-eabihf/release/deluge-rust | grep -cE
"TaskManager::|ResourceChecker"` → `0`): the C++ `ResourceChecker`/`TaskManager` that site 2's
`RESOURCE_SD` ceiling lives in is not linked into the Embassy device build at all.
`addRepeatingTask`/`isSDRoutineActive`/`startTaskManager` all resolve into `scheduler.rs`, which states
directly why — `scheduler.rs:526-528` says its reimplementation of the `scheduler_api.h` ABI exists
specifically to "keep the C++ TaskManager out of the link", and `scheduler.rs:228`/`:465-467` state the
Rust task runner has **no `RESOURCE_SD` gate by design**. Site 2 is therefore reclassified NEUTRAL
(forward-looking only, not currently live anywhere) rather than IMPROVED — full reasoning in its own
entry below. Because site 2 gates nothing on Embassy, sites 3 (`midi_engine.cpp`) and 6
(`smsysex.cpp`) are **not** redundant with it — they are re-derived IMPROVED, independently load-bearing,
below. This correction must not be read as softening: on Embassy those two sites are the only protection
SysEx dispatch and USB-MIDI servicing have against concurrent filesystem work, and a reader relying on
the original "not doing independent work" wording could conclude they're safe to delete.

**A premise correction, made during review of this document, that changes two more classifications.**
This document originally described `audio_recorder.cpp:174` and `audio_engine.cpp:1666` as "already
paired with `isSDRoutineActive()`, which softens them" — a premise inherited from this task's own brief
and not independently re-verified before use. It is false on Embassy: `isSDRoutineActive()` is hardwired
`return false` there (`src/bsp/rust/src/scheduler.rs:658`), with a comment explaining why — SD access on
this BSP goes through `block_on`, which parks the whole executor for the duration of a transfer, so there
is no busy-wait for `isSDRoutineActive()` to ever report `true` about. So on Embassy, before this flip,
`isSDRoutineActive() || sd_busy()` was exactly as dead as `sd_busy()` alone — both halves were always
`false`. Neither site 5 nor site 8 had *any* live half before this change; the "softening" never existed
on the BSP this whole plan is about. Verifying an inherited premise is exactly what this audit
exists to do; this correction is recorded here rather than silently folded in, and it is the reason sites
5 and 8 read differently below than in this document's first draft. (It does not change sites 1–4/6/7/9,
which never rested on that premise.)

**A duty-cycle caveat that applies wherever "busy is brief" appears below.** "Busy" spans one `with_fs`
call — true — but per-call brevity does not bound hold-off *frequency*. During a song load or a
streaming burst the storage owner issues many consecutive `with_fs` calls back-to-back; across that
whole window `sd_busy()` reads `true` on a large fraction of samples, even though no single `true` window
is long. A `RESOURCE_SD`-gated task (site 2) or an in-function check gated the same way (sites 3, 4, 5, 6)
can therefore be held off across most of a load, not just for one operation's duration. This does not
change any classification below — every one of these sites was written to defer, and deferring more
often during a burst is the mechanism working as designed, not a new failure mode — but "brief" must not
be read as "rare," and it is stated once here rather than re-derived at each site.

No site's deferral/assertion branch had ever executed, on host or Embassy, before this change — every
one of the nine reads below the fold is being exercised for the first time by this plan.

---

### 1. `waveform_renderer.cpp:691` — the overview-scan guard (already migrated, Task 3)

**What it does when busy:** `advanceOverviewScan` returns `true` before issuing a synchronous cluster
load, deferring that cluster to the next call instead of racing the storage worker for the FS mutex.

**Classification: IMPROVED — but "this is the deadlock fix itself" (this section's original wording)
overstates it for real hardware; corrected during final review.** The literal deadlock mechanism —
a synchronous off-owner read blocking on the FS mutex while MAIN's own `executor.poll()` spins on the
lock, so nothing is left able to resume the holder fiber — is the `embassy_futures::block_on` spin that
lives only in `efatfs_host_shim.rs` (`#![cfg(feature = "host_app")]`), i.e. the Embassy **host harness**
this plan's bug report and Lens 1 gate both run on. On the device build, `embassy_futures::block_on` does
not appear in `efatfs_fs.rs` at all; every entry point there (e.g. `deluge_efatfs_read_at`,
`efatfs_fs.rs:301`, the exact function the scan's fill path calls) gates on `on_fiber()` and rejects an
off-fiber caller immediately instead of spinning. So on hardware this guard's job is narrower than "fixes
a deadlock": it stops the scan from churning always-failing off-fiber read attempts, since the FS mutex
was never going to make MAIN's executor spin there in the first place. It is still the correct fix and
still IMPROVED — a scan that keeps retrying a call guaranteed to fail is real, measurable waste — the
correction is to the mechanism named, not the classification. Before this change the branch never fired
(dead flag), so the scan's synchronous off-owner read could — and, per the bug report that started this
whole plan (reproduced in the host harness), did — block the harness this way. Now the branch fires
correctly and defers instead. Step 0 above independently confirms the deferred scan still gets its work
done.

Migrated by commit `41745107e` (Task 3); not touched by this task beyond the temporary Step 0 probe,
which was reverted before commit.

### 2. `resource_checker.h:38` — the scheduler `RESOURCE_SD` ceiling (already migrated, Task 4)

**What it does when busy, on the one class of build where it links:** `ResourceChecker::checkResources()`
treats `RESOURCE_SD` as locked, holding off admission of any task that declares that resource.

**Classification: NEUTRAL — not linked into any BSP this repo currently builds; forward-looking only.
Corrected during final review; supersedes the IMPROVED classification this section originally gave.**
The original reasoning was that `deluge_storage_fs_busy()` finally going genuinely `true` made this
priority-ceiling live for the first time. That assumed `ResourceChecker::checkResources()` is in the
Embassy call graph. It is not. Verified from this branch's own device ELF:

```
$ arm-none-eabi-nm -C target/armv7a-none-eabihf/release/deluge-rust | grep -cE "TaskManager::|ResourceChecker"
0
```

`addRepeatingTask`, `isSDRoutineActive`, and `startTaskManager` all resolve into `scheduler.rs`, a
from-scratch Rust reimplementation of the `scheduler_api.h` ABI — which states directly why:
`scheduler.rs:526-528` says the reimplementation exists specifically to "keep the C++ TaskManager out of
the link." `scheduler.rs:228` and `:465-467` go further, stating the Rust task runner has **no
`RESOURCE_SD` gate by design** — SD serialization on Embassy comes from the single-owner storage
discipline (the storage owner IS the worker fiber) plus the `block_on_fiber` yield in `sd.rs`, not from a
resource-ceiling admission check. `ResourceChecker` is real code, but Embassy never calls it.

On the other class of build — cooperative/C-host (sim, `deluge_loadcheck`, `build-tests`, legacy RZA1) —
`ResourceChecker` IS linked, but `deluge_storage_fs_busy()` is hardwired `false` there (this document's
scope note up top), so the ceiling is a no-op for the opposite reason: the code runs, but its input never
goes `true`.

**`resource_checker.h`'s `RESOURCE_SD` branch is therefore a no-op on every build this repo currently
produces.** Its only observable live effect anywhere in this repo is the CI unit test
`Scheduler.fsBusyBlocksSdTask`, which exercises `ResourceChecker` directly rather than through a linked
app. Keeping the check is still correct — it is the right behaviour to have ready — and it becomes live,
exactly as this section originally described, if and when the Rust task runner ever grows a `RESOURCE_SD`
gate of its own (`scheduler.rs:228`'s comment describes what that would take). Until then it is
forward-looking, not IMPROVED, and the on-device hardware-smoke watch item this document previously
carried for it (see Concerns and "Gates NOT run" below) is moot — there is nothing to observe on
hardware, because the code isn't in the hardware build. **This also means sites 3 and 6 below, previously
classified NEUTRAL as "redundant with this ceiling," are not redundant with anything — see their
re-derivation below.**

Migrated by commits `1c3295df3`/`a6447337b` (Task 4); not touched by this task. The reclassification
above was made during final review of this document, not by re-touching the migration commits.

### 3. `midi_engine.cpp:502-504` — defer SysEx/USB-MIDI handling

**What it does when busy:** `MidiEngine::checkIncomingUsbMidi()` returns immediately, skipping
`check_incoming_usb()` and the per-cable device servicing loop for this call, retried on the next tick.
The existing comment already calls this "a hack to avoid SysEx handlers clashing with other sd-card
activity" — i.e. this check was *written* to do exactly what it can now actually do.

**Classification: IMPROVED, and independently load-bearing on Embassy. Corrected during final review —
supersedes the "NEUTRAL, redundant with site 2" classification this section originally gave.** The
original reasoning was that `checkIncomingUsbMidi()`'s caller, `PlaybackHandler::midiRoutine()`
(`playback_handler.cpp:132`, scheduled `RESOURCE_SD | RESOURCE_USB`, `deluge.cpp:542-543`), is already
held off admission by site 2's scheduler ceiling whenever `sd_busy()` is true, so this in-function check
could only ever observe a vanishingly narrow admit-to-check window. That reasoning is false — see the
site-2 correction above: `ResourceChecker`'s `RESOURCE_SD` ceiling is not linked into the Embassy build
at all, so it gates nothing there. **On Embassy this check is not a second layer sitting behind an
already-closed gate — it is the only gate.** It is the sole protection for SysEx dispatch and USB-MIDI
servicing against concurrent filesystem work on this BSP (alongside site 6). This distinction is not
just bookkeeping: a future reader relying on this section's original "not, in practice, doing independent
work" wording could conclude the check is safe to delete as dead-code cleanup. It is not — deleting it
would reopen exactly the hazard the check's own comment ("a hack to avoid SysEx handlers clashing with
other sd-card activity") was written for, with nothing behind it on this BSP.

### 4. `playback_handler.cpp:191` — defer a pending global MIDI undo/redo command

**What it does when busy:** `PlaybackHandler::slowRoutine()`'s guard is `pendingGlobalMIDICommand != NONE
&& !sd_busy()`; when busy, the whole body is skipped and `pendingGlobalMIDICommand` is left untouched
(it's only cleared inside the guarded body), so the pending command is retried, unlost, on the next tick.

**Classification: IMPROVED, and independently load-bearing (not redundant with site 2).**
`PlaybackHandler::slowRoutine()` IS also a scheduled task ("playback slow routine", `RESOURCE_SD`,
`deluge.cpp:568-569`), so admission through it is covered by site 2's ceiling the same way site 3 is.
But unlike site 3, `slowRoutine()` has a second, direct call path that bypasses the scheduler entirely:
`View::noteRowKindaMessage` — the undo/redo button handler — calls `playbackHandler.slowRoutine();`
directly, twice (`view.cpp:417` and `:436`, both commented "Do it now if not reading card"). Neither call
goes through task admission, so site 2's ceiling does not protect this path at all; this in-function
check is the *only* protection against dispatching `undo()`/`redo()` while a real FS operation is
already in flight, on that path. The dispatched op can itself load a sample and, on Embassy, can outlive
the call that dispatched it onto the storage worker (documented in the surrounding comment, referencing
`known-concurrency-bugs.md` B6) — deferring while busy avoids adding a second concurrent FS consumer
during that window. The retry is lossless by construction — the flag is cleared only on the taken branch,
matching the same "lossless deferral" shape verified for the waveform guard in Step 0.

### 5. `audio_recorder.cpp:174` — defer recorder `slowRoutine` work

**What it does when busy:** `AudioRecorder::slowRoutine()`'s guard is `isSDRoutineActive() ||
sd_busy()`; when either is true, the whole routine (including the check that calls `finishRecording()`,
which frees the `SampleRecorder`) is skipped for this tick.

**Classification: IMPROVED, and independently load-bearing (not redundant with site 2). Not "softened" by
`isSDRoutineActive()` — see the premise correction above.** This document's first draft called this site
"softened" by its `isSDRoutineActive()` half, inherited from the brief's characterization. That is false
on Embassy: `isSDRoutineActive()` is hardwired `false` there (`scheduler.rs:658`), so before this flip
neither half of `isSDRoutineActive() || sd_busy()` was ever `true` — this check was completely dead, not
partially live. The `sd_busy()` half newly backing this check is not "additive" to an existing protection;
it is the first protection this site has ever had on Embassy.

What it protects is real: the comment explains `finishRecording()` frees the recorder, and
`discardRecorder()` (site 8 below) forbids doing that from inside the SD card routine because the
recorder may be suspended part-way through its own `cardRoutine()` — freeing it there leaves that
routine running on freed memory. And this check is independently load-bearing, not redundant with site
2's ceiling: `AudioRecorder::slowRoutine()` IS also a scheduled task ("audio recorder slow",
`RESOURCE_SD | RESOURCE_SD_ROUTINE`, `deluge.cpp:565-567`), but it additionally has direct call paths
that bypass the scheduler — `deluge_app_tick`'s cooperative-BSP tick function calls
`audioRecorder.slowRoutine();` unconditionally (`deluge.cpp:618`), and `StemExport`'s yield loop does the
same (`stem_export.cpp:225`, alongside `AudioEngine::slowRoutine()`). Neither path is gated by site 2's
ceiling, so this in-function check is the only protection on those paths.

### 6. `smsysex.cpp:909` — defer dispatching the front SysEx op

**What it does when busy:** `smSysex::handleNextSysEx()` returns before setting `g_sysex_op_in_flight`
and before dispatching `runSysexOp` onto the storage owner; the queued `SysExQ` entry is untouched, so
it's retried, unlost, on the next tick.

**Classification: IMPROVED, and independently load-bearing on Embassy. Corrected during final review —
supersedes the "NEUTRAL, redundant with site 2" classification this section originally gave.**
`handleNextSysEx()` has exactly one caller: it is scheduled directly as "Handle pending SysEx traffic."
(`RESOURCE_SD`, `deluge.cpp:556-557`) — there is no other call site, unlike sites 4/5's bypass paths. The
original reasoning was that site 2's ceiling already holds this task off admission whenever `sd_busy()`
is true, making this in-function check redundant with admission-time gating. That is false — see the
site-2 correction above: nothing gates this task's admission on Embassy. **This check is the sole
protection for SysEx dispatch against concurrent filesystem work on this BSP, alongside site 3.** The
queued `SysExQ` entry and in-flight guard being left untouched either way still means deferring costs no
starvation risk — that part of the original reasoning holds — but "not doing independent work" does not:
with site 2 gating nothing, this check does all of the work, not none of it. Same caution as site 3: this
is not a redundant layer to prune, and deleting it on the "redundant" reading this section previously
gave would remove the only defence this call site has on Embassy.

### 7. `sample_marker_editor.cpp:902` — throttle a debug-only marker-randomization routine

**What it does when busy:** `SampleMarkerEditor::graphicsRoutine()`'s `if (!sd_busy() && ...)` guards a
block that randomly nudges a sample's loop-start/end markers, purely for manual loop-point stress
testing.

**Classification: NEUTRAL.** This entire block is compiled only under `#ifdef TEST_SAMPLE_LOOP_POINTS`
(`src/deluge/gui/ui/sample_marker_editor.cpp:901`), and that macro is commented out by default
(`src/definitions_cxx.hpp:34`: `// #define TEST_SAMPLE_LOOP_POINTS 1`). In every build this repo actually
produces — including the Embassy builds this whole plan is about — this code does not compile in at all,
so the newly-real `sd_busy()` signal has no observable effect anywhere except for a developer who locally
uncomments that macro to do manual loop-point stress testing, in which case a real signal is strictly more
correct than a permanently-false one for the exact reason the guard exists (throttle the randomizer away
from real card activity). NEUTRAL for the shipped product; a minor, harmless correctness improvement for
the one developer workflow that ever compiles this code.

### 8. `audio_engine.cpp` — `discardRecorder()`'s `ALPHA_OR_BETA` assertion

**What it does when busy (as activated by the flip alone, before this task's fix):**
`FREEZE_WITH_ERROR("E251")` if `isSDRoutineActive() || sd_busy()`.

**Classification: REGRESSION as activated, fixed to NEUTRAL in this task.** Full reasoning in Step 3
below — this document's first draft classified this site IMPROVED and KEPT it unchanged, reasoning that
`isSDRoutineActive()` already made the assertion "partially live" before this flip. That premise is false
on Embassy (see the correction above): neither half of the condition was ever `true` there before this
change, so this assertion was exactly as dead as site 9's before the flip, and giving it real teeth
exposes the exact same false-freeze risk site 9 has — arguably worse, since `discardRecorder()` is
reachable, unguarded, from three live UI paths (see Step 3) during recording completion, which is
FS-heavy. Downgraded to a diagnostic log in this task; see Step 3 for the full re-derivation.

### 9. `save_song_ui.cpp` — `performSave()`'s `ALPHA_OR_BETA` assertion

**What it does when busy (as activated by the flip, before this task's fix):**
`FREEZE_WITH_ERROR("E316")` if `sd_busy()`.

**Classification: REGRESSION as activated, fixed to NEUTRAL in this task.** Full reasoning in Step 3
below. Unlike site 8's documented double-free hazard, no documented hazard justifies halting here. Left as a
hard freeze, this assertion would turn an ordinary, harmless scheduling coincidence (a user pressing Save
while an unrelated background op like the overview scan briefly holds the mutex) into a user-visible
device freeze on every beta build — worse than the eternal no-op it replaced. Downgraded to a diagnostic
log in this task, which makes it behaviour-preserving (NEUTRAL) while keeping the information.

---

## Step 3: the two `ALPHA_OR_BETA` assertions, judged individually

Both assert *not busy* and have been trivially satisfied since the day `currentlyAccessingCard` went
dead. With a real signal, both *can* now fire — the question for each is whether that firing represents a
genuine invariant violation worth halting a beta build over, or legitimate concurrency that the assertion
was never entitled to rule out.

**This section was re-derived after review.** The first draft of this document reasoned that
`audio_engine.cpp:1666` was already "partially live" via `isSDRoutineActive()`, and KEPT it unchanged on
that basis while downgrading only `save_song_ui.cpp:124`. That premise — inherited from this task's own
brief, and not independently re-verified before use — is false on Embassy:
`isSDRoutineActive()` is hardwired `return false` there (`src/bsp/rust/src/scheduler.rs:658`). Neither
half of `isSDRoutineActive() || sd_busy()` was ever `true` on Embassy before this flip, so this assertion
was exactly as completely dead as `save_song_ui.cpp:124`'s — not "half-live." The re-derivation below
replaces the original KEEP.

### `audio_engine.cpp:1666` (`discardRecorder`, E251) — **DOWNGRADED to a diagnostic log**

```cpp
void discardRecorder(SampleRecorder* recorder) {
	if (ALPHA_OR_BETA_VERSION && (isSDRoutineActive() || deluge::sync::sd_busy())) {
		FREEZE_WITH_ERROR("E251");
	}
	...
```

The surrounding comment states a real hazard, specifically: `finishRecording()` frees the
`SampleRecorder`, and freeing it while the recorder's own `cardRoutine()` is suspended part-way through
leaves that routine running on freed memory, "then freeing it a second time" — a double free, which
"surfaces as M123 from the allocator, a long way from here." That invariant is genuine, and unlike site
9 this assertion protects something real. But three corrections to the original KEEP reasoning together
make KEEP the wrong call *for this round*:

1. **The "partially live" premise is false, as established above.** This assertion had zero live
   protection on Embassy before this flip, the same starting point as site 9.
2. **`sd_busy()` is a coarser signal than the invariant actually needs.** The comment's hazard is about
   *this recorder's own* `cardRoutine()` being suspended mid-transfer — but `sd_busy()` answers "is ANY
   FS operation in flight anywhere," since FatFS access is serialized through one worker but not
   per-caller-attributed. A `true` reading at this call site does not establish that the in-flight
   operation is this recorder's own; it could be an unrelated file's cluster load. So the same
   false-freeze shape that makes site 9 unacceptable — halting on a coincidence rather than a genuine
   conflict — applies here too, and arguably worse: recording completion is the FS-heaviest moment in
   the app (finalizing/closing the just-recorded file), which is exactly when other background FS work
   like the overview scan (Step 0: fires well over a hundred times per run) is likely to be active at the
   same time, for reasons unrelated to this recorder.
3. **The exposure is on a *live*, not legacy, call path.** The original KEEP argument #3 called
   `AudioRecorder::process()` "the legacy non-`USE_TASK_MANAGER` mainLoop," inherited from a stale
   in-source comment (`audio_recorder.cpp:171-173`). It is not legacy: `AudioRecorder::process()` calls
   `finishRecording()` → `discardRecorder()` **unguarded** (`audio_recorder.cpp:219`, no
   `isSDRoutineActive()`/`sd_busy()` check anywhere in `process()`), and `process()` itself is reached
   from three live UI call sites — `gui/menu_item/osc/audio_recorder.h:57`,
   `gui/ui/browser/sample_browser.cpp:506`, and `gui/views/instrument_clip_view.cpp:5563`. On an Embassy
   beta build, any ordinary recording finished through one of those paths while any unrelated FS
   operation happens to be mid-flight is now `FREEZE_WITH_ERROR("E251")`.

That combination — a coarse signal, applied to a real invariant it doesn't precisely establish, reachable
from ordinary UI use, at the app's FS-heaviest moment — crosses the same "worse than the no-op" bar as
site 9. Downgraded the same way:

```cpp
if (ALPHA_OR_BETA_VERSION && (isSDRoutineActive() || deluge::sync::sd_busy())) {
	D_PRINTLN("discardRecorder: isSDRoutineActive()/sd_busy() was true on entry (see sd_busy_audit.md)");
}
```

**The correct fix is narrower, and is a follow-up, not this round's job.** The BSP already tracks the
precise invariant per-op: `fiber.rs`'s `SD_ROUTINE_HELD`/`sd_routine_held()` (`fiber.rs:403,411-412`) is
"count of SD-routine-class ops in flight," consumed by the `RESOURCE_SD_ROUTINE` scheduler gate
(`scheduler.rs:461`) for exactly this class of hazard — but it is not currently exported through the
libdeluge C-ABI, so app code cannot ask it directly. Exporting it and switching this assertion (and site
5's `slowRoutine()` guard) onto it would recover a signal precise enough to KEEP as a hard assertion. That
export was explicitly out of scope for this round (no new C-ABI export) and is recorded here as the
correct next step, not implemented.

### `save_song_ui.cpp:124` (`performSave`, E316) — **DOWNGRADED to a diagnostic log**

```cpp
if (ALPHA_OR_BETA_VERSION && deluge::sync::sd_busy()) {
	FREEZE_WITH_ERROR("E316");
}
```

There is no comment here, and reading the surrounding code finds no memory-safety or data-corruption
rationale. **A claim in this document's first draft was wrong and has been corrected in the shipped code
comment:** it stated `performSave`'s own filesystem calls "simply queue behind" whatever holds the FS
mutex. On Embassy they do not queue — `performSave()` is not owner-dispatched (no `Owner::run` anywhere
under `src/deluge/gui/ui/save/`), so its filesystem calls run off the storage-worker fiber, and the
efatfs task-context C-ABI **rejects** off-fiber callers outright (`DELUGE_ERR_BUSY`,
`src/bsp/rust/src/efatfs_fs.rs:535-537`) rather than serializing them.

That correction **narrows, rather than strengthens, the "provably benign" conclusion this document
originally reached.** `performSave()` calls `StorageManager::fileExists()` at line 151, which opens the
file to test existence and reports "exists" only if the open succeeds. If that open happens off-fiber
(which — per the above — appears to be the normal case for `performSave`, independent of whether
`sd_busy()` is true at all), it returns `DELUGE_ERR_BUSY`, `fileExists()` reads as `false`, and
`fileAlreadyExisted` (line 151) is `false` regardless of whether the file actually exists — silently
skipping the overwrite-confirmation prompt at line 153 and overwriting an existing song without asking.
**This is a real, separately-tracked gap, not something this task's diagnostic-log change fixes or
should try to fix** — it is orthogonal to `sd_busy()` (it would reproduce even with `sd_busy()` always
`false`) and is being tracked outside this plan. It is recorded here because reasoning carefully about
"is it safe to proceed while busy" surfaced it, and a reader of the original "simply queue, nothing
unsafe" claim would not have found it.

What this task's own diagnostic-log downgrade *does* establish, more narrowly than the original draft
claimed: `sd_busy()` being `true` at line 124 is orthogonal to any known hazard at that instant — nothing
about the busy signal itself makes this call more or less dangerous than any other invocation of
`performSave()`. Firing `FREEZE_WITH_ERROR` on that orthogonal signal would turn an unrelated scheduling
coincidence into a user-visible device freeze on every beta build, which is still the right reason to
downgrade it — that conclusion survives the correction even though the "provably benign" framing around
it does not. Downgraded to a diagnostic `D_PRINTLN` that preserves the `ALPHA_OR_BETA_VERSION` gate (so
it costs nothing on release builds):

```cpp
// Diagnostic only, not a hard invariant (see docs/dev/sd_busy_audit.md, save_song_ui.cpp:124): this
// used to FREEZE_WITH_ERROR("E316") here, back when sd_busy() was permanently false and the check
// could never fire. No comment or invariant here ever explained *why* entering performSave() while
// busy would be unsafe, and this task found none -- sd_busy() being true at this exact instant is
// orthogonal to any hazard at this call site. It is NOT true that performSave's own filesystem calls
// then "queue" behind whatever holds the mutex: performSave is not owner-dispatched, so its FS calls
// run off the storage-worker fiber, and the efatfs C-ABI rejects off-fiber callers outright
// (DELUGE_ERR_BUSY, see efatfs_fs.rs's task-context bridge) rather than serializing them. Whether
// that rejection itself causes a problem here (e.g. StorageManager::fileExists silently reading as
// "doesn't exist" off-fiber) is a separate, already-tracked gap, not something this log line
// addresses -- see sd_busy_audit.md. Log it rather than crash a beta build over what this task
// established is ordinary scheduling coincidence, not a known hazard.
if (ALPHA_OR_BETA_VERSION && deluge::sync::sd_busy()) {
	D_PRINTLN("performSave: sd_busy() was true on entry (see sd_busy_audit.md)");
}
```

### Why both ended up downgraded, and what still differs between them

Both assertions turned out to have zero live protection before this flip (the "one already partially
live" asymmetry this section originally rested on was wrong), and both cross the same "false freeze on
an orthogonal or imprecise signal" bar once given a real signal — so both are downgraded this round, not
one of each as the first draft concluded. That is not "no reasoning, same outcome for both": what
differs, and is recorded for whoever picks up the follow-up work, is *why* each is worth revisiting
differently. Site 8 (`discardRecorder`) protects a real, documented invariant and has a known, precise
fix waiting (`sd_routine_held()`, once exported) — it should eventually go back to a hard assertion, on
the narrower signal. Site 9 (`performSave`) protects no known invariant at all; there is nothing to
"precisely re-derive" it onto, and its off-fiber `fileExists()` behavior is a separate, already-tracked
concern rather than a reason to reinstate this particular check. Collapsing them to "downgrade both, done"
would lose that distinction, which is why it's stated explicitly here rather than left implied by an
identical code change.

## Step 3b: stale doc reference

`docs/dev/target_architecture.md:66` named `currentlyAccessingCard` (deleted in this plan's Task 5) as a
still-existing "hand-placed guard" alongside `audioRoutineLocked`. Updated to say a hand-placed guard *at
call sites that query* `deluge_storage_fs_busy()` (naming the actual seam, with `deluge::sync::sd_busy()`
noted as its app-side alias) — `sd_busy()` itself is a query, not a guard; the guards are the nine call
sites in this document. The surrounding architectural point (concurrency safety in this codebase rests on
ad-hoc, hand-placed guards rather than a declared contract) is unchanged — only the example needed
correcting.

## Concerns

1. The Step 0 probe's 172 hits confirm the scan runs and makes progress in the one fixture (`cordae`)
   this plan's harness exercises. It is not a proof for every possible sample/fixture combination, and it
   does not confirm the scan reaches full completion (`overviewScanAllDone`) — only that it makes
   substantial, correctness-preserving progress within the run's virtual-time budget. It also only
   instrumented the success path, not the deferral path — see "What this does NOT prove" above; the
   evidence that the guard itself fires comes from Task 3's WEDGED→`exit=0` transition on the identical
   fixture, not from this probe's counter.
2. `resource_checker.h`'s `RESOURCE_SD` ceiling (site 2) is **not linked into any BSP this repo
   currently builds** — see the site-2 correction above. It has zero live effect on Embassy (the Rust
   task runner has no such gate, by design — `scheduler.rs:228`) and zero live effect on the
   cooperative/C-host BSPs (`deluge_storage_fs_busy()` is hardwired `false` there). Its only observable
   live effect anywhere in this repo is the CI unit test `Scheduler.fsBusyBlocksSdTask`. **The on-device
   ear/behaviour check this document previously flagged for it is therefore moot** — there is nothing to
   observe on hardware, because the code isn't in the hardware build; see "Gates NOT run" below for the
   fuller version of this point. It becomes a real, worth-watching site only if/when the Rust task runner
   grows a `RESOURCE_SD` gate of its own. Per the duty-cycle caveat above, sites 3, 4, 5, and 6 —
   independently load-bearing, not redundant with a ceiling that currently gates nothing — can hold off
   their tasks across most of a song load or streaming burst, not just for one FS operation; expected
   behaviour, not a new risk, but worth keeping in mind if a load-time regression is ever reported.
3. Two follow-ups this task found but explicitly did not implement, both noted in Step 3: (a) exporting
   `fiber::sd_routine_held()` through the libdeluge C-ABI would let `discardRecorder`'s assertion (site 8)
   go back to a hard, precisely-targeted check instead of a diagnostic log; (b) `StorageManager::fileExists()`
   appears to read as unconditionally `false` when called off the storage-worker fiber on Embassy (the
   efatfs task-context C-ABI rejects off-fiber callers with `DELUGE_ERR_BUSY` rather than serializing them),
   which would make `save_song_ui.cpp`'s overwrite-confirmation prompt silently skip on Embassy — a
   pre-existing gap this task's reasoning surfaced but did not create, is not scoped to fix, and is being
   tracked separately.
4. All classifications and both Step 3 decisions rest on reading the code plus the one Lens 1 fixture's
   execution evidence; none of this has run on real Embassy hardware yet, consistent with the rest of this
   plan.
5. **The seam self-reports `true` to its own holder, not just to other consumers.**
   `deluge_storage_fs_busy()` is `FS.try_lock().is_err()` (`efatfs_fs.rs:856-858`) — `try_lock()` fails
   for the holder's OWN async context too, not only for outside callers, so a caller reading
   `sd_busy() == true` may be reading back its own hold, not someone else's. This is harmless at every
   deferral site in this document (sites 1, 3, 4, 5, 6): defer-and-retry is lossless whether the busy
   signal is "someone else" or "myself, moments ago on the same call stack." It is the strongest argument
   for the Step 3 E251 downgrade specifically: a hard `FREEZE_WITH_ERROR` there would have fired on the
   CORRECT path where `discardRecorder` runs from the recorder's own owner-dispatched card routine while
   the fiber legitimately holds the mutex for that very op — not only on the false-positive "unrelated FS
   work" case Step 3 already argues from. Unstated premises like this one are exactly the kind that
   already produced a wrong decision on this branch (the `isSDRoutineActive()` "partially live" premise
   corrected earlier in this document); recording it here so it isn't inherited silently again.

## Gate results

Run at HEAD `1f272ddb9` (clean tree throughout; `deluge_host_out.wav`, a `deluge_loadcheck` render
artifact dropped in the repo root by that gate, was deleted afterward — not tracked, not part of this
change).

### 1. Unit + spec suites

`deluge_loadcheck` build (`build-sim`), **clean**:

```
$ cmake --build build-sim --target deluge_loadcheck
[0/2] Re-checking globbed directories...
[1/4] Generating version from git state...
-- Deluge Community Firmware v1.3.0-1f272ddb9
[2/4] Building CXX object app/CMakeFiles/deluge_app.dir/version/version.cpp.o
[3/4] Linking CXX executable deluge_loadcheck
```

`sim_block_host`, **2/2**:

```
$ cd src/bsp/rust && cargo test --test sim_block_host
test block_on_completes_when_hook_makes_progress ... ok
test block_on_wedges_instead_of_spinning_forever - should panic ... ok
test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

`lens1_vt_sim` selftest (`NO_BUILD=1`, binary already built), **both modes PASS**:

```
$ cd src/bsp/rust/harness/lens1_vt_sim && NO_BUILD=1 ./sweep.sh selftest
  --selftest-block:        PASS
  --selftest-block-nested: PASS
SELFTEST: PASS
```

`sync_specs` (built in `build-tests`; CTest name `sd_access_spec`), **3 examples, all passing**,
including the polarity case:

```
$ cmake --build build-tests --target sync_specs
[100%] Built target sync_specs
$ cd build-tests && ctest -R sd_access_spec --output-on-failure
    Start 35: sd_access_spec
1/1 Test #35: sd_access_spec ...................   Passed    0.01 sec
100% tests passed out of 1

$ ./spec_sync/sync_specs sd_access_spec --verbose
deluge::sync::sd_busy
  is false when the filesystem is not busy
  is true when the filesystem is busy
  tracks the seam rather than a hardcoded constant
```

That third example is the polarity case named in the plan's exit criteria (`deluge::sync::sd_busy()`
must track the live seam, not a hardcoded constant); Task 1's own report is where it was proven to fail
before the seam existed — not re-proven here, since the seam is now committed and the point of this run
is to confirm it still passes, which it does. `storage_op_spec` (the sibling spec in the same binary)
also ran clean (`ctest -R storage_op_spec`, 1/1 passed) but is out of this plan's scope.

CI unit suite (`UnitTests`, built in `build-tests`, run exactly as
`.github/workflows/tests.yml:48` does — `-ojunit` from a `results`-style directory), **158/158, 0
failures, 0 errors**:

```
$ cmake --build build-tests --target UnitTests
[100%] Built target UnitTests
$ ./unit/UnitTests -ojunit   # run from a scratch results dir
RANDOM TEST SEEDS = { -281995255, 157223302 }
conputeChangeFrom failed with SM01
conputeChangeFrom failed with SM01
$ echo exit=$?
exit=0
```

(The two `conputeChangeFrom failed with SM01` lines are stderr chatter from an existing test that
deliberately exercises a failure-reporting path — not a suite failure; exit code and the summed JUnit
XML both confirm 0 failures / 0 errors.) Summed across all 26 emitted `cpputest_*.xml` files:

```
total tests: 158
total failures: 0
total errors: 0
```

Matches the brief's stated expectation exactly.

### 2. `deluge_loadcheck` RUN, not just link

Per the brief, the invocation was *found*, not guessed: no `sim/CMakeLists.txt`/`tests/`/`scripts/`
hit gave a literal command line, so the real one came from this same plan family's prior practice
(`docs/superpowers/plans/2026-08-06-r4-phaseC-followups.md:18`, and confirmed working in
`.superpowers/sdd/2026-08-06-r4-phaseC-followups/task-8-report.md`, gate 3):
`./deluge_loadcheck --project <dir with SAMPLES/A.WAV> --file SAMPLES/A.WAV`, run against a scratch
project directory seeded with a real WAV file already present in the tree
(`toolchain/v25/linux-x86_64/arm-none-eabi-gcc/lib/python3.13/test/audiodata/pluck-pcm16.wav`, copied to
`SAMPLES/A.WAV`). It **ran** — not merely linked — and parsed the file:

```
$ ./build-sim/deluge_loadcheck --project <tmp> --file SAMPLES/A.WAV
0.0000: audio_file_manager.cpp:125: Cluster::size  4096 clusterSizeMagnitude  12
0.0000: audio_file_manager.cpp:125: Cluster::size  4096 clusterSizeMagnitude  12
0.0000: deluge.cpp:703: PIC firmware version reported: 0
0.0000: deluge.cpp:725: switching from host to peripheral
0.0000: storage_manager.cpp:87: free clusters:  2668213
[host-audio] capturing 2.00s (88200 frames) -> deluge_host_out.wav
0.0007: deluge.cpp:118: mic 10.0007: deluge.cpp:764: going into main loop
LOADED SAMPLES/A.WAV | channels=2 byteDepth=2 rawFormat=0 sampleRate=11025 dataStart=142 dataLen=13228 lengthSamples=3307 midiNote=-1.0000 loopStart=0 loopEnd=0 wtCycle=2048
exit=0
```

`LOADED` with real parsed descriptor fields (`channels`, `sampleRate`, `dataLen`, `lengthSamples`, …) is
the actual execution proof this gate exists for — this branch's changes (the seam, the guard, the
scheduler ceiling) do not touch the load-check path directly, but this confirms nothing in the
committed work silently broke it.

### 3. The golden differential — a real gate

`cordae`: **PASS, byte-identical.**

```
$ scripts/golden_embassy_diff.sh cordae check
ninja: Entering directory `.../build-embassy-hostapp`
[2/3] Building CXX object app/CMakeFiles/deluge_app.dir/version/version.cpp.o
   Compiling golden_vt_render v0.1.0 (.../src/bsp/rust/golden_vt_render)
   ... (full target-list rebuild; deluge_app object closure changed, forced a relink)
    Finished `release` profile [optimized] target(s) in 8.76s
PASS — cordae MIXDOWN matches golden (db63b128be9748ed…)
```

`icoustic`: **FAIL — a genuine divergence. This is a finding, not a baseline problem. The baseline was
NOT updated.**

```
$ scripts/golden_embassy_diff.sh icoustic check
    Finished `release` profile [optimized] target(s) in 0.03s
FAIL — icoustic MIXDOWN differs from golden
  golden sha256: ae0addcaf5f97774241fdcb6cc18a4f28a8ca31ba161d5a4cfb6e90eeb947489
  render sha256: a565391b293e9d88344e98a95518dc980171159f3d36565f41b460f1709e1fea
  render kept for inspection: /tmp/golden_embassy_diff.icoustic.SqdBpk/render/MIXDOWN_142BPM_E4-MINOR.WAV
```

Diagnostic follow-up on the kept render (read-only inspection, no baseline touched): both files are the
same length (`nframes=35194352`, `sampwidth=3`, `nchannels=2`, `framerate=44100` — 211,166,156 bytes
each), so this is a sample-content divergence, not a truncation/crash. The first differing byte is at
offset 25,096,308, which is frame 4,182,718 — **94.85 seconds into a 798-second render (≈11.9% through
the file)** — after which 5,219,669 of the remaining 186,069,804 bytes (≈2.8%) differ. `cordae` renders
clean while `icoustic` does not, so this is fixture-specific, not a wholesale renderer break.

**A bisect run after this gate refuted the "brief's own prediction" reasoning this section originally
gave** (that deferring the overview scan shifts `icoustic`'s cluster-load timing enough to move its
bytes). Full method in
`.superpowers/sdd/2026-08-07-sd-busy-seam-backing/icoustic-bisect-report.md` — that report is itself
gitignored, so the facts are restated here as the durable record:

- `icoustic` FAILS at the merge-base `921fd1bf9` (tip of the "R5a Phase 0" merge into `next`) AND at
  `e653023ef` (the `next` commit immediately before that merge), and **both commits produce a
  byte-identical render** (sha256 `a565391b29…`) at the exact same fingerprint this branch's HEAD
  produces: onset ~94.846s into a ~798s render, 2.805% of the remaining bytes differing from the
  `ae0addca…` baseline. A branch cannot cause a failure that already reproduces, byte-for-byte, at its
  own merge base.
- The `ae0addca…` baseline was captured 2026-07-30 and is stale relative to whatever actually changed
  the render — not relative to anything in this branch.
- The likely real cause is the separately-tracked "icoustic A-root efatfs range-load" item, **not** any
  of this branch's nine activated `sd_busy()` sites. Root-causing that item is out of this branch's
  scope.
- Bonus finding worth recording here because it has no other durable home: the preceding branch ("R5a
  Phase 0") was **render-inert** on `icoustic` — the two byte-identical renders at `e653023ef` and
  `921fd1bf9` straddle that merge, so whatever it changed did not move this fixture's bytes at all.
- `cordae` staying byte-identical (Gate 3 above) is the stronger signal here, not a weaker one: `cordae`
  is the fixture that actually wedged before this branch's fix, i.e. the one whose cluster-load schedule
  the new guard actually perturbs. If deferring the overview scan shifted timing audibly, `cordae` is
  where it would show first — and it doesn't move at all.

**Conclusion: pre-existing, not caused by this branch.** The bisect refutes the original "consistent
with the brief's own prediction" reasoning — identical bytes at the merge-base and at the pre-merge
commit mean nothing this branch touched is responsible for the divergence. Per this plan's scope, the
failure is CARRIED, not fixed, and the baseline was never updated. The failing render from this gate's
own run is preserved at the path above (the script's cleanup trap is deliberately disarmed on a FAIL,
"render kept for inspection" is not asserted lightly).

### 4. Device build, and re-verify the symbol is naturally rooted

Build: **succeeds**, ARM ELF produced.

```
$ cd src/bsp/rust && DELUGE_BUILD_CONFIG=Debug cargo device --release
warning: `deluge-bsp-rust` (bin "deluge-rust") generated 5 warnings (1 duplicate)
    Finished `release` profile [optimized + debuginfo] target(s) in 8.78s
$ file target/armv7a-none-eabihf/release/deluge-rust
target/armv7a-none-eabihf/release/deluge-rust: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
statically linked, with debug_info, not stripped
```

`nm` count:

```
$ nm -C target/armv7a-none-eabihf/release/deluge-rust | grep -c " T deluge_storage_fs_busy"
1
```

**1, with no manual `-u` link root** — the expected outcome by this task, and confirmation that Task 2's
open question is closed: the C++ closure has been rebuilt since (verified — `audio_engine.cpp.obj` in
the top-level `build/` tree carries a timestamp of 08:43:51, seven minutes ahead of the `1f272ddb9`
commit it belongs to, i.e. this device build reflects the final, fully-committed state of the branch,
not a stale pre-seam closure), and `deluge::sync::sd_busy()`'s nine callers now root
`deluge_storage_fs_busy` through the natural call graph. Cross-checked at the object level:
`sd_access.cpp.obj` shows `deluge::sync::sd_busy()` defined (`T`) and referencing `deluge_storage_fs_busy`
as undefined (`U`) — exactly the edge that, when linked against the Rust side's single `T` definition,
produces the natural-rooting count of 1 seen above.

### Exit-criteria items not re-verified by this task (already established earlier in the plan)

- **`spec_sync` polarity case proven to fail pre-seam**: proven once, by Task 1 (RED before the seam
  existed, GREEN after) — not re-proven here; Gate 1 above confirms it still passes at HEAD.
- **Lens 1 `cordae`, deterministic across three runs**: established by Task 3 Step 4 (three consecutive
  `exit=0` runs, byte-identical `LENS1_RESULT` lines) — not re-run here; this task's Gate 3 `cordae`
  golden-diff run is a second, independent confirmation the fixture still completes cleanly at HEAD.

### Gates NOT run, and why

- **On-device hardware checks — all outstanding, no host build can substitute.** SysEx-during-card-
  activity, save/load, waveform pre-scan, and multi-take record all need a real Embassy device; nothing
  in this session runs on hardware.
- **The scheduler `RESOURCE_SD` ceiling (site 2) cannot be exercised on hardware EITHER, not just off
  it — corrected during final review.** This section originally framed the ceiling as "unverifiable off
  hardware by construction," implying hardware would be able to verify it. It cannot. Verified from this
  branch's own device ELF (`arm-none-eabi-nm -C target/armv7a-none-eabihf/release/deluge-rust | grep -cE
  "TaskManager::|ResourceChecker"` → `0`): the C++ `ResourceChecker`/`TaskManager` this ceiling lives in
  is not linked into the Embassy device build at all — `scheduler.rs` reimplements the
  `scheduler_api.h` ABI specifically to keep the C++ TaskManager out of the link (`scheduler.rs:526-528`)
  and has no `RESOURCE_SD` gate by design (`scheduler.rs:228`, `:465-467`). On the cooperative/C-host BSP
  where `ResourceChecker` IS linked, `deluge_storage_fs_busy()` is hardwired `false`
  (`task_scheduler_c_api.cpp`), so the ceiling is a no-op there too, for the opposite reason. Put
  together: this ceiling is unverifiable off hardware because the busy signal is fake there, AND
  unverifiable on hardware because the code that would consume a real signal isn't in that build — there
  is currently no build this repo produces on which "does the ceiling actually hold off admission" is
  even a meaningful question to ask. **The on-device hardware-smoke watch item this document previously
  carried for site 2 is therefore moot** — it was never going to observe anything. The Embassy-linked
  `golden_vt_render` harness used for Gate 3 does link the real BSP and does exercise the seam's *read*
  side (relevant to the bisect below), but that is orthogonal to the ceiling, which isn't linked into
  that harness's C++ closure either.
- **`icoustic`'s divergence root cause.** Gate 3 captured and stopped, per the brief's explicit
  instruction not to treat a divergence as a reason to `update` the baseline. A later bisect (see Gate 3
  above) ruled out all nine activated `sd_busy()` sites as the cause — the divergence reproduces
  byte-identically at the branch's own merge-base and at the commit before that. The likely real cause is
  the separately-tracked "icoustic A-root efatfs range-load" item; root-causing that remains unresolved
  and out of this task's scope.
- **Lens 2 (`preemptive_race_tsan`) and the full margin sweep.** Not requested by this task's step list
  and not run — the brief's own timing guidance says not to run `sweep.sh`'s full margin sweep, and Lens
  2 is a separate lens from a different plan's gate set, last touched (per memory) in an unrelated,
  unmerged branch.
