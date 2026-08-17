# Storage & execution-model end state

**Status:** north-star architecture design. Brainstormed with Kate 2026-08-08.
**Supersedes:** `docs/superpowers/specs/2026-07-20-async-storage-end-architecture-design.md` (that
doc's strategy holds; its execution model was under-specified and its "where we are" section is
~3 weeks and two ladders stale — see Appendix A for the itemised drift).
**Scope:** the storage stack and the execution model it runs on. This is the storage instance of
`docs/dev/target_architecture_north_star.md`, not a replacement for it.
**Position:** written mid-R5a, after R0–R4 and the `sd_busy()` seam fix merged to `next`
(`997784659`). Defines the target the remaining rungs converge on.

---

## 0. Why this doc exists

The 2026-07-20 end-state design made a load-bearing claim:

> The worker fiber is fully retired. Its sole reason to exist is to let *synchronous* C++ FatFS calls
> cooperatively yield without deadlocking the non-reentrant FatFS. Once Rust owns the FS
> async-natively there is no C FatFS to protect and nothing to yield around.

**R4 deleted C-FatFS. The fiber did not go away.** It is still 706 lines with 69 live
`block_on_fiber` call sites. The claim was wrong about *why* the fiber exists, so the plan built on
it under-scoped the remaining work — R5 had to split into R5a/R5b once the code was read carefully.

This doc replaces that model with one that survived contact: **three execution tiers and a single
blocking invariant.** Everything else here is derived from it.

---

## 1. The end state — three tiers and one invariant

| Tier | Runs on | Contract |
|---|---|---|
| **Audio** | `AUDIO_EXEC` — `InterruptExecutor`, SGI 8, GIC priority 20 | Never blocks, never locks, never allocates. Reads resource-manager RAM chunks; publishes play-cursors and prefetch requests lock-free. **Already real.** |
| **Storage** | `STREAM_EXEC` (SGI 9, priority between audio and thread) + Embassy tasks | Owns all SD I/O and the filesystem. Async-native: `await`s, never spins. Streaming fill, recorder writeback, task-context file ops. |
| **Interaction** | thread mode — C++ UI, sequencer, commands | May **block** on storage completions. Never waits on itself. |

The rule that makes it correct:

> **A context may block only on work owned by a strictly higher-priority tier.**

**Corollary — no context may hold the FS mutex across a suspension.** This is the precise form of the
hazard. A blocking caller cannot starve a *running* holder (thread mode does not preempt itself, and
an IRQ-completed transfer progresses regardless), so the only way a blocker and a holder interleave is
if the holder gave up the CPU while holding the lock. Today the fiber does exactly that inside
`block_on_fiber`, which is the whole pathology of §1.2.

Phases 1 and 2 together remove every suspending holder: the streaming task moves to a tier that
preempts (Phase 1), and I/O-class sites stop suspending at all (Phase 2). What remains is a property
worth checking rather than assuming:

> **`yield_until` must never be reached while the FS mutex is held.**

Its three call sites are UI-level waits (`load_song_ui.cpp:517`, `session_view.cpp:3575`,
`stem_export.cpp:229`), none of which is inside an FS operation — so the property holds today. It
should be an explicit review item for Phase 2 and an assertion where cheap, because it is the single
thing that would reintroduce the deadlock class.

**What this means for `Owner`.** The peer-wait argument is *not* strong enough to make serialisation a
safety requirement — the corollary above does that work instead. `Owner` survives for two narrower
reasons: it is the home of `await` (§2), and it bounds how many interaction-tier operations are in
flight against the FS at once. Whether serialisation remains strictly *necessary* after R5b, as
opposed to merely useful, is an open question for R5b's brainstorm; this doc does not settle it.

### 1.0 The audio-tier contract, AUDITED (2026-08-09)

The audio tier's "never blocks" clause is the load-bearing half of §1's invariant, so it was audited by
reading the real-time render path end to end (`deluge_app_render` → `renderAudio` → voice/sample render →
resource-manager access), classifying every reachable wait.

**Verdict: the real-time render path contains no genuinely-blocking off-fiber call.** The chain that
matters: `sample_low_level_reader.cpp` → `deluge_sample_region_acquire`
(`crates/deluge_sample_source/src/abi.rs:430`) → `acquire_ex` (`cursor.rs:187`), documented and confirmed
"non-blocking, synchronous: schedules a fill if needed but never waits", returning Ready/Loading/
Unavailable immediately; and `ManagerResidency::acquire` (`manager_residency.rs:105`), a cache-hit lookup
or reserve+enqueue+signal that hands work to `streaming_fill_task` and returns.

**Render never touches the storage C-ABI at all.** It terminates at the resource-manager facade — it does
not reach `efatfs_fs.rs`, `sd.rs`, any of the 69 `block_on_fiber` sites, or any of the 29 `on_fiber()`
gates. `deluge_streaming_fill_chunk_blocking` / `_drain_queue_blocking` are reached ONLY by
`stem_export.cpp:224` (offline, deliberately on-fiber), the recorder read-back probe, and test harnesses
— none in the real-time graph.

**This narrows Phase 2's risk surface materially:** collapsing the I/O-class sites cannot make the audio
path block, because the audio path never calls them. Phase 2's risk is entirely that *streaming fill*
starves, not that render itself blocks.

Three nuances the audit surfaced, none a blocker:

1. **`Masked::enter` (`deluge_resource/src/sync.rs:33`) is a no-op on device, a real mutex on host.**
   On device `deluge_in_interrupt()` is true on the audio path so it never masks — sound, because
   `AUDIO_EXEC` cannot be preempted by the fiber, so the `Cell` access is already atomic with respect to
   it. On host (the Lens 2 TSan harness) both sides mask via a real `critical_section`, so a brief
   contended wait is possible — bounded to one `Cell` read/write, not a park. A **harness-fidelity**
   difference, not a device hazard.
2. **⚠️ Discrepancy with this doc's own "never allocates" clause.** `SampleRecorder::feedAudio`, reached
   from `currentSong->renderAudio` (`song.cpp:2464`, `sound.cpp:2610`,
   `global_effectable_for_clip.cpp:139`), calls `allocateBuffer`, which is a lock-free
   `freeBuffers_.try_pop` **or a heap allocation** when the pool is empty. Non-blocking, so it does not
   break the invariant — but an allocation on the render path contradicts the audio tier's stated
   contract. Worth its own look; not part of R5a.
3. **Undetermined corner:** the rest of the render graph (envelopes / LFO / patcher / wavetable band-data
   / MPE / arp) was not exhaustively traced. Wavetable render is believed to reuse the same region/cluster
   mechanism, unverified. Settling it needs a full call-graph trace or an ISR-context instrumented run.

**Scoping note that matters for the harness question:** `audio_host.rs:163-171`'s `should_skip_render()`
livelock concerns driving `deluge_app_render` *synchronously from the fiber's own thread-mode context in
the host harness* — a different scenario from the device's preemptive `AUDIO_EXEC`. So that guard protects
a host-harness hazard, not a device one. If margin-in-sim is ever revisited, driving render from a
context the in-spin `progress_hook` pumps is not obviously blocked by this audit's findings.

### 1.1 The invariant classifies every storage call site

This taxonomy is the doc's main working tool. Every `block_on_fiber` site is exactly one of:

| Class | Waits on | Legal as a block? |
|---|---|---|
| **I/O-class** | a transfer that completes by IRQ | **Yes** — the storage tier outranks the caller. |
| **Progress-class** | another *storage-tier* task's progress | **Only once that task is on `STREAM_EXEC`.** |
| **Interaction-class** | a predicate only user input / thread-mode UI can satisfy | **Never**, at any priority. |

Measured distribution of the 69 code-only sites (`grep`, comment lines excluded):

| File | Sites | Character |
|---|---:|---|
| `src/bsp/rust/src/efatfs_fs.rs` | 30 | device task-context/stream C-ABI |
| `src/bsp/rust/src/efatfs_host_shim.rs` | 30 | host mirror of the same surface |
| `src/bsp/rust/src/sd.rs` | 6 | block-transfer layer |
| `src/bsp/rust/src/streaming_loader.rs` | 2 | **progress-class** (`WaitChunkLoaded`, `WaitQueueDrained`) |
| `src/bsp/rust/src/fiber.rs` | 1 | the primitive itself |

The two 30-site groups are host/device mirrors of one logical API, so the work is one surface twice,
not sixty times.

### 1.2 Why progress-class is a correctness matter, not tuning

`streaming_fill_task` currently runs on the **thread** executor, and its read path holds the FS mutex
across the SD transfer await. Collapse an interaction-tier site to a plain block and it spins on that
mutex while the only context that can release it sits beneath the spin on the same executor. Nothing
breaks the cycle. Audio keeps rendering on `AUDIO_EXEC` while its data starves, so the symptom is
underrun-then-frozen-UI rather than silence — which is what makes it nasty rather than obvious.

Promoting `streaming_fill_task` to `STREAM_EXEC` is therefore a **prerequisite**, not a contingency
for a margin regression. See `2026-08-07-r5a-fiber-io-retirement-design.md` §2 and §3, which reach
this conclusion twice by independent routes.

---

## 2. Interaction-class dissolves into commands

Interaction-class work cannot become a block, so it must stop being a suspended stack.

A storage operation becomes a **reified request submitted through `Owner`**, with an explicit
completion/progress channel:

- `Owner` gains the result-returning variant **its own header already names as planned**
  (`src/deluge/storage/owner.h`: *"`await` (a blocking, result-returning variant) is a later rung;
  this seam is `run`-only"*). This completes a designed seam rather than inventing one.
- Progress and completion ride the **coalescing dirty-set** (`docs/dev/target_architecture.md` §4.3):
  allocation-free atomic set-bit raise, drained once per UI tick, UI re-reads the model. No
  synchronous callback out of storage into the UI.
- The UI returns to its tick loop between phases. **No stack ever spans the operation**, so there is
  nothing to suspend and no `on_fiber()` question to ask.

### 2.1 A storage operation is not a `deluge::edits::Edit`

State this outright, because the M-ladder is at M3 and the shapes rhyme. `deluge::edits::Edit`
reifies **undoable model mutations**. A load or save is neither undoable nor a model mutation. The
two share a shape — reified intent plus explicit completion — and stay **separate types**. Do not
route `LoadSong` through the Edit substrate.

### 2.2 What this deletes

`src/bsp/rust/src/fiber.rs` (706 lines, dual ARM `global_asm!` / host `corosensei`) ·
`block_on_fiber` (69 sites) · `yield_until` and its three call sites · the `deluge_worker_*` C-ABI
and its cooperative default in `task_scheduler_c_api.cpp` · and all **29** `on_fiber()` rejection
gates (27 in `efatfs_fs.rs`, 2 in `sd.rs`).

---

## 3. Deleting the gates *is* the feature work

One user-visible defect is currently tracked as a bug blocked on this arc, and one former defect is
now a residual inefficiency rather than a failure.

| Symptom | Mechanism |
|---|---|
| **Saving cannot succeed off-owner on the Rust BSP.** `performSave` runs from plain UI handlers (no `Owner::run` under `gui/ui/save/`); every write entry point rejects off-fiber, so the save fails at its first write. The skipped overwrite prompt is a symptom of the same cause, not a second bug. | the 29 gates |
| **The #4460 waveform pre-scan cannot fill a cold cluster *synchronously*.** `deluge_streaming_fill_chunk_blocking`'s off-fiber branch cannot suspend a stack, so it always reports not-ready. | the same gates, one layer down |

The save case needs Phase 2. The pre-scan case no longer does.

**Correction (2026-08-16): the pre-scan row was overstated and is now stale.** It previously read
"can never fill a cold cluster … the scan round-robins forever and churns priority-0 enqueues every
`slowRoutine` tick". That is no longer true, and the underlying defect was never the gate. The real
cause was that fire-and-forget enqueues were discarded because the load queue required a lease it did
not own — fixed in `94d1abc3d` (see the `overview-prescan-dead-on-device` memory). Today
`streaming_loader.rs:174-198` enqueues the chunk under a **queue-owned** lease and wakes the drain
*before* the off-fiber branch returns `false`, so the cluster does load asynchronously and the
consumer's next tick finds it resident. What survives is a shape, not a bug: the off-fiber caller gets
one wasted tick per cold chunk instead of the data. Phase 2 tidies that away by letting the call
complete inline; it is not fixing a never-fills failure.

**Correction worth preserving:** the save case was recorded earlier as a data-loss risk. It is not.
The destructive `unlink` is doubly unreachable — `createJsonFile` fails first and bails, and the
`unlink` is gated on `fileAlreadyExisted`, which is always false. The original song is never touched.
The failure is visible and loud, not silent.

---

## 4. What survives — so it is not re-litigated

- **`Owner` is permanent** (per `[[ui-storage-decoupling-northstar]]`: the seam is permanent, the
  fiber backing is not). Its rationale changes, not its existence. The original justification
  (serialise ops so they never re-enter non-reentrant FatFS) **died with R4** and `owner.h`'s comment
  saying so is stale. What survives is narrower than that comment: `Owner` is the home of `await`
  and the place in-flight interaction-tier storage work is bounded — not, per §1's corollary, a
  safety requirement.
- **`Owner::run`'s fire-and-forget contract survives** for genuinely fire-and-forget work. What is
  missing is `await`, not a different seam.
- **Save's end state is `Owner::await`.** Migrating save onto `Owner::run` was correctly rejected
  (Kate, 2026-08-08) — but the reason is that fire-and-forget cannot report whether a write
  succeeded, which is unacceptable for a save. It was never a headcount argument about growing a
  doomed mechanism; the seam is not doomed.
- **`on_fiber()` keeps a meaning until R5b.** It answers "am I on the storage worker", which is still
  a real question while `yield_until` exists. What changes at Phase 2 is that it stops being a
  precondition for doing I/O.

---

## 5. The ladder from here

| Rung | Content | Unblocks |
|---|---|---|
| **R5a Phase 0 residue** | Tasks 4/5/6 — the load-while-streaming contention scenario, the baseline margin sweep with Control A re-proven, the 0d deadlock-evidence spike. Newly possible: Lens 1 completes at all only since the `sd_busy()` seam fix. | the gate itself |
| **R5a Phase 1 — `STREAM_EXEC`** — **DONE 2026-08-16** (`feat/r5a-stream-exec`) | Device: a second `InterruptExecutor` on SGI 9 at GIC priority 24, between `AUDIO_SGI_PRIORITY` (20) and thread mode, so audio preempts streaming and streaming preempts the interaction tier. Lens 1: the fill task onto `HP_EXEC`. Threaded host BSP: a third `"deluge-stream"` thread. **Prerequisite the design did not have:** the manager's mask-skip predicate asked "am I in an interrupt" when the invariant is "am I the context that preempts all the others", so a third interrupt context would have deleted its own critical sections — fixed with `deluge_in_audio_context()`. The mutex-class test Phase 0 did not prove is delivered as `--selftest-block-mutex` with `--selftest-block-mutex-control` (holder on MAIN, must wedge) keeping it non-vacuous; the self-identifying wedge diagnosis as `preempt::in_spin()`. Owed to hardware: SGI priority ordering, nested IRQ-stack depth. | Phase 2's correctness |
| **R5a Phase 2 — collapse I/O-class** | Classify all 69 sites; collapse I/O-class to `embassy_futures::block_on`; leave the 2 progress-class on the fiber as R5b's inheritance. **Delete the 29 gates** — this is the rung where the sync→async bridge stops requiring the fiber. | **save off-owner** (§3); the pre-scan's wasted-tick shape follows, but it is no longer broken |
| **R5a Phase 3 — cleanups** | Delete `SD_BUS` (redundant since C-FatFS: every block transfer already serialises on the FS mutex) · `off_fiber_instant` if Phase 0a made it unnecessary · `RESOURCE_SD`, `RES_ERROR`, `RES_WRPRT` (live `-Wunused` warnings) · four stale C++ comments citing deleted C-FatFS globals · re-examine `deluge_storage_on_owner` and `stats::note_read(on_fiber())`. | warning-clean device build |
| **R5b — commands** | `Owner::await`; the three `yield_until` sites become commands with dirty-set progress; **delete the fiber**. Needs its own brainstorm → spec → plan. | interaction-class gone |
| **Optional tail** | SP5 (derive prefetch from published play-cursors, retire the C++ enqueue API) and SP6 (convert/stitch → Rust, zero FFI in the fill task). **Both YAGNI-gated**; the architecture is complete without either. SP6 additionally requires a dual-arch bit-exact spec. | zero-FFI fill task |
| **Legacy BSP retirement** | Committed (Kate, 2026-07-22). The end state has **one** BSP; efatfs is Rust-BSP-only and the RZA1 streaming read is retired-not-migrated. | one architecture, not two |

`Owner::await` is R5b's, but §4 means Phase 2 does not wait for it: Phase 2 makes off-owner storage
*work*, and R5b makes it *well-structured*.

---

## 6. Exit criteria

The arc is done when all of these hold:

- [ ] `src/bsp/rust/src/fiber.rs` deleted; zero `block_on_fiber`, `yield_until`, `deluge_worker_*`
- [ ] zero `on_fiber()` rejection gates
- [ ] `Owner::await` exists; no interaction-tier code touches the FS outside `Owner`
- [ ] save / load / browse work on device on the Rust BSP
- [ ] the #4460 pre-scan fills cold clusters on device (already works asynchronously since
      `94d1abc3d`; the criterion is that the off-fiber call completes inline, with no wasted tick)
- [ ] `SD_BUS`, `off_fiber_instant`, `RESOURCE_SD` gone (`currentlyAccessingCard` already is)
- [ ] one BSP — RZA1 retired
- [ ] gates green: Lens 1 margin no-regression with Control A non-vacuous · Lens 2 zero new races ·
      `fs_differential` clean · `golden_embassy_diff.sh` byte-identical on cordae / highsiderr /
      icoustic · `deluge_loadcheck` RUN · on-device flash

---

## 7. Verification model

Harness-first; device is a periodic confidence pass, not a per-rung blocker.

| Gate | What it proves |
|---|---|
| **Lens 1** (`lens1_vt_sim`) | Deterministic **wedge / livelock detection**. ⚠️ NOT a margin instrument — corrected 2026-08-09: lens1 never renders audio (`should_skip_render` skips every tick), so the underrun counters are unreachable, `cluster_reads` is actually the #4460 pre-scan, and Control A cannot fire. Margin evidence comes from hardware. See the R5a spec §3 |
| **Lens 2** (`preemptive_race_tsan`) | No new races — adding an executor is exactly this risk class |
| **`fs_differential`** | Byte-exact FS correctness against a real C-FatFS oracle on real FAT16/32 images |
| **`scripts/golden_embassy_diff.sh`** | The **only** golden renderer, and it links the Rust BSP — so it sees the fiber, both executors and every lock |
| **`deluge_loadcheck` RUN** | Real execution, not merely linking |
| **Device flash** | SGI priority ordering is a hardware property; audio under storage load |

**On using the goldens differentially:** `golden_vt_render` has known absolute divergences tracked from
U4b. A known absolute divergence does not invalidate a **before/after self-comparison** — each rung
asserts "this change altered nothing," not "the Embassy renderer matches C-host." So the harness gates
without first being closed.

**Correction (2026-08-08):** an earlier draft of this doc called icoustic's divergence an "A-root efatfs
range-load" issue. **That label was never established and measurement refutes it.** icoustic's left
channel diverges *during digital silence*, which no wrong-bytes-loaded mechanism explains; the right
channel is bit-identical across all 798 s. It is a low-level arithmetic/rounding divergence in the left
output path — a **render/DSP** matter with no storage component. Do not treat it as part of this arc.
Full characterisation is recorded in the `icoustic-golden-divergence` project memory.

**Golden cannot gate the FS layer itself.** Correct bytes produce identical audio regardless of which
filesystem read them, so FS-layer correctness lives in `fs_differential`, not in the goldens.

---

## 8. Known-uncovered

Gaps, not risks. Named here so they are not rediscovered as surprises.

- **MAIN starvation during a nested spin.** The Phase-0a hook wakes MAIN tasks but nothing polls MAIN
  until the spin returns, so every MAIN task slips by the full modeled latency in virtual time. This
  shapes Lens 1's only output through a mechanism no test covers — **a Phase 2 margin *improvement*
  could be an artifact of the audio task's virtual cadence slipping rather than streaming improving.**
  Do not trust a margin delta without accounting for this.
- ~~**Mutex-class spins are unproven on host.**~~ **CLOSED by Phase 1 (2026-08-16).** Phase 0
  proved the Timer-class case only, and mutex-class spins were unsatisfiable while the fill
  task sat on MAIN. Phase 1 moved it to `HP_EXEC` and carried the test:
  `--selftest-block-mutex` (must complete) plus `--selftest-block-mutex-control` (holder back
  on MAIN, must wedge) — both in `sweep.sh`'s `selftest` verb, both passing. The control is
  what keeps the positive case from passing on an uncontended lock; do not drop it because it
  "always passes".
- **The third host thread immediately earned its keep: TSan found a real ordering bug in the
  promotion itself.** `ProdOps::new()` (the fill task's first act) reaches
  `GeneralMemoryAllocator::ensureClusterSystem()`, which reads the global `Cluster::size` that
  `deluge_app_init` writes via `AudioFileManager::init()`. While the fill task shared a context
  with `deluge_app_init` that could not interleave; on `STREAM_EXEC` (SGI live before
  `app_task` runs) and on the `"deluge-stream"` thread it can. Device consequence was worse
  than the host race: the cluster system could be built from the 32768 default instead of the
  card's real size, silently wrong on any non-32KB card. Fixed by waiting on `FILL_WAKE` before
  constructing — complete rather than narrowing, since nothing signals it until the C++ enqueue
  path runs, and the `Signal` latches. **This is the argument for the host thread: without it
  the bug would have been device-only and invisible to every host gate.**
- **One uncatalogued TSan site survives, pre-existing and diagnostic-only.**
  `deluge_debug_now_frames` (`resource_clock.cpp:31`) reads `AudioEngine::audioSampleTimer`
  unsynchronised while `deluge_app_render` writes it. Reached from the loader-latency
  instrumentation in `Manager::mark_ready`. It raced the same way before Phase 1 — the reader was
  simply on the app thread rather than the streaming one — and is benign on device (aligned
  32-bit read of a wrapping diagnostic timestamp, as `resource_clock.cpp:29` already states).
  Not suppressed and not catalogued: cataloguing it would suppress future detection of the same
  signature. It makes `preemptive-race-tsan` exit non-zero, so it needs a disposition (relaxed
  atomic on both sides, or an explicit catalog entry) before that gate can be clean.
- **A third preemptive context invalidated the manager's mask-skip predicate**, and nothing
  named this before Phase 1 hit it. Worth carrying forward as a shape, not just a fixed bug:
  every time a new context is added that touches shared audio/resource state, re-ask which
  context preempts which. The invariant is "only the highest-priority toucher may skip";
  `deluge_in_interrupt()` is not that question.
- **`golden_vt_render/build.rs:73-74`** does an unfiltered `collect_objs`. Schedule before Phase 2.
- **`sd_image.rs` leaks tmpfs** — roughly 573 MB per PID, never removed.
- **On-device debt is the largest single gap.** The R4 arc plus roughly 115 M-ladder commits have
  never run on hardware, and Phase 1's SGI priority ordering is confirmable *only* on hardware.
  Phase 1 adds a second hardware-only item: **nested IRQ-stack depth.** Audio can now nest on
  top of a streaming SGI frame where it previously had the 8 KB IRQ stack to itself. The
  streaming side measures at order 1 KB off the release ELF (largest single frame on the fill
  path is `efatfs_fs::read_at` at 244 bytes), but the audio render's own peak is not measurable
  off-hardware, so the nested total is unverified rather than known-safe.
- **The device image does not build from a clean checkout on `next`.** `src/bsp/rust/Cargo.toml`
  pins `deluge-bsp` to a git rev that predates the pad-scroll opcodes `control.rs` calls, so
  `cargo device --release` fails to compile until the pin is resolved. Phase 1 worked around it
  with a clearly-marked local `[patch]` to the sibling checkout (which also has to patch
  `rza1l-hal`, or two copies of it collide on `_embassy_time_now` at the LTO link). Separately,
  `./dbt build Debug` fails its device link on a pre-existing `.ARM.exidx` / `.rtt_cached_reserve`
  overlap. Neither is caused by this arc, and both block device verification for it.

## 9. Risks

1. **The margin regression may be real.** Removing the mid-transfer yield is a genuine change, and
   Phase 0 models task-context latency that `off_fiber_instant` currently exempts — so the sweep is
   more likely to show something than less. Phase 1 is the designed answer; if it is insufficient,
   stop and reassess rather than patch.
2. **A second interrupt executor is new concurrency.** Priority inversion and lock-order reasoning
   need care; Lens 2 is the gate, and Phase 3's `SD_BUS` deletion simplifies it by removing a level.
3. **The `yield_until`-holding-FS property is unenforced.** §1's corollary rests on it, and today it
   holds only because none of the three call sites happens to sit inside an FS operation. Nothing
   prevents a future one from doing so, and the failure mode is the deadlock class this whole arc
   exists to remove. Make it a Phase 2 review item; assert it where cheap.
4. **P1 (preemptive audio) still owes its hardware gate.** R5a's execution depends on it.

## 10. Out of scope

- **The error conflation** — `StorageManager::fileExists` is `File::open(...).has_value()`, so it
  reads `DELUGE_ERR_BUSY` as *absent*. Wrong under any architecture, authorised independently, and
  fixed without reference to this ladder.
- **icoustic's golden divergence** — pre-existing (byte-identical renders at `921fd1bf9`, `e653023ef`
  and two later runs) and, per the 2026-08-08 characterisation, **not a storage problem at all**:
  left-channel-only, present in silence, ~−58 dB arithmetic/rounding. A render/DSP item.
- **exFAT** — FAT16/32 + LFN only. This is what makes a Rust-native FS tractable.
- **Flash settings (`flash_storage`)** — SPI/NVM flash, not the SD FAT volume.
- **SRAM residency (SP-R)** — a parallel track meeting this one only at the resource manager's
  per-asset backing-selection seam. Gated on the allocator's SRAM second pool.
- **The whole-program architecture plan** — `docs/dev/target_architecture_north_star.md` owns that.

---

## Appendix A — drift corrected from the 2026-07-20 design

Recorded so the supersession is auditable.

| 2026-07-20 claim | Reality on 2026-08-08 |
|---|---|
| "the frontier lives on `feat/async-sd-owner-substrate`" | Long merged; the arc has since run R0→R4 plus the entire SR/U region-port and SampleStream-collapse ladders |
| "recorder / preview / browser still run on the C++ fiber" | R3 moved the recorder; the browser is on the port |
| "the cluster→sector-map trick retires" (future) | Retired at R1 |
| `SP-delete` = delete C-FatFS **and** retire the fiber, one step | R4 deleted C-FatFS; fiber retirement became its own rung, then split R5a/R5b |
| "the worker fiber's **sole** reason to exist is non-reentrant C FatFS" | **Wrong, and load-bearing.** Its surviving job is bridging sync C++ to async Rust FS, plus suspending stacks for interaction-class waits |
| "C++ runs on a plain blocking worker context" | Under-specified. Blocking is legal only per §1's invariant; interaction-class work can never block |
| Streaming stays above the `file_io.h`/`stream_io.h` seam | Superseded by the region port — streaming relocated *below* into Rust |
| Rust BSP work lives in `deluge-sdk` | `~/GitHub/deluge-sdk` is the strategic repo, but the BSP that runs today is in-tree at `src/bsp/rust/` |
| P1 (preemptive audio) gates only SP-delete | Still true, and still owed |

## Appendix C — measured constraint: the fill task waits ~35 ms to be scheduled

Measured on device 2026-08-16 (Rust BSP, release, RTT capture), instrumenting the resource manager's
load queue with a timestamp at `loader_enqueue` and again at `loader_next` and `mark_ready`:

Two passes were taken. The first accumulated counters for the whole session; the second reset them
every 2 s — and that mattered. The cumulative means were inflated by song-load bursts, and the
cumulative maxima by idle gaps (one reported a 79-second "latency" for a chunk left enqueued while
playback was stopped). **The per-interval figures are the ones to design against:**

| Phase | steady mean | steady max | song-load burst |
|---|---|---|---|
| enqueue → `loader_next` (**scheduling**) | ~700 frames = **16 ms** | ~1100 = 25 ms | up to 11444 = 260 ms |
| `loader_next` → `mark_ready` (**read + convert**) | ~255 frames = **5.8 ms** | ~500 = 11 ms | ~5717 = 130 ms |

Fill time cross-validated across both passes (247 cumulative vs 239–269 per-interval), which is what
gives confidence in the split. Scheduling delay did not: 35 ms cumulative against ~16 ms
per-interval. **Quote the per-interval number.**

**Roughly three quarters of a streaming chunk's service latency is spent waiting for the fill task to
run**, not doing I/O — 16 ms against 5.8 ms in steady state. The card is fast: 5.8 ms for a 32 KB
cluster is ~5.7 MB/s. Against the ~186 ms a 32 KB cluster of 44.1 kHz stereo 16-bit audio buys, a
16 ms scheduling delay is ~9% of the budget consumed before any work starts — and the 260 ms
song-load burst exceeds a whole cluster's budget on its own.

Two consequences for this design:

1. **The storage tier's fill task needs a scheduling guarantee, not just a priority.** Whatever
   replaces the current arrangement should bound how long an enqueued fill can go unpolled.
   **16 ms steady-state mean is the number to beat, and the 260 ms song-load burst is the tail that
   matters** — it alone exceeds a cluster's entire 186 ms budget, which is consistent with dropouts
   clustering around load and heavy passages rather than arriving uniformly.
2. **Deeper lookahead is worth costing separately.** The 380 ms fill maximum says SD stalls are real
   and unbounded in the tail, so no scheduling guarantee alone removes underruns — the one standing
   prefetch (one cluster, ~186 ms) cannot absorb a 380 ms stall.

### What this rules out

Three hypotheses were tested and eliminated before arriving here, recorded so they are not retried:

- **Chunk-table scan cost** — real and large (5–27% of CPU), fixed by indexing the manager's
  `(asset, index)` and backing-pointer lookups on 2026-08-16. Force-culls across a capture fell from
  195 to 12, and the sample-preview truncation it caused is gone. It was NOT the cause of streaming
  underruns.
- **Loader queue ordering (priority inversion)** — the queue was ordered by `Voice::getPriorityRating`
  rather than by deadline, which looked like a textbook inversion. Measured queue depth is **mean 1.19
  cumulative, and mean 0.5 per-interval in steady playback**: `loader_next` finds nothing queued at
  all half the times it polls, so there is routinely not even one candidate to order. See
  `2026-08-16-loader-deadline-scheduling-design.md` for the full negative result.
- **Bandwidth** — ~5.9 MB/s measured against ~176 KB/s per streaming voice. Roughly 30 concurrent
  streams before saturation; normal polyphony is far below it.

## Appendix B — related documents

- `docs/dev/target_architecture_north_star.md` — the whole-program map this is one track of
- `docs/dev/target_architecture.md` §4.1–4.4 — execution contexts, task↔audio sharing, the
  coalescing dirty-set, commands
- `docs/superpowers/specs/2026-08-07-r5a-fiber-io-retirement-design.md` — R5a in detail
- `docs/superpowers/specs/2026-07-22-rustfs-migration-roadmap-design.md` — the R-ladder as executed
- `docs/dev/sd_busy_audit.md` — the filesystem-busy seam's 9-site consumer audit
- `docs/superpowers/specs/2026-08-16-loader-deadline-scheduling-design.md` — the deadline-ordering
  attempt and why it could not work (Appendix C summarises the outcome)
- `docs/superpowers/specs/2026-08-16-voices-cross-tier-race.md` — `Sound::voices_` is mutated by the
  audio tier without exclusion; found while investigating the same dropouts

The measurement is live on `next` — `Stats::queue_wait_*` / `fill_*` / `loader_depth_*`, dumped every
2 s with a per-interval reset by `audio_engine.cpp`, clocked by
`src/deluge/io/debug/resource_clock.cpp`. The refactor can therefore check itself against these
numbers as it lands, rather than re-deriving them.
