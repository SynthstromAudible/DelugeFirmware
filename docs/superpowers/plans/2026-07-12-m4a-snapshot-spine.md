# M4a — Snapshot spine + event-feed proof (param pilot) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the event→snapshot-patch loop for params end-to-end: a greenfield engine-side `ParamSnapshot` + patch protocol keyed by `ParamAddress`, driven by `SetParamValue::emitEvent`, asserted lane-for-lane equivalent to the current in-line poke (behind an A/B flag so goldens stay bit-identical), plus a host-sim cost microbench that informs the north-star's incremental-vs-rebuild granularity question.

**Architecture:** Behavior-preserving + additive. The model (`DocAutoParam` in `deluge-project`, the authoritative `{current_value, nodes}` store) already exists; the engine `AutoParam` holds a derived `currentValue` cursor projected from it. Today `SetParamValue::apply` pokes the engine in-line (`setValuePossiblyForRegion`) and `emitEvent` only observes. This milestone builds the model-first event-patch alongside, proves equivalence, and measures — it does NOT remove the in-line poke (that commit-flip is deferred).

**Tech Stack:** C++23; host-sim projection tests (`tests/projection/*.cpp`, real `ParamManager` + `ActionLogger`, x86 `build-sim` + ARM `build-sim-arm` via qemu); sim targets declared in `sim/CMakeLists.txt`; firmware `./dbt build Debug`.

## Global Constraints

- **Behavior-preserving.** The in-line poke stays the shipped default (A/B flag OFF by default); goldens bit-identical; zero RT change. The event-patch path is additive/asserted, never the default in M4a.
- **Standing golden gate, per task, x86:** `song_roundtrip OK=16`, `json_roundtrip OK=38`, `edit_undo_redo OK=40`, `param_record OK=3`, `audio_clip_edit_diff OK=1`; firmware `./dbt build Debug` links. Corpus `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- **Module topology (hard rule):** `src/deluge-project/` includes NOTHING from the engine tree. The `ParamSnapshot` lives engine-side (`src/deluge/…`) and may include `deluge-project` read-only. The model never references the snapshot. Net dependency: engine→model only.
- **Consequence stays the undo currency;** M4a does not touch undo. Recorded automation (drum-MPE / `auto_param` interior log) stays on its current path — `emitEvent` fires only for the reified user edit (`SetParamValue`).
- **Region semantics are the subtle part:** the equivalence spec MUST exercise a nonzero-length (region) edit, not just a point edit, because `setValuePossiblyForRegion` routes `length != 0 && timelineCounterIsSet` to `setValueForRegion` (node manipulation), a different path than the scalar `current_value` write.
- **Patch API keyed by `ParamAddress`** so ≥2 cost strategies (incremental patch vs full `projectFromStoreAfterRead` rebuild) are benchmarkable without a rewrite.
- **Commit discipline:** branch `deluge-project-param`. clang-format pre-commit hook reformats + FAILS the first commit → recover with a FRESH `git add -A && git commit` (never `--amend`). No Co-Authored-By / "Generated with" trailers.

## Verification model

Two kinds of check: (1) the standing golden gate stays bit-identical (behavior-preserving — the A/B keeps the in-line poke default); (2) NEW host-sim projection specs in `tests/projection/edit_test.cpp` that assert the characterized/equivalent model+cursor state (real TDD — these assert genuinely new behavior of the event-patch). The microbench is a measurement artifact (prints a table), not a pass/fail gate.

**Build + run a projection spec:**
```bash
cmake --build build-sim --target edit_test 2>&1 | grep -E "error:|Linking"
build-sim/edit_test ; echo "edit_test rc=$?"   # exit 0 = all checks pass
```

**THE GATE** (referenced below):
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim --target song_roundtrip json_roundtrip edit_undo_redo param_record_diff audio_clip_edit_diff edit_test 2>&1 | grep -E "error:|FAILED"; echo BUILD done
build-sim/edit_test; echo "edit_test rc=$?"
python3 tests/golden/songs_run.py | tail -2
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff | tail -1
python3 tests/golden/audio_clip_edit_run.py --binary build-sim/audio_clip_edit_diff | tail -1
./dbt build Debug 2>&1 | grep -E "error:|Linking CXX exec"
```
Expected: `edit_test rc=0`; song 16 / json 38 / edit 40 / param 3 / audio_clip 1; firmware links. (Ignore LSP "cstddef file not found" editor diagnostics — trust the compiler.)

---

### Task 1: Characterize the in-line-poke effect on model + cursor (the contract)

The event-patch must reproduce what the in-line poke does. Before building it, PIN that behavior with an assertion spec — this also reveals the model/engine sync reality (does the in-line scalar poke keep `DocAutoParam::current_value` in sync, or only the engine cursor? does a region poke update the model `nodes`?). This is the contract Tasks 2–3 satisfy.

**Files:**
- Modify: `tests/projection/edit_test.cpp` (add a characterization spec; follow the existing `check(bool, const char*)` idiom + the `Context ctx{song, model}` the file already builds)

**Interfaces:**
- Consumes: `deluge::edits::SetParamValue`, `deluge::edits::apply(ctx, edit)`, `deluge::edits::paramAddressFor(...)` (`model/edits/param_address_for.h`, already used in this file), `ctx.model.resolveParam(address)` → `deluge::project::DocAutoParam*` (`{int32_t current_value; ParamNodeVector nodes;}`), `engineStack->autoParam->getCurrentValue()`.
- Produces: a documented, asserted description of the post-edit state for (a) a SCALAR edit and (b) a REGION edit — the exact `{engine cursor, model current_value, model nodes}` transition the event-patch must match.

- [ ] **Step 1: Read the current mutation + address paths.** Read `src/deluge/model/edits/set_param_value.cpp` (apply → `setValuePossiblyForRegion`), `src/deluge/modulation/automation/auto_param.cpp:1045` (`setValuePossiblyForRegion`) and `:1089` (`setValueForRegion` node algorithm), and how `edit_test.cpp` already builds a `ModelStackWithAutoParam` + `ParamAddress` for the pilot. Note: for a scalar (`length==0` or no timeline counter) the value lands in the engine cursor; for a region it routes to `setValueForRegion` (nodes).

- [ ] **Step 2: Write the characterization spec** in `edit_test.cpp`. Using the file's existing setup (a real `UnpatchedParamSet` param + a built `ModelStackWithAutoParam` + its `ParamAddress`), drive TWO edits through `deluge::edits::apply` and, after each, capture BOTH sides:

```cpp
// M4a Task 1 — characterize what the in-line poke does to the engine cursor AND the model store,
// so the event-patch (Task 3) can reproduce it. NOTE: fill the asserted values from what the code
// ACTUALLY produces on this branch (run it, read the numbers) — this spec PINS current behavior.
void testInlinePokeCharacterization(deluge::edits::Context& ctx, ModelStackWithAutoParam* engineStack,
                                    const deluge::project::ParamAddress& addr) {
	// (a) SCALAR edit (length 0)
	deluge::edits::SetParamValue scalarEdit{addr, /*newValue*/ 0x40000000, /*pos*/ 0, /*length*/ 0, engineStack};
	deluge::edits::apply(ctx, scalarEdit);
	deluge::project::DocAutoParam* docAfterScalar = ctx.model.resolveParam(addr);
	check(engineStack->autoParam->getCurrentValue() == 0x40000000, "scalar: engine cursor updated");
	check(docAfterScalar != nullptr, "scalar: address resolves to a model param");
	// Record what the model current_value is here — assert it explicitly (whatever the current code yields):
	//   check(docAfterScalar->current_value == <observed>, "scalar: model current_value state");
	// If the in-line poke does NOT keep model current_value in sync, that is a real finding — document it in
	// the report; the event-patch (Task 3) will be what makes the model authoritative.

	// (b) REGION edit (nonzero length, timeline counter set). Build/borrow a timeline-counter-bearing stack per the
	// file's existing region-edit setup (see the automation-view path). Then:
	deluge::edits::SetParamValue regionEdit{addr, /*newValue*/ 0x20000000, /*pos*/ 0, /*length*/ 96, engineStack};
	deluge::edits::apply(ctx, regionEdit);
	deluge::project::DocAutoParam* docAfterRegion = ctx.model.resolveParam(addr);
	check(docAfterRegion != nullptr, "region: address resolves");
	// Assert the model node vector after the region edit — the count and the (pos,value) of each node, read from
	// the actual output. This is the exact node state the event-patch region path must reproduce.
	//   check(docAfterRegion->nodes.size() == <observed>, "region: node count");
	//   check(docAfterRegion->nodes[0].pos == 0 && docAfterRegion->nodes[0].value == 0x20000000, "region: node0");
}
```
Wire it into the file's `main()` alongside the existing pilot calls.

- [ ] **Step 3: Build + run; fill in the observed values.** Run `build-sim/edit_test`; where the spec has commented `<observed>` asserts, replace them with the actual numbers the code produced (uncomment + set the real values), so the spec now PINS the true current transition. Re-run until `rc=0`.

- [ ] **Step 4: THE GATE** — expected all green (this is purely additive test code).

- [ ] **Step 5: Commit** (fresh commit after clang-format):
```bash
git add -A && git commit -m "test(m4a): characterize in-line-poke effect on model store + engine cursor (scalar + region)"
```

---

### Task 2: The `ParamSnapshot` type + `applyPatch` protocol (keyed by ParamAddress)

**Files:**
- Create: `src/deluge/model/snapshot/param_snapshot.h`, `src/deluge/model/snapshot/param_snapshot.cpp`
- Modify: `sim/CMakeLists.txt` (add the new .cpp to the app sources if it isn't picked up by a glob — verify how `deluge_app` collects sources; most likely a glob already covers `src/deluge/model/**`)

**Interfaces:**
- Consumes: `deluge::project::Song::resolveParam(const project::ParamAddress&)` → `project::DocAutoParam*`; `project::ParamAddress`; `project::DocAutoParam` (`{current_value, nodes}`); the region node algorithm reference `AutoParam::setValueForRegion` (`auto_param.cpp:1089`).
- Produces: `namespace deluge::snapshot { class ParamSnapshot { public: static void applyPatch(project::Song& model, const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length); }; }` — resolves the address in the MODEL and applies the incremental patch to the model's `DocAutoParam` (scalar → `current_value`; region → nodes), reproducing the Task-1-characterized transition. Engine-side, model-read-only-plus-store-write (it writes the model store the engine borrows — that IS the authoritative write of the north-star's step 2; the "snapshot" it presents is the derived cursor the render reads, kept consistent via the model store).

- [ ] **Step 1: Write the failing equivalence spec first** (in `edit_test.cpp`). It drives the SAME scalar + region edits two ways and asserts the model `DocAutoParam` state matches Task 1's pinned values:

```cpp
// M4a Task 2/3 — the event-patch must land the model store in the SAME state Task 1 pinned for the in-line poke.
void testParamSnapshotEquivalence(deluge::edits::Context& ctx, ModelStackWithAutoParam* engineStack,
                                  const deluge::project::ParamAddress& addr) {
	// Reset the param to a known baseline (re-project / clear) so this runs independent of Task 1's edits.
	// SCALAR via applyPatch:
	deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, addr, /*value*/ 0x40000000, /*pos*/ 0, /*length*/ 0);
	deluge::project::DocAutoParam* doc = ctx.model.resolveParam(addr);
	check(doc->current_value == 0x40000000, "applyPatch scalar: model current_value == pinned");
	// REGION via applyPatch — assert the SAME node vector Task 1 pinned for the in-line region poke:
	deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, addr, /*value*/ 0x20000000, /*pos*/ 0, /*length*/ 96);
	doc = ctx.model.resolveParam(addr);
	check(doc->nodes.size() == /* Task-1 pinned count */ 1, "applyPatch region: node count == pinned");
	// ...assert each node (pos,value) == Task-1 pinned.
}
```
Run: `build-sim/edit_test` → expect FAIL (`ParamSnapshot` undefined / link error). This is the red state.

- [ ] **Step 2: Create `param_snapshot.h`** with the class + static `applyPatch` declaration (namespace `deluge::snapshot`; forward-declare `deluge::project::Song`, `deluge::project::ParamAddress`; include only what's needed; NO engine includes are required by the header).

- [ ] **Step 3: Create `param_snapshot.cpp`, implement `applyPatch`** to reproduce the Task-1-characterized transition:
  - Resolve: `DocAutoParam* doc = model.resolveParam(address); if (doc == nullptr) return;`
  - **Scalar** (`length == 0`): set `doc->current_value = value;` (and, if Task 1 showed the in-line scalar poke also clears/keeps nodes, match that — Task 1 pins it).
  - **Region** (`length != 0`): reproduce `AutoParam::setValueForRegion`'s node algorithm (`auto_param.cpp:1089`) operating on `doc->nodes` (a `ParamNodeVector` = `OrderedPosVector<ParamNode>`) instead of the engine's node store. Read that function and mirror its pos/length node insertion/replacement exactly — the equivalence spec's pinned node vector is your oracle. Use the DOM node-edit helpers where they exist (`deluge-project/param_edit.h` `setNodeAtPos`, already used by `edit_test.cpp`) rather than hand-rolling ordered inserts.
  Keep the body minimal — just enough to make the Step-1 spec pass.

- [ ] **Step 4: Build + run the spec** → `build-sim/edit_test` expected `rc=0` (equivalence holds). If the region node vector diverges, diff your node output against Task 1's pinned vector and reconcile against `setValueForRegion`.

- [ ] **Step 5: THE GATE** — expected all green (nothing calls `applyPatch` from production yet; goldens unaffected).

- [ ] **Step 6: Commit:**
```bash
git add -A && git commit -m "feat(m4a): ParamSnapshot::applyPatch — model-first param patch keyed by ParamAddress, equivalent to the in-line poke"
```

---

### Task 3: Wire `SetParamValue::emitEvent` → `applyPatch` behind an A/B flag

**Files:**
- Modify: `src/deluge/model/edits/set_param_value.cpp` (`emitEvent`)
- Create/modify: a flag define — add `DELUGE_M4_EVENT_PATCH` (default OFF) to the sim build only (e.g. a `target_compile_definitions(edit_test PRIVATE DELUGE_M4_EVENT_PATCH=1)` in `sim/CMakeLists.txt` for the spec target, OR a runtime toggle the spec flips — see Step 2). Firmware/default builds leave it OFF.

**Interfaces:**
- Consumes: `deluge::snapshot::ParamSnapshot::applyPatch`; the existing `SetParamValue` fields `address_`, `newValue_`, `pos_`, `length_`; `ctx.model`.

- [ ] **Step 1: Extend `SetParamValue::emitEvent`** to drive the patch when enabled, keeping the observational resolve for the default build:
```cpp
void SetParamValue::emitEvent(Context& ctx) {
	if (!address_.has_value()) {
		return; // engine-only lane (never-saved owner) — nothing to patch in the model
	}
#if ALPHA_OR_BETA_VERSION
	if (ctx.model.resolveParam(*address_) == nullptr) {
		D_PRINTLN("M4 event feed: SetParamValue address did not resolve to a DOM param");
	}
#endif
	// M4a: the first real consumer of the emission point. Off by default (A/B) — the in-line poke in apply() is
	// still the shipped path; when enabled, the model-first patch runs so the equivalence spec can compare them.
	if (m4EventPatchToggle()) {
		deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, *address_, newValue_, pos_, length_);
	}
}
```
where `m4EventPatchToggle()` is the single toggle accessor: declare `namespace deluge::edits { bool& m4EventPatchToggle(); }` returning a function-local `static bool` defaulting **false** (define it in `set_param_value.cpp`). The equivalence spec flips it (`deluge::edits::m4EventPatchToggle() = true;`) to run the event path without a separate build; the default/firmware build never sets it, so `emitEvent` does not patch and behaviour is unchanged. (A compile-time `DELUGE_M4_EVENT_PATCH` guard is an alternative but the runtime toggle avoids a second build target for the A/B assertion.)

- [ ] **Step 2: Update the equivalence spec (Task 2 Step 1)** to exercise BOTH paths through the real Edit: with the toggle OFF, `apply(ctx, edit)` (in-line poke) leaves the model in state X; reset; with the toggle ON, `apply(ctx, edit)` ALSO runs `emitEvent`→`applyPatch`, and the model must reach the SAME pinned state (now via the event path). Assert both reach Task-1's pinned `{current_value, nodes}`. Include the REGION edit in both.

- [ ] **Step 3: Build + run** → `build-sim/edit_test` `rc=0`.

- [ ] **Step 4: THE GATE** — expected all green. Critically: the DEFAULT build has the toggle OFF, so `emitEvent` does not patch and behaviour is identical → goldens bit-identical, firmware unaffected. Verify the toggle default is false.

- [ ] **Step 5: Commit:**
```bash
git add -A && git commit -m "feat(m4a): SetParamValue::emitEvent drives ParamSnapshot::applyPatch behind an off-by-default A/B toggle"
```

---

### Task 4: Host-sim cost microbench (incremental patch vs full rebuild)

**Files:**
- Create: `tests/projection/param_snapshot_bench.cpp`
- Modify: `sim/CMakeLists.txt` (add a `param_snapshot_bench` executable, mirroring the `edit_test` block at `sim/CMakeLists.txt:629-643`)

**Interfaces:**
- Consumes: `deluge::snapshot::ParamSnapshot::applyPatch`; the full-projection entry point the engine uses at load (`projectFromStoreAfterRead` on the param set — read `Sound::readParamsFromFile` / `UnpatchedParamSet::projectFromStoreAfterRead` for the exact call) as the "full rebuild" comparator.
- Produces: a runnable `build-sim/param_snapshot_bench` that prints a comparison table.

- [ ] **Step 1: Add the CMake target** in `sim/CMakeLists.txt` after the `edit_test` block, copying its shape verbatim:
```cmake
add_executable(param_snapshot_bench
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_platform.c
    ${REPO_ROOT}/src/bsp/host/host_audio.c
    ${REPO_ROOT}/src/bsp/host/host_link.c
    ${REPO_ROOT}/src/bsp/host/host_pcm.c
    ${REPO_ROOT}/src/bsp/host/host_midi.c
    ${REPO_ROOT}/src/bsp/host/host_debug.cpp
    ${REPO_ROOT}/tests/projection/param_snapshot_bench.cpp
)
target_include_directories(param_snapshot_bench PRIVATE ${HOST_INCLUDE_DIRS} ${REPO_ROOT}/src/deluge)
target_link_libraries(param_snapshot_bench PRIVATE deluge_app deluge_scheduler ${SIM_ALLOC_LINK})
if(RT_LIB)
    target_link_libraries(param_snapshot_bench PRIVATE ${RT_LIB})
endif()
```

- [ ] **Step 2: Write the microbench** (`param_snapshot_bench.cpp`). Set up a param set with realistic node counts (e.g. 0, 8, 64 nodes — the sizes the real corpus shows). For a fixed number of edits (e.g. 100k scalar + 10k region), time two strategies and print a table:
  - **Strategy A — incremental patch:** call `ParamSnapshot::applyPatch(...)` per edit.
  - **Strategy B — full rebuild:** per edit, mutate the store then call the full `projectFromStoreAfterRead` (the fallback path) to re-derive the cursor.
  Measure wall-time via a monotonic clock the host provides (bare `clock()` / `std::chrono::steady_clock` — this is a host-only bench, `Date.now`/`Math.random` restrictions apply to Workflow scripts, NOT to C++ test code, so `std::chrono` is fine here). Also count allocations if the sim allocator exposes a counter (`tests/spec_alloc` shows the pattern; optional). Print:
```
param_snapshot_bench: edits=100000 nodes=64
  strategy          total_ms   per_edit_ns
  incremental_patch    …          …
  full_rebuild         …          …
  ratio (full/incr)    …x
```
Return 0 always (measurement, not pass/fail). The ratio is the artifact: it quantifies how much cheaper incremental patches are than the full-rebuild fallback — the north-star's "incremental patch, full rebuild is the fallback" claim, measured. Repeat across the node-count sizes.

- [ ] **Step 3: Build + run:**
```bash
cmake --build build-sim --target param_snapshot_bench 2>&1 | grep -E "error:|Linking"
build-sim/param_snapshot_bench
```
Expected: prints the table for each node-count size; incremental should be cheaper than full-rebuild (ratio > 1) especially at higher node counts.

- [ ] **Step 4: THE GATE** — expected all green (new bench target doesn't touch production paths).

- [ ] **Step 5: Commit:**
```bash
git add -A && git commit -m "test(m4a): host-sim cost microbench — incremental applyPatch vs full projection rebuild"
```

---

### Task 5: Measurement write-up + M4a exit doc

**Files:**
- Create: `docs/dev/m4a_snapshot_measurement.md`

- [ ] **Step 1: Run the bench** (`build-sim/param_snapshot_bench`) and record the table(s) verbatim.

- [ ] **Step 2: Write the doc** — the measured incremental-vs-rebuild ratios per node-count; the finding from Task 1 (whether the in-line scalar poke kept the model `current_value` in sync — i.e. whether the event feed is what makes the model authoritative for scalar current-values); the directional read on granularity (does the number justify incremental patches / does per-owner cursor cost look acceptable), stated as a **proxy** with the on-device confirmation named as the next step; and the explicit M4a exit statement + what M4b/c/d inherit.

- [ ] **Step 3: THE GATE** — all green (docs only).

- [ ] **Step 4: Commit:**
```bash
git add docs/dev/m4a_snapshot_measurement.md && git commit -m "docs(m4a): snapshot cost measurement + event-feed equivalence findings"
```

---

## Notes / Risks

- **Task 1 is load-bearing.** If the in-line scalar poke does NOT sync the model `current_value` (plausible — the engine cursor and the model store may diverge for unautomated scalars, with save going through the separate `DocParamSet` serialize path), then the event-patch's scalar write is genuinely NEW behavior (making the model authoritative), and the equivalence is on the ENGINE-CURSOR + the model NODES, with the model current_value being an improvement. Document precisely; do not force a false "identical" assertion.
- **Region node algorithm.** Reproduce `setValueForRegion` (`auto_param.cpp:1089`) faithfully against `DocAutoParam::nodes` using `deluge-project/param_edit.h` helpers; the pinned node vector from Task 1 is the oracle. This is the one place to be careful.
- **A/B default.** The toggle MUST default OFF and the firmware/default build MUST NOT patch — that is what keeps goldens bit-identical and RT unchanged. Verify in Task 3 Step 4.
- **Granularity is measured, not chosen.** M4a emits the number; it does not lock per-owner-vs-per-song. The `ParamAddress`-keyed API keeps both open.
- **Scope discipline:** no notes, no hot Sound fields, no LearnedMIDI/reconcile, no removal of in-line pokes. Those are M4b/c/d + the deferred commit-flip.

## References
- Spec: `docs/superpowers/specs/2026-07-12-m4a-snapshot-spine-design.md`
- Pilot seam: `src/deluge/model/edits/{edit,set_param_value}.{h,cpp}`
- Model store + resolve: `src/deluge-project/param_automation.h` (`DocAutoParam`), `src/deluge-project/song.{h,cpp}` (`resolveParam`)
- Region algorithm: `src/deluge/modulation/automation/auto_param.cpp:1045` (`setValuePossiblyForRegion`), `:1089` (`setValueForRegion`)
- Harness + CMake pattern: `tests/projection/edit_test.cpp`, `sim/CMakeLists.txt:629-643`
- North star: `docs/dev/project_model_north_star.md` (§ Snapshot, events→snapshot)
