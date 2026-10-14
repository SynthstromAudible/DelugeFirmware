# PasteNoteRow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Reify the per-row paste mutation as `deluge::edits::PasteNoteRow`, funnel the two `pasteNotes` loop sites, and prove it via `edit_undo_redo` — closing `NOTES_PASTE`/`PATTERN_PASTE` and thereby M2d.

**Architecture:** A per-row Edit (funnel-phase borrow) mirroring `NudgeNote`/`AddNote`: `apply()` calls `noteRow->paste(...)` (self-recording into the caller's shared Action) and maps its `bool` to `Error`. The view keeps the loop, copy buffer, target-row resolution, Action, `clearArea`, and display.

**Tech Stack:** C++23, CMake `GLOB_RECURSE` (new edit files auto-picked-up), sim `edit_undo_redo` harness, golden `songs`/`param_record`, `dbt build Debug`.

## Global Constraints

- Behaviour-preserving; goto-`ramError` flow preserved via the `Error` return.
- New C++ idiomatic modern C++23 (`std::optional`, no C-casts) matching sibling edits.
- Commit at the end (branch `deluge-project-param`); after a clang-format pre-commit failure, re-run a **fresh** `git commit` (never `--amend`).
- Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.

---

### Task 1: Create the `PasteNoteRow` Edit

**Files:**
- Create: `src/deluge/model/edits/paste_note_row.h`
- Create: `src/deluge/model/edits/paste_note_row.cpp`

**Interfaces:**
- Consumes: `deluge::edits::Edit`/`Context`, `deluge::project::NoteRowAddress`, `ModelStackWithNoteRow`, `CopiedNoteRow`, `NoteRow::paste`.
- Produces: `class deluge::edits::PasteNoteRow` with ctor `(std::optional<project::NoteRowAddress>, CopiedNoteRow*, float scaleFactor, int32_t screenEndPos, Action*, ModelStackWithNoteRow*)` and `Error apply(Context&)`.

- [ ] **Step 1: Write the header** — `paste_note_row.h`:

```cpp
#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;
class CopiedNoteRow;
class Action;

namespace deluge::edits {

/// Paste one copied note-row into one target note-row (one iteration of InstrumentClipView::pasteNotes' kit/melodic
/// loop). apply() calls NoteRow::paste, which self-records the undo into the shared Action; the view keeps the loop,
/// copy buffer, target-row resolution, Action creation, clearArea, and display. The Error return preserves the site's
/// `goto ramError` branch (NoteRow::paste currently always succeeds; the real RAM failure — getOrCreateNoteRowForYNote
/// null — stays in the view).
class PasteNoteRow final : public Edit {
public:
	PasteNoteRow(std::optional<project::NoteRowAddress> address, CopiedNoteRow* copiedRow, float scaleFactor,
	             int32_t screenEndPos, Action* action, ModelStackWithNoteRow* engineStack)
	    : address_(address), copiedRow_(copiedRow), scaleFactor_(scaleFactor), screenEndPos_(screenEndPos),
	      action_(action), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	CopiedNoteRow* copiedRow_;           ///< Funnel-phase copy-buffer borrow.
	float scaleFactor_;
	int32_t screenEndPos_;
	Action* action_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
```

- [ ] **Step 2: Write the source** — `paste_note_row.cpp`:

```cpp
#include "model/edits/paste_note_row.h"

#include "model/model_stack.h"
#include "model/note/note_row.h"

namespace deluge::edits {

Error PasteNoteRow::apply(Context& /*ctx*/) {
	bool ok = engineStack_->getNoteRow()->paste(engineStack_, copiedRow_, scaleFactor_, screenEndPos_, action_);
	return ok ? Error::NONE : Error::INSUFFICIENT_RAM;
}

} // namespace deluge::edits
```

- [ ] **Step 3: Build to verify it compiles**

Run: `cmake --build build-sim --target edit_undo_redo 2>&1 | grep -iE "error:" | head`
Expected: no output (compiles; GLOB picks up the new files).

---

### Task 2: Funnel the two `pasteNotes` loop sites

**Files:**
- Modify: `src/deluge/gui/views/instrument_clip_view.cpp` (kit paste ~1462, melodic paste ~1495; include block ~line 63–72)

**Interfaces:**
- Consumes: `deluge::edits::PasteNoteRow` (Task 1), `deluge::edits::noteRowAddressFor`, `deluge::edits::apply`, `deluge::edits::Context`.

- [ ] **Step 1: Add the include** (alongside the other `model/edits/*` includes, alphabetical):

```cpp
#include "model/edits/paste_note_row.h"
```

- [ ] **Step 2: Funnel the KIT site.** Replace:

```cpp
			bool success = thisNoteRow->paste(modelStackWithNoteRow, thisCopiedNoteRow, scaleFactor, endPos, action);
			if (!success) {
				goto ramError;
			}
```
with:
```cpp
			deluge::edits::Context pasteCtx{*currentSong, currentSong->project};
			deluge::edits::PasteNoteRow pasteEdit{deluge::edits::noteRowAddressFor(*modelStackWithNoteRow),
			                                      thisCopiedNoteRow, scaleFactor, endPos, action, modelStackWithNoteRow};
			if (deluge::edits::apply(pasteCtx, pasteEdit) != Error::NONE) {
				goto ramError;
			}
```

- [ ] **Step 3: Funnel the MELODIC site.** Replace the identical `bool success = thisNoteRow->paste(...); if (!success) { goto ramError; }` block (the one after `getOrCreateNoteRowForYNote`) with the same PasteNoteRow snippet as Step 2. Leave the preceding `getOrCreateNoteRowForYNote` null-check + `goto ramError` untouched.

- [ ] **Step 4: Build**

Run: `cmake --build build-sim --target edit_undo_redo 2>&1 | grep -iE "error:" | head`
Expected: no output.

---

### Task 3: Prove it with an `edit_undo_redo` `pasteTarget`

**Files:**
- Modify: `tests/golden/edit_undo_redo.cpp` (add include, add `pasteTarget`, call it in `main`)

- [ ] **Step 1: Add the include** (alphabetical, in the `model/edits/*` block):

```cpp
#include "model/edits/paste_note_row.h"
```

- [ ] **Step 2: Add `pasteTarget`** (before the closing `} // namespace`), and add `#include "memory/heaps.h"` if not present (for `alloc_sdram`):

```cpp
// M2d: paste one synthetic copied note into an empty target row via PasteNoteRow, undo/redo. paste() records into the
// shared Action; the copy buffer is view state, so we build a minimal CopiedNoteRow here.
void pasteTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	NoteRow* noteRow = nullptr;
	int32_t rowIndex = -1;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips) && clip == nullptr; c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type == OutputType::KIT
		    || cl->output->type == OutputType::AUDIO) {
			continue;
		}
		if (!deluge::edits::clipAddressFor(*cl).has_value()) {
			continue;
		}
		auto* ic = (InstrumentClip*)cl;
		for (int32_t r = 0; r < std::ssize(ic->noteRows); r++) {
			if (ic->noteRows[r].notes.empty()) { // empty target so attemptNoteAdd has free space
				clip = ic;
				noteRow = &ic->noteRows[r];
				rowIndex = r;
				break;
			}
		}
	}
	if (clip == nullptr) {
		printf("# no-empty-addressable-row-for-paste\n");
		return;
	}
	// The pasted pos = xScroll + round(0 * 1) = xScroll; it must land inside the row for a note to appear.
	int32_t xScroll = song->persistentView.xScroll[0];
	if (xScroll < 0 || xScroll >= clip->loopLength) {
		printf("# paste-xscroll-out-of-range\n");
		return;
	}
	printf("# target paste\n");
	song->setCurrentClip(clip);

	char mem[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* ms =
	    setupModelStackWithTimelineCounter(mem, song, clip)->addNoteRow(clip->getNoteRowId(noteRow, rowIndex), noteRow);

	// Minimal copy buffer: one note at pos 0, length 1. notes[] is alloc_sdram'd so ~CopiedNoteRow can free it.
	CopiedNoteRow copied;
	copied.numNotes = 1;
	copied.notes = (Note*)deluge::memory::alloc_sdram(sizeof(Note));
	new (copied.notes) Note();
	copied.notes[0].pos = 0;
	copied.notes[0].setLength(1);
	copied.notes[0].setVelocity(64);
	copied.notes[0].setProbability(kNumProbabilityValues); // full probability
	copied.yNote = noteRow->y;
	copied.yDisplay = 0;

	int32_t before = std::ssize(noteRow->notes);
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	Action* action = actionLogger.getNewAction(ActionType::NOTES_PASTE, ActionAddition::NOT_ALLOWED);
	check(action != nullptr, "paste: harness opened an Action");
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::PasteNoteRow edit{deluge::edits::noteRowAddressFor(*ms), &copied, /*scaleFactor=*/1.0f,
	                                 /*screenEndPos=*/clip->loopLength, action, ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "paste: apply succeeds");
	check(std::ssize(noteRow->notes) == before + 1, "paste: one note pasted");
	actionLogger.revert(BEFORE, false, false);
	check(std::ssize(noteRow->notes) == before, "paste: undo removes the pasted note");
	actionLogger.revert(AFTER, false, false);
	check(std::ssize(noteRow->notes) == before + 1, "paste: redo re-pastes");

	song->setCurrentClip(nullptr);
}
```

- [ ] **Step 3: Call it in `main`** — add `pasteTarget(song);` after `tempoTarget(song);`.

- [ ] **Step 4: Build**

Run: `cmake --build build-sim --target edit_undo_redo 2>&1 | grep -iE "error:" | head`
Expected: no output. (If `setVelocity`/`setProbability`/`kNumProbabilityValues` names differ, adjust to the real `Note` API — grep `src/deluge/model/note/note.h`.)

- [ ] **Step 5: Run the corpus**

Run: `python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | grep -E "^(FAIL|OK=)"`
Expected: `OK=40 FAIL=0`.

- [ ] **Step 6: Confirm the paste target actually exercises**

Run a couple of songs directly and grep stdout for `# target paste` (vs the skip markers). Expected: exercised on ≥1 song.

---

### Task 4: Full gates + commit

- [ ] **Step 1: edit_test + firmware + golden**

```bash
cmake --build build-sim --target edit_test && ./build-sim/edit_test
./dbt build Debug 2>&1 | grep -E "error:|^\s+text"
python3 tests/golden/songs_run.py 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
```
Expected: `edit_test: OK`; firmware links (note elf text size); `OK=15 REGRESS=0`; `OK=3 REGRESS=0`.

- [ ] **Step 2: Update the catalog** — set the `NOTES_PASTE` / `PATTERN_PASTE` row in `docs/dev/edit_catalog.md` to **landed** (per-row `PasteNoteRow`; region clearArea stays direct; length via SetClipLength). Note that this closes M2d.

- [ ] **Step 3: Commit** (fresh commit; if the clang-format hook reformats + fails, `git add -u` then re-run `git commit`, NOT `--amend`):

```bash
git add src/deluge/model/edits/paste_note_row.h src/deluge/model/edits/paste_note_row.cpp \
        src/deluge/gui/views/instrument_clip_view.cpp tests/golden/edit_undo_redo.cpp docs/dev/edit_catalog.md
git commit -m "m2d: PasteNoteRow — reify NOTES_PASTE/PATTERN_PASTE per-row paste (closes M2d)"
```

- [ ] **Step 4: Update memory** — mark M2d COMPLETE in `m1-edit-substrate.md` (all clip ops + paste landed; unblocks M3 owner-1 flip).
