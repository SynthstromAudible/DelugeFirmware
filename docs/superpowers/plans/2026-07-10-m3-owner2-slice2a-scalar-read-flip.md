# M3 owner-2 Slice 2a — NoteRow scalar READ-FLIP (4 non-y scalars) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Flip the read authority of 4 engine `NoteRow` scalars (`muted`, `colourOffset`, `loopLengthIfIndependent`, `sequenceDirectionMode`) from the engine field to the DOM shadow (`docNoteRow_->field`), keeping the engine field as a synced fallback for unbound rows. `y` is EXCLUDED (it is the `OrderedPosVector<NoteRow,&NoteRow::y>` sort key — deferred to Slice 2b with the note vector).

**Architecture:** Owner-1 slice-3b nullable pattern (NOT the later ownDoc_/treeDoc_ pattern — NoteRow is stored by value in bulk, so it must NOT gain an inline DocClip). Per field: encapsulate the public field → `field_` (private) + `field()` getter returning `docNoteRow_ ? docNoteRow_->docField : field_` + `setField(x)` writing BOTH (`field_ = x; if (docNoteRow_) docNoteRow_->docField = x;`). Compiler-drive every read→`field()`, every write→`setField()`, compound `x+=n`→`setField(field()+n)`. Bound rows read DOM; unbound rows (rare — escaped a create hook, or pre-adopt) read the engine field = old behaviour. Additive/behaviour-preserving → golden bit-identical.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL is the gate.** Success per task = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `docNoteRow_` binding lifecycle stays as Slice 1 built it (create/clone/load hooks). Do NOT add anything to NoteRow copy/move/dtor.
- The engine field stays WRITTEN (setter writes both) so it remains a valid fallback; its eventual retirement is a later slice.
- `y` (int16) is OUT of scope. Do NOT touch `NoteRow::y` — it is the container key and would break the `&NoteRow::y` member-pointer template arg in `note_row_vector.h:23`.
- New/edited C++ idiomatic C++23 matching surrounding style.
- **NEVER `git commit --amend`** — the pre-commit clang-format hook reformats+fails the first commit; recover with a FRESH `git commit` (staged files carry the reformat). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**
- One fresh commit per task.

## Site map (from recon — exact locations)

**Container key (DO NOT TOUCH):** `note_row_vector.h:23` `NoteRowVector : OrderedPosVector<NoteRow,&NoteRow::y>`.

**Existing accessors to reconcile:** `NoteRow::getColourOffset(InstrumentClip*)` (note_row.h:245 / note_row.cpp:3728 — COMPUTED: returns 0 for non-KIT else raw `colourOffset`; make it call `colourOffset()`). `NoteRow::setSequenceDirectionMode(ModelStackWithNoteRow*, SequenceDirection)` (note_row.cpp:4532 — route through the new `setSequenceDirectionMode` value or fold the dual-write in). `NoteRow::setLength(...)` (note_row.cpp:2679 — already dual-writes loopLengthIfIndependent).

**Slice-1 dual-write block** at end of `NoteRow::readFromTree` (note_row.cpp ~3566-3571) sets docNoteRow_->{5 fields} directly. After encapsulation, route the 4 in-scope fields' reads through their setters (which dual-write); KEEP the `y` line in that block (y shadow still synced for the future y-flip). At load, the row is bound at insert site 2845 BEFORE readFromTree, so `docNoteRow_` is non-null → setters dual-write correctly.

## Global gate command block (run after EACH task)

```
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
./dbt build Debug 2>&1 | tail -3
cmake --build build-sim --target song_roundtrip edit_undo_redo param_record_diff json_roundtrip 2>&1 | tail -3
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -2
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -2
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -2
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip 2>&1 | tail -2
./dbt test 2>&1 | grep -iE "tests passed|failed|[0-9]+/[0-9]+" | head -3
```
Expected UNCHANGED every task: firmware links, OK=16/40/3/38, dbt 15/15. A REGRESS = a read site missed the accessor (reads stale/wrong) or a write missed the setter (shadow desync) — fix before committing.

---

### Task 1: Flip `muted` (bool) + establish the accessor pattern

**Files:** `note_row.h` (encapsulate), `note_row.cpp` (accessors + writers + readFromTree), `instrument_clip.cpp`/`instrument_clip_view.cpp`/`stem_export.cpp` (read sites + writers).

- [ ] **Encapsulate.** In `note_row.h`: make `muted` → private `bool muted_ : 1{false};` (match current bitfield/type). Add public `bool muted() const { return docNoteRow_ ? docNoteRow_->muted : muted_; }` and `void setMuted(bool v) { muted_ = v; if (docNoteRow_) { docNoteRow_->muted = v; } }`.
- [ ] **Drive reads.** Compiler-drive every read of `.muted`/`->muted` (recon: ~16 sites — stem_export.cpp ×7 [reads], instrument_clip_view.cpp ×3, instrument_clip.cpp ×3, note_row.cpp ×3) to `muted()`. WATCH: `mutedBeforeStemExport` is a DIFFERENT field — do NOT rewrite it. `Clip`-level muting is separate — only rewrite `NoteRow` `muted`.
- [ ] **Drive writes.** `NoteRow::toggleMute` (note_row.cpp:3127 `muted = !muted;`) → `setMuted(!muted());`. `stem_export.cpp:632` `thisNoteRow->muted = true;` → `thisNoteRow->setMuted(true);` (and its restore site). In `readFromTree`, the muted assignment → `setMuted(...)`; drop the explicit `docNoteRow_->muted = muted;` line from the trailing block.
- [ ] **Build + full gate** (block above). All UNCHANGED.
- [ ] **Commit:** `git commit -m "note: read-flip NoteRow::muted onto DocNoteRow (owner-2 slice 2a; nullable-fallback accessor)"`.

---

### Task 2: Flip `colourOffset` (int8)

**Files:** `note_row.h`, `note_row.cpp`, `instrument_clip_view.cpp`, `instrument_clip.cpp`.

- [ ] **Encapsulate.** `colourOffset` → private `int8_t colourOffset_`. Add `int8_t colourOffset() const { return docNoteRow_ ? docNoteRow_->colour_offset : colourOffset_; }` + `void setColourOffset(int8_t v) { colourOffset_ = v; if (docNoteRow_) { docNoteRow_->colour_offset = v; } }`.
- [ ] **Reconcile getColourOffset.** `NoteRow::getColourOffset(InstrumentClip*)` body (note_row.cpp:3728) — its raw `colourOffset` read → `colourOffset()`. Its callers are unchanged (still call getColourOffset).
- [ ] **Drive reads** (~14 sites: instrument_clip_view.cpp ×7, instrument_clip.cpp ×2, note_row.cpp ×2, …) → `colourOffset()`.
- [ ] **Drive writes.** COMPOUND at `instrument_clip_view.cpp:6385-6390` (`commandShiftColour`: `noteRow->colourOffset += offset;` / `-= 72` / `+= 72`) → `setColourOffset(noteRow->colourOffset() + offset)` etc. `InstrumentClip::cloneAsNewOverdub` (instrument_clip.cpp:4360 `newNoteRow->colourOffset = oldNoteRow->colourOffset;`) → `setColourOffset(oldNoteRow->colourOffset())`. Constructor (note_row.cpp:58 `colourOffset = random(71)`) → `colourOffset_ = random(71)` (raw — no docNoteRow_ yet at ctor). `bindNoteRowStructure` seed (instrument_clip.cpp) reads `nr.colourOffset` → `nr.colourOffset()`. In `readFromTree`, colourOffset assignment → `setColourOffset(...)`; drop that line from the trailing dual-write block.
- [ ] **Build + full gate.** All UNCHANGED.
- [ ] **Commit:** `git commit -m "note: read-flip NoteRow::colourOffset onto DocNoteRow (owner-2 slice 2a)"`.

---

### Task 3: Flip `loopLengthIfIndependent` (int32)

**Files:** `note_row.h`, `note_row.cpp`, `instrument_clip.cpp`, `instrument_clip_view.cpp`, `model_stack.cpp`, `stem_export.cpp`.

- [ ] **Encapsulate.** `loopLengthIfIndependent` → private `int32_t loopLengthIfIndependent_`. Add `int32_t loopLengthIfIndependent() const { return docNoteRow_ ? docNoteRow_->loop_length_if_independent : loopLengthIfIndependent_; }` + `void setLoopLengthIfIndependent(int32_t v) { loopLengthIfIndependent_ = v; if (docNoteRow_) { docNoteRow_->loop_length_if_independent = v; } }`. NOTE the distinct sibling `getLoopLength()` (computed vs parent) — leave it, just make its internal `loopLengthIfIndependent` read → `loopLengthIfIndependent()`.
- [ ] **Drive reads** (~40 sites, instrument_clip.cpp dominant ~20) → `loopLengthIfIndependent()`. This is the noisiest field — check each is a `NoteRow` (not a local named similarly).
- [ ] **Drive writes** (6 non-setLength writers): `instrument_clip.cpp:297` (`= newLengthHere`), `:336` (`= 0`), `:370` (`= 0`) → `setLoopLengthIfIndependent(...)`. `instrument_clip.cpp:4552` COMPOUND (`+= otherNoteRow->loopLengthIfIndependent`) → `setLoopLengthIfIndependent(loopLengthIfIndependent() + otherNoteRow->loopLengthIfIndependent())`. `instrument_clip_view.cpp:2490` (`= desiredNoteLength`) → setter. `note_row.cpp:2086` COMPOUND (in `processCurrentPos`) → setter. `NoteRow::setLength` (note_row.cpp:2679) already dual-writes — replace its `loopLengthIfIndependent = x; docNoteRow_->... = x;` pair with a single `setLoopLengthIfIndependent(x)`. `bindNoteRowStructure` seed read → `nr.loopLengthIfIndependent()`. In `readFromTree` → `setLoopLengthIfIndependent(...)`, drop that trailing-block line.
- [ ] **Build + full gate.** All UNCHANGED.
- [ ] **Commit:** `git commit -m "note: read-flip NoteRow::loopLengthIfIndependent onto DocNoteRow (owner-2 slice 2a)"`.

---

### Task 4: Flip `sequenceDirectionMode` (SequenceDirection enum)

**Files:** `note_row.h`, `note_row.cpp`, `model_stack.cpp`, `direction.h`, `instrument_clip.cpp`.

- [ ] **Encapsulate.** `sequenceDirectionMode` → private `SequenceDirection sequenceDirectionMode_`. Add `SequenceDirection sequenceDirectionMode() const { return docNoteRow_ ? static_cast<SequenceDirection>(docNoteRow_->sequence_direction) : sequenceDirectionMode_; }` + a raw setter `void setSequenceDirectionModeRaw(SequenceDirection v) { sequenceDirectionMode_ = v; if (docNoteRow_) { docNoteRow_->sequence_direction = static_cast<uint8_t>(v); } }`. (There is ALSO the existing `setSequenceDirectionMode(ModelStackWithNoteRow*, SequenceDirection)` public method — have its body call `setSequenceDirectionModeRaw` for the field write, keeping its surrounding logic.)
- [ ] **CAVEAT — disambiguate from `Clip::sequenceDirectionMode`.** `Clip`/`InstrumentClip` has its OWN `sequenceDirectionMode` field (parent default). Recon flagged these as `Clip`-level, NOT NoteRow — do NOT rewrite: `session_view.cpp:2270-2271`, `clip.cpp:349,353`, `instrument_clip.cpp:140,236-237`, `song.cpp:2253`, `playback/mode/session.cpp:2654`, `model_stack.cpp:70`, `direction.h:74`, `note_row.cpp:2043,2817`-RHS. Only ~5 genuine NoteRow-context reads exist: `model_stack.cpp:68`, `direction.h:71`, `instrument_clip.cpp:1252`, `note_row.cpp:84,2817`-LHS. Rewrite ONLY the NoteRow ones → `sequenceDirectionMode()`.
- [ ] **Drive writes.** `NoteRow::setSequenceDirectionMode` impl (note_row.cpp:4536 `sequenceDirectionMode = newMode`) → `setSequenceDirectionModeRaw(newMode)`. `NoteRow::generateRepeats` (note_row.cpp:2817 direct NoteRow write when flattening pingpong) → `setSequenceDirectionModeRaw(...)`. Constructor default (note_row.cpp:84) → `sequenceDirectionMode_ = ...` raw. `bindNoteRowStructure` seed → `static_cast<uint8_t>(nr.sequenceDirectionMode())`. In `readFromTree` → `setSequenceDirectionModeRaw(...)`, drop that trailing-block line (leaving ONLY the `y` line in the block).
- [ ] **Build + full gate.** All UNCHANGED.
- [ ] **Commit:** `git commit -m "note: read-flip NoteRow::sequenceDirectionMode onto DocNoteRow (owner-2 slice 2a)"`.

---

### Task 5: Delete-erase hooks + tighten the ALPHA invariant

**Files:** `instrument_clip.h`/`instrument_clip.cpp` (unbind helper + call sites), `writeDataToFile` (invariant).

Slice 1 was orphan-tolerant. Now add pool-erase so a removed row's `DocNoteRow` leaves `docClip_->note_rows` (memory hygiene + prerequisite for the eventual pool-iteration save). NOT required for read-correctness (a deleted engine NoteRow is never read), so this is golden-neutral.

- [ ] **Unbind helper.** In `instrument_clip.cpp`: `void InstrumentClip::unbindNoteRowStructure(NoteRow& nr) { if (docClip_ == nullptr || nr.docNoteRow_ == nullptr) { return; } auto& pool = docClip_->note_rows; for (auto it = pool.begin(); it != pool.end(); ++it) { if (&*it == nr.docNoteRow_) { pool.erase(it); break; } } nr.docNoteRow_ = nullptr; }` (declare in `instrument_clip.h`; `docNoteRow_` is public on NoteRow per Slice 1). O(n) scan by address is fine (per-clip pools are small).
- [ ] **Call at the 7 delete sites** (all in `instrument_clip.cpp`, all have `this->docClip_`): before each `noteRows.deleteNoteRowAtIndex(index)` call `unbindNoteRowStructure(noteRows[index]);`. Sites: `noteRemovedFromMode` (:1344), `seeWhatNotesWithinOctaveArePresent` (:1363), `prepareToEnterKitMode` (:1707), `deleteEmptyNoteRowsAtEitherEnd` (:1964 and :1987 — two passes; erase the row at each index BEFORE its delete), `deleteSoundsWhichWontSound` (:3317), `deleteNoteRow` (:3349). For loop-based deletes, erase the SAME index element the delete targets, immediately before the delete.
- [ ] **Tighten invariant.** In `writeDataToFile` (the ALPHA `#if ALPHA_OR_BETA_VERSION` block Task-2 added), keep the "every noteRow.docNoteRow_ != nullptr" check; the erase hooks now keep the pool size == live-row count, so it stays consistent.
- [ ] **Build + full gate.** All UNCHANGED (erase removes an unread pool entry → no observable change).
- [ ] **Commit:** `git commit -m "note: erase DocNoteRow on note-row delete (owner-2 slice 2a; pool hygiene) + tighten ALPHA invariant"`.

---

## Self-Review

**Spec coverage.** 4 non-y scalars flipped (Tasks 1-4), `y` correctly deferred (container key). Erase hooks + invariant (Task 5). Read-flip = nullable-fallback accessor (bound→DOM, unbound→engine field), matching owner-1 slice-3b; engine field retained as synced fallback (retirement is a later slice).

**Placeholder scan.** Every task cites exact recon sites; no TODOs.

**Type consistency.** Getters return the engine type (`static_cast` from the u8-stored `sequence_direction`); setters write both stores. Compound writes expanded via `field()`. `getColourOffset`/`getLoopLength`/`setSequenceDirectionMode(ModelStack*,…)` reconciled to call the new accessors. `Clip::sequenceDirectionMode` and `mutedBeforeStemExport` explicitly excluded from rewrites.

**Risk.** The two disambiguation traps — `Clip::sequenceDirectionMode` vs `NoteRow::sequenceDirectionMode` (Task 4 caveat) and `mutedBeforeStemExport` vs `muted` (Task 1) — are the likeliest way to mis-rewrite. The golden gate catches a read that stayed on a stale field (behaviour change) or a write that skipped the setter (shadow desync). `y` untouched keeps the container key intact.
