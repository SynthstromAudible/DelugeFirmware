# Project model — the north star (DOM / save-load / API end-state)

Status: **north-star / end-state (living doc, 2026-07-07).** This is the destination the
in-flight DOM / project-model work is heading toward: what the model *is*, how the engine
relates to it, and — the part that had gone blurry mid-grind — what the read/write **API**
looks like when it is done. It fills gap #2 ("Project-model end-state") called out in
`target_architecture_north_star.md`. It does **not** re-specify the individual slices; it
gives them a target to be checked against.

Scope note: this doc is about **clean, idiomatic C++23** as the end state. A Rust + serde
serialization backend is a *long-term stretch* and explicitly **out of scope** here — the
model is designed value-typed and serialization-swappable so that door stays open, but we
are not designing for it now.

---

## The thesis (one sentence)

> **The project model is the authoritative source of everything saved to disk; the engine
> is a *player of* that model** — it constructs runtime **snapshots** from the model and
> executes them, and it owns nothing persistent.

Everything below is the consequence of taking that sentence literally.

---

## The three layers

```
   ┌──────────────────────────────────────────────────────────────┐
   │  PROJECT MODEL   (deluge::project)                            │
   │  authoritative · owns ALL persistent structure as data       │
   │  clips · instruments · sounds · kits · drums · arrangement   │
   │  params · automation · scale · groove · transport · reverb   │
   │  ── this IS the song. save/load operates directly on it. ──  │
   └───────────────┬──────────────────────────────▲───────────────┘
      snapshot (build/patch)          reconcile { address, value }
   ┌───────────────▼──────────────────────────────┴───────────────┐
   │  SNAPSHOT   (engine-built, derived, never authoritative)      │
   │  runtime-optimized read-view of a model subtree              │
   └───────────────┬──────────────────────────────────────────────┘
                   │ consumed by
   ┌───────────────▼──────────────────────────────────────────────┐
   │  EXECUTOR   (the player: render · sequencing · voices)        │
   │  owns ONLY derived + ephemeral state:                        │
   │  interp cursors · LFO phase · playback position · DSP scratch │
   └──────────────────────────────────────────────────────────────┘
```

### 1. Project Model — authoritative, owns all persistent structure

`deluge::project::Song` is the whole song **as data**: value-typed, no live engine
pointers, no polymorphic pool objects reachable from it. Everything that goes to disk lives
here and *only* here. The current `Song` header still holds a mixed "runtime-reference
tier" (live `Clip*`, `GlobalEffects`, intrusive links) — that mixture is exactly what this
end-state dissolves. In the destination:

- **Structure is data.** A clip is a *clip description* (value-typed), not a live `Clip*`.
  An instrument/sound/kit/drum is its persistent parameterization, not the running DSP
  object. Arrangement, scene grid, and routing are data.
- **No engine dependency.** The model compiles and is meaningful without the audio engine.
  This is what makes save/load a direct serialization of the model rather than a traversal
  of the live god-object — the current save path's core problem.
- **Idiomatic C++23.** `enum class`, designated init, `std::optional`/`std::span`, value
  semantics; SDRAM-resident containers where variable-length (automation node vectors).

### 2. Snapshot — the engine's derived read-view

To play the model the engine builds a **snapshot**: a runtime-optimized projection of a
model subtree into the form the render/sequence path wants. A snapshot is:

- **Derived, never authoritative.** It is a cache of the model, not a second copy of truth.
- **Rebuilt or patched on change**, ideally *incrementally* (per affected lane / per
  affected executor unit) on hot paths — a full rebuild is the fallback, not the norm.
- **The only thing the executor reads.** The audio thread walks the snapshot, not the
  model — so the model is free to use SDRAM `Vec`s and lookup maps the RT path could not
  afford.

### 3. Executor — the player

Render, sequencing, voices. It **owns only what is not saved**: voice state, interpolation
cursors, LFO phase, live clip playback position, DSP scratch. None of it is persistent, so
tearing the executor down and rebuilding it from the model is loss-free — this is the seam
that eventually makes warm restart / fault isolation (`target_architecture_north_star.md`)
possible: the executor is a fault domain whose entire recoverable state is "re-snapshot the
model."

---

## Identity — ids, not pointers

The whole API rests on one decision: **things are named by stable ids, and references are
resolved at the moment of use and never stored.**

```cpp
struct OwnerId { enum class Kind { SONG, CLIP, INSTRUMENT, DRUM }; Kind kind; uint32_t serial_id; };
struct ParamAddress { OwnerId owner; params::Kind kind; int32_t param_id; std::optional<uint32_t> clip_serial; };
```

These already exist (`param_address.h`). They are the direct analog of Spark's typed ids
(`ClipId(Uuid)`) and its `AutomationOwner` enum — *the same pattern*, adapted to the
embedded target.

- **Durable, pointer-free.** An address survives the object moving in memory, being
  pool-reallocated, or being reloaded from disk. That is why it exists: the legacy path
  threaded a live `ParamCollection*`/`AutoParam*` chain through the `ModelStack`, which
  dangles the instant anything relocates. Addresses killed that hazard class.
- **Resolve at use, never hold.** `Song::resolveParam(addr) -> DocAutoParam*` (null if the
  address names nothing) is the read primitive. A resolved reference lives only for the
  duration of one operation — an ephemeral borrow, never a stored member. **Safety comes
  from not keeping the pointer, not from wrapping it in a handle.**
- **Handles are a convenience, not the substrate.** A fluent navigator
  (`model.clip(7).sound().param(Cutoff)`) is at most an ergonomic, type-checked *constructor
  for an address* that immediately resolves — it is never a persistent object you store
  across edits. Address-primary, not handle-primary.

Why an id and not a raw pointer, restated as the rule of thumb: **an address is for the
three places a pointer physically cannot go** — onto disk (serialization), across time or
threads (deferred reconcile of recorded automation), and across a reload/restart (re-bind).
For pure in-memory access a resolved reference is both faster and safer; addresses earn
their place only at those boundaries.

### Embedded divergence from Spark

Spark is a desktop DAW; the Deluge is an RZA1L with tight SDRAM and a hard-RT audio thread.
Adopt the *shape*, not the *mechanism*:

- `uint32 serial_id` + a small ordered map / index, **not** `Uuid` + `HashMap`.
- The command path must be **allocation-disciplined**: no boxed `dyn`-style heap objects on
  anything the audio thread touches; snapshot updates incremental, not full rebuilds, on hot
  paths.
- Off-audio-thread paths (UI edits, load) may allocate freely in SDRAM.

---

## The write API — Commands (grown from Action / Consequence)

Writes are **reified command objects**, not scattered setters and not handle mutators. This
is the piece to *grow out of the existing `Action`/`Consequence` system*, not invent beside
it.

### What Deluge already has, and the gap

Deluge today owns the **reverse** half of the command pattern and not the forward half:

| Concept | Spark | Deluge today |
|---|---|---|
| forward mutation, reified | `Command::execute(&mut Project)` | **scattered engine code** (`param_edit`, note editing, …) |
| inverse / undo | (command's undo) | `Consequence::revert(time, modelStack)` ✅ |
| transaction / undo-tree | `UndoManager` | `Action` + `ActionType` ✅ |
| coalescence (merge knob-turns) | `CoalescenceKey` | `Action`/`ActionType` merging (informal) ✅ |
| events → snapshot | `ProjectCommandEvent` → engine `Event`(snapshot) | **missing** |
| id addressing | `ClipId` / `AutomationOwner` | `OwnerId` / `ParamAddress` / `serialId` ✅ (half-built) |

A `Consequence` is a **snapshot-for-undo**: `ConsequenceParamChange`,
`ConsequenceNoteExistence`, `ConsequenceClipLength`, … record the *old* state so
`revert()` can restore it. The **forward** mutation lives elsewhere and *separately*
remembers to log a Consequence into the open `Action`. The do-site and the undo-record-site
are split. The 28 `ActionType`s are already a command catalog in embryo.

### The command shape

A **Command** reifies the forward mutation and owns the whole operation:

1. **Addresses its target by id** (`OwnerId`/`ParamAddress`/`serialId`), resolving at
   execute — no threaded `ModelStack` pointer chain.
2. **Mutates the Model** (the authoritative write).
3. **Produces the `Consequence`(s) that invert it**, into the open `Action` — unifying the
   do-site and the undo-record-site that are split today.
4. **Emits an event** the engine turns into a snapshot update.

### Write origin decides the path

- **User-intent edits** (knob, UI param change, structural edit) → Command → **Model
  directly**, bypassing the engine. The model is always current for anything the user
  *meant*.
- **Engine-observed, time-captured data** (recording automation during playback) →
  originates in the **executor/snapshot** because it needs RT time-tracking, then
  **reconciles back** into the Model as `{ address, value }` — held in the snapshot until
  synced, or written through directly, whichever is cleaner per case.

### `Action` sheds its UI baggage

`Action` stays as the **transaction / undo-tree / coalescence unit**. But today it carries
view state — `xScrollClip`, `yScrollArranger`, `xZoomClip`, `modeNotes`, `modKnobModeSongView`,
… That is a frontend concern. Spark keeps commands *pure-project* ("no access to engine or
host"). In the end state that view state moves to the frontend; the core command/Action
layer is model-only.

### Staged convergence (decided)

- **Now:** Command **produces** Consequences. The entire existing `Consequence`/`revert`
  machinery and `Action` undo-tree keep working; we add the forward-reified layer on top and
  migrate scattered mutations into it. `Consequence` stays the undo currency.
- **Later:** converge to a **single reversible Command** (`execute` / `undo`, capturing its
  own before-state, like Spark's `UndoableCommand`) and retire `Consequence` as a separate
  type. Named as the eventual simplification, not front-loaded.

---

## The save/load API

- **Save** = serialize the Model directly. No walk of live engine objects. Because the model
  is value-typed and self-contained, this is a structural write of the model, not a scrape
  of the running machine.
- **Load** = deserialize into the Model, then the engine **snapshots** from the freshly
  populated model. Loading stops being "construct engine objects and hope the model view
  follows."
- The on-disk **key currency is the address** (ids + enums), because that is the one naming
  scheme that is meaningful as data and stable across reload.

---

## How the in-flight work maps onto this (so it stops feeling lost)

Every active DOM slice is a course in the foundation under the command/event layer:

| In-flight effort | What it is really building |
|---|---|
| **Option I param-authority slices** (scalar `current_value` authority; store relocation into the tree) | "the **Model** owns the persistent param scalar+nodes" — the authoritative storage the Command layer writes into. Foundation, not detour. |
| **`DocAutoParam`/`DocParamSet`/`DocParamManager`** (`param_automation_dom.md`) | the value-typed param leaf/collection the model owns and serialization walks. |
| **`OwnerId`/`ParamAddress`/`resolveParam`** | the **identity + resolve-at-use** half of the API. |
| **value tier already DOM-resident** (metadata/groove/transport/scale/reverb/arrangement) | the "structure is data" principle, already realized for the easy tier. |
| **clip/drum store relocation into the tree** (Slice S) | pulling structural param storage out of engine ownership into the model — the "model owns structure" inversion, one owner at a time. |

**What is not yet started** and is the next conceptual layer: the **forward Command
reification**, the **event → snapshot feed**, and the **executor/snapshot boundary** as an
explicit thing.

---

## Sequencing (current state → destination)

Read top-to-bottom as "largely precedes." Each rung is golden-gated and committable.

```
  [DONE-ish]  value tier is data · OwnerId/ParamAddress exist · resolveParam seam
                    │
  [IN FLIGHT] Model owns param scalar+nodes (Option I) · clip/drum stores in the tree
                    │
      ┌─────────────┼──────────────────────────────┐
      ▼             ▼                               ▼
  Model owns    forward Command reification     Snapshot as an explicit
  structural    (address→mutate→Consequence)    engine-built read-view
  data (clips/  grown from Action/Consequence    (incremental patch on change)
  sounds as                    │                          │
  value data)                  ▼                          ▼
      │             Action sheds UI view-state    event → snapshot feed
      │                        │                          │
      └────────────┬───────────┴──────────────────────────┘
                   ▼
        save = serialize model · load = deserialize + snapshot
                   │
                   ▼
        [LATER] converge Command/Consequence into one reversible Command
                   │
                   ▼
        [FAR] executor as a rebuild-from-model fault/restart domain
             · serialization backend swappable (Rust+serde) — out of current scope
```

Ordering facts this makes explicit:

- **"Model owns structural data" and "forward Command reification" are the two big remaining
  bodies of work** and are largely independent — commands can be reified against the param
  storage that already exists before every structural owner is inverted.
- **The snapshot boundary can be introduced incrementally** — start by naming the derived
  view the engine already effectively rebuilds, then make its updates event-driven.
- **The Command/Consequence convergence is deliberately last** among the near-term work: it
  is a simplification that only pays off once forward mutations are all reified.

---

## Open questions (resolve as the work hits them)

- **Naming.** `Action` is the transaction; the forward-reified per-operation object needs a
  name (`Command`? `Edit`? `Op`?) that does not collide with `Action`/`Consequence`. Decide
  before the first forward command lands.
- **Snapshot granularity.** One snapshot per executor unit (per active clip / voice group)
  vs one per song? Incremental-patch protocol shape. Measure on the RT path.
- **Reconcile timing for recorded automation.** Write-through vs hold-in-snapshot-until-sync
  — likely per-case; enumerate the cases (automation record, live-resampling, param-record).
- **Structural command coverage.** Adding/removing a clip/instrument is a command too, not
  just param edits — confirm the structural `ActionType`s port cleanly to the command shape.
- **Coalescence formalization.** Today `Action`/`ActionType` merging is informal; a
  `CoalescenceKey`-style `{command-type, target-id}` (Spark's shape) would make knob-turn
  merging explicit.

---

## Reference: Spark

Spark (`~/GitHub/spark`, `libs/project/spark-project`) is the working proof of this exact
architecture in Rust: a `Project` that owns all structure keyed by typed-UUID ids; typed
`Command`/`UndoableCommand` structs that `execute(&mut Project)` and return events; an
`UndoManager` with coalescence; and an engine adapter that maps command events to snapshot
data consumed by the audio executor. This doc is that architecture adapted to the Deluge's
C++ codebase and embedded constraints — adopt the shape, diverge on the mechanism.
