# Level 3 — splitting a document model from the runtime model

Status: **examination** — 2026-06-25. This is a design exploration + probe findings to decide
*whether and how* to pursue an authoritative document model, **not** a commitment to build it. It
extends the later phases of `project_model_extraction.md`.

## The goal

Today the Deluge has **one representation that is simultaneously the document (what serializes) and
the runtime (what the engine renders).** `deluge::project::Song`'s components split into two tiers
(see the class comment in `src/deluge-project/song.h`):

- **Document / value tier** — `metadata`, `groove`, `transport`, `scale`, `reverb`, the
  `arrangement` sections. Already value-typed and serializable.
- **Runtime-reference tier** — `media` (the intrusive `Output` list the audio thread walks every
  block: `audio_engine.cpp:259/1297`), `clips` (`ClipArray` of non-owning `Clip*`), `global_fx`
  (`GlobalEffectableForSong`), `param_backup` (live `ParamManager`s). These *are* the running
  engine; they don't serialize as data.

**Level 3** introduces a second, authoritative, value-typed **document model**, from which the
runtime is projected/derived. The win: it **decouples the engine from the project state** — which
unlocks serde/Rust-owned serialization, checkpoint/restart and fault isolation
(`app-supervisor-recovery`), and a clean C-ABI model (`target_architecture.md`). It is the literal
north star of the whole project-model effort.

## The probe (the embedded-runtime-pointer case)

The sharpest first question is reference resolution: a document model can't hold engine pointers, so
every `Clip*`/`Output*` must become a stable id. `SessionMacro` (`{kind, Clip*, Output*, section}`)
is the minimal value type whose document/runtime boundary cuts *through* it, so it isolates exactly
that mechanism. A self-contained projection round-trip (`toDocument`/`fromDocument` + a bidirectional
id↔pointer registry) was built and run:

```
PROBE OK: SessionMacro runtime<->document round-trip fidelity preserved.
  DocSessionMacro = 16 bytes; standard_layout=1 trivially_copyable=1 (serde/repr(C)-ready)
  RuntimeSessionMacro = 32 bytes (holds raw engine pointers; NOT serializable as data)
```

**Findings:**

1. **Identity is the load-bearing problem, not the projection code.** The runtime has *no stable
   identity* — the pointer *is* the identity. A document model must introduce stable ids for clips
   and outputs. That is a real change, independent of how nice the doc types are.
2. **The projection primitive is cheap and mechanical** for a leaf type: two free functions + a
   id↔pointer registry. Fidelity round-trips for both the reference case and the empty case.
3. **Document nodes are genuine PODs** (16-byte, `trivially_copyable`, `standard_layout`) —
   serde-/`repr(C)`-/checkpoint-ready. The runtime form is not. This confirms the value the split
   buys.

What the probe deliberately does **not** measure: the schema-duplication cost of the heavy
polymorphic types (below).

## The three hard problems Level 3 must solve

1. **Stable identity.** Two options:
   - (a) *External registry* built by walking clips/outputs at each projection boundary (what the
     probe used). No engine change; O(n) map build per projection; ids are positional unless seeded.
   - (b) *Embedded id field* on every `Clip`/`Output`. Touches the engine types, but gives O(1)
     resolution, stable-across-reorder ids, and no per-projection map. **Likely required** for an
     *authoritative* DOM (option a is fine for a load-time DTO).

2. **Polymorphic schema duplication.** A document model must mirror the serialized schema as value
   types. The surface: **~44 self-serializing model classes / ~74 serialization methods**, and the
   polymorphic hierarchies `Clip → {InstrumentClip, AudioClip}` and
   `Output → {AudioOutput, Instrument → {Kit, MIDIInstrument, CVInstrument, SoundInstrument}}`.
   `SessionMacro` was 4 fields; an `InstrumentClip` document node is dozens (notes, automation,
   params). This is the dominant cost. **The Deluge DOM is its own native C++ design** — shaped for
   the firmware's constraints (real-time projection to the existing engine, SDRAM, the C-ABI/serde
   boundary), NOT a port of `spark-project` (which is `no_std`+alloc, repr-Rust not `repr(C)`,
   clean-slate for a different engine). `spark-project`/`deluge-xml` are **differential oracles**
   only: cross-checks for round-trip fidelity and a reference for *what data exists* in the format —
   they do not author our schema. The data content is well-known (the existing self-serializing
   classes), so the cost is bounded native design, not a blank page.

3. **Sync model (the strategic fork).**
   - *Load-time DTO* — build the document only at save, parse it at load; runtime stays
     authoritative in between. Cheapest; gets serde/Rust serialization with **no engine change**.
     (`deluge-xml` is the Rust analogue of this role — a format DTO — but ours is its own native
     design, with `deluge-xml` only as a round-trip oracle.) Does **not** decouple the engine from
     project state at runtime.
   - *Authoritative DOM* — the document is the source of truth; the runtime is a projected cache;
     edits flow through the document. This is what actually decouples engine from state and unlocks
     restart/fault-isolation — and it is the large, engine-touching program.

## Recommended path (incremental, go/no-go gated)

The probe de-risked problem (1) for the trivial case and confirmed the payoff. Before committing to
the authoritative DOM, take the cheap on-ramps that are valuable under *either* fork and that each
produce a go/no-go signal:

- **Phase A — serialization seam (Phase 5 of the extraction).** Pull save/load out of the model
  classes into a narrow interface. This is the prerequisite for *any* document model and is useful
  on its own. Gate: golden round-trip still byte-identical.
- **Phase B — the read-seam (WS5).** Route the engine's *reads* of project state through an
  interface — our own native seam (the read-seam is a general pattern; spark's `ProjectReader`
  validates it but is not the shape to copy). This is the surface a projected model would later sit
  behind. Gate: behavior preserved.
- **Phase C — stable identity probe #2.** Add an embedded stable id to `Clip`/`Output` (problem 1b)
  and measure the churn + any hot-path cost. Gate: no render regression; golden green.
- **Phase D — heavy-type schema probe.** Design *one* polymorphic doc node natively (`AudioClip`,
  the simpler Clip variant) from the live class's serialized fields, and round-trip it through
  projection (cross-checking shape against `deluge-xml` as an oracle, not copying it). This measures
  problem (2) — the real cost driver — against a real type. Gate: round-trip fidelity vs the live
  object.

After A–D the authoritative-vs-DTO decision is made on **evidence** (identity churn from C,
duplication cost from D), not speculation. Only then commit to the authoritative-DOM program.

## Decision criteria (when is the authoritative DOM worth it?)

Pursue the authoritative DOM if **engine/state decoupling becomes a hard requirement** — i.e. when
restartability/fault-isolation (`app-supervisor-recovery`) or a Rust-owned model are committed
goals. If the near-term need is only *serialization* (serde/Rust file format), the **load-time DTO**
fork delivers that far more cheaply and is the right first target. The probes above are valuable
either way.
