# M4a — snapshot cost measurement + event-feed equivalence findings

**Milestone:** M4a (snapshot spine + event-feed proof, param pilot) of the project-model migration.
**Branch:** `deluge-project-param`. **Status:** complete.
**Spec / plan:** `docs/superpowers/specs/2026-07-12-m4a-snapshot-spine-design.md`,
`docs/superpowers/plans/2026-07-12-m4a-snapshot-spine.md`.

M4a carved the greenfield engine-side `deluge::snapshot::ParamSnapshot` read-view + patch protocol, turned the
observational `SetParamValue::emitEvent` hook into a real model-first event→patch loop (behind an off-by-default A/B
toggle), and measured the incremental-patch cost against the full-projection fallback. It is behaviour-preserving:
the in-line engine poke stays the shipped default, so the standing goldens (song 16 / json 38 / edit 40 / param 3 /
audio_clip 1) are bit-identical and firmware links.

## What the in-line poke actually does (Task 1 — the pinned contract)

Before building the event-patch, `tests/projection/edit_test.cpp` (`testInlinePokeCharacterization`) pinned what
the current in-line poke does to BOTH the engine cursor and the model store, driving real `SetParamValue` edits
through `deluge::edits::apply`:

- **Scalar edit (`length == 0`)** → `setCurrentValueInResponseToUserInput`. The poke syncs the engine cursor AND
  the model store's `current_value` (already asserted by the pilot). No nodes are created for an unautomated param.
  **Finding: the model store is ALREADY authoritative for scalar current-values on the write path** — so the
  model-first scalar patch is a plain `current_value = value` write, not new behaviour. The event feed does not
  *make* the scalar model authoritative here; M3 already did. It reproduces it from the event instead of the poke.

- **Region edit (nonzero length + live timeline counter)** → `setValueForRegion`. Homogenizes the region to a flat
  segment: for a `[0,96)` write in a 128-tick loop over an unautomated param it produces nodes
  `[(0, value), (96, pre-region value)]` and syncs `current_value`. The node/value mutation is the DOM kernel
  `deluge::project::setValueForRegion`, whose bit-equivalence to the engine `AutoParam::setValueForRegion` is
  ALREADY an exhaustively-gated invariant (`tests/projection/param_automation_test.cpp`, the full
  configs × cases × clock × livePos × reversed matrix). So the region patch reuses that proven kernel.

**Consequence for the patch API.** The scalar patch needs only `(address, value)`. The region patch genuinely needs
timeline context (`effectiveLength`, interpolation flags, `recordingLinearly`, clock/livePos/reversed) that is not
derivable from `(value, pos, length)` alone — so `ParamSnapshot::applyPatch` carries an explicit `RegionContext`.
In M4a that context is gathered at the emission point from the still-borrowed engine stack (mirroring `apply()`'s own
funnel-phase borrow); the north-star read-side flip will source it from the model/snapshot instead.

## The equivalence proof (Tasks 2–3)

- **Task 2** (`testParamSnapshotEquivalence`): `ParamSnapshot::applyPatch`, called directly on a resolvable
  `ParamAddress`, lands the model store in exactly Task 1's pinned `{current_value, nodes}` for both a scalar and a
  region edit. `doc()` and `resolveParam(addr)` return the same `DocAutoParam` (the realistic on-device situation
  after the S5-b lazy relocation), so the patch writes the store the engine borrows.
- **Task 3** (`testEmitEventDrivesPatch`): with the A/B toggle OFF (the default), `emitEvent` leaves the store
  untouched — the in-line poke is the sole path, which is what keeps goldens bit-identical. With the toggle ON,
  `emitEvent` drives `applyPatch` and reproduces the pinned scalar and region state, sourcing the region context
  from the engine stack. The toggle (`deluge::edits::m4EventPatchToggle()`) defaults false; the firmware/default
  build never patches.

## Cost measurement (Task 4 — host-sim proxy)

`tests/projection/param_snapshot_bench.cpp` applies 200 000 single-param edits two ways against the SAME relocated
`DocParamSet` (43 unpatched lanes), at three automation densities:

- **incremental_patch** — `ParamSnapshot::applyPatch` (resolve one lane by `ParamAddress`, write it). The per-lane
  granularity.
- **full_rebuild** — write the store then `ParamSet::projectFromStoreAfterRead`, which re-derives the WHOLE owner's
  param set (`getNumParams()` lanes). The coarse-granularity fallback the incremental patch replaces.

Representative host run (x86 `build-sim`; wall-clock, so ±20% run-to-run — this is a **proxy**, not the device):

| nodes/lane | incremental_patch | full_rebuild | ratio (full / incr) |
|-----------:|------------------:|-------------:|--------------------:|
| 0          | ~107 ns/edit      | ~680 ns/edit | ~6.3×               |
| 8          | ~108 ns/edit      | ~755 ns/edit | ~7.0×               |
| 64         | ~108 ns/edit      | ~755 ns/edit | ~7.0×               |

Reference: the incremental **region** patch (homogenize `[0,96)`, effLen 128) is ~230 ns/edit — more than the
scalar patch (it rewrites nodes) but still well under the full rebuild.

### Reading

- The incremental scalar patch is **~6–9× cheaper** than the full-set rebuild, and the per-lane cost is **flat**
  across automation density (it resolves + writes one lane; ~107 ns includes the `clip_params` lookup +
  `slotInManager`). The full rebuild grows with density because it re-scans every lane's `isAutomated` /
  `paramHasAutomationNow`. So the incremental patch's advantage **widens** as songs get more automated — the regime
  that matters on the RT path.
- This directly supports the north-star's "incremental patch, full rebuild is the fallback" design: the fallback is
  genuinely the expensive path, and keying the patch by `ParamAddress` lets a single edit avoid re-projecting the
  owner's whole set.
- **Granularity is measured, not chosen here.** The `ParamAddress`-keyed API works whether the projected cursor is
  per-owner or shared, so a coarser grouping remains benchmarkable without a rewrite. M4a emits the number; it does
  not lock per-owner-vs-per-song.

### Proxy caveat

These are host wall-clock numbers, a model of the RT cost — not an audio-callback measurement on the RZ/A. The A/B
kept the in-line poke as the shipped default precisely so this decision stays reversible until an **on-device
audio-callback confirmation** (the explicit next step, out of M4a scope) validates the proxy.

## M4a exit + what the rest of M4 inherits

M4a exit criteria met: the `ParamSnapshot`/patch protocol exists engine-side (model-read/write, never referenced by
the model); `SetParamValue::emitEvent` drives an incremental param-cursor patch proven lane-for-lane equivalent to
the in-line poke (scalar + region); the microbench emits the granularity table; goldens are bit-identical and
firmware links; the in-line poke remains the default (no commit-flip in M4a).

Inherited by the later sub-milestones:

- **The commit-flip — SCALAR + STOPPED-TRANSPORT REGION landed**
  (`docs/superpowers/specs/2026-07-12-m4a-commit-flip-design.md`,
  `docs/superpowers/specs/2026-07-12-m4a-region-commit-flip-design.md`): `SetParamValue::apply` now writes the
  persistent store model-first via `ParamSnapshot::applyPatch` (the sole writer) for both the scalar path and the
  **stopped-transport** region path, with `AutoParam::projectScalarFromStore` / `projectRegionFromStore` deriving the
  engine RT tail from the store. The region flip reordered the undo snapshot
  (`recordParamChangeIfNotAlreadySnapshotted`) ahead of the patch — the undo-path work the scalar spec deferred. The
  M4a A/B toggle is retired (`emitEvent` is now observation-only). Behaviour-preserving (goldens bit-identical,
  x86≡ARM; `edit_undo_redo` + `param_record` are the load-bearing gates). **Still deferred: CLOCK-ACTIVE region** —
  its engine-side `resetInterpolationIncrement()` / `view.notifyParamAutomationOccurred()` sub-branch (chosen by a
  `mostRecentI == firstI` decision internal to `homogenizeRegion`) is not surfaced by `applyPatch`; flipping it needs
  the kernel to return that decision (and a non-view notify seam) — its own slice / M6 convergence. The on-device
  audio-callback confirmation of the RT cost remains the named follow-up. When clock-active region flips, the
  `RegionContext` is the seam that moves from the engine-stack borrow to the model/snapshot.
- **M4b** — hot Sound fields (`record + native cache` dual-writes) onto the snapshot; render reads the snapshot.
- **M4c** — the note VECTOR onto the snapshot (`AddNote`/`RemoveNote` events patch a note snapshot the playback path
  reads) — the biggest remaining read-side independence gap, RT-critical.
- **M4d** — LearnedMIDI onto the snapshot; make the engine-observed recorded-automation reconcile path explicit;
  split drum-MPE live-value vs recorded-automation → retire the `auto_param` interior undo log.
