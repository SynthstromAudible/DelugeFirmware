# Level 3 — splitting a document model from the runtime model

Status: **DECIDED — building the authoritative DOM** (2026-06-25). The examination (probes C + D,
below) is complete and the **authoritative DOM fork is chosen over the load-time DTO**: the document
becomes the source of truth and the runtime is projected from it, decoupling the engine from project
state. This doc began as the design exploration / probe findings that informed that decision; it now
also serves as the build's design reference. Extends the later phases of
`project_model_extraction.md`.

**Decision rationale (the DTO would have been cheaper but doesn't deliver the goal).** The whole
point is engine↔state decoupling (and the restart/fault-isolation + Rust-owned-model it unlocks),
which only the authoritative DOM provides; the DTO only decouples the file format. The examination
showed the DOM is *feasible* and its costs are *characterized*, not speculative: identity is cheap
(C), schema duplication is bounded-per-type/large-in-aggregate and dominated by the param/automation
subtree (D), and the DOM ends up *cleaner* than the serialized format (it drops the leaked
view/runtime fields, composing with the Phase-1 view-state split). Remaining unknowns are
construction effort, not feasibility — so Phases A and B below are now the committed build path, not
"either-fork on-ramps."

## The DOM/runtime boundary (definition)

**The DOM is exactly the state persisted to the song XML(s); runtime is everything transient (not
saved).** "Engine-read" is *not* the criterion — in the authoritative-DOM model the engine reads the
DOM (it's the live source of truth) and *separately* holds transient state. Two consequences:
- It supersedes the earlier "engine-read ⇒ runtime" framing **and** the Phase-D claim that the DOM
  excludes persisted view fields: if `beingEdited`/`selected`/`isPlaying` are written to the file,
  they are in the DOM. (Whether the *format* should keep persisting them is a separate, later
  cleanup.) Consistent with Phase 1: `PersistentViewState` is saved → DOM; `RuntimeViewState` → runtime.
- **Library mapping:** `deluge::project` = the **DOM** (value-typed saved state); `deluge::model` =
  the **runtime** (live engine object graph, intrusive lists, caches like `param_backup`, derived
  values), projected from the DOM. The value-tier components we already built (metadata/groove/
  transport/scale/reverb/arrangement) are mostly saved state → they *stay* in `deluge::project` and
  the DOM grows value forms of the saved data currently embedded in live objects.
- **The split cuts *through* components**, not just between them: saved fields → DOM, derived/cache/
  pointer fields → runtime (e.g. `transport` keeps saved tempo in the DOM but
  `samples_per_timer_tick_big` is runtime; `arrangement` macros hold a `Clip*` at runtime but a clip
  **id** in the DOM). The runtime-tier re-home (`clips`/`media`/`global_fx`/`param_backup` →
  `deluge::model`) is agreed but **deferred** — cosmetic relocation, not a DOM prerequisite.

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

1. **Stable identity.** Two placement options:
   - (a) *External registry* built by walking clips/outputs at each projection boundary (what the
     probe used). No engine change; O(n) map build per projection; ids are positional unless seeded.
   - (b) *Embedded id field* on every `Clip`/`Output`. Touches the engine types, but gives O(1)
     resolution, stable-across-reorder ids, and no per-projection map. **Required** for an
     *authoritative* DOM (option a is fine for a load-time DTO).

   **Id type — DECIDED: a 32-bit within-song serial id (a per-song monotonic counter), NOT a UUID.**
   Rationale (firmware-specific, investigated against the RZ/A1L TRM):
   - *No hardware entropy.* The Cortex-A9 core has no RNG (RNDR/RNDRRS is ARMv8.5-A AArch64 only),
     and the RZ/A1L (R7S721031) SoC has **no TRNG / crypto / entropy peripheral** (confirmed: the
     TRM's only "random" mentions are a CD-ROM decoder's "random logic" and audio/video "random
     pattern dither mode"). All randomness is a software MWC/LCG PRNG whose *only* seed is one timer
     read (`seedRandom()` = `jcong = deluge_clock_now()`). So UUIDv4's 122 collision-resistant bits
     would come from a weakly-seeded deterministic PRNG — fragile, and it is the *shared audio PRNG*,
     so drawing from it perturbs render determinism and the golden.
   - *Determinism.* Random UUIDs make serialization non-deterministic and non-reproducible across
     machines, which fights the golden methodology (`save(load(orig))` must be byte-identical). A
     serial counter is fully deterministic.
   - *Format cost.* The file format already references intra-song things by compact index
     (`output="3"`); a UUID is a 36-char string per reference + per object (~20× bloat, kills
     readability/debuggability). A 32-bit serial is a short int.
   - *Scope.* UUID buys *global* uniqueness (cross-document, merge, distributed editing); Deluge
     intra-song refs need only *within-song* uniqueness, and cross-resource refs (samples/presets)
     already use **paths**. The global property would go unused.
   - *Memory is a non-factor* (16 B vs 4 B × hundreds of objects ≈ a few KB in 64 MB SDRAM).

   So: a 32-bit per-song serial id (reorder-stable, deterministic, no entropy needed, compact,
   collision-free in scope). Reach for UUID only if cross-document/global identity becomes a
   committed product goal (cloud sync, collab, a shared clip/preset library) — and even then as
   local serial ids in-document + UUIDs only at the export/library boundary, never as the primary
   in-document id. ("spark uses UUID" does not transfer — that is the cloud-era successor's
   requirement set, not the native firmware DOM's; see the independence note above.)

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

## Build path (DOM committed)

The examination (C + D) is done and the DOM is chosen, so these phases are now the committed build
sequence (A and B were the "either-fork on-ramps"; they remain the right first steps because the DOM
needs both). Each is still golden-gated:

- **Phase A — serialization seam. ✅ DONE** (`deluge::project::saveSong`/`loadSong`,
  `src/deluge-project/serialization.{h,cpp}`). Whole-song save/load now funnels through one narrow
  boundary that delegates to the legacy per-class serialization; `Song::writeToFile` takes an explicit
  `Serializer&` (no more global grab); all callers (SaveSongUI, `writeTemplateSong`, LoadSongUI, the
  golden harness) routed through it. Pure relocation — golden byte-identical (OK=15), host +
  arm-none-eabi green. This is the insertion point where DOM serialization replaces the body next.
- **Phase B — the read-seam (WS5).** Route the engine's *reads* of project state through an
  interface — our own native seam (the read-seam is a general pattern; spark's `ProjectReader`
  validates it but is not the shape to copy). This is the surface a projected model would later sit
  behind. Gate: behavior preserved.
- **Phase C — stable identity probe #2. ✅ RUN (findings below).** Add an embedded **32-bit per-song
  serial id** to `Clip`/`Output` (problem 1b; id type decided above — not UUID) and measure the churn
  + any hot-path cost. Gate: no render regression; golden green.

  *Findings:* a minimal probe (id field + accessor on the two base classes, assigned in the base
  ctor from a monotonic counter) cost **4 files, +26/−2 lines, and zero call-site churn** — the whole
  polymorphic hierarchy inherits identity via the base ctor. `id()` is read **nowhere on the render
  path** (the audio loop walks `output->next` → `renderOutput`); +4 bytes/object is noise. Host build
  clean, golden green (OK=15). Conclusion: embedded identity is **cheap and non-invasive**. Caveat
  (scoped out): these ids are session-unique but **not save-stable** — clips/outputs are reconstructed
  on load, so persistence (serialize + restore the id) is the next layer, overlapping Phases A/D.
- **Phase D — heavy-type schema probe. ✅ RUN (findings below).** Design *one* polymorphic doc node
  natively (`AudioClip`, the simpler Clip variant) from the live class's serialized fields, and
  round-trip it through projection. This measures problem (2) — the real cost driver — against a real
  type.

  *Findings (modelled from `audio_clip.cpp::writeDataToFile` + `Clip::writeDataToFile`, probe runs):*
  - **Per-type breadth is bounded.** Even with the base-Clip fields, a `DocAudioClip` is ~17
    scalar/enum fields + 1 sample ref + 1 output ref + a paramManager. Manageable per type — but
    ×~44 self-serializing classes is the bulk of the program.
  - **The serialized schema IS the document model** — corrected (see "The DOM/runtime boundary"
    above): the DOM mirrors what's in the file, so the persisted fields (incl. `beingEdited`/
    `selected`/`isPlaying`/`lastSelectedParam{…}`) **are** in the DOM. The earlier "a clean DOM
    excludes them" was wrong. They are *arguably* view state that the format shouldn't persist — but
    that's a separate, optional **format cleanup**, not a DOM-definition concern.
  - **References validated against a real type:** a sample is referenced by **path** (cross-resource),
    an output by **stable serial id** (intra-song) — exactly the Phase-C decision.
  - **The paramManager / automation subtree is the dominant cost** — `ParamSet → AutoParam[]` (each
    with automation nodes) + patch cables + MIDI params, deep and **shared** across AudioClip,
    InstrumentClip, Sound, Kit. The per-clip scalar schema is small by comparison; this subtree is
    where the duplication work — and the payoff (it is the musical automation data) — concentrates.

After A–D the authoritative-vs-DTO decision is made on **evidence** (identity churn from C,
duplication cost from D), not speculation. Only then commit to the authoritative-DOM program.

**Examination bottom line (C + D run):** the DOM is feasible and its costs are now characterized.
Identity is cheap (C). Schema duplication is bounded per-type but large in aggregate (~44 classes),
dominated by the param/automation subtree (D) — and the DOM ends up *cleaner* than the serialized
format (it drops the leaked view/runtime fields). The remaining unknowns are construction effort
(Phases A/B), not feasibility. **Decision: build the authoritative DOM** (see Status above).

## Decision criteria (when is the authoritative DOM worth it?)

Pursue the authoritative DOM if **engine/state decoupling becomes a hard requirement** — i.e. when
restartability/fault-isolation (`app-supervisor-recovery`) or a Rust-owned model are committed
goals. If the near-term need is only *serialization* (serde/Rust file format), the **load-time DTO**
fork delivers that far more cheaply and is the right first target. The probes above are valuable
either way.
