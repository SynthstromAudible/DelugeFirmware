# M3 owner-3 Slice 2 — Drum `name` dual-write (setName funnel + DocDrum.name) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Shadow the drum `name` into `DocDrum` via a new `Drum::setName` write funnel — additive dual-write, NO read-flip (name reads stay on the engine `drumName`; the read-flip + privatization + the `addDrum`→currentSong mis-binding fix are a later slice). Extends the owner-3 shadow (slice 1 did `channel`/`note`).

**Architecture:** `Drum` gains a `setName(...)` funnel that writes `drumName` AND (if bound) `docDrum_->name`. All ~13 scattered raw `drumName =` writes route through it. `DocDrum` grows a `name` (`std::string`, precedented — `deluge-project/metadata.h` already uses `std::string`). `Song::adoptDrum` seeds `name` at bind. Nothing reads `DocDrum::name` → golden bit-identical.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL / additive.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `DocDrum::name` is WRITTEN, never READ this slice. `drumName` stays PUBLIC + authoritative (no privatization, no read-flip) — reads unchanged.
- `setName` must be BEHAVIOR-IDENTICAL to the raw assignments it replaces (preserve moves where a site does `drumName = std::move(x)`).
- Idiomatic C++23.
- **NEVER `git commit --amend`** (clang-format hook reformats+fails the first commit → FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Site map (from recon)

- `Drum::drumName` = `std::string drumName{}` at `src/deluge/model/drum/drum.h:54` (public raw).
- 13 raw `drumName =` write sites across 7 files: `gui/ui/audio_recorder.cpp`, `gui/ui/slicer.cpp`, `gui/ui/browser/sample_browser.cpp`, `gui/ui/rename/rename_drum_ui.cpp`, `gui/ui/load/load_instrument_preset_ui.cpp`, `gui/ui/save/save_kit_row_ui.cpp`, `gui/views/instrument_clip_view.cpp`. Find each with `grep -rn "drumName =" src/deluge` (note: `drumName ==` / `.drumName)` reads are NOT writes — only the assignment `=`). Some may be `->drumName =`, some `.drumName =` (on a `Drum` object/pointer).
- Serializer reads `drumName` (`Drum::writeDrumTagsToFile`, drum.cpp) — a READ, leave as `drumName`. Load sets `drumName` inside `readDrumTagFromFile` (drum.cpp) — that's an internal-to-Drum load write; route it through `setName` too so the shadow seeds on load (docDrum_ is null at that point → writes only drumName, harmless; adoptDrum re-seeds from drumName later).
- `DocDrum` at `src/deluge-project/doc_drum.h` (slice 1); `Song::adoptDrum` in `src/deluge/model/song/song.cpp` (slice 1); `DrumStructure` global store.

---

### Task 1: `Drum::setName` funnel (behavior-identical refactor)

**Files:** `src/deluge/model/drum/drum.h` (declare + inline, OR declare + impl in drum.cpp), the 7 write-site files.

- [ ] **Step 1: Add `setName`.** In `Drum` (`drum.h`), add a setter that (for now) just writes the field — the dual-write is added in Task 2 so this task stays a pure behavior-identical refactor:
```cpp
	void setName(std::string_view newName) { drumName = newName; }
```
Choose the signature/overloads that compile ALL 13 sites with IDENTICAL behavior — if a site does `drumName = std::move(x)` (move), add a `void setName(std::string&& newName) { drumName = std::move(newName); }` overload so the move is preserved; if sites pass `const char*`/string literals, `std::string_view` covers them (or add a `const std::string&` overload). Do NOT change what any site actually stores. (`std::string::operator=` accepts `string_view`? If not on this toolchain, use `drumName.assign(newName)` in the body.)
- [ ] **Step 2: Funnel the 13 sites.** Replace each raw `X.drumName = expr;` / `X->drumName = expr;` with `X.setName(expr);` / `X->setName(expr);`, preserving `std::move` where present. Route the load-path write in `Drum::readDrumTagFromFile` (drum.cpp) through `setName` too. Leave READS (`drumName` in comparisons, serializer, getName-like uses) untouched.
- [ ] **Step 3: Build + full gate** (block in Task 2 Step 4). All UNCHANGED — this is a pure refactor (setName == the old assignment), so BIT-IDENTICAL. A REGRESS means a site's behavior changed (a lost move, a wrong overload, or a read accidentally rewritten).
- [ ] **Step 4: Commit.** `git commit -m "drum: route drumName writes through Drum::setName funnel (owner-3 slice 2 prep; behaviour-identical)"`.

---

### Task 2: `DocDrum.name` + dual-write + adopt seed

**Files:** `src/deluge-project/doc_drum.h` (add name), `src/deluge/model/drum/drum.h` (setName dual-write), `src/deluge/model/song/song.cpp` (adoptDrum seed).

- [ ] **Step 1: `DocDrum.name`.** In `doc_drum.h`: add `#include <string>` and a member `std::string name;` to `DocDrum` (matching `metadata.h`'s precedent). Update the struct doc-comment to mention name.
- [ ] **Step 2: setName dual-write.** In `Drum::setName` (both overloads if two), after writing `drumName`, add `if (docDrum_ != nullptr) { docDrum_->name = drumName; }`. (Read back `drumName` after assign so both overloads share one mirror line and the stored value is exact — avoids re-move issues.) `doc_drum.h` must be included in the TU that defines setName (if setName is inline in drum.h, drum.h already fwd-declares DocDrum — for the dual-write it needs the COMPLETE type; either move setName's body to drum.cpp with `#include "deluge-project/doc_drum.h"`, or include it in drum.h. Prefer moving the body to drum.cpp to keep drum.h light.)
- [ ] **Step 3: adoptDrum seeds name.** In `Song::adoptDrum` (song.cpp, slice 1), where it seeds `drum_type`/`channel`/`note`, add `d.name = drum->drumName;`.
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
Expected: firmware links; `OK=16/40/3/38`; `edit_test: OK`; `param_automation projection: OK`; `./dbt test` 15/15. A REGRESS = the dual-write/seed leaked into engine state (it must not — `DocDrum::name` is write-only). 
- [ ] **Step 5: Commit.** `git commit -m "drum: dual-write drumName into DocDrum.name via setName + adoptDrum seed (owner-3 slice 2; shadow, no read-flip)"`.

---

## Self-Review

**Spec coverage.** Task 1 = the funnel (behaviour-identical); Task 2 = the DOM field + dual-write + seed. Matches "extend the owner-3 shadow with name." Read-flip / privatization / the addDrum→currentSong mis-binding fix are explicitly OUT (later slice).

**Placeholder scan.** Concrete code; the setName signature is "pick what compiles all 13 sites identically" (a real executor decision from the sites, not a TODO).

**Type consistency.** `DocDrum::name` is `std::string` (matches engine `drumName` + `metadata.h` precedent). `setName` mirrors `drumName` post-assign into `docDrum_->name`. adoptDrum seeds from `drum->drumName`.

**Risk.** (1) A lost `std::move` or a mis-chosen overload changing a site's stored value — golden gate catches behaviour drift (Task 1 must be bit-identical BEFORE Task 2 adds the shadow). (2) Accidentally rewriting a `drumName` READ as `setName` — only `=` assignments are writes. (3) Complete-type requirement for the dual-write — resolved by defining setName's body in drum.cpp with the doc_drum.h include. Nothing reads the shadow, so drift is silent this slice (surfaces at the read-flip).
