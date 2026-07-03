# Param automation: the accessor/projection target (DOM owns the curve, engine owns the cursor)

**Status:** design, approved in principle (branch `deluge-project-param`, 2026-07-03).

**Relationship to prior specs.** This supersedes the *end-target* of
`2026-07-02-dom-authoritative-automation-single-store-design.md`. That spec's "single store, engine borrows via a
raw pointer" model is retained here **only as a transitional bridge**, not as the destination. It builds on
`2026-07-02-dom-live-authoritative-automation-design.md` and `docs/dev/param_automation_dom.md` (the DOM node
representation `DocAutoParam` + the `deluge-project/param_edit.*` edit kernels), which this design promotes from a
serialization/DTO form to the live authority the engine reads and writes through.

---

## 1. What "borrowing" is, and why it is a bridge and not a destination

Today an `AutoParam` (an engine object on the audio path) owns its automation curve inline
(`ownNodes_`), reached through a redirectable pointer:

```cpp
ParamNodeVector  ownNodes_{};              // auto_param.h:166
ParamNodeVector* nodes_ = &ownNodes_;      // auto_param.h:170  (redirectable)
ParamNodeVector& nodes() { return *nodes_; }
```

`borrowNodeStorage(domStore)` (auto_param.h:147) moves the nodes into a DOM-owned `ParamNodeVector` and re-points
`nodes_` at it. The engine's ~30 node operations then run **directly on DOM memory through a raw pointer**, using
the engine's own op implementations (278 `nodes()` sites in `auto_param.cpp` alone).

Borrowing achieves single-copy DOM ownership cheaply — it reuses every engine op unchanged. But it does so by
**tightening** the coupling the program is trying to dissolve:

- **Raw pointer ⇒ manual lifetime.** The DOM store must outlive every borrower and never relocate. This is not an
  incidental bug source; it *is* the R2/R3/NoteRow relocation-dangle class, structurally.
- **Shared mutable memory across the seam we want to harden.** The north star is a C-ABI (eventually Rust-owned,
  fault-isolatable, serde-serializable) boundary around the DOM. You cannot hand another owner a buffer that a C++
  op is calling `.insert()` on. Direct cross-boundary mutation is exactly what a real boundary forbids.
- **The DOM's representation is welded to the firmware container.** The store must *be* a `ParamNodeVector` so the
  engine ops compile against it — so the DOM can never own plain POD/serde data while borrowing is the terminal
  shape.

So borrowing delivers "one copy" as *one-copy-shared-mutable-same-container* — the hardest possible thing to later
split across a boundary. It advances "who owns it" and regresses "can this be a boundary."

---

## 2. Grounding facts (measured, not assumed)

These determine what the end-target must and must not do. Verified by source trace (branch `deluge-project-param`):

- **The DSP never reads the curve — it reads a scalar.** The synth consumes `AutoParam::currentValue`
  (auto_param.h:127). The node vector is traversed only:
  - `processCurrentPos` — **once per sequencer tick** (auto_param.cpp:441): finds the node just reached
    (`nodes().firstAtOrAfter`), sets `currentValue`, computes an interpolation increment (`setupInterpolation`).
  - `tickSamples` — once per render window per param (auto_param.cpp:975): glides `currentValue` by the increment;
    **does not touch nodes**.
  - `getValueAtPos` / `setPlayPos` / `grabValueFromPos` — random-access reads (auto_param.cpp:1501/1568/1558).
  - So curve access is **per-tick / per-user-action, never per-sample.**
- **Cooperative, non-preemptive, single-threaded, no locks** (task_scheduler.cpp:239 — `runTask` just calls the
  task function on the main stack; `yield()` runs a nested loop on the same stack). Audio is the top-priority
  repeating task (deluge.cpp:519); `processCurrentPos` runs *inside* it (via `tickSongFinalizeWindows` →
  `actionSwungTick`, audio_engine.cpp:773). Node **writes** run on UI/MIDI tasks (view.cpp:821,
  mod_controllable_audio.cpp:855). Audio and edit code **never run concurrently** — they interleave only at
  explicit yield points, and edit ops are atomic with respect to those yields.
  - Sharp edge: an edit path can itself call `AudioEngine::routineWithClusterLoading()` (a yield point) and
    re-enter rendering. So a "safe point" is defined against **re-entrancy**, not against thread preemption.
- **Undo already treats the whole vector as the unit of state.** `swapState` does `nodes().swap(state->nodes)`
  (auto_param.cpp:2205); `ConsequenceParamChange` steals or deep-copies the whole vector
  (consequence_param_change.cpp:35/41). This matches "DOM owns the curve" semantics cleanly.
- **Most curves are empty.** ~10³–10⁴ `AutoParam`s per song (fixed arrays: `kMaxNumUnpatchedParams`,
  `kNumParams` — param.h:253-255), the large majority un-automated (`isAutomated()` false ⇒ `currentValue` only).
  Automated curves size ≈ clip length in ticks. Each `ParamNode` ≈ 8–12 bytes.

---

## 3. The snapshot / directional-sync model is rejected

An earlier proposal was: DOM owns the authoritative curve; the engine holds a *separate* RT snapshot buffer;
edits apply to the DOM and resync into the engine buffer at a safe boundary. Its entire justification is
**RT-isolation** — give the audio thread a stable buffer immune to a concurrent editor.

Grounding fact 2 removes that justification. There is no other thread and no preemption; cooperative scheduling
already serializes audio against edits, and edit ops don't yield mid-mutation. A second synced buffer would be
pure cost — two copies plus a resync protocol — buying nothing the scheduler doesn't already provide. **Rejected.**

---

## 4. The target: accessor / projection

Split state by what it genuinely *is*:

- **DOM owns the node curve** (plain, relocatable data: `DocAutoParam`) **and the operations on it**
  (`deluge-project/param_edit.*`).
- **The engine owns `currentValue` + the interpolation cursor + play position** — genuine per-voice RT state,
  correctly engine-side. It holds **no node vector** — only a handle to its `DocAutoParam`.
- **`processCurrentPos` *is* the projection function:** once per tick it reads the DOM curve at the current
  position (through the DOM read API) and updates the engine's scalar cursor. Recording/editing route through the
  DOM edit API.

The engine depends on an **API**, not on the concrete `ParamNodeVector` type. That API is the fault-isolation /
Rust / serde boundary. This is viable *here specifically* because curve access is per-tick / per-user-action,
never per-sample — so hiding the curve behind an API (even an eventual C-ABI) **costs nothing measurable.** That
one property is what makes the boundary free.

### 4.1 The API surface the engine needs from the DOM

**Read API (for the projection + random-access reads).** A small set of node-read primitives sufficient for
`processCurrentPos`, `setupInterpolation`, `setPlayPos`, `grabValueFromPos`:
- `getValueAtPos(doc, pos, loopLength, reversed)` — **already ported** (param_edit.h:41).
- `getDistanceToNextNode(doc, pos, loopLength, reversed)` — **already ported** (param_edit.h:47).
- `firstAtOrAfter(pos)` / node-at-index / node count — thin reads over the sorted `DocAutoParam::nodes`
  (`OrderedPosVector`), to be exposed as DOM read accessors (today reached via `nodes()`).

**Edit API (engine edit ops forward to these).** The `param_edit` kernels are faithful, golden-proven ports;
engine ops become thin forwarders:
- **Already ported:** `setNodeAtPos`, `homogenizeRegion`, `insertTime`, `deleteTime`, `shiftHorizontally`,
  `shiftValues`, `generateRepeats`, `moveRegionHorizontally`, `deleteNodesWithinRegion` (param_edit.h).
- **To port (remaining engine ops that still mutate `nodes()` directly):** `setValueForRegion` /
  `setCurrentValueInResponseToUserInput` (the recording/live-write path), `deleteAutomation`, `trimToLength`,
  `stealNodes` / `insertStolenNodes`, `appendParam`, `flattenReverse`, `nudgeNonInterpolatingNodesAtPos`,
  `transposeCCValuesToChannelPressureValues`, `makeInterpolationGoodAgain`, `copy` / `paste`. Each is ported as a
  pure kernel taking explicit timeline context (the established `param_edit` pattern), unit-tested against the
  engine original.

**Lifecycle.** Undo swaps whole `DocAutoParam::nodes` (matches today's `swapState`). Copy/move of param state is a
DOM-side value operation. The engine `AutoParam`'s copy/move reduce to copying the scalar cursor + rebinding the
handle.

### 4.2 End state of `AutoParam`

```cpp
// engine-side, RT cursor only — no node storage
int32_t currentValue;
int32_t valueIncrementPerHalfTick; float ...;   // interpolation cursor
DocAutoParam* doc_;                              // handle to DOM-owned curve (the boundary is doc_'s API)
```

`ownNodes_` / `nodes_` disappear. Every `AutoParam` binds to a `DocAutoParam` (empty curves are cheap). Every
`ParamManager` binds to a `DocParamManager`. This is the "DOM owns all param state, engine is a projection"
endgame of the authority program.

---

## 5. Migration: borrowing is the bridge; convert op-by-op

Borrowing's contributions divide cleanly:
- **Durable (keep):** the `nodes()`/handle seam (call sites already indirect through a method), the "DOM owns the
  store" fact, and the `DocAutoParam` / `param_edit` data+op form.
- **Throwaway (unwind):** the raw pointer, the shared concrete container, the direct RT mutation of DOM memory.

The conversion is **per operation**, and each step is independently golden-/`param_projection_test`-verifiable
(a forwarded op must match the engine op — exactly what the projection test already asserts for ported kernels):

1. **Read path:** replace `processCurrentPos` / `setupInterpolation` / `setPlayPos` / `grabValueFromPos` node
   reads (`nodes().firstAtOrAfter(...)`, etc.) with DOM read-API calls. `currentValue`/increment stay engine-side.
2. **Edit path:** replace each engine edit op body with a forward to its `param_edit` kernel (port the remaining
   ones first). `setValueForRegion` / the recording latch path is the most delicate (§7).
3. **Drop node storage:** once all reads/edits forward, remove `ownNodes_`/`nodes_`; `AutoParam` holds `doc_`.

This proceeds **owner-by-owner** (the store-lifetime frontier), reusing the phasing already established:
song-global (done as borrow) → AudioClip → InstrumentClip → kits/drums + `ParamBackup`. Behind the DOM API the
concrete store location is a DOM-side implementation detail, so the earlier "map vs inline store" question
dissolves — it is no longer an architectural fork.

### 5.1 Phase C, reframed

Phase C ("clips") becomes **the first borrow→accessor conversion**, at the AudioClip owner: bind the clip's
`AutoParam`s to per-clip `DocAutoParam`s, route their reads/edits through the DOM API, and shrink them toward
cursor-only — rather than replicating *raw borrow* across kits/drums/backup as a terminal state we would later
unwind. It also carries the golden-blind coverage the borrow model never had: a harness test that drives the
**steal/backup/restore** path (`ParamBackup::backUp → getForExactClip(stealInto) → restore`) in sim, since the
audio-thread edit interleaving is the part the load/save golden cannot see.

---

## 6. Head-to-head

| | Borrow (current bridge) | Snapshot (rejected) | Accessor/projection (target) |
|---|---|---|---|
| Copies of the curve | 1, shared | 2, synced | 1, in DOM |
| Engine↔DOM coupling | raw container ptr + direct mutation | separate buffer + resync protocol | API calls only |
| RT-safety it buys | — (cooperative) | solves a problem we don't have | — (cooperative) |
| Boundary-ready (Rust/serde/fault-iso) | no | partial | **yes** |
| Edit-op work | reuse as-is | reuse + resync | forward to DOM kernels (½ exist) |
| Runtime cost | fine | fine | fine (per-tick, not per-sample) |
| Dangle hazard | yes (raw-ptr lifetime) | reduced | **gone** (engine holds no node ptr) |

---

## 7. Risks / open questions

- **The recording latch path.** `setCurrentValueInResponseToUserInput` (auto_param.cpp:141) interleaves live node
  writes with `processCurrentPos`'s `recordOverNodeJustReached` latch (auto_param.cpp:529-539). Its port must keep
  the latch/override state (engine cursor) coherent with DOM node writes across a tick. This is the single op that
  most couples read and write and earns the most design/test care.
- **Universal binding.** The end state binds *every* `AutoParam` to a `DocAutoParam` and every `ParamManager` to a
  `DocParamManager`. That is a large surface (fixed param arrays; patch cables; MIDI CC; expression). Empty curves
  are cheap, but the binding wiring at construction/clone/steal is real work — sequenced owner-by-owner.
- **Store lifetime during transition.** While both borrowed and un-borrowed params coexist, the DOM store must
  still outlive borrowers (the existing invariant) until an owner is fully converted to handles.
- **MPE / `shiftParamVolumeByDB` / `transposeCC…`** are lower-traffic ops; port last, but they still read `nodes()`
  and must be forwarded before `ownNodes_` can be removed for a given owner.
- **`getValuePossiblyAtPos` / patch-cable / MIDI collections** have their own `processCurrentPos`/`tickSamples`
  variants (patch_cable_set.cpp:719, midi_param_collection.cpp:172) — the read-API conversion must cover them too.

---

## 8. Verification

- **Per-op faithfulness:** `param_projection_test` (dual-arch x86 SIMDe + ARM/qemu) asserts each forwarded op ==
  the engine original. Extend it as ops are ported. This is the primary safety net for a golden-blind refactor.
- **Round-trip:** golden `OK=15` (load/save unchanged by construction), `song_edit_roundtrip` (DOM edit → save →
  reload), `clip_clone_roundtrip` (real clone incl. reverse-flatten).
- **New (Phase C):** a sim harness test driving `ParamBackup` steal/restore, closing the edit-path coverage the
  load/save golden cannot reach.
- **Behaviour-preserving throughout:** every step is a faithful forward; any divergence is a bug caught by the
  projection test or a round-trip.
