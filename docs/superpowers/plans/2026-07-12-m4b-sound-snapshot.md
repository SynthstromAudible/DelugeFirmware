# M4b — SoundSnapshot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (inline).

**Goal:** Introduce `deluge::snapshot::SoundSnapshot` — the engine's first derived read-view — gathering the hot
Sound render fields, PROJECTED from `DocSound`; make the render read the snapshot; retire the scattered native
caches; eliminate the per-edit `record + native` dual-write for bound sounds.

**Architecture:** Doc-authoritative, snapshot-derived. Per field: the native member MOVES into `SoundSnapshot`; the
setter routes bound → `docSound_->field` + reproject / unbound → snapshot directly (no native write); the render
reads `snapshot_.field`. `Sound::projectSnapshot()` derives the whole snapshot from `docSound_` at the reseed hooks;
`seedDocSoundScalars` (snapshot→Doc, the bind-time seed from a loaded-while-unbound instrument) stays. Behaviour-
preserving: the snapshot holds the same value the native cache did.

**Tech Stack:** C++23; `build-sim` (x86) + `build-sim-arm` (qemu); firmware `./dbt build Debug`.

## Global Constraints

- **Behaviour-preserving, goldens BIT-IDENTICAL:** song 16 / json 38 / edit 40 / param 3 / audio_clip 1 (x86≡ARM);
  firmware links. Corpus `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`. **`param_record` +
  `song_roundtrip` are load-bearing** (they render + save/load the flipped fields).
- **Group C untouched** (`Source::dxPatch`, `Source::ranges`). `compressor.threshold` stays param-tier (not a
  snapshot field). The `LFO` runtime objects stay engine-owned (phase state).
- **Unbound sounds** (preview, `GlobalEffectable` — no `DocSound`) write the snapshot DIRECTLY; the render reads the
  snapshot UNCONDITIONALLY (no `docSound_ ? … : native` fallback — the snapshot always exists).
- **Commit discipline:** branch `deluge-project-param`; clang-format hook reformats + FAILS first commit → FRESH
  `git add -A && git commit` (never `--amend`); no Co-Authored-By.

## THE PER-FIELD RECIPE (applied by every slice)

For a hot Sound field `X` (native `Sound::x` ↔ `DocSound::x`), currently: file→native, `seedDocSoundScalars` does
`d.x = x` (native→Doc), render reads `sound.x`, `setX` dual-writes `x = v; if(docSound_) docSound_->x = v;`.

1. **Move** `Sound::x` (native member) → `SoundSnapshot::x`. Retire the native member (deleting it makes every missed
   read a COMPILE error — the safety net).
2. **`projectSnapshot()`** gains `snapshot_.x = docSound_->x;` (scalar copy; Group B runs the compute).
3. **`setX(v)`**: `if (docSound_ != nullptr) { docSound_->x = v; snapshot_.x = docSound_->x; } else { snapshot_.x = v; }`
   — bound writes the Doc then reprojects (the dual-write elimination: the snapshot write is a derivation); unbound
   writes the snapshot directly. (For Group B, the reproject/unbound branch runs the compute into the snapshot.)
4. **`seedDocSoundScalars`** line `d.x = x;` → `d.x = snapshot_.x;` (snapshot→Doc; the bind-time seed from a loaded-
   while-unbound instrument — SoundInstruments bind AFTER parse, so their loaded value sits in the snapshot until
   bind moves it into the freshly-bound Doc). KEEP this — it is the load→Doc bridge for the unbound-at-parse case.
5. **Render read sites:** `sound.x` / `sound->x` → `snapshot_.x` via a getter (add `getX()` returning
   `snapshot_.x`, or read `sound->snapshot().x`). Grep EVERY read site (disambiguate from same-named Source/holder
   fields).
6. **Load parse site** (`x = reader.read…`) → `setX(reader.read…)` so it routes through the bound/unbound setter
   (bound drum → Doc; unbound instrument → snapshot).

Net after the recipe: Doc is load+save authority; snapshot is the render projection; native is gone; no dual-write
for bound sounds; unbound sounds keep working via the direct-snapshot path.

## THE GATE (per task)
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim --target song_roundtrip json_roundtrip edit_undo_redo param_record_diff audio_clip_edit_diff edit_test 2>&1 | grep -E "error:|FAILED"; echo BUILD done
build-sim/edit_test; echo "edit_test rc=$?"
python3 tests/golden/songs_run.py | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff | tail -1
python3 tests/golden/audio_clip_edit_run.py --binary build-sim/audio_clip_edit_diff | tail -1
./dbt build Debug 2>&1 | grep -E "error:|Linking CXX exec"
```
Expected: edit_test rc=0; song 16 / json 38 / edit 40 / param 3 / audio_clip 1; firmware links.

---

### Task 1: Pilot — `SoundSnapshot` type + `projectSnapshot()` + flip `transpose`

**Files:** Create `src/deluge/model/snapshot/sound_snapshot.h`; Modify `src/deluge/processing/sound/sound.{h,cpp}`,
`src/deluge/model/voice/voice.cpp`, `src/deluge/model/clip/instrument_clip.cpp`.

**Interfaces:**
- Produces: `struct deluge::snapshot::SoundSnapshot { int16_t transpose; /* grows per slice */ };`
  `Sound::snapshot_` (member); `SoundSnapshot& Sound::snapshot()` / `int16_t Sound::getTranspose()`;
  `void Sound::projectSnapshot()`.

- [ ] **Step 1:** Create `sound_snapshot.h` — `namespace deluge::snapshot { struct SoundSnapshot { int16_t transpose{0}; }; }`
  (a plain struct; grows field-by-field in later slices). Header comment: engine-side derived read-view of the hot
  Sound render fields, projected from `DocSound`; the model never references it.

- [ ] **Step 2:** `sound.h` — add `#include "model/snapshot/sound_snapshot.h"`, add `deluge::snapshot::SoundSnapshot snapshot_{};`
  member, add `[[nodiscard]] int16_t getTranspose() const { return snapshot_.transpose; }`. DELETE `int16_t transpose = 0;`
  (`sound.h:139`).

- [ ] **Step 3:** `sound.cpp` —
  - add `void Sound::projectSnapshot()` that (this slice) does `if (docSound_ != nullptr) { snapshot_.transpose = docSound_->transpose; }`;
    call it at the END of `bindDocSound` (after `seedDocSoundScalars`, `sound.cpp:3936`+) and in `doneReadingFromFile`
    (`:3048` area, after `seedDocSoundScalars();`).
  - `setTranspose` (`:3994`) → the recipe step 3 branch (bound: `docSound_->transpose = value; snapshot_.transpose = docSound_->transpose;`
    else `snapshot_.transpose = value;`).
  - `seedDocSoundScalars` line `d.transpose = transpose;` (`:3907`) → `d.transpose = snapshot_.transpose;`.
  - load parse site `transpose = reader.readTagOrAttributeValueInt();` (`:640`) → `setTranspose(reader.readTagOrAttributeValueInt());`.
  - the ctor/default `transpose = -12;` (`:307`) → `setTranspose(-12);` (or `snapshot_.transpose = -12;` if pre-bind).
    VERIFY there are no other raw `transpose =` writes on a Sound (grep; exclude `source->transpose`, `sampleHolder.transpose`, `sources[..].transpose`).

- [ ] **Step 4:** Render read sites — `voice.cpp:227` `sound.sources[s].getRange(noteCodeAfterArpeggiation + sound.transpose)`
  → `+ sound.getTranspose()`; `voice.cpp:415` `+ sound.transpose` → `+ sound.getTranspose()`;
  `instrument_clip.cpp:4145` `if (sound->transpose || …)` → `sound->getTranspose()`. Grep to confirm these are ALL the
  `Sound::transpose` reads (the deleted member turns any miss into a build error).

- [ ] **Step 5:** THE GATE — bit-identical (the snapshot holds the same value the native did; render output
  unchanged). `song_roundtrip` exercises transpose save/load; `param_record` renders it.

- [ ] **Step 6:** Commit `feat(m4b): SoundSnapshot read-view + Sound::projectSnapshot — pilot flips transpose (render reads the snapshot, native retired)`.

---

### Task 2: Group A scalars

**Files:** `sound_snapshot.h`, `sound.{h,cpp}`, render read sites (grep per field).

Apply THE PER-FIELD RECIPE to each, one field or small batch per commit (each its own GATE):
`synthMode` (`sound.h:157`↔`synth_mode`; MANY read branches — `sound.cpp:338,1447-1553,1592,1695,2010-2214`,
`getSynthMode()`; do this one alone), `oscillatorSync` (`:164`, read `sound.cpp:2195`), `voicePriority` (`:169`, read
`voice.cpp:2574`), `modulatorTranspose[2]` (`:149`, read `voice.cpp:529`), `modulator1ToModulator0` (`:158`),
`oscRetriggerPhase[2]` (`:201`, read `voice_unison_part_source.cpp:79`), `modulatorRetriggerPhase[2]` (`:202`).
For array fields the snapshot holds the array; projectSnapshot loops. Each retires its native member.

- [ ] Per field: recipe steps 1-4 (+ getter), grep ALL read sites, GATE, commit
  `feat(m4b): flip <field> onto SoundSnapshot`.

---

### Task 3: `lfoConfig[]`

**Files:** `sound_snapshot.h`, `sound.{h,cpp}`, `voice.cpp`.

`lfoConfig[LFO_COUNT]` (`sound.h:115` ↔ `doc_sound.h:54-56` triplet `lfo_wave_type/lfo_sync_type/lfo_sync_level`).
Cheap flip (plain enums) but MANY read sites: `voice.cpp:149-152,700,762,772`, `sound.cpp:2467,2480,2823,2860,1553-1557`.
The snapshot holds `LFOConfig lfoConfig[LFO_COUNT]`; projectSnapshot copies the triplet from the Doc per index; the
`setLfoWaveType`/`setLfoSync` setters (`sound.h:189-190`) take the recipe branch. The `LFO` runtime objects
(`globalLFO1/3`, per-voice) are NOT touched.

- [ ] Recipe + grep all read sites + GATE + commit `feat(m4b): flip lfoConfig[] onto SoundSnapshot`.

---

### Task 4: Group B computed — `unisonDetuners[]` + `modulatorTransposers[]`

**Files:** `sound_snapshot.h`, `sound.{h,cpp}`, `voice.cpp`.

The `PhaseIncrementFineTuner` arrays move INTO the snapshot; `projectSnapshot` runs the SETUP (not a scalar copy):
- `unisonDetuners[kMaxNumVoicesUnison]` (`sound.h:154`) — `projectSnapshot` calls the `setupUnisonDetuners()` compute
  (`sound.cpp:3066`) reading `docSound_->{num_unison,unison_detune,unison_stereo_spread}` INTO `snapshot_.unisonDetuners`.
  Read sites `voice.cpp:515,559`. `setNumUnison`/`setUnisonDetune` (`:3206,3274`) reproject instead of re-running
  setup on the native.
- `modulatorTransposers[kNumModulators]` (`sound.h:152`) — `projectSnapshot` runs `recalculateModulatorTransposer()`
  (`sound.cpp:3182`) from `docSound_->modulator_cents[]` into the snapshot. Read `voice.cpp:549`. `setModulatorCents`
  (`:3178`) reprojects.

Retire the native arrays + the standalone `setupUnisonDetuners`/`recalculateModulatorTransposer` as separate methods
(fold into `projectSnapshot` or keep as private helpers writing the snapshot). GATE (this is the deepest render
check — `param_record` over unison/FM presets).

- [ ] Recipe (compute variant) + GATE + commit `feat(m4b): flip unisonDetuners + modulatorTransposers onto SoundSnapshot (computed projection)`.

---

### Task 5: `ModControllableSnapshot`

**Files:** Create `src/deluge/model/snapshot/mod_controllable_snapshot.h`; Modify
`src/deluge/model/mod_controllable/mod_controllable_audio.{h,cpp}` + read sites.

Same pattern, own owner: `ModControllableSnapshot` on `ModControllableAudio` (shared by Sound + GlobalEffectable,
both carry `docModControllable_`). Fields: `lpf_mode`/`hpf_mode`/`filter_route`/`mod_fx_type`/`clipping_amount` +
`delay`/`sidechain`/`stutter` sub-structs + the 5 non-threshold compressor knobs (`doc_mod_controllable.h:28-61`).
`projectModControllableSnapshot()` from `docModControllable_` at `bindDocModControllable` + `doneReadingFromFile`
(`sound.cpp:3050`). Threshold EXCLUDED (param-derived, `mod_controllable_audio.cpp:651-657`).

- [ ] Recipe per field group + grep read sites + GATE + commit
  `feat(m4b): ModControllableSnapshot — flip the DocModControllable scalar caches onto a snapshot`.

---

## Notes / Risks
- **Unbound sounds must still render.** After each slice, `param_record` / `song_roundtrip` over kits with
  GlobalEffectable affect-entire + any preview path must stay green — the direct-snapshot-write path is what keeps
  them working with no Doc.
- **Same-named fields.** `transpose`/`cents`/`retrigger`/etc. also exist on `Source` and `SampleHolder`; grep with
  member qualification and exclude those — only `Sound::x` reads flip.
- **projectSnapshot ordering.** Call it AFTER `seedDocSoundScalars` at load hooks (seed writes Doc from the loaded
  snapshot for unbound-at-parse instruments; then project re-derives the snapshot from the now-populated Doc — a
  round-trip that leaves both consistent). For bound drums the Doc is filled at parse, project fills the snapshot.
- **Deepest check is Task 4** (computed `PhaseIncrementFineTuner`) — if `param_record` diverges there, the compute's
  input (`docSound_->num_unison` etc.) or its call timing is the suspect.
