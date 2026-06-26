# Param / automation DOM

Status: **DESIGN (2026-06-26).** The heavy-tier successor to `project_model_level3.md`. The value tier
(metadata / groove / transport / scale / reverb / arrangement) is now DOM-resident with its
serialization in `deluge::project`; reverb is authority-flipped. This doc designs the **param /
automation subtree** — the Level-3 doc's identified dominant cost *and* payoff (it is the musical
automation data) — and lays out a bottom-up, golden-gated build. No code lands with this doc; it
commits the schema, the projection-vs-DTO decision, the scatter-collapse strategy, and the phase order
before the large build, the way `synth_phase*`, `resource_manager.md`, and `project_model_level3.md`
were handled.

## Why this tier is different

The value tier was value-typed PODs already living in `deluge::project::Song`; the migration just
relocated their (de)serialization. This tier is the **live modulation engine**, with no value-typed
mirror and a serialization surface scattered across many engine classes:

```
ParamManager
 ├─ summaries[] + collection offsets        (live: which collections exist, where)
 ├─ PatchedParamSet     → AutoParam[]        (patched params, e.g. synth params driven by the matrix)
 ├─ UnpatchedParamSet   → AutoParam[]        (unpatched params, e.g. global FX, volume/pan)
 ├─ ExpressionParamSet  → AutoParam[]        (MPE / expression)
 └─ PatchCableSet       → PatchCable[]        (each cable carries an AutoParam-shaped value/automation)
```

- **Leaf (`AutoParam`) is already nearly value-shaped.** `ParamNode`
  (`modulation/params/param_node.h`) is a clean POD: `{int32 value, pos (Positionable), bool
  interpolated}`. `AutoParam` (`modulation/automation/auto_param.h`) = `currentValue` + a
  `ParamNodeVector nodes`. `AutoParam::writeToFile` (`auto_param.cpp:1856`) emits a compact hex blob:
  `0x` + `currentValue`(8 hex) + per-node `value`(8 hex) + `pos|interpolated<<31`(8 hex).
- **Structure is live engine.** `ParamManager` / `ParamSet` / `PatchCableSet` hold the running
  modulation state (interpolation cursors, summaries, pool-allocated node storage) — not serializable
  as data.
- **Serialization scatter.** The param-name → param-id → AutoParam-blob mapping is hand-written per
  owning class — `ParamSet::writeParamAsAttribute`/`readParam` (`param_set.cpp:184/200`),
  `GlobalEffectable`, `ModControllableAudio`, `Sound`, `AudioClip`, `InstrumentClip` (6 classes, ~23
  methods). Crucially the file format is **structurally nested**, not a flat param map: e.g.
  `GlobalEffectable::readParamTagFromFile` (`global_effectable.cpp:897`) dispatches `delay` → `rate`/
  `feedback`, `lpf` → `frequency`/`resonance`/`morph`, plus flat params (`reverbAmount`, `lpfMorph`) and
  the patch-cable block.

## Target value schema (the DOM)

Value-typed, `repr(C)`/serde-ready, SDRAM-resident; **reuse `ParamNode` verbatim** (already a POD — do
not re-define it).

```cpp
// The automation leaf — value form of AutoParam.
struct DocAutoParam {
    int32_t current_value;        // AutoParam::currentValue
    Vec<ParamNode> nodes;         // AutoParam::nodes (empty => unautomated, use current_value)
};

// One parameter collection's values, keyed by param id within the collection.
struct DocParamSet {
    ParamKind kind;               // patched / unpatched / expression
    Vec<DocAutoParam> params;     // indexed by ParamType within the collection (sparse-friendly)
};

// A whole ParamManager.
struct DocParamManager {
    DocParamSet patched;          // optional per object kind
    DocParamSet unpatched;
    DocParamSet expression;       // MPE
    Vec<DocPatchCable> patch_cables;  // {source, destination param id, DocAutoParam amount}
};
```

Variable-length automation ⇒ `Vec` (SDRAM); a fixed-cap form can come later if the RT path ever reads
the DOM directly (it will not initially — projection builds the live `ParamManager`).

## Strategic decision: DTO on-ramp, then authoritative — RECORD

The global project-model decision is the **authoritative DOM** (engine projected from the document;
"DOM not DTO"). For this tier the cost is highest and the engine coupling deepest, so build it in two
moves and keep authoritative as the **end state**, not the first step:

1. **DTO on-ramp first.** Build the value schema and wire it at the **serialization boundary only** —
   on save, project the live `ParamManager` → `DocParamManager` and serialize the value DOM; on load,
   parse into `DocParamManager` and project → live `ParamManager`. The engine stays authoritative
   between. This **exercises the schema against the golden corpus under real data** (high confidence),
   gets the whole song serde-swappable, and is far less invasive than rewiring every edit path.
2. **Authoritative flip second.** Once the schema is proven, make `DocParamManager` the source of truth
   and the live `ParamManager` a projected cache (edits route through the DOM), completing the
   decoupling.

Rationale: the value tier and reverb went straight to authoritative because they were small / single
objects; the param tier is large and the schema unproven, so the DTO intermediate de-risks the schema
before the invasive authoritative projection. This is exactly the on-ramp `project_model_level3.md`
describes — adopted here as a *sequencing* choice, not a change of end goal.

## Collapsing the serialization scatter

A name↔id table **already exists** and is the foundation: `paramNameForFile(Kind, ParamType)` and
`fileStringToParam(Kind, name, allowPatched)` (`param.cpp:701/718`, both `constexpr`). But the scatter
is **not** just a flat map — the file format nests grouped effects (`delay.rate`, `lpf.frequency/
resonance/morph`, `hpf.*`) and has special blocks (patch cables, MPE). So the collapse is:

- A **param-descriptor table** mapping `(kind, param-id) ↔ file name ↔ collection ↔ structural group`
  (built on top of the existing `paramNameForFile`/`fileStringToParam`), driving a **generic**
  (de)serializer over `DocParamManager` that reproduces the grouped/nested layout.
- This replaces the ~23 hand-written per-class methods with one table + one generic walk. It is the
  largest single payoff of the tier and the main work of Phase 4. **Byte-stability:** the generic
  walk must reproduce the exact existing tag names, grouping, and order, or it is a deliberate golden
  re-baseline (inspect with the sorted-diff = pure-relocation check).

## Projection (DOM ↔ engine)

- `toDocument(const ParamManager&) -> DocParamManager`: copy each collection's AutoParams
  (`currentValue` + nodes) into `DocAutoParam`; copy patch cables.
- `fromDocument(const DocParamManager&, ParamManager& out)`: allocate/clear collections, set each
  AutoParam's `currentValue` + node vector, rebuild patch cables, then the existing post-load fixups
  (summaries, `setupPatching`) run as today.
- Patch cables and expression params are first-class in the schema (not afterthoughts) because they are
  AutoParam-bearing and part of the saved state.

## Phasing (bottom-up; each later phase golden-gated + committable)

- **Phase 0 — this doc.**
- **Phase 1 — `DocAutoParam` + projection + dual-arch spec.** The recursive leaf: value node, `toDoc`/
  `fromDoc` against a live `AutoParam`, and a CppSpec unit spec (x86 SIMDe + ARM/qemu) covering the
  unautomated case, the automated case (nodes incl. `interpolated`), and the patch-cable value range.
- **Phase 2 — `DocParamSet`** for one collection + projection.
- **Phase 3 — `DocParamManager`** (all collections + patch cables + expression) + projection.
- **Phase 4 — descriptor table + generic (de)serialization** of `DocParamManager`, replacing the
  6-class scatter; reproduce the nested layout (golden byte-stable, or justified re-baseline).
- **Phase 5a — DTO wiring:** route the param (de)serialization through `DocParamManager` at the
  save/load boundary (engine projected). Golden is the gate.
- **Phase 5b — authoritative flip:** `DocParamManager` becomes the source of truth; live `ParamManager`
  a projected cache; edits route through the DOM.

## Verification methodology

- **Golden song round-trip** (`tests/golden/song_roundtrip` + `songs_run.py`): all fixtures settle;
  any regroup re-baseline must pass the **sorted-diff = pure relocation** check (no value change).
- **Per-value-type dual-arch CppSpec unit spec** (the dsp-next methodology: `tests/dsp` style, x86
  SIMDe + ARM/qemu) — mandatory per primitive, lands with each phase.

## Risks / open questions (resolve as phases hit them)

- **Patch-cable value scaling.** `ParamNode::value` is stored in `[-2^30, 2^30-1]` for patch cables vs
  full range otherwise (`param_node.h`); `lshiftAndSaturate<1>()` / right-shift-by-1 convert between
  them. The DOM must preserve the *stored* representation faithfully (projection, not reinterpretation).
- **Late-2016 file back-compat** in `AutoParam::readFromFile` (decimal vs `0x` paths, overwriting cloned
  ParamManagers) — the DTO read path must keep both.
- **MPE / expression params** and **per-clip vs per-sound `ParamManager` sharing** (Kit drums, the
  song-global paramManager) — confirm ownership before Phase 3.
- **No RT-path DOM reads initially** — projection builds the live `ParamManager`; the audio thread never
  walks `DocParamManager`, so SDRAM `Vec` is fine.
