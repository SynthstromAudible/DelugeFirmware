# DOM Single-Store — Phase B Implementation Plan (song-global)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make the DOM (`deluge::project`) own the single copy of each song-global unpatched param's
automation node store, with the engine `AutoParam` holding a raw *borrow-pointer* into it — so every edit
and undo operates on DOM-owned data and `save` reads it directly.

**Architecture:** From `docs/superpowers/specs/2026-07-02-dom-authoritative-automation-single-store-design.md`
(Phase B). Phase A (idiomatic `OrderedPosVector<ParamNode>` + real value semantics) is done via the clone-idiom
retirement. Phase B: (1) unify `DocAutoParam::nodes` onto `OrderedPosVector<ParamNode>` so it *is* the runtime
store; (2) add an `AutoParam::nodes_` borrow-pointer; (3) after Song load, point each song-global unpatched
`AutoParam` at its DOM store; (4) flip `save` to read the DOM. Song is a singleton and the song-global unpatched
`DocParamSet.params` vector is sized once at load → the borrowed addresses are stable.

**Tech Stack:** C++23, `deluge::OrderedPosVector<ParamNode>` (= `ParamNodeVector`, `fast_vector`-backed),
`deluge::project` DOM types, golden `OK=15` oracle, `param_automation_test` (dual-arch), `song_edit_roundtrip`.

## Global Constraints

- **Behaviour-preserving until Task B4; B4 is byte-identical.** Golden `OK=15 REGRESS=0` at every task — any
  change before B4 is a bug; B4 must not change bytes either (it reads the same storage the engine edited).
- **Hot path:** node access is per-render. The borrow-pointer adds one indirection the engine *already* does
  (`nodes()` returns a reference either way). No per-node heap allocation is introduced.
- **Stable-address invariant (the safety-critical rule):** once a song-global `AutoParam` borrows
  `&project.global_fx.params.unpatched->params[p].nodes`, that address must never move — i.e. the `unpatched`
  optional must not be reset/reassigned and its `params` vector must not be resized while borrows are live.
  This holds for song-global (built once at load, Song is a heap singleton). Assert it; do not rely on it
  silently.
- clang-format pre-commit hook reformats and aborts (rc=1) — re-run `git add -A && git commit`.
- clangd "cstddef not found" / "std undeclared" errors are environment noise; trust the real builds.

### VERIFY recipe

```bash
./dbt build Debug                                                                 # ARM firmware
cmake --build build-sim --target song_roundtrip song_edit_roundtrip param_automation_test param_clone_test
./build-sim/param_automation_test    # expect: param_automation projection: OK
./build-sim/param_clone_test         # expect: param_clone: OK
DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" python3 tests/golden/songs_run.py \
    --binary ./build-sim/song_roundtrip                                           # expect: OK=15 REGRESS=0
# B3/B4 also:
DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" python3 tests/golden/songs_run.py \
    --binary ./build-sim/song_edit_roundtrip    # (its own pass/fail line; edit persists)
```

---

## Task B1: Unify `DocAutoParam::nodes` onto `OrderedPosVector<ParamNode>`

The DOM node list becomes the same container type as the engine store, so the engine can borrow a pointer to it
and use its existing node API unchanged. This is the bulk of Phase B: the `param_edit` kernels currently
hand-roll ordered ops on a bare `std::vector<ParamNode>` and must move to the `OrderedPosVector` API
(`insertSorted`/`firstAtOrAfter`/`insertAt`/`erase`) — which is a *better* fit (they stop hand-maintaining sort
order). Behaviour-preserving: the `param_automation_test` oracle (dual-arch, ~40 cases) proves each kernel
unchanged.

**Files:**
- Modify: `src/deluge-project/param_automation.h` — `DocAutoParam::nodes` type + the `toDocAutoParam`/
  `fromDocAutoParam` inline builders.
- Modify: `src/deluge-project/param_automation.cpp` — the `AutoParam` overloads if affected.
- Modify: `src/deluge-project/param_edit.{h,cpp}` — adapt all kernels to the `OrderedPosVector` API.
- Modify: `tests/projection/param_automation_test.cpp` — update any `doc.nodes.push_back(...)` / `std::vector`
  assumptions to the `OrderedPosVector` API.

**Interfaces:**
- Produces: `struct DocAutoParam { int32_t current_value{0}; ParamNodeVector nodes; };` where
  `ParamNodeVector = deluge::OrderedPosVector<ParamNode>`.
- Consumes: `OrderedPosVector` ops (`insertSorted(pos, isDefinitelyLast) -> expected<int,Error>`, `[]`, `size`,
  `begin/end`, `erase`, `clear`, `firstAtOrAfter`, `tryGet`), all already used by the engine.

- [ ] **Step 1: Change the member type + include**

In `param_automation.h`, include `modulation/params/param_node_vector.h` (for `ParamNodeVector`) and change:
```cpp
struct DocAutoParam {
	int32_t current_value{0};
	ParamNodeVector nodes; // OrderedPosVector<ParamNode>: the single runtime store the engine borrows (Phase B)
};
```
(`DocParamSet.params` stays `std::vector<DocAutoParam>`; `DocAutoParam` is now non-trivially-copyable but is
still copy/move-constructible via `OrderedPosVector`'s value semantics — DocParamSet copies/moves work.)

- [ ] **Step 2: Fix the projection builders**

`toDocAutoParam(int32_t, const ParamNodeVector&)` currently does `doc.nodes.reserve(...)` + `push_back`.
`OrderedPosVector` has no `reserve`/`push_back`; the source is already sorted ascending, so copy element-wise
as definitely-last inserts (mirroring `fromDocAutoParam`'s existing loop):
```cpp
inline DocAutoParam toDocAutoParam(int32_t current_value, const ParamNodeVector& nodes) {
	DocAutoParam doc;
	doc.current_value = current_value;
	(void)doc.nodes.reserveExtra(static_cast<int32_t>(nodes.size()));
	for (const ParamNode& node : nodes) {
		int32_t i = doc.nodes.insertSorted(node.pos, /*isDefinitelyLast=*/true).value_or(-1);
		if (i < 0) {
			break;
		}
		doc.nodes[i] = node;
	}
	return doc;
}
```
`fromDocAutoParam(const DocAutoParam&, ..., ParamNodeVector& nodes)` already iterates `doc.nodes` element-wise
and inserts — it needs no change beyond `doc.nodes` now being an `OrderedPosVector` (range-for still works).

- [ ] **Step 3: Adapt the `param_edit` kernels**

For each kernel in `param_edit.cpp` operating on `p.nodes` (`setNodeAtPos`, `getValueAtPos`,
`getDistanceToNextNode`, `insertTime`, `shiftHorizontally`, `shiftValues`, `generateRepeats`,
`homogenizeRegion`, `deleteTime`, `moveRegionHorizontally`, `deleteNodesWithinRegion`): replace bare-vector
ordered-maintenance with the `OrderedPosVector` API. Most sites already use `[]`/`size()`/iteration (unchanged);
the ones that INSERT (`push_back`, `insert(begin()+i, …)`, `emplace`) become `insertSorted`/`insertAt`, and any
manual "find insertion point" loop becomes `firstAtOrAfter`/`lower_bound`. Port kernel-by-kernel, running
`param_automation_test` after each so the dual-arch oracle catches any divergence. Update the file-top comment
(`param_edit.h:22`) from "std::vector<ParamNode>" to "OrderedPosVector<ParamNode>".

- [ ] **Step 4: Update the unit test**

In `param_automation_test.cpp`, replace any `doc.nodes.push_back({...})` / direct `std::vector` construction of
DocAutoParam nodes with the `OrderedPosVector` insert helper the test already uses for engine params (the
`setNode`-style `insertSorted` + assign). Keep every assertion identical.

- [ ] **Step 5: VERIFY + commit**

Run VERIFY. Expect `param_automation projection: OK`, `param_clone: OK`, golden `OK=15`, ARM clean.
```bash
git add -A && git commit -m "dom: unify DocAutoParam::nodes onto OrderedPosVector<ParamNode> (single-store prep)"
```

---

## Task B2: `AutoParam` borrow-pointer indirection (no borrow yet)

Add the redirect the engine will use, defaulting to the param's own storage → behaviour identical.

**Files:**
- Modify: `src/deluge/modulation/automation/auto_param.h` / `.cpp`.
- Modify: `tests/projection/param_automation_test.cpp` — add a borrow-into-external-store case.

**Interfaces:**
- Produces:
  ```cpp
  private:
    ParamNodeVector ownNodes_{};              // default backing (un-borrowed params)
    ParamNodeVector* nodes_ = &ownNodes_;      // borrow-pointer into the DOM store when borrowed
  public:
    ParamNodeVector& nodes() { return *nodes_; }
    const ParamNodeVector& nodes() const { return *nodes_; }
    // Point this param at a DOM-owned store and move current nodes into it. The store must outlive this
    // param and never relocate (see the stable-address invariant). Idempotent-safe: only call once per param.
    void borrowNodeStorage(ParamNodeVector* domStore);
  ```

- [ ] **Step 1: Add `nodes_` + retarget `nodes()`**

Replace the two `nodes()` bodies (`return ownNodes_;`) with `return *nodes_;` and add the private `nodes_`
member (initialised to `&ownNodes_`). Rule-of-5 is unaffected — `ownNodes_` still moves/copies with the
`AutoParam`; **`nodes_` is deliberately NOT copied by the copy ctor/assign** (a copy owns its own `ownNodes_`,
so its `nodes_` must point at *its* `ownNodes_`, not the source's store). Audit `auto_param.cpp`'s copy
ctor/assign: they must set `nodes_ = &ownNodes_` (or leave the default) and copy the *contents* via
`ownNodes_(other.nodes())` — read through `other.nodes()` (borrow-aware), write to own storage. Moves keep
`nodes_ = &ownNodes_` on the moved-into object.

- [ ] **Step 2: Implement `borrowNodeStorage`**
```cpp
void AutoParam::borrowNodeStorage(ParamNodeVector* domStore) {
	*domStore = std::move(ownNodes_); // move current nodes into the DOM store (ownNodes_ left empty)
	nodes_ = domStore;                // subsequent nodes() see the DOM store
}
```
Add `#include <utility>` if needed.

- [ ] **Step 3: Extend the unit test (prove the borrow works)**

Add to `param_automation_test.cpp`: construct an `AutoParam`, seed 2 nodes, create an external
`ParamNodeVector ext`, call `p.borrowNodeStorage(&ext)`, assert (a) `p.nodes()` now aliases `ext`
(`&p.nodes() == &ext`), (b) the 2 nodes moved into `ext`, (c) a subsequent engine edit (`setNodeAtPos`-equiv
via the engine op) lands in `ext`, and (d) `swapState` swaps `ext`'s contents with the snapshot.

- [ ] **Step 4: VERIFY + commit**

VERIFY (behaviour-preserving — every param still uses `ownNodes_`; golden `OK=15`).
```bash
git add -A && git commit -m "dom: AutoParam nodes_ borrow-pointer + borrowNodeStorage (no borrow yet)"
```

---

## Task B3: Borrow-point song-global unpatched params at the DOM store

After Song load, make each song-global unpatched `AutoParam` borrow its `DocAutoParam::nodes` in the DOM. Now
engine edits + undo operate on DOM-owned storage. Save still reads the engine (= the DOM store via the borrow),
so bytes are unchanged.

**Files:**
- Modify: `src/deluge/model/song/song.cpp` — the `<songParams>` load tail (~line 1634-1646) and/or the
  post-load setup (`setupPatchingForAllParamManagers`, ~line 1901).
- Modify: `src/deluge-project/param_automation.h` — a small helper to borrow-point a whole `DocParamSet` ↔
  engine `ParamSet` if it reads cleanly.

**Interfaces:**
- Consumes: `borrowNodeStorage` (B2), `song->project.global_fx.params.unpatched` (`std::optional<DocParamSet>`),
  `paramManager.getUnpatchedParamSet()`.

- [ ] **Step 1: Establish the setup sequence**

At the `<songParams>` tail the current order is: seed DOM from engine → read file into DOM →
`fromDocParamManager(DOM → engine)`. Change the node ownership so the DOM store is the single copy:
after `fromDocParamManager` has populated the engine (so `currentValue` + summary flags are set), for each
unpatched param `p` (`0 .. getUnpatchedParamSet()->getNumParams()-1`):
```cpp
DocParamSet& store = *project.global_fx.params.unpatched;
UnpatchedParamSet* eng = paramManager.getUnpatchedParamSet();
for (int32_t p = 0; p < eng->getNumParams(); p++) {
	// The DOM already holds the authoritative nodes (read from file). Point the engine at them; the engine's
	// ownNodes_ (just filled by fromDocParamManager with identical data) is discarded by the move.
	eng->params[p].borrowNodeStorage(&store.params[p].nodes);
}
```
Note: `fromDocParamManager` copied DOM→engine, so `eng->params[p].ownNodes_` == `store.params[p].nodes`
already; `borrowNodeStorage` moves the (identical) engine copy over the DOM store and repoints — net effect is
the engine now views the DOM store, values intact. (Optimisation to skip the redundant copy is deferred; keep
it correct first.)

- [ ] **Step 2: Guard the stable-address invariant**

Add an `ALPHA_OR_BETA` assertion right after the loop that `store.params.size() == eng->getNumParams()` (the
DOM set matches the engine set) and a comment that `project.global_fx.params.unpatched` must not be
reset/`params`-resized while the song is loaded. If any later code path resets it, that is a
dangling-borrow bug — grep `global_fx.params.unpatched =` / `.reset()` and confirm none run post-load.

- [ ] **Step 3: Handle the no-`<songParams>` path**

If a song has no `<songParams>` tag (params all default), the DOM `unpatched` store is still seeded from the
engine at the top of the block — confirm the borrow loop runs on that seeded store too (move the borrow loop so
it runs whether or not the read loop found tags). A song with no automation still gets borrow-pointed (empty
stores), which is correct and keeps the invariant uniform.

- [ ] **Step 4: VERIFY + commit + HARDWARE GATE**

VERIFY: golden `OK=15` (save reads engine = DOM store → identical bytes), `song_edit_roundtrip` still passes,
`param_automation_test`/`param_clone_test` OK, ARM clean. **HARDWARE-GATED (golden-blind):** on-device, automate
a song-global FX param (record + draw nodes), play it back, undo/redo the automation edit, save → reload — the
nodes must land, play, undo, and persist.
```bash
git add -A && git commit -m "dom: borrow-point song-global unpatched params at their DOM node store"
```

---

## Task B4: Flip `save` to read the DOM store (5c-3, song-global)

Make `save` read `project.global_fx.params` explicitly rather than the engine `paramManager`. Byte-identical,
because with the B3 borrow the engine params' `nodes()` *is* the DOM store — this flip just makes the DOM the
named read-authority and removes the engine-read dependency.

**Files:**
- Modify: `src/deluge/model/song/song.cpp` — the `<songParams>` write (~line 1092-1096).
- Possibly modify: `src/deluge-project/serialization.*` — a DOM-reading writer for song-global params if one is
  cleaner than threading the DOM through `GlobalEffectableForClip::writeParam*ToFile`.

- [ ] **Step 1: Route the save through the DOM**

Currently `writeParamAttributesToFile(writer, &paramManager, …)` / `writeParamTagsToFile(writer, &paramManager,
…)` read the engine. Either (a) confirm they read via `getUnpatchedParamSet()->params[p].nodes()` which is the
DOM store (making this a no-op documentation flip), or (b) add a DOM-reading writer
(`writeGlobalEffectableParamsFromDoc(writer, project.global_fx.params.unpatched)`) mirroring the existing
DOM read/write bricks (Phase 4/5a) and call it here. Prefer (b) so the read-authority is explicitly the DOM
(matches 5c-3's intent). The bytes must be identical to (a).

- [ ] **Step 2: VERIFY + commit**

VERIFY: golden `OK=15` **byte-identical** (diff a couple of saved songs pre/post to confirm zero delta) +
`song_edit_roundtrip` passes. This is the safety net the edit-roundtrip was built for.
```bash
git add -A && git commit -m "dom: flip song-global save to read the DOM param store (5c-3, byte-identical)"
```

---

## Hardware Gate (MANDATORY — B3 onward is golden-blind for edits)

The golden round-trips load→save and never edits/undoes. Before Phase C, on-device:
- Automate a song-global FX param (record + draw), play back — nodes sound.
- Undo/redo the automation edit — the swap now operates on DOM storage; must restore correctly.
- Save → reload — automation persists.
- Confirm no regression in live param feel (the borrow adds no per-node cost).

## Risks / reality check

- **Dangling borrow (the one fatal risk):** if `project.global_fx.params.unpatched` is reset or its `params`
  vector resized after B3, every song-global borrow dangles → crash on next node access/save. Song-global is
  built once at load, but B3-Step-2's grep + assertion is the guard. Do not skip it.
- **B1 kernel adaptation** is the largest surface, but fully covered by the dual-arch `param_automation_test`
  oracle — port kernel-by-kernel, test after each. Any oracle divergence is a real behaviour change.
- **Copy-ctor `nodes_` handling (B2-Step-1):** a copied `AutoParam` must point `nodes_` at its *own*
  `ownNodes_`, never the source's store — else clones alias the original's DOM storage. The `param_clone_test`
  independence checks catch this; keep them green.
- **Scope:** this plan is song-global only (Phase B). Clips (Phase C), the save-flip for clips (Phase D), and
  NoteRows/kit drums (Phase E) are separate plans — each needs its own DOM store buildout + hardware gate. Do
  not expand past song-global here.
- **Deferred:** the redundant DOM→engine→DOM copy at B3-Step-1 (correctness first); skipping `fromDocParamManager`'s
  node copy for borrowed params is a later optimisation.
