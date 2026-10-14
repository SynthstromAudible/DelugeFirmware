# Record-First Deserializer — Step 2a: XML → `Value` — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-09
**Parent:** `docs/superpowers/specs/2026-07-09-record-first-deserializer-design.md` (Step 1 complete). This is the first half of Step 2 (the faithful-replay swap); Step 2b wires it into the load and golden-gates.

## Goal

Produce a `Value` tree from a Deluge **XML** song file, and make `ValueTreeDeserializer` replay that tree to legacy `readFromFile` code so it behaves **bit-for-bit identically to reading the XML directly**. This is the piece Step 2b needs to route the whole (XML-corpus) load through the tree.

## Why the Step-1 `buildValue`-over-`XMLDeserializer` path is dead

The Step-1 whole-branch review found `XMLDeserializer::match` (`Deserializer.cpp:788-790`) returns `true` **unconditionally** (its own comment: "Only used by Json"), so the generic `buildValue`'s shape-dispatch (`match('{')`→object / `match('[')`→array / else scalar) cannot discover XML structure. And XML has **no object/array marker at all**: `XMLSerializer::writeArrayStart(tag)` (`Serializer.cpp:198`) is literally `writeOpeningTag(tag)` — an array container (`<sessionClips>`) is byte-identical to an object. The array-ness lives only in how `readFromFile` consumes it (permissive `match` + `readNextTagOrAttributeName` returning child names until the container closes). So: a dedicated `parseXml` (Kate's choice, option 2), and an adapter that replays in the source reader's mode.

## Component 1 — `parseXml(std::string_view) → std::optional<Value>`

A standalone recursive-descent XML parser (parallel to Step 1's `parseJson`), producing a **format-neutral, all-ordered-maps** tree — **no `Array`, no heuristics**:

- An **element** → a `Value::Map` (insertion-ordered).
- An **attribute** (`clipName=""`) → a `Scalar` entry (name → raw value string).
- A **text-only element** (`<cc>0</cc>`) → a `Scalar` entry (name → text).
- A **structured child element** (`<midiParams>…</midiParams>`) → a nested `Map` entry.
- **Repeated child names** (the mixed `<instrumentClip>`/`<audioClip>` under `<sessionClips>`) → repeated ordered entries. There is no `Array` — XML gives nothing to distinguish it, and `readFromFile` iterates XML "arrays" by reading child names until the container closes, not by an array type.

Scalars store the **raw value text** (matching `parseJson` and the load-path contract: callers parse via `readTagOrAttributeValue*`). Must handle the full Deluge XML surface: attribute values, text content, `0x…` hex, self-closing `<foo/>`, the megabyte `preview` blob, whitespace/newlines, and XML escaping. Returns `std::nullopt` on malformed input. Self-contained — does **not** drive `XMLDeserializer`.

## Component 2 — `ValueTreeDeserializer` gets a source-format mode

Constructed with a mode enum (default JSON, preserving Step-1 behavior). One class; JSON mode is unchanged and additive.

- **JSON mode** (existing): `match('{')`/`match('[')` descend by type; `match('}')`/`match(']')` pop. Strict.
- **XML mode** (new): mirror `XMLDeserializer`'s actual contract over the all-maps tree, using a frame stack `{const Value* map; std::size_t entryIndex}`:
  - `readNextTagOrAttributeName()` — return the current frame's next entry key, stage its value as *pending*; return `""` when the frame's entries are exhausted (element-content end) — **without** popping.
  - **Implicit descent** — if the caller calls `readNextTagOrAttributeName()` *again* while *pending* is a `Map`, descend into it and return its first key (XML "enters" a child element by continuing to read, with no `match('{')`). If the caller reads a value instead, serve *pending*'s scalar.
  - `match('{')`/`match('[')`/`match('}')`/`match(']')` — all permissive `true` (no-op), exactly `XMLDeserializer::match`.
  - `exitTag(name, closeObject)` — pop frames back to the caller's depth, mirroring `XMLDeserializer::exitTag`'s depth reconciliation.
  - value reads (`readTagOrAttributeValue*`, hex-bytes, char-at-a-time) — serve from *pending*'s scalar, as today.

The exact depth/`exitTag`/implicit-descent accounting is subtle and is **not hand-proven** — it is developed against Component 3.

## Component 3 — Differential fidelity harness (the correctness driver)

For a set of real corpus XML fragments — an attribute-only element; a nested element; a container of repeated child elements; a deeply nested one; one with a hex/`0x` value and one with a text-content element — a **hand-written call script** (the exact sequence `readFromFile` would issue for that fragment's shape: `readNextTagOrAttributeName` / `readTagOrAttributeValue*` / `match` / `exitTag`) is run against **both**:

1. the live `XMLDeserializer{fragmentBytes}`, and
2. `ValueTreeDeserializer{*parseXml(fragmentBytes), Mode::XML}`,

asserting the two produce **identical return sequences token-for-token**. XML mode is implemented by making these scripts pass. This pins the adapter against ground truth in isolation; it is necessary-but-not-sufficient (hand-scripted shapes), with Step 2b's whole-song golden as the final backstop.

## Testing

- **`parseXml` specs (CppSpec, `tests/spec/`):** XML string → expected `Value`, covering attributes, text-content elements, nested elements, repeated children, self-closing tags, `0x` hex values, escaping, and a `preview`-sized blob; `std::nullopt` on malformed input.
- **Differential harness (CppSpec):** the Component-3 scripts, `XMLDeserializer` ≡ XML-mode adapter, token-for-token, over real fragments.
- **JSON mode** stays green (Step-1 specs unchanged).
- Run via `./dbt test` (CppSpec `all_specs` + CppUTest `UnitTests`, both green as of Step 1).

## Scope

**In (2a):** `parseXml`; `ValueTreeDeserializer` XML mode + the mode enum; the differential harness.

**Out (2b or later):** the swap itself (format detection → parse whole file → wrap → run load → golden-gate); `firmwareVersion` wiring (the adapter's `tryReadingFirmwareTagFromFile` is a stub — the whole-song swap must parse the version tags or version-gated branches misfire); the JSON-fixture round-trip that finally exercises JSON mode end-to-end; preset/instrument *file* loads.

## Risks

1. **XML-mode fidelity** — the implicit-descent + `exitTag` depth accounting is the crux; mitigated by the harness (2a) then golden (2b).
2. **`parseXml` on the full Deluge XML surface** — hex attrs, megabyte `preview` blob, self-closing tags, escaping, whitespace; covered by fragment specs + the 2b roundtrip.
3. **`firmwareVersion`** — stubbed today; a 2b task, flagged here so it isn't forgotten.
4. **Peak load RAM** (whole tree + Model) — the standing on-device check from the parent spec.
5. **Format detection** in the swap — a 2b concern.

## Sequencing

2a (this spec) → 2b (swap + golden) → Step 3 (id-first clip construction; `ownDoc_`-kill). 2a's deliverables are additive and consume nothing yet (like Step 1), so they can land and be reviewed independently of the swap.
