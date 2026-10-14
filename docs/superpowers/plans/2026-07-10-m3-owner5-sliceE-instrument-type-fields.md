# M3 owner-5 Slice E — per-instrument-type DocOutput fields (virtual syncDocOutput) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Grow `DocOutput` with the per-instrument-type COLD descriptor fields (Instrument dirPath/defaultVelocity; NonAudioInstrument channel; MIDIInstrument channelSuffix/deviceDef/collapse/outputMPEY; CVInstrument cv2Source), populated via a VIRTUAL `syncDocOutput()` with per-type overrides. Additive shadow → golden bit-identical.

**Architecture:** `syncDocOutput()` becomes `virtual` on `Output` (Slice A made it non-virtual, base fields only). Each instrument type overrides it, calls its parent's, and adds its own fields — following the single-inheritance chain `Output → Instrument → MelodicInstrument → NonAudioInstrument → MIDIInstrument`/`CVInstrument`. This avoids downcasts + instrument-header includes in the base `output.cpp`. `DocOutput` grows the fields flat (defaulted; only the relevant type populates them). All cold (UI/load) — rebuild-at-save.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL / additive.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `doc_output.h` portable — `<cstdint>`/`<string>` only. `DocOutput` WRITTEN, never READ. Engine stays authoritative.
- The instrument `midiInput`/`muteMIDICommand` (LearnedMIDI) are DEFERRED to M4/M5 (same as drums — hot MIDI-match reads + device-identity). NOT in this slice.
- Idiomatic C++23. **NEVER `git commit --amend`** (clang-format hook → FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Field map (from recon — verify)

- `Instrument` (`instrument.h`): `dirPath` (std::string, :51), `defaultVelocity` (uint8_t, :90). Applies to SYNTH/KIT/MIDI_OUT/CV (every non-AUDIO output — all are Instruments; AUDIO is not).
- `NonAudioInstrument` (`non_audio_instrument.h`): `channel` (int32_t, :75, private — use `getChannel()` :59). Applies to MIDI_OUT + CV.
- `MIDIInstrument` (`midi_instrument.h`): `channelSuffix` (int32_t default -1, :106), `deviceDefinitionFileName` (std::string, :64), `collapseAftertouch` (bool, :108), `collapseMPE` (bool default true, :109), `outputMPEY` (`CCNumber`, :110). Applies to MIDI_OUT.
- `CVInstrument` (`cv_instrument.h`): `getCV2Mode()` returns `cvmode[1]` (`CVMode` enum, :77). Applies to CV.
- `OutputType` (`definitions_cxx.hpp:231`): SYNTH, KIT, MIDI_OUT, CV, AUDIO, NONE.

The save-time call site: Slice A calls `syncDocOutput()` at the start of `Output::writeToFile`. VERIFY every output type routes its save through that common `Output::writeToFile` (so the virtual dispatch fires) — if a subclass save path does NOT call the base, add the virtual `syncDocOutput()` call there too. Load seed rides `adoptOutput` (which calls `syncDocOutput()` — now virtual, so it dispatches correctly at assignMissingIds).

---

### Task 1: grow `DocOutput` with the per-type fields (additive, unused)

**Files:** `src/deluge-project/doc_output.h`.

- [ ] **Step 1.** Add to `DocOutput` (after the Slice-A base fields):
```cpp
	// Owner-5 Slice E: per-instrument-type descriptors (populated only for the relevant OutputType; 0/empty otherwise).
	std::string dir_path;                    ///< Instrument (non-AUDIO)
	uint8_t default_velocity{0};             ///< Instrument
	int32_t channel{0};                      ///< NonAudioInstrument (MIDI_OUT/CV)
	int32_t channel_suffix{-1};              ///< MIDIInstrument
	std::string device_definition_file_name; ///< MIDIInstrument
	bool collapse_aftertouch{false};         ///< MIDIInstrument
	bool collapse_mpe{true};                 ///< MIDIInstrument
	uint8_t output_mpe_y{0};                 ///< MIDIInstrument (CCNumber underlying)
	uint8_t cv2_source{0};                   ///< CVInstrument (CVMode underlying)
```
- [ ] **Step 2.** Build + full gate (block in Task 2). UNCHANGED (unused fields).
- [ ] **Step 3.** Commit `git commit -m "output: DocOutput per-instrument-type fields (owner-5 slice E data structures, unused)"`.

---

### Task 2: virtual `syncDocOutput()` + per-type overrides

**Files:** `output.h`/`.cpp` (make virtual), `instrument.h`/`.cpp`, `non_audio_instrument.h`/`.cpp`, `midi_instrument.h`/`.cpp`, `cv_instrument.h`/`.cpp`.

- [ ] **Step 1: make `Output::syncDocOutput()` virtual.** In `output.h`, add `virtual` to its declaration; body unchanged (base fields). Keep it self-guarding on `docOutput_ == nullptr`.
- [ ] **Step 2: `Instrument::syncDocOutput()` override.** In `instrument.cpp` (declare `void syncDocOutput() override;` in instrument.h; include `deluge-project/doc_output.h`):
```cpp
void Instrument::syncDocOutput() {
	Output::syncDocOutput();
	if (docOutput_ == nullptr) { return; }
	docOutput_->dir_path = dirPath;
	docOutput_->default_velocity = defaultVelocity;
}
```
- [ ] **Step 3: `NonAudioInstrument::syncDocOutput()` override** (adds channel; parent is MelodicInstrument→Instrument, so calls `Instrument::syncDocOutput()` transitively — call the DIRECT parent `MelodicInstrument::syncDocOutput()` if it has one, else `Instrument::syncDocOutput()`):
```cpp
void NonAudioInstrument::syncDocOutput() {
	Instrument::syncDocOutput(); // (or MelodicInstrument:: if that overrides — it doesn't add fields, so Instrument:: is fine)
	if (docOutput_ == nullptr) { return; }
	docOutput_->channel = getChannel();
}
```
- [ ] **Step 4: `MIDIInstrument::syncDocOutput()` override:**
```cpp
void MIDIInstrument::syncDocOutput() {
	NonAudioInstrument::syncDocOutput();
	if (docOutput_ == nullptr) { return; }
	docOutput_->channel_suffix = channelSuffix;
	docOutput_->device_definition_file_name = deviceDefinitionFileName;
	docOutput_->collapse_aftertouch = collapseAftertouch;
	docOutput_->collapse_mpe = collapseMPE;
	docOutput_->output_mpe_y = static_cast<uint8_t>(outputMPEY);
}
```
- [ ] **Step 5: `CVInstrument::syncDocOutput()` override:**
```cpp
void CVInstrument::syncDocOutput() {
	NonAudioInstrument::syncDocOutput();
	if (docOutput_ == nullptr) { return; }
	docOutput_->cv2_source = static_cast<uint8_t>(getCV2Mode());
}
```
(SoundInstrument needs NO override — its Instrument base covers dirPath/defaultVelocity, and its Sound fields go through `syncDocSound`. Kit needs NO override — Instrument base covers dirPath/defaultVelocity; kit drums are owner-3.)
- [ ] **Step 6: verify the save call site.** Confirm `Output::writeToFile` (where Slice A put `syncDocOutput()`) is on the common save path for MIDI/CV/Synth/Kit outputs so the virtual override fires. If any type's save bypasses it, add `syncDocOutput()` at that type's save entry. (adoptOutput's call already dispatches virtually for load.)
- [ ] **Step 7: Build + full gate.**
```
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
./dbt build Debug 2>&1 | tail -3
cmake --build build-sim --target song_roundtrip edit_undo_redo param_record_diff json_roundtrip edit_test param_projection_test 2>&1 | tail -3
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip 2>&1 | tail -1
./build-sim/edit_test 2>&1 | tail -1
./build-sim/param_projection_test 2>&1 | tail -1
./dbt test 2>&1 | grep -iE "tests passed|failed" | tail -1
```
Expected all UNCHANGED. A REGRESS = a sync read a wrong field or the virtual dispatch touched engine state (it must only WRITE DocOutput).
- [ ] **Step 8: Commit.** `git commit -m "output: per-instrument-type syncDocOutput overrides (Instrument/NonAudio/MIDI/CV) rebuild-at-save (owner-5 slice E; shadow)"`.

---

## Self-Review

**Spec coverage.** DocOutput grows the per-type fields; virtual syncDocOutput chain populates them per type. Instrument midiInput/muteMIDICommand deferred (M4/M5). Sound structure = separate DocSound (done A–D + E-modknobs/F/G to come).

**Placeholder scan.** Concrete overrides; the save-call-site verify (Step 6) is a real executor check.

**Type consistency.** DocOutput fields ↔ engine members (dirPath/deviceDef strings; defaultVelocity u8; channel/channelSuffix i32; collapse bools; outputMPEY/cv2Source enums → u8 underlying). Virtual chain calls parent then adds own.

**Risk.** (1) The virtual dispatch must fire at BOTH load (adoptOutput) and save (writeToFile) — Step 6 verifies the save path; adoptOutput covers load. (2) `channel` via `getChannel()` (private field). (3) `CCNumber`/`CVMode` underlying casts. (4) Nothing reads DocOutput → drift silent. (5) Ensure SoundInstrument/Kit don't need overrides (they inherit Instrument's) — confirmed.
