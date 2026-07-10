# readFromTree — LearnedMIDI conversion — Design + Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Convert `LearnedMIDI::readFromFile(Deserializer&, midiMessageType)` → tree-native `readFromTree(const Value&, Deserializer&, midiMessageType)`, convert all 7 call sites, and retire `readFromFile`. Second per-type conversion (a shared leaf), following the NoteRow pattern.

**Design:** `LearnedMIDI` reads `channel` (int), `mpeZone` (scalar "lower"/"upper"), `device` (self-descending `readDeviceReferenceFromFile`), and a message-type tag (`note`/`ccNumber`, skipped for `MIDI_MESSAGE_NONE`). Tree-native: scalars via `node.find()`, `device` via `readChildObject` (handles the self-descending device reader). `currentValue()` (valid everywhere post-2b) supplies the node at the 4 mid-stream call sites; the 3 already-bridged sites pass their node directly and drop the `readChildObject` wrapper. `readFromFile`/`readMPEZone`/the `read*FromFile` wrappers retire. `readNoteFromTree`/`readCCFromTree`/`readChannelFromTree` wrappers added.

**Tech Stack:** C++23/26. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Behaviour-preserving; golden bit-identity is the gate.** `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `./dbt test` 15/15, links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- Single fresh commit — never `--amend`.
- This REVERTS the drum.cpp `match('{')`/`match('}')` fix (`2c9b53415`) at drum's two sites — `readFromTree` reads tree-native, so the descent is no longer needed. json_roundtrip must stay `OK=38`.

---

### Task 1: `LearnedMIDI::readFromTree` + wrappers; convert 7 call sites; retire `readFromFile`

**Files:** `src/deluge/io/midi/learned_midi.{h,cpp}`; call sites `drum.cpp` (×2), `instrument_clip.cpp` (×1), `kit.cpp` (×1), `note_row.cpp` (×2), `song.cpp` (×1).

- [ ] **Step 1: Add `readFromTree` + wrappers to `learned_midi.h`.** Forward-decl `namespace deluge::storage { class Value; }`. Add:
```cpp
	void readFromTree(const deluge::storage::Value& node, Deserializer& reader, int32_t midiMessageType);
	inline void readNoteFromTree(const deluge::storage::Value& node, Deserializer& reader) {
		readFromTree(node, reader, MIDI_MESSAGE_NOTE);
	}
	inline void readCCFromTree(const deluge::storage::Value& node, Deserializer& reader) {
		readFromTree(node, reader, MIDI_MESSAGE_CC);
	}
	inline void readChannelFromTree(const deluge::storage::Value& node, Deserializer& reader) {
		readFromTree(node, reader, MIDI_MESSAGE_NONE);
	}
```

- [ ] **Step 2: Implement `readFromTree` in `learned_midi.cpp`** (add `#include "storage/value.h"` + `storage/value_tree_deserializer.h`):
```cpp
void LearnedMIDI::readFromTree(const deluge::storage::Value& node, Deserializer& reader, int32_t midiMessageType) {
	using deluge::storage::Value;
	if (const Value* n = node.find("channel")) {
		channelOrZone = stringToInt(n->str().c_str());
	}
	if (const Value* n = node.find("mpeZone")) {
		// mpeZone is a scalar "lower"/"upper" (was readMPEZone).
		if (n->str() == "lower") {
			channelOrZone = MIDI_CHANNEL_MPE_LOWER_ZONE;
		}
		else if (n->str() == "upper") {
			channelOrZone = MIDI_CHANNEL_MPE_UPPER_ZONE;
		}
	}
	if (const Value* n = node.find("device")) {
		static_cast<deluge::storage::ValueTreeDeserializer&>(reader).readChildObject(
		    *n, [&] { cable = MIDIDeviceManager::readDeviceReferenceFromFile(reader); });
	}
	if (midiMessageType != MIDI_MESSAGE_NONE) {
		if (const Value* n = node.find(getTagNameFromMIDIMessageType(midiMessageType))) {
			noteOrCC = std::min<int32_t>(stringToInt(n->str().c_str()), 127);
		}
	}
}
```

- [ ] **Step 3: Convert the 4 mid-stream call sites** (grab the node via `currentValue()`; keep the trailing `exitTag()`; REMOVE any `match('{')`/`match('}')`). Add `#include "storage/value_tree_deserializer.h"` where missing.
  - `drum.cpp` — the `midiMuteCommand` + `midiInput` handlers become:
```cpp
	else if (!strcmp(tagName, "midiMuteCommand")) {
		muteMIDICommand.readNoteFromTree(*static_cast<deluge::storage::ValueTreeDeserializer&>(reader).currentValue(),
		                                 reader);
		reader.exitTag();
	}
	else if (!strcmp(tagName, "midiInput")) {
		midiInput.readNoteFromTree(*static_cast<deluge::storage::ValueTreeDeserializer&>(reader).currentValue(),
		                           reader);
		reader.exitTag();
	}
```
  - `kit.cpp:316` `midiInput.readNoteFromFile(reader)` → `midiInput.readNoteFromTree(*static_cast<deluge::storage::ValueTreeDeserializer&>(reader).currentValue(), reader)` (keep its surrounding `match`/`exitTag` as-is EXCEPT drop any `match('{')` that wrapped ONLY this read — inspect first; if the match brackets other reads, leave them).
  - `instrument_clip.cpp:2629` `soundMidiCommand.readChannelFromFile(reader)` → `soundMidiCommand.readChannelFromTree(*static_cast<deluge::storage::ValueTreeDeserializer&>(reader).currentValue(), reader)`.

- [ ] **Step 4: Convert the 3 already-bridged call sites** (drop the `readChildObject` wrapper — the node is in hand).
  - `note_row.cpp` `muteMIDICommand`/`midiInput`:
```cpp
	if (const Value* n = node.find("muteMidiCommand")) {
		muteMIDICommand.readNoteFromTree(*n, reader);
	}
	if (const Value* n = node.find("soundMidiCommand")) {
		midiInput.readNoteFromTree(*n, reader);
	}
```
  - `song.cpp:2331` clip `muteMidiCommand`:
```cpp
	if (const deluge::storage::Value* muteNode = clipNode->find("muteMidiCommand")) {
		newClip->muteMIDICommand.readNoteFromTree(*muteNode, reader);
	}
```
(drop the `auto& treeReader = …readChildObject(…)` lines.)

- [ ] **Step 5: Retire `readFromFile` + the `read*FromFile` wrappers.** Delete `LearnedMIDI::readFromFile` (def + decl) and the `readNoteFromFile`/`readCCFromFile`/`readChannelFromFile` inline wrappers — the 7 callers are all converted above. **KEEP `readMPEZone`** — `melodic_instrument.cpp:83` still calls it directly (streaming, unconverted); `readFromTree` inlines its own scalar check rather than folding it. Re-grep after deleting to confirm zero `readFromFile`/`read*FromFile` references remain.

- [ ] **Step 6: Build firmware.** `./dbt build Debug 2>&1 | tail -1` — must link.

- [ ] **Step 7: Sim + goldens.**
```
cmake --build build-sim --target song_roundtrip edit_undo_redo param_record_diff json_roundtrip 2>&1 | tail -1
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip 2>&1 | tail -1
./dbt test 2>&1 | grep -iE "tests passed|failed" | head -1
```
Expected: `OK=16 REGRESS=0`, `OK=40`, `OK=3`, `OK=38`, `100% tests passed`, links. The `muteMidiCommand` corpus song (song_roundtrip fixture) + the problem-song device ref (json) are the coverage; a `json` FAIL means the device `readChildObject` bridge or a dropped mid-stream node.

- [ ] **Step 8: Commit.** `git commit -m "midi: LearnedMIDI::readFromTree; convert all callers; retire readFromFile (+ readMPEZone fold)"`.

## Self-Review

**Coverage.** All 7 call sites enumerated (4 mid-stream via `currentValue()`, 3 bridged). `readMPEZone` folded. Message-tag `NONE`-skip preserved. The drum `match` fix is intentionally reverted (superseded).

**Placeholder scan.** Step 5's grep-first is a real check (a stray MIDI-learn caller would block full retirement). Step 3's kit `match` note is an inspect-first instruction.

**Types.** `readFromTree(const Value&, Deserializer&, int32_t)`; wrappers inline; `find()->str()`/`stringToInt`; `readChildObject` on `ValueTreeDeserializer` (static_cast); `getTagNameFromMIDIMessageType` reused. Signature consistent h/cpp.
