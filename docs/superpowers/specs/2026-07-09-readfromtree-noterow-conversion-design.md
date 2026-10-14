# readFromTree — per-type conversion: NoteRow — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-09
**Parent:** readFromTree slice 4 (`docs/superpowers/specs/2026-07-09-readfromtree-slice4-view-state-design.md`) exhausted the dependency-free attribute peels via `readShellFromTree`. The remaining migration is per-type conversion of `readFromFile(Deserializer&)` streaming readers into tree-native `readFromTree(const Value&)` readers, dismantling the streaming interface one type at a time. NoteRow is the first — a self-contained leaf.

## Goal

Convert `NoteRow::readFromFile(Deserializer&, …)` into `NoteRow::readFromTree(Deserializer&, const Value& node, …)` that navigates the clip's note-row `Value` node directly, and drive it from `InstrumentClip::readFromFile`'s `noteRows` handler. Establish the reusable per-type conversion pattern and the `readScalarValue` primitive. Behaviour-preserving; golden bit-identity is the gate.

## The reusable pattern (every later type follows)

`Error Type::readFromTree(Deserializer& reader, const deluge::storage::Value& node, …)`:
- **Scalars** — read from `node.find(key)->str()` (with the same clamp/cast/side-effect the streaming handler had).
- **Nested objects still on the streaming reader** (types not yet converted) — drive their existing `readFromFile`/`read*FromFile` via `reader`'s `readChildObject(childNode, body)`. This is the incremental bridge: NoteRow converts without first converting Sound/LearnedMIDI. `readChildObject` works for **both** sub-reader conventions: implicit-descent readers (`LearnedMIDI::readFromFile`, no `match('{')`) read the entered frame's children directly; self-descending readers (`Sound::readParamsFromFile`, which calls `match('{')` first) see that `match('{')` fail-safe — the cursor is already *inside* the child (a Map frame, `pending_==null`), so `match('{')` returns false and its ignored return leaves the reader correctly positioned at the child's members. Verified against `match`'s JSON+XML branches.
- **Scalar blobs read char-at-a-time** (hex note data) — new `readScalarValue(scalarNode, body)` positions the cursor at the scalar so the existing char decoders work unchanged.
- **Errors** propagate by `return`; there is no cross-function `goto` (the streaming version's `goto ramError/someError` become `return Error::…`).

`reader` is still passed (typed `Deserializer&`, `static_cast` to `ValueTreeDeserializer&` for the bridges, as `readClipsFromFile` already does) because the nested/blob bridges need the live cursor. `node` supplies the scalars.

## New primitive: `readScalarValue`

`ValueTreeDeserializer` (`value_tree_deserializer.h`), sibling to `readChildObject`:
```cpp
/// Position the cursor at a scalar Value so the char-at-a-time value readers
/// (prepareToReadTagOrAttributeValueOneCharAtATime / readNextCharsOfTagOrAttributeValue / …) read from
/// it, run `body`, then restore the cursor exactly. Lets a tree-native reader hand a scalar (e.g. a hex
/// noteData blob) to existing char-decoding code without rewriting it. Hermetic (full save/restore).
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
The char readers key off `pendingScalar()` (which returns `pending_` when it is a raw-string scalar) and `charCursor_`; setting those two is all that is needed. Unlike `readChildObject` there is no frame descent — the char API never touches `stack_` — but stack_ is saved/restored anyway for uniformity and safety.

## NoteRow::readFromTree mapping

Signature: `Error NoteRow::readFromTree(Deserializer& reader, const deluge::storage::Value& node, int32_t* minY, InstrumentClip* parentClip, Song* song, int32_t readAutomationUpToPos)`. Body reads (each guarded by `node.find(key)` presence, mirroring "tag present"):

| Source tag | Tree read |
|---|---|
| `muted`, `y`, `colourOffset` | `int` → member |
| `drumIndex` | `drum = (Drum*)stringToInt(...)` (int-in-pointer sentinel, as today) |
| `gateOutput` | clamp 0..NUM_GATE_CHANNELS-1 → `drum = (Drum*)(0xFFFFFFFE - gateChannel)` |
| `length` | `loopLengthIfIndependent = int; readAutomationUpToPos = loopLengthIfIndependent;` |
| `sequenceDirection` | `stringToSequenceDirectionMode(str)` |
| `bendRange` | `newBendRange = int` (local; applied after) |
| `muteMidiCommand` | `readChildObject(child, [&]{ muteMIDICommand.readNoteFromFile(reader); })` |
| `soundMidiCommand` | `readChildObject(child, [&]{ midiInput.readNoteFromFile(reader); })` |
| `soundParams` | `readChildObject(child, [&]{ …paramManager setup + Sound::readParamsFromFile… })` — the pre-2.0 backup hack + `Sound::readParamsFromFile` reused; its inner `goto finishedNormalStuff` stays inside the lambda/helper (local goto, legal), errors surface via a captured `Error` |
| `notes` (pre-1.4 legacy) | `readChildObject(child, [&]{ …existing note-array loop… })` |
| `noteData` (V1.4+) | `readScalarValue(child, [&]{ …existing hex decode loop… })`; its `goto getOut/useDefaultLift` are local to the decode block and stay |

`drum` is initialised to the `0xFFFFFFFF` "no drum" sentinel at the top exactly as today. The `newBendRange` post-application block and any other post-loop logic in the current `readFromFile` are carried over verbatim after the reads. `reader.exitTag()` per-tag is no longer needed (no streaming loop); the tree reads consume nothing from the cursor except within the bridges, which restore it.

Ordering note: the only intra-NoteRow order dependency is `length` → `readAutomationUpToPos` (bounds subsequent automation reads inside `soundParams`/`noteData`). `length` is an attribute (written before child elements), and `find()` lets us read it first regardless of file order, so we read `length` before invoking the `soundParams`/`noteData` bridges — equivalent to the streaming order.

## Driving it from InstrumentClip

In `InstrumentClip::readFromFile`, the `noteRows` handler currently does `match('[')` + an array-of-boxes loop calling `newNoteRow->readFromFile(reader, …)`. Replace with: obtain the `noteRows` array node via `static_cast<ValueTreeDeserializer&>(reader).currentValue()`, iterate its array items, and for each: `NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(std::ssize(noteRows));` (RAM-check → `goto ramError` as today) then `error = newNoteRow->readFromTree(reader, item, &minY, this, song, readAutomationUpToPos);` (→ `goto someError` on failure). The trailing `reader.exitTag()` after the handler skips the streaming `noteRows` element (idiom already used for base attributes). `minY` threading is preserved (passed by pointer, mutated across rows).

`NoteRow::readFromFile` is then retired (declaration + definition removed); `readFromTree` is its sole reader.

## Scope

**In:** `readScalarValue` primitive; `NoteRow::readFromTree` (full mapping above); the `InstrumentClip` `noteRows` driver; removal of `NoteRow::readFromFile`. **Out:** converting Sound/LearnedMIDI/ExpressionParamSet themselves (they stay streaming, reached via `readChildObject`); other clip content (params, expression, instrument-ref); other types.

## Testing — the gate

`song_roundtrip` `OK=16 REGRESS=0` (bit-identical — note/`noteData` decode is exercised by essentially every instrument clip in the corpus, the hard gate), `edit_undo_redo` `OK=40`, `param_record` `OK=3`, `json_roundtrip` `OK=38`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.

## Risks

1. **`noteData` blob via `readScalarValue`** — the highest-value path; mitigated by reusing the decode verbatim (only the cursor positioning changes) and the bit-identical golden across the whole corpus. The char readers key off `pending_`/`charCursor_` only, which `readScalarValue` sets.
2. **`readAutomationUpToPos` ordering** — `length` must be read before `soundParams`/`noteData`; handled by reading scalars first, then the bridged blocks.
3. **`soundParams` legacy backup hack** — carried verbatim inside a `readChildObject` lambda/helper; its local `goto finishedNormalStuff` stays local. Sparse corpus coverage (pre-2016 files), so reproduce the current control flow exactly rather than "improving" it.
4. **`minY` threading + drum sentinel** — `minY` stays a pointer param mutated across rows; `drum`'s int-in-pointer sentinel logic is copied verbatim (later resolved to real drums by existing post-load code, unchanged).
