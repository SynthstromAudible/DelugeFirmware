# M3 owner-5 Slice A — DocOutput identity foundation (Output-base fields) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Land the DOM record for the uniform Output-base identity fields (`DocOutput`) + its store + binding + rebuild-at-save sync. First slice of owner-5, scoped to the fields that apply to EVERY output (SoundInstrument/MIDIInstrument/CVInstrument/Kit/AudioOutput) with NO subclass dispatch. Additive shadow → golden bit-identical.

**Architecture:** Portable flat `DocOutput` in an `OutputStructure` map keyed by `Output::id()` (mirrors `DrumStructure`/`DocDrum`). Each `Output` holds a non-owning `docOutput_` pointer, bound via `Song::adoptOutput` (mirror `adoptDrum`) hooked in `Song::assignMissingIds`'s existing output loop; erased on output removal. `Output::syncDocOutput()` copies the fields engine→record at load + save (rebuild-at-save, like `DocAudioClip`). These fields are cold (UI/arrangement), so no hot-path concern.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL / additive.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `doc_output.h` portable — `<cstdint>`/`<string>` only.
- `DocOutput` WRITTEN, never READ this slice. Engine `Output` stays authoritative.
- NEVER mint an output id where the baseline wouldn't (adoptOutput reuses `assignMissingIds`'s existing mint, like adoptDrum).
- Idiomatic C++23. **NEVER `git commit --amend`** (clang-format hook → FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Field map (from recon) + mirror targets

Output-base identity fields (all public raw on `Output`, `output.h`): `name` (std::string), `colour` (int16_t, written output.cpp:302), `modKnobMode` (uint8_t, written as `activeModFunction` output.cpp:265), `mutedInArrangementMode` (bool), `soloingInArrangementMode` (bool), `armedForRecording` (bool) (the arrangement flags, output.cpp:261-264). Verify exact names/types in `output.h`.

**Mirror these owner-3 files** (copy their shape): `src/deluge-project/doc_drum.h` → `doc_output.h`; `src/deluge-project/drum_structure.h` → `output_structure.h`; `Song::adoptDrum`/`removeDocDrum` (song.cpp) → `adoptOutput`/`removeDocOutput`; the `assignMissingIds` drum-loop adopt (song.cpp:~1206-1208) → the output loop (song.cpp:~1193-1211, `for (Output* o = project.media.first_output; ...)`).

`Song::outputForId(uint32_t)` (song.cpp:3726) exists. Outputs get ids in `assignMissingIds` (`if (!o->id()) o->setId(generateNewOutputId())`).

---

### Task 1: `DocOutput` + `OutputStructure` + Song wiring + `docOutput_` pointer (additive, unused)

**Files:** `src/deluge-project/doc_output.h` (new), `src/deluge-project/output_structure.h` (new), the project/Song header (add store), `src/deluge/model/output.h` (pointer + bind).

- [ ] **Step 1: `DocOutput`.** Create `src/deluge-project/doc_output.h` (copyright header, `<cstdint>` + `<string>`):
```cpp
#pragma once
#include <cstdint>
#include <string>
namespace deluge::project {
/// The DOM-owned identity record for one engine Output (M3 owner-5, Output-base fields). Keyed by Output::id() in
/// OutputStructure, mirroring DocDrum/DrumStructure. Covers the fields common to every output type (instruments,
/// kits, audio outputs); per-instrument-type + Sound structure are later owner-5 slices. Rebuilt from the engine
/// at load + save.
struct DocOutput {
	std::string name;
	int16_t colour{0};
	uint8_t mod_knob_mode{0};
	bool muted_in_arrangement{false};
	bool soloing_in_arrangement{false};
	bool armed_for_recording{false};
};
} // namespace deluge::project
```
- [ ] **Step 2: `OutputStructure`.** Create `src/deluge-project/output_structure.h` copying `drum_structure.h`'s shape over `DocOutput` (`deluge::fast_map<uint32_t, DocOutput> by_id;` + `find`/`getOrCreate`/`remove`). Include `deluge-project/doc_output.h`.
- [ ] **Step 3: wire into the project.** Add `OutputStructure output_structure;` next to `clip_structure`/`drum_structure`/`audio_clip_structure` on the project Song; include the header.
- [ ] **Step 4: `docOutput_` pointer.** In `output.h`: forward-declare `namespace deluge::project { struct DocOutput; }`; add public `deluge::project::DocOutput* docOutput_{nullptr};` + `void bindDocOutput(deluge::project::DocOutput* d) { docOutput_ = d; }`.
- [ ] **Step 5: Build + full gate** (block in Task 2 Step 4). All UNCHANGED (unused).
- [ ] **Step 6: Commit.** `git commit -m "output: DocOutput + OutputStructure store + docOutput_ pointer (owner-5 slice A data structures, unused)"`.

---

### Task 2: `adoptOutput` binding + erase + `syncDocOutput` rebuild-at-save

**Files:** `src/deluge/model/song/song.h`/`.cpp` (adoptOutput/removeDocOutput + assignMissingIds hook), `src/deluge/model/output.h`/`.cpp` (syncDocOutput + load/save calls + erase on destroy).

- [ ] **Step 1: `Song::adoptOutput(Output*)`.** Mirror `adoptDrum`: idempotent (early-return if `output->docOutput_ != nullptr`); if `!output->id()` mint via `generateNewOutputId()` + setId; `DocOutput& d = project.output_structure.getOrCreate(*output->id());`; call `output->syncDocOutput()` (Task 2 Step 3) to seed; `output->bindDocOutput(&d)`. (Declare in song.h.)
- [ ] **Step 2: hook `assignMissingIds`.** In the existing output loop (`song.cpp:~1193-1211`), after each output has its id, call `adoptOutput(o)` (idempotent + reuses the just-assigned id → no new/earlier mint).
- [ ] **Step 3: `Output::syncDocOutput()`.** In `output.cpp` (declare in output.h; include `deluge-project/doc_output.h`):
```cpp
void Output::syncDocOutput() {
	if (docOutput_ == nullptr) { return; }
	docOutput_->name = name; // verify the engine field name/type
	docOutput_->colour = colour;
	docOutput_->mod_knob_mode = modKnobMode;
	docOutput_->muted_in_arrangement = mutedInArrangementMode;
	docOutput_->soloing_in_arrangement = soloingInArrangementMode;
	docOutput_->armed_for_recording = armedForRecording;
}
```
(Match EXACT engine field names/types from output.h.)
- [ ] **Step 4: call sync at load + save.** At the END of `Output::readFromFile` (or the deepest concrete override's read completion — verify where Output attrs finish loading; if Output has no own readFromFile, seed happens via adoptOutput's sync at assignMissingIds, which runs post-load — that already covers load, so a separate load call may be unnecessary; confirm). At the START of `Output::writeToFile`/`writeDataToFile` call `syncDocOutput();` (rebuild-at-save). Guard-free (helper self-guards).
- [ ] **Step 5: erase on destroy.** Add `Song::removeDocOutput(Output*)` mirroring `removeDocDrum` (`if (output->id()) project.output_structure.remove(*output->id());`), called from `~Output` guarded `currentSong && docOutput_` (mirror `~Drum`/`~Clip` teardown guard so whole-song destruction skips per-output removal).
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
Expected all UNCHANGED. A REGRESS = adoptOutput perturbed the id lifecycle (most likely — verify no new/earlier mint vs the baseline loop) or the sync/erase touched engine state.
- [ ] **Step 7: Commit.** `git commit -m "output: adoptOutput binding + erase + syncDocOutput rebuild-at-save (owner-5 slice A; shadow, no read-flip)"`.

---

## Self-Review

**Spec coverage.** Task 1 = DocOutput + store + pointer; Task 2 = the adopt/erase binding + rebuild-at-save. Scoped to Output-BASE identity fields only (uniform across all output types, no subclass dispatch). Per-instrument-type fields (dirPath, MIDI channel/suffix, CV) + Sound structure are later owner-5 slices (B–G).

**Placeholder scan.** Concrete DocOutput + adoptOutput + syncDocOutput; the store/adopt say "mirror DrumStructure/adoptDrum" (executor reads those exact files). The load-sync-necessity is a verify step (adoptOutput's sync at assignMissingIds may already cover load).

**Type consistency.** DocOutput fields ↔ Output members (name string; colour i16; modKnobMode u8; 3 arrangement bools). Keyed by uint32_t output id. Store mirrors DrumStructure; adopt mirrors adoptDrum.

**Risk.** (1) Id-mint timing — adoptOutput must reuse assignMissingIds's existing mint (output id IS serialized, so drift → REGRESS); idempotent + id-check guards it. (2) Teardown erase must match ~Drum's guard (skip during whole-song destruction). (3) Output is a base with many subclasses (incl. Kit, AudioOutput) — the sync reads only base fields, so no subclass dispatch/cast needed this slice. Nothing reads DocOutput yet → drift silent this slice.
