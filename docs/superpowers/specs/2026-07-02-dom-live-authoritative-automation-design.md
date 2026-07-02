# DOM-live-authoritative automation — the clean split

**Status:** design approved (2026-07-02); ready for implementation planning.
**Supersedes:** the R2/R3 "redirect / owner-store" storage-relocation approach (to be unwound).
**Owner:** kate. Branch: `deluge-project`.

## 1. Motivation — why we're re-architecting

The param/automation DOM program (`deluge::project`) built a solid value + serialization
model (Phases 1–5b: `DocAutoParam`/`DocParamManager`, byte-identical serializers/readers,
projection both ways). Then, to make automation **DOM-live-authoritative** without
rewriting the ~20 timeline-aware edit ops, R2/R3 made the engine's live `AutoParam`
**share** DOM-owned node storage via a redirect pointer (`AutoParam::redirectNodeStorage`),
with fixed-size owner stores.

That shared-storage shortcut generated an entire class of pointer-lifetime bugs — each a
fixup hook for a different way memory moves:

- **Clone** (byte-copy an owner/manager) copies the redirect pointer → it aliases the
  source store. Patched by `AutoParam::beenCloned` sever (R3a).
- **Steal** (`stealParamCollectionsFrom`, a pointer-move) bypasses `beenCloned`. Patched by
  `severNodeStorageRedirects`.
- **Patch-cable clone** (`PatchCableSet::beenCloned` unusable path) `init()`ed through the
  redirect → cleared the source store. Patched again.
- **Relocation** (a `NoteRowVector`/`fast_vector` byte-relocates its elements on grow) moves
  the store to a new address while the redirect points at the old one → dangle. This one
  had **no** fixup hook and crashed 9/15 kit songs on save. It is what stopped the program.

Two root smells underlie all of it:

1. **The modulation subsystem clones by `memcpy` + manual `beenCloned` fixup**, not C++
   value semantics. Every copy/move is a hand-managed hazard.
2. **We tried to make the DOM own *live* node storage while the *edit engine* still lives on
   `AutoParam`**, so the two subsystems shared storage through raw pointers. That sharing is
   the sole reason all the machinery (redirect, stores, handles, sever hooks, relocation
   fixups) exists. Remove the sharing and **none** of it is necessary.

**Decision (kate):** automation is to be genuinely **DOM-live-authoritative**. The clean way
to achieve that is the spark-style split — the DOM owns the nodes *and the operations on
them*; the engine is a *projection*. The RT cost of re-deriving the engine's playback form is
explicitly deferred to the future engine graph-builder/executor split ("reproject then; that's
a future problem"). We design for correctness now, performance later.

## 2. End-state architecture

`deluge::project` is the **live authority** for automation:

- It **owns the nodes** (`DocAutoParam { int32 current_value; std::vector<ParamNode> nodes }`,
  already exists) and **owns the edit operations** on them.
- The engine's `AutoParam` is a **projection**: it holds nodes projected from the DOM for
  playback, re-derived when the DOM changes. It keeps its RT interpolation cursors.
- **Data flow is one-way and unshared:**
  - Edit → mutates the DOM (`DocAutoParam`) via the DOM edit API.
  - Projection → DOM → engine `AutoParam` nodes (`fromDoc…`), on load and after edits.
  - Playback → engine reads its own projected nodes (RT-owned copy).
  - Save → reads the DOM (5c-3).
- **No shared storage, no redirect, no handles, no fixup hooks.** The engine→DOM link is a
  projection, not a shared pointer, so there is nothing to dangle under clone/move/relocation.

Because the engine's projected nodes are engine-owned-by-value again, the existing
`memcpy`+`beenCloned` clone path is *safe* (no redirect to dangle) — fixing it → real value
semantics becomes optional cleanliness (Phase 5), not a prerequisite.

## 3. Reused vs. unwound

**Reused (≈80% of the DOM already built, solid):**
- `DocAutoParam` / `DocParamSet` / `DocParamManager` — the data model (Phases 1–3).
- `toDocParamManager` / `fromDocParamManager` — projection both directions (incl. the
  summary-flag refresh, `refreshAutomationFlags`).
- Byte-identical serializers (5a) + DOM-native readers (5b).
- R1's `nodes()` accessor seam on `AutoParam` (a clean indirection a projection can use).

**Unwound (scaffolding for the abandoned shared-storage approach):**
- `AutoParam::redirectedNodes_` / `redirectNodeStorage` / `severNodeStorageRedirect`.
- Owner stores: `GlobalEffects::param_node_store`, `Clip::paramNodeStore_`,
  `InstrumentClip::patchedNodeStore_` / `cableNodeStore_`.
- `ParamCollection::severNodeStorageRedirects` (+ overrides), the `stealParamCollectionsFrom`
  sever call, the `beenCloned` redirect-sever, the `PatchCableSet::beenCloned` sever.
- `ParamManager::authoritativeParams_` + `commitUnpatchedValueThroughDom` (the value-commit
  path — vestigial: never read at save).
- Commits to revert (most recent first): `e9f8fd998`, `3a537b799`, `c0d135537`, `3ca10f0df`,
  `c7b266d07`, `877be9047`, plus the redirect parts of R1/R2 (`49a61611a`, `a7071a4c9`) —
  keep R1's `nodes()` accessor, drop the redirect member.

## 4. Decomposition

A program, sequenced so every phase leaves a green, releasable tree.

### Phase 0 — Unwind R2/R3 (clean baseline)
Remove the redirect member, the owner stores, and all the sever/steal/relocation machinery.
Return to: engine `AutoParam` owns `ownNodes_`; save projects engine→DOM (5a); load projects
DOM→engine (5b). Keep R1's `nodes()` accessor (now always returns `ownNodes_`).
- **Verify:** `./dbt build Debug` clean; golden **OK=15** (byte-identical — this is the
  pre-R2 behaviour); `param_projection_test` OK; `automation_edit_test` reduced to the
  projection/edit cases that don't assume a redirect (or retired).
- **Payoff:** deletes the NoteRow blocker and the whole pointer-lifetime bug class in one move.

### Phase 1 — DOM edit ops (the data layer)
Implement the timeline-aware edit operations on `DocAutoParam` (and `DocParamSet` /
`DocParamManager`) in `deluge::project`, operating on `std::vector<ParamNode>`. Port from
`AutoParam` (`modulation/automation/auto_param.cpp`): `setNodeAtPos`, `setValueForRegion`,
`setValuePossiblyForRegion`, `getValueAtPos` (interpolation query), `homogenizeRegion`,
`deleteTime`/`insertTime`, `stealNodes`/`insertStolenNodes`, `moveRegionHorizontally`,
`generateRepeats`, `shiftHorizontally`, `deleteNodesWithinRegion`, etc.
- **Key design rule:** the DOM ops take timeline context (loopLength, livePos, reversed,
  quantization, `Action*`/undo-snapshot sink) as **parameters** — they must NOT reach into
  engine globals (`playbackHandler`, `actionLogger`). This is what makes them testable in
  isolation and portable.
- **Undo:** the ops accept an optional snapshot sink so callers can capture a before-state
  for the Action/undo system; the snapshot is a `DocAutoParam` value (cheap std::vector copy).
- **Interpolation cursors stay in the engine** — Phase 1 covers node *editing*, not RT
  playback cursor state.
- **Verify:** a dedicated dual-arch unit spec (x86 + ARM/qemu) per op, in the projection-test
  style; this is the strongest test surface the program has had (pure data, no engine).

### Phase 2 — Song-global authority pilot (the full loop)
Make `GlobalEffects::params` the live authority for the song-global param tier, end to end:
- Route song-global param edits (the `gui/…` + `Song::setBPM`-style paths) through the DOM
  edit ops.
- After each edit, **reproject** the affected lane DOM→engine (`fromDocAutoParam`) so playback
  sees it. (Naïve per-edit reprojection is fine — RT-optimal reprojection is the deferred
  engine-split problem.)
- Undo snapshots the DOM lane.
- **Flip save to read the DOM (5c-3)** for song-global.
- **Verify:** golden OK=15 (byte-stable), `param_projection_test` OK, plus a
  `automation_edit_test`-style sim harness proving edit → DOM → reproject → playback → save
  reads DOM, mutation-checked. Song-global is self-contained (singleton, DOM structure exists).

### Phase 3 — Clips / outputs
Same pattern for clip/output param managers. **Gated on** per-clip DOM param representation —
part of the broader project-model extraction (clips into `deluge::project`). Uses the Slice-2
`serialId` identity for the owner↔DOM-lane mapping.

### Phase 4 — NoteRows / kit drums + MPE/MIDI
Same, once NoteRows/drums and MPE/MIDI-CC collections have DOM representation.

### Phase 5 (optional) — Retire `memcpy`+`beenCloned` → value semantics
Independent cleanup: give `ParamCollection`/`ParamManager`/`NoteRow`/`AutoParam` real
copy/move constructors and delete the byte-copy idiom. Now safe (no redirects), removes code
and the last of the hand-managed clone hazards. Do only if/when it earns its keep.

## 5. Key mechanisms

- **Projection (DOM→engine):** reuse `fromDocParamManager`/`fromDocAutoParam`. After an edit,
  reproject the touched lane; on load, reproject the whole manager (already how 5b works).
- **Owner↔DOM identity:** song-global = the `GlobalEffects` singleton (trivial). Clips/outputs
  = `serialId` (Slice 2). NoteRows = per-drum identity within a clip (to be defined with the
  project-model extraction). This mapping is used only for *projection* (one-way), never for a
  shared mutable pointer — so it cannot dangle.
- **Undo/recording:** DOM edit ops expose a snapshot hook; the Action system stores
  `DocAutoParam` before-states. Live automation recording routes through the same DOM ops
  (correctness first; recording-rate reprojection perf is deferred).

## 6. Dependencies & sequencing

1. **Phases 3–4 depend on the project-model extraction** (clips/NoteRows represented in
   `deluge::project`). Phase 2 (song-global) is self-contained and can land first.
2. **Undo/Action integration** is a cross-cutting requirement of Phase 1's API shape — design
   the snapshot sink up front so Phase 2 can wire it.

## 7. Non-goals / explicitly deferred

- **RT-optimal reprojection / audio-thread consumption of DOM nodes** — deferred to the future
  engine **graph-builder/executor** split. Until then, correctness-first reprojection is
  acceptable.
- **Making the value/config tier's live-authority any different** — that tier is already
  DOM-authoritative and clean; untouched.
- **Cross-thread concurrency hardening** beyond the Deluge's existing edit-between-routines
  model — deferred with the engine split.

## 8. Verification strategy

- **Phase 0:** golden byte-identical (OK=15) + firmware clean — a pure unwind must not change
  behaviour.
- **Phase 1:** dual-arch (x86 + ARM/qemu) unit specs per DOM edit op — the pure-data layer is
  fully CI-verifiable, unlike the golden-blind engine edit path.
- **Phase 2+:** golden (save byte-stability) + sim `automation_edit_test`-style harness for the
  edit→DOM→reproject→save loop, mutation-checked; hardware spot-check for live-recording feel.

## 9. Risks

- **Edit-op port fidelity (Phase 1):** the ~20 ops carry subtle interpolation/reverse/loop
  semantics. Mitigation: port one at a time behind dual-arch specs; keep the engine op as a
  reference oracle during the port (diff DOM-op vs engine-op on random inputs).
- **Reprojection cost during live recording (Phase 2):** naïve per-node reprojection is O(n)
  per node. Acceptable for the pilot; the deferred engine split addresses it. Flag if it bites
  before then.
- **Project-model coupling (Phases 3–4):** don't start them until clips/NoteRows exist in the
  DOM; otherwise we recreate an identity/ownership tangle.
- **Scope:** this is a multi-phase program comparable to the whole 5c effort. Phase 0 + Phase 1
  + Phase 2 (song-global) is a coherent, self-contained first delivery; 3–5 ride on the
  project-model extraction and can be scheduled independently.
