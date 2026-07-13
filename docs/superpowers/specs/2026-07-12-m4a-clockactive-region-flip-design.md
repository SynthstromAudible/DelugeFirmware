# M4a clock-active region commit-flip — the last in-line param-store writer for user edits

**Status:** design (for review)
**Branch:** `deluge-project-param`
**Date:** 2026-07-12
**Follows:** `docs/superpowers/specs/2026-07-12-m4a-region-commit-flip-design.md` (stopped-transport region, DONE)

## Where this sits

Scalar + stopped-transport region user edits already write the param store model-first via `ParamSnapshot::applyPatch`.
The one carve-out left is the **clock-active region** edit (region automation edited while the transport runs), which
still uses the in-line `setValueForRegion`. This spec flips it, making `applyPatch` the sole writer of the persistent
param store for EVERY reified user param edit.

## Why clock-active was carved out

`AutoParam::setValueForRegion`'s clock-active tail (`auto_param.cpp:1156-1174`), after the node mutation, does one of:

- **playhead inside the edited region** (`mostRecentI == firstI`): `resetInterpolationIncrement()` +
  `currentValue = value` + `doc->current_value = value`;
- **playhead outside** (`mostRecentI != firstI`): `view.notifyParamAutomationOccurred(paramManager)` —
  current_value UNCHANGED.

The DOM kernel `deluge::project::setValueForRegion` already reproduces the STORE half (it computes `mostRecentI == firstI`
internally and writes `doc->current_value` only on the value-taking branch), and returns `automationChanged`. But it
DISCARDS the `mostRecentI == firstI` decision — so the Edit can't know whether to reset the interpolation cursor or
notify the UI. That decision is the missing piece.

**Two enabling facts make this clean:**
1. `projectRegionFromStore` lives in `auto_param.cpp`, which ALREADY calls `view.notifyParamAutomationOccurred` and
   `resetInterpolationIncrement` — so doing the clock-active tail there is NO layering break (it is engine-tier code,
   exactly where the in-line path already does it). The Edit never touches the view.
2. `currentValue = doc->current_value` is correct in ALL branches — value-taking branches wrote the new value into the
   store; the notify-only branch left it unchanged — so the cursor sync is uniform.

## Design

### 1. The DOM kernel surfaces its value-branch decision
`deluge::project::setValueForRegion` returns a small struct instead of a bare `bool`:
```cpp
// param_edit.h
enum class RegionCurrentValueEffect : uint8_t {
	Set,               // current_value written; NO interpolation reset (whole-region, or normal + transport stopped)
	SetAndResetInterp, // current_value written AND the interpolation cursor should reset (clock-active, playhead in region)
	NotifyOnly,        // current_value UNCHANGED; the engine notifies the UI of automation (clock-active, playhead outside)
};
struct RegionWriteResult {
	bool automationChanged;
	RegionCurrentValueEffect currentValueEffect;
};
std::optional<RegionWriteResult> setValueForRegion(...);  // nullopt iff pos >= effectiveLength (unchanged)
```
Kernel mapping (each already computed inside it today — this only NAMES the branch it took):
- `pos >= effectiveLength` → `std::nullopt`.
- whole-region (`length == effectiveLength`) → `{false, Set}`.
- normal + `!clockActive` → `{true, Set}`.
- normal + `clockActive` + `mostRecentI == firstI` → `{true, SetAndResetInterp}`.
- normal + `clockActive` + `mostRecentI != firstI` → `{true, NotifyOnly}`.

`param_automation_test.cpp`'s exhaustive engine≡DOM matrix (`~1969-2107`) gains the assertion that the kernel's
`currentValueEffect` matches the branch the engine-transcription lambda takes (it already computes `mostRecentI == firstI`),
so the new field is proven faithful across the full configs × cases × clock × livePos × reversed sweep. The existing
`*r == automationChanged` asserts become `r->automationChanged`.

### 2. `ParamSnapshot::applyPatch` forwards the result
Return type widens `std::optional<bool>` → `std::optional<RegionWriteResult>` (scalar path + unresolved →
`std::nullopt`; region path → the kernel's result). Scalar callers still ignore it. The equivalence spec's region
assertion changes `*regionChanged` → `regionChanged->automationChanged`.

### 3. `projectRegionFromStore` handles every clock state
```cpp
void AutoParam::projectRegionFromStore(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                       bool automatedBefore, deluge::project::RegionWriteResult result) {
	// The store (nodes + current_value) was ALREADY written model-first. Reproduce setValueForRegion's post-mutation
	// tail from the branch the kernel took. currentValue = doc->current_value is uniform (value-taking branches wrote
	// it; NotifyOnly left it unchanged).
	using deluge::project::RegionCurrentValueEffect;
	if (result.currentValueEffect == RegionCurrentValueEffect::SetAndResetInterp) {
		resetInterpolationIncrement();
	}
	else if (result.currentValueEffect == RegionCurrentValueEffect::NotifyOnly) {
		view.notifyParamAutomationOccurred(modelStack->paramManager);
	}
	currentValue = modelStack->doc()->current_value;
	bool automatedNow = isAutomated(*modelStack->doc());
	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, result.automationChanged,
	                                                          automatedBefore, automatedNow);
}
```
(Replaces the stopped-only 4-arg signature — the stopped path is `Set` → neither extra action, behaviour-identical.)

### 4. `SetParamValue::apply` — the region flip loses its clock-active carve-out
The region block drops `&& !playbackHandler.isEitherClockActive()` — it now covers EVERY resolvable region edit. It
mirrors the engine's `pos >= effectiveLength` early return BEFORE opening the Action (so a no-op held-pad-beyond-loop
edit adds no spurious undo step — a faithfulness fix that also covers the stopped path):
```cpp
if (length_ != 0 && engineStack_->timelineCounterIsSet() && address_.has_value()
    && ctx.model.resolveParam(*address_) != nullptr) {
	// Mirror setValueForRegion's pos>=effectiveLength early return (before any Action), so an edit beyond the loop
	// is a true no-op — no undo step, exactly as the in-line path.
	if (pos_ >= engineStack_->getLoopLength()) {
		return Error::NONE;
	}
	const int32_t oldValue = engineStack_->autoParam->getCurrentValue();
	const bool automatedBefore = engineStack_->autoParam->isAutomated(*engineStack_->doc());
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (action != nullptr) {
		action->recordParamChangeIfNotAlreadySnapshotted(engineStack_);
	}
	std::optional<deluge::project::RegionWriteResult> result = deluge::snapshot::ParamSnapshot::applyPatch(
	    ctx.model, *address_, newValue_, pos_, length_, gatherRegionContext(engineStack_));
	if (result.has_value()) {
		engineStack_->autoParam->projectRegionFromStore(engineStack_, oldValue, automatedBefore, *result);
	}
	return Error::NONE;
}
```
`recordingActive` (the scalar carve-out) is unaffected. Recording-armed region edits: `setValueForRegion` has NO
recording-node path (that is the scalar `setCurrentValueInResponseToUserInput` only), so a clock-active region edit
while armed is just a normal clock-active region edit — covered. `setValueForRegion` itself stays UNCHANGED for its
non-Edit callers.

## Behaviour preservation + gates

Behaviourally identical: the store mutation is the proven-equivalent kernel; the tail reproduces the exact
interpolation-reset / UI-notify / cursor-sync / notify the in-line path does, selected by the same branch decision
the kernel computes. The reordered undo snapshot is unchanged from the stopped-region flip.

- **Standing golden gate, BIT-IDENTICAL:** song 16 / json 38 / edit 40 / param 3 / audio_clip 1, x86≡ARM; firmware
  links. **`param_record` is the critical gate** — it records param automation DURING playback, so a wrong
  interpolation-reset or a dropped UI-notify (which affects the recorded value stream) surfaces there.
  `edit_undo_redo` covers the region undo/redo.
- **`param_automation_test`** gains the `currentValueEffect` assertion across the full matrix (the kernel-faithfulness
  proof).
- **`edit_test`:** extend `testRegionFlipViaApply` with a CLOCK-ACTIVE case is not feasible in the bare harness
  (needs a live transport); the kernel-effect faithfulness is proven by `param_automation_test`, and the
  clock-active apply() path by `param_record` over the corpus. Keep the stopped `testRegionFlipViaApply`.

## Risks / open questions

- **`param_record` is load-bearing.** It is the only gate that exercises clock-active param editing during playback.
  If it diverges, the interpolation-reset vs notify branch selection is the suspect — diff the kernel's
  `currentValueEffect` against the engine at the failing case.
- **The `pos >= effectiveLength` guard is new to the Edit** (the stopped flip lacked it). It removes a latent spurious
  no-op Action; verify goldens stay bit-identical (they should — the guard only skips a no-op).
- **`RegionWriteResult` return-type churn** touches `param_edit.{h,cpp}`, `param_snapshot.{h,cpp}`,
  `param_automation_test.cpp`, `edit_test.cpp`, `set_param_value.cpp`. All mechanical; the kernel's INTERNAL logic is
  untouched (only the return is named).

## Exit criteria

- `SetParamValue::apply`'s region branch is model-first for ALL resolvable region edits (no clock carve-out), with the
  `pos >= effectiveLength` early return; `projectRegionFromStore` reproduces the clock-active interpolation-reset /
  UI-notify tail from the kernel's `RegionCurrentValueEffect`.
- `deluge::project::setValueForRegion` returns `RegionWriteResult`, proven faithful (incl. `currentValueEffect`) by
  `param_automation_test`'s matrix; the engine `AutoParam::setValueForRegion` stays unchanged for its non-Edit callers.
- Goldens bit-identical (x86≡ARM); firmware links; `edit_test` + `param_automation_test` OK.
- **Every reified user param edit (scalar + region, all clock states) now writes the store via `applyPatch`** — the
  in-line poke survives only for unresolvable/not-yet-relocated lanes and recording-active SCALAR (drum-MPE lives in
  `setCurrentValueInResponseToUserInput`, retired by M4d).

## References
- Engine tail: `src/deluge/modulation/automation/auto_param.cpp:1089-1178`
- DOM kernel: `src/deluge-project/param_edit.{h,cpp}` (`setValueForRegion`)
- Equivalence matrix: `tests/projection/param_automation_test.cpp:~1969-2107`
- Edit + patch: `src/deluge/model/edits/set_param_value.cpp`, `src/deluge/model/snapshot/param_snapshot.{h,cpp}`
