# M4a clock-active region commit-flip Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (inline).

**Goal:** Make `ParamSnapshot::applyPatch` the writer of the persistent region param store for ALL clock states, by
teaching the DOM `setValueForRegion` kernel to return its value-branch decision (`RegionCurrentValueEffect`) and
having `AutoParam::projectRegionFromStore` reproduce the clock-active interpolation-reset / UI-notify tail from it.

**Architecture:** Behaviour-preserving. Task 1 is pure plumbing (kernel + patch return type widened to name the
branch the kernel already takes; `projectRegionFromStore` ready for all clock states; apply() still stopped-only, so
no behaviour change — proven by the kernel-faithfulness matrix + bit-identical goldens). Task 2 removes apply()'s
clock carve-out + adds the `pos >= effectiveLength` early return, routing clock-active region through the new path.

**Tech Stack:** C++23; `build-sim` (x86) + `build-sim-arm` (qemu); firmware `./dbt build Debug`.

## Global Constraints

- **Behaviour-preserving, goldens BIT-IDENTICAL:** song 16 / json 38 / edit 40 / param 3 / audio_clip 1 (x86≡ARM);
  firmware links. Corpus `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`. **`param_record` is the
  critical gate** (clock-active param editing during playback); `edit_undo_redo` covers region undo; `edit_test` +
  `param_automation_test` cover the kernel + patch.
- **The engine `AutoParam::setValueForRegion` stays byte-for-byte unchanged.** Only the DOM kernel
  `deluge::project::setValueForRegion`'s RETURN is widened (internal logic untouched); the flip lives in
  `SetParamValue::apply` + `projectRegionFromStore`.
- **Commit discipline:** branch `deluge-project-param`; clang-format hook reformats + FAILS first commit → FRESH
  `git add -A && git commit` (never `--amend`); no Co-Authored-By.

## THE GATE (referenced per task)
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim --target song_roundtrip json_roundtrip edit_undo_redo param_record_diff audio_clip_edit_diff edit_test param_projection_test param_snapshot_bench 2>&1 | grep -E "error:|FAILED"; echo BUILD done
build-sim/edit_test; echo "edit_test rc=$?"
build-sim/param_projection_test; echo "param_projection_test rc=$?"
python3 tests/golden/songs_run.py | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff | tail -1
python3 tests/golden/audio_clip_edit_run.py --binary build-sim/audio_clip_edit_diff | tail -1
./dbt build Debug 2>&1 | grep -E "error:|Linking CXX exec"
```
Expected: edit_test rc=0; param_projection_test rc=0; song 16 / json 38 / edit 40 / param 3 / audio_clip 1; firmware links.
(`param_automation_test` builds as target `param_projection_test` — verify the executable name in `sim/CMakeLists.txt`.)

---

### Task 1: kernel returns `RegionWriteResult`; patch + projection + tests rewired (behaviour-neutral plumbing)

**Files:** Modify `src/deluge-project/param_edit.{h,cpp}`, `src/deluge/model/snapshot/param_snapshot.{h,cpp}`,
`src/deluge/modulation/automation/auto_param.{h,cpp}`, `src/deluge/model/edits/set_param_value.cpp`,
`tests/projection/param_automation_test.cpp`, `tests/projection/edit_test.cpp`.

**Interfaces:**
- Produces: `deluge::project::RegionCurrentValueEffect {Set, SetAndResetInterp, NotifyOnly}` +
  `deluge::project::RegionWriteResult {bool automationChanged; RegionCurrentValueEffect currentValueEffect;}`;
  `std::optional<RegionWriteResult> setValueForRegion(...)`; `std::optional<RegionWriteResult> ParamSnapshot::applyPatch(...)`;
  `void AutoParam::projectRegionFromStore(ModelStackWithAutoParam const*, int32_t oldValue, bool automatedBefore, deluge::project::RegionWriteResult result)`.

- [ ] **Step 1: `param_edit.h`** — add the enum + struct (above `setValueForRegion`'s declaration) and change
  `setValueForRegion`'s return `std::optional<bool>` → `std::optional<RegionWriteResult>`. Update its doc comment: the
  return now carries `automationChanged` + which value-branch it took.

- [ ] **Step 2: `param_edit.cpp`** — thread the result through `setValueForRegion`:
  - `pos >= effectiveLength` → `return std::nullopt;`
  - whole-region → `return RegionWriteResult{false, RegionCurrentValueEffect::Set};`
  - `firstI == -1` → `return std::nullopt;`
  - `!clockActive` → `p.current_value = value; return RegionWriteResult{true, RegionCurrentValueEffect::Set};`
  - clock-active: after computing `mostRecentI`, `if (mostRecentI == firstI) { p.current_value = value; return RegionWriteResult{true, RegionCurrentValueEffect::SetAndResetInterp}; } return RegionWriteResult{true, RegionCurrentValueEffect::NotifyOnly};`

- [ ] **Step 3: `param_snapshot.h/.cpp`** — return type `std::optional<bool>` → `std::optional<RegionWriteResult>`
  (include `deluge-project/param_edit.h` in the .h, or forward-declare + include in .cpp — the struct is by-value in
  the signature so the header needs the complete type; include `param_edit.h`). Scalar / unresolved →
  `std::nullopt`; region → `return deluge::project::setValueForRegion(...);` (now returns the result directly).

- [ ] **Step 4: `auto_param.h`** — include `deluge-project/param_edit.h`; change `projectRegionFromStore`'s 4th param
  `bool automationChanged` → `deluge::project::RegionWriteResult result`.

- [ ] **Step 5: `auto_param.cpp`** — reimplement `projectRegionFromStore` per the design (§3): branch on
  `result.currentValueEffect` (`SetAndResetInterp` → `resetInterpolationIncrement()`; `NotifyOnly` →
  `view.notifyParamAutomationOccurred(modelStack->paramManager)`), then `currentValue = doc->current_value` + notify
  with `result.automationChanged`. `view` is already used in this file; `param_set.h` (notifyParamModifiedInSomeWay)
  already included.

- [ ] **Step 6: `set_param_value.cpp`** — update the EXISTING stopped-region call site: capture
  `std::optional<deluge::project::RegionWriteResult> result = ...applyPatch(...)` and call
  `if (result.has_value()) engineStack_->autoParam->projectRegionFromStore(engineStack_, oldValue, automatedBefore, *result);`
  Keep the `&& !playbackHandler.isEitherClockActive()` guard for now (Task 2 removes it). The stopped path only ever
  yields `Set`, so behaviour is identical.

- [ ] **Step 7: `param_automation_test.cpp`** — make the `engineSetValueForRegion` lambda RETURN the expected effect
  so the kernel's field is verified. Change its return type to `std::optional<deluge::project::RegionCurrentValueEffect>`:
  `pos>=effLen`/`firstI==-1` → `return std::nullopt;`; whole-region → `return RegionCurrentValueEffect::Set;` (after
  the clear+set); `!clockActive` → `return RegionCurrentValueEffect::Set;`; `mostRecentI==firstI` →
  `return RegionCurrentValueEffect::SetAndResetInterp;`; else → `return RegionCurrentValueEffect::NotifyOnly;`. In the
  matrix loop, capture `auto expectedEffect = engineSetValueForRegion(...);` and add
  `check(domRet.has_value() == expectedEffect.has_value(), "setValueForRegion: has_value matches engine");` and
  `check(!domRet.has_value() || domRet->currentValueEffect == *expectedEffect, "setValueForRegion: currentValueEffect matches engine branch");`
  Change every `domRet` / spot-check `*r` deref: `*r == false/true` → `r->automationChanged == false/true`;
  `domRet.has_value()` stays. The `ap.currentValue == doc.current_value` + `expectDocMatchesEngine` asserts are
  unchanged.

- [ ] **Step 8: `edit_test.cpp`** — in `testParamSnapshotEquivalence`, the region assertion
  `regionChanged.has_value() && *regionChanged` → `regionChanged.has_value() && regionChanged->automationChanged`
  (add: `&& regionChanged->currentValueEffect == deluge::project::RegionCurrentValueEffect::Set` — the pilot region
  edit is stopped/clock-inactive-in-harness → `Set`). `testRegionFlipViaApply` is unchanged (still asserts store +
  cursor + undo).

- [ ] **Step 9: THE GATE** — all green, goldens BIT-IDENTICAL (apply() still flips only stopped region; the plumbing
  is behaviour-neutral, and `param_automation_test` proves the kernel's new field faithful).

- [ ] **Step 10: Commit** `feat(m4a): setValueForRegion returns RegionWriteResult (names its value-branch); projectRegionFromStore reproduces the clock-active tail`.

---

### Task 2: `apply()` flips ALL region edits (drop clock carve-out + add pos guard)

**Files:** Modify `src/deluge/model/edits/set_param_value.cpp`.

- [ ] **Step 1:** In the region block, delete `&& !playbackHandler.isEitherClockActive()` from the condition, and add
  the early return at the TOP of the block (before `getNewAction`):
```cpp
if (length_ != 0 && engineStack_->timelineCounterIsSet() && address_.has_value()
    && ctx.model.resolveParam(*address_) != nullptr) {
	// Mirror setValueForRegion's pos>=effectiveLength early return (before any Action) so a held-pad edit beyond the
	// loop is a true no-op — no undo step, exactly as the in-line path.
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
Update the block's lead comment: region is now model-first for ALL clock states; only unresolvable/not-yet-relocated
lanes keep the in-line poke.

- [ ] **Step 2: THE GATE** — goldens BIT-IDENTICAL. **`param_record` is the critical check** (clock-active editing
  during playback). Then rebuild + run `edit_test` on ARM:
```bash
cmake --build build-sim-arm --target edit_test param_projection_test 2>&1 | grep -E "error:|Linking"
qemu-arm -L /usr/arm-linux-gnueabihf build-sim-arm/edit_test; echo "arm edit_test rc=$?"
qemu-arm -L /usr/arm-linux-gnueabihf build-sim-arm/param_projection_test; echo "arm param_projection_test rc=$?"
```

- [ ] **Step 3: Commit** `feat(m4a): SetParamValue::apply writes the CLOCK-ACTIVE region store model-first — every reified param edit is now model-first`.

---

## Notes / Risks
- **`param_record` divergence** ⇒ the interpolation-reset vs UI-notify branch (or the pos guard) is the suspect;
  diff the kernel `currentValueEffect` against the engine at the failing case.
- **`param_projection_test` executable name** — confirm the `param_automation_test.cpp` target's output name in
  `sim/CMakeLists.txt` (it is registered as `param_projection_test` in the ctest block); build that target.
- **Header include of `param_edit.h` in `auto_param.h`** — `param_edit.h` is `<cstdint>` + `param_automation.h`
  (already an `auto_param.h` include), so no heavy/circular pull.
