# readFromTree slice 4 — clip view-state via `readShellFromTree` — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or subagent-driven-development) to implement this plan. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Introduce `virtual Clip::readShellFromTree(const Value& clipNode)` and move the pure view-state/UI attributes out of `InstrumentClip::readFromFile` / `AudioClip::readFromFile` into it.

**Architecture:** A virtual reads each clip type's view-state from the clip `Value` node, called from `readClipsFromFile` right after construction. Base reads the six shared fields; `InstrumentClip` overrides to add its ~15 view attrs; `AudioClip` needs no override. Behaviour-preserving; golden bit-identity is the gate.

**Tech Stack:** C++23/26. Firmware `./dbt build Debug`; sim goldens `python3 tests/golden/<name>_run.py`; specs `./dbt test`.

## Global Constraints

- **Behaviour-preserving; golden bit-identity is the gate.** Success = `song_roundtrip` `OK=16 REGRESS=0`, `edit_undo_redo` `OK=40`, `param_record` `OK=3`, `json_roundtrip` `OK=38`, `./dbt test` 15/15, `./dbt build Debug` links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- Single fresh `git commit` — never `--amend` (clang-format hook reformats+fails; recover with a NEW commit).
- Every moved read writes the SAME member with the SAME cast/side-effect as the deleted handler — read-path relocation only.

---

### Task 1: `readShellFromTree` virtual + move the view-state reads

**Files:**
- Modify: `src/deluge/model/clip/clip.h` (declare virtual; forward-declare `deluge::storage::Value`)
- Modify: `src/deluge/model/clip/clip.cpp` (base impl)
- Modify: `src/deluge/model/clip/instrument_clip.h` (declare override)
- Modify: `src/deluge/model/clip/instrument_clip.cpp` (override impl; remove ~21 handlers from `readFromFile`)
- Modify: `src/deluge/model/clip/audio_clip.cpp` (remove 6 shared handlers from `readFromFile`)
- Modify: `src/deluge/model/song/song.cpp` (`readClipsFromFile` — call `readShellFromTree`)

**Interfaces:**
- Produces: `virtual void Clip::readShellFromTree(const deluge::storage::Value& clipNode)`; `void InstrumentClip::readShellFromTree(const deluge::storage::Value&) override`.
- Consumes: `Value::find(key)->const Value*`, `Value::str()->const std::string&`, `stringToInt` (functions.h). `clip.cpp` already has `namespace params = deluge::modulation::params;`.

- [ ] **Step 1: Declare the virtual in `clip.h`.** Near the other `read*`/`write*` members, add (and ensure a forward decl `namespace deluge::storage { class Value; }` exists in the file — add it by the other forward decls if absent):
```cpp
	virtual void readShellFromTree(const deluge::storage::Value& clipNode);
```

- [ ] **Step 2: Base impl in `clip.cpp`.** Add (include `storage/value.h` if not already included in clip.cpp):
```cpp
// Reads the pure per-type view-state from the clip's Value node (durable-id readFromTree slice 4): the fields
// below have no construction/song coupling, so readClipsFromFile calls this right after construction and the
// subclass readFromFile loops no longer handle them. Base reads the fields shared by both clip types.
void Clip::readShellFromTree(const deluge::storage::Value& clipNode) {
	using deluge::storage::Value;
	if (const Value* n = clipNode.find("onAutomationInstrumentClipView")) {
		onAutomationClipView = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedParamID")) {
		lastSelectedParamID = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedParamKind")) {
		lastSelectedParamKind = static_cast<params::Kind>(stringToInt(n->str().c_str()));
	}
	if (const Value* n = clipNode.find("lastSelectedParamShortcutX")) {
		lastSelectedParamShortcutX = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedParamShortcutY")) {
		lastSelectedParamShortcutY = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedParamArrayPosition")) {
		lastSelectedParamArrayPosition = stringToInt(n->str().c_str());
	}
}
```

- [ ] **Step 3: Declare the override in `instrument_clip.h`.** Near the other overrides:
```cpp
	void readShellFromTree(const deluge::storage::Value& clipNode) override;
```

- [ ] **Step 4: Override impl in `instrument_clip.cpp`.** Add (include `storage/value.h` if needed):
```cpp
void InstrumentClip::readShellFromTree(const deluge::storage::Value& clipNode) {
	Clip::readShellFromTree(clipNode);
	using deluge::storage::Value;
	if (const Value* n = clipNode.find("clipName")) {
		name = n->str();
	}
	if (const Value* n = clipNode.find("inKeyMode")) {
		inScaleMode = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("yScroll")) {
		yScroll = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("keyboardLayout")) {
		keyboardState.currentLayout = static_cast<KeyboardLayoutType>(stringToInt(n->str().c_str()));
	}
	if (const Value* n = clipNode.find("yScrollKeyboard")) {
		keyboardState.isomorphic.scrollOffset = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("keyboardRowInterval")) {
		keyboardState.isomorphic.rowInterval = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("drumsScrollOffset")) {
		keyboardState.drums.scroll_offset = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("drumsZoomLevel")) {
		keyboardState.drums.zoom_level = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("inKeyScrollOffset")) {
		keyboardState.inKey.scrollOffset = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("inKeyRowInterval")) {
		keyboardState.inKey.rowInterval = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("crossScreenEditLevel")) {
		wrapEditLevel = stringToInt(n->str().c_str());
		wrapEditing = true;
	}
	if (const Value* n = clipNode.find("onKeyboardScreen")) {
		onKeyboardScreen = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedInstrumentType")) {
		lastSelectedOutputType = static_cast<OutputType>(stringToInt(n->str().c_str()));
	}
	if (const Value* n = clipNode.find("lastSelectedPatchSource")) {
		lastSelectedPatchSource = static_cast<PatchSource>(stringToInt(n->str().c_str()));
	}
	if (const Value* n = clipNode.find("affectEntire")) {
		affectEntire = stringToInt(n->str().c_str());
	}
}
```

- [ ] **Step 5: Call it from `readClipsFromFile`.** In `src/deluge/model/song/song.cpp`, after the inline base-shell reads and before `Error error = newClip->readFromFile(reader, this);`, add:
```cpp
			newClip->readShellFromTree(*clipNode); // slice 4: per-type view-state (guarded: only when clipNode present)
```
Place it inside the existing `if (clipNode != nullptr) { … }` block (so `clipNode` is non-null), at its end.

- [ ] **Step 6: Remove the moved handlers.** In `src/deluge/model/clip/instrument_clip.cpp` `InstrumentClip::readFromFile`, delete the `else if` blocks for: `clipName`, `inKeyMode`, `yScroll`, `keyboardLayout`, `yScrollKeyboard`, `keyboardRowInterval`, `drumsScrollOffset`, `drumsZoomLevel`, `inKeyScrollOffset`, `inKeyRowInterval`, `crossScreenEditLevel`, `onKeyboardScreen`, `onAutomationInstrumentClipView`, `lastSelectedParamID`, `lastSelectedParamKind`, `lastSelectedParamShortcutX`, `lastSelectedParamShortcutY`, `lastSelectedParamArrayPosition`, `lastSelectedInstrumentType`, `lastSelectedPatchSource`, `affectEntire`. In `src/deluge/model/clip/audio_clip.cpp` `AudioClip::readFromFile`, delete the `else if` blocks for: `onAutomationInstrumentClipView`, `lastSelectedParamID`, `lastSelectedParamKind`, `lastSelectedParamShortcutX`, `lastSelectedParamShortcutY`, `lastSelectedParamArrayPosition`. Do NOT touch `instrumentPreset*`, `midiChannel`, `cvChannel`, `midi*`, `filePath`, sample-pos, `transpose`, or any nested/`params` handlers. Keep the first surviving handler as `if` (not `else if`) if the deletion removed the leading `if`.

- [ ] **Step 7: Build firmware + goldens.** Run:
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
Expected: `OK=16 REGRESS=0`, `OK=40 FAIL=0`, `OK=3 REGRESS=0`, `OK=38 FAIL=0`, `100% tests passed`, links. A REGRESS points at a wrong cast or a field consulted mid-`readFromFile` — check the specific attribute in the REGRESSing song's gen1.

- [ ] **Step 8: Commit.** `git add -A && git commit -q -m "clip: view-state read tree-natively via readShellFromTree virtual (readFromTree slice 4)"`.

---

## Self-Review

**Spec coverage.** Base 6 fields + InstrumentClip 15 fields = the spec's Design 1/2; call site + dual-body handler removal = Design 3. `AudioClip` correctly gets no override (base virtual reads its shared view-state). Scope excludes binding/data/nested per the spec.

**Placeholder scan.** All code concrete. The forward-decl and `storage/value.h` includes are conditional ("if absent") — an executor check, not a placeholder.

**Type consistency.** `find()->const Value*` then `str()`/`stringToInt`; enum casts (`params::Kind`/`KeyboardLayoutType`/`OutputType`/`PatchSource`) copied verbatim from the handlers; `name` is `std::string` (direct assign). Members are reachable: base fields on `Clip`, InstrumentClip fields on `InstrumentClip`/`keyboardState`.

**Note for the executor.** One task, golden-gated. `song_roundtrip` bit-identity is the backstop for any missed cast or ordering assumption.
