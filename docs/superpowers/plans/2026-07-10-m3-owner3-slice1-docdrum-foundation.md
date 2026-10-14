# M3 owner-3 Slice 1 — DocDrum foundation + funneled-scalar dual-write — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Land the DOM record for kit/drum STRUCTURE (`DocDrum`) + its store + binding lifecycle + dual-write of the cleanly-funneled scalars (`channel`, `note`), as an additive shadow (nothing reads it → golden bit-identical). First slice of owner-3, mirroring owner-2 Slice 1's shape and owner-1's `ClipStructure`/`adoptClip` machinery.

**Architecture:** A portable flat `DocDrum` value type lives in a GLOBAL `DrumStructure` map keyed by `Drum::id()` (like `ClipStructure` but keyed by drum id, matching the param tier's `OwnerId::drum`). Each engine `Drum` holds a non-owning `docDrum_` pointer. Unlike `Clip`, a `Drum` has NO id at construction (minted lazily), so binding is NOT ctor-time: a `Song::adoptDrum(Drum*)` (idempotent: mint-id-if-needed → getOrCreate the DocDrum → seed → bind) is hooked at `assignMissingIds` (covers load) + runtime `Kit::addDrum`; the store entry is erased on drum removal. `channel`/`note` dual-write through the single `NonAudioDrum::modChange` funnel; `drum_type` is ctor-set/immutable so it's captured once at bind.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL / additive.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`. `DocDrum` is WRITTEN but never READ this slice — a REGRESS means a dual-write leaked into engine state or a hook corrupted the drum list / id lifecycle.
- `doc_drum.h` MUST stay portable — `<cstdint>` only, no engine includes (mirror `doc_clip.h`/`doc_note_row.h`).
- NEVER mint a drum id where the existing code wouldn't have (id lifecycle must stay golden-neutral — session/serial id assignment is shared infra; `adoptDrum`'s mint must reuse `Song::generateNewDrumId()` and only fire where `assignMissingIds` already would).
- Idiomatic C++23.
- **NEVER `git commit --amend`** (clang-format hook reformats+fails the first commit → recover with a FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Reference — mirror these existing files (owner-1 machinery)

- `src/deluge-project/clip_structure.h` — `ClipStructure` = a `deluge::fast_map<uint32_t, DocClip> by_id` with `find`/`getOrCreate`/`remove`. **Copy this shape verbatim for `DrumStructure` (DocDrum).**
- `src/deluge-project/doc_clip.h` / `doc_note_row.h` — portable value-type headers (copyright + `<cstdint>` + namespace `deluge::project`). Mirror for `doc_drum.h`.
- `Song::adoptClip(Clip*)` and `Song::removeDocClip(...)` (engine `song.cpp`; project `song`) — the idempotent bind + the teardown erase. **Copy this shape for `adoptDrum`/`removeDocDrum`.** Find them by grep.
- Where `clip_structure` + `session_clip_structure` are declared on the project/Song and constructed — declare `drum_structure` alongside (drums need only ONE store — global by durable id, no session/serial twin, since every drum gets an id via `assignMissingIds`).
- `Song::assignMissingIds()` (`song.cpp:~1190-1206`) — already iterates drums assigning ids; this is where to also `adoptDrum` each.

## Drum facts (from recon)

- Hierarchy: `Drum` (base, `drum.h:45`) → `NonAudioDrum` (`non_audio_drum.h:26`) → `MIDIDrum`/`GateDrum`; and `SoundDrum` (`: Sound, Drum`). `DrumType {SOUND, MIDI, GATE}` (`definitions_cxx.hpp:747`).
- Base `Drum`: `DrumType type` (const, ctor-set, `drum.h:53`), `std::optional<uint32_t> id_` + `id()`/`setId()` (`drum.h:65,94-95`) — GENERIC to all kinds. `Drum* next` intrusive list (`drum.h:63`).
- `NonAudioDrum::channel` (uint8_t, `non_audio_drum.h:48`) — MIDI channel (MIDIDrum) or gate channel (GateDrum). `MIDIDrum::note` (uint8_t, `midi_drum.h:46`). Both mutated ONLY via `NonAudioDrum::modChange` (`non_audio_drum.cpp:47-85`), invoked from `NonAudioDrum::modEncoderAction` (channel) + `MIDIDrum::modEncoderAction` (note, `midi_drum.cpp:165-176`).
- Kit owns drums via intrusive list `Kit::firstDrum` (`kit.h:149`) + `Drum::next`. `Kit::addDrum` (`kit.cpp:434-442`), `Kit::removeDrum`→`drumRemoved` (`kit.cpp:448-474`). `Song::generateNewDrumId()` (`song.h:268`).

---

### Task 1: `DocDrum` + `DrumStructure` + Song wiring (additive, unused)

**Files:** `src/deluge-project/doc_drum.h` (new), `src/deluge-project/drum_structure.h` (new), the project/Song header where `clip_structure` is declared (add `drum_structure`).

- [ ] **Step 1: `DocDrum`.** Create `src/deluge-project/doc_drum.h` (copyright header like doc_note_row.h), `<cstdint>` only:
```cpp
#pragma once
#include <cstdint>
namespace deluge::project {
/// The DOM-owned structural record for one engine Drum (M3 owner-3 — kit/drum structure). Kept in `DrumStructure`
/// keyed by the drum's durable `Drum::id()`. `drum_type` is the DrumType underlying value (0=SOUND,1=MIDI,2=GATE);
/// `channel`/`note` apply to the non-audio kinds (MIDI channel or gate channel; MIDI note) and are 0 for SOUND.
/// Engine-light: enums stored as underlying values, no engine includes.
struct DocDrum {
	uint8_t drum_type{0};
	uint8_t channel{0};
	uint8_t note{0};
};
} // namespace deluge::project
```
- [ ] **Step 2: `DrumStructure`.** Create `src/deluge-project/drum_structure.h` by copying `clip_structure.h`'s exact shape but over `DocDrum` (map `deluge::fast_map<uint32_t, DocDrum> by_id;` + `find(id)`, `getOrCreate(id)`, `remove(id)` with the same signatures/semantics as ClipStructure). Include `deluge-project/doc_drum.h`.
- [ ] **Step 3: wire into the project.** Where `ClipStructure clip_structure` (+ `session_clip_structure`) is declared on the project model / Song, add a single `DrumStructure drum_structure;` (NO session twin). Include `deluge-project/drum_structure.h`. Ensure it's default-constructed like the clip stores.
- [ ] **Step 4: Build + full gate** (command block in Task 2 Step 7). All UNCHANGED (the structures are unused; the new Song member must not perturb serialization — ClipStructure's members aren't written, mirror that).
- [ ] **Step 5: Commit.** `git commit -m "drum: DocDrum + DrumStructure store + Song.drum_structure (owner-3 slice 1 data structures, unused)"`.

---

### Task 2: `Drum::docDrum_` + adoptDrum binding + erase + dual-write channel/note

**Files:** `src/deluge/model/drum/drum.h` (pointer + bind), `src/deluge/model/song/song.h`/`song.cpp` (`adoptDrum`/`removeDocDrum` + assignMissingIds hook), `src/deluge/model/instrument/kit.cpp` (addDrum + removeDrum hooks), `src/deluge/model/drum/non_audio_drum.cpp` (channel/note dual-write).

- [ ] **Step 1: pointer on Drum.** In `drum.h`: forward-declare `namespace deluge::project { struct DocDrum; }`; add public `deluge::project::DocDrum* docDrum_{nullptr};` + `void bindDocDrum(deluge::project::DocDrum* d) { docDrum_ = d; }`. (Non-owning; nothing reads it yet. Do NOT copy it in any Drum copy path — drums aren't value-copied like NoteRow, but confirm no clone carries it.)
- [ ] **Step 2: `Song::adoptDrum(Drum*)`.** Mirror `adoptClip`: idempotent (early-return if already bound, i.e. `drum->docDrum_ != nullptr`); if `!drum->id().has_value()` mint via `generateNewDrumId()` + `setId`; `DocDrum& d = project.drum_structure.getOrCreate(*drum->id());` seed it — `d.drum_type = static_cast<uint8_t>(drum->type); if (drum->type != DrumType::SOUND) { d.channel = static_cast<NonAudioDrum*>(drum)->channel; } if (drum->type == DrumType::MIDI) { d.note = static_cast<MIDIDrum*>(drum)->note; }` — then `drum->bindDocDrum(&d)`. (Declare in song.h; include the drum subclass headers in song.cpp as needed.)
- [ ] **Step 3: hook `assignMissingIds`.** In `Song::assignMissingIds()` (the loop that assigns drum ids, `song.cpp:~1190-1206`), after each drum has an id, call `adoptDrum(drum)`. (adoptDrum is idempotent + mints if needed, so it composes with whatever id logic is there — but do NOT double-mint; if the loop already assigns the id, adoptDrum will see it and skip the mint.)
- [ ] **Step 4: hook runtime `Kit::addDrum`.** After a drum is added at runtime, `currentSong->adoptDrum(drum)` (guard `currentSong != nullptr`, like adoptClip's callers). This covers runtime-created drums (the load path routes through assignMissingIds, so addDrum's hook is for runtime; harmless-idempotent if both fire).
- [ ] **Step 5: erase on removal.** Add `Song::removeDocDrum(Drum*)` mirroring `removeDocClip` — `if (drum->id()) project.drum_structure.remove(*drum->id());`. Call it from `Kit::removeDrum`/`drumRemoved` (`kit.cpp:448-474`) when a drum is genuinely destroyed, guarded `currentSong && drum->docDrum_`. (Match removeDocClip's teardown guard so whole-song destruction doesn't do per-drum removal.)
- [ ] **Step 6: dual-write channel/note.** In `NonAudioDrum::modChange` (`non_audio_drum.cpp:47-85`), after the engine `channel` write, add `if (docDrum_ != nullptr) { docDrum_->channel = channel; }`. In `MIDIDrum`'s note path (the `modChange`/`modEncoderAction` that sets `note`, `midi_drum.cpp:165-176`), after the engine `note` write, `if (docDrum_ != nullptr) { docDrum_->note = note; }`. (Guarded non-null; write-only.)
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
Expected: firmware links; `OK=16/40/3/38`; `edit_test: OK`; `param_automation projection: OK`; `./dbt test` 15/15. A REGRESS = adoptDrum minted an id where the baseline didn't (id lifecycle perturbed → serialization diff), or a dual-write/hook touched engine state. The MOST LIKELY failure is the id-mint timing in `assignMissingIds` — verify adoptDrum does NOT mint before/differently than the existing loop.
- [ ] **Step 8: Commit.** `git commit -m "drum: bind DocDrum via adoptDrum (assignMissingIds/addDrum) + erase on remove + dual-write channel/note (owner-3 slice 1; shadow, no read-flip)"`.

---

## Self-Review

**Spec coverage.** Task 1 = DocDrum + DrumStructure + wiring; Task 2 = the pointer, the adopt/erase binding lifecycle, and the funneled-scalar dual-write. Scope matches the chosen "infra + funneled scalars": name / MIDI-learn / SoundDrum path / the drum_id join / save-load flip are OUT (later slices).

**Placeholder scan.** Concrete DocDrum/adoptDrum code; DrumStructure/adoptDrum/removeDocDrum explicitly say "mirror the existing ClipStructure/adoptClip/removeDocClip" (the executor reads those exact files) — a directed mirror, not a TODO.

**Type consistency.** `DocDrum` scalars are `uint8_t` matching engine `channel`/`note` (u8) + `drum_type` (DrumType underlying). Keyed by `uint32_t` drum id (matching `Drum::id()`'s `optional<uint32_t>`). `adoptDrum` casts on `drum->type` to reach subclass fields. Global store (no session twin), unlike ClipStructure's twin.

**Risk.** (1) Id-mint timing — `adoptDrum` must reuse `assignMissingIds`'s minting, not add a second/earlier mint (would shift serialized ids → golden REGRESS). Idempotent + id-check guards this; the gate catches it. (2) The teardown erase must match `removeDocClip`'s guard (skip per-drum removal during whole-song destruction) to avoid a dangle/double-free. (3) SoundDrum has no channel/note — adoptDrum seeds them 0 via the type guard; correct. Nothing reads DocDrum yet, so drift is silent this slice (surfaces at the read-flip, like owner-2).
