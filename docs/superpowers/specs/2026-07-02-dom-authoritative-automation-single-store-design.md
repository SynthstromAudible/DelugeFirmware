# DOM-authoritative automation via a single shared node store — Design Spec

**Date:** 2026-07-02
**Status:** Approved (brainstorm), pending implementation plan
**Supersedes the storage strategy of:** `2026-07-02-dom-live-authoritative-automation-design.md`
(that spec chose *projection* — two copies kept in sync; this spec replaces it with a *single
shared store* owned by the DOM at a stable address, with the engine holding a raw borrow-pointer).

---

## 1. Context

`deluge::project` (the DOM) is already serialization-authoritative for automation both ways (5a save
/ 5b load, golden `OK=15`), and the DOM value/node types (`DocAutoParam`, `DocParamSet`,
`DocParamManager`) plus the full set of edit-op kernels (`src/deluge-project/param_edit.*`) are in
place and dual-arch tested. Song-global gold-knob *value* edits are already DOM-first (Phase 2 / 2b).

What is **not** done: automation **node** edits (draw / delete / shift / region-set) are still engine
node-vector mutations, and `save` still reads the engine, not the DOM. The remaining goal is to make
the DOM the **single authoritative owner of automation node data** so that every edit and every undo
operates on DOM-owned storage, and `save` reads it directly.

### History that shapes this design

An earlier attempt (R2/R3, the `merry-churning-trinket` plan) made the engine `AutoParam` a **view
over owner-held storage via a redirect pointer** — shared storage. It shipped green for
address-stable owners (song-global singleton, heap-allocated clips) but was **fatal for NoteRows /
kit drums**, which live in a `fast_vector` that *byte-relocates its elements on grow*: the storage
moved out from under live redirect pointers → dangling-pointer crash in `save`. Phase 0 unwound all
of R2/R3 to a clean baseline, and a *projection* redesign (two copies + per-edit sync) was adopted to
sidestep the dangle.

This spec returns to a **single shared store**, but fixes R3's root error: R3 embedded the store *in
the owner*, so relocating the owner moved the store and dangled the engine's pointer. This spec puts
the store **in the DOM at a stable address**, with the engine holding only a raw borrow-pointer — the
one shape byte-relocation permits (§4.2). That makes owner relocation harmless for *every* owner by
construction, so owner scope becomes a rollout axis rather than a safety boundary; projection (two
copies + per-edit sync) is unnecessary for the single-copy goal.

## 2. Goal

The DOM owns **one** `fast_vector<ParamNode>` per automation param, held **in the DOM at a stable
address**. The engine `AutoParam` holds a raw *borrow*-pointer to it and operates on that exact vector
in place. No projection, no per-edit copy. `save` reads the DOM store, which is current by
construction because it is the same memory the engine edited.

**The node stores live in the DOM, never in the engine owners.** This is both the honest form of "the
DOM owns the nodes" and the only shape byte-relocation permits (see §4.2): a `fast_vector`-backed
owner container relocates its elements by `memcpy`, so an engine owner can safely carry only a raw
pointer, never owning storage. Because the storage lives in the DOM and the owner carries only a
pointer, **relocation of any engine owner is a non-issue by construction** — the thing that relocates
owns nothing but a pointer, and the pointed-at DOM store never moves.

**Owner scope is a rollout axis, not an architectural boundary.** The same model applies uniformly to
song-global params, AudioClip / InstrumentClip params (unpatched + patched + patch-cables), and
NoteRow / kit-drum params. They differ only in how much DOM per-owner representation must be built and
wired, and in hardware-test surface — *not* in whether the architecture supports them. Rollout order:
song-global → clips → NoteRows/kit drums. NoteRows come last because their per-owner DOM
representation and lifecycle wiring are the most work, **not** because they would dangle (they cannot
— they own no storage).

## 3. Storage decision (locked)

**A sorted, contiguous `fast_vector<ParamNode>` is the storage. Its type is not changing — only where
it lives: it moves from the engine `AutoParam` into a DOM-owned store.**

`ParamNodeVector` today is `deluge::OrderedPosVector<ParamNode>`, which is *already*
`std::vector<ParamNode, memory::fast_allocator<ParamNode>>` (i.e. `fast_vector<ParamNode>`) kept
sorted by `pos`, plus a thin ordered-helper skin. The container choice was already made and is
optimal for the workload:

- **Access pattern:** small N (a handful to dozens of breakpoints); sorted by `pos`; the hot path is a
  binary search for the playback cursor (`firstAtOrAfter`) plus a contiguous interpolation walk; undo
  is an O(1) whole-vector swap.
- **Why not node-based** (`std::map`/RB-tree, B-tree, skip list): pointer-chasing destroys the
  cache-friendly search/walk, per-node heap allocation is RT-hostile, and the asymptotic edge only
  appears at large N with frequent mid-inserts — not this workload.
- **Why not SoA** (parallel `pos[]`/`value[]`): marginal denser binary search, pointless at 8-byte
  `ParamNode` (a cache line already holds 8), and it complicates edits + serialization.
- **Why not inline/small-buffer** (`etl::vector`, `SmallVector<ParamNode,K>`): `etl::vector` is
  fixed-capacity → hard node cap → risks truncating drawn automation (a behaviour change); a spilling
  small-vector is a bespoke container (the opposite of retiring the wrapper). The `fast_allocator`
  already pools these allocations, capturing most of the RT benefit without a cap or a new type.

**Keep the `fast_allocator` (use `fast_vector<ParamNode>`, not plain `std::vector<ParamNode>`).**
Two payoffs: (1) zero RT-allocation regression — live automation drawing keeps allocating from the
fast pool exactly as today, because the element storage type is unchanged; (2) Phase A becomes almost
purely mechanical — the backing storage does not change, only the call-site spelling.

`DocAutoParam::nodes` (currently plain `std::vector<ParamNode>`) becomes `fast_vector<ParamNode>` so
the DOM and engine share the identical type. Serialization is element-wise, so the allocator is
invisible across the save / serde / C-ABI boundary; portability is unaffected (a Rust port owns its
own allocation regardless).

## 4. Architecture

### 4.1 Ordered helpers as free functions (retire the wrapper for ParamNode only)

`OrderedPosVector` backs **five** types (`NoteVector`, `NoteRowVector`, `ClipInstanceVector`,
`MIDIParamVector`, `ParamNodeVector`). The template is **not** deleted. Only ParamNode's *use* of it
is retired: its ~12 non-native operations (`firstAtOrAfter`, `insertSorted`, `searchDual`,
`shiftHorizontal`, `generateRepeats`, `tryGet`, `tryGetLast`, `find`, `testSequentiality`, and the
`init`/`cloneFrom`/`beenCloned` byte-copy idiom) become free functions over `fast_vector<ParamNode>`,
living in the DOM layer (`src/deluge-project/param_node_ops.{h,cpp}`), consolidated with the existing
`param_edit` kernels. Native operations (`begin`/`end`/`erase`/`insert`/`empty`/`clear`/`swap`/`[]`)
use the `fast_vector` surface directly. This is the architecturally correct line: the portable DOM
owns its node algorithms over a plain vector rather than depending on a firmware container class.

### 4.2 Single copy: the DOM owns the store, the engine borrows a raw pointer

**Why the store must live in the DOM (byte-relocation, precisely).** A `fast_vector`-backed owner
container (e.g. `NoteRowVector = fast_vector<NoteRow>`) relocates its elements by `memcpy`, not
move-construction. So the *only* members an owner can safely carry across relocation are
trivially-copyable ones — raw pointers, ints, PODs. Any owning storage embedded in the owner (an
inline `fast_vector<ParamNode>`, a `unique_ptr`, anything with a destructor or a non-trivial move) is
unsafe: the control block is bit-copied and its identity/ownership breaks (a `unique_ptr` would
double-free; an inline node vector is what dangled in R3). Therefore the node store cannot live in the
owner at all — it lives in the DOM, and the owner carries only a raw *borrow*-pointer.

**The engine indirection** (the `nodes()` accessor already exists as residue):

```cpp
private:
  fast_vector<ParamNode> ownNodes_{};             // fallback backing (un-flipped params)
  fast_vector<ParamNode>* nodes_ = &ownNodes_;    // borrow-pointer into the DOM store when flipped
public:
  fast_vector<ParamNode>& nodes() { return *nodes_; }
  void borrowNodeStorage(fast_vector<ParamNode>* domStore);  // point at the DOM store; move current nodes in
```

Un-flipped params keep `nodes_ == &ownNodes_` → behaviour identical. Flipped params get `nodes_`
pointed at their DOM store. The pointer is trivially-copyable, so it survives owner relocation
untouched; the DOM store it points at never moves. Hot-path cost is unchanged: the engine already
dereferences `nodes_` either way.

**The DOM holds the stores at stable addresses.** The stability requirement moves up to the DOM's own
storage of these stores — and there it is easy to satisfy, because the DOM is *not* constrained to a
byte-relocating container. The DOM holds per-owner param stores via move-aware / stable ownership
(`std::vector<std::unique_ptr<OwnerParamDom>>`, a `deque`, or a slab pool): `unique_ptr` keeps the
pointed-at store object put across the DOM vector's own reallocations, so the engine's raw
borrow-pointer stays valid. Inside each stable DOM store object, `fast_vector<ParamNode>` sits
embedded **by value** — no per-param heap allocation, because the containing DOM object is what is
stable. This model is uniform across all owners; owner relocation, clone, and delete become DOM
store-lifecycle events (clone deep-copies the DOM store and repoints; delete frees it), reusing the
existing clone-idiom hook sites.

### 4.3 Why single-shared-storage dissolves the hard problems

These were artifacts of the two-copy projection model and simply do not arise with one copy:

- **Edit routing** — the engine's existing `setValueForRegion` / `deleteAutomation` /
  `shiftHorizontally`, operating through `nodes()`, mutate the DOM vector in place. The store is
  current by construction; there is no "route 10+ edit paths DOM-first" work and no reprojection.
- **Undo** — `ConsequenceParamChange` already swaps the param's node vector
  (`remotelySwapParamState`). Once `nodes()` *is* the DOM vector, that swap operates on DOM-owned
  data; undo/redo is DOM-correct with **no new consequence type**. `AutoParamState::nodes` changes
  type to `fast_vector<ParamNode>` alongside the accessor migration.
- **Save flip (5c-3)** — `save` reading the DOM store reads the same bytes the engine edited; no
  staleness is possible. The flip is byte-identical and trivially safe.

## 5. File structure

**Create:**
- `src/deluge-project/param_node_ops.{h,cpp}` — free-function ordered helpers over
  `fast_vector<ParamNode>` (consolidated with / adjacent to `param_edit.*`).

**Modify (Phase A — the bulk, ~160 call-sites across 8 files):**
- `src/deluge/modulation/automation/auto_param.{h,cpp}` — `ownNodes_`/`nodes()` become
  `fast_vector<ParamNode>`; all `nodes().foo()` → `foo(nodes())`; `AutoParamState::nodes` retyped.
- `src/deluge/modulation/params/param_node_vector.h` — the `ParamNodeVector` alias is retired from the
  automation path (kept only if any non-automation user remains; expected: none).
- `src/deluge/model/consequence/consequence_param_change.cpp`
- `src/deluge/model/action/action.cpp`
- `src/deluge/model/clip/instrument_clip.cpp`
- `src/deluge/modulation/patch/patch_cable.cpp`
- `src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp`

**Modify (Phases B–D):**
- `src/deluge-project/param_automation.h` — `DocAutoParam::nodes` → `fast_vector<ParamNode>`.
- `src/deluge/modulation/automation/auto_param.{h,cpp}` — `nodes_` borrow-pointer + `borrowNodeStorage`.
- `src/deluge/model/song/song.cpp` — borrow-point song-global unpatched params at their DOM stores after load/setup.
- `src/deluge-project/global_effects.h` / clip owners — per-owner DOM store + clone/steal safety.
- `save` path — flip serialize to read the DOM store (5c-3/5c-4 for these owners).

**Tests:**
- `tests/projection/param_automation_test.cpp` — extend for the free helpers + borrow-pointer (assert engine
  ops land in an external DOM-owned `fast_vector`).
- `tests/golden/song_edit_roundtrip.cpp` — extend to edit → **undo → redo** → save → reload.

## 6. Phasing (each builds; flip stages are hardware-gated)

- **Phase A — retire `ParamNodeVector` → `fast_vector<ParamNode>` + free ordered helpers
  (behaviour-preserving, golden-safe).** The bulk, but mechanical: backing storage type is unchanged
  (still `fast_vector<ParamNode>`), only the wrapper class and call-site spelling change. Many small
  bisectable commits (per-method or per-file), golden `OK=15` at each — the shape of the completed
  std-container migration. No redirection yet; every param uses `ownNodes_`.
- **Phase B — DOM owns the store, engine borrows (song-global).** Add `nodes_` +
  `borrowNodeStorage`; `DocAutoParam::nodes` → `fast_vector<ParamNode>`; establish the DOM's
  stable-address holding of the song-global store (Song is a singleton → already stable). After
  load/setup, point each song-global unpatched `AutoParam`'s `nodes_` at its DOM store and move
  current nodes in. Undo becomes DOM-correct with no code change — the existing swap operates on
  `nodes()`, which is now the DOM store (`AutoParamState::nodes` was already retyped in Phase A).
  Golden `OK=15`. **HARDWARE-GATED (golden-blind):** automate a song-global FX param (record + draw),
  play it back, undo/redo, save → reload.
- **Phase C — clips** (AudioClip + InstrumentClip: unpatched + patched + patch-cables). Build the
  DOM's per-clip param representation and hold it at stable addresses; borrow-point clip `AutoParam`s
  at their DOM stores; wire clone/steal/delete to the DOM store lifecycle (clone deep-copies + repoints;
  delete frees). Golden `OK=15`. **HARDWARE-GATED:** clip automation, clip duplicate, undo across
  clones.
- **Phase D — flip save to read the DOM (5c-3/5c-4 for these owners).** Byte-identical → golden
  `OK=15`.
- **Phase E — NoteRows / kit drums.** Build the DOM's per-NoteRow param representation + lifecycle
  wiring; borrow-point NoteRow `AutoParam`s at their DOM stores. **No architectural change** — the
  store already lives in the DOM and the NoteRow carries only a raw borrow-pointer, so byte-relocation
  is harmless by construction. This phase is last only because it is the most per-owner DOM-buildout
  and hardware-test surface, not because it is unsafe. **HARDWARE-GATED:** kit/drum automation, note-row
  add/remove/relocate (grow the `NoteRowVector`), undo, save → reload. Until wired, NoteRow params keep
  `ownNodes_` (safe, just not yet DOM-owned).

## 7. Testing strategy

- **Golden `OK=15` is the behaviour oracle throughout.** Everything is behaviour-preserving until
  Phase D, which is byte-identical. Any golden change before D is a bug.
- **`param_projection_test`** (dual-arch, x86 SIMDe + ARM/qemu) covers the free helpers and the
  borrow-pointer (engine ops must land in an external DOM-owned `fast_vector`).
- **`song_edit_roundtrip`** (edit-aware harness — the only thing that sees edits) is extended to
  edit → undo → redo → save → reload for a song-global param and a clip param.
- **Hardware gates at B and C** (mandatory; the golden cannot see edits): record/draw/playback,
  undo/redo, save→reload, and (C) clip duplicate.
- Verification recipe per task: `./dbt build Debug` (0 errors) AND
  `cmake --build build-sim --target song_roundtrip song_edit_roundtrip param_projection_test &&
  ./build-sim/param_projection_test && ./build-sim/song_edit_roundtrip &&
  DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" python3 tests/golden/songs_run.py --binary
  ./build-sim/song_roundtrip`.

## 8. Risks / reality check

- **Phase A touches the single most safety-critical automation file** (`auto_param.cpp`, ~136 of the
  ~160 sites). Mitigation: the backing storage type does not change, so there is no
  allocation/layout behaviour to reason about — only call-site spelling; plus small bisectable
  commits and the golden + dual-arch projection test as the net.
- **Store lifecycle must track owner lifecycle.** The DOM store is created/cloned/freed in step with
  its engine owner (clone deep-copies + repoints the borrow-pointer; delete frees). This is the real
  per-owner work in Phases C/E, reusing the existing clone-idiom hook sites. Owner *relocation* is
  safe by construction (the owner carries only a raw pointer); it is *clone/delete* pairing that needs
  care, and it is recovered from R3's proven approach.
- **RT read/edit coordination is unchanged.** The audio thread reads nodes; edits (which may realloc)
  are main-thread — identical to today (both `fast_vector`), so no new RT hazard is introduced.
- **Golden-blind edit behaviour (B/C):** unverifiable in CI; gated on the user's on-device testing.
- **Stop-anywhere:** Phase A (retiring the wrapper) is independently valuable (continues the
  std-container migration) and abandonable for spark convergence; `deluge::project` remains the bridge.

## 9. Out of scope — future direction: sequencer-container modernization

The byte-`memcpy` relocation that forces this design (§4.2) is a broader legacy constraint: it makes
the whole sequencer container family (`Note`, `NoteRow`, `ClipInstance`, `MIDIParam`) trivially-
copyable-only, which is why the `init`/`cloneFrom`/`beenCloned` byte-copy clone idiom exists.
Retiring that — making those idiomatic move-aware RAII types in a move-aware container — is genuinely
valuable long-term (removes a class of manual-memory fragility; aligns with the C++23/26 container
migration). It is **explicitly not part of this effort**, for two reasons:

1. **This design does not need it, and is correct regardless of it.** "DOM owns the nodes, engine
   borrows" is the right architecture even with perfect NoteRow move semantics; byte-relocation merely
   makes it mandatory rather than preferable. A future NoteRow-copy fix would not change or undo
   anything here.
2. **This effort *reduces* the pressure to do it.** The biggest pain from NoteRow byte-relocation was
   the dangling automation storage — which this effort removes by moving nodes to the DOM. Afterward,
   NoteRow's byte-copy constraint is ordinary modernization debt, not a blocker.

So sequencer-container modernization is a separate, later north-star, motivated by general safety and
gated on a real perf evaluation (byte-`memcpy` relocation is fast; move-constructing non-trivial rows,
or a `vector<unique_ptr<NoteRow>>` indirection, has cache/cycle cost on the note hot path). Ordering
is deliberate: do DOM-owns-nodes first (contained, high-value); consider container modernization after.

## 10. Global constraints

- New/modified C++ is idiomatic modern C++23/26 (enum class, `std::span`/`optional`, designated init;
  no C-casts/goto). Behaviour-preserving changes stay faithful but still modern.
- Never add `Co-Authored-By` / "Generated with Claude" trailers to commits.
- Commit freely at natural stopping points (branch `deluge-project-param`, not main).
- clang-format pre-commit hook may reformat and abort the commit (rc=1); re-run `git add -A && git
  commit`.
- Rebuild sim targets before running the golden or it tests stale code.
