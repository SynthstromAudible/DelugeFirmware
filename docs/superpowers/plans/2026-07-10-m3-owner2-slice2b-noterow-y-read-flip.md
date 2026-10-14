# M3 owner-2 Slice 2b — NoteRow `y` READ-FLIP — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Flip the read authority of engine `NoteRow::y` (int16, the pitch / container sort key) from the engine field to the DOM shadow (`docNoteRow_->y`), the 5th and LAST NoteRow metadata scalar — completing "all NoteRow metadata DOM-authoritative." Same nullable-fallback pattern as Slice 2a's four scalars.

**Architecture:** `y` is the `OrderedPosVector<NoteRow, &NoteRow::y>` sort key (`note_row_vector.h:23`), so privatizing it needs the container to be a `friend`. Recon confirmed the container's write-side generic methods (`insertSorted`/`shiftHorizontal`/`generateRepeats`) are NEVER instantiated for `NoteRow` (lazy instantiation — `NoteRowVector` uses its own `insertNoteRowAtY` + `repositionElement` + the read-only `firstAtOrAfter` projection), so NO `OrderedPosVector` template refactor is needed — a narrow friend suffices. DOM scaffolding already exists: `DocNoteRow::y` (doc_note_row.h:28), the seed in `bindNoteRowStructure`, and the `readFromTree` dual-write. This slice adds the `y()` getter / `setY()` setter and converts the ~40 call sites.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL is the gate.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- The engine field stays WRITTEN (setter writes both) → valid fallback for unbound rows. Its retirement is a later slice.
- **`setY` is a PLAIN write-through — it MUST NOT assert/enforce sortedness.** Recon found transpose loops (`instrument_clip.cpp:1436-1497`) mutate `->y` in place across the y-sorted `NoteRowVector` with NO re-sort (pre-existing latent behaviour); a sort-asserting setter would break them. Preserve exactly: write `y_` and (if bound) `docNoteRow_->y`, nothing else.
- Do NOT touch the OTHER `OrderedPosVector` users (`Note::pos`, `ParamNode::pos`, `ClipInstance::pos`, `MIDIParam::cc`). Do NOT touch `Note` (it has no `y`; `y` is NoteRow-only).
- New/edited C++ idiomatic C++23 matching the existing 4 scalar accessors (`note_row.h:173-239`).
- **NEVER `git commit --amend`** (clang-format hook reformats+fails the first commit → recover with a FRESH `git commit`, staged files carry the reformat). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**
- One fresh commit.

## Site map (from recon)

- **Container key (KEEP):** `note_row_vector.h:23` `class NoteRowVector final : public deluge::OrderedPosVector<NoteRow, &NoteRow::y>`. The `&NoteRow::y` template arg MUST still form — handled by the friend (below).
- **Container raw y-write (friend context):** `note_row_vector.cpp:38` `noteRow->y = y;` inside `NoteRowVector::insertNoteRowAtY` (its own insert, bypasses base `insertSorted`). Runs BEFORE the row is bound (bind happens after insert), so `docNoteRow_` is null there.
- **DOM scaffolding already present:** `doc_note_row.h:28` `int16_t y`; `instrument_clip.cpp:~1249` seed `d.y = nr.y` (→ becomes `nr.y()`); `note_row.cpp:~3562` readFromTree dual-write `docNoteRow_->y = y` (kept from Slice 1/2a — the ONLY line left in that trailing block).
- **y-WRITE sites to funnel through `setY` (recon Q4):** `instrument_clip.cpp:1337` (`+= changes[degree]`), `:1399` (`+= semitones`), `:1436` (`+= change`), `:1448` & `:1492` (`+= changeInSemitones`), `:3285` (`= (rootNote%12)+60`), `:3294` (`= getYNoteFromYVisual(...)`). Compound `+=` → `setY(y() + delta)`.
- **Internal-to-NoteRow writes:** `note_row.cpp:55-56` ctor `y = newY` → `y_ = newY` (raw; no docNoteRow_ at ctor). `note_row.cpp:3550` readFromTree `y = std::max(y, (int16_t)*minY)` → `y_ = std::max<int16_t>(y_, *minY)` (raw internal; the trailing dual-write block then mirrors it to the shadow — keep that block's `docNoteRow_->y = y_;` line).
- **y-READ sites (~30, recon Q4):** `instrument_clip.cpp` (23), `instrument_clip_view.cpp` (16), singles in `model_stack.cpp:46`, `note_row_vector.cpp:38`(RHS is the param `y`, LHS is the field — see above), `sound_instrument.cpp:194`, `param_set.cpp:760`, `melodic_instrument.cpp:506`, `note_address_for.cpp:42`. Convert `noteRow->y`/`.y` reads → `y()`. **WATCH: do NOT rewrite `yDisplay`/`yScroll`/`yNote`/`yVisual`/local `y` params — only the `NoteRow` `y` MEMBER (`->y`, `.y` on a NoteRow).**

## Task 1: Flip `NoteRow::y`

**Files:** `note_row.h` (encapsulate + friend), `note_row.cpp`, `instrument_clip.cpp`, `instrument_clip_view.cpp`, `note_row_vector.cpp`, `model_stack.cpp`, `sound_instrument.cpp`, `param_set.cpp`, `melodic_instrument.cpp`, `note_address_for.cpp`.

- [ ] **Step 1: Encapsulate + friend.** In `note_row.h`: make `y` → private `int16_t y_{0}` (preserve its current initializer/type). Add public:
```cpp
	int16_t y() const { return docNoteRow_ ? docNoteRow_->y : y_; }
	void setY(int16_t v) { y_ = v; if (docNoteRow_) { docNoteRow_->y = v; } } // plain write-through; NO sort assertion
```
Add a friend so the container can form `&NoteRow::y` and do its raw insert-write. Add near the top of the class:
```cpp
	friend class NoteRowVector; // sorts by (and raw-writes) the private y_ key
```
(Include/forward-declare `NoteRowVector` as needed — `note_row.h` is included by `note_row_vector.h`, so forward-declare `class NoteRowVector;` above `NoteRow`.)

- [ ] **Step 2: Verify the friend compiles the container.** Build ONLY enough to check `note_row_vector.h`'s base-specifier `&NoteRow::y` still forms with `y_` private. If it does NOT compile (base-specifier access can't see the friend on some toolchains), FALLBACK: keep `y` public (rename back), keep the `y()`/`setY()` accessors, and rely on call-site discipline + the golden gate + a final `grep -n "->y\b\|\.y\b"` audit for completeness. Note which path you took in the report. Either way the read-flip is achieved; privatization is the enforcement bonus.

- [ ] **Step 3: In `note_row_vector.cpp:38`,** `noteRow->y = y;` — this is a `NoteRowVector` method (friend), row not yet bound. Leave as raw `noteRow->y_ = y;` (friend access) OR `noteRow->setY(y)` (setY on an unbound row just writes `y_`). Prefer `setY(y)` for uniformity unless it forces exposing internals oddly.

- [ ] **Step 4: Drive internal NoteRow writes.** `note_row.cpp` ctor `y = newY` → `y_ = newY`. `readFromTree` `y = std::max(...)` → `y_ = std::max<int16_t>(y_, ...)`; keep the trailing `docNoteRow_->y = y_;` dual-write line (it now reads `y_`). Any other bare `y` inside `NoteRow` methods that is the MEMBER → `y_` (or `y()` for reads); do NOT rename local variables named `y`.

- [ ] **Step 5: Drive external write sites → `setY`.** The 7 `instrument_clip.cpp` sites in the site map: compound `thisNoteRow->y += delta` → `thisNoteRow->setY(thisNoteRow->y() + delta)`; direct `= expr` → `setY(expr)`. Preserve loop structure exactly (no added re-sort).

- [ ] **Step 6: Drive external read sites → `y()`.** All `->y`/`.y` reads on a `NoteRow` across the ~9 files → `y()`. Update the `bindNoteRowStructure` seed `d.y = nr.y` → `d.y = nr.y()`. Audit with `grep -n "\->y\b" src/deluge` and confirm every remaining bare `->y` is either the friend container line, `yDisplay/yScroll/yNote/yVisual`, or a non-NoteRow type.

- [ ] **Step 7: Build + full gate.**
```
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
./dbt build Debug 2>&1 | tail -3
cmake --build build-sim --target song_roundtrip edit_undo_redo param_record_diff json_roundtrip edit_test param_projection_test 2>&1 | tail -3
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -2
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -2
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -2
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip 2>&1 | tail -2
./build-sim/edit_test 2>&1 | tail -1
./build-sim/param_projection_test 2>&1 | tail -1
./dbt test 2>&1 | grep -iE "tests passed|failed|[0-9]+/[0-9]+" | head -3
```
Expected: firmware links; `OK=16/40/3/38`; `edit_test: OK`; `param_automation projection: OK`; `./dbt test` 15/15. A REGRESS = a NoteRow y read missed `y()` (reads stale) or a write missed `setY` (shadow desync) — fix before committing. (`edit_test`/`param_projection_test` are the off-default-gate sim tests that touch NoteRow directly; included here so a missed test-file site can't hide.)

- [ ] **Step 8: Commit.** `git commit -m "note: read-flip NoteRow::y onto DocNoteRow (owner-2 slice 2b; all NoteRow metadata now DOM-authoritative)"`.

---

## Self-Review

**Spec coverage.** Single field (`y`), the last NoteRow scalar. Nullable-fallback accessor identical to Slice 2a. Friend solves the container-key access with a documented fallback (Step 2). drum_id + the note vector are explicitly out (owner-3 / Slice 2c).

**Placeholder scan.** Every site cited from recon; friend fallback is a concrete branch, not a TODO.

**Type consistency.** `y()` returns `int16_t`; `setY(int16_t)` writes both stores, no sort assertion (constraint honoured). `&NoteRow::y` template arg preserved (never rewritten). `yDisplay`/`yScroll`/`yNote`/local `y` excluded.

**Risk.** (1) The friend + private-`y` in a base-specifier template arg is the one uncertain compile — Step 2 tests it early with a clean fallback. (2) The transpose-loop no-resort invariant is preserved by a plain-write `setY`. (3) Over-eager rewrite of `yDisplay`/locals — the grep audit in Step 6 catches stragglers. The golden gate arbitrates behaviour.
