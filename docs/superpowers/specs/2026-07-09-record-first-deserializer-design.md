# Record-First Deserializer — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-09
**Unblocks:** the durable-id program (`docs/superpowers/specs/2026-07-08-durable-id-unification-design.md`) — specifically killing `ownDoc_` — and, longer term, the [project-model north star](../../dev/project_model_north_star.md) (the Model owning persistent state, deserialized as a value type).

## Goal

Decouple **parsing** from **construction** at load. Today the reader is a streaming (SAX-style) tokenizer and objects are built *as* tags stream by (`readFromFile` mutates the object mid-parse). That parse-as-you-construct model cannot establish a record before constructing, which is exactly what blocks identity-before-values (a loaded clip's `id` arrives mid-parse, forcing the `ownDoc_` transient buffer).

Replace it with a **record-first** load: parse the whole file into an in-memory tree, then construct from the tree — so a clip's `id` (and anything else) is available by random-access *before* construction. `ownDoc_` dies; the streaming megaswitch survives only as a compatibility replay path until (if ever) it's rewritten.

## Hard constraint

**Read both XML and JSON now; write JSON only (eventually).** XML reading is transitional — kept until a future JSON-only cutover — so the design must (a) work for both formats through one construction path and (b) quarantine XML behind a boundary that deletes cleanly later.

The codebase already reads both: `Deserializer` is an abstract **streaming** interface (`readNextTagOrAttributeName`, `readTagOrAttributeValue*`, `match`, `exitTag`, `prepareToReadTagOrAttributeValueOneCharAtATime`/`readNextCharOfTagOrAttributeValue`, `reset`, `tryReadingFirmwareTagFromFile`), implemented by `XMLDeserializer` and `JsonDeserializer`; `activeDeserializer` is chosen at file open (`StorageManager::openXMLFile`/`openJsonFile`). Consumers never know which format backs them. The problem is purely that the interface is *streaming*.

## Architecture

Three components, above the existing format parsers:

1. **`Value`** — the in-memory tree. `orderedMap<string, Value>` | `array<Value>` | scalar. Format-agnostic. Two deliberate choices:
   - **Scalars are raw strings.** The existing readers return value strings and callers parse them (`readTagOrAttributeValueInt`/`…Hex`/`…HexBytes`). Keeping a `Value` scalar as the raw string means the adapter parses on demand and the consumer contract is unchanged; no type decisions baked into the tree.
   - **The map preserves insertion order** (a vector of key→value pairs, not `std::map`). `readFromFile` consumes tags in file order and some logic is order-sensitive, so the tree must replay them in original order. Random-access-by-key (the `id` peek) is a linear scan over the pairs — load-time only, fine.

2. **`ValueTreeDeserializer : Deserializer`** — the keystone. A cursor over a `Value` node that re-implements the *existing* streaming interface by walking the tree: `readNextTagOrAttributeName` advances through the ordered map/array in order; `match('{')`/`match('[')` descends into the current child when it's a map/array (else returns `false`, exactly like the file reader); `readTagOrAttributeValue*` returns/parses the current scalar; `exitTag` pops up; the one-char-at-a-time value path serves characters from the stored scalar string. Because it satisfies the same interface, **every existing `readFromFile` works unchanged, tree-backed instead of stream-backed.** This is what makes the migration incremental rather than a big-bang rewrite.

3. **Format → `Value` parsers (hybrid).**
   - **XML: generic builder over the streaming interface.** `buildValue(XMLDeserializer&)` drives the existing tokenizer schema-free — read a name, then `match('{')` → object / `match('[')` → array / else scalar (`readTagOrAttributeValue`), recursively. Reuses the tested XML tokenizer; the whole XML path (this builder + `XMLDeserializer`) deletes cleanly at the JSON-only cutover.
   - **JSON: dedicated `parseJson(bytes) → Value`.** A proper recursive-descent parser, permanent, independent of the legacy streaming interface — a real step toward the serde end-state.
   Both emit the same `Value`, so construction is format-agnostic.

### Load data flow

```
file --(XMLDeserializer+buildValue, or parseJson)--> Value doc   // whole tree, in RAM
loader, per clip node:
    id   = node["id"]                                   // random-access peek
    clip = construct(song, /*loadedId=*/id)             // id-first; binds clip_structure[id]
    clip->readFromFile(ValueTreeDeserializer{node})     // legacy code, tree-backed
```

Whole-document DOM (chosen over per-element): one code path, no stream/DOM duality, random access over the entire document. Costs a transient whole-file tree in RAM (see Risks).

## The durable-id payoff (why this unblocks `ownDoc_`)

With the whole document as a random-access tree, the loader looks up a clip's `id` in its node **before** constructing. So:

- Re-add the `loadedId` ctor param: `Clip(ClipType, Song&, std::optional<uint32_t> loadedId = std::nullopt)` → `id_ = loadedId.value_or(song.generateNewClipId())`.
- The ctor binds `clip_structure[id_]` under the **real** id (no throwaway, no migration, no id-space collision).
- `ownDoc_`, the `treeDoc_`-nullable fallback in the accessors, and `adoptClip` (+ its runtime insert-site hooks and the load-binding pass) are **deleted**. Accessors become `treeDoc_->field`; `treeDoc_` is never null.
- `readFromFile` (over the adapter) re-reads the `id` tag → `setId(sameId)`, a no-op; structural tags write `treeDoc_` (the correct entry).

## Migration steps

Each step is golden-gated (`songs`/`param_record`/`edit_undo_redo` bit-identical, firmware links) before the next.

- **Step 1 — Infrastructure.** `Value` + ordered map, `parseJson`, `buildValue(XMLDeserializer&)`, `ValueTreeDeserializer`. Unit-tested in isolation.
- **Step 2 — Faithful-replay swap (highest risk).** Route the whole song load through the tree: parse file → `Value` → wrap in `ValueTreeDeserializer` → run the existing load (every `readFromFile`) over it. Behaviour-preserving; the adapter must reproduce the stream for every reader call the entire load makes. Golden load→save bit-identity is the differential test (load-via-tree ≡ load-via-stream).
- **Step 3 — id-first clip construction.** Loader peeks `clipNode["id"]`, constructs with it, ctor binds `clip_structure`, delete `ownDoc_`/`treeDoc_`-fallback/`adoptClip`. The durable-id payoff.

## Scope

**In:** dual-format whole-document `Value`-tree loading; legacy `readFromFile` over the adapter (Step 2); clip id-first construction + `ownDoc_`-kill (Step 3).

**Out (works indefinitely via the adapter; migrate later if ever):** rewriting any `readFromFile` → a native `readFromTree`; outputs/drums id-first (they have no `ownDoc_`; their durable-id work is separate); preset/instrument *file* loads (song load first — same infra reused later); JSON *writing* (`JSONSerializer` already exists; separate concern).

## Testing

- **Unit:** `Value` construction; `parseJson` on hand-written JSON; `ValueTreeDeserializer` replay (build a tree, walk it via the adapter, assert the emitted name/value/match/exitTag sequence matches the tree order); `buildValue` over a small XML snippet.
- **Golden (Steps 2–3):** `songs` `OK=15 REGRESS=0`, `param_record` `OK=3`, `edit_undo_redo` `OK=40` — load-via-tree must equal load-via-stream, bit-for-bit.
- **JSON read path:** the corpus is XML, so JSON reading has no golden coverage. Generate JSON fixtures by loading a corpus song and saving JSON (`JSONSerializer`), then assert **JSON-load ≡ XML-load** (same resulting song / same re-saved bytes). Add a small JSON fixture set for the round-trip.

## Risks

1. **Adapter fidelity (the big one).** The entire existing load depends on `ValueTreeDeserializer` reproducing every streaming behaviour: `match` semantics, `exitTag`, the one-char-at-a-time value reader for large blobs (sample/note data), hex bytes, `tryReadingFirmwareTagFromFile`, EOF/end-of-object signalling. Mitigation: end-to-end golden bit-identity (a strong differential check) + focused unit tests on the adapter.
2. **Peak load RAM.** Whole-doc tree *plus* the Model being built, simultaneously — the assumption the whole approach rests on. A song file is up to a few MB; the tree with node overhead is a few × that, transient, freed after load; samples stream from card at load time so SDRAM headroom should exist. **Verify on-device** before relying on it.
3. **Generic XML builder edge cases.** Schema-free tree-building over the streaming interface may hit format quirks (empty tags, the firmware/version header, attribute-vs-nested-tag ambiguity). Caught by golden bit-identity.
4. **Ordered-map key scan** is linear — load-time only, acceptable.

## Non-goals / notes

- Not a serialization rewrite — write stays as-is (`JSONSerializer`/`XMLSerializer`).
- The generic-XML path is intentionally throwaway: at the JSON-only cutover, delete `buildValue(XMLDeserializer&)` + `XMLDeserializer`; `parseJson` + `Value` + `ValueTreeDeserializer` stay.
- Longer term, hot `readFromFile`s can be rewritten to read `Value` directly (`readFromTree`), retiring the adapter for those types; not required here.
