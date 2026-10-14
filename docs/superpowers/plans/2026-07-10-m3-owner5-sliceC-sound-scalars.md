# M3 owner-5 Slice C — DocSound remaining top-level scalars — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Grow `DocSound` with the remaining top-level `Sound` scalar fields (polyphony/mode/transpose/unison/modulator transpose+cents/retrigger phases) and their `syncDocSound` copies. Additive shadow → golden bit-identical. Purely additive to Slice B's foundation (the `DocSound` type, nesting, binding, and `syncDocSound()` all already exist).

**Architecture:** These fields include render-HOT scalars (numUnison, synthMode, polyphonic — read in `voice.cpp` inner loops), but this slice is a REBUILD-AT-SAVE SHADOW (nothing reads `DocSound`; the engine stays the render read-authority; the live read-projection is M4's job). So hotness does NOT change the approach — just add fields + sync lines.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL / additive.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `doc_sound.h` portable — `<cstdint>` only. `DocSound` WRITTEN, never READ. Engine `Sound` stays authoritative.
- Idiomatic C++23. **NEVER `git commit --amend`** (clang-format hook → FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Field map (from recon — VERIFY exact names/types in `sound.h`)

`polyphonic` (`PolyphonyMode` enum, sound.h:115), `synthMode` (`SynthMode` enum, :136), `transpose` (int16_t, :118), `numUnison` (uint8_t, :120), `unisonDetune` (int8_t, :121), `unisonStereoSpread` (uint8_t, :122), `modulator1ToModulator0` (bool, :137), `modulatorTranspose[2]` (int16_t, :128), `modulatorCents[2]` (int8_t, :129), `oscRetriggerPhase[2]` (uint32_t, :156), `modulatorRetriggerPhase[2]` (uint32_t, :157). Confirm each name/type/array-extent before use.

---

### Task 1: Add the scalars to `DocSound` + `syncDocSound`

**Files:** `src/deluge-project/doc_sound.h`, `src/deluge/processing/sound/sound.cpp` (`syncDocSound`).

- [ ] **Step 1: grow `DocSound`.** In `doc_sound.h`, add after the Slice-B fields (enums as underlying; [2] arrays as plain fixed arrays):
```cpp
	uint8_t polyphonic{0};              ///< PolyphonyMode underlying
	uint8_t synth_mode{0};              ///< SynthMode underlying
	int16_t transpose{0};
	uint8_t num_unison{0};
	int8_t unison_detune{0};
	uint8_t unison_stereo_spread{0};
	bool modulator1_to_modulator0{false};
	int16_t modulator_transpose[2]{};
	int8_t modulator_cents[2]{};
	uint32_t osc_retrigger_phase[2]{};
	uint32_t modulator_retrigger_phase[2]{};
```
- [ ] **Step 2: extend `syncDocSound`.** In `Sound::syncDocSound()` (sound.cpp), after the Slice-B lines, add:
```cpp
	d.polyphonic = static_cast<uint8_t>(polyphonic);
	d.synth_mode = static_cast<uint8_t>(synthMode);
	d.transpose = transpose;
	d.num_unison = numUnison;
	d.unison_detune = unisonDetune;
	d.unison_stereo_spread = unisonStereoSpread;
	d.modulator1_to_modulator0 = modulator1ToModulator0;
	for (int i = 0; i < 2; i++) {
		d.modulator_transpose[i] = modulatorTranspose[i];
		d.modulator_cents[i] = modulatorCents[i];
		d.osc_retrigger_phase[i] = oscRetriggerPhase[i];
		d.modulator_retrigger_phase[i] = modulatorRetriggerPhase[i];
	}
```
(Use the EXACT engine field names verified in Step 0. If `synthMode` is accessed via a getter `getSynthMode()` rather than the raw field, use whichever the raw serializer uses — match `Sound::writeToFile`'s source for each.)
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
Expected all UNCHANGED (write-only shadow growth). A REGRESS = a wrong engine field read or a sync side effect (there must be none — sync only WRITES DocSound).
- [ ] **Step 4: Commit.** `git commit -m "sound: DocSound remaining top-level scalars (unison/modulator/mode/retrigger) rebuild-at-save (owner-5 slice C; shadow)"`.

---

## Self-Review

**Spec coverage.** Adds the ~11 remaining top-level Sound scalars to DocSound + syncDocSound. Rebuild-at-save shadow, no read-flip (hot fields included but not read-flipped — that's M4). LFO/modKnobs/ModControllableAudio/sources = later slices.

**Placeholder scan.** Concrete field list + sync code; the "verify names in sound.h" is a real executor step (Step 0 of Task 1).

**Type consistency.** DocSound fields ↔ Sound members in underlying form (PolyphonyMode/SynthMode → u8; transpose i16; unisonDetune i8; the [2] arrays match extent + element type: modulatorTranspose i16, modulatorCents i8, retrigger phases u32).

**Risk.** Low — purely additive to an existing sync. The only failure modes: a mistyped field name/extent (compile error) or reading a field the serializer doesn't (e.g. synthMode via getter vs raw) — but since nothing reads DocSound, even a slightly-wrong source is golden-neutral this slice (would only matter at a future read/save flip). Match `Sound::writeToFile`'s source per field to be safe.
