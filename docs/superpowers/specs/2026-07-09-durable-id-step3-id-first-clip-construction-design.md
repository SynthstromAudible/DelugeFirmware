# Durable-ID Step 3 — id-first clip construction / `ownDoc_`-kill (readFromTree slice 1) — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-09
**Parent:** the durable-id-unification program (spec `2026-07-08-durable-id-unification-design.md`) and the record-first deserializer (`2026-07-09-record-first-deserializer-design.md`, Steps 1/2a/2b landed). This is the step the whole deserializer detour was *for*.

## Goal

Read a clip's durable `id` from the loaded document **before** constructing the clip, so the `Clip` ctor binds its DOM structure record (`clip_structure[id]`) at construction — the **ctor-binding invariant**. That retires the transient `ownDoc_`, the `treeDoc_`-fallback in every accessor, the migrate-on-`setId` rekey, and `adoptClip`'s seed-then-flip. It also lands the **first slice of the `readFromTree` migration** (the project-model north-star end-state): the clip *shell* (id + the four `DocClip` fields) is read tree-natively; everything else in the clip still delegates to the legacy streaming `readFromFile` for now.

This is possible only because Step 2b made the **Value tree the sole song-load path** — the streaming parse-as-you-construct reader (which could not give identity-before-values) no longer drives a song load.

## Scope

**In:** the clip loader (`readClipFromTree`), the `Clip`/`InstrumentClip`/`AudioClip` ctor change (ctor-binding + `loadedId`), moving the four shell tags out of `readFromFile` into `readClipFromTree`, and the resulting deletions (`ownDoc_`, `treeDoc_`-fallback, `adoptClip`, `Clip::setId` + the `"id"` handler, migrate-on-`setId`).

**Out:** the transient `sessionId`/`mintSessionId`/`assignSessionIds` machinery (durable-id Phase 4); Output/Drum id-first construction (they mint ids but bind no structure store — their own phase); moving any non-shell clip content (view state, instrument ref, notes, params) into `readFromTree` (later `readFromTree` slices); presets/instrument *files* (still stream; not clips).

## Chosen approach (why `readFromTree` over a peek-hack)

Two ways to get the id before construction were weighed: (a) a `peek` on the streaming adapter that keeps the legacy `readFromFile` intact, and (b) start a genuine per-type `readFromTree` loader that reads the shell from the `Value` directly and *removes* those handlers from `readFromFile`. We chose (b): (a) is a dead-end thrown away when `readFromTree` lands, whereas (b) invests in the north-star serde now, establishes the "peel tags off `readFromFile`" pattern that future slices extend, and reads the shell from the tree the way the end-state will.

## Component 1 — `ValueTreeDeserializer::currentValue()`

A small read-only accessor returning the `Value*` the adapter is currently positioned at (its `pending_` node — the clip-content Map most recently named by `readNextTagOrAttributeName`). This is how the loader reaches the clip's `Value` without changing `loadSong`'s signature (it still takes only the `Deserializer&`). It is the seed of tree-native access: later slices read more keys off this node.

```cpp
[[nodiscard]] const deluge::storage::Value* currentValue() const { return pending_; }
```

Declared on `ValueTreeDeserializer`; `readClipsFromFile` obtains the concrete adapter type via the load path (the song load constructs the adapter, so the loader can hold it as `ValueTreeDeserializer&`, or the accessor is exposed through a narrow interface — the plan picks the least-invasive wiring, since only the tree adapter is ever the song reader now).

## Component 2 — ctor-binding

The `Clip` base ctor becomes:

```cpp
Clip(ClipType newType, Song& song, uint32_t loadedId = 0);
// body:
id_ = loadedId != 0 ? loadedId : song.generateNewClipId();
treeDoc_ = &song.project.getOrCreateDocClip({.clip_id = id_}); // bind at construction; never null hereafter
```

- `InstrumentClip`/`AudioClip` ctors thread `loadedId` (default 0) to the base. The ~17 construction sites split cleanly: the two **load** sites in `readClipsFromFile` pass the file id; every **runtime/clone** site passes nothing (0 → mints), exactly as today.
- `ownDoc_` is **deleted**. Because `treeDoc_` is bound from birth, every accessor collapses from `(treeDoc_ != nullptr ? treeDoc_ : &ownDoc_)->field` to `treeDoc_->field`.
- The `DocClip` member initializers (`loop_length{0}`, `colour_offset{-60}`, `section{0}`, `launch_style{…}`) supply a fresh entry's defaults, so a just-constructed clip works immediately — no bind-at-creation step, no seed pass.
- Lifetime: `~Clip` already calls `removeDocClip` (clip.cpp) to drop the entry on destruction. Clones mint a fresh id and bind their own entry in the ctor; `copyBasicsFrom` copies the field *values* through the setters into that bound store (it must use the setters, not touch a raw field — verified in the plan).
- The `clipIdCounter_` advance-past-loaded-ids logic (song.cpp ~3638) stays: loaded clips use the file id, so the counter must skip past them to avoid a later runtime-clip collision.

## Component 3 — `readClipFromTree` and the delegation seam

`readClipsFromFile` (the clips-array loop) per element:

1. read the element name (`instrumentClip` / `track` → INSTRUMENT, `audioClip` → AUDIO) → clip type.
2. `const Value* clipNode = adapter.currentValue();`
3. read the id from the node — `const Value* idNode = clipNode->find("id"); uint32_t loadedId = idNode ? stringToInt(idNode->str().c_str()) : 0;` (the first tree-native read; `Value` stores raw-text scalars — `find(key)→const Value*` then `str()` — matching `parseJson`/`parseXml`).
4. construct `new InstrumentClip(song, loadedId, /*initViewFromSong=*/false)` or `new AudioClip(song, loadedId)` — ctor binds `clip_structure[loadedId]`.
5. read the four shell fields from `clipNode` and apply via the existing setters (which now write straight to the bound store): `section`, `colourOffset`, `length`/`trackLength` (→ `setLoopLength`), `launchStyle`. Missing keys keep the ctor defaults.
6. `clip->readFromFile(adapter)` for **everything else**. The adapter walks the same clip node; the four shell tags (and `id`) no longer have handlers, so they fall through to `exitTag(tagName)` (skipped) — no double-read of values.
7. insert the clip (unchanged).

`readFromTree` owns the clip *shell*; the adapter still drives the clip *contents*. The `readFromFile` handlers deleted: `id`, `section`, `colourOffset`, `length`/`trackLength` (loopLength), `launchStyle`. All are in `Clip::readTagFromFile` (the shared base), so both clip types benefit.

**Behavioral nuance (golden-verified):** the shell fields are now set *before* `readFromFile`'s content reads, rather than inline in file order. `loopLength` in particular becomes available throughout the content read (generally equivalent-or-safer, since it can only be needed, not un-needed, earlier). The bit-identity golden is the check.

## Component 4 — deletions (the payoff)

- `ownDoc_` + the `treeDoc_`-fallback in every `Clip` accessor.
- **`adoptClip`** entirely: with ctor-binding, its `isDomBound()` guard is always true on entry (a no-op), so it and its four call sites (song.cpp ~370, ~1986, ~1989, ~5264) are removed. (Its dead `!clip->id()` → `mintSessionId` line goes with it; `sessionId` retirement itself is Phase 4.)
- the migrate-on-`setId` rekey path, `Clip::setId`, and the `readFromFile` `"id"` handler — id is set only at construction; no other `Clip::setId` callers exist.

## Testing — the gate

Behaviour-preserving, so the existing golden nets are the whole gate (no new fixtures needed):

- `song_roundtrip` `OK=15 REGRESS=0` — **bit-identical** (the hard gate; catches the shell-before-contents ordering nuance and any clone/undo binding regression).
- `edit_undo_redo` `OK=40`, `param_record` `OK=3` (undo re-insert exercises the idempotent bind/`removeDocClip` lifetime).
- `json_roundtrip` fixtures 14/14 + BYO backup 37/37 (the tree load is the path being changed).
- `./dbt test` 15/15 (CppSpec + CppUTest), firmware links (`./dbt build Debug`).
- Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.

## Risks

1. **Clone/undo binding lifetime** — a transient or undone clip must bind its own entry and `removeDocClip` on destruction; `copyBasicsFrom` must write copied values through the setters. `edit_undo_redo` + the clone golden paths are the net.
2. **Shell-before-contents ordering** — see the nuance above; `song_roundtrip` bit-identity is the check.
3. **`currentValue()` positioning** — the accessor must return the clip-content Map (not the array-box) at step 2; an off-by-one is caught immediately by the golden.
4. **`getOrCreateDocClip` in the ctor** — requires `Song`/`project` far enough constructed at clip-construction time; loaded and runtime paths both already pass a live `Song&`.

## Sequencing

1. `ValueTreeDeserializer::currentValue()` (additive, no behavior change).
2. ctor-binding: `Clip(…, loadedId)` + bind, `ownDoc_` deleted, accessors collapsed, all 17 sites threaded. (The bind replaces `adoptClip`'s seed at construction; land with `adoptClip` still present-but-now-redundant, golden-gated.)
3. `readClipFromTree` shell read + move the four handlers out of `readFromFile`; wire `readClipsFromFile`.
4. delete `adoptClip` + call sites, `Clip::setId` + the `"id"` handler + migrate-on-`setId`.

Then the durable-id program continues (Phase 3 param tier → id; Phase 4 delete the `sessionId` corpse), and the `readFromTree` migration continues peeling clip content off `readFromFile` in later slices.
