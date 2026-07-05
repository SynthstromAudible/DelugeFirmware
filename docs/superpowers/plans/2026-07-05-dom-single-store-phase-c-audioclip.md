# DOM single-store Phase C — AudioClip param nodes DOM-bound (Model A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make each AudioClip's unpatched automation node storage DOM-owned, with the engine's `AutoParam`s
borrowing it — the first Movement-1 binding step of the accessor/projection migration.

**Architecture:** Model A (`ParamSet`-anchored store). The `ParamSet` gains
`std::unique_ptr<deluge::project::DocParamSet> domStore_`; at fresh population each `AutoParam` borrows into
`domStore_->params[p].nodes` and the store is RAII-freed with the set. The `ParamSet` is heap-stable and migrates
through `ParamBackup` by pointer-move (never relocates), so the store outlives the clip and follows the params
through steal/backup/restore. Spec:
`docs/superpowers/specs/2026-07-03-param-automation-accessor-projection-design.md` §5.1.

**Tech Stack:** C++23, firmware container `ParamNodeVector` (`OrderedPosVector<ParamNode>`), DOM types in
`src/deluge-project/param_automation.h`, sim/golden harness.

## Global Constraints

- **Behaviour-preserving / byte-identical.** Binding moves node storage; it must not change any rendered or saved
  bytes. Golden `OK=15 REGRESS=0` is a hard gate; any change is a bug.
- **Unbound is always correct.** A `ParamSet` with `domStore_ == nullptr` uses `ownNodes_` exactly as today.
  Missing a bind site degrades gracefully to legacy — never a dangle, never a correctness change. The only new
  invariant is Model A's lifetime (RAII on the set), which the type system enforces.
- **Verification commands** (run from repo root `/home/kate/GitHub/DelugeFirmware`):
  - Sim unit: `cmake --build build-sim --target param_projection_test && ./build-sim/param_projection_test`
  - Golden: `cmake --build build-sim --target song_roundtrip && DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" python3 tests/golden/songs_run.py` → **OK=15**
  - Clip clone: `cmake --build build-sim --target clip_clone_roundtrip && python3 tests/golden/clip_clone_run.py`
  - Song edit: `cmake --build build-sim --target song_edit_roundtrip && ./build-sim/song_edit_roundtrip` → PERSISTED
  - Firmware: `./dbt build Debug` clean.
- clang-format pre-commit hook reformats and aborts the commit (rc=1) — re-run `git add -A && git commit`.
- No `Co-Authored-By` / "Generated with Claude" trailers. Commit at each task boundary.

---

### Task C1: `ParamSet::domStore_` + `bindDomStore()` + copy/move

**Files:**
- Modify: `src/deluge/modulation/params/param_set.h` (add member + method decl; base copy/move handling)
- Modify: `src/deluge/modulation/params/param_set.cpp` (define `bindDomStore`; base copy/move ctors)
- Test: `tests/projection/param_automation_test.cpp` (binary `param_projection_test`)

**Interfaces:**
- Produces: `void ParamSet::bindDomStore();` — allocates `domStore_`, borrow-points every `params[p]` into
  `domStore_->params[p].nodes`. Idempotent no-op if already bound. Consumed by C2/C3.

- [ ] **Step 1: Write the failing test.** In `tests/projection/param_automation_test.cpp`, add a case: construct
  an `UnpatchedParamSet`, author nodes on one param via `setNodeAtPos`, call `bindDomStore()`, then assert
  (a) `set.params[p].nodes()` aliases `set.domStore().params[p].nodes` (same address), (b) a subsequent
  `setNodeAtPos` lands in the DOM store, (c) after a copy-construct the *copy* is unbound (`domStore()==nullptr`)
  and owns its own nodes. (Mirror the existing Phase-B borrow case in this file.) Add a test-only accessor
  `DocParamSet* domStore()` if needed.

- [ ] **Step 2: Run it, confirm it fails to compile** (`bindDomStore` undeclared):
  `cmake --build build-sim --target param_projection_test`.

- [ ] **Step 3: Add the member + method.** In `param_set.h`, `#include "deluge-project/param_automation.h"` and
  `#include <memory>`; in the `ParamSet` `protected:` section add:
  ```cpp
  /// DOM-owned node store this set's AutoParams borrow (Model A). nullptr = unbound (params use ownNodes_).
  /// RAII-freed with the set; the set is heap-stable and migrates by pointer, so the store outlives the clip
  /// and follows the params through ParamBackup steal/restore. See the Phase C spec §5.1.
  std::unique_ptr<deluge::project::DocParamSet> domStore_;
  ```
  Add public `void bindDomStore();` and a test accessor `deluge::project::DocParamSet* domStore() { return domStore_.get(); }`.

- [ ] **Step 4: Define `bindDomStore`** in `param_set.cpp`:
  ```cpp
  void ParamSet::bindDomStore() {
  	if (domStore_) {
  		return; // already bound
  	}
  	domStore_ = std::make_unique<deluge::project::DocParamSet>(deluge::project::toDocParamSet(*this));
  	for (int32_t p = 0; p < numParams_; p++) {
  		params[p].borrowNodeStorage(&domStore_->params[p].nodes);
  	}
  }
  ```

- [ ] **Step 5: Fix copy/move.** `std::unique_ptr` makes `ParamSet` non-copyable by default, breaking the subclass
  copy-ctors (e.g. `UnpatchedParamSet(UnpatchedParamSet const&)`). Give `ParamSet` an explicit copy-ctor that
  leaves the clone **unbound** (`domStore_` stays nullptr — a clone owns its own nodes until separately bound) and
  a defaulted move-ctor/assignment that transfers `domStore_`. Add matching declarations; verify the subclass
  copy-ctors still compile (they delegate to the base).

- [ ] **Step 6: Run the test, confirm PASS.** `./build-sim/param_projection_test`. Then confirm no regression:
  golden `OK=15`.

- [ ] **Step 7: Commit** (`dom: ParamSet domStore_ + bindDomStore (Model A anchor; clone starts unbound)`).

---

### Task C2: Bind AudioClips at load

**Files:**
- Modify: `src/deluge/model/song/song.cpp` (the post-load all-clips loop, ~L1723-1752)
- Test: `tests/golden/…` (golden corpus already contains audio-clip songs)

**Interfaces:**
- Consumes: `ParamSet::bindDomStore()` (C1).

- [ ] **Step 1: Locate the post-load clips loop** (`song.cpp:1723-1752`) that iterates
  `project.clips.session_clips` and `project.clips.arrangement_only_clips` calling `claimOutput`.

- [ ] **Step 2: Add a bind pass** right after that loop: for each clip in both arrays, if it is an AudioClip
  (`clip->type == ClipType::AUDIO`), fetch its unpatched set (`getUnpatchedParamSet()` on its paramManager, guarded
  by `containsAnyMainParamCollections()`) and call `bindDomStore()`. Keep it AudioClip-only in Phase C (skip
  InstrumentClips). Add an `ALPHA_OR_BETA_VERSION` count-sanity guard mirroring the song-global `E4B0` check.

- [ ] **Step 3: Build + golden.** `./dbt build Debug` clean; golden **OK=15** (save reads `nodes()` = the borrowed
  store, so bytes are unchanged — a regression here means the seed/borrow diverged).

- [ ] **Step 4: Song-edit round-trip.** Ensure `song_edit_roundtrip` covers (or extend it to cover) editing an
  AudioClip automation param → save → reload → PERSISTED. This proves the bound store is the live edit target.

- [ ] **Step 5: Commit** (`dom: borrow-point AudioClip unpatched params at their DOM store on load`).

---

### Task C3: Bind AudioClips at clone / overdub / fresh setup

**Files:**
- Modify: `src/deluge/model/clip/audio_clip.cpp` (clone :88; overdub post-`setOutput` :265; fresh setup via
  `solicitParamManager`/`setOutput` :1238-1249)
- Test: `tests/golden/clip_clone_roundtrip.cpp` / `clip_clone_run.py`

**Interfaces:**
- Consumes: `ParamSet::bindDomStore()` (C1). Restore-from-backup must **not** rebind (params keep their borrow).

- [ ] **Step 1: Clone.** After `newClip->paramManager.cloneParamCollectionsFrom(&paramManager, true)`
  (`audio_clip.cpp:88`) — the clone's params start unbound (own their nodes) — call `bindDomStore()` on the new
  clip's unpatched set (guarded by `containsAnyMainParamCollections()`).

- [ ] **Step 2: Overdub.** After `newClip->setOutput(...)` in `cloneAsNewOverdub` (`audio_clip.cpp:265`, past the
  `E421` guard), bind the new clip's unpatched set.

- [ ] **Step 3: Fresh new-clip setup.** In the `setOutput`/`solicitParamManager` fresh-population path
  (`audio_clip.cpp:1238-1249`), bind after population — but only when the manager was freshly set up, **not** when
  it was restored from `ParamBackup` (a restored set's params already borrow their store; `bindDomStore`'s
  `if (domStore_) return` guard also protects this, since a restored param's set still holds its `domStore_`).

- [ ] **Step 4: Clip-clone round-trip.** `clip_clone_run.py` → 38/0 (real `AudioClip::clone`, incl. reverse-flatten,
  drives C1+C3). Golden **OK=15**. Firmware `./dbt build Debug` clean.

- [ ] **Step 5: Commit** (`dom: bind AudioClip DOM store on clone / overdub / fresh setup`).

---

### Task C4: Steal/backup/restore harness test (the frontier coverage)

**Files:**
- Create/extend: `tests/projection/param_automation_test.cpp` or a small sim test driving `ParamBackup`
- Modify (if needed): test CMake target

**Interfaces:**
- Consumes: `ParamBackup::backUp`, `getForExactClip(stealInto)` / `getPreferablyWithClip(stealInto)` (steal-in).

- [ ] **Step 1: Write the test.** Build a live `ParamManager` with a bound AudioClip-style unpatched set, author
  automation on a param, then: `backUp(modControllable, clipA, &pm)` → assert the stolen set's param still borrows
  the *same* `DocParamSet` address (pointer-move, no relocation) and its nodes are intact; then
  `getForExactClip(modControllable, clipA, &pm2)` into a fresh manager → assert the nodes survived and a
  subsequent edit lands in the same DOM store. Then destroy the backup manager and assert no leak/dangle
  (store freed with the set).

- [ ] **Step 2: Run it, confirm PASS.** This is the golden-blind coverage: it exercises the exact
  steal/backup/restore lifetime that the load/save golden cannot see.

- [ ] **Step 3: Commit** (`test: AudioClip param steal/backup/restore keeps the DOM borrow valid`).

---

### Task C5: Full verification sweep + memory

**Files:**
- None (verification) + memory note.

- [ ] **Step 1: x86 golden** `OK=15 REGRESS=0`.
- [ ] **Step 2: Targeted ARM golden** (kernel-relevant cases via `run.py --filter`) — confirm x86≡ARM.
- [ ] **Step 3:** `param_projection_test` OK, `song_edit_roundtrip` PERSISTED, `clip_clone_run.py` 38/0,
  `./dbt build Debug` clean.
- [ ] **Step 4:** Update memory (`dom-single-store-phase-b` → add a Phase C note, or a new
  `dom-single-store-phase-c` file) recording: Model A (ParamSet-anchored `domStore_`), AudioClip bound at
  load/clone/overdub, steal-path harness added, all green; remaining owners (InstrumentClip, kits/drums,
  cables/MIDI/expression) + Movement 2 (accessor flip) still ahead.
- [ ] **Step 5: Commit** any memory/doc updates.

---

## Notes / risks

- **Expression tail collection.** A steal can attach a rare `ExpressionParamSet` to an AudioClip's manager
  (`stealExpressionParams`). Phase C binds only the unpatched set; an unbound expression set is safe (legacy
  `ownNodes_`). Do not attempt to bind it here.
- **Save path unchanged.** Save iterates `nodes()` (the borrowed store); no serializer change. If golden moves,
  the seed (`toDocParamSet`) or borrow diverged — treat as a bug, not a re-baseline.
- **Not in scope:** InstrumentClip/kits/drums/cables/MIDI/expression binding (later Movement-1 steps), and the
  Movement-2 accessor op-flip (engine ops → `param_edit` kernels; drop `ownNodes_`). Song-global stays on its
  `project.global_fx` store (spec §5.1 transitional inconsistency).
