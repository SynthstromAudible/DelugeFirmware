# Record-First Deserializer — Step 2b: Faithful-Replay Swap — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-09
**Parent:** `docs/superpowers/specs/2026-07-09-record-first-deserializer-design.md`; builds on Step 2a (`parseXml`, `ValueTreeDeserializer` XML mode — both landed). This is the second half of Step 2 and the highest-risk step.

## Goal

Route the whole **song** load through the tree: read the whole song file, parse it to a `Value` (`parseXml`/`parseJson`), wrap it in `ValueTreeDeserializer{tree, mode}`, and run the existing `Song::readFromFile` over that adapter instead of the file stream — **bit-for-bit identically** to the streaming load. This is the step that makes Step 3 (id-first clip construction) possible, and it proves the adapter's fidelity over the entire load, not just Step-2a's harness fragments.

## Rollout: hard-swap (no fallback)

The song load runs via the tree, one path — no runtime toggle. The golden gate (load-via-tree ≡ load-via-stream, bit-identical) is the sole safety net; if it's green, ship it. This forces the fidelity issue to be fully resolved now rather than deferred behind a flag. Consequence: on-device is committed immediately and the peak-RAM regression can't be toggled off — so the on-device RAM check (below) is a hard gate.

## The swap

At the song-load seam (`load_song_ui.cpp` — `openDelugeFile(currentFileItem, "song")` at :344, then `deluge::project::loadSong(*preLoadedSong, *activeDeserializer)` at :449), replace "open → stream reader → `loadSong`" with:

```
bytes  = readWholeSongFile(fileItem)                              // whole file → heap buffer
tree   = endsWith(name, ".Json") ? parseJson(bytes) : parseXml(bytes)   // → std::optional<Value>
if (!tree) return Error::FILE_CORRUPTED;
free(bytes)                                                       // tree owns its scalar copies
adapter = ValueTreeDeserializer{*tree, endsWith(name,".Json") ? SourceFormat::Json : SourceFormat::Xml}
loadSong(*preLoadedSong, adapter)                                // existing read, tree-backed
```

Format detection reuses the existing `.Json`-filename convention (`openDelugeFile`). The whole-file read is a new small helper (`f_open`+`f_size`→allocate→`f_read`, or a loop over the existing cluster reader). **Nothing in `Song::readFromFile` or any sub-`readFromFile` changes** — they run unchanged against the adapter.

Scope is **song-file-only**: everything inline in the song (clips, inline instruments, params, note rows) flows through the adapter; externally-referenced preset/kit *files* keep streaming (their own `openDelugeFile` calls, untouched) — their migration is later.

### `firmwareVersion` (resolves the Step-2a stub)

`tryReadingFirmwareTagFromFile` is format-agnostic — the real `XMLDeserializer` impl is: `firmwareVersion` → `readTagOrAttributeValue()` → `song_firmware_version = FirmwareVersion::parse(...)`; `earliestCompatibleFirmware` → parse + if newer than current and not `ignoreIncorrectFirmware` → `FILE_FIRMWARE_VERSION_TOO_NEW`; else `RESULT_TAG_UNUSED`. The adapter reproduces it verbatim (it has `readTagOrAttributeValue`). `song_firmware_version` gates ~dozens of version-conditional parse branches, so this must be correct; corpus songs carry `firmwareVersion` (e.g. `c1.3.0`), so golden exercises it. The too-new rejection now fires *during* the read rather than at file-open — golden-neutral (corpus songs are all compatible).

## RAM and lifetime

Peak during parse ≈ whole-file buffer + the growing `Value` tree (scalars are string copies) ≈ 2× file size; a song is up to a few MB (note/param text + the `preview` hex blob; samples are **not** inline). Free the buffer right after `parse*`; tree + Model coexist during the read. On 64MB SDRAM with sample data not yet resident at song-load time, this should fit — **but with no toggle-off, verifying peak on-device with the largest realistic song is a hard gate.** If tight, the deferred optimization is `Value` scalars as `string_view`s into a retained buffer (a Step-1 `Value` change).

Lifetime: buffer, tree, adapter are locals of the load operation — buffer freed after `parse*`, tree+adapter live across `loadSong`, destroyed on return. `Value`'s internals are already heap-backed.

**New failure mode — OOM:** unlike streaming, this needs the whole file + tree in RAM at once, so buffer/tree allocation can fail on a big song / low memory — that path must return `INSUFFICIENT_RAM` cleanly, not crash.

**Positioning:** `parseXml` yields a Map whose single entry is `song` → the `<song>` element Map; `loadSong`/`readFromFile` expects the reader positioned to read the song's tags (`match('{')` into `song`, then loop). The adapter root must line up with the streaming reader's sequence — an off-by-one diverges immediately (golden catches it), but it's the concrete wiring detail.

## Testing — golden is the sole net

- **The golden harnesses become the differential test.** `song_roundtrip`, `edit_undo_redo`, `param_record_diff` currently read fixture bytes and call `loadSong(*song, XMLDeserializer{bytes})` directly. Switch them to `parseXml(bytes) → Value → ValueTreeDeserializer{tree, Xml} → loadSong(adapter)`. Then load-via-tree → save → compare against the existing stream-generated baseline is the differential test: **bit-identical over the 15-song corpus means the adapter faithfully replays every `readFromFile` call the real load makes.** Gates: `songs` `OK=15 REGRESS=0`, `param_record` `OK=3`, `edit_undo_redo` `OK=40`, firmware links.
- **JSON round-trip fixture** (closes the last Step-2a carry-forward — JSON mode/`parseJson` are still unexercised end-to-end): load an XML corpus song, save it as JSON (`JSONSerializer`), load *that* via `parseJson` + `SourceFormat::Json`, and assert the resulting song re-saves identically to the XML load.
- **Debugging a golden diff:** any divergence → capture the divergent song's structure as a new `xml_replay_diff` fragment to isolate the adapter bug (the Step-2a harness is the isolation tool).
- **On-device (yours to run):** peak-RAM check with the largest realistic song; a real load-and-play smoke.

## Sequencing

1. Adapter `tryReadingFirmwareTagFromFile` real impl + the whole-file-read helper (additive, unit-testable).
2. Flip the golden harnesses to load-via-tree; drive to bit-identical (the debugging grind).
3. Wire the `performLoad` production seam once golden is green.
4. The JSON round-trip fixture.

Then Step 3 (id-first clip construction; `ownDoc_`-kill).

## Risks

1. **Whole-load fidelity** — the crux; golden is the net, the Step-2a `xml_replay_diff` harness is the isolation tool for any diff.
2. **Peak RAM** — the gating on-device check; no toggle-off.
3. **OOM** — new failure path (return `INSUFFICIENT_RAM`, don't crash).
4. **JSON untested by corpus** — the round-trip fixture.
5. **`song`-root positioning** — golden catches it, but the concrete wiring detail.

## Scope

**In:** the song-file load swap (whole-file read → parse → adapter → `loadSong`), the `firmwareVersion` real impl, harness/golden flip to load-via-tree, the JSON round-trip fixture.

**Out:** preset/instrument *file* loads (still streaming; later); deleting the streaming `XMLDeserializer`/`buildValue` (only once presets migrate + JSON-only cutover); the `Value`-string_view RAM optimization (only if on-device RAM is tight); Step 3's id-first construction.
