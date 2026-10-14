# DOM-live-authoritative automation — Phase 0 + Phase 1 kickoff — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unwind the R2/R3 shared-storage redirect (Phase 0), returning automation to a clean engine-owns-nodes baseline, then stand up the DOM edit-op data layer with its first two ops (Phase 1 kickoff), toward the DOM-live-authoritative split.

**Architecture:** `deluge::project` becomes the live authority for automation via *projection* (not shared storage): it owns the nodes (`DocAutoParam`) and the edit ops; the engine `AutoParam` is a projected, engine-owned-by-value copy. Phase 0 removes the redirect/store/sever machinery; Phase 1 begins the DOM edit ops (`setNodeAtPos`, `getValueAtPos`) as pure, dual-arch-tested data operations.

**Tech Stack:** C++23/26 firmware; sim build (`build-sim`, host g++ `-m32`); golden round-trip harness (`tests/golden/songs_run.py`, `song_roundtrip`); projection sim test (`tests/projection/param_automation_test.cpp`, target `param_projection_test`); ARM firmware via `./dbt build Debug`.

## Global Constraints

- New/modified C++ is idiomatic modern C++23/26 (enum class, `std::span`/`optional`, designated init; no C-casts/goto). Behaviour-preserving changes stay faithful but still modern.
- Never add `Co-Authored-By` / "Generated with Claude" trailers to commits.
- Commit freely at the natural stopping points below (branch `deluge-project`, not main).
- **Phase 0 is behaviour-preserving.** The golden is the oracle: it MUST stay `OK=15 REGRESS=0` at every task boundary. Any change is a bug.
- Verification recipe per task: `./dbt build Debug` (0 errors) AND `cmake --build build-sim --target song_roundtrip param_projection_test && ./build-sim/param_projection_test && DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" python3 tests/golden/songs_run.py --binary ./build-sim/song_roundtrip`. MUST rebuild `song_roundtrip` before the golden or it tests stale code.
- clang-format pre-commit hook may reformat and abort the commit (rc=1); re-run `git add -A && git commit` to finish.

## File Structure (Phase 0 unwind surface + Phase 1 additions)

**Phase 0 — remove the redirect/store/sever machinery from:**
- `src/deluge/modulation/automation/auto_param.h` / `.cpp` — the `redirectedNodes_` member, `redirectNodeStorage`, `severNodeStorageRedirect`, the `beenCloned` redirect-sever prologue; **keep** the `nodes()` accessor (simplify to return `ownNodes_`).
- `src/deluge/modulation/params/param_collection.h` — the `severNodeStorageRedirects()` virtual.
- `src/deluge/modulation/params/param_set.h`/`.cpp`, `patch/patch_cable_set.h`/`.cpp`, `midi/midi_param_collection.h`/`.cpp` — the `severNodeStorageRedirects` overrides + the `PatchCableSet::beenCloned` unusable-path sever.
- `src/deluge/modulation/params/param_manager.h`/`.cpp` — `authoritativeParams_` + accessors + `moveFrom` carry; the `stealParamCollectionsFrom` sever loop.
- `src/deluge-project/global_effects.h` — `param_node_store`, `params` (the vestigial captured DocParamManager), and (keep `global_effectable`, `unautomated_param_values`).
- `src/deluge/model/song/song.cpp` — the `readFromFile` redirect block + `toDocParamManager` capture + `setAuthoritativeParams`.
- `src/deluge/model/clip/clip.h`/`.cpp` — `paramNodeStore_`, `redirectParamsToOwnStore`, `paramNodeStoreEntryForTest`.
- `src/deluge/model/clip/audio_clip.cpp` — the `redirectParamsToOwnStore()` call sites.
- `src/deluge/model/clip/instrument_clip.h`/`.cpp` — `patchedNodeStore_`, `cableNodeStore_`, the `redirectParamsToOwnStore` override + call sites + test accessors.
- `tests/projection/automation_edit_test.cpp` + `sim/CMakeLists.txt` — retire the `automation_edit_test` target (its cases test removed redirect behaviour); **keep** the `host_debug.cpp` additions on `song_roundtrip` + `param_projection_test`.

**Phase 1 — create:**
- `src/deluge-project/param_edit.h` / `.cpp` — DOM edit ops on `DocAutoParam` (pure data; timeline context as parameters).
- Extend `tests/projection/param_automation_test.cpp` — dual-arch unit specs for the DOM edit ops.

---

## Phase 0 — Unwind R2/R3

### Task 0.1: Remove the song-global store + capture wiring

**Files:**
- Modify: `src/deluge/model/song/song.cpp` (`readFromFile` tail, ~1905–1916)
- Modify: `src/deluge-project/global_effects.h`

**Interfaces:**
- Produces: `GlobalEffects` with only `{ global_effectable, unautomated_param_values }` (no `params`, no `param_node_store`). `Song::readFromFile` no longer captures a DocParamManager or redirects.

- [ ] **Step 1: Remove the redirect + capture block from `Song::readFromFile`.** Delete the entire block that moves each unpatched param's nodes into `project.global_fx.param_node_store[p]` + `redirectNodeStorage`, the `project.global_fx.params = toDocParamManager(paramManager)` line, and the `paramManager.setAuthoritativeParams(&project.global_fx.params)` line (the ~15 lines before `return Error::NONE;`).

- [ ] **Step 2: Remove the fields from `GlobalEffects`.** In `global_effects.h`, delete the `DocParamManager params;` and `std::array<ParamNodeVector, ...> param_node_store;` members (and the now-unused includes `param_automation.h`/`param_node_vector.h` if nothing else uses them). Keep `global_effectable` and `unautomated_param_values`.

- [ ] **Step 3: Build + golden.**
Run: `./dbt build Debug` → 0 errors. Then `cmake --build build-sim --target song_roundtrip param_projection_test && ./build-sim/param_projection_test && DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" python3 tests/golden/songs_run.py --binary ./build-sim/song_roundtrip`
Expected: `param_automation projection: OK`; golden `OK=15 REGRESS=0`.
(If `param_projection_test` fails to compile because it references `toDocParamManager` capture — it doesn't; it tests projection directly, which stays.)

- [ ] **Step 4: Commit.**
```bash
git add -A && git commit -m "param: unwind song-global redirect store + DocParamManager capture (Phase 0)"
```

### Task 0.2: Remove the clip/instrument stores + call sites

**Files:**
- Modify: `src/deluge/model/clip/clip.h`, `clip.cpp`
- Modify: `src/deluge/model/clip/audio_clip.cpp`
- Modify: `src/deluge/model/clip/instrument_clip.h`, `instrument_clip.cpp`

**Interfaces:**
- Produces: `Clip` and `InstrumentClip` with no `paramNodeStore_`/`patchedNodeStore_`/`cableNodeStore_`, no `redirectParamsToOwnStore`, no `*ForTest` accessors. Clip param managers own their nodes directly (`ownNodes_`).

- [ ] **Step 1: `Clip` base.** In `clip.h`, delete `redirectParamsToOwnStore()` (the `virtual` decl), `paramNodeStoreEntryForTest`, and the `std::array<ParamNodeVector, ...> paramNodeStore_` member + the `<array>`/`param_node_vector.h` includes added for it. In `clip.cpp`, delete the `Clip::redirectParamsToOwnStore()` definition and the `auto_param.h`/`param_set.h` includes added for it (keep any that pre-existed).

- [ ] **Step 2: `AudioClip`.** In `audio_clip.cpp`, delete the two `redirectParamsToOwnStore()` call sites (end of `clone`, tail of `readFromFile`).

- [ ] **Step 3: `InstrumentClip`.** In `instrument_clip.h`, delete the `redirectParamsToOwnStore()` override, `patchedNodeStoreEntryForTest`, `cableNodeStoreEntryForTest`, and the `patchedNodeStore_` + `cableNodeStore_` members. In `instrument_clip.cpp`, delete the `InstrumentClip::redirectParamsToOwnStore()` definition and the two call sites (end of `clone`, tail of `readFromFile`).

- [ ] **Step 4: Revert `Clip::redirectParamsToOwnStore` virtual-ness.** Confirm no remaining callers/overrides reference it (grep `redirectParamsToOwnStore` → 0 hits).

- [ ] **Step 5: Build + golden** (recipe as Task 0.1 Step 3). Expected: `OK=15 REGRESS=0`.

- [ ] **Step 6: Commit.**
```bash
git add -A && git commit -m "param: unwind clip/instrument redirect stores + call sites (Phase 0)"
```

### Task 0.3: Remove the sever machinery (clone + steal safety)

**Files:**
- Modify: `src/deluge/modulation/params/param_collection.h`
- Modify: `src/deluge/modulation/params/param_set.h`, `param_set.cpp`
- Modify: `src/deluge/modulation/patch/patch_cable_set.h`, `patch_cable_set.cpp`
- Modify: `src/deluge/modulation/midi/midi_param_collection.h`, `midi_param_collection.cpp`
- Modify: `src/deluge/modulation/params/param_manager.cpp`

**Interfaces:**
- Produces: no `severNodeStorageRedirects` anywhere; `stealParamCollectionsFrom` has no sever loop; `PatchCableSet::beenCloned` unusable path is back to plain `param.init()`.

- [ ] **Step 1: base virtual.** In `param_collection.h`, delete the `virtual void severNodeStorageRedirects() {}` declaration + its doc comment.

- [ ] **Step 2: overrides.** In `param_set.h`/`.cpp`, `patch_cable_set.h`/`.cpp`, `midi_param_collection.h`/`.cpp`: delete the `severNodeStorageRedirects()` decl + definition in each.

- [ ] **Step 3: PatchCableSet::beenCloned unusable path.** In `patch_cable_set.cpp`, in `beenCloned`, revert the unusable-cable loop to just `patchCables[c].param.init();` (delete the `severNodeStorageRedirect()` line + its comment).

- [ ] **Step 4: steal sever.** In `param_manager.cpp`, in `stealParamCollectionsFrom`, delete the trailing `for (j < stopAtOther) { ... severNodeStorageRedirects(); }` loop + its comment.

- [ ] **Step 5: Build + golden.** Expected: `OK=15 REGRESS=0`.

- [ ] **Step 6: Commit.**
```bash
git add -A && git commit -m "param: unwind clone/steal node-storage sever machinery (Phase 0)"
```

### Task 0.4: Remove the redirect member + authoritativeParams; simplify nodes()

**Files:**
- Modify: `src/deluge/modulation/automation/auto_param.h`, `auto_param.cpp`
- Modify: `src/deluge/modulation/params/param_manager.h`, `param_manager.cpp`

**Interfaces:**
- Produces: `AutoParam` with only `ownNodes_` (renamed back to a plain `nodes_` member is optional; keep `ownNodes_` + accessor to minimise churn). `nodes()` returns `ownNodes_` unconditionally. No `redirectNodeStorage`, no `severNodeStorageRedirect`, no `redirectedNodes_`. `ParamManager` has no `authoritativeParams_`.

- [ ] **Step 1: `AutoParam` header.** In `auto_param.h`: delete `redirectedNodes_`, `redirectNodeStorage`, `severNodeStorageRedirect`; simplify both `nodes()` overloads to `return ownNodes_;`. Keep the `ownNodes_` member and the `nodes()` accessor (the clean R1 seam the projection uses).

- [ ] **Step 2: `AutoParam` impl.** In `auto_param.cpp`: delete the `severNodeStorageRedirect()` definition and the redirect-sever prologue at the top of `beenCloned` (the `if (redirectedNodes_ != nullptr) { memcpy(&ownNodes_, ...); redirectedNodes_ = nullptr; }` block). `beenCloned` returns to operating on `ownNodes_` directly.

- [ ] **Step 3: `ParamManager`.** In `param_manager.h`: delete `authoritativeParams_`, `authoritativeParams()`, `setAuthoritativeParams()`, the `deluge::project::DocParamManager` forward-decl, and the `authoritativeParams_` carry in `moveFrom`. In `param_manager.cpp`: delete any `authoritativeParams_` init.

- [ ] **Step 4: value-commit path.** Remove `commitUnpatchedValueThroughDom` from `src/deluge-project/param_automation.{h,cpp}` and its call site in `auto_param.cpp` (`setCurrentValueInResponseToUserInput` `getOut:`), reverting that label to `currentValue = value;` directly. Remove the now-unused includes in `auto_param.cpp` (`deluge-project/param_automation.h`, `param_manager.h`, `param_set.h`) if nothing else there needs them.

- [ ] **Step 5: Build + golden.** Expected: `OK=15 REGRESS=0`.

- [ ] **Step 6: Commit.**
```bash
git add -A && git commit -m "param: remove AutoParam redirect member + authoritativeParams value-commit (Phase 0)"
```

### Task 0.5: Retire the redirect sim harness; keep the link fix

**Files:**
- Delete: `tests/projection/automation_edit_test.cpp`
- Modify: `sim/CMakeLists.txt`

**Interfaces:**
- Produces: no `automation_edit_test` target; `song_roundtrip` + `param_projection_test` keep their `host_debug.cpp` source + `src/deluge` include (the independent link fix from `b56e5979b`).

- [ ] **Step 1: Remove the target.** In `sim/CMakeLists.txt`, delete the `add_executable(automation_edit_test ...)` block, its `target_include_directories`/`target_link_libraries`, and the `add_test(NAME automation_edit_test ...)`. Leave the `host_debug.cpp` additions on `song_roundtrip` and `param_projection_test` intact.

- [ ] **Step 2: Delete the test file.** `git rm tests/projection/automation_edit_test.cpp`.

- [ ] **Step 3: Reconfigure + build + golden.**
Run: `cmake -S sim -B build-sim && cmake --build build-sim --target song_roundtrip param_projection_test && ./build-sim/param_projection_test && DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" python3 tests/golden/songs_run.py --binary ./build-sim/song_roundtrip`
Expected: configures clean; `param_automation projection: OK`; golden `OK=15 REGRESS=0`.

- [ ] **Step 4: Full ARM firmware build.** `./dbt build Debug` → 0 errors (deluge.elf links).

- [ ] **Step 5: Commit.**
```bash
git add -A && git commit -m "test: retire redirect sim harness; Phase 0 unwind complete (baseline restored)"
```

**Phase 0 done:** engine owns nodes by value again, save projects engine→DOM (5a), load projects DOM→engine (5b); no redirect/store/sever/handle machinery; NoteRow blocker gone; golden byte-identical throughout.

---

## Phase 1 — DOM edit ops (kickoff: infrastructure + first two ops)

Phase 1 ports the timeline-aware edit ops onto `DocAutoParam` in `deluge::project`, as pure data operations that take timeline context as parameters. This kickoff establishes the module + test harness and lands the two most fundamental ops (`setNodeAtPos` = draw; `getValueAtPos` = playback-read). The remaining ~18 ops follow the same pattern in a **follow-up plan**, ported one at a time against the engine as a reference oracle.

### Task 1.1: DOM edit-op module scaffold + first op `setNodeAtPos`

**Files:**
- Create: `src/deluge-project/param_edit.h`, `src/deluge-project/param_edit.cpp`
- Modify: `tests/projection/param_automation_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace deluge::project {
  // Ensure DocAutoParam has a node at `pos` with `value`; create/modify as needed; keep `nodes`
  // sorted by pos. `interpolate` sets ParamNode::interpolated on the touched node. Returns the index
  // of the touched node. Faithful port of AutoParam::setNodeAtPos (auto_param.cpp).
  int32_t setNodeAtPos(DocAutoParam& p, int32_t pos, int32_t value, bool interpolate);
  }
  ```

- [ ] **Step 1: Write the failing test.** Add to `param_automation_test.cpp`'s `main()`:
```cpp
// Phase 1: DOM edit op setNodeAtPos operates directly on DocAutoParam (no engine).
{
  using deluge::project::DocAutoParam;
  DocAutoParam p{};                       // current_value 0, empty nodes
  int32_t i0 = deluge::project::setNodeAtPos(p, 48, 100, true);
  check(p.nodes.size() == 1 && i0 == 0, "setNodeAtPos: first insert");
  check(p.nodes[0].pos == 48 && p.nodes[0].value == 100 && p.nodes[0].interpolated, "setNodeAtPos: fields");
  int32_t i1 = deluge::project::setNodeAtPos(p, 0, -50, false);   // inserts BEFORE (sorted)
  check(p.nodes.size() == 2 && i1 == 0 && p.nodes[0].pos == 0, "setNodeAtPos: sorted insert at front");
  int32_t i2 = deluge::project::setNodeAtPos(p, 48, 7, false);    // modifies existing node at 48
  check(p.nodes.size() == 2 && p.nodes[1].pos == 48 && p.nodes[1].value == 7 && !p.nodes[1].interpolated,
        "setNodeAtPos: modify existing");
}
```

- [ ] **Step 2: Add the include** at the top of `param_automation_test.cpp`: `#include "deluge-project/param_edit.h"`.

- [ ] **Step 3: Run to verify it fails.**
Run: `cmake --build build-sim --target param_projection_test 2>&1 | tail`
Expected: FAIL — `param_edit.h: No such file` / `setNodeAtPos not declared`.

- [ ] **Step 4: Read the engine reference.** Open `src/deluge/modulation/automation/auto_param.cpp` and read `AutoParam::setNodeAtPos(int32_t pos, int32_t value, bool shouldInterpolate)` in full — it does a sorted `firstAtOrAfter`/`insertSorted` on `nodes()` then sets the node's fields. Note it returns the node index.

- [ ] **Step 5: Write the port.** Create `param_edit.h`:
```cpp
#pragma once
#include "deluge-project/param_automation.h" // DocAutoParam / ParamNode
#include <cstdint>
namespace deluge::project {
int32_t setNodeAtPos(DocAutoParam& p, int32_t pos, int32_t value, bool interpolate);
} // namespace deluge::project
```
Create `param_edit.cpp` — port faithfully to `std::vector<ParamNode>` (sorted-by-pos):
```cpp
#include "deluge-project/param_edit.h"
#include <algorithm>
namespace deluge::project {
int32_t setNodeAtPos(DocAutoParam& p, int32_t pos, int32_t value, bool interpolate) {
  auto& nodes = p.nodes;
  auto it = std::lower_bound(nodes.begin(), nodes.end(), pos,
                             [](const ParamNode& n, int32_t key) { return n.pos < key; });
  int32_t i = static_cast<int32_t>(it - nodes.begin());
  if (it == nodes.end() || it->pos != pos) {
    it = nodes.insert(it, ParamNode{});
  }
  it->pos = pos;
  it->value = value;
  it->interpolated = interpolate;
  return i;
}
} // namespace deluge::project
```
(Confirm `ParamNode`'s exact member names/order in `modulation/params/param_node.h`; use them.)

- [ ] **Step 6: Wire `param_edit.cpp` into the deluge-project library.** Confirm `src/deluge-project/CMakeLists.txt` (or the GLOB it uses) picks up the new `.cpp`; if it's an explicit list, add it.

- [ ] **Step 7: Run to verify it passes.**
Run: `cmake --build build-sim --target param_projection_test && ./build-sim/param_projection_test`
Expected: `param_automation projection: OK` (exit 0).

- [ ] **Step 8: ARM firmware build.** `./dbt build Debug` → 0 errors.

- [ ] **Step 9: Commit.**
```bash
git add -A && git commit -m "param: DOM edit-op module + setNodeAtPos on DocAutoParam (Phase 1)"
```

### Task 1.2: `getValueAtPos` (interpolation query) on DocAutoParam

**Files:**
- Modify: `src/deluge-project/param_edit.h`, `param_edit.cpp`
- Modify: `tests/projection/param_automation_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace deluge::project {
  // Value at `pos` interpolating between nodes, matching AutoParam::getValueAtPos. Timeline context
  // (loop length) is a parameter, not an engine global. Returns current_value if no nodes.
  int32_t getValueAtPos(const DocAutoParam& p, uint32_t pos, int32_t loopLength, bool reversed = false);
  }
  ```

- [ ] **Step 1: Write the failing test.** Add to `param_automation_test.cpp`:
```cpp
// Phase 1: getValueAtPos interpolates through DocAutoParam nodes.
{
  using deluge::project::DocAutoParam;
  DocAutoParam p{};
  deluge::project::setNodeAtPos(p, 0, 100, true);
  deluge::project::setNodeAtPos(p, 48, -200, false);
  check(deluge::project::getValueAtPos(p, 0, 96) == 100, "getValueAtPos: at node 0");
  check(deluge::project::getValueAtPos(p, 48, 96) == -200, "getValueAtPos: at node 48");
  int32_t mid = deluge::project::getValueAtPos(p, 24, 96);
  check(mid < 100 && mid > -200, "getValueAtPos: interpolates between");
  DocAutoParam empty{}; empty.current_value = 777;
  check(deluge::project::getValueAtPos(empty, 10, 96) == 777, "getValueAtPos: empty -> current_value");
}
```

- [ ] **Step 2: Run to verify it fails.**
Run: `cmake --build build-sim --target param_projection_test 2>&1 | tail`
Expected: FAIL — `getValueAtPos not declared`.

- [ ] **Step 3: Read the engine reference.** Read `AutoParam::getValueAtPos` in `auto_param.cpp` in full (the `firstAtOrAfter(pos + !reversed)`, left/right node selection, `interpolated`-gated linear interpolation, the infinite-length branch). Note it uses `modelStack->getLoopLength()` — that becomes the `loopLength` parameter.

- [ ] **Step 4: Write the port.** Add the faithful translation to `param_edit.{h,cpp}`, replacing `nodes()`→`p.nodes`, `currentValue`→`p.current_value`, `modelStack->getLoopLength()`→`loopLength`. Preserve the interpolation math exactly (fixed-point). Keep the `reversed` handling.

- [ ] **Step 5: Run to verify it passes.**
Run: `cmake --build build-sim --target param_projection_test && ./build-sim/param_projection_test`
Expected: `OK` (exit 0).

- [ ] **Step 6: Oracle diff (fidelity guard).** Add a randomized cross-check to the test: build a live `AutoParam` and a `DocAutoParam` with the same random nodes, and assert `AutoParam::getValueAtPos(pos, msWithLoopLen)` == `deluge::project::getValueAtPos(doc, pos, loopLen)` for a spread of positions. (Reuse the model-stack construction pattern; loop length via a stub.) This pins the port to the engine semantics.

- [ ] **Step 7: ARM firmware build.** `./dbt build Debug` → 0 errors.

- [ ] **Step 8: Commit.**
```bash
git add -A && git commit -m "param: getValueAtPos interpolation on DocAutoParam + engine oracle diff (Phase 1)"
```

**Phase 1 kickoff done:** the DOM edit-op module exists with the draw (`setNodeAtPos`) and playback-read (`getValueAtPos`) ops, dual-arch tested and pinned to the engine oracle. The remaining ops (`setValueForRegion`, `homogenizeRegion`, `deleteTime`/`insertTime`, `stealNodes`/`insertStolenNodes`, `moveRegionHorizontally`, `generateRepeats`, `shiftHorizontally`, `deleteNodesWithinRegion`, `getDistanceToNextNode`, the undo-snapshot sink, …) and Phase 2 (song-global authority wiring: route edits → DOM ops → reproject → save-reads-DOM) are a follow-up plan, each op ported the same way (read engine → port → test → oracle-diff).

---

## Self-review notes
- **Spec coverage:** Phase 0 (unwind) and Phase 1 (DOM edit ops kickoff) map to the spec §3–4. Phases 2–5 are explicitly deferred to follow-up plans per the spec's decomposition. Phase 3–4's project-model dependency is not in scope here.
- **Type consistency:** `DocAutoParam{ current_value, nodes }` and `ParamNode{ value, pos, interpolated }` are the existing types (`param_automation.h` / `param_node.h`) — confirm member names at Task 1.1 Step 5 before finalizing the port.
- **No behaviour change in Phase 0:** guaranteed by the golden `OK=15` gate at every task.
