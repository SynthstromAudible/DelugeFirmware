# readFromTree slice 2 — loopLength (complete the DocClip shell) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or subagent-driven-development) to implement this plan. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Move `length`/`trackLength` (loopLength) into the tree-native clip read, completing the persistent DocClip shell and emptying the last shell handler out of `readFromFile`.

**Architecture:** `readClipsFromFile` reads `length` from the clip `Value` node (setting `loopLength` + `originalLength`); both `readFromFile` bodies init `readAutomationUpToPos` from `loopLength()`; the `length` handler is deleted from `Clip::readTagFromFile`. Behaviour-preserving; the bit-identity golden is the gate.

**Tech Stack:** C++23/26. Goldens via `python3 tests/golden/<name>_run.py`; firmware via `./dbt build Debug`; specs via `./dbt test`.

## Global Constraints

- **Behaviour-preserving; golden bit-identity is the gate.** Success = `song_roundtrip` `OK=15 REGRESS=0`, `edit_undo_redo` `OK=40`, `param_record` `OK=3`, `json_roundtrip` fixtures 14/14 + BYO backup `OK=37 FAIL=0`, `./dbt test` 15/15, `./dbt build Debug` links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- Single fresh `git commit` — never `--amend` (clang-format hook reformats+fails; recover with a NEW commit).

---

### Task 1: loopLength → tree-native; delete the `length` handler

**Files:** Modify `src/deluge/model/song/song.cpp` (`readClipsFromFile` shell block, ~2267), `src/deluge/model/clip/instrument_clip.cpp:2504`, `src/deluge/model/clip/audio_clip.cpp:1123`, `src/deluge/model/clip/clip.cpp` (delete the `length` handler at ~743).

- [ ] **Step 1: Read length in the loader.** In `src/deluge/model/song/song.cpp`, in the shell-read block, immediately after the `launchStyle` read (the `if (const deluge::storage::Value* n = clipNode->find("launchStyle")) { … }` at ~2267), add:
```cpp
				// loopLength (durable-id readFromTree slice 2): "length" is the current attribute name, "trackLength"
				// the legacy alias. Clamp to >=1 and seed originalLength, matching the old readTagFromFile handler.
				const deluge::storage::Value* lenNode = clipNode->find("length");
				if (lenNode == nullptr) {
					lenNode = clipNode->find("trackLength");
				}
				if (lenNode != nullptr) {
					newClip->setLoopLength(std::max<int32_t>(1, stringToInt(lenNode->str().c_str())));
					newClip->originalLength = newClip->loopLength();
				}
```
- [ ] **Step 2: Init `readAutomationUpToPos` from loopLength in InstrumentClip.** In `src/deluge/model/clip/instrument_clip.cpp:2504`, change:
```cpp
	int32_t readAutomationUpToPos = kMaxSequenceLength;
```
to:
```cpp
	// loopLength is read tree-natively (readClipsFromFile) before this runs; bound automation reads by it, exactly as
	// the old <length> attribute handler did (length is always written before any automation). 0 (absent) -> the max.
	int32_t readAutomationUpToPos = loopLength() > 0 ? loopLength() : kMaxSequenceLength;
```
- [ ] **Step 3: Same init in AudioClip.** In `src/deluge/model/clip/audio_clip.cpp:1123`, apply the identical change (same two-line comment + `int32_t readAutomationUpToPos = loopLength() > 0 ? loopLength() : kMaxSequenceLength;`).
- [ ] **Step 4: Delete the `length` handler.** In `src/deluge/model/clip/clip.cpp`, delete the whole block (and update the adjacent comment left from Step 3 of the previous slice):
```cpp
	// "section", "colourOffset", "launchStyle" are read tree-natively in readClipsFromFile (durable-id Step 3); no
	// handlers here. "length"/"trackLength" stays -- its handler also sets originalLength + readAutomationUpToPos.

	else if (!strcmp(tagName, "trackLength") || !strcmp(tagName, "length")) {
		setLoopLength(reader.readTagOrAttributeValueInt());
		setLoopLength(std::max((int32_t)1, loopLength()));
		originalLength = loopLength(); // it's 0 otherwise, which is fine except for audio clips
		*readAutomationUpToPos = loopLength();
	}
```
Replace it with the single line:
```cpp
	// "section", "colourOffset", "launchStyle", "length"/"trackLength" are all read tree-natively in readClipsFromFile
	// (durable-id readFromTree slices 1-2); no handlers here -- those tags fall through to exitTag() below.
```
- [ ] **Step 5: Build firmware + all goldens.** Run:
```
./dbt build Debug 2>&1 | tail -1
cmake --build build-sim --target song_roundtrip edit_undo_redo param_record_diff json_roundtrip 2>&1 | tail -1
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip 2>&1 | tail -1
./dbt test 2>&1 | grep -iE "tests passed|failed" | head -1
```
Expected: `OK=15 REGRESS=0`, `OK=40 FAIL=0`, `OK=3 REGRESS=0`, `OK=37 FAIL=0`, `100% tests passed`, firmware links. A `song_roundtrip` REGRESS means an `originalLength`/`readAutomationUpToPos` divergence — check the REGRESSing song's automation-node counts.
- [ ] **Step 6: Commit.** `git add -A && git commit -q -m "clip: loopLength read tree-natively; complete the DocClip shell (readFromTree slice 2)"`.

---

## Self-Review

**Spec coverage.** The three edits (loader read, two `readFromFile` inits, handler delete) = the spec's Design 1/2/3. The `trackLength` legacy alias is checked (spec Risk 2). `stringToInt`/`kMaxSequenceLength`/`std::max` are already in scope at each site.

**Placeholder scan.** All code is concrete; no TODOs.

**Type consistency.** `find(key)→const Value*` then `str()`; `setLoopLength(int32_t)`; `originalLength` is a public `int32_t` Clip member; `loopLength()→int32_t`. Consistent with slice 1.

**Note for the executor.** One task, golden-gated. The only thing that can go wrong is the `readAutomationUpToPos` semantics; the attribute-ordering proof (length is always written before automation) makes it equivalent, and `song_roundtrip` bit-identity is the backstop.
