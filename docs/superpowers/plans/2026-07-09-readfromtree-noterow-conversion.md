# readFromTree — NoteRow conversion — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or subagent-driven-development) to implement this plan. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Convert `NoteRow::readFromFile(Deserializer&)` into tree-native `NoteRow::readFromTree(Deserializer&, const Value&)`, driven from `InstrumentClip`, establishing the per-type conversion pattern + the `readScalarValue` primitive.

**Architecture:** Task 1 extracts the two risky blocks (hex `noteData` decode, `soundParams` setup) into private helpers with `readFromFile` still calling them — a pure golden-neutral refactor that de-risks the decode move. Task 2 adds `readScalarValue`, writes `readFromTree` (scalars via `find()`, nested via `readChildObject`, `noteData` via `readScalarValue`+helper), wires the `InstrumentClip` driver, and deletes `readFromFile`. Behaviour-preserving; golden bit-identity is the gate.

**Tech Stack:** C++23/26. Firmware `./dbt build Debug`; sim goldens `python3 tests/golden/<name>_run.py`; specs `./dbt test`.

## Global Constraints

- **Behaviour-preserving; golden bit-identity is the gate.** Success (BOTH tasks) = `song_roundtrip` `OK=16 REGRESS=0`, `edit_undo_redo` `OK=40`, `param_record` `OK=3`, `json_roundtrip` `OK=38`, `./dbt test` 15/15, `./dbt build Debug` links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- Single fresh `git commit` per task — never `--amend` (clang-format hook reformats+fails; recover with a NEW commit).
- Reproduce legacy control flow (the pre-2016 `soundParams` backup hack, the 4 `noteData` hex variants) **verbatim** — this is a faithful conversion, not a cleanup.

---

### Task 1: Extract `readNoteDataHex` + `readSoundParams` helpers (pure refactor)

**Files:** `src/deluge/model/note/note_row.h` (declare 2 private helpers), `src/deluge/model/note/note_row.cpp` (extract bodies; `readFromFile` calls them).

**Goal:** Move the `doReadNoteData:` decode block and the `soundParams` setup block out of `readFromFile` into private helpers, with `readFromFile` calling them — no behaviour change. This isolates the delicate hex-decode/goto logic so Task 2 reuses it untouched.

- [ ] **Step 1: Declare helpers in `note_row.h`.** In the private section:
```cpp
	// Decode a hex noteData value (V1.4+) into notes[]. noteHexLength selects the format variant
	// (20/22/26/28). Reads the value char-at-a-time from the reader's current scalar. Returns
	// INSUFFICIENT_RAM on allocation failure, else NONE.
	Error readNoteDataHex(Deserializer& reader, uint16_t noteHexLength);
	// Set up this NoteRow's paramManager for a <soundParams> block (incl. the pre-2016 backup-clone
	// hack) and read the sound params. Returns any error from the clone/read.
	Error readSoundParams(Deserializer& reader, InstrumentClip* parentClip, Song* song, int32_t readAutomationUpToPos);
```

- [ ] **Step 2: Extract `readNoteDataHex`.** Move the block currently at the `doReadNoteData:` label (from `int32_t minPos = 0;` through the `getOut: {}` label, i.e. the entire decode loop) into `Error NoteRow::readNoteDataHex(Deserializer& reader, uint16_t noteHexLength) { … return Error::NONE; }`. Keep the internal `goto getOut`/`useDefaultLift` (now function-local). Replace the two `return Error::INSUFFICIENT_RAM;` inside with the same. In `readFromFile`, the four variant handlers (`noteData`→20, `noteDataWithLift`→22, `noteDataWithIteranceAndFill`→26, `noteDataWithSplitProb`→28) become:
```cpp
		else if (!strcmp(tagName, "noteData")) {
			error = readNoteDataHex(reader, 20);
			if (error != Error::NONE) { goto someError; }
		}
```
…and analogously for the other three (with their firmware-version guards preserved: `noteDataWithLift`/`noteDataWithIteranceAndFill` keep their `song_firmware_version < community({1,3,0})` guards and the `#if ALPHA_OR_BETA_VERSION` around the 26 variant). Delete the now-empty `doReadNoteData:`/`getOut:` labels from `readFromFile`. (If `readFromFile` has no `someError` handling for these — it does; it's used elsewhere — reuse it.)

- [ ] **Step 3: Extract `readSoundParams`.** Move the `soundParams` handler body (the pre-2016 backup-clone hack + `goto finishedNormalStuff` + `paramManager.setupWithPatching(); Sound::initParams(&paramManager);` + `Sound::readParamsFromFile(...)`) into `Error NoteRow::readSoundParams(Deserializer& reader, InstrumentClip* parentClip, Song* song, int32_t readAutomationUpToPos)`, keeping the internal `goto finishedNormalStuff` (now function-local), returning `Error::NONE` at the end and the clone error where it currently `return error`s. In `readFromFile`:
```cpp
		else if (!strcmp(tagName, "soundParams")) {
			error = readSoundParams(reader, parentClip, song, readAutomationUpToPos);
			if (error != Error::NONE) { goto someError; }
		}
```

- [ ] **Step 4: Build + goldens.** Run the full gate (command block in Task 2 Step 7). Expected all-green and UNCHANGED (pure refactor): `OK=16 REGRESS=0`, `OK=40`, `OK=3`, `OK=38`, `100% tests passed`, links. A REGRESS here means the extraction changed behaviour — diff the moved code against the original.

- [ ] **Step 5: Commit.** `git commit -m "note: extract readNoteDataHex + readSoundParams helpers from NoteRow::readFromFile (pure refactor)"`.

---

### Task 2: `readScalarValue` + `NoteRow::readFromTree` + driver; delete `readFromFile`

**Files:** `src/deluge/storage/value_tree_deserializer.h` (add `readScalarValue`), `src/deluge/model/note/note_row.{h,cpp}` (add `readFromTree`, remove `readFromFile`), `src/deluge/model/clip/instrument_clip.cpp` (drive `readFromTree`).

**Interfaces:**
- Produces: `template <typename F> void ValueTreeDeserializer::readScalarValue(const Value&, F&&)`; `Error NoteRow::readFromTree(Deserializer& reader, const deluge::storage::Value& node, int32_t* minY, InstrumentClip* parentClip, Song* song, int32_t readAutomationUpToPos)`.
- Consumes: `readChildObject` (slice 3), `readNoteDataHex`/`readSoundParams` (Task 1), `Value::find/str/items`.

- [ ] **Step 1: Add `readScalarValue` to `value_tree_deserializer.h`.** Next to `readChildObject`, in the `public:` section:
```cpp
	/// Position the cursor at a scalar Value so the char-at-a-time value readers read from it, run `body`,
	/// then restore the cursor exactly. Lets a tree-native reader hand a scalar (e.g. a hex noteData blob)
	/// to existing char-decoding code without rewriting it. Hermetic (full save/restore).
	template <typename F>
	void readScalarValue(const Value& scalarNode, F&& body) {
		auto savedStack = stack_;
		const Value* savedPending = pending_;
		std::size_t savedCharCursor = charCursor_;
		int32_t savedCallerDepth = callerDepth_;

		pending_ = &scalarNode;
		charCursor_ = 0;
		body();

		stack_ = std::move(savedStack);
		pending_ = savedPending;
		charCursor_ = savedCharCursor;
		callerDepth_ = savedCallerDepth;
	}
```

- [ ] **Step 2: Declare `readFromTree` in `note_row.h`** (near `readFromFile`), and add `#include`/forward-decl for `deluge::storage::Value` if absent (forward-decl `namespace deluge::storage { class Value; }`):
```cpp
	Error readFromTree(Deserializer& reader, const deluge::storage::Value& node, int32_t* minY,
	                   InstrumentClip* parentClip, Song* song, int32_t readAutomationUpToPos);
```

- [ ] **Step 3: Implement `NoteRow::readFromTree` in `note_row.cpp`** (add `#include "storage/value.h"` + `#include "storage/value_tree_deserializer.h"`). Read scalars via `node.find()`, nested via `readChildObject`, `noteData` variants via `readScalarValue`+`readNoteDataHex`, then the post-loop logic verbatim:
```cpp
Error NoteRow::readFromTree(Deserializer& reader, const deluge::storage::Value& node, int32_t* minY,
                            InstrumentClip* parentClip, Song* song, int32_t readAutomationUpToPos) {
	using deluge::storage::Value;
	auto& treeReader = static_cast<deluge::storage::ValueTreeDeserializer&>(reader);
	drum = (Drum*)0xFFFFFFFF; // "no drum" sentinel, resolved to a real drum later
	int32_t newBendRange = -1;

	// length first: it bounds readAutomationUpToPos for the param/note reads below (attribute, so order-free here).
	if (const Value* n = node.find("length")) {
		loopLengthIfIndependent = stringToInt(n->str().c_str());
		readAutomationUpToPos = loopLengthIfIndependent;
	}
	if (const Value* n = node.find("muted")) {
		muted = stringToInt(n->str().c_str());
	}
	if (const Value* n = node.find("y")) {
		y = stringToInt(n->str().c_str());
	}
	if (const Value* n = node.find("colourOffset")) {
		colourOffset = stringToInt(n->str().c_str());
	}
	if (const Value* n = node.find("drumIndex")) {
		drum = (Drum*)stringToInt(n->str().c_str());
	}
	if (const Value* n = node.find("gateOutput")) {
		int32_t gateChannel = std::clamp<int32_t>(stringToInt(n->str().c_str()), 0, NUM_GATE_CHANNELS - 1);
		drum = (Drum*)(0xFFFFFFFE - gateChannel);
	}
	if (const Value* n = node.find("sequenceDirection")) {
		sequenceDirectionMode = stringToSequenceDirectionMode(n->str().c_str());
	}
	if (const Value* n = node.find("bendRange")) {
		newBendRange = stringToInt(n->str().c_str());
	}
	if (const Value* n = node.find("muteMidiCommand")) {
		treeReader.readChildObject(*n, [&] { muteMIDICommand.readNoteFromFile(reader); });
	}
	if (const Value* n = node.find("soundMidiCommand")) {
		treeReader.readChildObject(*n, [&] { midiInput.readNoteFromFile(reader); });
	}
	Error error = Error::NONE;
	if (const Value* n = node.find("soundParams")) {
		treeReader.readChildObject(*n, [&] { error = readSoundParams(reader, parentClip, song, readAutomationUpToPos); });
		if (error != Error::NONE) {
			return error;
		}
	}
	if (const Value* n = node.find("expressionData")) {
		treeReader.readChildObject(*n, [&] {
			paramManager.ensureExpressionParamSetExists();
			ParamCollectionSummary* summary = paramManager.getExpressionParamSetSummary();
			ExpressionParamSet* expressionParams = (ExpressionParamSet*)summary->paramCollection;
			if (expressionParams) {
				expressionParams->readFromFile(reader, summary, readAutomationUpToPos);
			}
		});
	}
	// Hex note data (V1.4+). Only one variant is present per note row; guards mirror readFromFile.
	if (const Value* n = node.find("noteData")) {
		treeReader.readScalarValue(*n, [&] { error = readNoteDataHex(reader, 20); });
	}
	else if (song_firmware_version < FirmwareVersion::community({1, 3, 0})
	         && (n = node.find("noteDataWithLift")) != nullptr) {
		treeReader.readScalarValue(*n, [&] { error = readNoteDataHex(reader, 22); });
	}
#if ALPHA_OR_BETA_VERSION
	else if (song_firmware_version < FirmwareVersion::community({1, 3, 0})
	         && (n = node.find("noteDataWithIteranceAndFill")) != nullptr) {
		treeReader.readScalarValue(*n, [&] { error = readNoteDataHex(reader, 26); });
	}
#endif
	else if ((n = node.find("noteDataWithSplitProb")) != nullptr) {
		treeReader.readScalarValue(*n, [&] { error = readNoteDataHex(reader, 28); });
	}
	if (error != Error::NONE) {
		return error;
	}
	// Legacy pre-1.4 <notes> array — reuse the streaming note loop via a child bridge.
	if (const Value* n = node.find("notes")) {
		treeReader.readChildObject(*n, [&] { error = readLegacyNotes(reader); });
		if (error != Error::NONE) {
			return error;
		}
	}

	y = std::max(y, (int16_t)*minY);
	*minY = y + 1;
	if (newBendRange != -1) {
		ExpressionParamSet* expressionParams = paramManager.getOrCreateExpressionParamSet();
		if (expressionParams) {
			expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] = newBendRange;
		}
	}
	return Error::NONE;
}
```
NOTE on the `noteData` chain: declare `const Value* n = nullptr;` before the chain so the `else if ((n = …))` assignments compile (or restructure as nested `if`s). NOTE on `readLegacyNotes`: the pre-1.4 `<notes>` array loop (lines under the old `else if (!strcmp(tagName, "notes"))`) must also be extracted into a private `Error NoteRow::readLegacyNotes(Deserializer& reader)` helper as part of Task 1 if you prefer — OR, since it's rarely-hit legacy, inline the existing loop body into the lambda here verbatim. Pick extraction for symmetry; either is behaviour-identical. (If extracting, add its declaration in Task 1 Step 1 and move its body there.)

- [ ] **Step 4: Drive `readFromTree` from `InstrumentClip::readFromFile`.** Replace the `noteRows` handler's array loop. Currently:
```cpp
		else if (!strcmp(tagName, "noteRows")) {
			reader.match('[');
			int32_t minY = -32768;
			while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "noteRow")) {
					NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(std::ssize(noteRows));
					if (!newNoteRow) { goto ramError; }
					error = newNoteRow->readFromFile(reader, &minY, this, song, readAutomationUpToPos);
					if (error != Error::NONE) { goto someError; }
				}
				reader.match('}');
				reader.exitTag(nullptr, true);
			}
			reader.match(']');
		}
```
becomes (read the `noteRows` node tree-natively via `currentValue()`, iterating BOTH representations — `parseXml` gives a **Map with repeated `"noteRow"` keys**, `parseJson` gives an **Array of `{noteRow:{…}}` boxes** — then drive `readFromTree` per row; the trailing outer `reader.exitTag()` skips the streaming `noteRows` element):
```cpp
		else if (!strcmp(tagName, "noteRows")) {
			int32_t minY = -32768;
			const deluge::storage::Value* rowsNode =
			    static_cast<deluge::storage::ValueTreeDeserializer&>(reader).currentValue();
			if (rowsNode != nullptr) {
				const bool asArray = rowsNode->isArray(); // JSON: array of {noteRow:{…}}; XML: map of noteRow entries
				const size_t count = asArray ? rowsNode->items().size() : rowsNode->entries().size();
				for (size_t idx = 0; idx < count; idx++) {
					const deluge::storage::Value* rowNode;
					if (asArray) {
						const deluge::storage::Value& item = rowsNode->items()[idx];
						rowNode = item.isMap() ? item.find("noteRow") : nullptr;
					}
					else {
						const auto& [key, value] = rowsNode->entries()[idx];
						rowNode = (key == "noteRow") ? &value : nullptr;
					}
					if (rowNode == nullptr) { continue; }
					NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(std::ssize(noteRows));
					if (!newNoteRow) { goto ramError; }
					error = newNoteRow->readFromTree(reader, *rowNode, &minY, this, song, readAutomationUpToPos);
					if (error != Error::NONE) { goto someError; }
				}
			}
		}
```
`Value::isArray/items/entries/isMap/find` are confirmed present. The gotos are inline (not in a lambda), so `ramError`/`someError` work. Add `#include "storage/value.h"` + `value_tree_deserializer.h` to instrument_clip.cpp if not already present (slice 4 added value.h).

- [ ] **Step 5: Delete `NoteRow::readFromFile`** (declaration in `note_row.h` + definition in `note_row.cpp`). `readFromTree` is its sole reader.

- [ ] **Step 6: Build firmware.** `./dbt build Debug 2>&1 | tail -1` — must link (catches signature/enum/include errors).

- [ ] **Step 7: Build sim + full golden gate.**
```
cmake --build build-sim --target song_roundtrip edit_undo_redo param_record_diff json_roundtrip 2>&1 | tail -1
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip 2>&1 | tail -1
./dbt test 2>&1 | grep -iE "tests passed|failed" | head -1
```
Expected: `OK=16 REGRESS=0`, `OK=40`, `OK=3`, `OK=38`, `100% tests passed`, links. A `song_roundtrip` REGRESS is the signal — dump the REGRESSing song's gen1 and diff around its `noteRows` (likely the array unwrap shape in Step 4, or a `noteData` variant guard). A `json_roundtrip` FAIL points at a bridge descent issue.

- [ ] **Step 8: Commit.** `git commit -m "note: NoteRow::readFromTree (first per-type tree conversion) + readScalarValue; retire readFromFile"`.

---

## Self-Review

**Spec coverage.** Task 2 covers the spec's mapping table, the `readScalarValue` primitive, the InstrumentClip driver, and the `readFromFile` retirement. Task 1's helper extraction is the de-risking prerequisite for reusing the `noteData`/`soundParams` logic verbatim. `readLegacyNotes` is the one addition beyond the spec's table (the pre-1.4 `notes` array) — same reuse pattern.

**Placeholder scan.** Step 4's tree-shape unwrap is marked VERIFY (a real check against `parseXml`'s array representation, not a placeholder) with a concrete fallback. All other code is concrete.

**Type consistency.** `find()->const Value*` + `str()`/`stringToInt`; `readChildObject`/`readScalarValue` templates on `ValueTreeDeserializer` (reached via `static_cast`); `readNoteDataHex(Deserializer&, uint16_t)`/`readSoundParams(…)`/`readLegacyNotes(Deserializer&)` helper signatures consistent between note_row.h and .cpp; `drum` sentinel casts + `newBendRange` post-application copied verbatim.

**Note for the executor.** Task 1 is golden-neutral (validate before Task 2). Task 2's highest-risk points are the Step 4 array-unwrap shape and the `noteData` variant selection; the bit-identical `song_roundtrip` across the whole corpus is the backstop. If Task 1's goldens aren't identical, STOP — the extraction diverged.
