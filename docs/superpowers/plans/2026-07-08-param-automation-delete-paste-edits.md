# Param-Automation Delete/Paste Edits — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reify the two non-value param-automation mutations M1c excluded — automation-delete and automation-paste — as `deluge::edits::Edit` subclasses, funnelling their call sites through `deluge::edits::apply`, behaviour-preservingly.

**Architecture:** Two new Edit types (`DeleteParamAutomation`, `PasteParamAutomation`) each fold a call site's inline `getNewAction`+mutation into their `apply`, exactly as `SetParamValue` did for value writes. `deleteAutomation`/`paste` engine kernels are unchanged; the Edits own the `Action` creation + delegation. Copy is not reified (read-only). Same proven recipe: `addressFor(*ms)` + construct + `apply`.

**Tech Stack:** C++23 app tree; sim `edit_test` (ctest); golden nets (`songs_run.py`, `param_record_run.py`, `edit_undo_redo`) over the corpus; firmware `./dbt build Debug`.

Spec: `docs/superpowers/specs/2026-07-08-param-automation-delete-paste-edits-design.md`.

## Global Constraints

- **Behaviour-preserving, golden-gated.** Each task ends green: `edit_test` (ctest), `songs_run.py` OK=15, `param_record` OK=3, `edit_undo_redo` OK (all with `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`), `./dbt build Debug` clean.
- **`DeleteParamAutomation` carries the site's `ActionAddition`** (funnel field, default `NOT_ALLOWED`; the tempo site `song.cpp:2971` passes `ALLOWED`). Preserve each site's exact value.
- **`isPatchCable` is computed at the call site and passed in** — never recomputed inside `apply`.
- **Funnel-phase fields** (`engineStack_`, paste's `copied_`/`isPatchCable_`, delete's `addition_`) are engine/transient, documented removable at the M3/M4 flip — like `SetParamValue::engineStack_`.
- **Headless `edit_test` crash-safety:** these unit tests open real Actions (the harness song has 1 clip, so `getNewAction` succeeds). Reset the ActionLogger with a **leak-reset** — `actionLogger.firstAction[BEFORE] = nullptr; actionLogger.firstAction[AFTER] = nullptr;` — NOT `deleteAllLogs` (freeing a consequence pumps the uninitialised audio engine). Leaked Actions are fine at process exit.
- New files under `src/deluge/model/edits/` are GLOBbed into `deluge_app` — no CMake change. Commit per task. Never add Co-Authored-By trailers.

---

### Task 1: `DeleteParamAutomation` Edit

**Files:**
- Create: `src/deluge/model/edits/delete_param_automation.h`
- Create: `src/deluge/model/edits/delete_param_automation.cpp`
- Modify: `tests/projection/edit_test.cpp` (delete unit case)
- Modify: `src/deluge/gui/menu_item/automation/automation.cpp`, `src/deluge/gui/views/view.cpp`, `src/deluge/gui/views/automation_view.cpp` (×2), `src/deluge/model/song/song.cpp` (+ Edit includes each)

**Interfaces:**
- Consumes: `deluge::edits::{Edit,Context,apply,addressFor}`, `ActionLogger::getNewAction`, `AutoParam::deleteAutomation`.
- Produces: `DeleteParamAutomation(std::optional<project::ParamAddress> address, ModelStackWithAutoParam* engineStack, ActionAddition addition = ActionAddition::NOT_ALLOWED)`.

- [ ] **Step 1: Confirm baseline green**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim -j --target edit_test && ctest --test-dir build-sim -R '^edit_test$' --output-on-failure
```
Expected: `edit_test: OK`.

- [ ] **Step 2: Write the Edit header**

Create `src/deluge/model/edits/delete_param_automation.h`:
```cpp
#pragma once

#include "deluge-project/param_address.h"
#include "model/action/action.h" // ActionAddition
#include "model/edits/edit.h"
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::edits {

/// Clear a param's automation — the reification of the AUTOMATION_DELETE call sites. apply() opens the
/// AUTOMATION_DELETE Action and delegates to AutoParam::deleteAutomation, which snapshots the stolen
/// before-nodes into that Action as the undo Consequence.
class DeleteParamAutomation final : public Edit {
public:
	DeleteParamAutomation(std::optional<project::ParamAddress> address, ModelStackWithAutoParam* engineStack,
	                      ActionAddition addition = ActionAddition::NOT_ALLOWED)
	    : address_(address), engineStack_(engineStack), addition_(addition) {}

	Error apply(Context& ctx) override;

private:
	/// Durable lane id (nullopt = never-saved owner, engine-resolved); unused by the funnel body.
	std::optional<project::ParamAddress> address_;
	/// Funnel-phase borrow, like SetParamValue::engineStack_. Retired at the M3/M4 flip.
	ModelStackWithAutoParam* engineStack_;
	/// Funnel-phase: the site's Action-coalescing policy (tempo passes ALLOWED; others NOT_ALLOWED).
	ActionAddition addition_;
};

} // namespace deluge::edits
```

- [ ] **Step 3: Write the Edit body**

Create `src/deluge/model/edits/delete_param_automation.cpp`:
```cpp
#include "model/edits/delete_param_automation.h"

#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error DeleteParamAutomation::apply(Context& /*ctx*/) {
	// Funnel the site's getNewAction(AUTOMATION_DELETE)+deleteAutomation pair. deleteAutomation records the
	// undo Consequence into the Action (recordParamChangeIfNotAlreadySnapshotted, stealData=true).
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, addition_);
	engineStack_->autoParam->deleteAutomation(action, engineStack_);
	return Error::NONE;
}

} // namespace deluge::edits
```

- [ ] **Step 4: Write the failing unit test**

Append to `tests/projection/edit_test.cpp` (add `#include "deluge-project/param_edit.h"` — for `deluge::project::setNodeAtPos` — and `#include "model/edits/delete_param_automation.h"` at the top; call `testDeleteParamAutomation(ctx);` from `main` after the existing cases):
```cpp
void testDeleteParamAutomation(deluge::edits::Context& ctx) {
	constexpr int32_t kParam = 5;
	// Leak-reset (crash-safe: no consequence freeing -> no headless audio pump).
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);

	// Seed automation into the owner store (song-less stack -> collection fallback store).
	deluge::project::setNodeAtPos(*ms->doc(), 0, 100, false);
	deluge::project::setNodeAtPos(*ms->doc(), 32, -200, true);
	check(!ms->doc()->nodes.empty(), "delete: seeded automation");

	deluge::edits::DeleteParamAutomation edit{deluge::edits::addressFor(*ms), ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "delete: apply succeeds");
	check(ms->doc()->nodes.empty(), "delete: automation cleared");

	Action* a = actionLogger.firstAction[BEFORE];
	check(a != nullptr && a->type == ActionType::AUTOMATION_DELETE, "delete: AUTOMATION_DELETE Action opened");
	check(a != nullptr && a->firstConsequence != nullptr, "delete: undo Consequence recorded (stolen nodes)");
}
```

- [ ] **Step 5: Build + run the test**

Run: `cmake --build build-sim -j --target edit_test && ctest --test-dir build-sim -R '^edit_test$' --output-on-failure`
Expected: `edit_test: OK` (delete case passes: nodes cleared, `AUTOMATION_DELETE` Action + Consequence present). `deluge::project::setNodeAtPos(DocAutoParam&, int32_t pos, int32_t value, bool interpolate)` is declared in `deluge-project/param_edit.h`.

- [ ] **Step 6: Funnel the 5 delete sites**

Add the three Edit includes (`model/edits/edit.h`, `model/edits/param_address_for.h`, `model/edits/delete_param_automation.h`) to each of: `automation.cpp`, `view.cpp`, `automation_view.cpp`, `song.cpp`. Then replace each site.

`gui/menu_item/automation/automation.cpp` (variable `modelStack`):
```cpp
			deluge::edits::Context editCtx{*currentSong, currentSong->project};
			deluge::edits::DeleteParamAutomation edit{deluge::edits::addressFor(*modelStack), modelStack};
			deluge::edits::apply(editCtx, edit);
```
(replacing the `Action* action = getNewAction(...); modelStack->autoParam->deleteAutomation(action, modelStack);` pair; keep the surrounding `displayPopup`.)

`gui/views/view.cpp` `modEncoderButtonAction_deleteAutomation` (variable `modelStackWithParam`): replace the `getNewAction`+`deleteAutomation` pair with the same block using `modelStackWithParam`.

`gui/views/automation_view.cpp` `:1589` and `:2557` (variable `modelStackWithParam`): same block. (Both use the default `ActionAddition` — omit the 3rd arg.)

`model/song/song.cpp` `clearTempoAutomation` (variable `tempoParam`) — **pass `ALLOWED`**:
```cpp
	deluge::edits::Context editCtx{*this, this->project};
	deluge::edits::DeleteParamAutomation edit{deluge::edits::addressFor(*tempoParam), tempoParam,
	                                          ActionAddition::ALLOWED};
	deluge::edits::apply(editCtx, edit);
```
(The `int32_t pos = -1;` local becomes unused — delete it if the compiler warns.)

- [ ] **Step 7: Full net + firmware**

Run:
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim -j --target edit_test param_record_diff edit_undo_redo 2>&1 | grep -iE "error|FAILED" || echo clean
ctest --test-dir build-sim -R 'edit_test|edit_undo_redo' --output-on-failure
python3 tests/golden/songs_run.py && python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff
./dbt build Debug 2>&1 | tail -2
```
Expected: ctests pass; songs OK=15; param_record OK=3; firmware clean.

- [ ] **Step 8: Commit**

```bash
git add src/deluge/model/edits/delete_param_automation.h src/deluge/model/edits/delete_param_automation.cpp tests/projection/edit_test.cpp src/deluge/gui/menu_item/automation/automation.cpp src/deluge/gui/views/view.cpp src/deluge/gui/views/automation_view.cpp src/deluge/model/song/song.cpp
git commit -m "edits: DeleteParamAutomation Edit funnels the 5 automation-delete sites"
```

---

### Task 2: `PasteParamAutomation` Edit

**Files:**
- Create: `src/deluge/model/edits/paste_param_automation.h`
- Create: `src/deluge/model/edits/paste_param_automation.cpp`
- Modify: `tests/projection/edit_test.cpp` (paste unit case)
- Modify: `src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp` (`pasteAutomation`, + Edit includes)

**Interfaces:**
- Consumes: `deluge::edits::{...}`, `ActionLogger::getNewAction`, `Action::recordParamChangeIfNotAlreadySnapshotted`, `AutoParam::paste`, `CopiedParamAutomation`.
- Produces: `PasteParamAutomation(std::optional<project::ParamAddress> address, int32_t startPos, int32_t endPos, float scaleFactor, CopiedParamAutomation* copied, bool isPatchCable, ModelStackWithAutoParam* engineStack)`.

- [ ] **Step 1: Write the Edit header**

Create `src/deluge/model/edits/paste_param_automation.h`:
```cpp
#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithAutoParam;
class CopiedParamAutomation;

namespace deluge::edits {

/// Paste clipboard automation into a param region — the reification of pasteAutomation's mutation. apply()
/// opens the AUTOMATION_PASTE Action, pre-snapshots the before-state, then delegates to AutoParam::paste.
class PasteParamAutomation final : public Edit {
public:
	PasteParamAutomation(std::optional<project::ParamAddress> address, int32_t startPos, int32_t endPos,
	                     float scaleFactor, CopiedParamAutomation* copied, bool isPatchCable,
	                     ModelStackWithAutoParam* engineStack)
	    : address_(address), startPos_(startPos), endPos_(endPos), scaleFactor_(scaleFactor), copied_(copied),
	      isPatchCable_(isPatchCable), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::ParamAddress> address_;
	int32_t startPos_;
	int32_t endPos_;
	float scaleFactor_;
	CopiedParamAutomation* copied_; ///< Funnel-phase clipboard borrow.
	bool isPatchCable_;             ///< Computed at the call site; carried verbatim.
	ModelStackWithAutoParam* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
```

- [ ] **Step 2: Write the Edit body**

Create `src/deluge/model/edits/paste_param_automation.cpp`:
```cpp
#include "model/edits/paste_param_automation.h"

#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error PasteParamAutomation::apply(Context& /*ctx*/) {
	// Funnel pasteAutomation's exact sequence: open the Action, pre-snapshot the before-state (copy, not
	// steal), then delegate to AutoParam::paste.
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_PASTE);
	if (action != nullptr) {
		action->recordParamChangeIfNotAlreadySnapshotted(engineStack_, /*stealData=*/false);
	}
	engineStack_->autoParam->paste(startPos_, endPos_, scaleFactor_, engineStack_, copied_, isPatchCable_);
	return Error::NONE;
}

} // namespace deluge::edits
```

- [ ] **Step 3: Write the failing unit test**

Append to `tests/projection/edit_test.cpp` (add `#include "model/edits/paste_param_automation.h"`, `#include "modulation/automation/copied_param_automation.h"`, `#include "model/note/note_row.h"`, `#include "modulation/params/param_node.h"` if not present; call `testPasteParamAutomation(ctx);` from `main`):
```cpp
void testPasteParamAutomation(deluge::edits::Context& ctx) {
	constexpr int32_t kParam = 6;
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);
	// paste needs getLoopLength(); a no-timeline stack gets it from the noteRow's independent loop length.
	// song stays null (makeStack memset) so addressFor short-circuits to nullopt -> collection store.
	NoteRow nr;
	nr.loopLengthIfIndependent = 128;
	ms->setNoteRow(&nr);

	// Clipboard: two nodes on their own timeline; the first MUST be at pos 0.
	ParamNode copiedNodes[2];
	copiedNodes[0].pos = 0;
	copiedNodes[0].value = 500000000;
	copiedNodes[0].interpolated = false;
	copiedNodes[1].pos = 64;
	copiedNodes[1].value = -500000000;
	copiedNodes[1].interpolated = true;
	CopiedParamAutomation copied;
	copied.width = 128;
	copied.nodes = copiedNodes;
	copied.numNodes = 2;

	deluge::edits::PasteParamAutomation edit{deluge::edits::addressFor(*ms), 0, 128, 1.0f, &copied,
	                                         /*isPatchCable=*/false, ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "paste: apply succeeds");
	check(!ms->doc()->nodes.empty(), "paste: nodes written");

	Action* a = actionLogger.firstAction[BEFORE];
	check(a != nullptr && a->type == ActionType::AUTOMATION_PASTE, "paste: AUTOMATION_PASTE Action opened");
}
```
(`ParamNode::interpolated` is a public bool field; `pos`/`value` are public too — the same fields `param_automation_test` reads.)

- [ ] **Step 4: Build + run the test**

Run: `cmake --build build-sim -j --target edit_test && ctest --test-dir build-sim -R '^edit_test$' --output-on-failure`
Expected: `edit_test: OK` (paste case: nodes written, `AUTOMATION_PASTE` Action opened).

- [ ] **Step 5: Funnel the paste site**

In `src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp` (`pasteAutomation`), add the three Edit includes. Replace the `getNewAction`+`recordParamChangeIfNotAlreadySnapshotted`+`paste` block (currently `:884-897`) with the Edit, keeping `isPatchCable` computed where it is today:
```cpp
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		bool isPatchCable = (modelStackWithParam->paramCollection
		                     == modelStackWithParam->paramManager->getPatchCableSetAllowJibberish());
		deluge::edits::Context editCtx{*currentSong, currentSong->project};
		deluge::edits::PasteParamAutomation edit{deluge::edits::addressFor(*modelStackWithParam), startPos, endPos,
		                                         scaleFactor, getCopiedParamAutomation(), isPatchCable,
		                                         modelStackWithParam};
		deluge::edits::apply(editCtx, edit);

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_PASTED));
		// ... keep the existing post-paste refresh / playback-active block below unchanged ...
```
(Move the `isPatchCable` computation ABOVE the Edit construction — it was between `getNewAction` and `paste`; now it feeds the Edit's ctor. The surrounding popup + `if (playbackHandler.isEitherClockActive())` refresh stay.)

- [ ] **Step 6: Full net + firmware + ARM**

Run:
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim -j --target edit_test param_record_diff edit_undo_redo 2>&1 | grep -iE "error|FAILED" || echo clean
ctest --test-dir build-sim -R 'edit_test|edit_undo_redo' --output-on-failure
python3 tests/golden/songs_run.py && python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff
./dbt build Debug 2>&1 | tail -2
cmake --build build-sim-arm -j --target edit_test 2>&1 | grep -iE "error|FAILED" || echo "arm clean"
ctest --test-dir build-sim-arm -R '^edit_test$' --output-on-failure 2>&1 | tail -3
```
Expected: ctests pass; songs OK=15; param_record OK=3; firmware clean; ARM edit_test passes.

- [ ] **Step 7: Commit**

```bash
git add src/deluge/model/edits/paste_param_automation.h src/deluge/model/edits/paste_param_automation.cpp tests/projection/edit_test.cpp src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp
git commit -m "edits: PasteParamAutomation Edit funnels the automation-paste site"
```

---

## Milestone Close-Out

- [ ] Param-mutation Edit family complete (value writes M1c + delete/paste). Update the [[m1-edit-substrate]] memory. On-device smoke (delete-automation + undo; copy/paste-automation + undo) rides the next flash. Structural mutations are M2.

## Out of Scope

- Copy (read-only, not an Edit). Structural (note/clip) mutations (M2). Deleting the M4-gated interior log.
