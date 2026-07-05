# Param automation Movement 2 — accessor op-flip Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or subagent-driven-development).
> Steps use checkbox (`- [ ]`) syntax. Design: `docs/superpowers/specs/2026-07-03-param-automation-accessor-projection-design.md`.

**Goal:** Complete the accessor/projection target — move the engine's ~30 node operations off the raw
`ParamNodeVector` and onto the DOM's `param_edit` kernels + a `DocAutoParam` handle, so `AutoParam` depends on an
**API**, not a concrete container. This is the step that makes the DOM boundary (C-ABI / Rust / serde) real.

**Architecture:** After Movement 1 every clip/drum `AutoParam` borrows DOM-owned nodes via `nodes_`
(`ParamNodeVector*`). Movement 2 re-types that handle to `doc_` (`DocAutoParam*`), forwards `AutoParam`'s edit ops
to `deluge::project::param_edit` kernels, converts the per-tick RT read path to DOM reads, and converges the RT
`currentValue` with `DocAutoParam::current_value`. End state: `AutoParam` = `{interpolation cursor + doc_}`.

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

## Phase M2-B — Re-type the handle: `nodes_` (`ParamNodeVector*`) → `doc_` (`DocAutoParam*`)

Replace `ownNodes_`/`nodes_` with `ownDoc_` (`DocAutoParam`) / `doc_` (`DocAutoParam*`, default `&ownDoc_`).
`nodes()` becomes `doc_->nodes`. Borrow (`borrowNodeStorage`) points `doc_` at the DOM store's `DocAutoParam`
(not just its `.nodes`). The self-pointer copy/move machinery (Phase B) carries over verbatim, retyped.

**Critical sub-task — converge `currentValue`.** The kernels read/write `DocAutoParam::current_value`, but today
the engine's `AutoParam::currentValue` is a *separate* scalar (Movement 1 borrowed only `.nodes`). Make
`doc_->current_value` the single home: `getCurrentValue()` returns `doc_->current_value`; the RT cursor writes go
there. This changes the per-sample hot read from a member load to a pointer-deref load — measure it's negligible
(it is: scalar deref, not per-sample vector access). Update every `currentValue` write site (the interpolation
glide `applyValueIncrement`, `setCurrentValueBasicForSetup`, etc.).

- [ ] Retype members + `nodes()`; adapt copy/move/`borrowNodeStorage`/`swapState`.
- [ ] Point `borrowNodeStorage` at the DOM `DocAutoParam` (callers in `bindDomStore`/`bindMainParamSetDomStores`
  pass `&store.params[i]` instead of `&store.params[i].nodes`).
- [ ] Converge `currentValue` → `doc_->current_value` (accessor + all write sites).
- [ ] **Verify:** golden `OK=15` x86≡ARM (behaviour-preserving retype), `param_projection_test` OK,
  `song_edit_roundtrip` PERSISTED, firmware builds.

**Risk:** the `currentValue` convergence is the subtle part (per-sample read, save reads `current_value`). Gate on
golden + the edit round-trips; the RT *feel* is a hardware check (M2-D).

---

## Phase M2-C — Forward `AutoParam` edit ops → `param_edit` kernels (op-by-op)

With `doc_` in hand and all kernels ported, replace each `AutoParam` edit-op body with a forward:
`deluge::project::param_edit::<op>(*doc_, …timeline ctx…)`, keeping the engine method as the thin wrapper that
supplies timeline context + Action/notify (the "wiring seam"). One op per commit; each is verified by the existing
`param_projection_test` oracle (the forward must equal the pre-forward engine op) + golden.

- [ ] Forward the already-ported ops first (setNodeAtPos, homogenizeRegion, insertTime, deleteTime,
  shiftHorizontally, shiftValues, generateRepeats, moveRegionHorizontally, deleteNodesWithinRegion,
  getDistanceToNextNode) — mechanical, each golden+oracle-verified.
- [ ] Forward the M2-A-ported ops (deleteAutomation, trimToLength, stealNodes/insertStolenNodes, appendParam,
  flattenReverse, nudge…, transpose…, makeInterpolationGoodAgain, copy/paste, setValueForRegion) — each verified.
- [ ] **Verify per op:** golden `OK=15`; `param_projection_test` OK; `clip_clone_roundtrip` 38/0 (clone drives
  flattenReverse/append); firmware builds. **Deliverable:** every non-RT node op runs through the DOM API.

---

## Phase M2-D — Convert the RT read path + recording (HARDWARE-GATED)

The per-tick projection and the recording write are engine-side (per-voice cursor) but currently read/mutate
`nodes()` directly. Convert them to DOM reads/kernels. **This is the risky phase — do NOT start before the
Movement-1 hardware gate is green.**

- [ ] `processCurrentPos` — read the node just reached and the next node via DOM read accessors
  (`findNodeAtOrAfter`/index reads on `*doc_`) instead of `nodes().firstAtOrAfter(...)`; keep the cursor
  (`currentValue`/increment) update engine-side. `setupInterpolation` likewise.
- [ ] `setPlayPos` / `grabValueFromPos` / `getValuePossiblyAtPos` — read via `param_edit::getValueAtPos` (already
  ported).
- [ ] `setCurrentValueInResponseToUserInput` (recording) — route the node write through the M2-A kernel; keep the
  latch/override state coherent with `processCurrentPos` across a tick (spec §7). **The delicate seam.**
- [ ] **Verify:** golden `OK=15`; `song_edit_roundtrip` PERSISTED; AND **on-device**: record + draw automation
  live, playback, undo/redo, MPE + MIDI-CC + patch-cable edits — confirm it lands and feels right. Sim can't see
  this; the device is the oracle.

---

## Phase M2-E — Drop the raw `ParamNodeVector` coupling

With reads + edits through `doc_`/kernels, remove the remaining direct `nodes()` mutation sites in the ~6 engine
consumer files (`param_set.cpp`, `patch_cable*`, `consequence_param_change.cpp`, `instrument_clip*`, `action.cpp`,
the automation views), making `nodes()` a private read-only detail or removing it. `AutoParam` is now
`{interpolation cursor, doc_}` + `ownDoc_` fallback for the (few, runtime-created) unbound params.

- [ ] Sweep the ~6 consumer files; route their node mutations through the DOM API.
- [ ] Make `AutoParam::nodes()` private / read-only (or delete once no external mutator remains).
- [ ] **Verify:** golden `OK=15` x86≡ARM; full harness; firmware builds; on-device smoke test.
- [ ] **Deliverable:** the engine depends on the `param_edit` API, not `ParamNodeVector`. The DOM is now a clean
  boundary candidate — a subsequent (separate) effort can put the C-ABI / Rust ownership there.

---

## Risks / reality check

- **Full payoff is deferred.** M2 makes the engine depend on the *API*; the actual C-ABI / Rust ownership is a
  further effort. M2's own value is the decoupling (the API surface + no raw shared container).
- **RT hot path.** M2-B (`currentValue` deref) + M2-D (per-tick DOM reads) touch the audio path. Per-tick /
  per-sample scalar reads are fine; verify no per-sample *vector* traversal creeps in.
- **Recording seam (§7).** `setCurrentValueInResponseToUserInput` × `processCurrentPos` latch is the one place
  read and write genuinely interleave; it earns the most test + the hardware check.
- **Unbound params remain.** Runtime-created params (CC learned / cable added after load) stay unbound (`ownDoc_`)
  — M2 keeps the `ownDoc_` fallback; it does not require binding-everything.
- **Stop-anywhere.** M2-A alone (full DOM edit-op suite) is independently valuable; M2-C leaves a clean
  API-forwarded engine even if M2-D/E wait on hardware.
