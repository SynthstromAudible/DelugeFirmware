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
