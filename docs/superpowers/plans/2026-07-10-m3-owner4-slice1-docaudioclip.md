# M3 owner-4 Slice 1 — DocAudioClip foundation + rebuild-at-save — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Land the DOM record for AudioClip-specific descriptor fields (`DocAudioClip`) + its store + binding + a rebuild-from-engine sync at load and save. Additive shadow (nothing reads it → golden bit-identical). First slice of owner-4.

**Architecture:** A portable flat `DocAudioClip` (sample ref by path + ranges + pitch + the audio scalar/enum descriptors) lives in an `AudioClipStructure` map keyed by `Clip::id()` (separate from `DocClip`, which is shared with `InstrumentClip`). Each `AudioClip` holds a non-owning `docAudioClip_` pointer, bound the SAME way `Clip` binds `docClip_`. `AudioClip::syncDocAudioClip()` copies all engine descriptor fields → the record; called at load (`readFromFile` end) and save (`writeDataToFile` start) — REBUILD-AT-SAVE, so no per-mutator dual-write is needed (the ~8 menu/consequence mutators keep the engine authoritative at runtime; the DOM is correct at save). The audio RENDER hot-path reads stay on the engine (the DOM is a persistence copy, like the note vector).

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL / additive.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `doc_audio_clip.h` portable — `<cstdint>`/`<string>` only (like `doc_clip.h` + the `<string>` precedent in `metadata.h`).
- `DocAudioClip` WRITTEN, never READ this slice. Engine `AudioClip`/`SampleHolderForClip` stay authoritative for runtime + render.
- Idiomatic C++23. **NEVER `git commit --amend`** (clang-format hook → FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Field map (from recon)

Portable `DocAudioClip` mirrors the AudioClip persistent audio-specific surface:
- `sampleHolder` (`SampleHolderForClip : SampleHolder : AudioFileHolder`): `filePath` (std::string, `audio_file_holder.h`), `startPos`/`endPos` (uint64_t, `sample_holder.h`), `transpose` (int16_t)/`cents` (int8_t) (`sample_holder_for_clip.h:32-33`). (NOT the `AudioFile* audioFile` pointer, NOT view/cache fields.)
- `sampleControls` (`SampleControls`): `interpolationMode` (enum → underlying uint8_t), `pitchAndSpeedAreIndependent` (bool), `reversed` (bool). (NOT `invertReversed` — runtime-only, never serialized.)
- `attack` (int32_t, `audio_clip.h:110`), `voicePriority` (`VoicePriority` enum → uint8_t, `:112`), `overdubsShouldCloneOutput` (bool, declared on `Clip` base `clip.h:228` but AudioClip-only, tag `overdubsShouldCloneAudioTrack`).
- (EXCLUDE `outputNameWhileLoading` — transient load cache resolved to `output`; and all RT state: voiceSample/guide/renderData/recorder/nextSampleRestartPos.)

Serialization is FLAT attributes on `<audioClip>` (`AudioClip::writeDataToFile` audio_clip.cpp:1057-1107 / `readFromFile` :1109-1200): `filePath, startSamplePos, endSamplePos, pitchSpeedIndependent, linearInterpolation, reversed, attack, priority, transpose, cents, overdubsShouldCloneAudioTrack`.

**Mirror the DocClip binding:** READ how `Clip` binds `docClip_` (grep `docClip_`, `getOrCreateDocClip`, `ClipStructure` — likely `clip.cpp` ctor + `clip_structure.h` + a `Song::project.getOrCreateDocClip`), and replicate that exact lifecycle for `docAudioClip_`/`AudioClipStructure`.

---

### Task 1: `DocAudioClip` + `AudioClipStructure` + Song wiring + `docAudioClip_` pointer (additive, unused)

**Files:** `src/deluge-project/doc_audio_clip.h` (new), `src/deluge-project/audio_clip_structure.h` (new), the project/Song header (add store), `src/deluge/model/clip/audio_clip.h` (pointer + bind).

- [ ] **Step 1: `DocAudioClip`.** Create `src/deluge-project/doc_audio_clip.h` (copyright header, `<cstdint>` + `<string>`):
```cpp
#pragma once
#include <cstdint>
#include <string>
namespace deluge::project {
/// The DOM-owned descriptor record for one AudioClip (M3 owner-4). Keyed by Clip::id() in AudioClipStructure,
/// separate from DocClip (which is shared with InstrumentClip). The sample is referenced by portable PATH + ranges,
/// never a pointer. Enums stored as underlying values. Rebuilt from the engine AudioClip at load + save.
struct DocAudioClip {
	std::string file_path;
	uint64_t start_pos{0};
	uint64_t end_pos{0};
	int16_t transpose{0};
	int8_t cents{0};
	uint8_t interpolation_mode{0}; ///< SampleControls::interpolationMode underlying
	bool pitch_and_speed_independent{false};
	bool reversed{false};
	int32_t attack{0};
	uint8_t voice_priority{0}; ///< VoicePriority underlying
	bool overdubs_should_clone{false};
};
} // namespace deluge::project
```
- [ ] **Step 2: `AudioClipStructure`.** Create `src/deluge-project/audio_clip_structure.h` by copying `clip_structure.h`'s shape over `DocAudioClip` (`deluge::fast_map<uint32_t, DocAudioClip> by_id;` + `find`/`getOrCreate`/`remove`). Include `deluge-project/doc_audio_clip.h`.
- [ ] **Step 3: wire into the project.** Where `ClipStructure clip_structure` is declared on the project/Song, add `AudioClipStructure audio_clip_structure;`. Include the header.
- [ ] **Step 4: `docAudioClip_` pointer.** In `audio_clip.h`: forward-declare `namespace deluge::project { struct DocAudioClip; }`; add public `deluge::project::DocAudioClip* docAudioClip_{nullptr};` + `void bindDocAudioClip(deluge::project::DocAudioClip* d) { docAudioClip_ = d; }`. Bind it wherever/however `docClip_` is bound for AudioClip (mirror the DocClip lifecycle — likely in the AudioClip ctor after the base Clip ctor sets the id, and in any load-binding/adopt pass that binds docClip_). Erase from `audio_clip_structure` wherever `docClip_`/DocClip is removed (mirror `removeDocClip`).
- [ ] **Step 5: Build + full gate** (block in Task 2 Step 3). All UNCHANGED (structures unused).
- [ ] **Step 6: Commit.** `git commit -m "audio: DocAudioClip + AudioClipStructure store + docAudioClip_ binding (owner-4 slice 1 data structures, unused)"`.

---

### Task 2: `syncDocAudioClip()` rebuild-at-load + rebuild-at-save

**Files:** `src/deluge/model/clip/audio_clip.h`/`.cpp`.

- [ ] **Step 1: `syncDocAudioClip()`.** In `audio_clip.cpp` (declare in `audio_clip.h`; include `deluge-project/doc_audio_clip.h`):
```cpp
void AudioClip::syncDocAudioClip() {
	if (docAudioClip_ == nullptr) { return; }
	deluge::project::DocAudioClip& d = *docAudioClip_;
	d.file_path = sampleHolder.audioFile ? sampleHolder.audioFile->filePath : sampleHolder.filePath; // match writeDataToFile's choice
	d.start_pos = sampleHolder.startPos;
	d.end_pos = sampleHolder.endPos;
	d.transpose = sampleHolder.transpose;
	d.cents = sampleHolder.cents;
	d.interpolation_mode = static_cast<uint8_t>(sampleControls.interpolationMode);
	d.pitch_and_speed_independent = sampleControls.pitchAndSpeedAreIndependent;
	d.reversed = sampleControls.reversed;
	d.attack = attack;
	d.voice_priority = static_cast<uint8_t>(voicePriority);
	d.overdubs_should_clone = overdubsShouldCloneOutput;
}
```
(Match the EXACT engine field names/types — verify against audio_clip.h + the sample_holder chain. The file_path choice must mirror writeDataToFile's `audioFile ? audioFile->filePath : sampleHolder.filePath` ternary.)
- [ ] **Step 2: call it at load + save.** At the END of `AudioClip::readFromFile` (after all fields are read, before return) call `syncDocAudioClip();`. At the START of `AudioClip::writeDataToFile` (before writing attributes) call `syncDocAudioClip();` (rebuild-at-save — captures runtime menu/consequence mutations without per-mutator dual-write). Guard both are fine (helper self-guards on null).
- [ ] **Step 3: Build + full gate.**
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
Expected all UNCHANGED (the sync is write-only into the unread DOM record). A REGRESS = the sync read a wrong engine field or perturbed engine state (it must not — it only WRITES the DocAudioClip). Corpus note: if no audio clips exist in the golden corpus, this is still exercised by any AudioClip construction/teardown — but flag in the report whether the corpus actually contains audio clips (if not, the sync is compiled-but-cold and the proof is weaker).
- [ ] **Step 4: Commit.** `git commit -m "audio: rebuild DocAudioClip from the engine at load + save (owner-4 slice 1; shadow, no read-flip)"`.

---

## Self-Review

**Spec coverage.** Task 1 = DocAudioClip + store + binding (mirroring DocClip); Task 2 = the rebuild-at-load/save sync. Matches "foundation + rebuild-at-save" — no read-flip, no per-mutator dual-write, engine stays render-authoritative. The eventual read-flip of COLD fields (attack/priority/reversed/interp/overdub/filePath) — with continuous dual-write — and the hot fields staying engine-read is a later slice.

**Placeholder scan.** Concrete DocAudioClip + sync code; the binding "mirror docClip_" is a directed read-and-replicate (executor reads the exact clip.cpp lifecycle). No TODOs.

**Type consistency.** DocAudioClip fields ↔ the sample-holder chain + AudioClip members in emitted/portable form (path string; ranges uint64; transpose i16/cents i8; enums as underlying u8; attack i32). Keyed by uint32_t clip id. Separate store, mirroring DrumStructure/ClipStructure.

**Risk.** (1) The `file_path` ternary must match `writeDataToFile` exactly (`audioFile ? audioFile->filePath : sampleHolder.filePath`). (2) Binding lifecycle must mirror docClip_ precisely — a mis-timed bind gives a null docAudioClip_ (→ sync no-ops, golden-neutral but incomplete). (3) If the corpus has no audio clips, golden-neutrality is trivially satisfied but under-proven — report corpus audio-clip presence. Nothing reads the record, so any imperfection is silent this slice.
