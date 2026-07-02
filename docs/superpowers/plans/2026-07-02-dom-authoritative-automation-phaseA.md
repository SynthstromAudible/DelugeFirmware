# DOM-authoritative automation — Phase A (retire the ParamNode wrapper) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire ParamNode's use of the `OrderedPosVector` wrapper — migrate `ParamNodeVector`'s ~160 call-sites onto a `fast_vector<ParamNode>` plus free ordered-helper functions in the DOM layer — behaviour-preservingly, so a later phase can make the DOM the single owner of that vector.

**Architecture:** `ParamNodeVector = OrderedPosVector<ParamNode>` is already `fast_vector<ParamNode>` (a pool-allocated `std::vector`) plus a thin ordered-helper skin. Phase A introduces free functions (`deluge::project::param_node_ops`) that provide those ordered ops over `fast_vector<ParamNode>&`, moves every call site onto them via a *delegating seam* (so migration is incremental and bisectable), then flips the engine's node storage type from `ParamNodeVector` to `fast_vector<ParamNode>` in one small final step. Native vector ops (`begin/end/erase/empty/clear/swap/size/operator[]`) are unchanged throughout — `fast_vector` is a `std::vector`, so those sites already work. The `OrderedPosVector` template stays intact for its four other users (`NoteVector`, `NoteRowVector`, `ClipInstanceVector`, `MIDIParamVector`).

**Tech Stack:** C++23/26 firmware; sim build (`build-sim`, host g++ `-m32`); golden round-trip harness (`tests/golden/songs_run.py`, target `song_roundtrip`); dual-arch projection sim test (`tests/projection/param_automation_test.cpp`, target `param_projection_test`); ARM firmware via `./dbt build Debug`.

## Global Constraints

- New/modified C++ is idiomatic modern C++23/26 (enum class, `std::span`/`optional`, designated init; no C-casts/goto). Behaviour-preserving changes stay faithful but still modern. (verbatim from spec §10)
- Never add `Co-Authored-By` / "Generated with Claude" trailers to commits. (verbatim from spec §10)
- Commit freely at natural stopping points (branch `deluge-project-param`, not main). (verbatim from spec §10)
- **Phase A is behaviour-preserving. The golden is the oracle: it MUST stay `OK=15 REGRESS=0` at every task boundary. Any change is a bug.**
- clang-format pre-commit hook may reformat and abort the commit (rc=1); re-run `git add -A && git commit`.
- Rebuild sim targets before running the golden or it tests stale code.

**Verification recipe (used verbatim by every task's verify step, referred to below as `VERIFY`):**

```bash
./dbt build Debug   # 0 errors (ARM firmware)
cmake --build build-sim --target song_roundtrip param_projection_test \
  && ./build-sim/param_projection_test \
  && DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" \
     python3 tests/golden/songs_run.py --binary ./build-sim/song_roundtrip
```

Expected: firmware builds with 0 errors; `param_automation projection: OK`; golden `OK=15 REGRESS=0`.

---

## File Structure

- **Create** `src/deluge-project/param_node_ops.h` / `.cpp` — free ordered-helper functions for the
  engine's ParamNode vector. During the migration (Tasks A1–A3) they take `ParamNodeVector&` and
  delegate to `OrderedPosVector`'s methods (pure pass-throughs — the seam). At the flip (Task A4)
  their signatures become `fast_vector<ParamNode>&` and their bodies are re-expressed with `<algorithm>`
  directly. Namespace `deluge::project`.
- **Modify** `src/deluge/modulation/automation/auto_param.{h,cpp}` — call sites → free fns (A2); at A4,
  `AutoParamState::nodes` and `AutoParam::ownNodes_` retype to `fast_vector<ParamNode>`, `nodes()`
  returns `fast_vector<ParamNode>&`.
- **Modify** `src/deluge/model/consequence/consequence_param_change.cpp`,
  `src/deluge/model/action/action.cpp`, `src/deluge/model/clip/instrument_clip.cpp`,
  `src/deluge/modulation/patch/patch_cable.cpp`,
  `src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp` — call sites → free fns (A3).
- **Modify** `src/deluge/modulation/params/param_node_vector.h` — at A4, drop the `ParamNodeVector`
  alias if unused (or keep as `using ParamNodeVector = fast_vector<ParamNode>;` if any site still
  spells the type name).
- **Test** `tests/projection/param_automation_test.cpp` — extend with a `param_node_ops` unit test
  (A1); it already builds into `param_projection_test`.

**The mechanical transformation rule (applies to every call-site task A2/A3):**
For each *non-native* op, rewrite `V.op(args)` → `deluge::project::op(V, args)`, where `V` is the node
vector expression (`nodes()`, `state.nodes`, `param->nodes()`, etc.). **Leave native ops untouched:**
`begin end erase empty clear swap size operator[] front back data` already exist on `fast_vector`.
Non-native ops to convert: `firstAtOrAfter insertAt insertSorted reserveExtra tryGet tryGetLast
find_key lower_bound searchDual searchMultiple repositionElement deleteAtKey shiftHorizontal
generateRepeats testSequentiality init cloneFrom beenCloned`.

---

## Task A1: Free ordered-helper seam + unit test

**Files:**
- Create: `src/deluge-project/param_node_ops.h`, `src/deluge-project/param_node_ops.cpp`
- Modify: `tests/projection/param_automation_test.cpp`
- Modify: `src/deluge-project/CMakeLists.txt` (add `param_node_ops.cpp` if the dir lists sources explicitly)

**Interfaces:**
- Consumes: `ParamNodeVector` (`modulation/params/param_node_vector.h`), `ParamNode`.
- Produces: in namespace `deluge::project`, one free function per non-native op, each taking
  `ParamNodeVector& v` as the first parameter and forwarding to the corresponding `v.method(...)`.
  End-state (A4) will re-declare the same names over `fast_vector<ParamNode>&` with identical
  behaviour. Names/signatures (delegating bodies shown for the representative subset; the rest follow
  the identical one-line forward-to-method pattern):

```cpp
// param_node_ops.h
#pragma once
#include "modulation/params/param_node_vector.h" // ParamNodeVector, ParamNode
#include "util/functions.h"
#include <cstdint>

namespace deluge::project {

[[nodiscard]] int32_t firstAtOrAfter(ParamNodeVector& v, int32_t key, int32_t searchFrom = 0);
[[nodiscard]] std::expected<int32_t, Error> insertSorted(ParamNodeVector& v, int32_t key,
                                                         bool isDefinitelyLast = false);
[[nodiscard]] std::expected<void, Error> insertAt(ParamNodeVector& v, int32_t i, int32_t numToInsert = 1);
[[nodiscard]] std::expected<void, Error> reserveExtra(ParamNodeVector& v, int32_t numAdditional);
[[nodiscard]] ParamNode* tryGet(ParamNodeVector& v, int32_t index);
[[nodiscard]] ParamNode* tryGetLast(ParamNodeVector& v);
void searchDual(const ParamNodeVector& v, int32_t const* searchTerms, int32_t* resultingIndexes);
void shiftHorizontal(ParamNodeVector& v, int32_t amount, int32_t effectiveLength);
[[nodiscard]] bool generateRepeats(ParamNodeVector& v, int32_t wrapPoint, int32_t endPos);
void testSequentiality(const ParamNodeVector& v, char const* errorCode);
void init(ParamNodeVector& v);
[[nodiscard]] bool cloneFrom(ParamNodeVector& dst, ParamNodeVector const* src);
[[nodiscard]] Error beenCloned(ParamNodeVector& v);
// ... add one forwarder per remaining non-native op the compiler flags in A2/A3
//     (find_key, lower_bound, searchMultiple, repositionElement, deleteAtKey) — same pattern.

} // namespace deluge::project
```

```cpp
// param_node_ops.cpp  (delegating bodies — the seam; each is a one-line forward)
#include "deluge-project/param_node_ops.h"

namespace deluge::project {

int32_t firstAtOrAfter(ParamNodeVector& v, int32_t key, int32_t searchFrom) {
	return v.firstAtOrAfter(key, searchFrom);
}
std::expected<int32_t, Error> insertSorted(ParamNodeVector& v, int32_t key, bool isDefinitelyLast) {
	return v.insertSorted(key, isDefinitelyLast);
}
std::expected<void, Error> insertAt(ParamNodeVector& v, int32_t i, int32_t numToInsert) {
	return v.insertAt(i, numToInsert);
}
std::expected<void, Error> reserveExtra(ParamNodeVector& v, int32_t n) { return v.reserveExtra(n); }
ParamNode* tryGet(ParamNodeVector& v, int32_t index) { return v.tryGet(index); }
ParamNode* tryGetLast(ParamNodeVector& v) { return v.tryGetLast(); }
void searchDual(const ParamNodeVector& v, int32_t const* s, int32_t* r) { v.searchDual(s, r); }
void shiftHorizontal(ParamNodeVector& v, int32_t a, int32_t len) { v.shiftHorizontal(a, len); }
bool generateRepeats(ParamNodeVector& v, int32_t wrap, int32_t end) { return v.generateRepeats(wrap, end); }
void testSequentiality(const ParamNodeVector& v, char const* code) { v.testSequentiality(code); }
void init(ParamNodeVector& v) { v.init(); }
bool cloneFrom(ParamNodeVector& dst, ParamNodeVector const* src) { return dst.cloneFrom(src); }
Error beenCloned(ParamNodeVector& v) { return v.beenCloned(); }

} // namespace deluge::project
```

- [ ] **Step 1: Write the failing test.** Append to `tests/projection/param_automation_test.cpp` a
  `test_param_node_ops()` that builds a `ParamNodeVector`, exercises the free fns, and asserts they
  match direct method calls on a second identical vector:

```cpp
static void test_param_node_ops() {
	ParamNodeVector viaFree, viaMethod;
	for (int32_t pos : {0, 16, 48, 96}) {
		int32_t k1 = deluge::project::insertSorted(viaFree, pos).value();
		viaFree[k1].pos = pos;   // insertSorted reserves the slot; caller sets the key + value
		viaFree[k1].value = pos * 10;
		int32_t k2 = viaMethod.insertSorted(pos).value();
		viaMethod[k2].pos = pos;
		viaMethod[k2].value = pos * 10;
		ASSERT_EQ(k1, k2);
	}
	ASSERT_EQ(deluge::project::firstAtOrAfter(viaFree, 40), viaMethod.firstAtOrAfter(40));
	ASSERT_EQ(deluge::project::tryGet(viaFree, 2)->value, viaMethod.tryGet(2)->value);
	deluge::project::shiftHorizontal(viaFree, 8, 128);
	viaMethod.shiftHorizontal(8, 128);
	ASSERT_EQ(viaFree.size(), viaMethod.size());
	for (size_t i = 0; i < viaFree.size(); i++) {
		ASSERT_EQ(viaFree[i].pos, viaMethod[i].pos);
		ASSERT_EQ(viaFree[i].value, viaMethod[i].value);
	}
	printf("param_node_ops: OK\n");
}
```

  Wire `test_param_node_ops()` into the test's `main()` alongside the existing calls. (Use the file's
  existing assert macro / print idiom — match `test_param_automation`'s style, don't introduce a new
  framework.)

- [ ] **Step 2: Run it to verify it fails to compile** (`param_node_ops.h` doesn't exist yet).
  Run: `cmake --build build-sim --target param_projection_test`
  Expected: FAIL — `fatal error: deluge-project/param_node_ops.h: No such file`.

- [ ] **Step 3: Create `param_node_ops.h` and `.cpp`** with the declarations + delegating bodies shown
  above. Add `param_node_ops.cpp` to the deluge-project source list if `src/deluge-project/CMakeLists.txt`
  enumerates sources (check; some dirs glob).

- [ ] **Step 4: Run the test to verify it passes.**
  Run: `cmake --build build-sim --target param_projection_test && ./build-sim/param_projection_test`
  Expected: PASS — `param_node_ops: OK` and `param_automation projection: OK`.

- [ ] **Step 5: `VERIFY`** (full recipe — ARM build + golden must stay `OK=15`; nothing engine-side
  changed yet, so this is a smoke check that the new TU links into firmware too).

- [ ] **Step 6: Commit.**

```bash
git add -A && git commit -m "param: free ordered-helper seam over ParamNodeVector (Phase A / A1)"
```

---

## Task A2: Migrate `auto_param.cpp` call sites onto the seam

**Files:**
- Modify: `src/deluge/modulation/automation/auto_param.cpp` (the bulk — ~136 non-native sites)
- Modify: `src/deluge/modulation/automation/auto_param.h` (add `#include "deluge-project/param_node_ops.h"`)

**Interfaces:**
- Consumes: `deluge::project::*` free fns from A1 (delegating over `ParamNodeVector&`).
- Produces: an `auto_param.cpp` whose every *non-native* node op goes through the free fns; native ops
  unchanged. Behaviour identical (delegation).

Apply the mechanical transformation rule. Split into a few bisectable commits so each is reviewable and
each keeps the golden green — suggested grouping (each its own commit): (a) `firstAtOrAfter` +
`insertSorted` + `insertAt`; (b) `tryGet` + `tryGetLast` + `searchDual`; (c) `shiftHorizontal` +
`generateRepeats` + `reserveExtra` + `testSequentiality`; (d) `init` + `cloneFrom` + `beenCloned` +
any stragglers the compiler flags. Representative transformation:

```cpp
// before
int32_t i = nodes().firstAtOrAfter(pos);
ParamNode* node = nodes().tryGet(i);
nodes().shiftHorizontal(amount, effectiveLength);
// after
int32_t i = deluge::project::firstAtOrAfter(nodes(), pos);
ParamNode* node = deluge::project::tryGet(nodes(), i);
deluge::project::shiftHorizontal(nodes(), amount, effectiveLength);
```

- [ ] **Step 1: Add the include** to `auto_param.h` (or `.cpp` if that's where the ops are used):
  `#include "deluge-project/param_node_ops.h"`.

- [ ] **Step 2: Convert group (a)** — every `nodes().firstAtOrAfter(...)`, `.insertSorted(...)`,
  `.insertAt(...)` → `deluge::project::…(nodes(), …)`. Handle `std::expected` return sites unchanged
  (the free fn returns the same `std::expected`).

- [ ] **Step 3: Build + verify group (a).** Run `VERIFY`. Expected: 0 errors, golden `OK=15`. Commit:
  `git add -A && git commit -m "param: route auto_param search/insert ops through the seam (Phase A / A2a)"`

- [ ] **Step 4: Convert group (b)** (`tryGet`/`tryGetLast`/`searchDual`), `VERIFY`, commit
  (`…A2b`).

- [ ] **Step 5: Convert group (c)** (`shiftHorizontal`/`generateRepeats`/`reserveExtra`/`testSequentiality`),
  `VERIFY`, commit (`…A2c`).

- [ ] **Step 6: Convert group (d)** (`init`/`cloneFrom`/`beenCloned` + any remaining non-native op the
  compiler flags). `VERIFY`, commit (`…A2d`).

- [ ] **Step 7: Completeness check.** Confirm no non-native method calls remain in the file:
  Run: `grep -nE "nodes\(\)\.(firstAtOrAfter|insertSorted|insertAt|reserveExtra|tryGet|tryGetLast|find_key|lower_bound|searchDual|searchMultiple|repositionElement|deleteAtKey|shiftHorizontal|generateRepeats|testSequentiality|init|cloneFrom|beenCloned)" src/deluge/modulation/automation/auto_param.cpp`
  Expected: no output (all migrated). Native ops (`begin/erase/empty/clear/swap/…`) legitimately remain.

---

## Task A3: Migrate the remaining 5 files onto the seam

**Files:**
- Modify: `src/deluge/model/consequence/consequence_param_change.cpp` (`state.nodes.*`)
- Modify: `src/deluge/model/action/action.cpp`
- Modify: `src/deluge/model/clip/instrument_clip.cpp`
- Modify: `src/deluge/modulation/patch/patch_cable.cpp`
- Modify: `src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp`

**Interfaces:**
- Consumes: `deluge::project::*` free fns (A1).
- Produces: all non-native ParamNode-vector ops across these files routed through the seam.

- [ ] **Step 1: Add `#include "deluge-project/param_node_ops.h"`** to each of the 5 files that uses a
  non-native op.

- [ ] **Step 2: Apply the transformation rule** in each file. The node-vector expression varies
  (`state.nodes`, a local `ParamNodeVector&`, `param->nodes()`); the rule is the same:
  `EXPR.op(args)` → `deluge::project::op(EXPR, args)` for non-native ops. Note
  `consequence_param_change.cpp` uses `.swap` (native — leave it) and `.cloneFrom` (non-native →
  `deluge::project::cloneFrom(state.nodes, …)`).

- [ ] **Step 3: `VERIFY`.** Expected: 0 errors, golden `OK=15`.

- [ ] **Step 4: Completeness check across all five files:**
  Run: `grep -rnE "\.(firstAtOrAfter|insertSorted|insertAt|reserveExtra|tryGet|tryGetLast|find_key|lower_bound|searchDual|searchMultiple|repositionElement|deleteAtKey|shiftHorizontal|generateRepeats|testSequentiality|cloneFrom|beenCloned)\(" src/deluge/model/consequence/consequence_param_change.cpp src/deluge/model/action/action.cpp src/deluge/model/clip/instrument_clip.cpp src/deluge/modulation/patch/patch_cable.cpp src/deluge/gui/views/automation/editor_layout/mod_controllable.cpp`
  Expected: no output for ParamNode vectors. (If a `Note`/`NoteRow`/`MIDIParam` vector legitimately
  calls a method here, it stays — those types keep `OrderedPosVector`; verify by the receiver's type.)

- [ ] **Step 5: Commit.**
  `git add -A && git commit -m "param: route remaining ParamNode-vector call sites through the seam (Phase A / A3)"`

---

## Task A4: The flip — retype engine node storage to `fast_vector<ParamNode>`

**Files:**
- Modify: `src/deluge/modulation/automation/auto_param.h` (`AutoParamState::nodes`, `AutoParam::ownNodes_`, `nodes()` return type)
- Modify: `src/deluge-project/param_node_ops.h` / `.cpp` (signatures → `fast_vector<ParamNode>&`; bodies re-expressed with `<algorithm>`)
- Modify: `src/deluge/modulation/params/param_node_vector.h` (retire/alias)
- Modify: `tests/projection/param_automation_test.cpp` (the A1 unit test now builds a `fast_vector<ParamNode>`)

**Interfaces:**
- Consumes: all call sites now go through `deluge::project::*` (A2/A3), so they are agnostic to the
  vector's concrete type.
- Produces: engine automation node storage is `fast_vector<ParamNode>`; `nodes()` returns
  `fast_vector<ParamNode>&`; `deluge::project::*` operate on `fast_vector<ParamNode>&` directly (no
  `OrderedPosVector`). The `OrderedPosVector` template is untouched for its 4 other users.

This is the atomic-but-small flip. Because every call site already goes through the seam, only the
type declarations + the helper *bodies* change; call sites do not.

- [ ] **Step 1: Retype the engine storage.** In `auto_param.h`: `AutoParamState::nodes` →
  `deluge::fast_vector<ParamNode> nodes{};`; `AutoParam::ownNodes_` →
  `deluge::fast_vector<ParamNode> ownNodes_{};`; `nodes()` / `const nodes()` return
  `deluge::fast_vector<ParamNode>&`. Replace `#include "modulation/params/param_node_vector.h"` with
  the `fast_vector` include (`#include "util/containers.h"`) if needed.

- [ ] **Step 2: Re-express the helper bodies** in `param_node_ops.cpp` over `fast_vector<ParamNode>&`,
  preserving exact semantics. These mirror `OrderedPosVector`'s implementations (which are themselves
  `std::ranges` over the same backing). Show-through of the non-trivial ones:

```cpp
int32_t firstAtOrAfter(fast_vector<ParamNode>& v, int32_t key, int32_t searchFrom) {
	auto it = std::ranges::lower_bound(v.begin() + searchFrom, v.end(), key, {}, &ParamNode::pos);
	return static_cast<int32_t>(it - v.begin());
}
ParamNode* tryGet(fast_vector<ParamNode>& v, int32_t index) {
	return (index < 0 || static_cast<size_t>(index) >= v.size()) ? nullptr : &v[index];
}
void init(fast_vector<ParamNode>& v) { new (&v) fast_vector<ParamNode>(); }        // byte-copy idiom reset
bool cloneFrom(fast_vector<ParamNode>& dst, fast_vector<ParamNode> const* src) {
	new (&dst) fast_vector<ParamNode>(*src);   // copy-construct over the byte-copied alias (deep copy)
	return true;
}
// insertSorted/insertAt/reserveExtra/searchDual/shiftHorizontal/generateRepeats/beenCloned/
// testSequentiality: port each verbatim from the corresponding OrderedPosVector method body,
// substituting `v` for `elements_`. (These already exist there; copy them across unchanged.)
```

  Update the `.h` signatures to `fast_vector<ParamNode>&` to match.

- [ ] **Step 3: Retire the alias.** In `param_node_vector.h`, if no site still spells the type name,
  delete the file's alias (and the include from `auto_param.h`); otherwise set
  `using ParamNodeVector = deluge::fast_vector<ParamNode>;` so any lingering spelling still resolves.
  Confirm the `OrderedPosVector<ParamNode>` instantiation has no remaining users:
  Run: `grep -rn "OrderedPosVector<ParamNode>\|ParamNodeVector" src/deluge/ | grep -v "note\|clip_instance\|midi_param"`
  Expected: only the alias line itself (or nothing).

- [ ] **Step 4: Update the A1 unit test** to build `deluge::fast_vector<ParamNode>` instead of
  `ParamNodeVector` (the free fns now take that type). Its assertions are unchanged.

- [ ] **Step 5: `VERIFY`.** Expected: firmware 0 errors; `param_node_ops: OK`; `param_automation
  projection: OK`; golden `OK=15 REGRESS=0`. **This is the critical gate — the flip is behaviour-
  preserving, so any golden delta is a bug (bisect the helper bodies against the OrderedPosVector
  originals).**

- [ ] **Step 6: Commit.**
  `git add -A && git commit -m "param: flip engine automation storage to fast_vector<ParamNode>; retire the wrapper (Phase A / A4)"`

---

## Phase A self-check (run after A4)

- [ ] Full ARM golden at the phase boundary (per `golden-arm-runs-targeted` memory — full sweep only at
  phase boundaries): run the golden with the ARM oracle over the kernel-relevant fixtures; confirm
  `x86 ≡ ARM` bit-exact.
- [ ] `grep -rn "OrderedPosVector<ParamNode>" src/deluge/` → empty (wrapper retired for ParamNode).
- [ ] `OrderedPosVector` still compiles for `NoteVector`/`NoteRowVector`/`ClipInstanceVector`/
  `MIDIParamVector` (covered by the firmware build).

---

## Subsequent phases (B–E) — roadmap, planned separately

Phase A is a complete, CI-verifiable deliverable on its own (behaviour-preserving; the engine now
stores automation nodes in a bare `fast_vector<ParamNode>`). Phases B–E make the DOM the owner of that
vector and are **hardware-gated and golden-blind for edit behaviour**, so they get their own plan once
Phase A lands and the Phase B hardware gate is scoped. Per spec §6:

- **Phase B** — DOM owns the store at a stable address; `AutoParam` gains `fast_vector<ParamNode>* nodes_`
  + `borrowNodeStorage`; song-global params borrow-point at their DOM store; `DocAutoParam::nodes` →
  `fast_vector<ParamNode>`. Undo becomes DOM-correct with no code change. **Hardware-gated.**
- **Phase C** — clips (AudioClip + InstrumentClip: unpatched + patched + patch-cables); per-clip DOM
  store + clone/steal/delete lifecycle wiring. **Hardware-gated.**
- **Phase D** — flip `save` to read the DOM store (byte-identical; golden `OK=15`).
- **Phase E** — NoteRows / kit drums (per-owner DOM representation + wiring; no architectural change).
  **Hardware-gated.**

Out of scope (spec §9): sequencer-container modernization (retiring the `Note`/`NoteRow`/`ClipInstance`/
`MIDIParam` byte-copy relocation idiom) — a separate later north-star.
