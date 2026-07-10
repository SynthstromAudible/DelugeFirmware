# M3 owner-3 Slice 3 — addDrum mis-binding fix + read-flip (name/channel/note) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** (Task 1) Fix the `Kit::addDrum`→`currentSong` adopt mis-binding so drums bind into the song that owns the kit, not ambient `currentSong`. (Tasks 2-4) Flip the read authority of the three shadowed drum structure fields — `drumName`, `channel`, `note` — onto `DocDrum`, using the nullable-fallback accessor. Their dual-writes already exist (setName / modChange).

**Architecture:** Task 1 threads the correct `Song*` into `addDrum`; the adopt is idempotent so this is golden-neutral (the drum store isn't serialized). Tasks 2-4 use owner-1 slice-3b's pattern: per field, private `field_` + `field()` getter returning `docDrum_ ? docDrum_->docField : field_` + setter writing both (setters already exist from slice 1/2). Bound drums read DOM, unbound read the engine field. `DocDrum` lives on the base `Drum`, so `NonAudioDrum::channel()`/`MIDIDrum::note()` read `docDrum_->channel`/`->note`.

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL is the gate, per task.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- The engine field stays a synced fallback (setter writes both). Retirement is later.
- Idiomatic C++23. **NEVER `git commit --amend`** (clang-format hook → FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Site map (verified)

- `Kit::addDrum(Drum*)` def `kit.cpp:434` (adopt at `:447` `currentSong->adoptDrum(newDrum)`). Callers: LOAD — `kit.cpp:371` (in `readDrumFromFile`, has `Song* song`), `instrument_clip.cpp:4041` (reconnection, in `InstrumentClip::readFromFile` — has a loading `Song*`; find its param/var name). RUNTIME — `sample_browser.cpp:1948`, `slicer.cpp:686`, `load_instrument_preset_ui.cpp:1082`, `add_kit_row.cpp:16`, `instrument_clip_view.cpp:5531`.
- `Song::adoptDrum(Drum*)` `song.cpp:1218` (idempotent). `assignMissingIds` adopt `song.cpp:1208`.
- `Drum::drumName` `std::string` `drum.h:54`; `Drum::setName` (slice 2, drum.cpp) already dual-writes. `Drum::getColourOffset`-style getters: none for name yet.
- `NonAudioDrum::channel` `uint8_t` `non_audio_drum.h:48`; dual-write in `NonAudioDrum::modChange`.
- `MIDIDrum::note` `uint8_t` `midi_drum.h:46`; dual-write in the MIDIDrum modChange path.
- `DocDrum {drum_type, channel, note, name}` `doc_drum.h`; `docDrum_` on `Drum`.

---

### Task 1: Fix the addDrum adopt mis-binding

- [ ] **Step 1: thread the song.** Change `Kit::addDrum(Drum* newDrum)` → `Kit::addDrum(Drum* newDrum, Song* adoptSong = nullptr)` (update decl in `kit.h`). In the body, replace `currentSong->adoptDrum(newDrum)` with:
```cpp
	Song* songForAdopt = (adoptSong != nullptr) ? adoptSong : currentSong;
	if (songForAdopt != nullptr) { songForAdopt->adoptDrum(newDrum); }
```
- [ ] **Step 2: load callers pass the loading song.** At `kit.cpp:371` (inside `readDrumFromFile`, which has `Song* song`): `addDrum(newDrum, song)`. At `instrument_clip.cpp:4041` (reconnection, inside `InstrumentClip::readFromFile` — locate its `Song*` param/local): `kit->addDrum(newDrum, <thatSong>)`. Runtime callers stay `addDrum(newDrum)` (default → currentSong, correct at runtime).
- [ ] **Step 3: Build + full gate** (block in Task 4 Step). All UNCHANGED — the drum store isn't serialized, so which song's store gets the entry can't change file bytes; this is a latent-correctness fix, golden-neutral. (If it REGRESSES, something DOES read the store — investigate.)
- [ ] **Step 4: Commit.** `git commit -m "drum: adopt into the kit's loading song, not currentSong, in Kit::addDrum (owner-3 fix; unblocks read-flip)"`.

---

### Task 2: Read-flip `drumName`

- [ ] **Step 1: encapsulate.** In `drum.h`: `std::string drumName{}` → private `std::string drumName_{}`. Add `const std::string& name() const { ... }` — BUT the DOM/engine may differ in type only by storage; return by value if a fallback branch is needed: `std::string name() const { return docDrum_ ? docDrum_->name : drumName_; }` (docDrum_ needs complete type → if `name()` can't be inline in drum.h with only the fwd-decl, put it in drum.cpp; a `const std::string&` returning ref can't span the ternary cleanly, so return by value `std::string`). `setName` (slice 2) already writes `drumName_` + `docDrum_->name` — update its body to the renamed `drumName_`.
- [ ] **Step 2: drive reads.** Convert every READ of `drumName` (now `drumName_`) OUTSIDE the class to `name()`: the serializer `writeDrumTagsToFile` `drumName.c_str()` → `name().c_str()`, comparisons, `getName`-like uses, `drawDrumName` (the MEMBER, not the local `std::string drumName` in instrument_clip_view — leave that local). Internal-to-Drum reads may use `drumName_` directly. Audit `grep -rn "drumName" src/deluge` — every remaining bare `drumName` is `drumName_` (internal), `name()`, the local var, or the `DocDrum::name`/`setName` plumbing.
- [ ] **Step 2b: Build + full gate. Commit** `git commit -m "drum: read-flip drumName onto DocDrum::name (owner-3 slice 3)"`.

---

### Task 3: Read-flip `channel`

- [ ] **Step 1: encapsulate.** In `non_audio_drum.h`: `uint8_t channel{}` → private `uint8_t channel_{}`. Add `uint8_t channel() const { return docDrum_ ? docDrum_->channel : channel_; }` + `void setChannel(uint8_t v) { channel_ = v; if (docDrum_) { docDrum_->channel = v; } }`. Route the `modChange` channel write + any ctor/read-from-file default through `setChannel`/`channel_`; the existing modChange dual-write becomes the setter body (dedupe). (`docDrum_` complete type → put non-inline bodies in non_audio_drum.cpp.)
- [ ] **Step 2: drive reads.** Convert external `->channel`/`.channel` reads on a NonAudioDrum (MIDI/Gate) to `channel()`: serializer (`writeAttribute("channel", channel)` / gateOutput), `getGateDrumForChannel` comparisons, MIDI-routing reads, etc. WATCH: `channel` is a common identifier — only the `NonAudioDrum` member (grep in non_audio_drum/midi_drum/gate_drum/kit + MIDI routing; exclude unrelated `channel` locals/params, MIDIDevice channels, etc.).
- [ ] **Step 2b: Build + full gate. Commit** `git commit -m "drum: read-flip NonAudioDrum::channel onto DocDrum::channel (owner-3 slice 3)"`.

---

### Task 4: Read-flip `note`

- [ ] **Step 1: encapsulate.** In `midi_drum.h`: `uint8_t note` → private `uint8_t note_`. Add `uint8_t note() const { return docDrum_ ? docDrum_->note : note_; }` + `void setNote(uint8_t v) { note_ = v; if (docDrum_) { docDrum_->note = v; } }`. Route the modChange note write + ctor/read defaults through it.
- [ ] **Step 2: drive reads.** Convert external `->note`/`.note` reads on a MIDIDrum to `note()`: serializer (`writeAttribute("note", note)`), MIDI send/routing. WATCH: `note` is extremely common — restrict to the `MIDIDrum` member only (grep midi_drum + wherever a `MIDIDrum*` reads `->note`); exclude `Note` objects, note-row `note`, MIDI message `note` locals, etc.
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
Expected all UNCHANGED. A REGRESS = a read missed the accessor (stale) or the encapsulation caught a same-named unrelated field. 
- [ ] **Step 4: Commit.** `git commit -m "drum: read-flip MIDIDrum::note onto DocDrum::note (owner-3 slice 3; all shadowed drum fields now DOM-read-authoritative)"`.

---

## Self-Review

**Spec coverage.** Task 1 = the mis-binding fix (prerequisite). Tasks 2-4 = read-flip the 3 shadowed fields (name/channel/note; drum_type is immutable, captured at bind). After this, all shadowed owner-3 fields are DOM-read-authoritative. drum_id (the join) is a shadow that flips with the save/load slice; MIDI-learn / SoundDrum path / save-load flip remain.

**Placeholder scan.** Concrete accessors; the reconnection site's song var is a locate-by-context step. Read-flip conversions cite grep-scoped disambiguation.

**Type consistency.** Getters return the engine field type; setters write both. `channel`/`note` u8, `name` std::string (by value to span the ternary). `DocDrum` on base Drum; NonAudioDrum/MIDIDrum accessors reach `docDrum_->channel/note`.

**Risk.** (1) `channel`/`note` are very common identifiers — the biggest risk is encapsulation catching an unrelated same-named field/local (compile error → caught) or a read missing the accessor (behaviour drift → golden REGRESS). Scope each grep to the drum subclass member. (2) `name()` returns by value (ternary can't return a ref cleanly) — a minor perf cost on the cold name path, acceptable. (3) Task 1 golden-neutral — if it regresses, something reads the drum store (shouldn't this slice).
