# M3 owner-2 Slice 2c — Note vector DOM SAVE-authority (rebuild-at-save) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Give the DOM a faithful representation of the per-note musical data (`DocNote`) and make it the SAVE authority for notes — without touching the hot note read-path. The `DocNote` pool on `DocNoteRow` is REBUILT from the engine note vector at save; the note serializer writes from the pool. Golden bit-identical proves the representation reproduces the file bytes exactly.

**Why this shape (decided):** The note vector is the hottest sequencer data and the north-star has the engine play *derived snapshots*, so a live read-flip / hot-path DOM accessor belongs at the **M4 snapshot boundary, NOT here**. This slice therefore builds NO per-note binding, NO back-pointer, NO `Note` encapsulation, NO read-flip — only: a `DocNote` type, a transient pool on `DocNoteRow`, a rebuild-from-engine, and the save-write flip. The read/projection story is explicitly M4's.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL is the gate.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `doc_note.h` MUST stay portable — `<cstdint>` only, NO engine includes (mirror `doc_note_row.h`/`doc_clip.h`). Store iterance as its packed `uint16_t` (`Iterance::toInt()`); reconstruct `Iterance::fromInt(...)` on the ENGINE side (the writer) for the legacy `toPresetIndex()` path.
- The engine note vector (`NoteRow::notes`) stays authoritative at runtime and for playback — untouched. The DocNote pool is a SAVE-TIME scratch representation, freed after each write (must NOT permanently double note RAM).
- No `Note` field encapsulation, no read-site changes, no hot-path changes.
- New C++ idiomatic C++23.
- **NEVER `git commit --amend`** (clang-format hook reformats+fails the first commit → recover with a FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Reference — what the note serializer emits today (`note_row.cpp` `NoteRow::writeToFile`, ~3590-3683)

Guarded by `if (!notes.empty())`. Two hex blobs, each a per-note loop over `notes[n]`:
1. **`noteDataWithSplitProb`** (current format): per note writes `pos` (8 hex), `getLength()` (8), `getVelocity()` (2), `getLift()` (2), `getProbability()` (2), `getIterance().toInt()` (4), `getFill()` (2).
2. **`noteDataWithLift`** (`#if ALPHA_OR_BETA_VERSION` legacy compat): per note writes `pos`, `getLength()`, `getVelocity()`, `getLift()`, then a computed `oldProbability` (2) derived from `getIterance().toPresetIndex()`, `getFill()`, `getProbability()` (see the existing priority logic at 3656-3676 — preserve it verbatim).

`Note` fields (`note.h`): `pos` (int32, via `Positionable`), `length` (int32), `velocity` (u8), `probability` (u8), `iterance` (`Iterance`), `fill` (u8), `lift` (u8). `Iterance` (`iterance/iterance.h`): `uint16_t toInt()`, `static Iterance fromInt(int32_t)`, `int32_t toPresetIndex()` (all non-const).

---

### Task 1: `DocNote` type + `DocNoteRow.notes` pool

**Files:** `src/deluge-project/doc_note.h` (new), `src/deluge-project/doc_note_row.h` (add pool).

- [ ] **Step 1: `DocNote`.** Create `src/deluge-project/doc_note.h` (copyright header like doc_note_row.h), `<cstdint>` only:
```cpp
#pragma once
#include <cstdint>
namespace deluge::project {
/// The DOM-owned save representation of one engine Note (M3 owner-2 Slice 2c). A flat mirror of the fields the
/// note serializer emits. `iterance` is the packed `Iterance::toInt()` form (reconstruct via `Iterance::fromInt`
/// on the engine side) so this header stays portable (no engine includes). Rebuilt from the engine note vector
/// at save; the engine `Note` stays authoritative at runtime.
struct DocNote {
	int32_t pos{0};
	int32_t length{0};
	uint8_t velocity{0};
	uint8_t probability{0};
	uint16_t iterance{0}; ///< Iterance::toInt() packed form
	uint8_t fill{0};
	uint8_t lift{0}; ///< noteOff velocity
};
} // namespace deluge::project
```
- [ ] **Step 2: pool on DocNoteRow.** In `src/deluge-project/doc_note_row.h`: add `#include "deluge-project/doc_note.h"` + `#include <vector>`, and a member on `DocNoteRow`:
```cpp
	/// M3 owner-2 Slice 2c: SAVE-time note representation, rebuilt from the engine note vector at write and freed
	/// after (transient scratch — must not permanently double note RAM). Contiguous vector (no stable-address
	/// requirement — nothing holds a pointer into it, unlike the note-row pool).
	std::vector<DocNote> notes;
```
- [ ] **Step 3: Build + full gate** (command block in Task 3 Step 4). All UNCHANGED (unused structures).
- [ ] **Step 4: Commit.** `git commit -m "note: DocNote + DocNoteRow.notes pool (owner-2 slice 2c data structures, unused)"`.

---

### Task 2: Rebuild + save-write flip

**Files:** `src/deluge/model/note/note_row.h` (declare `rebuildDocNotes`), `src/deluge/model/note/note_row.cpp` (impl + writeToFile flip).

- [ ] **Step 1: `rebuildDocNotes`.** In `note_row.cpp`, add (declare in `note_row.h`, include `deluge-project/doc_note.h`):
```cpp
void NoteRow::rebuildDocNotes() {
	if (docNoteRow_ == nullptr) { return; }
	auto& pool = docNoteRow_->notes;
	pool.clear();
	pool.reserve(notes.size());
	for (int32_t n = 0; n < std::ssize(notes); n++) {
		Note& note = notes[n];
		pool.push_back(deluge::project::DocNote{
		    .pos = note.pos,
		    .length = note.getLength(),
		    .velocity = static_cast<uint8_t>(note.getVelocity()),
		    .probability = static_cast<uint8_t>(note.getProbability()),
		    .iterance = note.getIterance().toInt(),
		    .fill = static_cast<uint8_t>(note.getFill()),
		    .lift = static_cast<uint8_t>(note.getLift()),
		});
	}
}
```
- [ ] **Step 2: Flip the serializer.** In `NoteRow::writeToFile`, inside the `if (!notes.empty())` block: call `rebuildDocNotes();` first. Then make BOTH hex loops (`noteDataWithSplitProb` and the `#if ALPHA_OR_BETA_VERSION` `noteDataWithLift`) write from `docNoteRow_->notes` WHEN BOUND, falling back to the engine `notes` when `docNoteRow_ == nullptr`. To avoid duplicating the hex-emit logic four ways, extract two file-local helpers that take the raw scalar values and do the writing:
  - `writeSplitProbRecord(Serializer&, int32_t pos, int32_t length, uint8_t velocity, uint8_t lift, uint8_t probability, uint16_t iteranceInt, uint8_t fill)` — the 7 `intToHex` writes.
  - `writeLiftRecord(Serializer&, int32_t pos, int32_t length, uint8_t velocity, uint8_t lift, uint16_t iteranceInt, uint8_t fill, uint8_t probability)` — pos/length/velocity/lift + the `oldProbability` priority computation (preserve 3656-3676 verbatim), reconstructing the Iterance via `Iterance::fromInt(iteranceInt)` to call `.toPresetIndex()`.
  Each loop then iterates the pool (`for (auto& d : docNoteRow_->notes) writeSplitProbRecord(writer, d.pos, d.length, d.velocity, d.lift, d.probability, d.iterance, d.fill);`) or the engine notes (`for (n...) { Note& note = notes[n]; writeSplitProbRecord(writer, note.pos, note.getLength(), note.getVelocity(), note.getLift(), note.getProbability(), note.getIterance().toInt(), note.getFill()); }`). The `intToHex(...)` widths must match the originals EXACTLY (pos/length default 8, others 2, iterance 4). 

  (Simpler acceptable alternative if the helper extraction gets awkward: iterate the pool when bound / engine when not, with the loop body inline — as long as output is byte-identical. The helper is preferred to avoid drift, but correctness/bit-identity is the gate, not the factoring.)
- [ ] **Step 3: Free the scratch.** After the whole notes block (still inside writeToFile), release the pool so it doesn't persist: `if (docNoteRow_ != nullptr) { std::vector<deluge::project::DocNote>().swap(docNoteRow_->notes); }` (frees capacity, unlike `clear()`).
- [ ] **Step 4: Build + full gate.**
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
Expected: firmware links; `OK=16/40/3/38`; `edit_test: OK`; `param_automation projection: OK`; `./dbt test` 15/15. A REGRESS = the DocNote representation doesn't reproduce the exact bytes — the hex-emit values/widths diverged, or the fallback path differs from the pool path. The two paths MUST produce identical output (the pool is a faithful rebuild).
- [ ] **Step 5: Commit.** `git commit -m "note: flip note serializer to write from the DocNote pool, rebuilt-at-save (owner-2 slice 2c; DOM is note save-authority)"`.

---

## Self-Review

**Spec coverage.** Task 1 = `DocNote` + pool (portable header, packed iterance). Task 2 = rebuild-from-engine + the save-write flip + scratch-free. No read-flip, no encapsulation, no hot-path touch — matches the chosen "rebuild-at-save (lightest)" depth. Load is unchanged (the file still fills the engine vector; the pool is built at save). Playback-projection is M4's.

**Placeholder scan.** Concrete code for `DocNote`, the pool, `rebuildDocNotes`, the flip, and the scratch-free. The helper-vs-inline factoring is explicitly bit-identity-gated, not a TODO.

**Type consistency.** `DocNote` fields ↔ the serializer's emitted values in the emitted form (iterance as `toInt()` u16; velocity/probability/fill/lift as u8; pos/length as i32). `intToHex` widths preserved (8/8/2/2/2/4/2). Iterance reconstructed engine-side via `fromInt` for `toPresetIndex()`. Pool is `std::vector` (no stable-address need — nothing points into it).

**Risk.** (1) The bit-identity of the two write paths (pool vs engine fallback) — they must emit identically; the golden gate arbitrates. (2) The alpha/beta `oldProbability` legacy computation must be preserved verbatim and fed the reconstructed Iterance — a subtle spot; the `#if ALPHA_OR_BETA_VERSION` blob is exercised by the sim goldens (sim is a beta build). (3) Forgetting the scratch-free would double note RAM on device — Step 3 guards it. `y`/metadata slices are unaffected (this only touches the note serializer).
