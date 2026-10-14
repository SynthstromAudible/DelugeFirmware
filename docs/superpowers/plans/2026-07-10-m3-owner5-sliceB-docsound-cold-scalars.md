# M3 owner-5 Slice B — DocSound (nested) + cold scalars — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Introduce a portable `DocSound` NESTED (as `std::optional<DocSound>`) inside both `DocOutput` and `DocDrum`, bind the engine `Sound`'s `docSound_` pointer to whichever parent owns it, and rebuild-at-save the Sound-level COLD scalar fields. Additive shadow → golden bit-identical. Second slice of owner-5.

**Architecture:** A `Sound` is owned 1:1 by a `SoundInstrument` (Output, type SYNTH) or a `SoundDrum` (Drum, type SOUND). So `DocSound` nests: `DocOutput.sound` (`std::optional<DocSound>`) for SoundInstruments, `DocDrum.sound` for SoundDrums. `Sound` gains `docSound_` (non-owning, into the parent's optional — stable address since DocOutput/DocDrum live in node-stable maps). Binding rides the existing `adoptOutput` (SYNTH → emplace parent's `sound` + bind) and `adoptDrum` (SOUND → same). No separate store, no separate erase (the optional dies with its parent record). `Sound::syncDocSound()` copies the cold scalars engine→record at save (rebuild-at-save; load seeds via adopt). This slice = the 6 cold scalars only; hot scalars (C), LFO (D), modKnobs (E), ModControllableAudio (F), sources (G) are later slices growing `DocSound`.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL / additive.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `doc_sound.h` portable — `<cstdint>` + `<optional>` (no `<string>` needed yet). `DocOutput`/`DocDrum` gain `#include "deluge-project/doc_sound.h"` + `#include <optional>`.
- `DocSound` WRITTEN, never READ this slice. Engine `Sound` stays authoritative (esp. the render hot path — none of THESE fields are hot, but keep the discipline).
- Idiomatic C++23. **NEVER `git commit --amend`** (clang-format hook → FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Field map (from recon) — Slice B cold scalars only

On `Sound` (`src/deluge/processing/sound/sound.h`, verify exact names/types): `voicePriority` (`VoicePriority` enum, sound.h:145), `maxVoiceCount` (uint8_t, :116), `sideChainSendLevel` (int32_t, :113), `outputMidiChannel` (uint8_t, :125), `outputMidiNoteForDrum` (uint8_t, :126), `oscillatorSync` (bool, :143). (NOT the hot scalars — polyphonic/synthMode/transpose/numUnison/etc. are Slice C. NOT `defaultArpSettings` — compound, later.)

`Sound` ownership: `SoundInstrument : public Sound, public MelodicInstrument` (Output, `OutputType::SYNTH`); `SoundDrum : public Sound, public Drum` (`DrumType::SOUND`). Detect via the `type` enum (no dynamic_cast). `adoptOutput` (owner-5 Slice A, song.cpp) + `adoptDrum` (owner-3, song.cpp) are the bind points.

---

### Task 1: `DocSound` + nest in DocOutput/DocDrum + `Sound::docSound_` (additive, unused)

**Files:** `src/deluge-project/doc_sound.h` (new), `doc_output.h` + `doc_drum.h` (nest optional), `src/deluge/processing/sound/sound.h` (pointer + bind).

- [ ] **Step 1: `DocSound`.** Create `src/deluge-project/doc_sound.h` (copyright header, `<cstdint>` only):
```cpp
#pragma once
#include <cstdint>
namespace deluge::project {
/// The DOM-owned structural record for one engine Sound (M3 owner-5). NESTED as std::optional<DocSound> inside the
/// owner's DocOutput (SoundInstrument) or DocDrum (SoundDrum) — a Sound is owned 1:1. Params are already in the DOM
/// param tier; this holds Sound's NON-param structure. Slice B = cold scalars; later slices grow it (hot scalars,
/// LFO, modKnobs, sources). Enums stored as underlying values.
struct DocSound {
	uint8_t voice_priority{0};
	uint8_t max_voice_count{0};
	int32_t side_chain_send_level{0};
	uint8_t output_midi_channel{0};
	uint8_t output_midi_note_for_drum{0};
	bool oscillator_sync{false};
};
} // namespace deluge::project
```
- [ ] **Step 2: nest in DocOutput + DocDrum.** In `doc_output.h` and `doc_drum.h`: add `#include "deluge-project/doc_sound.h"` + `#include <optional>`, and a member `std::optional<DocSound> sound;` on each struct. (Default = nullopt; only SYNTH outputs / SOUND drums populate it.)
- [ ] **Step 3: `Sound::docSound_`.** In `sound.h`: forward-declare `namespace deluge::project { struct DocSound; }`; add public `deluge::project::DocSound* docSound_{nullptr};` + `void bindDocSound(deluge::project::DocSound* d) { docSound_ = d; }`.
- [ ] **Step 4: Build + full gate** (block in Task 2 Step 5). All UNCHANGED (unused). WATCH: `Sound` is a huge, layout-sensitive class — adding a pointer member must not perturb any golden (it shouldn't; it's additive), but if a golden shifts, a layout-dependent uninit read exists — investigate rather than paper over.
- [ ] **Step 5: Commit.** `git commit -m "sound: DocSound nested in DocOutput/DocDrum + Sound::docSound_ pointer (owner-5 slice B data structures, unused)"`.

---

### Task 2: bind via adoptOutput/adoptDrum + `syncDocSound` rebuild-at-save

**Files:** `src/deluge/model/song/song.cpp` (adoptOutput/adoptDrum bind), `src/deluge/processing/sound/sound.cpp`/`.h` (syncDocSound + save call).

- [ ] **Step 1: `Sound::syncDocSound()`.** In `sound.cpp` (declare in sound.h; include `deluge-project/doc_sound.h`):
```cpp
void Sound::syncDocSound() {
	if (docSound_ == nullptr) { return; }
	deluge::project::DocSound& d = *docSound_;
	d.voice_priority = static_cast<uint8_t>(voicePriority);
	d.max_voice_count = maxVoiceCount;
	d.side_chain_send_level = sideChainSendLevel;
	d.output_midi_channel = outputMidiChannel;
	d.output_midi_note_for_drum = outputMidiNoteForDrum;
	d.oscillator_sync = oscillatorSync;
}
```
(Verify exact engine field names/types in sound.h.)
- [ ] **Step 2: bind in `adoptOutput`.** In `Song::adoptOutput` (song.cpp, Slice A), after binding the DocOutput: if `output->type == OutputType::SYNTH`, `d.sound.emplace();` (on the DocOutput `d`) then get the Sound subobject (`static_cast<Sound*>(static_cast<SoundInstrument*>(output))`) and `sound->bindDocSound(&*d.sound);` then `sound->syncDocSound();` to seed. (Include the SoundInstrument header in song.cpp.)
- [ ] **Step 3: bind in `adoptDrum`.** In `Song::adoptDrum` (song.cpp, owner-3), after binding the DocDrum: if `drum->type == DrumType::SOUND`, `d.sound.emplace();` then `static_cast<Sound*>(static_cast<SoundDrum*>(drum))->bindDocSound(&*d.sound)` + `syncDocSound()`. (SoundDrum header in song.cpp.)
- [ ] **Step 4: rebuild-at-save.** At the START of `Sound::writeToFile` (sound.cpp:~3822), call `syncDocSound();` (captures runtime mutations; self-guards when unbound). (No separate load call — adopt's seed at assignMissingIds covers load, and the writeToFile sync covers save; mirrors the DocOutput/DocAudioClip pattern.)
- [ ] **Step 5: Build + full gate.**
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
Expected all UNCHANGED. A REGRESS = the sync read a wrong field, the bind cast is wrong (SoundInstrument/SoundDrum MI cast), or a layout perturbation. The MI cast is the subtle spot: `static_cast<Sound*>(static_cast<SoundInstrument*>(output))` must land on the correct `Sound` subobject.
- [ ] **Step 6: Commit.** `git commit -m "sound: bind DocSound via adoptOutput/adoptDrum + syncDocSound cold scalars rebuild-at-save (owner-5 slice B; shadow)"`.

---

## Self-Review

**Spec coverage.** Task 1 = DocSound + nesting + pointer; Task 2 = the bind (both owner types) + cold-scalar rebuild-at-save. Nesting (not a separate store) chosen for the 1:1 ownership — dies with the parent, no orphan/erase. Slice B = 6 cold scalars; hot scalars/LFO/modKnobs/ModControllableAudio/sources are C–G.

**Placeholder scan.** Concrete DocSound + syncDocSound + the adopt binds; the MI cast is spelled out. No TODOs.

**Type consistency.** DocSound scalars ↔ Sound members (enums as u8; sideChainSendLevel i32; the two output-midi u8; oscillatorSync bool). `std::optional<DocSound>` nested; `docSound_` raw non-owning into the optional (stable via node-based map). Bind dispatched by `type` enum, not dynamic_cast.

**Risk.** (1) The MI static_cast to the `Sound` subobject — get the double-cast right (`SoundInstrument`/`SoundDrum` → `Sound`). (2) `Sound` is the largest engine class — the added pointer is additive but watch for any golden shift (would signal a latent layout-dependent uninit read). (3) Nested-optional address stability — the parent DocOutput/DocDrum live in `fast_map` (node-based `std::map`), so `&*d.sound` is stable across other inserts. Nothing reads DocSound yet → drift silent this slice.
