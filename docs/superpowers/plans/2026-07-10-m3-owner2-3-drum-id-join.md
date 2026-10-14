# M3 owner-2/3 bridge — `DocNoteRow::drum_id` kit-row→drum join (dual-write) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Record the kit-row→drum join in the DOM by dual-writing each kit `NoteRow`'s drum's durable id into a new `DocNoteRow::drum_id`. Additive shadow — the FILE format is UNCHANGED (still the positional `drumIndex`/`gateOutput` on disk); nothing reads `drum_id` yet → golden bit-identical. This resolves owner-2's explicitly-deferred `drum_id`, now unblocked because owner-3 gave every drum a durable `Drum::id()`.

**Architecture:** `DocNoteRow` grows `std::optional<uint32_t> drum_id`. A `NoteRow::syncDrumId()` helper reads the row's CURRENT `drum` member and writes `docNoteRow_->drum_id = drum ? drum->id() : std::nullopt` (guarded on `docNoteRow_`). It is called ONLY after a REAL drum assignment (never while `drum` holds a load-time sentinel int): the runtime funnel `NoteRow::setDrum`, the load-reconnection final assigns, and the bind seed.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL / additive.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `DocNoteRow::drum_id` is WRITTEN, never READ this slice. On-disk format UNCHANGED (do NOT touch `NoteRow::writeToFile`'s `drumIndex` or the readFromTree sentinel scheme).
- **CRITICAL — the sentinel trap:** during load, `NoteRow::readFromTree` stashes a raw int in the `drum` pointer slot (`(Drum*)0xFFFFFFFF`, a drumIndex, or `(Drum*)(0xFFFFFFFE-gateChannel)`). `syncDrumId()` dereferences `drum->id()`, so it MUST NEVER be called while `drum` holds a sentinel — ONLY after `drum` is a real `Drum*` (or nullptr). Do NOT call it in `readFromTree`.
- Idiomatic C++23.
- **NEVER `git commit --amend`** (clang-format hook reformats+fails first commit → FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Site map (verified)

- `DocNoteRow` — `src/deluge-project/doc_note_row.h` (add `drum_id` + `#include <optional>`).
- `NoteRow::drum` is `Drum*` (public, note_row.h ~236). `NoteRow::setDrum(Drum* newDrum, Kit*, ModelStackWithNoteRow*, ...)` — the runtime funnel, `note_row.cpp:3807`.
- **Load reconnection FINAL assigns** (real drums — hook these), `instrument_clip.cpp`: `:4017` `thisNoteRow->drum = nullptr;`, `:4025` `= kit->getGateDrumForChannel(gateChannel)`, `:4036` `= newDrum`, `:4072` `= drumFromIndex`. Locate by grep in the reconnection block ~4016-4072.
- Other raw real assign: `instrument_clip.cpp:2613` `thisNoteRow->drum = nullptr;`. `note_row.cpp:setDrumToNull` (`:3792`) also clears the drum — check it.
- **DO NOT hook** the `readFromTree` sentinel stashes (`note_row.cpp` ~3468-3492, `drum = (Drum*)0xFFFFFFFF` etc.) — `drum` is not a real pointer there.
- `bindNoteRowStructure` (owner-2 slice 1, `instrument_clip.cpp`) — seeds DocNoteRow at bind; add a drum_id seed.
- `Drum::id()` returns `std::optional<uint32_t>` (`drum.h`).

---

### Task 1: `DocNoteRow::drum_id` + `syncDrumId` + hook the real-assign sites

**Files:** `src/deluge-project/doc_note_row.h`, `src/deluge/model/note/note_row.h`/`.cpp`, `src/deluge/model/clip/instrument_clip.cpp`.

- [ ] **Step 1: `DocNoteRow::drum_id`.** In `doc_note_row.h`: add `#include <optional>`; add member `std::optional<uint32_t> drum_id;` to `DocNoteRow` (place it with the scalar metadata, before the `notes` vector). Update the doc-comment to note it holds the row's drum's durable id (nullopt = no drum).
- [ ] **Step 2: `NoteRow::syncDrumId()`.** In `note_row.cpp` (declare in `note_row.h`; `drum.h`/`deluge-project/doc_note_row.h` are needed for the complete types — include as needed):
```cpp
void NoteRow::syncDrumId() {
	if (docNoteRow_ == nullptr) { return; }
	docNoteRow_->drum_id = (drum != nullptr) ? drum->id() : std::nullopt;
}
```
(Reads the CURRENT `drum` member — caller guarantees it is a real pointer or nullptr, never a sentinel.)
- [ ] **Step 3: Hook `setDrum`.** In `NoteRow::setDrum` (note_row.cpp:3807), after the `drum = newDrum;` assignment (find where the member is set), call `syncDrumId();`. Also in `NoteRow::setDrumToNull` (:3792) after it nulls `drum`, call `syncDrumId();` (→ nullopt).
- [ ] **Step 4: Hook the load-reconnection finals.** In `instrument_clip.cpp`, after each of the 4 real assigns in the reconnection block (`:4017`, `:4025`, `:4036`, `:4072`), call `thisNoteRow->syncDrumId();`. (These run after the sentinel is resolved to a real drum/nullptr.) NOTE: at reconnection, a loaded drum has its file-restored id if the file had one; id-less legacy drums get their id later at `assignMissingIds`, so their `drum_id` stays nullopt until a subsequent `setDrum` — acceptable for a shadow (nothing reads it). Also hook the raw `:2613` assign.
- [ ] **Step 5: Seed at bind.** In `InstrumentClip::bindNoteRowStructure`, add `d.drum_id = (nr.drum != nullptr) ? nr.drum->id() : std::nullopt;` alongside the other seeds. (At create-time bind the drum may not be set yet → nullopt; the later `setDrum` corrects it via syncDrumId. `nr.drum` here is always a real pointer/nullptr — bind never runs while a sentinel is stashed, since bind happens at insert and readFromTree's sentinel is on a DIFFERENT, already-inserted row's own load path; if unsure, guard identically.)
- [ ] **Step 6: Build + full gate.**
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
Expected: firmware links; `OK=16/40/3/38`; `edit_test: OK`; `param_automation projection: OK`; `./dbt test` 15/15. A REGRESS = a `syncDrumId` was called on a sentinel (crash/garbage id — but goldens would more likely CRASH), or the field addition perturbed serialization (it must not — DocNoteRow isn't serialized). If a crash/hang: a sentinel deref — audit that no hook runs before reconnection.
- [ ] **Step 7: Commit.** `git commit -m "note: dual-write DocNoteRow::drum_id from the kit-row's drum id (owner-2/3 join; shadow, format unchanged)"`.

---

## Self-Review

**Spec coverage.** Adds the `drum_id` field + the `syncDrumId` funnel + hooks at every REAL drum assignment (runtime `setDrum`/`setDrumToNull`, the 4 load-reconnection finals, `:2613`, bind seed). File format untouched (drumIndex stays). Resolves owner-2's deferred join as a shadow; the actual save/load flip to drum_id (a file-format change + golden re-baseline) is a later slice.

**Placeholder scan.** Concrete code; the sentinel-trap constraint is explicit; sites are grep-locatable.

**Type consistency.** `DocNoteRow::drum_id` is `std::optional<uint32_t>` = `Drum::id()`'s return type — direct assignment, no conversion. `syncDrumId` reads the `Drum*` member.

**Risk.** (1) THE sentinel trap — calling `syncDrumId` on a stashed-int `drum` derefs garbage; mitigated by hooking ONLY post-reconnection real assigns + never in readFromTree. (2) Legacy id-less drums yield nullopt at reconnection until assignMissingIds — acceptable for a shadow; noted for the eventual read/save flip. (3) Nothing reads `drum_id`, so any residual imperfection is silent this slice.
