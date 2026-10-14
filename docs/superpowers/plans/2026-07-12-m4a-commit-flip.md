# M4a commit-flip (scalar) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (inline) or subagent-driven-development.

**Goal:** Make `ParamSnapshot::applyPatch` the sole writer of the persistent scalar param store on the
`SetParamValue` user-edit path, with the engine deriving its RT state (cursor/interpolation/notify/latch) from the
store — deleting the redundant in-line scalar store write. Region + recording stay on the in-line poke (deferred).

**Architecture:** Behaviour-preserving reorder. `SetParamValue::apply`'s scalar branch, when the lane is
model-resolvable and NOT recording-active, writes the store via `applyPatch` then calls a new
`AutoParam::projectScalarFromStore` that reproduces the scalar non-recording RT tail from the store. All other cases
(region, recording-active scalar, unresolvable/nullopt lane) keep the existing in-line `setValuePossiblyForRegion`.

**Tech Stack:** C++23; `build-sim` (x86) + `build-sim-arm` (qemu); firmware `./dbt build Debug`.

## Global Constraints

- **Behaviour-preserving, goldens BIT-IDENTICAL:** song 16 / json 38 / edit 40 / param 3 / audio_clip 1 (x86≡ARM);
  firmware links. Corpus `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`. `param_record` +
  `edit_undo_redo` are load-bearing (playing + undo of scalar edits).
- **`setCurrentValueInResponseToUserInput` stays byte-for-byte unchanged** — its direct drum-MPE caller and the
  region/recording paths depend on it verbatim. The flip lives ONLY in `SetParamValue::apply` + a NEW method.
- **Recording-active scalar = the recording branch** (`isPlaying && recording != OFF && armedForRecording`) RECORDS
  automation nodes; it is NOT flipped — it stays on the in-line poke, grouped with the deferred region/recording.
- **nullopt / unresolvable address → in-line poke.** `applyPatch` no-ops when `resolveParam` fails, so the flip is
  taken ONLY when `address_.has_value() && ctx.model.resolveParam(*address_) != nullptr`; otherwise the in-line poke
  (which resolves via `doc()` with relocate/fallback) runs — no lane dropped.
- **Commit discipline:** branch `deluge-project-param`; clang-format hook reformats + FAILS first commit → FRESH
  `git add -A && git commit` (never `--amend`); no Co-Authored-By.

## THE GATE (referenced per task)
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim --target song_roundtrip json_roundtrip edit_undo_redo param_record_diff audio_clip_edit_diff edit_test 2>&1 | grep -E "error:|FAILED"; echo BUILD done
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

### Task 1: `AutoParam::projectScalarFromStore` — the extracted scalar RT tail

**Files:** Modify `src/deluge/modulation/automation/auto_param.{h,cpp}`; Test `tests/projection/edit_test.cpp`.

**Interfaces:**
- Produces: `void AutoParam::projectScalarFromStore(ModelStackWithAutoParam const* modelStack, int32_t oldValue, bool isPlaying)`
  — reproduces the NON-recording scalar tail of `setCurrentValueInResponseToUserInput` from the ALREADY-written
  store `current_value`: interpolation reset, the playing-non-recording `renewedOverridingAtTime` latch, cursor
  sync, notify. Sources the value from `modelStack->doc()->current_value` (no `value` arg).

- [ ] **Step 1:** Add the declaration in `auto_param.h` next to `setCurrentValueInResponseToUserInput`, doc-commented
  as the model-first projection half of the scalar poke (M4a commit-flip).

- [ ] **Step 2:** Implement in `auto_param.cpp` — a faithful transcription of the non-recording scalar tail
  (`:180`, `:326-334`, `:352-359`), reading the value from the store:
```cpp
void AutoParam::projectScalarFromStore(ModelStackWithAutoParam const* modelStack, int32_t oldValue, bool isPlaying) {
	// M4a commit-flip: the store's current_value was ALREADY written model-first (ParamSnapshot::applyPatch); this
	// derives the engine's RT state from it, reproducing setCurrentValueInResponseToUserInput's non-recording scalar
	// tail exactly. Scalar applyPatch never touches nodes, so isAutomated is invariant across the write
	// (automatedBefore == automatedNow) and the notify's automationChanged is false.
	auto& nodesRef = modelStack->doc()->nodes;
	resetInterpolationIncrement();
	if (isPlaying && !nodesRef.empty()) {
		renewedOverridingAtTime = AudioEngine::audioSampleTimer;
		if (renewedOverridingAtTime <= 1) {
			renewedOverridingAtTime = 0xFFFFFFFF;
		}
	}
	currentValue = modelStack->doc()->current_value;
	bool automatedNow = isAutomated(*modelStack->doc());
	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, false, automatedNow, automatedNow);
}
```

- [ ] **Step 3:** Add an `edit_test` assertion (in `testParamSnapshotEquivalence`, which already has a resolvable
  store `store`): write `store->current_value = 0x33333333`, set the engine `ms->autoParam` cursor to a different
  value, call `ms->autoParam->projectScalarFromStore(ms, /*oldValue*/ prev, /*isPlaying*/ false)`, assert
  `ms->autoParam->getCurrentValue() == 0x33333333` (cursor synced from store). (notify's effect is covered by the
  goldens; the cursor sync is the observable contract here.)

- [ ] **Step 4:** THE GATE — expected all green (new method has one test caller; production paths unchanged).

- [ ] **Step 5:** Commit `feat(m4a): AutoParam::projectScalarFromStore — derive the scalar RT cursor/notify from the model store`.

---

### Task 2: flip `SetParamValue::apply`'s scalar branch to model-first

**Files:** Modify `src/deluge/model/edits/set_param_value.cpp`; Test `tests/projection/edit_test.cpp`.

**Interfaces:**
- Consumes: `ParamSnapshot::applyPatch`, `AutoParam::projectScalarFromStore`, `ctx.model.resolveParam`.

- [ ] **Step 1:** In `apply(Context& ctx)` (un-anonymise the param — it now uses `ctx.model`), hoist `isPlaying` and
  `recordingArmed` out of the pre-log block so they are available for the flip decision. Compute
  `recordingActive = isPlaying && recordingArmed`.

- [ ] **Step 2:** After the existing undo pre-log, take the model-first path when eligible; else the current in-line
  poke unchanged:
```cpp
const bool flippable = scalarPath && !recordingActive && address_.has_value()
                       && ctx.model.resolveParam(*address_) != nullptr;
if (flippable) {
	const int32_t oldValue = engineStack_->autoParam->getCurrentValue();
	deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, *address_, newValue_, 0, 0, std::nullopt);
	engineStack_->autoParam->projectScalarFromStore(engineStack_, oldValue, isPlaying);
	return Error::NONE;
}
// Region, recording-active scalar, or an unresolvable/nullopt lane: the in-line poke stays the writer.
engineStack_->autoParam->setValuePossiblyForRegion(newValue_, engineStack_, pos_, length_, mayDeleteNodesInLinearRun_);
return Error::NONE;
```
Add includes: `model/snapshot/param_snapshot.h` (already present), `modulation/automation/auto_param.h` (present).

- [ ] **Step 3:** Simplify `emitEvent` — the scalar store write is now owned by `apply()`, so `emitEvent` must NOT
  also patch scalar (double-write). Keep the `ALPHA_OR_BETA` address-resolves observation. Keep the REGION
  event-patch behind `m4EventPatchToggle()` (region is not yet flipped, so the toggle-driven region A/B stays for the
  equivalence spec). I.e. in `emitEvent`, drop the scalar `applyPatch(... std::nullopt)` branch; keep only the
  region branch under the toggle.

- [ ] **Step 4:** Extend `edit_test`'s `testSetParamValuePilot` to run over a RESOLVABLE lane (song + clip set, as
  in `testParamSnapshotEquivalence`) so the flip is exercised, and assert after `apply`: the engine cursor AND the
  model store `current_value` both equal the new value, and undo still restores the old engine value (the pre-log
  path is unchanged). Keep the existing song-less pilot too (exercises the nullopt/in-line fallback).

- [ ] **Step 5:** THE GATE — bit-identical goldens (the flip only reorders store-then-cursor; both reach the same
  state), edit_test rc=0, firmware links. Then rebuild+run edit_test on ARM:
```bash
cmake --build build-sim-arm --target edit_test 2>&1 | grep -E "error:|Linking"
qemu-arm -L /usr/arm-linux-gnueabihf build-sim-arm/edit_test; echo "arm rc=$?"
```

- [ ] **Step 6:** Commit `feat(m4a): SetParamValue::apply writes the scalar store model-first via applyPatch (in-line poke retired for the resolvable scalar path)`.

---

## Notes / Risks
- **notify args:** `automatedBefore`/`automatedNow` are equal for the scalar flip (applyPatch never touches nodes),
  and `automationChanged` is false — matching the non-recording `getOut:` notify exactly. If `param_record`
  diverges, re-check the notify arg order against `auto_param.cpp:358`.
- **Relocation timing:** a resolvable-but-unrelocated lane returns null from `resolveParam` on first touch → the
  in-line poke runs (and relocates via `doc()`); the next edit is model-first. Safe, self-healing.
- **Region asymmetry is intentional:** post-flip, scalar writes via applyPatch, region via setValueForRegion — both
  the same store. Region flip is a separate slice (needs the region undo-snapshot reorder).
