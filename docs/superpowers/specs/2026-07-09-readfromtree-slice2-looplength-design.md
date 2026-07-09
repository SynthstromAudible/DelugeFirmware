# readFromTree slice 2 — loopLength (complete the DocClip shell) — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-09
**Parent:** durable-id Step 3 (`docs/superpowers/specs/2026-07-09-durable-id-step3-id-first-clip-construction-design.md`), which moved the clip shell (`id`, `section`, `colourOffset`, `launchStyle`) into tree-native reads in `readClipsFromFile` and delegated the rest to legacy `readFromFile`. It **deferred `loopLength`** because the `length`/`trackLength` handler has side effects. This slice finishes it.

## Goal

Move `length`/`trackLength` (loopLength) into the tree-native clip read, completing the persistent **DocClip shell** — all five structural fields (`id`, `section`, `colourOffset`, `launchStyle`, `loopLength`) are then read directly from the `Value` node, and the last shell handler leaves `Clip::readTagFromFile`.

## Why it was deferred, and why it's safe now

The `length`/`trackLength` handler does three things:
```cpp
setLoopLength(reader.readTagOrAttributeValueInt());
setLoopLength(std::max((int32_t)1, loopLength()));
originalLength = loopLength();       // transient Clip member
*readAutomationUpToPos = loopLength(); // bounds subsequent note/param automation reads
```
`readAutomationUpToPos` is a local in `readFromFile` passed to every automation-bearing read (notes, params, instrument, expression). The worry was read ordering. **Verified safe:** `length` is written as a clip **attribute** (`Clip::writeDataToFile` → `writer.writeAttribute("length", …)`), so it always precedes every child element (notes/params) in the file. The handler therefore always sets `readAutomationUpToPos` *before* any automation read — so initializing it from `loopLength()` at the top of `readFromFile` (once the loader has read length) is exactly equivalent.

## Design

1. **`readClipsFromFile`** (`src/deluge/model/song/song.cpp`, in the shell-read block after `launchStyle`): read `length` or `trackLength` from `clipNode`; if present:
```cpp
if (const deluge::storage::Value* n = clipNode->find("length") ? clipNode->find("length") : clipNode->find("trackLength")) {
    newClip->setLoopLength(std::max<int32_t>(1, stringToInt(n->str().c_str())));
    newClip->originalLength = newClip->loopLength();
}
```
(`originalLength` is a public `Clip` member, already accessed freely in the loader. Files carry `length`; `trackLength` is the legacy alias — check both.)

2. **Both `readFromFile` bodies** — `InstrumentClip::readFromFile` (init at ~2504) and `AudioClip::readFromFile` (init at ~1123): change
```cpp
int32_t readAutomationUpToPos = kMaxSequenceLength;
```
to
```cpp
int32_t readAutomationUpToPos = loopLength() > 0 ? loopLength() : kMaxSequenceLength;
```
When `length` was present the loader set `loopLength() >= 1`, so this is `loopLength()`; when absent, `loopLength()` is the ctor default 0, so it stays `kMaxSequenceLength` — matching the old behaviour in both cases.

3. **Remove the `length`/`trackLength` handler** from `Clip::readTagFromFile` (the `else if (!strcmp(tagName, "trackLength") || !strcmp(tagName, "length"))` block). Removing it makes the tag fall through to the final `else reader.exitTag(tagName)` (skipped) — correct, since the loader already read it.

## Scope

**In:** the three edits above. **Out:** everything else in the clip (view state, instrument ref, notes, params) — later, larger `readFromTree` slices; and non-clip types.

## Testing — the gate

Pure behaviour-preserving: `song_roundtrip` `OK=15 REGRESS=0` (bit-identical, the hard gate — it catches any `originalLength`/`readAutomationUpToPos`/ordering divergence), `edit_undo_redo` `OK=40`, `param_record` `OK=3`, `json_roundtrip` fixtures 14/14 + backup `OK=37`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.

## Risks

1. **`readAutomationUpToPos` semantics** — the whole reason for care; mitigated by the attribute-ordering proof above and the bit-identity golden.
2. **`trackLength` legacy alias** — the loader must check both keys (old files use `trackLength`); a corpus song with the legacy name is the check.
