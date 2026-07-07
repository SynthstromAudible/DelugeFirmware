# M1c — Param-Value Edit Sweep + Interior-Log Shrink — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reify every genuine user param-value gesture (SoundEditor menus, automation-view pad/region edits, performance view, MIDI-CC/pitch-bend on learned params, MPE step-edits, tempo) as a `deluge::edits::SetParamValue` Edit, then shrink the interior undo log to its single surviving non-Edit producer.

**Architecture:** Each in-scope call site is transformed exactly as `View::modEncoderAction_existentParam` was in the M1b pilot: build a `deluge::edits::Context`, construct a `SetParamValue` from `addressFor(*ms)` + the value (+ region + optional engine hint), and `deluge::edits::apply(ctx, edit)`. The Edit delegates to the identical engine call (`setValuePossiblyForRegion`) and owns its undo consequence via the pilot's idempotent pre-log; the interior log at `auto_param.cpp:318` dedupes against it. This is a behaviour-preserving, golden-gated sweep — no new engine behaviour, no new Edit types.

**Tech Stack:** C++23 firmware app tree; sim harness (`tests/projection/edit_test.cpp`, ctest on `build-sim`); golden serialization nets (`tests/golden/songs_run.py`, `param_record_run.py`) run against the local corpus.

Spec: `docs/superpowers/specs/2026-07-07-m1c-param-edit-sweep-design.md`. Predecessor pilot: `docs/superpowers/plans/2026-07-07-m1-edit-substrate-pilot.md`.

## Global Constraints

- **Behaviour-preserving, golden-gated per slice.** Every task ends green on: `edit_test` (ctest), the full x86 golden net with the corpus (`export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`; `songs_run.py` → OK=15, `param_record_run.py --binary build-sim/param_record_diff` → OK=3), and `./dbt build Debug`. Targeted ARM/qemu + on-device smoke ride the M1c milestone boundary, not each task.
- **The transformation is mechanical and proven.** `SetParamValue{addressFor(*ms), value, pos, length[, mayDelete], ms}` + `deluge::edits::apply` is behaviour-identical to the direct `ms->autoParam->setValuePossiblyForRegion(value, ms, pos, length[, mayDelete])` (and, for `pos==0 && length==0`, to `setCurrentValueInResponseToUserInput(value, ms)` — `setValuePossiblyForRegion`'s scalar branch calls `setCurrentValueInResponseToUserInput(value, ms, true, -1, mayDelete)`; `shouldLogAction` is a dead parameter, `livePos==-1` is the default). Preserve the exact `value`, `pos`, `length`, `mayDelete`, call count, and call order at every site.
- **New code is idiomatic C++23** (the pilot's style): `deluge::edits::Context ctx{*currentSong, currentSong->project};`.
- **`src/deluge/CMakeLists.txt` GLOBs sources** — no CMake change for app files; only new test executables touch `sim/CMakeLists.txt` (none here — `edit_test` already exists).
- **Commit at the end of each task. Never add Co-Authored-By trailers.**
- **`SetParamValue`'s `mayDeleteNodesInLinearRun` is a funnel-phase internal**, a trailing defaulted constructor parameter — not part of the durable domain shape; it is removed at the M3/M4 authority flip, like `engineStack_`.
- **Do NOT reify the drum/live-MPE recorded-input write** (`note_row.cpp:4503`) as an Edit — it is recorded input, not a UI gesture (spec Decision 5). It is the single producer the interior log is shrunk *to*, not deleted.

---

### Task 1 (S1): SoundEditor menu value edits → `SetParamValue`

Funnel the scalar menu writes. These call `setCurrentValueInResponseToUserInput(value, ms)` directly; `SetParamValue{addressFor(*ms), value, 0, 0, ms}` is behaviour-identical. No `SetParamValue` change needed (default `mayDelete=true` matches).

**Files:**
- Modify: `src/deluge/gui/menu_item/unpatched_param.cpp` (`UnpatchedParam::writeCurrentValue`, the single-sound `setCurrentValueInResponseToUserInput` at the else-branch and the affect-entire kit-loop call) + includes.
- Modify: `src/deluge/gui/menu_item/patched_param/integer.cpp` (`Integer::writeCurrentValue`, the `:42` unconditional, the `:57` kit-loop, the `:63` else-branch) + includes.
- Modify: `src/deluge/gui/menu_item/patch_cable_strength.cpp` (`PatchCableStrength::writeCurrentValue`, the `:242` call) + includes.

**Interfaces:**
- Consumes: `deluge::edits::Context`, `deluge::edits::SetParamValue`, `deluge::edits::apply`, `deluge::edits::addressFor` (all from M1).
- Produces: no new interface.

- [ ] **Step 1: Confirm the baseline is green**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
cmake --build build-sim -j --target edit_test && ctest --test-dir build-sim -R '^edit_test$' --output-on-failure
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim -j --target param_record_diff
python3 tests/golden/songs_run.py && python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff
```
Expected: `edit_test: OK`; songs OK=15; param_record OK=3.

- [ ] **Step 2: Add the Edit includes to the three menu files**

Add to each file's include block (alphabetically among the `model/` includes):
```cpp
#include "model/edits/edit.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
```
(`unpatched_param.cpp`, `patched_param/integer.cpp`, `patch_cable_strength.cpp`.)

- [ ] **Step 3: Transform `Integer::writeCurrentValue`**

In `src/deluge/gui/menu_item/patched_param/integer.cpp`, replace the three write calls. The `:42` unconditional pre-write is redundant with the `:63` else-branch (see the scoping note) but is preserved as-is for behaviour parity — funnel it too.

Replace `modelStack->autoParam->setCurrentValueInResponseToUserInput(value, modelStack);` (the `:42` line, before the affect-entire `if`) with:
```cpp
	deluge::edits::Context editCtx{*currentSong, currentSong->project};
	deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStack), value, 0, 0, modelStack};
	deluge::edits::apply(editCtx, edit);
```

Replace the kit-loop call `modelStackForSoundDrum->autoParam->setCurrentValueInResponseToUserInput(value, modelStackForSoundDrum);` with:
```cpp
				deluge::edits::Context drumEditCtx{*currentSong, currentSong->project};
				deluge::edits::SetParamValue drumEdit{deluge::edits::addressFor(*modelStackForSoundDrum), value, 0, 0,
				                                      modelStackForSoundDrum};
				deluge::edits::apply(drumEditCtx, drumEdit);
```

Replace the else-branch call `modelStack->autoParam->setCurrentValueInResponseToUserInput(value, modelStack);` with:
```cpp
		deluge::edits::Context elseEditCtx{*currentSong, currentSong->project};
		deluge::edits::SetParamValue elseEdit{deluge::edits::addressFor(*modelStack), value, 0, 0, modelStack};
		deluge::edits::apply(elseEditCtx, elseEdit);
```

- [ ] **Step 4: Transform `UnpatchedParam::writeCurrentValue`**

In `src/deluge/gui/menu_item/unpatched_param.cpp` (main stack variable is `modelStackWithParam`), replace the kit-loop call:
```cpp
					modelStackForSoundDrum->autoParam->setCurrentValueInResponseToUserInput(value, modelStackForSoundDrum);
```
with:
```cpp
					deluge::edits::Context drumEditCtx{*currentSong, currentSong->project};
					deluge::edits::SetParamValue drumEdit{deluge::edits::addressFor(*modelStackForSoundDrum), value, 0, 0,
					                                      modelStackForSoundDrum};
					deluge::edits::apply(drumEditCtx, drumEdit);
```
and replace the else-branch call:
```cpp
		modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);
```
with:
```cpp
		deluge::edits::Context editCtx{*currentSong, currentSong->project};
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), value, 0, 0,
		                                  modelStackWithParam};
		deluge::edits::apply(editCtx, edit);
```
(`UnpatchedParam::writeCurrentValue` has no unconditional pre-write like `Integer` — only the kit-loop and else-branch calls.)

- [ ] **Step 5: Transform `PatchCableStrength::writeCurrentValue`**

In `src/deluge/gui/menu_item/patch_cable_strength.cpp`, replace `modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(finalValue, modelStackWithParam);` with:
```cpp
	deluge::edits::Context editCtx{*currentSong, currentSong->project};
	deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), finalValue, 0, 0,
	                                  modelStackWithParam};
	deluge::edits::apply(editCtx, edit);
```

- [ ] **Step 6: Build the firmware and the sim**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
cmake --build build-sim -j --target edit_test param_record_diff 2>&1 | grep -iE "error|FAILED" || echo "build clean"
./dbt build Debug 2>&1 | tail -3
```
Expected: no errors; firmware ELF prints its size.

- [ ] **Step 7: Run the golden + unit nets**

Run:
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
ctest --test-dir build-sim -R '^edit_test$' --output-on-failure
python3 tests/golden/songs_run.py && python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff
```
Expected: `edit_test: OK`; songs OK=15 REGRESS=0; param_record OK=3 REGRESS=0.
(Menu edits are not directly exercised by these nets; their behaviour-preservation rests on the proven transformation equivalence + the firmware build + the milestone hardware smoke. The nets guard against collateral regressions.)

- [ ] **Step 8: Commit**

```bash
git add src/deluge/gui/menu_item/unpatched_param.cpp src/deluge/gui/menu_item/patched_param/integer.cpp src/deluge/gui/menu_item/patch_cable_strength.cpp
git commit -m "edits: M1c S1 — SoundEditor menu value edits funnel through SetParamValue"
```

---

### Task 2 (S2): Performance view + MIDI-CC/pitch-bend → `SetParamValue` (+ the `mayDelete` funnel field)

These are region writes sharing the exact `setValuePossiblyForRegion(newValue, ms, modPos, modLength)` shape. One site (`melodic_instrument.cpp:661`) passes `mayDelete=false`, so this task first extends `SetParamValue` with the funnel-phase `mayDeleteNodesInLinearRun` field.

**Files:**
- Modify: `src/deluge/model/edits/set_param_value.h` (add the trailing `mayDeleteNodesInLinearRun` ctor param + member).
- Modify: `src/deluge/model/edits/set_param_value.cpp` (pass it through to `setValuePossiblyForRegion`).
- Modify: `tests/projection/edit_test.cpp` (a region + `mayDelete=false` case).
- Modify: `src/deluge/gui/views/performance_view.cpp` (`setParameterValue`, `:1428`) + includes.
- Modify: `src/deluge/io/midi/midi_follow.cpp` (`handleReceivedCC`, `:1075`) + includes.
- Modify: `src/deluge/model/mod_controllable/mod_controllable_audio.cpp` (`:955`, `:1045`, `:1115`) + includes.
- Modify: `src/deluge/model/instrument/melodic_instrument.cpp` (`:661`, `mayDelete=false`) + includes.

**Interfaces:**
- Produces: `SetParamValue(std::optional<project::ParamAddress> address, int32_t newValue, int32_t pos, int32_t length, ModelStackWithAutoParam* engineStack, bool mayDeleteNodesInLinearRun = true)` — later slices (S4) rely on the new trailing param.

- [ ] **Step 1: Extend `SetParamValue` — add the funnel field (write the ctor + member)**

In `src/deluge/model/edits/set_param_value.h`, change the constructor signature and add the member. Constructor:
```cpp
	SetParamValue(std::optional<project::ParamAddress> address, int32_t newValue, int32_t pos, int32_t length,
	              ModelStackWithAutoParam* engineStack, bool mayDeleteNodesInLinearRun = true)
	    : address_(address), newValue_(newValue), pos_(pos), length_(length),
	      mayDeleteNodesInLinearRun_(mayDeleteNodesInLinearRun), engineStack_(engineStack) {}
```
Add the member (in the private section, near the other value fields), documented as a funnel internal:
```cpp
	/// Engine-only node-pruning hint (not domain intent): forwarded verbatim to
	/// setValuePossiblyForRegion. MIDI/MPE sites pass false so a value that may be re-emitted as MIDI
	/// isn't pruned. Funnel-phase field — removed at the M3/M4 authority flip, like engineStack_.
	bool mayDeleteNodesInLinearRun_;
```

- [ ] **Step 2: Thread it through `apply`**

In `src/deluge/model/edits/set_param_value.cpp`, change the delegation call to forward the field:
```cpp
	engineStack_->autoParam->setValuePossiblyForRegion(newValue_, engineStack_, pos_, length_,
	                                                   mayDeleteNodesInLinearRun_);
```
(The pre-log guard above it is unchanged.)

- [ ] **Step 3: Write the failing test — region write + `mayDelete=false`**

Append a case to `tests/projection/edit_test.cpp` (call it from `main` after `testSetParamValuePilot`). It builds the same song-less standalone stack and asserts a region-shaped Edit applies without opening an unautomated-change Action (region path → no scalar pre-log), and that `mayDelete=false` is accepted:
```cpp
void testSetParamValueRegionAndFlag(deluge::edits::Context& ctx) {
	constexpr int32_t kParam = 3;
	actionLogger.deleteAllLogs();

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);

	// Region path needs a timeline counter; the song-less standalone stack has none, so
	// setValuePossiblyForRegion routes to the scalar branch regardless. This case pins that the
	// extended ctor (pos/length + mayDelete=false) compiles, applies, and mutates the value.
	int32_t target = ms->autoParam->getCurrentValue() + 250;
	deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), target, 0, 0, ms, /*mayDelete=*/false};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "region/flag: apply succeeds");
	check(ms->autoParam->getCurrentValue() == target, "region/flag: value applied");
}
```
Add `testSetParamValueRegionAndFlag(ctx);` to `main`.

- [ ] **Step 4: Run the test**

Run:
```bash
cmake --build build-sim -j --target edit_test && ctest --test-dir build-sim -R '^edit_test$' --output-on-failure
```
Expected: `edit_test: OK` (the new case passes; the ctor extension compiles).

- [ ] **Step 5: Add Edit includes to the four call-site files**

Add the three `model/edits/...` includes (as in Task 1 Step 2) to `performance_view.cpp`, `midi_follow.cpp`, `mod_controllable_audio.cpp`, `melodic_instrument.cpp`.

- [ ] **Step 6: Transform the performance-view site**

In `src/deluge/gui/views/performance_view.cpp`, replace:
```cpp
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newParameterValue, modelStackWithParam,
			                                                          view.modPos, view.modLength);
```
with:
```cpp
			deluge::edits::Context editCtx{*currentSong, currentSong->project};
			deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), newParameterValue,
			                                  static_cast<int32_t>(view.modPos), static_cast<int32_t>(view.modLength),
			                                  modelStackWithParam};
			deluge::edits::apply(editCtx, edit);
```

- [ ] **Step 7: Transform the MIDI sites**

Apply the same shape (the `SetParamValue{addressFor(*ms), newValue, (int32_t)modPos, (int32_t)modLength, ms}` block) at:
- `src/deluge/io/midi/midi_follow.cpp:1075` (variable `modelStackWithParam`, `newValue`, locals `modPos`/`modLength`).
- `src/deluge/model/mod_controllable/mod_controllable_audio.cpp:955` and `:1045` (same variables).
- `src/deluge/model/mod_controllable/mod_controllable_audio.cpp:1115` (variable `newValue` from the 14-bit pitch-bend computation).

For `src/deluge/model/instrument/melodic_instrument.cpp:661`, pass `mayDelete=false`:
```cpp
	if (modelStackWithParam->autoParam) {
		deluge::edits::Context editCtx{*currentSong, currentSong->project};
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), newValue,
		                                  static_cast<int32_t>(modPos), static_cast<int32_t>(modLength),
		                                  modelStackWithParam, /*mayDelete=*/false};
		deluge::edits::apply(editCtx, edit);
	}
```
(Confirm each site's `modPos`/`modLength` types; cast to `int32_t` exactly as the pilot did, preserving the value.)

- [ ] **Step 8: Build + full net**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim -j --target edit_test param_record_diff 2>&1 | grep -iE "error|FAILED" || echo clean
ctest --test-dir build-sim -R '^edit_test$' --output-on-failure
python3 tests/golden/songs_run.py && python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff
./dbt build Debug 2>&1 | tail -3
```
Expected: `edit_test: OK`; songs OK=15; param_record OK=3 (this net **does** exercise MIDI-CC record-playback — the primary guard for this slice); firmware clean.

- [ ] **Step 9: Commit**

```bash
git add src/deluge/model/edits/set_param_value.h src/deluge/model/edits/set_param_value.cpp tests/projection/edit_test.cpp src/deluge/gui/views/performance_view.cpp src/deluge/io/midi/midi_follow.cpp src/deluge/model/mod_controllable/mod_controllable_audio.cpp src/deluge/model/instrument/melodic_instrument.cpp
git commit -m "edits: M1c S2 — perf view + MIDI-CC/pitch-bend funnel through SetParamValue (+ mayDelete funnel field)"
```

---

### Task 3 (S3): Automation-view pad/region edits → `SetParamValue`

The highest-quirk slice: two sites write neighbour nodes to freeze interpolation, and two value-writes are **deliberately called twice** (a documented workaround). Reproduce the call structure verbatim — each engine call becomes one `deluge::edits::apply`, in the same order and count.

**Files:**
- Modify: `src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp` (`:803`, `:1017`, `:1025`, `:1036`, `:1037`, `:1344`, `:1346`) + includes.

**Interfaces:** Consumes M1 + S2's `SetParamValue`. Produces nothing new.

- [ ] **Step 1: Add the Edit includes**

Add the three `model/edits/...` includes to `src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp`.

- [ ] **Step 2: Transform the unselected-pad + knob site (`:803`)**

Replace:
```cpp
		modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, view.modPos,
		                                                          view.modLength);
```
with:
```cpp
		deluge::edits::Context editCtx{*currentSong, currentSong->project};
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), newValue,
		                                  static_cast<int32_t>(view.modPos), static_cast<int32_t>(view.modLength),
		                                  modelStackWithParam};
		deluge::edits::apply(editCtx, edit);
```

- [ ] **Step 3: Transform the neighbour-node freezes (`:1017`, `:1025`) — variable `modelStack`, writes `currentValue`**

Replace each of the two neighbour writes with the `SetParamValue` block, preserving the exact `(currentValue, squareNodeLeftStart/RightStart, kParamNodeWidth)` arguments. For the left node:
```cpp
		deluge::edits::Context leftEditCtx{*currentSong, currentSong->project};
		deluge::edits::SetParamValue leftEdit{deluge::edits::addressFor(*modelStack), currentValue,
		                                      squareNodeLeftStart, kParamNodeWidth, modelStack};
		deluge::edits::apply(leftEditCtx, leftEdit);
```
and the analogous `rightEdit` for `squareNodeRightStart`.

- [ ] **Step 4: Transform the double-called single-pad write (`:1036`, `:1037`)**

Reproduce the two calls verbatim (keep the workaround comment above them). Replace both lines:
```cpp
	deluge::edits::Context padEditCtx{*currentSong, currentSong->project};
	deluge::edits::SetParamValue padEdit1{deluge::edits::addressFor(*modelStack), newValue, squareStart, squareWidth,
	                                      modelStack};
	deluge::edits::apply(padEditCtx, padEdit1);
	deluge::edits::SetParamValue padEdit2{deluge::edits::addressFor(*modelStack), newValue, squareStart, squareWidth,
	                                      modelStack};
	deluge::edits::apply(padEditCtx, padEdit2);
```

- [ ] **Step 5: Transform the double-called in-between fill (`:1344`, `:1346`) — variable `modelStackWithParam`**

Same pattern as Step 4, using `modelStackWithParam` and `(newValue, squareStart, squareWidth)`, two `apply` calls.

- [ ] **Step 6: Build + full net**

Run the Task-2 Step-8 command block. Expected: `edit_test: OK`; songs OK=15; param_record OK=3 (exercises automation recording); firmware clean.

- [ ] **Step 7: Commit**

```bash
git add src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp
git commit -m "edits: M1c S3 — automation-view pad/region edits funnel through SetParamValue"
```

---

### Task 4 (S4): MPE step-editing held-pad edits → `SetParamValue`

Two region sites call `param->setValueForRegion(view.modPos, view.modLength, newValue, ms)` directly. Route them through `SetParamValue` (→ `setValuePossiblyForRegion` → `setValueForRegion`). **Precondition to verify per site:** `view.modLength != 0` and `ms->timelineCounterIsSet()` both hold (they do — the sites are guarded by the step-editing condition), so `setValuePossiblyForRegion` dispatches to `setValueForRegion` identically with the default `NOTE_EDIT` action type. The live-play branch (`note_row.cpp:4503`) is **NOT** touched (spec Decision 5).

**Files:**
- Modify: `src/deluge/gui/views/instrument_clip_view.cpp` (`reportNoteOffForMPEEditing`, `:7923`) + includes (already has `model/model_stack.h`; add the edits includes).
- Modify: `src/deluge/model/note/note_row.cpp` (`recordPolyphonicExpressionEvent`, the **step-editing branch** `:4493` only) + includes.

**Interfaces:** Consumes M1 + S2's `SetParamValue`. Produces nothing new.

- [ ] **Step 1: Verify the dispatch precondition (read-only)**

Confirm both region sites are reached only when `view.modLength != 0` and the stack has a timeline counter:
- `instrument_clip_view.cpp:7923` — inside `reportNoteOffForMPEEditing` (a note-off during held-pad MPE editing; `view.modLength` is the held region).
- `note_row.cpp:4493` — inside the `if (view.modLength && ... == view.activeModControllableModelStack.getTimelineCounterAllowNull())` branch — the condition literally guarantees it.
Record this in the commit message. If either cannot be shown, keep that site's direct `setValueForRegion` call and note it (do not force a mis-routing).

- [ ] **Step 2: Add the Edit includes** to `instrument_clip_view.cpp` and `note_row.cpp`.

- [ ] **Step 3: Transform the `reportNoteOffForMPEEditing` site (`:7923`)**

Replace:
```cpp
		param->setValueForRegion(view.modPos, view.modLength, newValue, modelStackWithAutoParam);
```
with:
```cpp
		deluge::edits::Context editCtx{*currentSong, currentSong->project};
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithAutoParam), newValue,
		                                  static_cast<int32_t>(view.modPos), static_cast<int32_t>(view.modLength),
		                                  modelStackWithAutoParam};
		deluge::edits::apply(editCtx, edit);
```

- [ ] **Step 4: Transform the `recordPolyphonicExpressionEvent` STEP-EDITING branch (`:4493`) only**

Replace:
```cpp
		param->setValueForRegion(view.modPos, view.modLength, newValueBig, modelStackWithAutoParam);
```
with:
```cpp
		deluge::edits::Context editCtx{*currentSong, currentSong->project};
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithAutoParam), newValueBig,
		                                  static_cast<int32_t>(view.modPos), static_cast<int32_t>(view.modLength),
		                                  modelStackWithAutoParam};
		deluge::edits::apply(editCtx, edit);
```
**Leave the `else` branch (`:4503`, `setCurrentValueInResponseToUserInput` with `livePos`/`doMPEMode`) exactly as-is** — it is the recorded-input outlier retained for S6.

- [ ] **Step 5: Build + full net**

Run the Task-2 Step-8 command block. Expected all green. (MPE step-edits are not in the local golden fixtures; behaviour-preservation rests on the verified dispatch equivalence + firmware build + milestone hardware smoke of an MPE held-pad edit.)

- [ ] **Step 6: Commit**

```bash
git add src/deluge/gui/views/instrument_clip_view.cpp src/deluge/model/note/note_row.cpp
git commit -m "edits: M1c S4 — MPE step-editing held-pad edits funnel through SetParamValue (live-play outlier untouched)"
```

---

### Task 5 (S5): Tempo → `SetParamValue`

`Song::setBPM` scalar-writes the tempo param. `SetParamValue{addressFor(*tempoParam), intTempo, 0, 0, tempoParam}` is behaviour-identical: `setValuePossiblyForRegion(intTempo, tempoParam, 0, 0)` → scalar → `setCurrentValueInResponseToUserInput(intTempo, tempoParam, true, -1, true)`. The original passes `shouldLogAction` (dead) and `pos=-1` (== the scalar branch's hardcoded `livePos=-1`). The pre-log calls `recordUnautomatedParamChange`, which early-returns for `UNPATCHED_TEMPO` (tempo has its own `recordTempoChange`) — so, like the interior log today, it produces no consequence. Behaviour-preserving.

**Files:**
- Modify: `src/deluge/model/song/song.cpp` (`Song::setBPM`, `:2900`) + includes.

- [ ] **Step 1: Add the Edit includes** to `src/deluge/model/song/song.cpp`.

- [ ] **Step 2: Transform the tempo write**

`Context` needs a `::Song&`; inside `Song::setBPM`, `*this` is that `::Song`. Replace:
```cpp
	tempoParam->autoParam->setCurrentValueInResponseToUserInput(intTempo, tempoParam, shouldLogAction, pos);
```
with:
```cpp
	deluge::edits::Context editCtx{*this, this->project};
	deluge::edits::SetParamValue edit{deluge::edits::addressFor(*tempoParam), intTempo, 0, 0, tempoParam};
	deluge::edits::apply(editCtx, edit);
```
The `pos` local (was `-1`) is now unused — delete its declaration if the compiler warns. Keep `shouldLogAction`; it is still passed to `setBPMInner(tempoBPM, shouldLogAction)` on the next line.

- [ ] **Step 3: Build + full net**

Run the Task-2 Step-8 command block. Expected: all green. `songs_run.py` round-trips tempo, so this net directly guards the slice.

- [ ] **Step 4: Commit**

```bash
git add src/deluge/model/song/song.cpp
git commit -m "edits: M1c S5 — tempo (Song::setBPM) funnels through SetParamValue"
```

---

### Task 6 (S6): Shrink + close the books on the interior log

Every UI-gesture scalar producer of `auto_param.cpp:318` is now an Edit. Prove the only remaining producer is the recorded-input MPE path, annotate the line as retained-for-recorded-input with the M4 dependency, and leave the pilot pre-log + dedupe permanent. **No deletion.**

**Files:**
- Modify: `tests/projection/edit_test.cpp` (a test asserting the interior log + Edit pre-log still yield exactly one consequence, i.e. dedupe holds with all UI paths now Edits).
- Modify: `src/deluge/modulation/automation/auto_param.cpp` (comment at the interior log site — no code change).

**Interfaces:** none.

- [ ] **Step 1: Add the producer-provenance test**

Append to `tests/projection/edit_test.cpp` a case that re-runs a scalar Edit and asserts the interior log does not double-record (the pre-log + interior-log dedupe still collapses to one consequence — the guarantee that the interior log is now redundant for the Edit path):
```cpp
void testInteriorLogRedundantForEdits(deluge::edits::Context& ctx) {
	constexpr int32_t kParam = 4;
	actionLogger.deleteAllLogs();

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);

	int32_t target = ms->autoParam->getCurrentValue() + 77;
	deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), target, 0, 0, ms};
	deluge::edits::apply(ctx, edit);

	Action* action = actionLogger.firstAction[BEFORE];
	check(action != nullptr, "S6: an Action was opened");
	// Exactly one consequence: the Edit pre-log recorded it; the interior log at auto_param.cpp:318
	// found it via containsConsequenceParamChange and did not double-record. This is the invariant
	// that makes the interior log redundant for every Edit path (only the recorded-input MPE
	// producer still relies on it — see the M4 dependency).
	int32_t consequenceCount = 0;
	for (Consequence* c = action->firstConsequence; c != nullptr; c = c->next) {
		if (c->type == Consequence::PARAM_CHANGE) {
			consequenceCount++;
		}
	}
	check(consequenceCount == 1, "S6: interior log did not double-record (dedupe holds)");
}
```
Add `testInteriorLogRedundantForEdits(ctx);` to `main`.

- [ ] **Step 2: Run the test**

Run: `cmake --build build-sim -j --target edit_test && ctest --test-dir build-sim -R '^edit_test$' --output-on-failure`
Expected: `edit_test: OK`.

- [ ] **Step 3: Annotate the interior log site (documentation, no behaviour change)**

In `src/deluge/modulation/automation/auto_param.cpp`, above the interior log block (the `if (nodesRef.empty() || !isPlaying) { if (value != currentValue) { actionLogger.recordUnautomatedParamChange(modelStack); } }`), replace the existing `// If still unautomated (or not currently playing), record value change` comment with:
```cpp
	// RETAINED FOR RECORDED INPUT (M1c S6). Every UI-gesture producer of this log is now a
	// deluge::edits::SetParamValue Edit whose idempotent pre-log records the consequence; this call
	// then dedupes to a no-op for them (Action::containsConsequenceParamChange). The ONE remaining
	// live producer is drum-MPE recorded input (drum.cpp:123's recording guard is commented out →
	// note_row.cpp:4503 reaches here when stopped). Deleting this line awaits M4's model/runtime
	// split, which moves that live-sounding write off the undo path. See
	// docs/superpowers/specs/2026-07-07-m1c-param-edit-sweep-design.md (Decisions 3 & 5).
	// If still unautomated (or not currently playing), record value change
```

- [ ] **Step 4: Full net + firmware**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim -j --target edit_test param_record_diff 2>&1 | grep -iE "error|FAILED" || echo clean
ctest --test-dir build-sim -R '^edit_test$' --output-on-failure
python3 tests/golden/songs_run.py && python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff
./dbt build Debug 2>&1 | tail -3
```
Expected: `edit_test: OK`; songs OK=15; param_record OK=3; firmware clean.

- [ ] **Step 5: Commit**

```bash
git add tests/projection/edit_test.cpp src/deluge/modulation/automation/auto_param.cpp
git commit -m "edits: M1c S6 — shrink interior log to its recorded-input producer; document M4 deletion dependency"
```

---

## Milestone Close-Out (after S6)

- [ ] **Targeted ARM/qemu golden** on the kernel-relevant fixtures (`build-sim-arm`), per the golden-ARM-runs-targeted norm.
- [ ] **On-device smoke** on the next flash: a menu param edit, a MIDI-CC move on a learned param, an automation-view pad edit, an MPE held-pad edit, a tempo change — and **undo** of each — behave as before.

## Out of Scope (own work later)

- Automation-**delete** Edit (`deleteAutomation` + `getNewAction(AUTOMATION_DELETE)` sites), copy/paste automation Edit.
- **Deleting** `auto_param.cpp:318` — M4 dependency (model/runtime split; the drum-MPE entanglement in spec Decisions 3 & 5).
