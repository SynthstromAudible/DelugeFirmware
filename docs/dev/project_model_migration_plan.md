# Project model — migration plan (funnel-then-flip)

Status: **program plan (living doc, 2026-07-07).** This is the route map from the current
state of the codebase to the end-state defined in `project_model_north_star.md`. The north
star says what the destination *is*; this doc says **how we get there**: the milestone
ladder, the per-owner method, module topology, norms for new code, the rewrite strategy for
old code, and the risk register.

Granularity: **milestones + first slices.** Each milestone has exit criteria and its first
one-to-three slices sketched concretely enough to start; later slices are named but
deliberately unspecified — earlier slices will teach us their shape.

---

## Ground state (assumed, outside this plan)

The plan starts from **Option I complete**: the branch `deluge-project-param` landed —

- the model owns the persistent param **scalar + automation nodes**;
- the clip/drum param stores live **in the project tree** (no engine-seed);
- the param **save path reads the DOM**;
- `OwnerId` / `ParamAddress` / `resolveParam` exist and are the addressing currency;
- the value tier (metadata / groove / transport / scale / reverb / arrangement) is
  DOM-resident data.

Unblocking the Slice S save-flip (the instrument-save corruption, Task #64) is part of
landing Option I and is **not** re-planned here.

What remains, per the north star, is: **forward command reification**, **structural
ownership inversion** (clips / notes / kits / drums / sounds as value data), the
**snapshot boundary + event feed**, and the **save/load completion**.

---

## The strategy: funnel-then-flip

Option I's method (grow model type → dual-write → flip read → flip save → retire engine
copy) works, but it pays its worst cost — hunting down every scattered engine write-site —
**per owner, repeatedly**. The Slice S save-flip blockage is exactly that failure mode: a
write path the flip missed.

So the program reorders one thing: **before inverting an owner's structure, first reify its
mutations as commands** ("Edits", see M1), even while those Edits still write to engine
objects. Once all writes to an owner go through a handful of Edit objects, flipping
authority means changing those few sites instead of chasing dozens. The write-site hunt is
paid **once per owner, as an explicit reusable artifact** — the Edit catalog *is* the
write-path inventory, and it unifies the do-site with the undo-record-site, which the north
star wants anyway.

Inside each rung, Option I's slice discipline is retained: behavior-preserving,
golden-gated, committable slices.

---

## The milestone ladder

```
   M1  Edit substrate + param Edits (the proof)          ── strictly first
        │
   M2  structural write funnel (Edits over engine objs)  ──┐ interleaves: an owner's
        │                                                   │ M3 inversion may start as
   M3  ownership inversion, per owner, riding the funnel ──┘ soon as ITS writes funnel
        │  (per-owner load flips happen here)
        ▼
   M4  snapshot boundary + event feed                     ── needs M3's bulk
        │
   M5  save/load completion (serialize/deserialize model) ── needs M3 complete
        │
   M6  [LATER] converge Edit/Consequence → one reversible command
        │
   [FAR] executor as rebuild-from-model restart/fault domain · Rust+serde backend
```

### M1 — Edit substrate + param Edits (the proof)

Resolve the north star's naming question first: the forward-reified per-operation object is
an **`Edit`** — `Command` collides with MIDI/HID command handling, `Action` is taken by the
transaction. (Confirm or amend at slice 1; the name must be settled before the first Edit
lands.)

Build the spine. An Edit:

1. addresses its target by id (`OwnerId` / `ParamAddress`), resolving at execute;
2. mutates (for params: the **model directly** — storage is already DOM-owned);
3. produces the `Consequence`(s) that invert it, into the open `Action`;
4. emits a change event. In M1 the emission **point** exists but the feed is a no-op /
   direct engine poke — the real event → snapshot feed is M4.

Then migrate all param mutations into Edits. Params go first because their model storage
already exists, so these are true model-writes from day one — proving the whole shape
end-to-end, including coalescence (knob-turn merging).

**First slices**

- **M1a — naming + skeleton.** The `Edit` base/concept, its execution context, wiring into
  the open `Action`. No behavior change; golden-neutral by construction.
- **M1b — pilot Edit: param value change (knob-turn), end-to-end.** One real mutation path
  moved into an Edit, with coalescence. This slice is the proof; everything about the shape
  gets stress-tested here.
- **M1c — sweep the remaining param mutation call-sites** into Edits (`param_edit`,
  automation editing, menu-driven sets, patch-cable amounts, MIDI-CC).
- **M1d — save/load round-trip harness** (see Cross-cutting). Low-risk milestone, so the
  gate infrastructure lands here, before any M3 save-flip needs it.

**Exit criteria:** every param mutation flows through an Edit; do-site and undo-record-site
are unified for params; coalescence works; goldens green; round-trip harness running in CI.

### M2 — Structural write funnel

Reify the rest of the persistent-mutation surface as Edits **while they still write to
engine objects**. Each Edit is id-addressed even though its execute body still pokes engine
state — the address discipline comes first, the ownership comes later (M3).

The 28 `ActionType`s are the starting catalog: note add/remove/edit, clip
create/delete/length/section, instrument swap, arrangement edits, kit row operations, …
Structural creation/deletion are Edits too, not just value tweaks.

`Action` sheds its UI baggage in this milestone: the view-state it carries (`xScrollClip`,
`yScrollArranger`, zoom, mode fields, …) moves to a frontend-owned side-record keyed by the
Action, leaving the core transaction layer model-only.

**First slices**

- **M2a — catalog.** Inventory the `ActionType`s and the mutation surface behind each into
  a checkable Edit catalog (a table in this doc's companion or a header comment block):
  Edit name → addressed target → engine sites it subsumes.
- **M2b — pilot structural Edit: note add/remove.** The highest-traffic structural
  mutation; exercises id-addressing a note inside a NoteRow inside a clip.
- **M2c — Action view-state shed.** Mechanical, independently committable.

**Exit criteria:** every persistent-state mutation in the codebase goes through an Edit;
legacy scattered setters retired, or made private to Edit implementations so stragglers are
compile errors; `Action` is view-state-free.

### M3 — Structural ownership inversion, per owner

Option I's flip sequence, now with single-digit write-sites per owner:

> grow the model value type → dual-write from the Edits → flip read authority → flip save →
> **flip load** → retire the engine-held copy (engine class shrinks toward pure executor
> state).

Per-owner **load** flips happen here, inside each owner's slices — M5 only collapses the
remainder.

**Owner order** (easiest first, hardest last; each owner may start as soon as its writes
are funneled — no need to wait for all of M2):

1. **Clip structure** — length, loop points, section, colour, launch behavior.
2. **Notes / NoteRows** — the clone-idiom retirement already modernized the containers.
3. **Kits / drums** — structural membership and per-drum descriptors.
4. **Sample & audio-clip descriptors** — file refs, start/end/pitch metadata.
5. **Sounds / instruments** — last and largest: splitting `Sound`'s persistent
   parameterization from its DSP state, guided by the existing synth state-decoupling audit
   (`docs/dev/synth_state_decoupling.md`). By this point the method has been drilled on
   four easier owners.

**First slices** (owner 1, clip structure): grow the model clip value type in `clips.h`;
dual-write from the clip Edits; read flip; save flip gated on the round-trip harness; load
flip; retire the engine fields.

**Exit criteria (per owner):** the model type is the single authority; the engine object
holds no persistent copy of that owner's data; save and load for that owner operate on the
model; goldens + round-trip green; hardware check at the owner boundary for save flips.

### M4 — Snapshot boundary + event feed

Deliberately after M3's bulk, so snapshots project **from the model** and are built once,
not twice (projecting from engine objects first would mean building the projection layer
twice).

- **Name the derived read-view** the engine already effectively rebuilds; seal the executor
  behind it — the audio thread reads snapshots, never the model.
- Make Edit-emitted **events drive incremental snapshot patches**; full rebuild is the
  fallback, not the norm, on hot paths.
- Resolve the north star's granularity question (per-executor-unit vs per-song; patch
  protocol shape) **by measurement on the RT path**, not up front.

**First slices:** (a) name and carve the snapshot type over what the engine rebuilds today,
behavior-identical; (b) event-feed pilot for param changes (the M1 emission point gets its
first real consumer).

**Exit criteria:** executor reads only snapshots; edits propagate via events; RT cost
measured and acceptable; the reconcile path (engine-observed recorded automation → model)
is explicit.

### M5 — Save/load completion

With every owner flipped: **save** is a pure serialization of the model — no live-object
walk anywhere; **load** is deserialize-into-model, then the engine snapshots from the
freshly populated model. Retire the legacy read-tags-into-engine-objects paths wholesale.
The on-disk key currency is the address (ids + enums), per the north star.

**Exit criteria:** the model compiles and round-trips to disk with the engine absent (the
host-side proof that "no engine dependency" is real).

### M6 — [LATER] Edit/Consequence convergence

Converge to a single reversible Edit (`execute` / `undo`, capturing its own before-state)
and retire `Consequence` as a separate type. Deliberately last among near-term work: a
simplification that only pays once all forward mutations are reified.

### [FAR] — named, not planned

Executor as a rebuild-from-model restart/fault domain (`target_architecture_north_star.md`);
serialization backend swappable to Rust + serde. Out of scope here.

---

## Cross-cutting

### Module topology and the purity gradient

- `src/deluge-project/` keeps the hard rule: **no includes from the engine tree, ever**.
  Enforce mechanically with an include-lint (same pattern as the libdeluge allowlist lint).
- Edits **begin life in the app tree** (`src/deluge/model/edits/`) because during the
  funnel phase they mutate engine objects. **Relocation is the purity certificate**: when
  an owner flips in M3 and its Edits no longer touch engine state, those Edits move into
  `src/deluge-project/edits/`, where the include-lint physically prevents regression to
  engine-poking. End-state: all Edits live inside the model module, pure-project.
- Snapshot code is the mirror image: engine-side, depends on `deluge-project` read-only;
  the model never knows it exists.
- Net dependency direction is strictly one-way: **engine → model, never back.**

### Norms for new code (in force from the moment M1a lands)

1. **New persistent state goes in the model first** — no new engine-seeded fields, ever.
   The engine may hold a derived copy, never the authority.
2. **New mutations of persistent state are written as Edits** — no new scattered setters.
3. **The address rule**: ids at the three boundaries (disk, deferred/cross-thread, reload);
   ephemeral resolved references everywhere else; never stored members.
4. **Idiomatic C++23**: value types, `enum class`, designated init,
   `std::optional`/`std::span`, fixed-cap strings, SDRAM vectors for variable-length.
   Complex encodings (Q-format, units) documented at the declaration.

### Rewrite strategy for old code

- **Strangler-in-place only.** Engine classes shrink toward pure executor state; we never
  build a parallel replacement and swap.
- **Slices are behavior-preserving and golden-gated**: full x86 sweep per slice, targeted
  ARM per slice, full ARM + hardware at milestone boundaries.
- Legacy logic being relocated is **rewritten into modern idiom while keeping behavior
  faithful** (the Phase 3 norm).
- File layout is not sacred — `git mv` freely to express the target shape.
- When the funnel reduces a legacy mutator to a thin forwarding wrapper, **delete it and
  inline callers** rather than leaving the husk.

### Idiom debt inside the model module

Two populations, two lanes:

- **Existing residue.** `deluge-project` is not uniformly idiomatic: the value-tier headers
  went through the Phase 3 refinement (WS1–WS3), but code relocated in during the DOM
  slices (`serialization.cpp`, `param_backup`, parts of `param_automation`) was moved
  faithfully, not rewritten, and Phase 3's WS4 (modernize) + WS5 (read-seam) were never
  finished. This residue gets a **dedicated modernization lane**: idiom-only,
  behavior-preserving commits, run as interstitial work between milestone pushes, **never
  mixed into a behavior-flip commit** — when a golden fixture shifts, the commit must be
  unambiguously "moved authority" or "rewrote a loop," not both. Finishing Phase 3 WS4/WS5
  is hereby a named backlog item of this plan.
- **New entrants.** Anything crossing the border into `deluge-project` — Edits relocating
  in after their owner flips, model types grown during M3 — is rewritten to idiom **at the
  moment of crossing**. The move is the cheapest rewrite opportunity available (the code is
  already being re-read and re-gated). No "move now, clean later" IOUs — those IOUs are how
  the current residue accumulated.
- **Mechanical enforcement.** Ratchet `src/deluge-project/.clang-tidy` to a materially
  stricter check set than the app tree (`modernize-*`, no C-casts, enum-class enforcement
  where checkable). Files awaiting their WS4 pass carry explicit suppressions — a visible,
  shrinking worklist.

### New gate infrastructure: save/load round-trip harness (M1d)

The Task #64 lesson: the golden harness (load → render → compare audio) is **blind to
save-path corruption**. Before any M3 save-flip:

- **Round-trip gate**: load fixture → save → reload → save again; compare semantically
  (and byte-wise where the format is canonical).
- **Undo/redo scripting hook**: moving mutation sites into Edits can silently break
  `Consequence` logging, which audio goldens also cannot see. Script edit → undo → compare,
  edit → undo → redo → compare, over the fixture set.

---

## Risk register

| Risk | Mitigation |
|---|---|
| **RT discipline of the Edit path** — knob-turns arrive at UI cadence near the audio thread | Edits are UI/off-RT constructs and may allocate only off-RT. Audio-thread-originated writes (recorded automation) stay on the **reconcile path** (`{address, value}` back into the model) — they are *not* Edits. |
| **Funnel completeness** — "every write goes through an Edit" is only as true as the sweep | As each owner funnels, make its legacy mutators private to the Edit implementations, so stragglers become compile errors; round-trip + undo harnesses catch what slips. |
| **Sound/instrument inversion size** | Deliberately last; guided by the synth state-decoupling audit; method drilled on four easier owners first. |
| **Undo semantics drift** while relocating mutation sites | Undo/redo scripting hook (M1d) over the fixture set. |
| **Snapshot RT cost** (rebuild vs patch) | M4 resolves granularity by measurement; incremental patch is the design intent, full rebuild the fallback. |
| **Edit-shape rework** — command shapes designed before the model owns their targets (the funnel-then-flip trade-off) | Param Edits (M1) prove the shape against real model storage first; structural Edits address by id from day one, so the flip changes their *body*, not their *interface*. |

---

## Relationship to other programs

This plan is self-contained but adjacent to: **libdeluge separation** (module/lint
patterns are shared; no ordering dependency), **synth untangling** (owner 5 in M3 consumes
its state-decoupling audit; the Engine-tier work is independent), and the **target
architecture north star** (M-far's restartable executor is that doc's fault-isolation
seam). Neighboring programs are referenced as interfaces, not planned here.
