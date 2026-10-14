# M4a commit-flip — event-patch as the sole writer of the scalar param store

**Status:** design (for review)
**Branch:** `deluge-project-param`
**Date:** 2026-07-12
**Follows:** `docs/superpowers/specs/2026-07-12-m4a-snapshot-spine-design.md` (M4a), `docs/dev/m4a_snapshot_measurement.md`

## Where this sits

M4a built `ParamSnapshot::applyPatch` (the model-first param patch) and drove it from
`SetParamValue::emitEvent` behind an off-by-default A/B toggle, keeping the in-line engine poke as the shipped
default so goldens stay bit-identical. The measurement showed the incremental patch is ~6–9× cheaper than the
full-projection rebuild. This spec is the **commit-flip**: make the event-patch the sole writer of the persistent
param store for the user-edit path, and delete the redundant in-line store write — **Option A (funnel, keep the
engine's RT tail)**, chosen 2026-07-12.

## The seam (why this is not a mechanical deletion)

The in-line poke `AutoParam::setCurrentValueInResponseToUserInput` (scalar) does FOUR things
(`auto_param.cpp:352-359` + `:180`):

1. `resetInterpolationIncrement()` — the interpolation cursor;
2. `currentValue = value` — the **engine RT cursor the patcher reads every render**;
3. `doc()->current_value = value` — the **persistent model store** (this is what `applyPatch` reproduces);
4. `notifyParamModifiedInSomeWay(...)` — patch-source / summary flags;

plus, when playing (not recording), a `renewedOverridingAtTime` latch (`:328-334`).

`applyPatch` reproduces only **(3)**. Nodes are already SHARED storage (engine and model both use
`DocAutoParam::nodes`), so only `currentValue` (2) is a separate copy. Deleting the in-line poke wholesale would
drop (1)(2)(4) and the latch → the render reads a stale cursor with no notify. Those are the engine's DERIVED
executor state, which the north-star says the engine keeps. So the flip must **externalize the persistent store
write to `applyPatch`, while the engine keeps deriving its RT state (1,2,4,latch) — now sourced from the model
store instead of the in-line argument.**

## Scope decision: scalar now, region deferred

- **Scalar user-edit path (IN):** `SetParamValue::apply` on the scalar branch (`length == 0` or no live timeline
  counter). Clean: the persistent write is a single `current_value`; the Edit already owns its undo pre-log
  (`recordUnautomatedParamChange`), so no undo change is needed.
- **Region user-edit path (OUT — deferred):** `setValueForRegion` opens its OWN undo Action
  (`recordParamChangeIfNotAlreadySnapshotted`, `auto_param.cpp:1112`) **interleaved** with the node write, and the
  region Edit relies on that internal Action (apply() pre-logs only on the scalar path). Externalizing the node
  write to `applyPatch` would require moving that undo snapshot ahead of the patch — undo-path work M4a's constraint
  ("Consequence stays the undo currency; M4a does not touch undo") excludes. Region keeps the in-line poke until a
  dedicated slice (or M6 Consequence convergence) reworks the region undo ordering.
- **Recording / drum-MPE (OUT):** `setCurrentValueInResponseToUserInput`'s recording branches, and its lone
  remaining direct caller (drum-MPE live expression), are NOT `SetParamValue` edits and are untouched — they keep
  the interior log (M4d retires it). This flip changes ONLY `SetParamValue::apply`'s scalar branch.

## Design

### 1. A narrow engine projection entry point
Add `AutoParam::projectScalarFromStore(ModelStackWithAutoParam const* modelStack, int32_t oldValue, bool isPlaying)`
that reproduces the scalar poke's RT tail from the ALREADY-written store `current_value` (no `value` argument — the
store is authoritative):
- `resetInterpolationIncrement();`
- the playing-non-recording `renewedOverridingAtTime` latch (the `:328-334` block), gated on `isPlaying`;
- `currentValue = modelStack->doc()->current_value;`
- `notifyParamModifiedInSomeWay(modelStack, oldValue, /*automationChanged=*/false, automatedBefore, automatedNow);`

This is a faithful extract of the non-recording scalar tail of `setCurrentValueInResponseToUserInput` — the same
sequence, sourcing the value from the store. `setCurrentValueInResponseToUserInput` itself is UNCHANGED (drum-MPE
and any other direct caller keep it verbatim).

### 2. `SetParamValue::apply` scalar branch flips to model-first
On the scalar path, replace the in-line `setValuePossiblyForRegion` call with:
1. capture `oldValue = engineStack_->autoParam->getCurrentValue()` and `isPlaying` (as apply() already computes);
2. the existing undo pre-log (unchanged);
3. `ParamSnapshot::applyPatch(ctx.model, *address_, newValue_, 0, 0, std::nullopt)` — the AUTHORITATIVE store write.
   When the address is nullopt (a never-saved engine-only lane not yet id-addressed), fall back to writing the
   engine store directly via the existing path so no lane is dropped (see Risks);
4. `engineStack_->autoParam->projectScalarFromStore(engineStack_, oldValue, isPlaying)` — engine derives its RT
   state from the store.

The REGION branch (`length_ != 0 && timelineCounterIsSet()`) still calls `setValuePossiblyForRegion` verbatim.

### 3. The A/B toggle and `emitEvent` retire for scalar
With the scalar store write now inside `apply()` unconditionally (model-first), the M4a A/B toggle and the
`emitEvent`→`applyPatch` scalar path are redundant for scalar and are simplified: `emitEvent` keeps the
`ALPHA_OR_BETA` address-resolves observation and the REGION event-patch (still behind the toggle, since region is
not yet flipped). `m4EventPatchToggle()` remains for the region A/B until region flips. (Alternatively, keep
`emitEvent` as-is and let `apply()` own the scalar flip; the equivalence spec already proves both reach the same
state. Decide at plan time — leaning: `apply()` owns the scalar write, `emitEvent` no longer patches scalar.)

## Behaviour preservation + gates

The change is **observationally identical**: the store `current_value`, the engine `currentValue` cursor, the
interpolation reset, the notify, and the latch all end in the same state as before — only the WRITE ORDER changes
(store-first, then cursor) and the store write moves from inside the engine mutation to `applyPatch`. Both write the
same value.

- **Standing golden gate, bit-identical:** song 16 / json 38 / edit 40 / param 3 / audio_clip 1, x86≡ARM; firmware
  links. `param_record` and `edit_undo_redo` are the load-bearing ones (they exercise playing + undo of scalar
  param edits) — a stale cursor or dropped notify/latch or a broken undo would surface there.
- **`edit_test`:** the existing pilot + equivalence specs already assert the scalar post-state
  (`{cursor, current_value, nodes}`); extend `testSetParamValuePilot` to also assert the cursor + store after the
  flipped `apply()` (they must still match). Add a case that the store address resolves so the model-first path
  (not the fallback) is exercised.

## Risks / open questions

- **Unaddressable scalar lanes (nullopt address).** Some engine-only lanes (never-saved owners not yet
  id-addressed) have no `ParamAddress`; `applyPatch` no-ops on them. `apply()` MUST fall back to the in-line store
  write for those so no edit is dropped. Simplest: if `address_` is nullopt, keep calling `setValuePossiblyForRegion`
  (the current path) for that edit; else use the model-first path. This keeps every lane covered and is trivially
  behaviour-preserving for the nullopt case.
- **The `notify` args must match exactly.** `automationChanged` is false on the scalar non-recording path;
  `automatedBefore`/`automatedNow` are `isAutomated(doc)` before/after. `projectScalarFromStore` must compute them
  the same way (before = captured pre-write, after = post-write) so the notify is bit-identical.
- **The latch (`renewedOverridingAtTime`).** Only set on the playing-non-recording scalar path with non-empty
  nodes. Reproduce that exact guard, or the "edit while playing an automated param" behaviour drifts (caught by
  `param_record`).
- **Region stays split.** Until region flips, the writer of the store is `applyPatch` for scalar but
  `setValueForRegion` for region — both write the same store, so this is consistent; just noted so the asymmetry is
  intentional and documented, not an oversight.

## Exit criteria

- `SetParamValue::apply`'s scalar branch writes the persistent store via `ParamSnapshot::applyPatch` (model-first),
  with a nullopt-address fallback; the engine derives its RT cursor/interpolation/notify/latch from the store via
  `AutoParam::projectScalarFromStore`; the in-line scalar store write is gone from the user-edit path.
- `setCurrentValueInResponseToUserInput` is unchanged for its direct (drum-MPE) callers.
- Goldens bit-identical (x86≡ARM); firmware links; `edit_test` OK incl. the extended scalar-post-state asserts.
- Region + recording remain on the in-line poke (documented deferral); the on-device audio-callback confirmation of
  the RT cost remains the named follow-up before the region flip.

## References
- Seam: `src/deluge/modulation/automation/auto_param.cpp:172-360` (scalar), `:1089-1178` (region)
- Edit: `src/deluge/model/edits/set_param_value.{h,cpp}`
- Patch: `src/deluge/model/snapshot/param_snapshot.{h,cpp}`
- Measurement / M4a: `docs/dev/m4a_snapshot_measurement.md`
