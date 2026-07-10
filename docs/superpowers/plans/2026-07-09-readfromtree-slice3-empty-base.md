# readFromTree slice 3 — empty `Clip::readTagFromFile` — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or subagent-driven-development) to implement this plan. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Read every remaining base-clip tag tree-natively in `Song::readClipsFromFile`, add the reusable nested-read primitive `ValueTreeDeserializer::readChildObject`, and delete `Clip::readTagFromFile` outright.

**Architecture:** Two tasks. Task 1 moves the plain attributes + the selection/sync-scale hints into the loader's tree-native shell-read block (same `Clip` members, same side effects). Task 2 adds `readChildObject` (a hermetic scoped sub-parse of a child `Value`), routes `muteMidiCommand` through it, and deletes the now-empty `Clip::readTagFromFile` (definition, declaration, and both subclass `else{}` fallbacks). Behaviour-preserving; golden bit-identity is the gate.

**Tech Stack:** C++23/26. Firmware `./dbt build Debug`; sim goldens `python3 tests/golden/<name>_run.py`; specs `./dbt test`.

## Global Constraints

- **Behaviour-preserving; golden bit-identity is the gate.** Success = `song_roundtrip` `OK=16 REGRESS=0` (15 corpus + `synthetic/syncscale.XML`), `edit_undo_redo` `OK=40`, `param_record` `OK=3`, `json_roundtrip` `OK=37`, `./dbt test` 15/15, `./dbt build Debug` links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- Single fresh `git commit` per task — never `--amend` (clang-format hook reformats+fails; recover with a NEW commit).
- The moved reads write the **same** `Clip`/`Song` members as the deleted handlers — this is read-path relocation only, no DocClip-store ownership change.

---

### Task 1: Move plain attributes + selection/sync hints into the loader

**Files:**
- Modify: `src/deluge/model/song/song.cpp` (`readClipsFromFile` shell-read block, after the slice-2 `length` read at ~2279)
- Modify: `src/deluge/model/clip/clip.cpp` (remove handlers from `Clip::readTagFromFile`, 717-781)

**Interfaces (all confirmed in scope):**
- `clipNode->find(key) -> const deluge::storage::Value*`; `Value::str() -> const std::string&`.
- `newClip->setSection/setColourOffset/...` already used here. Direct public members: `newClip->activeIfNoSolo`, `newClip->soloingInSessionMode` (clip.h 175/177), `newClip->armedForRecording`, `newClip->sequenceDirectionMode` (timeline_counter.h 52/55, public base).
- `stringToInt` (functions.h:146, already used in this function), `stringToSequenceDirectionMode` (functions.h:194, reachable — `functions.h` is already transitively included since `stringToInt` compiles here).
- Song side effects: `this->setCurrentClip(newClip)`, `this->runtimeView.inClipMinderViewOnLoad = <bool>`, `this->project.transport.sync_scaling_clip = newClip` (all used verbatim in song.cpp already).

- [ ] **Step 1: Add the attribute reads in the loader.** In `src/deluge/model/song/song.cpp`, inside the `if (clipNode != nullptr) { ... }` shell block, immediately after the slice-2 `length` block (after the closing brace of `if (lenNode != nullptr) { ... }` at ~2279), add:

```cpp
					// Plain base-clip attributes (readFromTree slice 3): pure field writes, same members the
					// deleted Clip::readTagFromFile handlers wrote.
					if (const deluge::storage::Value* n = clipNode->find("isPlaying")) {
						newClip->activeIfNoSolo = (stringToInt(n->str().c_str()) != 0);
					}
					if (const deluge::storage::Value* n = clipNode->find("isSoloing")) {
						newClip->soloingInSessionMode = (stringToInt(n->str().c_str()) != 0);
					}
					if (const deluge::storage::Value* n = clipNode->find("isArmedForRecording")) {
						newClip->armedForRecording = (stringToInt(n->str().c_str()) != 0);
					}
					// "status" is the legacy back-compat form of isPlaying/isSoloing (status==2 -> playing, never soloing).
					if (const deluge::storage::Value* n = clipNode->find("status")) {
						newClip->soloingInSessionMode = false;
						newClip->activeIfNoSolo = (stringToInt(n->str().c_str()) == 2);
					}
					if (const deluge::storage::Value* n = clipNode->find("sequenceDirection")) {
						newClip->sequenceDirectionMode = stringToSequenceDirectionMode(n->str().c_str());
					}

					// Selection / sync-scale hints (readFromTree slice 3): read the flag, then perform the same Song-state
					// side effect the deleted handlers did. Clips are processed in file order, so these last-wins singletons
					// resolve identically to firing mid-readFromFile. isSyncScaleClip is guarded by synthetic/syncscale.XML.
					if (const deluge::storage::Value* n = clipNode->find("beingEdited")) {
						if (stringToInt(n->str().c_str())) {
							setCurrentClip(newClip);
							runtimeView.inClipMinderViewOnLoad = true;
						}
					}
					if (const deluge::storage::Value* n = clipNode->find("selected")) {
						if (stringToInt(n->str().c_str())) {
							setCurrentClip(newClip);
							runtimeView.inClipMinderViewOnLoad = false;
						}
					}
					const deluge::storage::Value* syncNode = clipNode->find("isSyncScaleClip");
					if (syncNode == nullptr) {
						syncNode = clipNode->find("isSyncScaleTrack"); // legacy alias
					}
					if (syncNode != nullptr && stringToInt(syncNode->str().c_str())) {
						project.transport.sync_scaling_clip = newClip;
					}
```

- [ ] **Step 2: Remove the moved handlers from `Clip::readTagFromFile`.** In `src/deluge/model/clip/clip.cpp` (717-781), delete the `isPlaying`, `isSoloing`, `isArmedForRecording`, `status`, `beingEdited`, `selected`, `isSyncScaleTrack`/`isSyncScaleClip`, and `sequenceDirection` handler blocks, and the trailing commented-out `activeModFunction` block. `muteMidiCommand` STAYS for now (Task 2 removes it). After this step the method body contains only the `muteMidiCommand` handler (plus the slice-1/2 comment about tree-native shell fields). Leave the method signature and the subclass `else{}` calls untouched — Task 2 deletes them.

- [ ] **Step 3: Build firmware + goldens.** Run:
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
Expected: `OK=16 REGRESS=0`, `OK=40 FAIL=0`, `OK=3 REGRESS=0`, `OK=37 FAIL=0`, `100% tests passed`, firmware links. A `song_roundtrip` REGRESS on `synthetic/syncscale.XML` specifically = the `isSyncScaleClip` move is wrong.

- [ ] **Step 4: Commit.** `git add -A && git commit -q -m "clip: base-clip attributes + selection/sync hints read tree-natively (readFromTree slice 3, part 1)"`.

---

### Task 2: `readChildObject` primitive + `muteMidiCommand`; delete `Clip::readTagFromFile`

**Files:**
- Modify: `src/deluge/storage/value_tree_deserializer.h` (add `readChildObject` template + `enterChildObject` private decl)
- Modify: `src/deluge/storage/value_tree_deserializer.cpp` (add `enterChildObject` body)
- Modify: `src/deluge/model/song/song.cpp` (`readChildObject` call for `muteMidiCommand` in the shell block)
- Modify: `src/deluge/model/clip/clip.cpp` (delete `Clip::readTagFromFile` definition)
- Modify: `src/deluge/model/clip/clip.h` (delete the declaration, line 134)
- Modify: `src/deluge/model/clip/instrument_clip.cpp:2915` and `src/deluge/model/clip/audio_clip.cpp:1216` (delete the `else{}` fallback)

**Interfaces:**
- Produces: `template <typename F> void ValueTreeDeserializer::readChildObject(const deluge::storage::Value& childObject, F&& body)` — positions a hermetic sub-cursor inside `childObject`, runs `body()` (which drives the ordinary streaming interface over the object's children), then fully restores the outer cursor.
- Consumes: `newClip->muteMIDICommand` (`LearnedMIDI`, clip.h:188); `LearnedMIDI::readNoteFromFile(Deserializer&)`.

- [ ] **Step 1: Read `match()`/`reset()` internals first.** Before writing `enterChildObject`, read `src/deluge/storage/value_tree_deserializer.cpp`'s `match`, `readNextTagOrAttributeName`, `reset`, and the XML helpers to see exactly how a descent sets `stack_`/`pending_`/`callerDepth_`. `enterChildObject` must leave the cursor in the same state a real `readNextTagOrAttributeName()` (producing `childObject`) followed by `match('{')` would — i.e. positioned to read `childObject`'s children — in BOTH `SourceFormat::Xml` and `SourceFormat::Json`.

- [ ] **Step 2: Add the primitive to the header.** In `src/deluge/storage/value_tree_deserializer.h`, in the `public:` section (near `currentValue()`), add:
```cpp
	/// Run `body` against a specific child object node, then restore the cursor exactly. `body` drives
	/// the ordinary streaming interface (readNextTagOrAttributeName / match / exitTag) over `childObject`'s
	/// children -- exactly as a mid-stream readFromFile would -- letting a tree-native loader delegate a
	/// nested object to a shared streaming sub-reader (e.g. LearnedMIDI::readFromFile) without rewriting it.
	/// Hermetic: the entire cursor state is saved and restored around `body`, so the sub-read can never
	/// desync the parent stream regardless of format/depth subtleties.
	template <typename F>
	void readChildObject(const Value& childObject, F&& body) {
		auto savedStack = stack_;
		const Value* savedPending = pending_;
		std::size_t savedCharCursor = charCursor_;
		int32_t savedCallerDepth = callerDepth_;

		enterChildObject(childObject);
		body();

		stack_ = std::move(savedStack);
		pending_ = savedPending;
		charCursor_ = savedCharCursor;
		callerDepth_ = savedCallerDepth;
	}
```
And in the `private:` section, declare:
```cpp
	// Position the cursor to read `childObject`'s children from the start (the state after a map-entry
	// descent into it). Used only by readChildObject, which brackets it with save/restore.
	void enterChildObject(const Value& childObject);
```

- [ ] **Step 3: Implement `enterChildObject`.** In `src/deluge/storage/value_tree_deserializer.cpp`, add the body. Start with the descent-reuse form (adjust per Step 1's findings if a mode mis-reads):
```cpp
void ValueTreeDeserializer::enterChildObject(const Value& childObject) {
	// Present childObject as the freshly-named pending entry, then reuse the real, mode-correct descent
	// (match('{') pushes a frame for the pending Map and clears pending) so the sub-reader's first
	// readNextTagOrAttributeName() yields childObject's first child.
	pending_ = &childObject;
	charCursor_ = 0;
	match('{');
}
```

- [ ] **Step 4: Route `muteMidiCommand` through the loader.** In `src/deluge/model/song/song.cpp`, in the shell block (after the Task 1 sync-scale read), add:
```cpp
					// muteMidiCommand (readFromTree slice 3): a nested <note> object read by the shared streaming
					// sub-reader LearnedMIDI::readFromFile. readChildObject drives that reader against the child node
					// hermetically. Covered by the corpus song carrying a muteMidiCommand (song_roundtrip + json_roundtrip).
					if (const deluge::storage::Value* muteNode = clipNode->find("muteMidiCommand")) {
						auto& treeReader = static_cast<deluge::storage::ValueTreeDeserializer&>(reader);
						treeReader.readChildObject(*muteNode, [&] { newClip->muteMIDICommand.readNoteFromFile(reader); });
					}
```

- [ ] **Step 5: Delete `Clip::readTagFromFile`.** Remove the entire `Clip::readTagFromFile` definition from `src/deluge/model/clip/clip.cpp` (717-781, now containing only the `muteMidiCommand` handler + comment) and the declaration at `src/deluge/model/clip/clip.h:134`.

- [ ] **Step 6: Delete the subclass fallbacks.** In `src/deluge/model/clip/instrument_clip.cpp` (2914-2916) and `src/deluge/model/clip/audio_clip.cpp` (1215-1217), delete the `else { readTagFromFile(reader, tagName, song, &readAutomationUpToPos); }` branch entirely. An unrecognized tag now falls straight to the existing `reader.exitTag()` after the if/else chain — identical to the old empty-handler behaviour. Leave the `readAutomationUpToPos` local (still used by note/param reads).

- [ ] **Step 7: Build firmware + goldens.** Run the same command block as Task 1 Step 3. Expected identical: `OK=16 REGRESS=0`, `OK=40`, `OK=3`, `OK=37`, `100% tests passed`, links. If the `muteMidiCommand` corpus song REGRESSes in `song_roundtrip` (XML-tree mode) or FAILs in `json_roundtrip` (JSON mode), `enterChildObject`'s depth setup is wrong for that mode — the hermetic save/restore means the parent stream is intact, so debug is localized to the muteMIDICommand read (compare the emitted `<muteMidiCommand>` bytes).

- [ ] **Step 8: Commit.** `git add -A && git commit -q -m "clip: muteMidiCommand via readChildObject; delete Clip::readTagFromFile (readFromTree slice 3, part 2)"`.

---

## Self-Review

**Spec coverage.** Task 1 = spec Design groups 1 (plain attributes) + 2 (selection/sync hints); Task 2 = group 3 (`muteMidiCommand` via `readChildObject`) + the deletion (method + decl + subclass fallbacks). The `status`/`isSyncScaleTrack` legacy aliases are handled. `isSyncScaleClip` guarded by `synthetic/syncscale.XML` (`OK=16`).

**Placeholder scan.** All code concrete. The one deferred detail — `enterChildObject`'s exact depth setup — has a concrete starting implementation (Step 3), an explicit read-first instruction (Step 1), and a mode-specific validation + debug note (Step 7). Not a placeholder: a bounded, golden-gated implementation choice whose failure is contained by the hermetic save/restore.

**Type consistency.** `find(key)->const Value*` then `str().c_str()` → `stringToInt`/`stringToSequenceDirectionMode`; member writes match declared types (`bool`, `SequenceDirection`); `readChildObject<F>` template + `enterChildObject(const Value&)` private helper; `readNoteFromFile(Deserializer&)` takes the base `reader`. `sync_scaling_clip`/`setCurrentClip`/`runtimeView` used exactly as elsewhere in song.cpp.

**Ordering note for the executor.** Task 1 leaves `muteMidiCommand` in `Clip::readTagFromFile` and keeps the method + subclass fallbacks, so the build is green between tasks (unrecognized moved tags are skipped in the streaming pass, exactly as slices 1-2). Only Task 2 empties and deletes the method.
