# DOM-authoritative automation via a single shared node store — Design Spec

**Date:** 2026-07-02
**Status:** Approved (brainstorm), pending implementation plan
**Supersedes the storage strategy of:** `2026-07-02-dom-live-authoritative-automation-design.md`
(that spec chose *projection* — two copies kept in sync; this spec replaces it with a *single
shared store*, which is strictly cleaner for the address-stable owners in scope).

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

This spec returns to **single shared storage** — but scoped to the address-stable owners where it is
provably safe (song-global + clips), with NoteRows explicitly deferred. Projection was solving a
problem (owner relocation) that is out of scope; for the in-scope owners, a single copy is the
stronger and simpler design.

## 2. Goal

The DOM owns **one** `fast_vector<ParamNode>` per automation param. The engine `AutoParam` operates on
that exact vector in place. No projection, no per-edit copy. `save` reads the DOM store, which is
current by construction because it is the same memory the engine edited.

**Scope:** song-global unpatched params + AudioClip / InstrumentClip params (unpatched + patched +
patch-cables). All three are address-stable owners (a `Song` singleton; heap-allocated `Clip`s that
never move). **NoteRows / kit drums are deferred** to a future on-demand stable-allocation effort —
their owner byte-relocates, which dangles any handle into their node storage.

## 3. Storage decision (locked)

**A sorted, contiguous `fast_vector<ParamNode>` is the storage. It is not changing — only its owner
is.**

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

### 4.2 Single copy: DOM owns the vector, engine references it

`AutoParam` regains a redirect indirection (the `nodes()` accessor already exists as residue):

```cpp
private:
  fast_vector<ParamNode> ownNodes_{};             // fallback backing (un-flipped / legacy params)
  fast_vector<ParamNode>* nodes_ = &ownNodes_;    // redirectable to DOM-owned storage
public:
  fast_vector<ParamNode>& nodes() { return *nodes_; }
  void redirectNodeStorage(fast_vector<ParamNode>* store);  // point at the DOM vector; move current nodes in
```

Un-flipped params keep `nodes_ == &ownNodes_` → behaviour identical. Flipped (in-scope) params get
`nodes_` pointed at their `DocAutoParam::nodes` after load/setup. Because clips are heap-stable and
Song is a singleton, the pointer never dangles for the in-scope owners. (NoteRows would dangle on
relocation — hence deferred.)

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
- `src/deluge/modulation/automation/auto_param.{h,cpp}` — `nodes_` redirect + `redirectNodeStorage`.
- `src/deluge/model/song/song.cpp` — redirect song-global unpatched params after load/setup.
- `src/deluge-project/global_effects.h` / clip owners — per-owner DOM store + clone/steal safety.
- `save` path — flip serialize to read the DOM store (5c-3/5c-4 for these owners).

**Tests:**
- `tests/projection/param_automation_test.cpp` — extend for the free helpers + redirect (assert engine
  ops land in an external DOM-owned `fast_vector`).
- `tests/golden/song_edit_roundtrip.cpp` — extend to edit → **undo → redo** → save → reload.

## 6. Phasing (each builds; flip stages are hardware-gated)

- **Phase A — retire `ParamNodeVector` → `fast_vector<ParamNode>` + free ordered helpers
  (behaviour-preserving, golden-safe).** The bulk, but mechanical: backing storage type is unchanged
  (still `fast_vector<ParamNode>`), only the wrapper class and call-site spelling change. Many small
  bisectable commits (per-method or per-file), golden `OK=15` at each — the shape of the completed
  std-container migration. No redirection yet; every param uses `ownNodes_`.
- **Phase B — DOM owns the vector, engine references it (song-global).** Add `nodes_` +
  `redirectNodeStorage`; `DocAutoParam::nodes` → `fast_vector<ParamNode>`; after load/setup, redirect
  each song-global unpatched `AutoParam` at its `DocAutoParam::nodes` and move current nodes in.
  Undo becomes DOM-correct with no code change — the existing swap operates on `nodes()`, which is now
  the DOM vector (`AutoParamState::nodes` was already retyped in Phase A). Golden `OK=15`.
  **HARDWARE-GATED (golden-blind):** automate a song-global FX param (record + draw), play it back,
  undo/redo, save → reload.
- **Phase C — extend redirect to clips** (AudioClip + InstrumentClip: unpatched + patched +
  patch-cables). Per-clip DOM store + clone/steal safety (recover R3's solved approach — clips are
  heap-stable → no dangle). Golden `OK=15`. **HARDWARE-GATED:** clip automation, clip duplicate, undo
  across clones.
- **Phase D — flip save to read the DOM (5c-3/5c-4 for these owners).** Byte-identical → golden
  `OK=15`.
- **Deferred:** NoteRows / kit drums — redirect pointer dangles on owner byte-relocation; needs the
  on-demand stable-allocation effort. Until then they keep `ownNodes_` (safe, just not DOM-owned).

## 7. Testing strategy

- **Golden `OK=15` is the behaviour oracle throughout.** Everything is behaviour-preserving until
  Phase D, which is byte-identical. Any golden change before D is a bug.
- **`param_projection_test`** (dual-arch, x86 SIMDe + ARM/qemu) covers the free helpers and the
  redirect (engine ops must land in an external DOM-owned `fast_vector`).
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
- **Redirect reintroduces owner-lifetime coupling.** Bounded to address-stable owners by construction;
  NoteRows explicitly walled off. Clone/steal safety for clips is recovered from R3's proven approach.
- **RT read/edit coordination is unchanged.** The audio thread reads nodes; edits (which may realloc)
  are main-thread — identical to today (both `fast_vector`), so no new RT hazard is introduced.
- **Golden-blind edit behaviour (B/C):** unverifiable in CI; gated on the user's on-device testing.
- **Stop-anywhere:** Phase A (retiring the wrapper) is independently valuable (continues the
  std-container migration) and abandonable for spark convergence; `deluge::project` remains the bridge.

## 9. Global constraints

- New/modified C++ is idiomatic modern C++23/26 (enum class, `std::span`/`optional`, designated init;
  no C-casts/goto). Behaviour-preserving changes stay faithful but still modern.
- Never add `Co-Authored-By` / "Generated with Claude" trailers to commits.
- Commit freely at natural stopping points (branch `deluge-project-param`, not main).
- clang-format pre-commit hook may reformat and abort the commit (rc=1); re-run `git add -A && git
  commit`.
- Rebuild sim targets before running the golden or it tests stale code.
