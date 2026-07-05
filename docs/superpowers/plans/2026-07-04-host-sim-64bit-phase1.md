# Host-sim 64-bit (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the `DELUGE_SIM_X64=ON` host-sim compile, link, and run as a 64-bit binary on x86-64 Linux, then measure whether the golden renders stay stable — the feasibility verdict for the Apple Silicon port.

**Architecture:** The app does pointer arithmetic via `(uint32_t)ptr` casts, which truncate on a 64-bit target. Replace each with `(uintptr_t)ptr`. On the 32-bit firmware `uintptr_t ≡ uint32_t`, so shared code stays byte-identical; only the host-only allocation-tracking hash (`#if MEM_GUARD`) widens its stored key. No shared struct layout changes.

**Tech Stack:** C++23, CMake + Ninja, clang/gcc, the Rust TLSF staticlib (already 64-bit-clean, `x86_64-unknown-linux-gnu` target wired), `scripts/golden_mixdown.sh` for the golden A/B.

## Global Constraints

- Shared (firmware-compiled) code must stay **byte-identical on ARM**: use `uintptr_t` (or `reinterpret_cast<uintptr_t>`), never a fixed 64-bit type. Verify via the `dbt build Debug` size line (`text`/`data`/`bss`) being unchanged.
- **No change to any shared struct layout or data representation.** Only the host-only `alloc_metadata` hash (`#if MEM_GUARD`) may change a field type.
- `DELUGE_SIM_X64=ON` must build under **both clang and gcc**.
- The existing default `-m32` sim, the ARM firmware, and the qemu specs must remain green.
- No firmware behaviour change.
- Fix the truncation exactly where the compiler flags it; keep the surrounding assignment's target type (`int32_t`/`uint32_t` byte counts) as-is so downstream type deduction is unchanged.

## File Structure

All changes are edits to existing files; no new source files.

- `src/deluge/memory/alloc_metadata.{cpp,h}` — host-only alloc-tracking hash; widen its address key/field/sentinels to `uintptr_t`.
- `src/deluge/memory/stack_guard.cpp` — pointer-difference arithmetic → `uintptr_t`.
- `src/deluge/model/sample/{sample_low_level_reader,sample_recorder,sample}.cpp` — pointer arithmetic → `uintptr_t`.
- `src/deluge/processing/engines/audio_engine.cpp`, `src/deluge/storage/wave_table/wave_table.cpp`, `src/deluge/storage/audio/audio_file_manager.cpp`, `src/deluge/storage/storage_manager.cpp`, `src/deluge/storage/cluster/cluster.cpp`, `src/deluge/model/clip/instrument_clip.cpp`, `src/deluge/model/note/note_row.cpp`, `src/deluge/model/song/song.cpp`, `src/deluge/util/functions.cpp`, `src/deluge/util/algorithm/quick_sorter.cpp` — pointer arithmetic → `uintptr_t`.
- New golden reference files under `~/.cache/deluge-golden` (created by the harness during measurement; not committed).

---

### Task 1: Widen the host-only allocation-tracking hash + stack guard to pointer width

**Files:**
- Modify: `src/deluge/memory/alloc_metadata.cpp`, `src/deluge/memory/alloc_metadata.h`
- Modify: `src/deluge/memory/stack_guard.cpp`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: nothing other tasks depend on (self-contained). `alloc_metadata` is compiled only under `#if MEM_GUARD` (host-sim); `stack_guard` arithmetic stays `uintptr_t` (firmware-neutral).

- [ ] **Step 1: Establish the 64-bit build baseline (the failing "test")**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
CC=clang CXX=clang++ cmake -B build-sim-x64 -S sim -G Ninja -DDELUGE_SIM_X64=ON
cmake --build build-sim-x64 -- -k 0 2>&1 | grep "alloc_metadata\|stack_guard" | grep -c "cast from pointer"
```
Expected: a non-zero count (~10) — these are the truncations to fix.

- [ ] **Step 2: Widen the address key/field/sentinels in `alloc_metadata`**

In `src/deluge/memory/alloc_metadata.h`, change the stored address field of the slot struct from `uint32_t address;` to `uintptr_t address;`.

In `src/deluge/memory/alloc_metadata.cpp`:
- Change the `kEmpty` / `kTombstone` sentinel constants and the `key`, `callsite`, and hash-input parameters/locals from `uint32_t` to `uintptr_t` (e.g. `recordAllocWithEpoch`, `findSlot`, `lookupAlloc`, `hashAddress`).
- Change the pointer→key casts from `(uint32_t)address` / `(uint32_t)callsite` to `(uintptr_t)address` / `(uintptr_t)callsite`.
- If `hashAddress` mixes bits assuming 32 bits, fold the high half in first, e.g.:
```cpp
static uint32_t hashAddress(uintptr_t key) {
	key ^= (key >> 32); // no-op on 32-bit; folds the high half on 64-bit
	uint32_t k = (uint32_t)key;
	// ... existing 32-bit mix on k ...
}
```
Keep `requestedSize`/`epoch` as `uint32_t` (they are not addresses).

- [ ] **Step 3: Widen the pointer arithmetic in `stack_guard.cpp`**

For each `(uint32_t)ptr` the compiler flagged (around lines 62 and 66), change it to `(uintptr_t)ptr`. These are stack-address differences; the surrounding result variable keeps its existing type.

- [ ] **Step 4: Verify these files now compile in 64-bit mode**

Run:
```bash
cmake --build build-sim-x64 -- -k 0 2>&1 | grep "alloc_metadata\|stack_guard" | grep -c "cast from pointer"
```
Expected: `0`.

- [ ] **Step 5: Commit**

```bash
git add src/deluge/memory/alloc_metadata.cpp src/deluge/memory/alloc_metadata.h src/deluge/memory/stack_guard.cpp
git commit -m "sim(64-bit): widen host alloc-tracking hash + stack guard to uintptr_t"
```

---

### Task 2: Widen pointer arithmetic in the sample subsystem

**Files:**
- Modify: `src/deluge/model/sample/sample_low_level_reader.cpp` (~19 sites)
- Modify: `src/deluge/model/sample/sample_recorder.cpp` (~18 sites)
- Modify: `src/deluge/model/sample/sample.cpp` (~2 sites)

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: nothing other tasks depend on. These are shared/firmware files — the ARM firmware must stay byte-identical.

- [ ] **Step 1: Record the firmware size baseline (firmware-neutrality gate)**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
./dbt build Debug 2>&1 | tail -2
```
Expected: a build with a size line like `text  data  bss ...`. **Record the `text`, `data`, `bss` numbers** — they must be unchanged after this task.

- [ ] **Step 2: See the failing sites**

Run:
```bash
cmake --build build-sim-x64 -- -k 0 2>&1 | grep -E "sample_low_level_reader|sample_recorder|sample\.cpp" | grep -c "cast from pointer"
```
Expected: a non-zero count (~39).

- [ ] **Step 3: Apply the `uintptr_t` widening**

For every `(uint32_t)ptr` cast the compiler flags in these three files, change `(uint32_t)` to `(uintptr_t)`. The result stays assigned to its existing type. Real examples from these files:

```cpp
// sample_low_level_reader.cpp:45  — before:
uint32_t withinCluster = (uint32_t)currentPlayPos - (uint32_t)&clusters[0]->data + 4
// after:
uint32_t withinCluster = (uintptr_t)currentPlayPos - (uintptr_t)&clusters[0]->data + 4
```
```cpp
// sample_recorder.cpp:609  — before:
int32_t bytesTilClusterEnd = (uint32_t)clusterEndPos - (uint32_t)writePos;
// after:
int32_t bytesTilClusterEnd = (uintptr_t)clusterEndPos - (uintptr_t)writePos;
```
Enumerate every remaining site with:
```bash
cmake --build build-sim-x64 -- -k 0 2>&1 | grep -E "sample_low_level_reader|sample_recorder|sample\.cpp" | grep "cast from pointer"
```
If any flagged site is **not** pointer arithmetic (i.e. it stores a pointer to use later as a pointer), STOP and flag it — the investigation found none, so this would be a surprise.

- [ ] **Step 4: Verify the sample files compile in 64-bit mode**

Run:
```bash
cmake --build build-sim-x64 -- -k 0 2>&1 | grep -E "sample_low_level_reader|sample_recorder|sample\.cpp" | grep -c "cast from pointer"
```
Expected: `0`.

- [ ] **Step 5: Verify the firmware is byte-neutral**

Run:
```bash
./dbt build Debug 2>&1 | tail -1
```
Expected: the `text`/`data`/`bss` numbers are **identical** to the Step 1 baseline (on 32-bit ARM `uintptr_t == uint32_t`).

- [ ] **Step 6: Commit**

```bash
git add src/deluge/model/sample/sample_low_level_reader.cpp src/deluge/model/sample/sample_recorder.cpp src/deluge/model/sample/sample.cpp
git commit -m "sim(64-bit): widen sample-path pointer arithmetic to uintptr_t"
```

---

### Task 3: Widen pointer arithmetic in the audio engine, wavetable, storage, and model code

**Files:**
- Modify: `src/deluge/processing/engines/audio_engine.cpp` (~8 sites + the `std::min` cascade)
- Modify: `src/deluge/storage/wave_table/wave_table.cpp` (~7 sites)
- Modify: `src/deluge/storage/audio/audio_file_manager.cpp` (~4), `src/deluge/storage/storage_manager.cpp` (~2), `src/deluge/storage/cluster/cluster.cpp` (~1)
- Modify: `src/deluge/model/clip/instrument_clip.cpp` (~4), `src/deluge/model/note/note_row.cpp` (~1), `src/deluge/model/song/song.cpp` (~1)
- Modify: `src/deluge/util/functions.cpp` (~2), `src/deluge/util/algorithm/quick_sorter.cpp` (~1)

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: nothing other tasks depend on. Shared/firmware files — ARM must stay byte-identical.

- [ ] **Step 1: Confirm the firmware size baseline is still current**

Run:
```bash
./dbt build Debug 2>&1 | tail -1
```
Expected: same `text`/`data`/`bss` as recorded in Task 2 (Task 2 was firmware-neutral).

- [ ] **Step 2: See the failing sites**

Run:
```bash
cmake --build build-sim-x64 -- -k 0 2>&1 | grep "cast from pointer" | wc -l
```
Expected: a non-zero count (~30 remaining after Tasks 1–2).

- [ ] **Step 3: Apply the `uintptr_t` widening**

For every flagged `(uint32_t)ptr` in these files, change `(uint32_t)` to `(uintptr_t)`. Real example:
```cpp
// audio_engine.cpp:176  — before:
uint32_t offsetBytes = inputRingPos - (uint32_t)inputRing;
// after:
uint32_t offsetBytes = inputRingPos - (uintptr_t)inputRing;
```

- [ ] **Step 4: Fix the `std::min` type cascade in `audio_engine.cpp`**

Around line 1228, the widened subtraction feeds `numSamplesFeedingNow`, which then hits `std::min(numSamplesFeedingNow, 256u)` (audio_engine.cpp:1232) — "no matching function for call to 'min'" because the operands are now different integer types. Keep the arithmetic result narrowed to its original type so the `min` operands match again. Before:
```cpp
numSamplesFeedingNow = (stopPos - (uint32_t)recorder->sourcePos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE);
// ...
numSamplesFeedingNow = std::min(numSamplesFeedingNow, 256u);
```
After — widen only the address, then cast the whole expression back to the original type of `numSamplesFeedingNow` (`uint32_t`):
```cpp
numSamplesFeedingNow = (uint32_t)((stopPos - (uintptr_t)recorder->sourcePos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE));
// ...
numSamplesFeedingNow = std::min(numSamplesFeedingNow, 256u);
```
(If `stopPos` is itself a `uint32_t` holding an address, widen it consistently so the subtraction is done in `uintptr_t`, then narrow the shifted result.)

- [ ] **Step 5: Verify all truncation + `min` errors are gone**

Run:
```bash
cmake --build build-sim-x64 -- -k 0 2>&1 | grep -cE "cast from pointer|no matching function"
```
Expected: `0`.

- [ ] **Step 6: Verify the firmware is byte-neutral**

Run:
```bash
./dbt build Debug 2>&1 | tail -1
```
Expected: `text`/`data`/`bss` identical to the Task 2 baseline.

- [ ] **Step 7: Commit**

```bash
git add src/deluge/processing/engines/audio_engine.cpp src/deluge/storage/wave_table/wave_table.cpp src/deluge/storage/audio/audio_file_manager.cpp src/deluge/storage/storage_manager.cpp src/deluge/storage/cluster/cluster.cpp src/deluge/model/clip/instrument_clip.cpp src/deluge/model/note/note_row.cpp src/deluge/model/song/song.cpp src/deluge/util/functions.cpp src/deluge/util/algorithm/quick_sorter.cpp
git commit -m "sim(64-bit): widen engine/storage/model pointer arithmetic to uintptr_t"
```

---

### Task 4: Green 64-bit build (clang + gcc) that links and runs, with no regression

**Files:** none (verification + any residual fix the build surfaces).

**Interfaces:**
- Consumes: the compiled changes from Tasks 1–3.
- Produces: a runnable `build-sim-x64/deluge_render` (and `deluge_host`).

- [ ] **Step 1: Full clang 64-bit build (real targets)**

Run:
```bash
cmake --build build-sim-x64 --target deluge_host deluge_render deluge_loadcheck 2>&1 | grep -c "error:"
```
Expected: `0`. (If a residual pointer-truncation appears in a file not covered above, apply the same `uintptr_t` fix and re-run.)

- [ ] **Step 2: Full gcc 64-bit build**

Run:
```bash
cmake -B build-sim-x64-gcc -S sim -G Ninja -DDELUGE_SIM_X64=ON
cmake --build build-sim-x64-gcc --target deluge_host deluge_render deluge_loadcheck 2>&1 | grep -c "error:"
```
Expected: `0`.

- [ ] **Step 3: The 64-bit binary runs**

Run:
```bash
file build-sim-x64/deluge_render | grep -o "64-bit"
build-sim-x64/deluge_render 2>&1 | head -1
```
Expected: `64-bit`; then the `usage:` line (proves the 64-bit binary links and executes).

- [ ] **Step 4: Regression — the 32-bit sim still builds**

Run:
```bash
cmake --build build-sim --target deluge_render 2>&1 | grep -c "error:"
```
Expected: `0`.

- [ ] **Step 5: Regression — the qemu specs still pass**

Run:
```bash
cmake --build build-qemu 2>&1 | grep -c "error:"
qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9 build-qemu/spec/all_specs fixedpoint_vfp_spec >/dev/null 2>&1; echo "vfp=$?"
qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9 build-qemu/spec/all_specs fixedpoint_spec >/dev/null 2>&1; echo "fp=$?"
```
Expected: `0` errors, `vfp=0`, `fp=0`.

- [ ] **Step 6: Commit any residual fix (if Step 1/2 needed one)**

```bash
git add -A
git commit -m "sim(64-bit): resolve residual truncations; 64-bit build links and runs"
```
(If no residual fix was needed, skip — nothing to commit.)

---

### Task 5: Measure golden stability under 64-bit (the feasibility verdict)

**Files:** none (measurement; produces a written characterization, no source changes).

**Interfaces:**
- Consumes: the runnable `build-sim-x64` from Task 4.
- Produces: a stability report that decides replace-vs-coexist (returned to the user).

- [ ] **Step 1: Layout-invariance under 64-bit (padsweep), per fixture**

For each fixture, render at several heap-pad layouts and assert all bit-identical (this catches any residual address/layout coupling exposed by 64-bit):
```bash
for fx in cordae highsiderr icoustic; do
  echo "== $fx =="
  BUILD_DIR=$PWD/build-sim-x64 FIXTURE=$fx scripts/golden_mixdown.sh padsweep
done
```
Expected: each fixture reports all pad layouts bit-identical. Record any fixture that fails (that is residual layout-dependence, not a re-baseline issue).

- [ ] **Step 2: 64-bit vs 32-bit golden diff, per fixture**

Compare the 64-bit render against the existing (32-bit) golden reference:
```bash
for fx in cordae highsiderr icoustic; do
  echo "== $fx =="
  BUILD_DIR=$PWD/build-sim-x64 FIXTURE=$fx scripts/golden_mixdown.sh check || echo "  DIFFERS from 32-bit golden"
done
```
Record, per fixture: bit-identical to the 32-bit golden, or differs.

- [ ] **Step 3: Characterize and write the verdict**

Write a short section appended to `docs/superpowers/specs/2026-07-04-host-sim-64bit-apple-silicon-design.md` under a new "## Phase 1 result" heading, stating for each fixture: padsweep pass/fail, and 32-bit-golden match/differ. Classify the overall outcome:
- **All padsweep-clean AND bit-identical to 32-bit** → 64-bit is a drop-in; replace-or-coexist is a free choice.
- **All padsweep-clean but differs from 32-bit** → consistent-but-shifted (expected from struct-size changes); coexist keeps 32-bit as golden, or replace with a re-baseline.
- **Any padsweep failure** → residual layout-dependence; list the fixture(s) for follow-up before any decision.

- [ ] **Step 4: Commit the verdict**

```bash
git add docs/superpowers/specs/2026-07-04-host-sim-64bit-apple-silicon-design.md
git commit -m "docs(64-bit): record Phase 1 golden-stability verdict"
```

- [ ] **Step 5: Return to the user with the decision gate**

Present the per-fixture results and recommend replace-vs-coexist. Do not proceed to Phase 2 (Apple Silicon/Darwin) until the user decides.

---

## Self-Review

**Spec coverage:** Every Phase-1 spec item maps to a task — the 80 truncations (Tasks 1–3), residual non-truncation errors (Task 4 Step 1 + the `std::min` cascade in Task 3 Step 4), build-clean under clang+gcc and runs (Task 4), no-regression on 32-bit sim + firmware + qemu specs (Tasks 2/3 firmware gate + Task 4 Steps 4–5), golden measurement + decision gate (Task 5). Phase 2 is intentionally out of scope.

**Placeholder scan:** No TBD/TODO; every code step shows real before/after; the mechanical sites are enumerated by an exact build command rather than hand-waved.

**Type consistency:** `uintptr_t` used uniformly for address arithmetic; `alloc_metadata` field/key/sentinels/`hashAddress` all move to `uintptr_t` together; `numSamplesFeedingNow` stays `uint32_t` so `std::min(…, 256u)` type-checks.
