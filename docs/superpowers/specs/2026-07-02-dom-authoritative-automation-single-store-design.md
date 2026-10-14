# DOM-authoritative automation via a single shared node store — Design Spec

**Date:** 2026-07-02
**Status:** Approved — **RESUMING at Phase B.** The prerequisite standalone effort,
`2026-07-02-sequencer-clone-idiom-retirement-design.md` (retiring the byte-copy clone idiom from the
sequencer, keeping `OrderedPosVector<ParamNode>` as the store), is **done** (branch
`deluge-project-param`: full value semantics, `NoteRow` copyable, `cloneFrom` eliminated, clone path
sim-covered end-to-end via `clip_clone_roundtrip`). That effort **was Phase A** as locked in §3/§4.1 —
so Phase A is complete. **§5 and §6-Phase A below are superseded** by how it actually landed: no
`param_node_ops` free functions and no `fast_vector<ParamNode>` retirement — `OrderedPosVector<ParamNode>`
was kept (per §3/§4.1), so read those two sections as historical. §4.2 has been corrected for the
post-clone reality (owners now relocate by **move**, not `memcpy` — the conclusion is unchanged and in
fact strengthened). The DOM store work (single-copy unification, DOM ownership, engine borrow pointers —
Phases B+) resumes now.
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

**The storage stays `deluge::OrderedPosVector<ParamNode>` — the existing sorted, contiguous
`fast_vector<ParamNode>` container. No new type, no facade, no alias.** What changes is *where it
lives* (it moves into the DOM; the engine borrows a pointer — §4.2) and *how idiomatic it is* (Phase A
polishes the container itself — a shared win for all five users).

`OrderedPosVector<ParamNode>` is `std::vector<ParamNode, memory::fast_allocator<ParamNode>>`
(`fast_vector<ParamNode>`) kept sorted by `pos`, plus a thin ordered-op skin. That container choice is
optimal and is not changing. Why a sorted contiguous vector is right:

- **Access pattern:** small N (a handful to dozens of breakpoints); sorted by `pos`; the hot path is a
  binary search for the playback cursor (`firstAtOrAfter`) plus a contiguous interpolation walk; undo
  is an O(1) whole-vector swap.
- **Why not node-based** (`std::map`/RB-tree, B-tree, skip list): pointer-chasing destroys the
  cache-friendly search/walk, per-node heap allocation is RT-hostile, and the asymptotic edge only
  appears at large N with frequent mid-inserts — not this workload.
- **Why not SoA** (parallel `pos[]`/`value[]`): marginal denser binary search, pointless at 8-byte
  `ParamNode` (a cache line already holds 8), and it complicates edits + serialization.
- **Why not inline/small-buffer** (`etl::vector`, `SmallVector<ParamNode,K>`): `etl::vector` is
  fixed-capacity → hard node cap → risks truncating drawn automation (a behaviour change); such a
  container is also not trivially copyable in a way that would let it live inside a byte-relocated
  owner. The `fast_allocator` already pools these allocations, capturing most of the RT benefit
  without a cap.

**Keep the `fast_allocator` (`fast_vector<ParamNode>`, not plain `std::vector<ParamNode>`).** Zero
RT-allocation regression — live automation drawing keeps allocating from the fast pool exactly as
today. `OrderedPosVector` adds no data members beyond the one vector, so it is layout-trivial and
serializes element-wise exactly as a bare vector would.

**Why not a bare vector + free functions, and why not a purpose-built facade.** Two options were
considered and rejected. A *bare `std::vector` + free functions* would be motivated only by the
C-ABI/Rust-POD boundary — explicitly out of scope here — and it leaves the sorted-`pos` invariant
exposed. A *purpose-built `AutomationNodes` facade* is not justified either: enumerating the automation
operations shows the generic ordered ops (`firstAtOrAfter`, `insertSorted`, binary search, erase) are
**not** automation-specific — notes/MIDI/clips use the identical `OrderedPosVector` logic — so a facade
would either duplicate them or thinly forward to them; and the genuinely automation-specific ops
(`getValueAtPos`, `homogenizeRegion`, `deleteTime`, …) almost all take **timeline context**
(`loopLength`, `effectiveLength`, `reversed`, cut position), so they belong as free functions taking
`(nodes, context)` — which is exactly what `param_edit.*` already is — not as methods that would drag
playback concerns into a storage type. So: keep `OrderedPosVector<ParamNode>` as the store, keep
`param_edit` as free functions, and make the container itself idiomatic.

## 4. Architecture

### 4.1 The node container stays `OrderedPosVector<ParamNode>`, polished to idiomatic C++23

The automation node store remains `deluge::OrderedPosVector<ParamNode>` — the shared sequencer
container (also backing `NoteVector`, `NoteRowVector`, `ClipInstanceVector`, `MIDIParamVector`). It is
already substantially idiomatic (`std::ranges`, `std::expected`, `[[nodiscard]]`, `if constexpr`; no
C-casts). Phase A improves the container in place — a **shared win** for all five users — in two parts:

1. **Light idiomatic pass** (additive, low-risk): a `requires`-constraint on the `keyMember` template
   parameter, `std::span`/range accessors, `[[gnu::restrict]]` in place of `__restrict__`, and
   `[[nodiscard]]`/doc completeness.
2. **Retire the byte-copy clone idiom** (`init`/`cloneFrom`/`beenCloned`) into proper C++ copy/clone
   semantics. This is the one genuinely non-idiomatic wart (a placement-new-over-aliased-buffer +
   per-element `memcpy` dance that exists because containing objects are `memcpy`'d during clone/undo).
   Retiring it means enabling a real copy constructor on `OrderedPosVector` and converting **every**
   byte-copy caller across all users — `note_row`, `midi_param_collection`, `auto_param`,
   `patch_cable_set`, and the two undo consequences (`consequence_param_change`,
   `consequence_note_array_change`), plus the `ParamManager` backup/undo path. This overlaps the
   sequencer-relocation modernization once noted as future work (§9); it is pulled into Phase A by
   decision. It touches the **undo critical path** and is **hardware-gated**.

`DocAutoParam::nodes` (currently plain `std::vector<ParamNode>`) is unified onto the same
`OrderedPosVector<ParamNode>` store in Phase B, when the DOM owns the single copy; the `param_edit`
kernels are adapted to it then (gaining `insertSorted`/`firstAtOrAfter` instead of hand-rolling on a
bare vector). Serialization stays element-wise and unaffected.

### 4.2 Single copy: the DOM owns the store, the engine borrows a raw pointer

**Why the store must live in the DOM (relocation, precisely).** When a `fast_vector`-backed owner
container (e.g. `NoteRowVector`) grows, it relocates its elements to a new address — by
**move-construction** now that `NoteRow` is movable (post-clone-work), or by `memcpy` for
trivially-copyable owners. Either way, the *address* of anything embedded in the owner changes, so a raw
borrow-pointer into an owner-embedded store dangles: move-construction transfers the store's heap buffer
but the store *object's* address (the `fast_vector` control block the pointer targets) still moves, and
the moved-from original is destroyed when the old vector storage is freed. This is exactly what dangled
in R3. (The correction over the pre-clone draft: relocation is a *move*, not a `memcpy` — but the
conclusion is unchanged and in fact strengthened, since it now holds for both relocation modes.)
Therefore the node store cannot live in the owner at all — it lives in the DOM, and the owner (via the
engine `AutoParam`) carries only a raw *borrow*-pointer, which is trivially relocatable and survives
untouched.

**The engine indirection** (the `nodes()` accessor already exists as residue):

```cpp
private:
  OrderedPosVector<ParamNode> ownNodes_{};             // fallback backing (un-flipped params)
  OrderedPosVector<ParamNode>* nodes_ = &ownNodes_;    // borrow-pointer into the DOM store when flipped
public:
  OrderedPosVector<ParamNode>& nodes() { return *nodes_; }
  void borrowNodeStorage(OrderedPosVector<ParamNode>* domStore); // point at the DOM store; move current nodes in
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

**Create:** *(none — superseded.)* The earlier draft created `param_node_ops.{h,cpp}` (free-function
ordered helpers over a bare `fast_vector<ParamNode>`); §3/§4.1 rejected that in favour of keeping
`OrderedPosVector<ParamNode>` with its ordered-op methods, so no such file is created.

**Modify (Phase A) — DONE** via the clone-idiom retirement (see that effort's memory/commits). The draft
below is historical; as built, `OrderedPosVector<ParamNode>` was kept (not retired to `fast_vector` +
free helpers), so `nodes().foo()` call sites were **not** rewritten to `foo(nodes())`, and
`AutoParamState::nodes` stays `ParamNodeVector` (an `OrderedPosVector<ParamNode>` alias). The value
semantics Phase B relies on (real copy/move, no byte-copy idiom) are in place. *(Original list, historical:
`auto_param.{h,cpp}`, `param_node_vector.h`, `consequence_param_change.cpp`, `action.cpp`,
`instrument_clip.cpp`, `patch_cable.cpp`, `mod_controllable.cpp`.)*

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

- **Phase A — idiomatic node container + real value semantics. DONE** (as the clone-idiom retirement,
  not as the drafted `fast_vector` + free-helpers rewrite). `OrderedPosVector<ParamNode>` was kept and
  made idiomatic; the byte-copy clone idiom (`init`/`cloneFrom`/`beenCloned`) is fully retired into
  copy/move construction + assignment across all five users and the undo consequences; golden `OK=15`
  throughout and the clip-clone path is sim-covered (`clip_clone_roundtrip`, 38-song sweep). `AutoParam`
  still holds its store as an inline `ParamNodeVector ownNodes_` accessed via `nodes()` — there is **no**
  `nodes_` borrow-pointer yet (that is the first step of Phase B).
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
