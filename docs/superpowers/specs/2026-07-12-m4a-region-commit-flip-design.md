# M4a region commit-flip — event-patch as the writer of the stopped-transport region param store

**Status:** design (for review)
**Branch:** `deluge-project-param`
**Date:** 2026-07-12
**Follows:** `docs/superpowers/specs/2026-07-12-m4a-commit-flip-design.md` (scalar flip, DONE)

## Where this sits

The scalar commit-flip landed: `SetParamValue::apply`'s scalar branch writes the store model-first via
`ParamSnapshot::applyPatch` and derives the engine RT state via `AutoParam::projectScalarFromStore`. This spec does
the same for the REGION branch, which the scalar spec deferred because `setValueForRegion` interleaves its undo
Action with the node write. That undo reorder is the substance here.

## The region seam

`SetParamValue::apply` (region branch) → `setValuePossiblyForRegion` → `AutoParam::setValueForRegion`
(`auto_param.cpp:1089`). What it does, in order:

1. `oldValue = currentValue`; `automatedBefore = isAutomated(doc)`; `effectiveLength = getLoopLength()`.
2. `if (pos >= effectiveLength) return;` (no-op — no Action, no notify).
3. `Action* action = getNewAction(NOTE_EDIT, ALLOWED); if (action) action->recordParamChangeIfNotAlreadySnapshotted(modelStack);`
   — **the undo snapshot: captures the pre-mutation DocAutoParam** (dedupe-safe).
4. node mutation into the model store: whole-region (`length == effectiveLength`) clears/`deleteAutomation`;
   normal calls `homogenizeRegion` (a DOM kernel) — both write `doc->nodes` and, in the value-taking cases,
   `doc->current_value`.
5. the current-value decision: `if (!clockActive) → currentValue = value` (goto). Else a live-pos check
   (`mostRecentI == firstI`) picks between `resetInterpolationIncrement() + currentValue = value` (playhead in the
   edited region) and `view.notifyParamAutomationOccurred(...)` (playhead outside — current_value unchanged).
6. `notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore, automatedNow)`.

`ParamSnapshot::applyPatch`'s region path already reproduces steps 4 + the store half of 5 exactly (it calls the
proven-equivalent DOM kernel `deluge::project::setValueForRegion`, which takes clock/livePos/reversed and writes
`doc->current_value` only in the value-taking cases). It returns the kernel's `automationChanged`.

## Scope decision: stopped-transport region now, clock-active region deferred

**The clock-active region path has engine-side effects the DOM kernel does not surface**: on a clock-active edit the
engine either `resetInterpolationIncrement()` (playhead in region) or `view.notifyParamAutomationOccurred()`
(playhead outside), chosen by `mostRecentI == firstI` — a decision internal to `homogenizeRegion` that `applyPatch`
does not return. Reproducing it would mean extending the kernel's return AND calling a view-tier notify from the
Edit (a layering break). So:

- **IN — stopped-transport region** (`!playbackHandler.isEitherClockActive()`): the RT tail is just
  `currentValue = doc->current_value` + notify (no interpolation reset, no UI notify — the engine's `!clockActive`
  path takes neither). Clean and reproducible.
- **OUT — clock-active region** (playing/recording automation): keeps the in-line `setValuePossiblyForRegion`
  (unchanged). This mirrors the scalar flip carving out recording-active, and covers the RT-sensitive case where
  the interpolation cursor + UI notify matter. A later slice can flip it by teaching the kernel to return the
  sub-branch decision (and routing the UI notify through a non-view seam) — or it rides M6 convergence.
- **OUT — unresolvable/nullopt lane:** in-line poke (as the scalar flip does), so no lane is dropped.

## Design

### 1. `ParamSnapshot::applyPatch` returns the region `automationChanged`
Change the return type from `void` to `std::optional<bool>`: the scalar and no-op-resolve paths return
`std::nullopt`; the region path returns the DOM kernel's `automationChanged` (nullopt when `pos >= effectiveLength`).
Existing scalar callers (apply, bench, tests) ignore the return — a widening, not a behaviour change.

### 2. `AutoParam::projectRegionFromStore` — the stopped-transport region RT tail
```cpp
void AutoParam::projectRegionFromStore(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                       bool automatedBefore, bool automationChanged) {
	// M4a region commit-flip: the store (nodes + current_value) was ALREADY written model-first
	// (ParamSnapshot::applyPatch). Derive the engine RT tail for the STOPPED-TRANSPORT case — the engine's
	// !clockActive setValueForRegion path takes neither the interpolation reset nor the UI notify, so this is
	// exactly cursor-sync + notify.
	currentValue = modelStack->doc()->current_value;
	bool automatedNow = isAutomated(*modelStack->doc());
	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                                          automatedNow);
}
```
`setValueForRegion` itself is UNCHANGED (clock-active region + every other caller keep it verbatim).

### 3. `SetParamValue::apply` region branch flips (stopped-transport, resolvable)
Before the existing in-line poke, add:
```cpp
const bool regionPath = length_ != 0 && engineStack_->timelineCounterIsSet();
if (regionPath && !playbackHandler.isEitherClockActive() && address_.has_value()
    && ctx.model.resolveParam(*address_) != nullptr) {
	const int32_t oldValue = engineStack_->autoParam->getCurrentValue();
	const bool automatedBefore = engineStack_->autoParam->isAutomated(*engineStack_->doc());
	// Undo: snapshot the pre-mutation DocAutoParam into the Action, exactly as setValueForRegion's line 1112 does
	// (dedupe-safe). This is the undo reorder the scalar spec deferred — the snapshot moves ahead of the patch.
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (action != nullptr) {
		action->recordParamChangeIfNotAlreadySnapshotted(engineStack_);
	}
	std::optional<bool> changed = deluge::snapshot::ParamSnapshot::applyPatch(
	    ctx.model, *address_, newValue_, pos_, length_, gatherRegionContext(engineStack_));
	engineStack_->autoParam->projectRegionFromStore(engineStack_, oldValue, automatedBefore, changed.value_or(false));
	return Error::NONE;
}
```
`emitEvent` can then drop its region A/B branch entirely (region is now written by apply() on the stopped-transport
path; the clock-active path keeps the in-line poke and never needed the event patch) — leaving `emitEvent` as the
`ALPHA_OR_BETA` address-resolves observation only, and `m4EventPatchToggle()` retires. (Decide at plan time whether
to keep the toggle+region equivalence spec as a regression net for the DOM-kernel path; leaning: keep the
equivalence assertion by calling `applyPatch` directly in `edit_test`, drop the runtime toggle.)

## Behaviour preservation + gates

Behaviourally identical for the stopped-transport region edit: the undo snapshot (now taken by the Edit before the
patch) captures the same pre-state; `applyPatch` writes the same nodes + current_value the in-line kernel did (proven
equivalent); the tail syncs the cursor + fires the same notify. The whole-region `deleteAutomation`-steal is subsumed
by the pre-snapshot + `applyPatch`'s whole-region node-clear (the engine already snapshots at line 1112 before
`deleteAutomation`, and the steal dedupes against it).

- **Standing golden gate, BIT-IDENTICAL:** song 16 / json 38 / edit 40 / param 3 / audio_clip 1, x86≡ARM; firmware
  links. `edit_undo_redo` (region undo/redo) + `param_record` are load-bearing.
- **`edit_test`:** add a `testRegionFlipViaApply` mirroring `testScalarFlipViaApply` — a resolvable lane, a
  stopped-transport region `apply()`, asserting the model store nodes + current_value match the pinned region vector
  AND the engine cursor synced, then undo restores. Keep the existing `applyPatch`-region equivalence assertion.

## Risks / open questions

- **Undo equivalence of snapshot-vs-steal (whole region).** The engine's whole-region path uses `deleteAutomation`'s
  steal; the flip uses `recordParamChangeIfNotAlreadySnapshotted` (full snapshot) before the clear. Both restore the
  same nodes on revert; `edit_undo_redo` over a region-clear + undo is the check. If it diverges, inspect the
  Consequence node payload.
- **`getNewAction(NOTE_EDIT, ALLOWED)` coalescing.** `setValueForRegion` opens the Action with `ALLOWED`; the flip
  must use the same type + addition so held-pad region drags coalesce into one Action exactly as before.
- **automatedBefore capture timing.** Must read `isAutomated(doc)` BEFORE `applyPatch` mutates nodes (a whole-region
  clear flips automated→unautomated); the notify arg order must match `auto_param.cpp:1176`.
- **Clock-active region stays in-line — intentional and documented,** not an oversight; the store is model-written
  either way (applyPatch on the stopped path, the DOM kernel via setValueForRegion on the clock-active path).

## Exit criteria

- `SetParamValue::apply`'s region branch writes the store via `applyPatch` (model-first) on the stopped-transport,
  resolvable path, with the undo snapshot reordered ahead of the patch and the RT tail via `projectRegionFromStore`;
  clock-active + unresolvable region keeps the in-line poke.
- `applyPatch` returns the region `automationChanged`; `setValueForRegion` unchanged.
- Goldens bit-identical (x86≡ARM); firmware links; `edit_test` OK incl. `testRegionFlipViaApply`.

## References
- Seam: `src/deluge/modulation/automation/auto_param.cpp:1089-1178` (setValueForRegion), `:352-360` (scalar tail)
- DOM kernel: `src/deluge-project/param_edit.h:172` (`setValueForRegion`)
- Undo: `src/deluge/model/action/action.cpp:193` (`recordParamChangeIfNotAlreadySnapshotted`)
- Edit + patch: `src/deluge/model/edits/set_param_value.{h,cpp}`, `src/deluge/model/snapshot/param_snapshot.{h,cpp}`
