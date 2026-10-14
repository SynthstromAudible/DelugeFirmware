# M2a + M2b — Edit Catalog + Note-Add/Remove Structural Pilot — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Open M2 with the Edit catalog (M2a) and the melodic note-add/remove pilot (M2b) — cracking structural id-addressing (`NoteRowAddress`) and proving `AddNote`/`RemoveNote` end-to-end over a real loaded song.

**Architecture:** `NoteRowAddress` is the structural analogue of `ParamAddress` (clip id + `y`). `noteRowAddressFor` builds it from a resolved `ModelStackWithNoteRow`. `AddNote`/`RemoveNote` are `deluge::edits::Edit` subclasses whose `apply` funnels the verified `getNewAction(NOTE_EDIT, ALLOWED)` + engine mutation (`attemptNoteAdd` / `deleteNoteByPos`) — id-addressed, still executing against engine objects (ownership is M3). The primary net is `edit_undo_redo` extended with note cases; the two entangled UI sites are funnelled last, preserved via `getNewAction` coalescing.

**Tech Stack:** C++23 app tree + `deluge-project`; sim `edit_undo_redo` harness (ctest over the corpus); golden nets; firmware `./dbt build Debug`.

Spec: `docs/superpowers/specs/2026-07-08-m2a-m2b-structural-edit-note-pilot-design.md`.

## Global Constraints

- **Behaviour-preserving, golden-gated.** Each task ends green (`export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`): `edit_test`, `edit_undo_redo`, `songs_run.py` OK=15, `param_record` OK=3, `./dbt build Debug` clean.
- **Melodic-only** (`noteRowId = noteRow->y`). `noteRowAddressFor` returns `nullopt` for kit rows (deferred to M2c) and non-instrument clips.
- **Funnel-phase field** `engineStack_` (the `ModelStackWithNoteRow*`) is documented removable at M3/M4, like `SetParamValue::engineStack_`. The address is carried, not yet resolved to a model target (that's M3).
- New files under `src/deluge/model/edits/` and `src/deluge-project/` are GLOBbed — no CMake change. Commit per task. Never add Co-Authored-By trailers.

---

### Task 1 (M2a): Edit catalog + migration-plan M2 ladder

**Files:**
- Create: `docs/dev/edit_catalog.md`
- Modify: `docs/dev/project_model_migration_plan.md` (rewrite the M2 section to the phase ladder)

- [ ] **Step 1: Write the catalog**

Create `docs/dev/edit_catalog.md`: a living table driving the M2 sweep. One row per `ActionType` (grep `enum class ActionType` in `src/deluge/model/action/action.h` for the full 28). Columns: `ActionType(s) | Edit (planned/landed) | Addressed target | Engine call-sites | Phase | Status`. Seed the DONE rows from M1 (`PARAM_UNAUTOMATED_VALUE_CHANGE`→`SetParamValue`; `AUTOMATION_DELETE`→`DeleteParamAutomation`; `AUTOMATION_PASTE`→`PasteParamAutomation`) and the M2b rows (`NOTE_EDIT`→`AddNote`/`RemoveNote`, phase M2b, status: this plan). Mark every other structural type `pending` with its target phase (M2c–M2g per the spec ladder). Include a header paragraph: purpose = the authoritative "what's left" checklist; M2-done = every row an Edit.

- [ ] **Step 2: Rewrite the plan's M2 section to the ladder**

In `docs/dev/project_model_migration_plan.md`, replace the M2 "First slices" (M2a/M2b/M2c) with the full owner-aligned ladder (M2a–M2h) from the spec's "M2 Phase Ladder" table, linking `docs/dev/edit_catalog.md`.

- [ ] **Step 3: Commit**

```bash
git add docs/dev/edit_catalog.md docs/dev/project_model_migration_plan.md
git commit -m "docs: M2a — Edit catalog + M2 phase ladder in the migration plan"
```

---

### Task 2 (M2b core): `NoteRowAddress` + `AddNote`/`RemoveNote` + `edit_undo_redo` proof

**Files:**
- Create: `src/deluge-project/note_address.h`
- Create: `src/deluge/model/edits/note_address_for.h`, `.cpp`
- Create: `src/deluge/model/edits/add_note.h`, `.cpp`
- Create: `src/deluge/model/edits/remove_note.h`, `.cpp`
- Modify: `tests/golden/edit_undo_redo.cpp` (note add/remove/undo/redo cases)

**Interfaces:**
- Produces: `deluge::project::NoteRowAddress`; `std::optional<NoteRowAddress> deluge::edits::noteRowAddressFor(const ModelStackWithNoteRow&)`; `AddNote(std::optional<NoteRowAddress>, int32_t pos, int32_t length, int32_t velocity, int32_t probability, Iterance iterance, int32_t fill, ModelStackWithNoteRow* engineStack)`; `RemoveNote(std::optional<NoteRowAddress>, int32_t pos, ModelStackWithNoteRow* engineStack)`.

- [ ] **Step 1: `NoteRowAddress`**

Create `src/deluge-project/note_address.h`:
```cpp
#pragma once

#include <cstdint>

namespace deluge::project {

/// The durable structural id of a MELODIC note-row (parallel to ParamAddress). Kit rows (a drum-id row-key)
/// are added in M2c.
struct NoteRowAddress {
	uint32_t clip_id{0};        ///< Clip::serialId() when saved, else sessionId().
	bool clip_is_session{false};
	int32_t y{0};               ///< noteRow->y — the stable melodic row id.

	auto operator<=>(const NoteRowAddress&) const = default;
};

} // namespace deluge::project
```

- [ ] **Step 2: `noteRowAddressFor`**

Create `src/deluge/model/edits/note_address_for.h`:
```cpp
#pragma once

#include "deluge-project/note_address.h"
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// The durable address of the melodic note-row a resolved ModelStack names. nullopt for a kit row (M2c) or a
/// clip with no serial/session id (unaddressable). The structural analogue of addressFor().
std::optional<deluge::project::NoteRowAddress> noteRowAddressFor(const ModelStackWithNoteRow& modelStack);

} // namespace deluge::edits
```

Create `src/deluge/model/edits/note_address_for.cpp`:
```cpp
#include "model/edits/note_address_for.h"

#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"

namespace deluge::edits {

std::optional<deluge::project::NoteRowAddress> noteRowAddressFor(const ModelStackWithNoteRow& modelStack) {
	auto* clip = static_cast<InstrumentClip*>(modelStack.getTimelineCounterAllowNull());
	NoteRow* noteRow = modelStack.getNoteRowAllowNull();
	if (clip == nullptr || noteRow == nullptr || clip->output == nullptr) {
		return std::nullopt;
	}
	// Melodic-only: kit note-rows key on the drum id (M2c), not y.
	if (clip->output->type == OutputType::KIT) {
		return std::nullopt;
	}
	deluge::project::NoteRowAddress addr{};
	if (std::optional<uint32_t> serial = clip->serialId()) {
		addr.clip_id = *serial;
		addr.clip_is_session = false;
	}
	else if (uint32_t session = clip->sessionId(); session != 0) {
		addr.clip_id = session;
		addr.clip_is_session = true;
	}
	else {
		return std::nullopt; // unaddressable clip
	}
	addr.y = noteRow->y;
	return addr;
}

} // namespace deluge::edits
```
(Confirm `getTimelineCounterAllowNull`/`getNoteRowAllowNull` are the const accessors on `ModelStackWithNoteRow` — they are the ones `addressFor` uses.)

- [ ] **Step 3: `AddNote`**

Create `src/deluge/model/edits/add_note.h`:
```cpp
#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include "model/iterance/iterance.h" // Iterance
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// Create a note in a melodic note-row — reifies the pad-tap note-add. apply funnels
/// getNewAction(NOTE_EDIT)+NoteRow::attemptNoteAdd (which records the note-array undo into the Action).
class AddNote final : public Edit {
public:
	AddNote(std::optional<project::NoteRowAddress> address, int32_t pos, int32_t length, int32_t velocity,
	        int32_t probability, Iterance iterance, int32_t fill, ModelStackWithNoteRow* engineStack)
	    : address_(address), pos_(pos), length_(length), velocity_(velocity), probability_(probability),
	      iterance_(iterance), fill_(fill), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t pos_, length_, velocity_, probability_;
	Iterance iterance_;
	int32_t fill_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
```

Create `src/deluge/model/edits/add_note.cpp`:
```cpp
#include "model/edits/add_note.h"

#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"

namespace deluge::edits {

Error AddNote::apply(Context& /*ctx*/) {
	NoteRow* noteRow = engineStack_->getNoteRow();
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	// attemptNoteAdd records the note-array undo Consequence into `action` itself.
	noteRow->attemptNoteAdd(pos_, length_, velocity_, probability_, iterance_, fill_, engineStack_, action);
	return Error::NONE;
}

} // namespace deluge::edits
```

- [ ] **Step 4: `RemoveNote`**

Create `src/deluge/model/edits/remove_note.h` (same shape, fields `{address, pos, engineStack}`) and `remove_note.cpp`:
```cpp
#include "model/edits/remove_note.h"

#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"

namespace deluge::edits {

Error RemoveNote::apply(Context& /*ctx*/) {
	NoteRow* noteRow = engineStack_->getNoteRow();
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	noteRow->deleteNoteByPos(engineStack_, pos_, action); // finds the note at pos; records the note-array undo
	return Error::NONE;
}

} // namespace deluge::edits
```
(`remove_note.h` mirrors `add_note.h`: ctor `RemoveNote(std::optional<project::NoteRowAddress> address, int32_t pos, ModelStackWithNoteRow* engineStack)`.)

- [ ] **Step 5: Extend `edit_undo_redo` with note add/remove cases**

In `tests/golden/edit_undo_redo.cpp`, add a note target. Includes: `model/clip/instrument_clip.h` (already), `model/note/note_row.h`, `model/note/note.h`, `model/edits/note_address_for.h`, `model/edits/add_note.h`, `model/edits/remove_note.h`. Add helpers + a target driven from `main` (after the instrument-clip target):

```cpp
// Count / presence of a note at pos in a note-row.
bool noteAtPos(NoteRow* noteRow, int32_t pos) {
	int32_t i = noteRow->notes.firstAtOrAfter(pos);
	return i < std::ssize(noteRow->notes) && noteRow->notes[i].pos == pos;
}

// M2b: find the first MELODIC InstrumentClip + a note-row, and drive AddNote/RemoveNote undo/redo over it.
void noteTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips); c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type == ClipType::INSTRUMENT && cl->output != nullptr && cl->output->type != OutputType::KIT
		    && cl->output->type != OutputType::AUDIO) {
			clip = (InstrumentClip*)cl;
			break;
		}
	}
	if (clip == nullptr || std::ssize(clip->noteRows) == 0) {
		printf("# no-melodic-noterow\n");
		return;
	}
	printf("# target note-add-remove\n");

	char mem[MODEL_STACK_MAX_SIZE];
	NoteRow* noteRow = &clip->noteRows[0]; // NoteRowVector is an OrderedPosVector<NoteRow>
	int32_t noteRowId = clip->getNoteRowId(noteRow, 0);
	ModelStackWithNoteRow* ms =
	    setupModelStackWithTimelineCounter(mem, song, clip)->addNoteRow(noteRowId, noteRow);

	check(deluge::edits::noteRowAddressFor(*ms).has_value(), "note: noteRowAddressFor resolves");

	// Pick a pos with no note (search a few candidates within the loop).
	int32_t effLen = ms->getLoopLength();
	int32_t freePos = -1;
	for (int32_t p = 0; p < effLen; p += 6) {
		if (!noteAtPos(noteRow, p)) {
			freePos = p;
			break;
		}
	}
	if (freePos < 0) {
		printf("# note-row-full\n");
		return;
	}

	deluge::edits::Context ctx{*currentSong, currentSong->project};
	// ADD -> undo -> redo
	deluge::edits::AddNote add{deluge::edits::noteRowAddressFor(*ms), freePos, /*length=*/1, /*velocity=*/64,
	                           /*probability=*/20, Iterance{} /*no iterance*/, /*fill=*/0, ms};
	check(deluge::edits::apply(ctx, add) == Error::NONE, "note-add: apply succeeds");
	check(noteAtPos(noteRow, freePos), "note-add: note present after add");
	actionLogger.revert(BEFORE, false, false);
	check(!noteAtPos(noteRow, freePos), "note-add: undo removes the note");
	actionLogger.revert(AFTER, false, false);
	check(noteAtPos(noteRow, freePos), "note-add: redo restores the note");

	// REMOVE the just-added note -> undo -> redo
	if (actionLogger.firstAction[BEFORE] != nullptr) {
		actionLogger.firstAction[BEFORE]->openForAdditions = false; // don't coalesce the remove into the add
	}
	deluge::edits::RemoveNote rem{deluge::edits::noteRowAddressFor(*ms), freePos, ms};
	check(deluge::edits::apply(ctx, rem) == Error::NONE, "note-remove: apply succeeds");
	check(!noteAtPos(noteRow, freePos), "note-remove: note absent after remove");
	actionLogger.revert(BEFORE, false, false);
	check(noteAtPos(noteRow, freePos), "note-remove: undo restores the note");
}
```
Call `noteTarget(song);` from `main` before `std::free(xml)`. `Iterance` is `model/iterance/iterance.h`; `Iterance{}` is the default no-iterance value. `clip->noteRows` is a `NoteRowVector` (`OrderedPosVector<NoteRow>`) — use `[i]` + `std::ssize`. `ModelStackWithNoteRow::getTimelineCounterAllowNull`/`getNoteRowAllowNull` are the const getters `noteRowAddressFor` uses.

- [ ] **Step 6: Build + run + full net**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim -j --target edit_undo_redo 2>&1 | grep -iE "error:|FAILED" || echo clean
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo | tail -3
# spot-check the note target is exercised on a melodic song:
build-sim/edit_undo_redo "$DELUGE_SONG_CORPUS/SONG1.XML" | grep -E "note-add-remove|no-melodic"
ctest --test-dir build-sim -R 'edit_test|edit_undo_redo' --output-on-failure | tail -4
python3 tests/golden/songs_run.py | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff | tail -1
./dbt build Debug 2>&1 | tail -2
```
Expected: `edit_undo_redo` OK over the corpus; `# target note-add-remove` on ≥1 song; ctests pass; songs OK=15; param_record OK=3; firmware clean.

- [ ] **Step 7: Commit**

```bash
git add src/deluge-project/note_address.h src/deluge/model/edits/note_address_for.h src/deluge/model/edits/note_address_for.cpp src/deluge/model/edits/add_note.h src/deluge/model/edits/add_note.cpp src/deluge/model/edits/remove_note.h src/deluge/model/edits/remove_note.cpp tests/golden/edit_undo_redo.cpp
git commit -m "edits: M2b core — NoteRowAddress + AddNote/RemoveNote (melodic) with loaded-song undo/redo coverage"
```

---

### Task 3 (M2b funnel): swap the two primary pad-tap sites

The add (`instrument_clip_view.cpp:4620`, in the edit-pad-press loop) and remove (`:4455`, `scrollVertical_grabNotesPressed`) sites each sit inside a multi-press loop with a shared, pre-opened `NOTE_EDIT` `action`. Replace only the single engine-mutation call with the Edit; the Edit's `apply` re-obtains the action via `getNewAction(NOTE_EDIT, ALLOWED)`, which **coalesces** into the site's already-open same-type action (same UI, same tick) — behaviour-identical. Leave the surrounding loop / MPE / `action` usage untouched.

**Files:**
- Modify: `src/deluge/gui/views/instrument_clip_view.cpp` (2 sites + Edit includes)

- [ ] **Step 1: Add Edit includes** (`model/edits/edit.h`, `param_address_for.h` may already be present via other headers — add `model/edits/add_note.h`, `model/edits/remove_note.h`, `model/edits/note_address_for.h`).

- [ ] **Step 2: Funnel the add site (`:4620`)**

Replace:
```cpp
			bool success =
			    noteRow->attemptNoteAdd(pos, editPadPresses[i].intendedLength, editPadPresses[i].intendedVelocity,
			                            editPadPresses[i].intendedProbability, editPadPresses[i].intendedIterance,
			                            editPadPresses[i].intendedFill, modelStackWithNoteRow, action);
```
with an `AddNote` funnel that preserves `success` (the return value gates `deleteOnScroll` below). Since `apply` returns `Error` not the add's `int32_t`, capture success by re-checking presence, OR (simpler, behaviour-exact) keep `attemptNoteAdd`'s return by having the Edit expose it. **Decision:** for behaviour-exactness, funnel via the Edit but read `success` from the note-row after:
```cpp
			deluge::edits::Context noteCtx{*currentSong, currentSong->project};
			deluge::edits::AddNote addEdit{deluge::edits::noteRowAddressFor(*modelStackWithNoteRow), pos,
			                               editPadPresses[i].intendedLength, editPadPresses[i].intendedVelocity,
			                               editPadPresses[i].intendedProbability, editPadPresses[i].intendedIterance,
			                               editPadPresses[i].intendedFill, modelStackWithNoteRow};
			deluge::edits::apply(noteCtx, addEdit);
			bool success = noteRowHasNoteAtPos(noteRow, pos); // helper: notes.firstAtOrAfter(pos) matches pos
```
Add a small file-local `noteRowHasNoteAtPos` helper (or inline the `firstAtOrAfter` check). **Verify** this `success` equals `attemptNoteAdd`'s old return in the add-failed (overlap) case — `attemptNoteAdd` returns 0 and adds nothing, so presence-check is false, matching. If any edge diverges, keep the direct call for the add and funnel only the remove in this task, noting it.

- [ ] **Step 3: Funnel the remove site (`:4455`)**

Replace:
```cpp
					thisNoteRow->deleteNoteByPos(modelStackWithNoteRow, pos, action);
```
with:
```cpp
					deluge::edits::Context noteCtx{*currentSong, currentSong->project};
					deluge::edits::RemoveNote remEdit{deluge::edits::noteRowAddressFor(*modelStackWithNoteRow), pos,
					                                  modelStackWithNoteRow};
					deluge::edits::apply(noteCtx, remEdit);
```

- [ ] **Step 4: Full net + firmware + ARM**

Run the Task-2 Step-6 command block, plus:
```bash
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff | tail -1
cmake --build build-sim-arm -j --target edit_undo_redo 2>&1 | grep -iE "error:|FAILED" || echo "arm clean"
python3 tests/golden/edit_undo_redo_run.py --binary build-sim-arm/edit_undo_redo \
    --wrapper 'qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9' --filter SONG1 | tail -1
```
Expected: all green; ARM SONG1 OK. `songs_run.py` (note serialization) is the guard that the funnelled pad-tap paths still produce identical note data.

- [ ] **Step 5: Commit**

```bash
git add src/deluge/gui/views/instrument_clip_view.cpp
git commit -m "edits: M2b — funnel the primary pad-tap note add/remove sites through AddNote/RemoveNote"
```

- [ ] **Step 6: Update the catalog + memory**

Mark `NOTE_EDIT` → `AddNote`/`RemoveNote` **landed (melodic)** in `docs/dev/edit_catalog.md`; note kit rows + the other note types remain (M2c). Update the [[m1-edit-substrate]]/M2 memory: M2a+M2b done; structural addressing proven; on-device smoke (pad-tap add/remove + undo, melodic) owed.

```bash
git add docs/dev/edit_catalog.md && git commit -m "docs: catalog — NOTE_EDIT add/remove landed (melodic, M2b)"
```

## Milestone Close-Out

- [ ] On-device smoke rides the next flash: pad-tap add a note + undo/redo; pad-tap remove + undo/redo, on a melodic clip. Kit rows and the rest of the note/structural surface are M2c onward (per the catalog).

## Out of Scope

Kit note-rows (drum-id row-key), the other `NOTE_EDIT`/`NOTEROW_*` sites, all other structural ActionTypes (M2c–M2g); M2h Action view-state shed; resolving `NoteRowAddress` to a model target (M3).
