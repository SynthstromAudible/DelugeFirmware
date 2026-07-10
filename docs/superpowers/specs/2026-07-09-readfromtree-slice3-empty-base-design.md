# readFromTree slice 3 — empty `Clip::readTagFromFile` — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-09
**Parent:** readFromTree slice 2 (`docs/superpowers/specs/2026-07-09-readfromtree-slice2-looplength-design.md`), which completed the DocClip shell (id/section/colourOffset/launchStyle/loopLength read tree-natively in `readClipsFromFile`). This slice reads the **remaining** base-clip tags tree-natively and deletes `Clip::readTagFromFile` entirely.

## Goal

Empty `Clip::readTagFromFile`: every base-clip tag it still handles is read directly from the clip's `Value` node in `readClipsFromFile`, and the method (plus its declaration and both subclass `else{}` fallbacks) is deleted. This also introduces the **reusable nested-read primitive** (`ValueTreeDeserializer::readChildObject`) that every future nested slice — notes, params, expression — will use to drive a shared streaming sub-reader against a child `Value` node.

## What's left in `Clip::readTagFromFile`

Three groups (`Clip::readTagFromFile`, `src/deluge/model/clip/clip.cpp:717`):

1. **Plain attributes** — pure field writes, same shape as slices 1-2:
   - `isPlaying` → `activeIfNoSolo`
   - `isSoloing` → `soloingInSessionMode`
   - `isArmedForRecording` → `armedForRecording`
   - `status` (legacy back-compat) → `soloingInSessionMode = false; activeIfNoSolo = (value == 2)`
   - `sequenceDirection` → `sequenceDirectionMode = stringToSequenceDirectionMode(value)`

2. **Selection / sync-scale hints** — read a flag, then mutate *Song* state:
   - `beingEdited` → if set: `song->setCurrentClip(this); song->runtimeView.inClipMinderViewOnLoad = true;`
   - `selected` → if set: `song->setCurrentClip(this); song->runtimeView.inClipMinderViewOnLoad = false;`
   - `isSyncScaleClip`/`isSyncScaleTrack` (legacy alias) → if set: `song->project.transport.sync_scaling_clip = this;`

3. **Nested child element** — `muteMidiCommand` → `muteMIDICommand.readNoteFromFile(reader)`, a shared streaming sub-reader (`LearnedMIDI::readFromFile`, reads channel/mpeZone/device/note).

(The commented-out `activeModFunction` block is dead and goes with the method.)

## Why deletion (not just emptying) is safe

Both subclasses dispatch unrecognized tags to the base via an `else` branch, with `reader.exitTag()` after the whole if/else chain:

```cpp
// instrument_clip.cpp:2915 / audio_clip.cpp:1216
else {
    readTagFromFile(reader, tagName, song, &readAutomationUpToPos);
}
```

Once every base tag is read by the loader, those tags reappear during the streaming `readFromFile` pass (the loader read the clip node's attributes, then `readFromFile` re-streams the same node) and are simply **skipped** — exactly as slices 1-2's id/section/etc. already are. Removing the `else` branch entirely makes an unrecognized tag fall straight to the existing `reader.exitTag()` — byte-for-byte the same as the empty method would have done. So the `else{}` branch, the method, and its declaration all go. The `&readAutomationUpToPos` argument disappears with the branch; the `readAutomationUpToPos` local stays (still passed to note/param reads).

## The nested-read primitive

`LearnedMIDI::readFromFile` is shared far beyond clips (MIDI-learn on params, device references, etc.) and must not be rewritten tree-native. Instead, add to `ValueTreeDeserializer` (`src/deluge/storage/value_tree_deserializer.h`):

```cpp
/// Position the cursor inside `childObject` (a Map most recently produced by a name read, or handed
/// in directly by a tree-native loader), invoke `body` -- which drives the ordinary streaming
/// interface to read the object's children, exactly as a mid-stream readFromFile would -- then
/// restore the cursor to precisely where it was. The bridge that lets a tree-native loader delegate
/// a nested object to a shared streaming sub-reader (e.g. LearnedMIDI::readFromFile) without
/// rewriting that reader tree-native.
template <typename F>
void readChildObject(const Value& childObject, F&& body) {
    auto savedStack = stack_;
    const Value* savedPending = pending_;
    std::size_t savedCharCursor = charCursor_;
    int32_t savedCallerDepth = callerDepth_;

    enterChildObject(childObject); // non-template helper (.cpp): cursor state as-if just descended in
    body();

    stack_ = std::move(savedStack);
    pending_ = savedPending;
    charCursor_ = savedCharCursor;
    callerDepth_ = savedCallerDepth;
}
```

`enterChildObject(const Value& childObject)` is a private non-template method in the `.cpp` that puts the cursor in the state the sub-reader's first `readNextTagOrAttributeName()` expects — i.e. positioned *inside* `childObject`, ready to read its children — by mirroring the existing map-entry descent (`pending_ = &childObject; match('{');`, adjusting `callerDepth_` the same way a real `readNextTagOrAttributeName()`+`match('{')` pair does in XML mode). The exact entry sequence is derived against both the Xml and Json branches of `match`/`readNextTagOrAttributeNameXml` when implementing.

**Key safety property:** because `readChildObject` saves and restores the *entire* cursor state around `body()`, the sub-read is **hermetic** — no matter how `enterChildObject`'s depth accounting behaves, it cannot desync the parent stream. The only correctness obligation is that `body()` reads the right values out of `childObject`, which the golden validates directly (a corpus song with a `muteMidiCommand` round-trips bit-identically only if `muteMIDICommand` is read correctly).

The loader call site (in `readClipsFromFile`, after the attribute reads):

```cpp
if (const deluge::storage::Value* muteNode = clipNode->find("muteMidiCommand")) {
    auto& treeReader = static_cast<deluge::storage::ValueTreeDeserializer&>(reader);
    treeReader.readChildObject(*muteNode, [&] { newClip->muteMIDICommand.readNoteFromFile(reader); });
}
```

(`readClipsFromFile` already `static_cast`s `reader` to `ValueTreeDeserializer` for `currentValue()`, so the cast idiom and include are established.)

## Design — the loader reads

In `Song::readClipsFromFile` (`src/deluge/model/song/song.cpp`), extend the tree-native shell-read block (after the slice-2 `length` read) with the group-1 and group-2 reads, then the group-3 nested read. Each attribute uses the established `clipNode->find(key)` → `str()`/`stringToInt` idiom; absent keys leave the ctor default (matching the old "tag not present" behaviour). `status` and `isSyncScaleTrack` are legacy aliases — check the current name first, then the alias.

Group 2's side effects call the same `song->setCurrentClip(this)` / `song->project.transport.sync_scaling_clip = this` as today; because clips are processed in file order and these are last-wins singletons, the outcome is identical to firing them mid-`readFromFile`.

Then remove all three groups' handlers from `Clip::readTagFromFile`, which empties it, and delete it.

## Scope

**In:** the loader reads for the ten remaining base-clip tags; the `readChildObject` primitive + `enterChildObject` helper; deletion of `Clip::readTagFromFile` (definition, `clip.h` declaration, both subclass `else{}` fallbacks). **Out:** subclass-specific tags (`InstrumentClip`/`AudioClip` keep their own `readTagFromFile`); notes/params/expression (later slices, which reuse `readChildObject`); any DocClip-store ownership change (these stay plain `Clip`/`Song` members — this is read-path relocation only).

## Testing — the gate

Behaviour-preserving; bit-identity is the gate. `song_roundtrip` `OK=15 REGRESS=0`, `edit_undo_redo` `OK=40`, `param_record` `OK=3`, `json_roundtrip` fixtures 14/14 + backup `OK=37`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.

Corpus coverage of the moved tags: `selected` 34 songs, `beingEdited` 11, `sequenceDirection` 3, `muteMidiCommand` 1 — all exercised. `isSyncScaleClip`/`isSyncScaleTrack`: **0 songs** — see Risks.

## Risks

1. **`readChildObject` dual-mode depth accounting** — the one subtle piece; mitigated by the full save/restore (hermetic sub-read) plus the `muteMidiCommand` corpus song round-tripping bit-identically in both the XML-tree (`song_roundtrip`) and JSON (`json_roundtrip`) nets.
2. **`isSyncScaleClip` golden-blind** — 0 corpus songs carry it, so the sync-scale move is unverified by the goldens. It is a mechanical one-line pointer set (`transport.sync_scaling_clip = this`) identical to the deleted handler; flagged for Kate. A targeted fixture can be added later (per the "add harnesses when we have valid files" convention).
3. **Selection hints fire pre-`readFromFile`** — `beingEdited`/`selected` now call `setCurrentClip` before the clip body is read rather than during it. Nothing in the remaining clip read depends on `currentClip`; covered by 45 corpus songs.
