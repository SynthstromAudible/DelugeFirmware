# M4a region commit-flip (stopped-transport) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (inline).

**Goal:** Make `ParamSnapshot::applyPatch` the writer of the persistent region param store on the stopped-transport
`SetParamValue` user-edit path — reordering the undo snapshot ahead of the patch — with the engine deriving its RT
tail (cursor + notify) from the store. Clock-active region + unresolvable lanes keep the in-line poke.

**Architecture:** Behaviour-preserving reorder. `apply()`'s region branch, when the lane is model-resolvable and the
transport is stopped, opens the same Action + snapshots the pre-state, calls `applyPatch` (store write), then
`AutoParam::projectRegionFromStore` (cursor sync + notify). `applyPatch` returns the region `automationChanged` so
the notify args match. The M4a A/B toggle retires (both scalar + stopped-region are now flipped in `apply()`).

**Tech Stack:** C++23; `build-sim` (x86) + `build-sim-arm` (qemu); firmware `./dbt build Debug`.

## Global Constraints

- **Behaviour-preserving, goldens BIT-IDENTICAL:** song 16 / json 38 / edit 40 / param 3 / audio_clip 1 (x86≡ARM);
  firmware links. Corpus `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`. `edit_undo_redo` (region
  undo/redo) + `param_record` are load-bearing.
- **`AutoParam::setValueForRegion` stays byte-for-byte unchanged** — clock-active region, and every non-Edit caller,
  depend on it verbatim. The flip lives ONLY in `SetParamValue::apply` + a NEW `projectRegionFromStore` + the
  `applyPatch` return widening.
- **Clock-active region = `playbackHandler.isEitherClockActive()`** is NOT flipped (its interpolation-reset / UI
  notify sub-branch isn't surfaced by the kernel) — it keeps the in-line poke.
- **nullopt / unresolvable address → in-line poke** (as the scalar flip does), so no lane is dropped.
- **Undo snapshot faithfulness:** the flip opens `getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED)` and
  `recordParamChangeIfNotAlreadySnapshotted(engineStack_)` BEFORE `applyPatch` — the exact call + type
  `setValueForRegion` uses at `auto_param.cpp:1109-1112`, so held-pad region drags coalesce identically.
- **Commit discipline:** branch `deluge-project-param`; clang-format hook reformats + FAILS first commit → FRESH
  `git add -A && git commit` (never `--amend`); no Co-Authored-By.

## THE GATE (referenced per task)
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim --target song_roundtrip json_roundtrip edit_undo_redo param_record_diff audio_clip_edit_diff edit_test param_snapshot_bench 2>&1 | grep -E "error:|FAILED"; echo BUILD done
build-sim/edit_test; echo "edit_test rc=$?"
python3 tests/golden/songs_run.py | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff | tail -1
python3 tests/golden/audio_clip_edit_run.py --binary build-sim/audio_clip_edit_diff | tail -1
./dbt build Debug 2>&1 | grep -E "error:|Linking CXX exec"
```
Expected: edit_test rc=0; song 16 / json 38 / edit 40 / param 3 / audio_clip 1; firmware links.

---

### Task 1: `applyPatch` returns `automationChanged`; add `AutoParam::projectRegionFromStore`

**Files:** Modify `src/deluge/model/snapshot/param_snapshot.{h,cpp}`,
`src/deluge/modulation/automation/auto_param.{h,cpp}`; Test `tests/projection/edit_test.cpp`.

**Interfaces:**
- Changed: `static std::optional<bool> ParamSnapshot::applyPatch(...)` — scalar / no-op-resolve return
  `std::nullopt`; region returns the DOM kernel's `automationChanged`.
- Produces: `void AutoParam::projectRegionFromStore(ModelStackWithAutoParam const* modelStack, int32_t oldValue, bool automatedBefore, bool automationChanged)`
  — the stopped-transport region RT tail: `currentValue = doc->current_value` + `notifyParamModifiedInSomeWay`.

- [ ] **Step 1:** In `param_snapshot.h`, change `applyPatch`'s return type `void` → `std::optional<bool>` (add
  `<optional>` already present). Update the doc comment: returns the region kernel's automationChanged, nullopt for
  scalar / unresolved.

- [ ] **Step 2:** In `param_snapshot.cpp`, thread the return: `if (doc == nullptr) return std::nullopt;`
  scalar path `doc->current_value = value; return std::nullopt;` region path
  `return deluge::project::setValueForRegion(...);` (the kernel already returns `std::optional<bool>`).

- [ ] **Step 3:** In `auto_param.h`, declare `projectRegionFromStore` next to `projectScalarFromStore`, doc-commented
  as the stopped-transport region tail.

- [ ] **Step 4:** In `auto_param.cpp`, implement it (after `projectScalarFromStore`):
```cpp
void AutoParam::projectRegionFromStore(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                       bool automatedBefore, bool automationChanged) {
	// M4a region commit-flip: the store (nodes + current_value) was ALREADY written model-first
	// (ParamSnapshot::applyPatch's region kernel). Derive the engine RT tail for the STOPPED-TRANSPORT case — the
	// engine's !clockActive setValueForRegion path takes neither the interpolation reset nor the UI notify, so this
	// is exactly cursor-sync + notify.
	currentValue = modelStack->doc()->current_value;
	bool automatedNow = isAutomated(*modelStack->doc());
	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                                          automatedNow);
}
```

- [ ] **Step 5:** In `edit_test.cpp` `testParamSnapshotEquivalence`, capture the now-returned value on the region
  `applyPatch` call and assert it: `std::optional<bool> ac = deluge::snapshot::ParamSnapshot::applyPatch(...); check(ac.has_value() && *ac == true, "applyPatch region: automationChanged==true (normal homogenize)");`
  (the pilot region edit `[0,96)` in a 128 loop is the normal homogenize path → true). Other `applyPatch` call
  sites (scalar in the same test, `param_snapshot_bench.cpp`, `set_param_value.cpp`) already ignore the return —
  a discarded `std::optional<bool>` is fine (do NOT mark `applyPatch` `[[nodiscard]]`).

- [ ] **Step 6:** THE GATE — expected all green (new method unused in production yet; applyPatch return widened,
  callers unaffected).

- [ ] **Step 7:** Commit `feat(m4a): applyPatch returns region automationChanged + AutoParam::projectRegionFromStore`.

---

### Task 2: flip `apply()`'s region branch; retire the A/B toggle

**Files:** Modify `src/deluge/model/edits/set_param_value.{h,cpp}`; Test `tests/projection/edit_test.cpp`.

- [ ] **Step 1:** In `apply()`, after the scalar model-first block, add the region model-first block:
```cpp
const bool regionPath = length_ != 0 && engineStack_->timelineCounterIsSet();
if (regionPath && !playbackHandler.isEitherClockActive() && address_.has_value()
    && ctx.model.resolveParam(*address_) != nullptr) {
	const int32_t oldValue = engineStack_->autoParam->getCurrentValue();
	const bool automatedBefore = engineStack_->autoParam->isAutomated(*engineStack_->doc());
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
Add includes if missing: `model/action/action.h` (already present via action_logger), `modulation/params/param_set.h`
is not needed. `ActionType` + `ActionAddition` come from `model/action/action_logger.h` (already included).

- [ ] **Step 2:** Retire the toggle: in `emitEvent`, delete the region-patch branch (the `if (m4EventPatchToggle() &&
  length_ != 0 && ...)` block) so `emitEvent` is just the `#if ALPHA_OR_BETA_VERSION` address-resolves observation.
  Delete `bool& m4EventPatchToggle()` (definition in `.cpp`, declaration in `.h`). Keep the file-local
  `gatherRegionContext` helper — it is now used by `apply()`'s region block.

- [ ] **Step 3:** In `edit_test.cpp`: remove `testEmitEventDrivesPatch` (it tested the now-deleted toggle-gated
  emitEvent patch) and its `main()` call. Add `testRegionFlipViaApply(ctx, song, audioClip)` mirroring
  `testScalarFlipViaApply` but with a stopped-transport region edit:
```cpp
void testRegionFlipViaApply(deluge::edits::Context& ctx, ::Song* engineSong, TimelineCounter* clipTC) {
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	constexpr int32_t kParam = 7;
	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	pm.getUnpatchedParamSet()->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);
	ms->song = engineSong;
	ms->setTimelineCounter(clipTC);
	ms->paramManager = &pm;
	std::optional<deluge::project::ParamAddress> addrOpt = deluge::edits::addressFor(*ms);
	check(addrOpt.has_value(), "region-flip: resolvable address");
	deluge::project::ParamAddress addr = *addrOpt;
	deluge::project::DocAutoParam* store = ms->doc();
	check(store != nullptr && ctx.model.resolveParam(addr) == store, "region-flip: store resolves");
	// Seed a known scalar baseline so the far-edge node value is predictable.
	store->nodes.erase(store->nodes.begin(), store->nodes.end());
	store->current_value = 0x40000000;
	{
		deluge::edits::SetParamValue edit{addr, 0x20000000, 0, 96, ms};
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "region-flip: apply succeeds");
	}
	// Model-first: applyPatch homogenized [0,96); projectRegionFromStore synced the cursor (clock inactive).
	check(store->nodes.size() == 2, "region-flip: node count (homogenized)");
	check(store->nodes[0].pos == 0 && store->nodes[0].value == 0x20000000, "region-flip: node0 == value");
	check(store->nodes[1].pos == 96 && store->nodes[1].value == 0x40000000, "region-flip: node1 == pre-region value");
	check(store->current_value == 0x20000000, "region-flip: current_value synced");
	check(ms->autoParam->getCurrentValue() == 0x20000000, "region-flip: engine cursor synced");
	// Undo restores the pre-mutation state (the reordered snapshot): nodes cleared, value back.
	actionLogger.revert(BEFORE, /*updateVisually=*/false, /*doNavigation=*/false);
	check(store->nodes.empty(), "region-flip: undo cleared the nodes");
	check(ms->autoParam->getCurrentValue() == 0x40000000, "region-flip: undo restored engine value");
}
```
Wire `testRegionFlipViaApply(ctx, song, audioClip);` into `main()` after `testScalarFlipViaApply`.

- [ ] **Step 4:** THE GATE — bit-identical goldens (`edit_undo_redo` region undo/redo is the key check), edit_test
  rc=0, firmware links. Then rebuild + run edit_test on ARM:
```bash
cmake --build build-sim-arm --target edit_test 2>&1 | grep -E "error:|Linking"
qemu-arm -L /usr/arm-linux-gnueabihf build-sim-arm/edit_test; echo "arm rc=$?"
```

- [ ] **Step 5:** Commit `feat(m4a): SetParamValue::apply writes the stopped-transport region store model-first (undo snapshot reordered; A/B toggle retired)`.

---

## Notes / Risks
- **Undo revert of the reordered snapshot.** The Consequence captured before `applyPatch` must restore nodes +
  current_value on revert. `testRegionFlipViaApply`'s undo asserts it; `edit_undo_redo` over the corpus is the
  broad net. If a region undo diverges, check that `recordParamChangeIfNotAlreadySnapshotted` captured the FULL
  DocAutoParam (it snapshots via `recordParamChangeDefinitely`).
- **automatedBefore before mutation.** Read `isAutomated` BEFORE `applyPatch` (a whole-region clear flips
  automated→unautomated); notify arg order matches `auto_param.cpp:1176`.
- **Clock-active region stays in-line — intentional.** Its store is still model-written (via the DOM kernel inside
  `setValueForRegion`); only the *driver* differs. Flipping it needs the kernel to surface the interpolation/UI
  sub-branch — a later slice.
