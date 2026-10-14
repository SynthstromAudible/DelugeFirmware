# M3 owner-2 Slice 1 — DocNoteRow scalars (dual-write) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Land the note-row DOM record (`DocNoteRow` scalars) + its binding infrastructure as an additive shadow (dual-write, no read-flip), per the owner-2 Slice 1 design.

**Architecture:** `DocClip` gains a stable-address pool of `DocNoteRow`; each engine `NoteRow` holds a non-owning `docNoteRow_` pointer, bound at create/clone/load; the scalar writers dual-write it. Nothing reads it yet → golden-neutral. **Orphan-tolerant:** the 15 delete sites are NOT hooked in Slice 1 (a removed row's pool entry lingers harmlessly since nothing reads it; precise erase lands with the read-flip).

**Tech Stack:** C++23/26. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Additive / golden bit-identical.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `./dbt test` 15/15, links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- Engine `NoteRow` fields stay authoritative; `DocNoteRow` is written but never read (Slice 1).
- `docNoteRow_` lifetime is managed ONLY at explicit hooks — never in `NoteRow`'s copy/move/dtor.
- Single fresh commit per task — never `--amend`.

---

### Task 1: Data structures (`DocNoteRow`, pool, pointer) — additive, unused

**Files:** `src/deluge-project/doc_note_row.h` (new), `src/deluge-project/doc_clip.h` (add pool), `src/deluge/model/note/note_row.h` (add `docNoteRow_` + `bindDocNoteRow`).

- [ ] **Step 1: `DocNoteRow` value type.** Create `src/deluge-project/doc_note_row.h`:
```cpp
#pragma once
#include <cstdint>
namespace deluge::project {
/// The DOM-owned structural record for one engine NoteRow (M3 owner-2). Owned by its DocClip's note_rows pool
/// (stable address); the engine NoteRow holds a non-owning pointer. Field values are seeded from the engine row
/// at bind (NoteRow::colourOffset is random() at creation), not from these defaults.
struct DocNoteRow {
	int16_t y{0};
	int32_t loop_length_if_independent{0}; ///< 0 = obeys parent clip
	int8_t colour_offset{0};
	uint8_t sequence_direction{0}; ///< SequenceDirection underlying value
	bool muted{false};
};
} // namespace deluge::project
```

- [ ] **Step 2: `DocClip` gains the pool.** In `src/deluge-project/doc_clip.h`, add `#include "deluge-project/doc_note_row.h"` + `#include <list>`, and a member (stable addresses — `std::list`):
```cpp
	/// M3 owner-2: per-note-row structural records, one per engine NoteRow. A std::list for STABLE element
	/// addresses (the engine NoteRow holds a raw pointer here). Order within the list is irrelevant — note-row
	/// order is a function of `y`, reproduced at save from the engine's y-ordered noteRows.
	std::list<DocNoteRow> note_rows;
```

- [ ] **Step 3: `NoteRow` gains the pointer.** In `src/deluge/model/note/note_row.h`: forward-decl `namespace deluge::project { struct DocNoteRow; }`, and add (public):
```cpp
	/// M3 owner-2 (dual-write phase): non-owning pointer to this row's DOM record in its clip's DocClip.note_rows
	/// pool. Bound at create/clone/load by InstrumentClip. nullptr until bound; nothing reads it yet.
	deluge::project::DocNoteRow* docNoteRow_{nullptr};
	void bindDocNoteRow(deluge::project::DocNoteRow* d) { docNoteRow_ = d; }
```
NOTE: `NoteRow` has a custom copy-ctor/assign (`note_row.h`) — leave them; the default member-copy of `docNoteRow_` (a raw pointer) is fine for the by-value container's shifts (the pointer rides the moved element). Clone aliasing is fixed at the explicit clone hook (Task 2), not here.

- [ ] **Step 4: Build + goldens.** `./dbt build Debug 2>&1 | tail -1`; sim build; run the full gate (command block in Task 2 Step 6). All must be UNCHANGED (the structures are unused). If anything REGRESSes, the mere addition perturbed layout/init — investigate.

- [ ] **Step 5: Commit.** `git commit -m "note: DocNoteRow + DocClip.note_rows pool + NoteRow::docNoteRow_ (owner-2 Slice 1 data structures, unused)"`.

---

### Task 2: Bind + dual-write (create / clone / load / scalar writers)

**Files:** `src/deluge/model/clip/instrument_clip.cpp` (create + clone bind hooks), `src/deluge/model/song/song.cpp` or `note_row.cpp` (load bind), `src/deluge/model/note/note_row.cpp` (readFromTree dual-write), the note-row scalar Edits (`set_note_row_length.cpp`, + mute/colour as funneled).

**Interfaces:**
- Consumes: `InstrumentClip::docClip_` (public, inherited from Clip), `docClip_->note_rows.emplace_back(...)`, `NoteRow::bindDocNoteRow`.
- Produces: a helper `DocNoteRow& InstrumentClip::makeDocNoteRowFor(NoteRow& nr)` — `emplace_back` a `DocNoteRow` seeded from `nr`'s current engine fields into `docClip_->note_rows`, bind `nr`, return it.

- [ ] **Step 1: `makeDocNoteRowFor` helper.** In `instrument_clip.cpp`, add a private helper that seeds a fresh pool entry from the engine row and binds it:
```cpp
void InstrumentClip::bindNoteRowStructure(NoteRow& nr) {
	if (docClip_ == nullptr) { return; }
	deluge::project::DocNoteRow& d = docClip_->note_rows.emplace_back();
	d.y = nr.y;
	d.loop_length_if_independent = nr.loopLengthIfIndependent;
	d.colour_offset = nr.colourOffset;
	d.sequence_direction = static_cast<uint8_t>(nr.sequenceDirectionMode);
	d.muted = nr.muted;
	nr.bindDocNoteRow(&d);
}
```
(Declare in `instrument_clip.h`; include `deluge-project/doc_note_row.h`.)

- [ ] **Step 2: Create-bind hooks.** After each site that inserts a NEW engine note-row into `this` clip, call `bindNoteRowStructure(*newRow)`. Sites (grep `insertNoteRowAtIndex\|insertNoteRowAtY` in `instrument_clip.cpp` — `createNewNoteRowForYVisual`, `createNewNoteRowForKit`, and any inline `noteRows.insert*` that keeps the row). For each: after the row is created and its `y`/`drum` set, bind. (The seed copies whatever fields are set at that point; later dual-writes keep it in sync.)

- [ ] **Step 3: Clone-rebind hook.** At `InstrumentClip::clone` (the `newClip->noteRows = noteRows;` deep copy, ~line 195): the copied rows' `docNoteRow_` alias THIS clip's pool. Immediately after the copy, rebuild the clone's pool:
```cpp
		newClip->noteRows = noteRows; // deep copy-assign
		newClip->docClip_->note_rows.clear();
		for (int32_t n = 0; n < std::ssize(newClip->noteRows); n++) {
			newClip->bindNoteRowStructure(newClip->noteRows[n]);
		}
```
(Seeds each clone row's fresh pool entry from the just-copied engine values.)

- [ ] **Step 4: Load-bind.** In the note-row load path (`Song::readClipsFromFile` → the `noteRows` handler that calls `NoteRow::readFromTree`, or right after each row is inserted), call `bindNoteRowStructure` on the new row BEFORE `readFromTree` sets its fields — then Step 5's dual-write keeps the pool in sync as fields are read. (If binding after readFromTree is simpler, seed then — same result, since the seed copies the engine fields.)

- [ ] **Step 5: Dual-write from `NoteRow::readFromTree` + scalar Edits.** In `NoteRow::readFromTree`, after setting each scalar (`y`/`muted`/`colourOffset`/`loopLengthIfIndependent`/`sequenceDirection`), mirror to `docNoteRow_` if non-null:
```cpp
	if (docNoteRow_) { docNoteRow_->y = y; docNoteRow_->muted = muted; /* …the 5 fields… */ }
```
(Cleanest: one block at the end of `readFromTree` copying all 5, since the row is fully read by then.) In `set_note_row_length.cpp` (and mute/colour Edits as they exist), after the engine setter, dual-write the corresponding `docNoteRow_` field guarded on non-null.

- [ ] **Step 6: Build + full gate.**
```
./dbt build Debug 2>&1 | tail -1
cmake --build build-sim --target song_roundtrip edit_undo_redo param_record_diff json_roundtrip 2>&1 | tail -1
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip 2>&1 | tail -1
./dbt test 2>&1 | grep -iE "tests passed|failed" | head -1
```
Expected: ALL unchanged (`OK=16`, `OK=40`, `OK=3`, `OK=38`, 15/15, links) — the shadow is write-only. A REGRESS means a dual-write leaked into engine state (it must not) or a hook corrupted `noteRows`.

- [ ] **Step 7: Commit.** `git commit -m "note: bind + dual-write DocNoteRow scalars at create/clone/load + note-row Edits (owner-2 Slice 1; shadow, no read-flip)"`.

---

## Self-Review

**Spec coverage.** Task 1 = the data structures; Task 2 = the binding lifecycle (create/clone/load) + dual-write. Delete-erase is intentionally deferred (orphan-tolerant, per the plan's Slice-1 scoping — nothing reads the shadow). Read-flip = Slice 2a; note vector = Slice 2b.

**Placeholder scan.** Step 2's "grep the create sites" is an executor discovery step (the exact site list); every other step has concrete code. No TODOs.

**Type consistency.** `DocNoteRow` fields ↔ engine `NoteRow` fields (`y`/`loopLengthIfIndependent`/`colourOffset`/`sequenceDirectionMode`/`muted`); `std::list` for stable addresses; `docNoteRow_` raw non-owning pointer; `bindNoteRowStructure` seeds from engine values.

**Executor note.** The risk is a missed create site (→ a runtime row with a null `docNoteRow_` — harmless in Slice 1, caught at the read-flip's assert) or a dual-write leaking into engine state (→ golden REGRESS, caught immediately). Keep dual-writes strictly additive (write `docNoteRow_` only, never read it back into engine state).
