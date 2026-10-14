# Sequencer clone-idiom retirement — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the legacy byte-copy clone idiom (`init`/`cloneFrom`/`beenCloned` + raw `memcpy` of owning objects) with proper C++ copy/clone semantics across all `OrderedPosVector` users and the `ParamManager`/`ParamCollection` clone chain, and do a light idiomatic pass on `OrderedPosVector`.

**Architecture:** Bottom-up real copy semantics. `OrderedPosVector` gains a real copy constructor (element-wise, not memcpy); `AutoParam`, `MIDIParam`, `NoteRow` gain deep-copy constructors; the polymorphic `ParamCollection` hierarchy gains a virtual `clone()` that replaces the type-erased `memcpy` in `ParamManager`. The clone path is golden-blind, so a dedicated clone/undo sim harness is built **first** as the safety net. See `docs/superpowers/specs/2026-07-02-sequencer-clone-idiom-retirement-design.md`.

**Tech Stack:** C++23/26 firmware; sim build (`build-sim`, host g++ `-m32`); golden round-trip harness (`tests/golden/songs_run.py`, target `song_roundtrip`); projection sim test (`param_projection_test`); new clone/undo sim test (`param_clone_test`); ARM firmware via `./dbt build Debug`.

## Global Constraints

- New/modified C++ is idiomatic modern C++23/26 (enum class, `std::span`/`optional`/`expected`, designated init; no C-casts/goto). Behaviour-preserving changes stay faithful but modern.
- Never add `Co-Authored-By` / "Generated with Claude" trailers to commits.
- Commit freely at natural stopping points (branch `deluge-project-param`, not main).
- **Behaviour-preserving throughout.** The clone/undo harness (A0) is the primary oracle for clone semantics; the golden guards the load/save path (`OK=15 REGRESS=0`) at every task boundary; `param_projection_test` stays green.
- clang-format pre-commit hook may reformat and abort the commit (rc=1); re-run `git add -A && git commit`.
- Rebuild sim targets before running the golden.
- **HARDWARE GATE (mandatory before merge):** clip duplicate, kit-row add/remove, undo/redo across clones, save→reload — the golden cannot see these.

**Verification recipe (referred to below as `VERIFY`):**

```bash
./dbt build Debug   # 0 errors (ARM firmware)
cmake --build build-sim --target song_roundtrip param_projection_test param_clone_test \
  && ./build-sim/param_clone_test && ./build-sim/param_projection_test \
  && DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" \
     python3 tests/golden/songs_run.py --binary ./build-sim/song_roundtrip
```

Expected: firmware 0 errors; `param_clone: OK`; `param_automation projection: OK`; golden `OK=15 REGRESS=0`.

---

## File Structure

- **Create** `tests/projection/param_clone_test.cpp` — the clone/undo deep-independence harness (A0). Mirrors `param_automation_test.cpp`'s sim setup (constructs real `AutoParam`/`ParamManager` against the host allocator; runs on x86 + ARM/qemu).
- **Modify** `sim/CMakeLists.txt` — add the `param_clone_test` target (mirror `param_projection_test`).
- **Modify** `src/deluge/util/container/ordered_pos_vector.h` — light idiomatic pass (A1); real copy ctor (A2); remove `init`/`cloneFrom`/`beenCloned` (A4).
- **Modify** `src/deluge/modulation/automation/auto_param.{h,cpp}` — copy ctor + named reverse-clone (A2); drop `init/beenCloned/cloneFrom` usage (A4).
- **Modify** `src/deluge/modulation/midi/midi_param.h`, `midi/midi_param_collection.{h,cpp}` — `MIDIParam` copyable, `MIDIParamCollection::clone` (A2/A3).
- **Modify** `src/deluge/model/note/note_row.{h,cpp}`, `note/note_row_vector.cpp` — `NoteRow` copy ctor + named reverse-clone (A2); replace `cloneFrom` memcpy (A3).
- **Modify** `src/deluge/modulation/params/param_collection.h`, `param_set.{h,cpp}`, `patch/patch_cable_set.{h,cpp}` — virtual `clone()` (A3).
- **Modify** `src/deluge/modulation/params/param_manager.{h,cpp}` — replace the polymorphic `memcpy` (`:217`) with virtual `clone()` dispatch (A3).

**Note on incremental safety:** `OrderedPosVector` copy is currently `= delete`d to stop byte-copy and real copy being mixed. Enabling it (A2) must land together with converting that type's byte-copy callers, or the two idioms coexist unsafely. Keep `init`/`beenCloned`/`cloneFrom` present until A4 so un-converted callers still build; convert callers bottom-up in between.

---

## Task A0: Clone/undo deep-independence harness (the golden-blind safety net)

**Files:**
- Create: `tests/projection/param_clone_test.cpp`
- Modify: `sim/CMakeLists.txt`

**Interfaces:**
- Consumes: real `AutoParam`, `ParamManager`, `UnpatchedParamSet`, `PatchCableSet` (as constructed in `param_automation_test.cpp`).
- Produces: executable `param_clone_test`, exit 0 on success, prints `param_clone: OK`.

- [ ] **Step 1: Add the CMake target.** In `sim/CMakeLists.txt`, copy the `param_projection_test` target block to a `param_clone_test` block pointing at `tests/projection/param_clone_test.cpp` (same libs/includes).

- [ ] **Step 2: Write the harness** asserting deep-copy independence against the *current* (byte-copy) clone paths, so it is green before any change and guards the refactor. Mirror `param_automation_test.cpp`'s includes + `check()`/`failures` idiom and its `_exit(0/1)` tail.

```cpp
// param_clone_test.cpp — asserts clones are deep-independent of their sources.
// (a) OrderedPosVector<ParamNode> clone
{
	AutoParam src;
	setNode(src, 0, 111, false);      // helper as in param_automation_test.cpp
	setNode(src, 48, -222, true);
	AutoParam dst;
	dst.cloneFrom(&src, /*copyAutomation=*/true); // current clone path (A4 makes this a copy ctor)
	check(dst.nodes().size() == src.nodes().size(), "clone: node count copied");
	src.nodes()[0].value = 999;       // mutate source
	check(dst.nodes()[0].value == 111, "clone: dst independent of src mutation");
}
// (b) ParamManager clone (exercises the polymorphic collection memcpy @param_manager.cpp:217)
{
	ParamManagerForTimeline pm; /* setUpUnpatched + init as in projection test */
	// ... set an unpatched param value + a node ...
	ParamManagerForTimeline clone;
	clone.cloneParamCollectionsFrom(&pm, /*copyAutomation=*/true, /*...*/);
	// mutate pm's unpatched AutoParam, assert clone's is unchanged
	check(/* clone value != mutated pm value */ true, "clone: ParamManager collections deep-copied");
}
// (c) undo swap round-trip (already clean — regression guard)
{
	// ConsequenceParamChange-style: snapshot nodes via swap, mutate, swap back, assert restored.
}
```

  (Fill the `ParamManager` setup from the exact idiom `param_automation_test.cpp` already uses to build a manager + unpatched set; reuse its `setNode` helper by copying it in.)

- [ ] **Step 3: Build + run — expect PASS on current code.**
  Run: `cmake --build build-sim --target param_clone_test && ./build-sim/param_clone_test`
  Expected: `param_clone: OK` (the current byte-copy paths already produce deep copies for these types).

- [ ] **Step 4: `VERIFY`** (harness + projection + golden all green; nothing engine-side changed).

- [ ] **Step 5: Commit.** `git add -A && git commit -m "test: clone/undo deep-independence harness (clone-idiom retirement / A0)"`

---

## Task A1: Light idiomatic pass on `OrderedPosVector`

**Files:**
- Modify: `src/deluge/util/container/ordered_pos_vector.h`

**Interfaces:**
- Produces: same public surface, more idiomatic. No behaviour change.

- [ ] **Step 1: Constrain the template.** Add a `requires` clause that `keyMember` is a pointer-to-member yielding an integral key:

```cpp
template <typename T, auto keyMember = &T::pos>
	requires std::is_member_object_pointer_v<decltype(keyMember)>
class OrderedPosVector {
```

- [ ] **Step 2: Add a `std::span` view accessor** (alongside `begin/end`):

```cpp
[[nodiscard]] std::span<T> span() { return {elements_.data(), elements_.size()}; }
[[nodiscard]] std::span<T const> span() const { return {elements_.data(), elements_.size()}; }
```
(add `#include <span>`).

- [ ] **Step 3: Replace `__restrict__`** (lines 140, 150) with `[[gnu::restrict]]` on the pointer params, and add `[[nodiscard]]` to any pure query still missing it (`find_key`, `lower_bound` already have it; check `size`/`empty` — they have it).

- [ ] **Step 4: `VERIFY`.** Expected: 0 errors; all three test binaries green; golden `OK=15`. (Additive-only; behaviour identical.)

- [ ] **Step 5: Commit.** `git add -A && git commit -m "container: idiomatic pass on OrderedPosVector (requires-clause, std::span, [[gnu::restrict]]) (A1)"`

---

## Task A2: Bottom-up real copy semantics (leaf types)

**Files:**
- Modify: `src/deluge/util/container/ordered_pos_vector.h`, `modulation/automation/auto_param.{h,cpp}`, `modulation/midi/midi_param.h`, `model/note/note_row.{h,cpp}`

**Interfaces:**
- Produces: `OrderedPosVector<T>` copyable (element-wise deep copy); `AutoParam`, `MIDIParam`, `NoteRow` copyable; reverse-clone available as a named method. `init`/`cloneFrom`/`beenCloned` remain (removed in A4).

- [ ] **Step 1: Real copy ctor on `OrderedPosVector`.** Replace the deleted copy with a defined one that element-wise deep-copies (valid for non-trivially-copyable `T`):

```cpp
OrderedPosVector(OrderedPosVector const& other) { elements_ = other.elements_; } // std::vector deep copy
OrderedPosVector& operator=(OrderedPosVector const& other) { elements_ = other.elements_; return *this; }
```
(This makes `T` require copyability; `ParamNode`/`Note`/`ClipInstance` are trivially copyable, `MIDIParam` becomes copyable in Step 3. Keep `cloneFrom`/`init`/`beenCloned` for now.)

- [ ] **Step 2: `VERIFY`** — the copy ctor is additive; nothing calls it yet. Green. Commit (`…A2a: OrderedPosVector copy ctor`).

- [ ] **Step 3: `MIDIParam` copyable.** In `midi_param.h`, give `MIDIParam` a defaulted-or-defined copy that copies its `cc` and deep-copies its embedded `AutoParam` (which becomes copyable in Step 4 — order Step 4 before this if the compiler requires). `VERIFY`, commit (`…A2b`).

- [ ] **Step 4: `AutoParam` copy ctor** deep-copying `ownNodes_` (via the OrderedPosVector copy ctor) + the scalar/interpolation state; keep the reverse-clone as a named method `Error cloneReversedFrom(AutoParam const&, int32_t reverseWithLength)` carved out of the current `beenCloned` reverse branch (auto_param.cpp:1566-1609). `VERIFY`, commit (`…A2c`).

- [ ] **Step 5: `NoteRow` copy ctor** deep-copying `paramManager` + `notes` + clearing the owned linked-list/pointer members (mirror the non-reverse body of `NoteRow::beenCloned`, note_row.cpp:86-202); reverse-clone stays a named method. `VERIFY`, commit (`…A2d`).

---

## Task A3: The keystone — virtual `ParamCollection::clone()` + retire the polymorphic memcpy

**Files:**
- Modify: `modulation/params/param_collection.h`, `param_set.{h,cpp}`, `midi/midi_param_collection.{h,cpp}`, `patch/patch_cable_set.{h,cpp}`, `params/param_manager.{h,cpp}`, `model/note/note_row_vector.cpp`

**Interfaces:**
- Consumes: the copy ctors from A2.
- Produces: `ParamCollection` has `virtual std::expected<ParamCollection*, Error> clone(/*args matching cloneParamCollectionsFrom*/) const = 0;` implemented by each subclass; `ParamManager::cloneParamCollectionsFrom` dispatches through it; `NoteRowVector::cloneFrom` uses copy construction.

- [ ] **Step 1: Declare the virtual.** Add a pure virtual `clone()` to `ParamCollection` (`param_collection.h`) returning a freshly-allocated deep copy (fallible), carrying the same `copyAutomation`/reverse args the current `beenCloned` chain takes.

- [ ] **Step 2: Implement per subclass** — `ParamSet` (loops its `AutoParam`s using the A2 copy ctor / reverse-clone), `MIDIParamCollection` (copies its `MIDIParamVector` + embedded AutoParams), `PatchCableSet` (copies `patchCables[]` AutoParams + allocates & deep-copies `destinations[]`). Each mirrors its existing `beenCloned` body but as a from-scratch deep copy rather than fix-up-after-memcpy. `VERIFY` after each subclass; commit per subclass (`…A3a/b/c`).

- [ ] **Step 3: Replace the polymorphic memcpy.** In `ParamManager::cloneParamCollectionsFrom` (param_manager.cpp ~193-220), replace the `memcpy(...objectSize)` + `beenCloned()` pair with `auto cloned = src->clone(...); if (!cloned) return err; summary->paramCollection = *cloned;`. `VERIFY` — **the A0 harness case (b) is the guard here.** Commit (`…A3d`).

- [ ] **Step 4: Replace `NoteRowVector::cloneFrom` memcpy** (note_row_vector.cpp:55) with element-wise copy construction of each `NoteRow` (via the A2 copy ctor). `VERIFY`, commit (`…A3e`).

---

## Task A4: Remove the idiom + audit destinations

**Files:**
- Modify: `src/deluge/util/container/ordered_pos_vector.h` and every former caller of `init`/`cloneFrom`/`beenCloned`; `patch/patch_cable_set.cpp`

**Interfaces:**
- Produces: no `init`/`cloneFrom`/`beenCloned` on `OrderedPosVector`; all clone paths use copy ctors / `clone()`.

- [ ] **Step 1: Delete `init`/`cloneFrom`/`beenCloned`** from `OrderedPosVector` and the `beenCloned` overrides on `NoteRow`/`AutoParam`/`ParamSet`/`MIDIParamCollection`/`PatchCableSet`/`ParamManager` now that all callers use copy ctors / `clone()`. Compiler-driven: each remaining caller is an error to convert.

- [ ] **Step 2: Audit `PatchCableSet` `Destination` deep-copy** (patch_cable_set.cpp:755-797): confirm `Destination` owns no heap beyond the `destinations[]` arrays already allocated; the from-scratch `clone()` in A3 must deep-copy those arrays (not shallow `*to = *from` a pointer). Fix if it aliases.

- [ ] **Step 3: Completeness check.**
  Run: `grep -rn "\.beenCloned()\|->beenCloned()\|\.cloneFrom(\|nodes()\.init()" src/deluge/ | grep -iE "nodes|param|note"`
  Expected: no `OrderedPosVector` idiom calls remain (unrelated `cloneFrom` on AudioOutput/arpSettings/sidechain/voiceSample are different methods — leave them).

- [ ] **Step 4: `VERIFY` + full ARM golden at the phase boundary** (per `golden-arm-runs-targeted`). Expected: 0 errors; `param_clone: OK`; `param_automation projection: OK`; golden `OK=15`, x86≡ARM.

- [ ] **Step 5: Commit.** `git add -A && git commit -m "container: remove byte-copy clone idiom; all clone paths use real copy/clone semantics (A4)"`

---

## Completion

- [ ] All tasks green; `param_clone_test` + `param_projection_test` pass on x86 **and** ARM/qemu; golden `OK=15 REGRESS=0`.
- [ ] **HARDWARE (mandatory, golden-blind):** flash and verify clip duplicate, kit-row add/remove, undo/redo across clones, save→reload — no corruption, deep independence holds.
- [ ] Only after this lands do we resume the deferred DOM automation store work (`2026-07-02-dom-authoritative-automation-single-store-design.md`, Phases B+), including the §4.2 correction.
