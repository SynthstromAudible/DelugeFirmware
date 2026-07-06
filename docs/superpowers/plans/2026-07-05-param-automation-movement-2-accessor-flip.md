# Param automation Movement 2 — accessor op-flip Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or subagent-driven-development).
> Steps use checkbox (`- [ ]`) syntax. Design: `docs/superpowers/specs/2026-07-03-param-automation-accessor-projection-design.md`.

**Goal:** Complete the accessor/projection target — move the engine's ~30 node operations off the raw
`ParamNodeVector` and onto the DOM's `param_edit` kernels + a `DocAutoParam` handle, so `AutoParam` depends on an
**API**, not a concrete container. This is the step that makes the DOM boundary (C-ABI / Rust / serde) real.

**Architecture:** After Movement 1 every clip/drum `AutoParam` borrows DOM-owned nodes via `nodes_`
(`ParamNodeVector*`). Movement 2 forwards `AutoParam`'s edit ops to `deluge::project::param_edit` kernels,
converts the per-tick RT read path to DOM reads, and — the target — **removes every engine→DOM pointer**. End
state: **`AutoParam` = `{ interpolation cursor }` only** (no `nodes_`, no `doc_`). The node data
(`DocAutoParam` = `{current_value, nodes}`) lives in the owner's store; every op takes the `DocAutoParam` as a
*parameter*, resolved by the caller (`ParamSet`/collection, which holds `domStore_` and iterates by index). A
transitional `doc_` (`DocAutoParam*`) is used as scaffolding through M2-B/C and **dropped in M2-E**.

Why fully drop the pointer (not just retype `nodes_`→`doc_`): a stored `doc_` is the *same* coupling as `nodes_`
— a raw engine pointer into DOM memory with the same lifetime/relocation hazard. Removing it (caller passes the
`DocAutoParam` fresh each call) (a) **eliminates the relocation-dangle hazard class** that dominated Movement 1
— with no stored pointer, the variable stores (MIDI-CC / patch cables) can grow freely on runtime add — and
(b) lets **universal binding complete**: every param always has a store slot, so the unbound `ownNodes_`/`ownDoc_`
fallback is *removed*, not retyped. `currentValue` (the RT-interpolated cursor the DSP reads per-sample) stays in
`AutoParam` and is NOT merged with `DocAutoParam::current_value` (the base value, DOM-side) — they coincide only
for unautomated params.

**Tech Stack:** C++23; DOM types `DocAutoParam`/`DocParamSet` + kernels in `src/deluge-project/param_edit.*`;
sim/golden + `param_projection_test` (dual-arch x86 SIMDe + ARM/qemu) as the per-op oracle.

## Global Constraints

- **Behaviour-preserving / byte-identical** at every step. Golden `OK=15` (x86≡ARM) is a hard gate; each forwarded
  op MUST match the engine op — exactly what `param_projection_test` already asserts for ported kernels.
- **HARDWARE GATE PREREQUISITE.** Movement 1 has NOT been validated on-device. Phases M2-D/E touch the RT read
  path and the automation-recording handoff, which the golden/harness structurally cannot see. **Do not start
  M2-D until Movement 1 is validated on hardware** (record/draw automation on audio + synth + kit-drum → play →
  undo/redo → save/reload). M2-A/B/C are golden-verifiable and may proceed before that.
- Every `param_edit` kernel is a *faithful port* (pure data, timeline context passed explicitly, no engine
  globals) with a dual-arch oracle case. clang-format pre-commit reformats+aborts (re-run `git add -A && git
  commit`). No `Co-Authored-By`. Commit per task.
- Verify: `cmake --build build-sim --target param_projection_test && ./build-sim/param_projection_test`;
  golden `songs_run.py` (x86) + `--binary build-sim-arm/song_roundtrip --wrapper 'qemu-arm …'` (ARM); firmware
  `./dbt build Debug`.

---

## Phase M2-A — Port the remaining edit-op kernels to `param_edit` (SAFE, golden-untouched)

Purely additive: port each engine-only edit op to a `deluge::project::param_edit` kernel with a dual-arch oracle
case. The engine keeps using its own ops (no forwarding yet), so behaviour is unchanged and the golden can't move.
This is the mechanical bulk and de-risks M2-C (the kernels exist + are proven before any flip).

**Files:** `src/deluge-project/param_edit.{h,cpp}` (add kernels); `tests/projection/param_automation_test.cpp`
(oracle case per kernel, diffing engine op vs kernel over the existing config/case matrices).

Kernels to add (each: port + oracle diff + commit). Order easy→hard:

- [ ] **`shiftParamVolumeByDB(DocAutoParam&, float)`** — value transform; oracle-diff vs `AutoParam::shiftParamVolumeByDB`.
- [ ] **`transposeCCValuesToChannelPressure(DocAutoParam&)`** — value remap; oracle-diff.
- [ ] **`deleteAutomation(DocAutoParam&)`** — clear nodes (the node kernel; the engine's Action/notify stays at the
  wiring seam, as `deleteNodesWithinRegion` already does).
- [ ] **`trimToLength(DocAutoParam&, int32_t newLength, int32_t loopLength)`** — node kernel (drop the Action arg;
  snapshot is a wiring concern).
- [ ] **`makeInterpolationGoodAgain(DocAutoParam&, int32_t clipLength, int32_t quantizationRShift)`** — oracle-diff.
- [ ] **`nudgeNonInterpolatingNodesAtPos(DocAutoParam&, int32_t pos, int32_t offset, int32_t lengthBeforeLoop)`** —
  node kernel (Action at the seam).
- [ ] **`appendParam(DocAutoParam& dest, const DocAutoParam& src, int32_t oldLength, int32_t reverseThisRepeatWith
  Length, bool pingpongingGenerally)`** — the append/repeat concat; oracle-diff.
- [ ] **`flattenReverse(DocAutoParam&, int32_t reverseDirectionWithLength)`** — already reasoned about in the
  clone-idiom work; oracle-diff (the `param_clone_test` reversed-position cases are the reference).
- [ ] **`copy`/`paste`** node kernels (`CopiedParamAutomation` is a value carrier already) — oracle-diff over the
  Phase-1 copy/paste cases.
- [ ] **`stealNodes`/`insertStolenNodes`(DocAutoParam&, …, StolenParamNodes*)** — the region steal/restore node
  kernels; oracle-diff. (Higher effort — region + loop-wrap.)
- [ ] **`setValueForRegion` / `setValuePossiblyForRegion`** node kernels — the region-write path; oracle-diff.
- [ ] **`setCurrentValueInResponseToUserInput` (recording)** — **the delicate one.** It interleaves the live node
  write with `processCurrentPos`'s `recordOverNodeJustReached` latch (auto_param.cpp:529-539). Port the *node
  mutation* only; the latch/override state is engine RT-cursor state that stays in M2-D. Oracle-diff the node
  result for a scripted sequence of live-value writes.

**Verify (M2-A):** `param_projection_test` OK (x86+ARM) with every new oracle case; golden `OK=15` unchanged (no
engine op forwarded yet); firmware builds. **Deliverable:** the DOM owns a complete, proven edit-op suite.

---

## Phase M2-B — Transitional handle: `nodes_` (`ParamNodeVector*`) → `doc_` (`DocAutoParam*`)

Retype the borrow handle so ops can forward to kernels that take `DocAutoParam&`. Replace `ownNodes_`/`nodes_`
with `ownDoc_` (`DocAutoParam`) / `doc_` (`DocAutoParam*`, default `&ownDoc_`); `nodes()` becomes `doc_->nodes`;
`borrowNodeStorage` points `doc_` at the DOM store's `DocAutoParam` (not just its `.nodes`). The Phase-B
self-pointer copy/move machinery carries over verbatim, retyped. **This is scaffolding — `doc_` is removed in
M2-E.** `currentValue` stays a distinct `AutoParam` member (the RT cursor); it is NOT merged into
`doc_->current_value`.

- [x] Retype members + `nodes()`; adapt copy/move/`borrowNodeStorage`/`swapState`.
- [x] Point `borrowNodeStorage`/`bindDomStore`/`bindMainParamSetDomStores` at the DOM `DocAutoParam`
  (`&store.params[i]` instead of `&store.params[i].nodes`).
- [x] **Verify:** golden `OK=15` x86≡ARM (behaviour-preserving retype), `param_projection_test` OK,
  `song_edit_roundtrip` PERSISTED, firmware builds. **DONE `1e0130557`.**

---

## Phase M2-C — Forward `AutoParam` edit ops → `param_edit` kernels (op-by-op)

With `doc_` in hand and all kernels ported, replace each `AutoParam` edit-op body with a forward:
`deluge::project::param_edit::<op>(*doc_, …timeline ctx…)`, keeping the engine method as the thin wrapper that
supplies timeline context + Action/notify (the "wiring seam"). One op per commit; each is verified by the existing
`param_projection_test` oracle (the forward must equal the pre-forward engine op) + golden.

- [x] Forward the already-ported ops (setNodeAtPos, homogenizeRegion, insertTime, deleteTime, shiftHorizontally,
  shiftValues, generateRepeats, moveRegionHorizontally, deleteNodesWithinRegion, getDistanceToNextNode) — DONE
  across 5 commits (`05607c46c`..`2eaa31d95`).
- [x] Forward the M2-A-ported ops that are clean node cores (appendParam, flattenReverse, nudge, transpose,
  makeInterpolationGoodAgain) — DONE.
- [x] **Verify per batch:** golden `OK=15` x86≡ARM; `param_projection_test` OK x86≡ARM; `song_edit` PERSISTED;
  `clip_clone` OK; firmware builds. **16 ops now run through the DOM API.**

**Deferred out of M2-C (principled, not skipped):**
- `setValueForRegion` / `setCurrentValueInResponseToUserInput` (recording) → **M2-D**: entangled with the RT
  interpolation cursor (`resetInterpolationIncrement`), `view.notifyParamAutomationOccurred`, and playback state;
  the kernel's `optional<bool>` return can't reconstruct the reset-vs-notify sub-branch. Belongs with the RT seam.
- `trimToLength`, `stealNodes`/`insertStolenNodes`, `copy`/`paste` → **M2-E**: their node storage is the engine's
  undo/clipboard memory (`trimToLength`'s action-branch keeps old nodes for the snapshot-steal; `StolenParamNodes`
  and `CopiedParamAutomation` point into `alloc_fast`/`alloc_sdram`). The kernels use `std::vector` carriers, so a
  clean swap needs the undo/clipboard record restructured — which is exactly what M2-E's signature-threading does.
- `deleteAutomation` = trivial `nodes().clear()` + wiring; no dedicated kernel warranted.

---

## Phase M2-D — Route the RT read path + recording through DOM kernels

**Framing correction (post-M2-B/C).** After M2-B, `nodes()` already resolves to `doc_->nodes`, so the RT path
already *reads DOM-owned nodes*. M2-D is therefore NOT about changing where reads point — it's about routing the
last read op (`getValueAtPos`) and the recording write (`recordLiveValue`, `setValueForRegion`) through the DOM
**kernels**, so no engine method carries its own copy of the projection/record math. The per-voice interpolation
**cursor** (`currentValue`, `valueIncrementPerHalfTick`, `renewedOverridingAtTime` latch) stays engine-side — it
is RT state, not node data, and is explicitly out of scope for the DOM (spec §7).

This splits by risk. **M2-D.1 is sim-verifiable** (oracle covers `getValueAtPos`); **M2-D.2/.3 are the recording
seam and MUST NOT start before the Movement-1 on-device gate is green** (record/draw automation on audio + synth
+ kit-drum → play → undo/redo → save/reload). Keep M2-D.1 as its own commit so it can land pre-hardware.

### M2-D.1 — Forward `getValueAtPos` to the DOM read kernel (sim-verifiable, LOW risk)

`AutoParam::getValueAtPos(pos, modelStack, reversed)` (auto_param.cpp:1220) is a pure read that internally
lazily fetches `modelStack->getLoopLength()`. The kernel `param_edit::getValueAtPos(const DocAutoParam&, uint32_t
pos, int32_t loopLength, bool reversed)` already exists and is oracle-tested. `getLoopLength()` is always safe to
call eagerly (it's a length, not a null-deref; `processCurrentPos` already calls it unconditionally at line 453).

- [x] **Step 1:** Replace the `getValueAtPos` body with `return deluge::project::getValueAtPos(*doc_, pos,
  modelStack->getLoopLength(), reversed);`. Its callers — `setPlayPos`, `grabValueFromPos`,
  `getValuePossiblyAtPos`, and the still-engine-native `trimToLength`/`copy`/recording paths — then read
  through the DOM kernel automatically; no caller edits needed.
- [x] **Step 2 (verify):** `param_projection_test` OK x86≡ARM; golden `OK=15` x86≡ARM; firmware builds. **DONE
  `df545adbc`.**

  *Caveat (already satisfied):* the kernel takes `loopLength` where the engine special-cases `2147483647`
  (infinite length); the getValueAtPos oracle already diffs engine vs kernel across `loopLengths[] = {128,
  2147483647}` × positions × reversed (param_automation_test.cpp:215), so the infinite-length path is covered.

### M2-D.2 — `setValueForRegion`: RESOLVED BY ANALYSIS (no forward needed)

**Reversed the earlier plan after auditing the op.** `AutoParam::setValueForRegion`'s only node *mutations* are
(a) its `homogenizeRegion(modelStack, …)` call — **already routed through the DOM kernel by M2-C's homogenizeRegion
forward** — and (b) the whole-region `deleteAutomation` branch, a trivial `nodes().clear()` on the borrowed
(DOM-owned) store. Everything else it does — `currentValue = value`, `resetInterpolationIncrement()`,
`view.notifyParamAutomationOccurred()`, and the `mostRecentI` *read* — is engine RT-cursor / UI state that
*correctly stays engine-side* in the accessor model (it is not node data).

Forwarding the whole op to `param_edit::setValueForRegion` would be **strictly worse**: it duplicates the
`currentValue`/`mostRecentI` decision inside the kernel and forces the wrapper to *re-derive* the reset-vs-notify
RT sub-branch (which neither the golden, the projection oracle, nor the record-playback harness can see — those
are cursor/UI effects, not nodes). So the `param_edit::setValueForRegion` kernel stays as an oracle-tested
building block (and the `recordLiveValue` composition piece), but the engine op is left as-is: its node mutation
already crosses the DOM boundary. No M2-D.2 code change. This also needs no new hardware/harness gate — the node
mutation is covered by the existing homogenizeRegion oracle + golden.

### M2-D.3 — Route the recording write through `recordLiveValue`; keep the latch coherent (HARDWARE-GATED, delicate)

`setCurrentValueInResponseToUserInput`'s recording branch (auto_param.cpp:141+, the `livePos`/`ticksToClear`
block) and `processCurrentPos`'s `recordOverNodeJustReached` path (auto_param.cpp:600+) are the one place a live
node write and the per-tick cursor genuinely interleave (spec §7).
- [x] **DONE `62d583ce8`.** In `setCurrentValueInResponseToUserInput`, routed the node mutation through
  `param_edit::recordLiveValue` (`interpolateLeft` = `reversed || shouldInterpolateRegionStart`,
  `recordingLinearly` with the Song-timeline guard). Kept the `renewedOverridingAtTime` latch,
  `resetInterpolationIncrement`, `deleteRedundantNodeInLinearRun`, Action snapshot, and `notifyParamModified…`
  engine-side; the "clip too short" early-out stays a direct `deleteAutomation`. The `leftI==-1` early-outs are
  dropped (the DOM container's inserts are infallible). Verified against the `param_record_diff` baseline
  (`OK=3`, x86 AND ARM) + projection + golden + firmware.
- [ ] Leave `processCurrentPos`/`setupInterpolation` node *reads* as-is (they already read `doc_->nodes` via
  `nodes()`); do NOT try to kernel-ise the cursor state machine. The only change here, if any, is that the
  `recordOverNodeJustReached` node write can reuse the same kernel helper as above for consistency — optional.
- [ ] **Verify:** golden `OK=15`; `song_edit_roundtrip` PERSISTED; AND **`param_record_diff` `OK=N` unchanged**
  (the differential harness above — the recording node write must reproduce its engine-native baseline
  bit-for-bit; this is now the correctness gate in place of on-device). An on-device ear-check (drift-back feel,
  latching, MPE/MIDI-CC/patch-cable knob-recording) remains a nice-to-have confidence pass, not a blocker —
  bit-identical node+cursor state makes the feel identical by construction.

**M2-E started** (endgame plan `~/.claude/plans/merry-churning-trinket.md`, approved). **E1 COMPLETE**
(`599582272` + `0af32f336`): `ModelStackWithAutoParam::doc()` (in auto_param.cpp) returns
`autoParam->docForThreading()` (== `doc_`, since a param's own ops have `modelStack->autoParam == this`); every
MSWAP edit op now reads its DOM node store via `modelStack->doc()` (kernel-forwards inline; the 12 nodes()-heavy
ops via a scoped `auto& nodesRef = modelStack->doc()->nodes`). Behaviour-identical (`doc()==doc_` through E1/E2);
this is the access path E3 re-roots at the owner. Gotcha handled: the projection-test oracle cut corners (null /
no `autoParam`) so its ModelStack builds now set `autoParam`, incl. internal-getValueAtPos callers
(trimToLength/copy) + nudge (null ModelStack). x86≡ARM projection OK, golden OK=15, `param_record_diff` OK=3, fw
builds. NEXT = E2 (thread the non-MSWAP ops + private helpers + serialization + `isAutomated(doc)`).

**M2-D COMPLETE** (D.1 getValueAtPos forwarded `df545adbc`; D.2 resolved by analysis — no forward needed;
D.3 recording write forwarded `62d583ce8`). Every node read and the recording write now run through DOM kernels;
only the RT interpolation cursor + override latch remain as engine state (correctly — they are not node data).
The record-playback harness retired the on-device correctness gate for the seam.

### Retiring the hardware gate with a differential harness (feasibility audit, 2026-07-06)

**Question:** can the on-device "record/draw automation live" gate for M2-D.2/.3 (+ M2-E) be replaced by a
host-sim harness? **Verdict: yes for correctness — no hard blocker found.** The refactor's contract is
behaviour-preservation, so the oracle is free: capture a baseline from HEAD *before* the forward, then assert
the forwarded code reproduces it bit-for-bit — the same methodology as `param_projection_test`, lifted to
integration scope (real `Song`/`Clip`/`ParamManager` + scripted recording ticks).

**Design — lean differential "record-playback" harness** (extends `song_edit_roundtrip`'s native song load):
build a `ModelStackWithAutoParam` for a loaded clip's param; drive a scripted interleaving of
`setCurrentValueInResponseToUserInput` (knob turns) + `processCurrentPos` (ticks) + `swapState` (undo) +
save/reload; capture the node vector + `currentValue` + latch; diff vs a committed baseline. **"Time" is
controlled by writing `AudioEngine::audioSampleTimer` — no real audio scheduler is driven, so it's fully
deterministic.**

**Per-dependency drivability audit** (external reads of the recording seam + `processCurrentPos` +
`setValueForRegion`):
- *Settable globals:* `playbackHandler.recording` / `.ticksLeftInCountIn` / clock state (`playbackState |=
  PLAYBACK_CLOCK_EITHER_ACTIVE`) / `.getTimePerInternalTick`, `AudioEngine::audioSampleTimer`,
  `Buttons::isShiftButtonPressed` (default false), `getRootUI` (force the non-`automationView` branch) — ✅.
- *Real loaded objects:* `getTimelineCounter` (Clip), `paramCollection`/`paramId`/`paramManager`,
  `getLoopLength`, `song` — ✅ already provided by the `song_edit_roundtrip` load path.
- *Undo/display/view:* `actionLogger.getNewAction`/`recordUnautomatedParamChange`, `display->displayPopup`,
  `view.notifyParamAutomationOccurred` — ✅ real or safe-stub in sim.
- *Play-position reads (the only real work):* `getLivePos`, `getLastProcessedPos`,
  `getPosAtWhichPlaybackWillCut` — script "time" by setting the clip/playbackHandler position fields per step.
  Bounded investigation (find the backing fields), NOT a blocker.

**What a harness still can't cover (the honest residual, all low-risk for M2):** real-time CPU budget on the
Cortex (M2 is a per-*tick* change with ~unchanged code size → negligible), encoder/display I/O plumbing
(untouched — the op takes an already-decoded value), and subjective "feel" (bit-identical node+cursor state ⇒
identical feel by construction). So the harness retires the **correctness** gate; an ear/finger check becomes
optional confidence, not a blocker.

**Status: BUILT (`9c0dc6409`).** `tests/golden/param_record_diff.cpp` + `param_record_run.py` +
`param_record_baselines/` + a CTest entry. It loads a fixture song, builds a real `ModelStackWithAutoParam` for
a song-global param, arms recording (`RecordingMode::ARRANGEMENT`), and replays a scripted knob-turn sequence
(time = `AudioEngine::audioSampleTimer`; `currentlyActioningSwungTickOrResettingPlayPos = true` suppresses
`expectEvent`'s `scheduleSwungTick`), diffing a canonical node dump against a committed baseline (3 fixtures,
`OK=3`). **It immediately caught a real firmware bug** — the M2-C `homogenizeRegion` forward's unconditional
Song→Clip cast for `recordingLinearly` (ASan global-buffer-overflow), fixed in `0ec566d82` — exactly the class
of defect the `param_projection_test` oracle (NoteRow stack) structurally cannot reach.

This is now the **verification line for M2-D.3** in place of "on-device": the baseline is engine-native
recording, so forwarding the recording write to `recordLiveValue` must reproduce it. Documented follow-ups
(broaden coverage): undo (`swapState`), save/reload persistence, and a finite-loop `InstrumentClip` target.

---

## Phase M2-E — Remove every engine→DOM pointer (`doc_` gone)

The real "drop `nodes_`/`doc_`". Thread the `DocAutoParam` from the callers instead of storing it, and complete
universal binding so there's no unbound fallback. End: `AutoParam = { interpolation cursor }`, zero DOM pointers.

- [ ] **Complete universal binding.** Bind at every param-creation site so every `AutoParam` always has a store
  slot — including runtime-created ones (MIDI CC learned, patch cable added). Make the variable stores (MIDI-CC,
  patch-cable) **growable**: on add, grow the owner's `DocParamSet.params`. This is now SAFE precisely because no
  `doc_`/borrow pointer is stored — a store relocation dangles nothing (callers resolve index→slot per call).
  Removes the MIDI-CC/patch-cable "new entries stay unbound" caveat from Movement 1.
- [ ] **Thread the handle.** Change the op signatures to take `DocAutoParam&` (or `(DocParamSet& store, int idx)`)
  and update the callers — `ParamSet`/`MIDIParamCollection`/`PatchCableSet` iterate their store alongside their
  params and pass `store.params[i]`. The ~6 consumer files (`param_set.cpp`, `patch_cable*`,
  `consequence_param_change.cpp`, `instrument_clip*`, `action.cpp`, automation views) pass the slot through.
- [ ] **Remove `doc_`/`ownDoc_`/`nodes()`** from `AutoParam`. It now holds only `currentValue` + the interpolation
  increment. The self-pointer copy/move machinery is deleted (nothing to re-anchor).
- [ ] **Verify:** golden `OK=15` x86≡ARM; full harness; firmware builds; on-device smoke test.
- [ ] **Deliverable:** no engine object holds a pointer into DOM node data; the relocation-dangle hazard class is
  gone. The DOM is a clean boundary candidate — a subsequent (separate) effort can put the C-ABI / Rust ownership
  there (the op signatures already take a resolvable handle, not a raw container).

---

## Risks / reality check

- **Full payoff is deferred.** M2 makes the engine depend on the *API*; the actual C-ABI / Rust ownership is a
  further effort. M2's own value is the decoupling (the API surface + no raw shared container).
- **RT hot path.** `currentValue` (the per-sample DSP read) stays an `AutoParam` member — unchanged, fast.
  M2-D's per-tick DOM reads are per-tick, not per-sample; verify no per-sample *vector* traversal creeps in.
- **Recording seam (§7).** `setCurrentValueInResponseToUserInput` × `processCurrentPos` latch is the one place
  read and write genuinely interleave; it earns the most test + the hardware check.
- **Universal binding scope (M2-E).** Fully dropping `doc_` requires every param to always have a store slot,
  incl. runtime-created ones (growable MIDI-CC / patch-cable stores). This is the largest sub-task, but it is what
  removes the relocation-dangle hazard class outright (no stored pointer ⇒ growth/relocation dangles nothing).
- **Stop-anywhere.** M2-A alone (full DOM edit-op suite) is independently valuable. M2-C leaves a clean
  API-forwarded engine (with `doc_` scaffolding) even if M2-D/E wait on hardware — a legitimate resting point;
  M2-E's full pointer removal is the ambitious finish, not a correctness requirement for M2-C.
