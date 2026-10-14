# Clone-Idiom Retirement — Entangled Tail Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish retiring the legacy byte-copy clone idiom (`init`/`cloneFrom`/`beenCloned` + polymorphic
`memcpy`) across the last three entangled users — PatchCableSet, the reverse-clone path, and NoteRow — then
delete the idiom entirely.

**Architecture:** The keystone (polymorphic `ParamCollection` memcpy at `param_manager.cpp:217`) is already
retired: `cloneParamCollectionsFrom` dispatches to a virtual `clone()`, and 4 of 5 collections deep-copy via
real copy construction (A3a–A3e, committed on `deluge-project-param`). This plan builds the shared reverse/
strip infrastructure the remaining `clone()` overrides need, converts PatchCableSet and NoteRow to real copy
semantics, and removes `beenCloned` + the OrderedPosVector idiom methods.

**Tech Stack:** C++23 (GCC 15.2.1 / dbt v23), `deluge::exception` (enum class) for leaf OOM, `Error`
factories for owner types, `deluge::memory::alloc_fast` + placement `new`.

## Global Constraints

- Idiomatic modern C++23 for all new code (no C-casts, no goto, `enum class`, designated init where it reads
  well). Behaviour-preserving: logic stays faithful.
- `deluge::exception` is an `enum class` — throw `deluge::exception::BAD_ALLOC`, catch by ref
  `catch (deluge::exception&)`.
- Fallibility split: **leaf value types** (`OrderedPosVector`, `AutoParam`, `MIDIParam`, `NoteRow`) get copy
  ctors that may **throw** on OOM. **Owner factories** (`ParamCollection::clone`, `NoteRowVector::cloneFrom`)
  **catch** those throws and return `Error` / `bool`.
- **Golden-blind path.** The golden (load→save) never exercises editing. The clone/undo path is exercised only
  by clip-duplicate / kit-row / undo. The A0 harness (`tests/projection/param_clone_test.cpp`) is the primary
  automated safety net; each behaviour-changing task extends it. **On-device testing is mandatory** before the
  branch is considered done (see Hardware Gate).
- clang-format pre-commit hook reformats and aborts the commit (rc=1) on touched files — re-run
  `git add -A && git commit`.
- clangd "cstddef not found" / "std undeclared" / "no matching constructor" diagnostics are environment noise;
  trust the real builds.

### VERIFY recipe (referenced throughout)

```bash
./dbt build Debug                                                            # ARM firmware
cmake --build build-sim --target song_roundtrip param_clone_test param_projection_test
./build-sim/param_clone_test        # expect: param_clone: OK
./build-sim/param_projection_test   # expect: param_automation projection: OK
DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" python3 tests/golden/songs_run.py \
    --binary ./build-sim/song_roundtrip                                      # expect: OK=15 REGRESS=0
```

The golden is behaviour-preserving-by-construction here (it never edits) — any REGRESS is a real bug.

---

## Task 1: Shared reverse / strip-automation infrastructure

Builds the pieces the `clone()` overrides need so they stop delegating the `!copyAutomation` and
`reverseDirectionWithLength != 0` cases to the legacy base `memcpy` path. No behaviour change yet — the new
methods are only wired into the existing 4 overrides at the end of this task.

**Files:**
- Modify: `src/deluge/modulation/automation/auto_param.h` / `.cpp` — add `flattenReverse`.
- Modify: `src/deluge/modulation/params/param_collection.h` / `.cpp` — add virtual `stripAutomation` +
  `flattenReverseAutomation` (base no-op).
- Modify: `src/deluge/modulation/params/param_set.h` / `.cpp` — override both (loop params).
- Modify: `src/deluge/modulation/midi/midi_param_collection.h` / `.cpp` — override both (loop params).
- Modify: `src/deluge/modulation/params/param_manager.h` / `.cpp` — add `flattenReverseAutomation`.
- Modify: `tests/projection/param_clone_test.cpp` — extend for `copyAutomation=false` + reverse.

**Interfaces:**
- Produces:
  - `void AutoParam::flattenReverse(int32_t reverseDirectionWithLength)` — reverse this param's already-owned
    nodes in place (moves nodes out, rebuilds reversed). Extracted verbatim from the reverse branch of
    `AutoParam::beenCloned` (`auto_param.cpp:1582-1628`) but **owned-semantics**: `ParamNodeVector oldNodes =
    std::move(nodes());` then `nodes()` is empty, `nodes().insertAt(0, numNodes)`, rebuild. No `init()` on an
    owned buffer (that would leak).
  - `virtual void ParamCollection::stripAutomation()` — default no-op; empties every owned automation node
    vector. `virtual void ParamCollection::flattenReverseAutomation(int32_t reverseDirectionWithLength)` —
    default no-op.
  - `void ParamSet::stripAutomation()` = `for (p) params[p].nodes().clear();`
    `void ParamSet::flattenReverseAutomation(int32_t r)` = `for (p) params[p].flattenReverse(r);`
  - `MIDIParamCollection` same, looping `params` (its `MIDIParamVector`, `.param` per entry).
  - `void ParamManager::flattenReverseAutomation(int32_t r)` — walk `summaries` (as `beenCloned`/
    `cloneParamCollectionsFrom` do) calling `summary->paramCollection->flattenReverseAutomation(r)`.
- Consumes: `AutoParam` copy ctor / copy assign (already landed, A2).

- [ ] **Step 1: Extract `AutoParam::flattenReverse`**

Add to `auto_param.h` after `beenCloned`:
```cpp
/// Reverse this param's automation in place (used when flattening a reversed clip on clone). Operates on
/// already-owned nodes — moves them out, rebuilds reversed. reverseDirectionWithLength is the loop length.
void flattenReverse(int32_t reverseDirectionWithLength);
```
Add to `auto_param.cpp` — copy the body of the `if (reverseDirectionWithLength && numNodes)` block from
`beenCloned` (lines 1582-1628), but replace the alias-detach:
```cpp
void AutoParam::flattenReverse(int32_t reverseDirectionWithLength) {
	int32_t numNodes = std::ssize(nodes());
	if (!reverseDirectionWithLength || !numNodes) {
		return;
	}
	// Move the owned nodes out, then rebuild reversed into a fresh buffer.
	ParamNodeVector oldNodes = std::move(nodes());
	nodes().clear();
	if (nodes().insertAt(0, numNodes).error_or(Error::NONE) != Error::NONE) {
		// OOM rebuilding: restore what we can (leave empty — caller's clone already succeeded structurally).
		throw deluge::exception::BAD_ALLOC;
	}
	ParamNode* rightmostNode = &oldNodes[numNodes - 1];
	int32_t oldNodeToLeftValue = rightmostNode->value;
	ParamNode* leftmostNode = &oldNodes[0];
	bool anythingAtZero = !leftmostNode->pos;
	for (int32_t iOld = 0; iOld < numNodes; iOld++) {
		int32_t iNew = -iOld - !anythingAtZero;
		if (iNew < 0) {
			iNew += numNodes;
		}
		ParamNode* oldNode = &oldNodes[iOld];
		ParamNode* newNode = &nodes()[iNew];
		int32_t iOldToRight = (iOld + 1 == numNodes) ? 0 : iOld + 1;
		ParamNode* oldNodeToRight = &oldNodes[iOldToRight];
		int32_t newPos = -oldNode->pos;
		if (newPos < 0) {
			newPos += reverseDirectionWithLength;
		}
		newNode->pos = newPos;
		newNode->value = oldNode->interpolated ? oldNode->value : oldNodeToLeftValue;
		newNode->interpolated = oldNodeToRight->interpolated;
		oldNodeToLeftValue = oldNode->value;
	}
}
```
Note: `std::move(nodes())` then `nodes().clear()` — moving a `ParamNodeVector` (OrderedPosVector wrapping
`fast_vector`) leaves the source valid-but-unspecified; `clear()` normalises it to empty before `insertAt`.
Include `<utility>` if not already present.

- [ ] **Step 2: Add base virtuals to `ParamCollection`**

`param_collection.h`, after `clone`:
```cpp
/// Empty every owned automation node vector (used for a "copy structure, drop automation" clone). Default:
/// nothing to strip.
virtual void stripAutomation() {}
/// Reverse every owned param's automation in place (flatten-reversing on clone). Default: no-op.
virtual void flattenReverseAutomation(int32_t reverseDirectionWithLength) {}
```
(Inline in the header — trivial defaults.)

- [ ] **Step 3: Override in `ParamSet`**

`param_set.h` (public, in `ParamSet`):
```cpp
void stripAutomation() override;
void flattenReverseAutomation(int32_t reverseDirectionWithLength) override;
```
`param_set.cpp`:
```cpp
void ParamSet::stripAutomation() {
	for (int32_t p = 0; p < numParams_; p++) {
		params[p].nodes().clear();
	}
}
void ParamSet::flattenReverseAutomation(int32_t reverseDirectionWithLength) {
	for (int32_t p = 0; p < numParams_; p++) {
		params[p].flattenReverse(reverseDirectionWithLength);
	}
}
```

- [ ] **Step 4: Override in `MIDIParamCollection`**

`midi_param_collection.h`:
```cpp
void stripAutomation() override;
void flattenReverseAutomation(int32_t reverseDirectionWithLength) override;
```
`midi_param_collection.cpp` (iterate `params`, each entry's `.param` is an `AutoParam`):
```cpp
void MIDIParamCollection::stripAutomation() {
	for (int32_t i = 0; i < std::ssize(params); i++) {
		params[i].param.nodes().clear();
	}
}
void MIDIParamCollection::flattenReverseAutomation(int32_t reverseDirectionWithLength) {
	for (int32_t i = 0; i < std::ssize(params); i++) {
		params[i].param.flattenReverse(reverseDirectionWithLength);
	}
}
```
(Verify the `MIDIParamVector` element access idiom against how `beenCloned`/existing loops index `params`;
mirror it exactly.)

- [ ] **Step 5: Add `ParamManager::flattenReverseAutomation`**

`param_manager.h` (near `beenCloned`):
```cpp
/// Reverse all collections' automation in place (used when a NoteRow's clone must flatten a reversed
/// sequence — the collections were copied reverse-agnostically). reverseDirectionWithLength is the loop length.
void flattenReverseAutomation(int32_t reverseDirectionWithLength);
```
`param_manager.cpp`:
```cpp
void ParamManager::flattenReverseAutomation(int32_t reverseDirectionWithLength) {
	for (ParamCollectionSummary* summary = summaries; summary->paramCollection; summary++) {
		summary->paramCollection->flattenReverseAutomation(reverseDirectionWithLength);
	}
}
```
(Match the summary-walk termination condition used by `destructAndForgetParamCollections` /
`cloneParamCollectionsFrom` — walk while `summary->paramCollection` non-null.)

- [ ] **Step 6: Retire the base-legacy delegation in the 4 existing overrides**

In `UnpatchedParamSet::clone`, `PatchedParamSet::clone`, `ExpressionParamSet::clone`
(`param_set.cpp`) and `MIDIParamCollection::clone` (`midi_param_collection.cpp`), replace the current guard
```cpp
if (!copyAutomation || reverseDirectionWithLength != 0) {
	return ParamCollection::clone(result, copyAutomation, reverseDirectionWithLength);
}
```
with copy-construct-then-adjust (example for `UnpatchedParamSet`; apply the same shape to each, using each
type's own `sizeof`/ctor):
```cpp
void* mem = deluge::memory::alloc_fast(sizeof(UnpatchedParamSet));
if (mem == nullptr) {
	return Error::INSUFFICIENT_RAM;
}
UnpatchedParamSet* clone;
try {
	clone = new (mem) UnpatchedParamSet(*this);
}
catch (deluge::exception&) {
	delugeDealloc(mem);
	return Error::INSUFFICIENT_RAM;
}
if (!copyAutomation) {
	clone->stripAutomation();
}
else if (reverseDirectionWithLength != 0) {
	try {
		clone->flattenReverseAutomation(reverseDirectionWithLength);
	}
	catch (deluge::exception&) {
		clone->~UnpatchedParamSet();
		delugeDealloc(mem);
		return Error::INSUFFICIENT_RAM;
	}
}
result = clone;
return Error::NONE;
```
Note the `!copyAutomation` case now copy-constructs (deep-copies nodes) then clears them — a transient
allocation the old path avoided. Automation is usually empty, so this is usually free; accept the rare
heavy-automation-preset-swap cost for idiom uniformity (documented tradeoff).

- [ ] **Step 7: Extend the A0 harness**

In `param_clone_test.cpp`, add cases after the existing clone checks:
- `clone(copyAutomation=false)` on an unpatched set with a node → assert the clone's param has **0 nodes**
  and the source still has its node (independence + strip).
- `clone(copyAutomation=true, reverse=L)` on an unpatched set with 2 nodes at known positions → assert the
  clone's node positions are the reversed image (`newPos = -oldPos (+ L if <0)`), source unchanged.
Reuse the existing `check(bool, msg)` idiom.

- [ ] **Step 8: VERIFY + commit**

Run the VERIFY recipe. Expect `param_clone: OK`, projection OK, golden `OK=15 REGRESS=0`, ARM clean.
```bash
git add -A && git commit -m "clone: shared reverse/strip infra; retire base-legacy delegation in 4 collections"
```

---

## Task 2: PatchCableSet real-copy `clone()` + copy ctor

The last collection still on the legacy base path. Its copy ctor must deep-copy the cables' automation and the
two sentinel-terminated `Destination*` heap arrays. `PatchCable` (`patch_cable.h`) is safely copyable via its
implicit copy assignment: members are `from`, `polarity`, `destinationParamDescriptor`, `param` (AutoParam —
deep-copies nodes), `rangeAdjustmentPointer` (transient, rebuilt by `setupPatching`; shallow copy matches the
old memcpy). `Destination` is trivially copyable.

**Files:**
- Modify: `src/deluge/modulation/patch/patch_cable_set.h` — declare copy ctor + `clone` override +
  private `cloneDestinations` helper.
- Modify: `src/deluge/modulation/patch/patch_cable_set.cpp` — implement; add includes
  (`memory/general_memory_allocator.h`, `util/exceptions.h`, `<new>`) if absent.
- Modify: `tests/projection/param_clone_test.cpp` — extend patch-cable case for destinations independence +
  copyAutomation=false + reverse.

**Interfaces:**
- Produces: `PatchCableSet(PatchCableSet const& other)` (deep copy, may throw `deluge::exception`);
  `Error PatchCableSet::clone(ParamCollection*&, bool, int32_t) const override`.
- Consumes: `AutoParam` copy (A2), `stripAutomation`/`flattenReverseAutomation` (Task 1 — override in
  PatchCableSet looping usable cables).

- [ ] **Step 1: Declare in `patch_cable_set.h`**

After the `PatchCableSet(ParamCollectionSummary*)` ctor:
```cpp
// Deep copy: real-copies each usable cable's automation and the destination arrays (see clone()).
PatchCableSet(PatchCableSet const& other);
Error clone(ParamCollection*& result, bool copyAutomation, int32_t reverseDirectionWithLength = 0) const override;
void stripAutomation() override;
void flattenReverseAutomation(int32_t reverseDirectionWithLength) override;
```
In the `private:` section:
```cpp
/// Deep-copy a sentinel-terminated Destination array (terminated by an entry with sources == 0). Returns
/// nullptr if src is nullptr. Throws deluge::exception::BAD_ALLOC on OOM.
static Destination* cloneDestinations(Destination const* src);
```

- [ ] **Step 2: Implement `cloneDestinations`**
```cpp
Destination* PatchCableSet::cloneDestinations(Destination const* src) {
	if (src == nullptr) {
		return nullptr;
	}
	auto* dst = static_cast<Destination*>(deluge::memory::alloc_fast(sizeof(Destination) * (kMaxNumPatchCables + 1)));
	if (dst == nullptr) {
		throw deluge::exception::BAD_ALLOC;
	}
	Destination* to = dst;
	Destination const* from = src;
	while (true) {
		*to = *from; // Destination is trivially copyable.
		if (!from->sources) {
			break; // copied the terminating 0 entry
		}
		from++;
		to++;
	}
	return dst;
}
```

- [ ] **Step 3: Implement the copy ctor**

Order: copy the array members (auto-destructed on a later throw), then allocate the raw `destinations`
last with local cleanup so a throw leaves no leaked raw pointer.
```cpp
PatchCableSet::PatchCableSet(PatchCableSet const& other)
    : ParamCollection(other), numUsablePatchCables(other.numUsablePatchCables),
      numPatchCables(other.numPatchCables) {
	sourcesPatchedToAnything[0] = other.sourcesPatchedToAnything[0];
	sourcesPatchedToAnything[1] = other.sourcesPatchedToAnything[1];

	// Real-copy the live cables (deep-copies each cable's AutoParam nodes — may throw); init the rest,
	// matching beenCloned()'s "avoid a rare crash" initialization of the unused tail.
	for (int32_t c = 0; c < numUsablePatchCables; c++) {
		patchCables[c] = other.patchCables[c];
	}
	for (int32_t c = numUsablePatchCables; c < kMaxNumPatchCables; c++) {
		patchCables[c].param.init();
	}

	// Deep-copy the destination arrays last, so a throw above leaves no raw allocation to clean up.
	destinations[GLOBALITY_LOCAL] = cloneDestinations(other.destinations[GLOBALITY_LOCAL]);
	try {
		destinations[GLOBALITY_GLOBAL] = cloneDestinations(other.destinations[GLOBALITY_GLOBAL]);
	}
	catch (deluge::exception&) {
		if (destinations[GLOBALITY_LOCAL]) {
			delugeDealloc(destinations[GLOBALITY_LOCAL]);
		}
		throw;
	}
}
```
Note: `patchCables[c] = other.patchCables[c]` requires `PatchCable` copy assignment. It's implicit and
correct (AutoParam member deep-copies; other members are scalar/POD). If the compiler reports it deleted,
the culprit is a non-assignable member — investigate before proceeding (do not `memcpy`).

- [ ] **Step 4: Implement `clone` + the two overrides**
```cpp
Error PatchCableSet::clone(ParamCollection*& result, bool copyAutomation, int32_t reverseDirectionWithLength) const {
	void* mem = deluge::memory::alloc_fast(sizeof(PatchCableSet));
	if (mem == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	PatchCableSet* clone;
	try {
		clone = new (mem) PatchCableSet(*this);
	}
	catch (deluge::exception&) {
		delugeDealloc(mem);
		return Error::INSUFFICIENT_RAM;
	}
	if (!copyAutomation) {
		clone->stripAutomation();
	}
	else if (reverseDirectionWithLength != 0) {
		try {
			clone->flattenReverseAutomation(reverseDirectionWithLength);
		}
		catch (deluge::exception&) {
			clone->~PatchCableSet();
			delugeDealloc(mem);
			return Error::INSUFFICIENT_RAM;
		}
	}
	result = clone;
	return Error::NONE;
}
void PatchCableSet::stripAutomation() {
	for (int32_t c = 0; c < numUsablePatchCables; c++) {
		patchCables[c].param.nodes().clear();
	}
}
void PatchCableSet::flattenReverseAutomation(int32_t reverseDirectionWithLength) {
	for (int32_t c = 0; c < numUsablePatchCables; c++) {
		patchCables[c].param.flattenReverse(reverseDirectionWithLength);
	}
}
```
The `~PatchCableSet()` in the cleanup path frees the destination arrays (via `freeDestinationMemory`) and the
cables — confirm the dtor is safe to run on a fully-constructed clone (it is; that's its normal job).

- [ ] **Step 5: Extend the harness**

In `param_clone_test.cpp`'s patch-cable section (which already sets `numUsablePatchCables = 1` and a
patch-cable node): additionally give the source non-null `destinations[GLOBALITY_LOCAL]` (a 1-entry + sentinel
array), clone it, and assert `clone->destinations[GLOBALITY_LOCAL] != source->destinations[GLOBALITY_LOCAL]`
(distinct pointer) with equal contents. Add a `copyAutomation=false` clone asserting the cable's node count is
0. Free the test's destination arrays at teardown.

- [ ] **Step 6: VERIFY + commit**

VERIFY recipe. `git add -A && git commit -m "clone: PatchCableSet real-copy clone() + deep-copied destinations"`

---

## Task 3: NoteRow copy ctor + real-copy `NoteRowVector::cloneFrom`

The hardest, highest-risk task (golden-blind; `NoteRow` needs a `ModelStack` so it can't be fully unit-tested
here). `NoteRow` is currently move-only (`OldDrumNameHead firstOldDrumName` deletes copy), cloned via
memcpy + a separate `beenCloned` pass. A defaulted copy ctor is impossible (that move-only member), so the
copy ctor is explicit and **must enumerate every data member** — audit against `note_row.h:159-199,287-288`.

The paramManager side already routes through the new `clone()` dispatch (via
`cloneParamCollectionsFrom`), so the copy ctor delegates to it. The reverse-flattening of *notes* stays a
named post-clone transform (it is not copy semantics).

**Files:**
- Modify: `src/deluge/model/note/note_row.h` — replace deleted copy ctor with a declared one; declare
  `flattenReverse`.
- Modify: `src/deluge/model/note/note_row.cpp` — implement copy ctor; refactor `beenCloned`'s note-reverse
  block into `flattenReverse` (owned-semantics).
- Modify: `src/deluge/model/note/note_row_vector.cpp` — `cloneFrom` → real copy-assignment.
- Modify: `src/deluge/model/clip/instrument_clip.cpp:182-201` — copy then flatten-reverse loop.
- Modify: `tests/projection/param_clone_test.cpp` — extend if a NoteRow-level check is feasible; otherwise
  document the hardware dependency in-comment.

**Interfaces:**
- Produces: `NoteRow(NoteRow const& other)` (deep copy; throws `deluge::exception` on OOM);
  `Error NoteRow::flattenReverse(ModelStackWithNoteRow* modelStack)` (reverse notes + automation in place).
- Consumes: `ParamManager::cloneParamCollectionsFrom` (already clone()-dispatched),
  `ParamManager::flattenReverseAutomation` (Task 1), `OrderedPosVector` copy assignment (A2),
  `NoteVector` copy (A2).

- [ ] **Step 1: Declare the copy ctor + `flattenReverse` in `note_row.h`**

Replace lines 129-134 (the move-only comment + deleted copy):
```cpp
// Movable (so NoteRowVector can shuffle rows). Copyable via an explicit deep-copy ctor: the param manager
// and notes are deep-copied, the old-drum-name list is NOT shared (reset to empty on the copy), and
// transient playback state is reset. firstOldDrumName being move-only forces the ctor to be explicit.
NoteRow(NoteRow&& other) noexcept = default;
NoteRow& operator=(NoteRow&& other) noexcept = default;
NoteRow(NoteRow const& other);
NoteRow& operator=(NoteRow const&) = delete; // clones construct fresh; no in-place copy-assign needed
```
After `beenCloned`'s declaration (line 240):
```cpp
/// Flatten a reversed sequence after a plain clone: reverse this row's notes and automation in place.
/// Call only when the row's effective direction is REVERSE and flattening was requested.
Error flattenReverse(ModelStackWithNoteRow* modelStack);
```

- [ ] **Step 2: Implement the copy ctor (audit every member)**

`note_row.cpp` — enumerate against the header. Deep members go through their own copy; move-only/transient
members are reset:
```cpp
NoteRow::NoteRow(NoteRow const& other)
    : y(other.y), muted(other.muted), mutedBeforeStemExport(other.mutedBeforeStemExport),
      exportStem(other.exportStem), loopLengthIfIndependent(other.loopLengthIfIndependent),
      lastProcessedPosIfIndependent(other.lastProcessedPosIfIndependent),
      repeatCountIfIndependent(other.repeatCountIfIndependent),
      currentlyPlayingReversedIfIndependent(other.currentlyPlayingReversedIfIndependent),
      sequenceDirectionMode(other.sequenceDirectionMode),
      // paramManager: default-constructed, deep-cloned below (fallible → throw).
      drum(other.drum), // pointer shared by design (clone points at the same Drum)
      // firstOldDrumName: NOT copied — a clone starts with no owned drum-name list.
      notes(other.notes), // NoteVector deep copy (may throw)
      probabilityValue(other.probabilityValue), iteranceValue(other.iteranceValue),
      fillValue(other.fillValue), muteMIDICommand(other.muteMIDICommand), midiInput(other.midiInput),
      colourOffset(other.colourOffset), sequenced(other.sequenced),
      ignoreNoteOnsBefore_(0) { // transient: reset (matches beenCloned)
	// Deep-clone the automation/param collections through the clone()-dispatched path.
	Error error = paramManager.cloneParamCollectionsFrom(&other.paramManager, true, true);
	if (error != Error::NONE) {
		throw deluge::exception::BAD_ALLOC;
	}
	// ignoreUntil / ignoredTicks are transient playback state; default (0) via member init is correct.
}
```
**Audit checklist** (every `note_row.h` data member must appear above or be intentionally reset): `y`,
`muted`, `mutedBeforeStemExport`, `exportStem`, `loopLengthIfIndependent`, `lastProcessedPosIfIndependent`,
`repeatCountIfIndependent`, `currentlyPlayingReversedIfIndependent`, `sequenceDirectionMode`, `paramManager`
(cloned), `drum` (shared), `firstOldDrumName` (reset), `notes` (deep), `probabilityValue`, `iteranceValue`,
`fillValue`, `muteMIDICommand`, `midiInput`, `colourOffset`, `sequenced`, `ignoreNoteOnsBefore_` (reset),
`ignoreUntil`/`ignoredTicks` (transient, private). Confirm `LearnedMIDI` (`muteMIDICommand`/`midiInput`) and
`ParamManagerForTimeline`'s default construction leave `paramManager` in a state `cloneParamCollectionsFrom`
can populate (it is called on fresh managers elsewhere — e.g. `instrument_clip.cpp:170`).

- [ ] **Step 3: Refactor the note-reverse block into `NoteRow::flattenReverse`**

Move `note_row.cpp:108-197`'s reverse logic into `flattenReverse`, converting alias-detach to owned-move and
adding the automation reverse (which `beenCloned` used to get via `paramManager.beenCloned(reverseWithLength)`):
```cpp
Error NoteRow::flattenReverse(ModelStackWithNoteRow* modelStack) {
	int32_t effectiveLength = modelStack->getLoopLength();
	// Reverse the automation in place (collections were copied reverse-agnostically by the copy ctor).
	paramManager.flattenReverseAutomation(effectiveLength);

	int32_t numNotes = std::ssize(notes);
	if (numNotes) {
		NoteVector oldNotes = std::move(notes); // own a deep copy of the source order
		notes.clear();
		Error error = notes.insertAt(0, numNotes).error_or(Error::NONE);
		if (error != Error::NONE) {
			return error;
		}
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();
		if (clip->allowNoteTails(modelStack)) {
			// ... existing tails-branch body (lines 122-156), reading oldNotes, writing notes ...
		}
		else {
			// ... existing no-tails-branch body (lines 161-184) ...
		}
	}
	if (sequenceDirectionMode != SequenceDirection::PINGPONG) {
		sequenceDirectionMode = SequenceDirection::OBEY_PARENT;
	}
	return Error::NONE;
}
```
Keep the note-rewrite arithmetic byte-for-byte from the current block. Note `paramManager.flattenReverseAutomation`
can throw (from `AutoParam::flattenReverse`'s rebuild); wrap in try/catch → `return Error::INSUFFICIENT_RAM`
if you want to preserve the `Error`-returning contract, or let it propagate to `NoteRowVector::cloneFrom`'s
catch. Prefer catching here to keep `flattenReverse` `Error`-returning.

- [ ] **Step 4: `NoteRowVector::cloneFrom` → real copy-assignment**

Replace the memcpy loop (`note_row_vector.cpp:45-58`) with OrderedPosVector copy-assignment (which
element-wise copy-constructs each `NoteRow`), catching leaf OOM:
```cpp
bool NoteRowVector::cloneFrom(NoteRowVector const* other) {
	try {
		*this = *other; // OrderedPosVector copy-assign → per-NoteRow deep copy ctor (may throw)
	}
	catch (deluge::exception&) {
		clear();
		return false;
	}
	return true;
}
```
Remove the now-unused `<cstring>`/`<new>` includes if nothing else needs them. This depends on
`OrderedPosVector<NoteRow>` copy-assignment (added in A2) and the `NoteRow` copy ctor (Step 2).

- [ ] **Step 5: Update `InstrumentClip::clone` (copy then flatten-reverse)**

Replace the separate `beenCloned` loop (`instrument_clip.cpp:189-197`). `noteRows.cloneFrom` now produces
fully deep-copied rows, so the per-row pass only needs to flatten-reverse when requested:
```cpp
if (!newClip->noteRows.cloneFrom(&noteRows)) {
	error = Error::INSUFFICIENT_RAM;
	goto deleteClipAndGetOut;
}

modelStack->setTimelineCounter(newClip);

if (shouldFlattenReversing) {
	for (int32_t i = 0; i < std::ssize(newClip->noteRows); i++) {
		NoteRow* noteRow = &newClip->noteRows[i];
		int32_t noteRowId = newClip->getNoteRowId(noteRow, i);
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);
		if (noteRow->getEffectiveSequenceDirectionMode(modelStackWithNoteRow) == SequenceDirection::REVERSE) {
			noteRow->flattenReverse(modelStackWithNoteRow);
		}
	}
}
```
Clean up the above to a single `addNoteRow` per row (the sketch double-calls). Preserve the existing
"keep going on error" tolerance. Confirm the `getEffectiveSequenceDirectionMode` gate matches the old
`flatteningReversingNow` condition (`shouldFlattenReversing && effective dir == REVERSE`) — the note-reverse
must only run for rows that were actually reversing, exactly as `NoteRow::beenCloned` decided internally.

- [ ] **Step 6: VERIFY + commit**

VERIFY recipe. The golden won't catch note-clone regressions — rely on build + the harness's paramManager
independence + **the mandatory hardware gate below**. Commit:
```bash
git add -A && git commit -m "clone: NoteRow deep-copy ctor + real-copy NoteRowVector; flatten-reverse as post-clone step"
```

---

## Task 4: Remove the byte-copy idiom (A4)

Nothing calls the idiom methods anymore. Delete them and make the base `clone()` pure virtual.

**Files:**
- Modify: `src/deluge/modulation/params/param_collection.h` / `.cpp` — `clone` → pure virtual; remove
  `beenCloned`.
- Modify: `param_set.{h,cpp}`, `midi_param_collection.{h,cpp}`, `patch_cable_set.{h,cpp}` — remove
  `beenCloned` overrides + the `cloneFrom(ParamCollection*, bool)` legacy helpers if now unused.
- Modify: `src/deluge/modulation/automation/auto_param.{h,cpp}` — remove `beenCloned` (its reverse body now
  lives in `flattenReverse`; its copy body in the copy ctor). Remove `cloneFrom` if unused.
- Modify: `src/deluge/modulation/params/param_manager.{h,cpp}` — remove `beenCloned`.
- Modify: `src/deluge/model/note/note_row.{h,cpp}` — remove `beenCloned`.
- Modify: `src/deluge/util/container/ordered_pos_vector.h` — remove the idiom methods `init` / `cloneFrom` /
  `beenCloned` **only if** no legitimate callers remain (see Step 1); keep `clear`/copy ctor.
- Audit: every file from the breadth grep — `consequence_param_change.cpp`, `kit.cpp`,
  `mod_controllable_audio.cpp`, `song.cpp`, `sound_instrument.cpp`.

- [ ] **Step 1: Grep for surviving idiom callers**
```bash
cd /home/kate/GitHub/DelugeFirmware
grep -rn "\.beenCloned(\|->beenCloned(" src/deluge --include="*.cpp" --include="*.h"
grep -rn "\.init()\b" src/deluge/modulation src/deluge/model/note --include="*.cpp"  # OrderedPosVector::init survivors
grep -rn "\.cloneFrom(" src/deluge --include="*.cpp"                                  # OrderedPosVector::cloneFrom survivors
```
Classify each hit: (a) idiom clone chain — should now be gone; (b) legitimate vector op (e.g. a `.init()`
that means "reset to empty" outside cloning, or a `.cloneFrom` deep-copy that should become a copy ctor
`X y = x;`). Convert (b) to idiomatic equivalents; only then delete the methods. If any (a) remains, a
conversion was missed — fix before deleting.

- [ ] **Step 2: Make base `clone` pure virtual, delete base bodies**

`param_collection.h`: `virtual Error clone(ParamCollection*& result, bool copyAutomation, int32_t reverseDirectionWithLength = 0) const = 0;`
Remove `ParamCollection::clone`'s memcpy body and `ParamCollection::beenCloned` from `.cpp`/`.h`. Confirm all
5 concrete collections override `clone` (they do: UnpatchedParamSet, PatchedParamSet, ExpressionParamSet,
PatchCableSet, MIDIParamCollection) — `ParamSet` stays abstract.

- [ ] **Step 3: Delete the remaining `beenCloned` declarations/definitions**

Remove `beenCloned` from `ParamSet` + 3 subclasses, `MIDIParamCollection`, `PatchCableSet`, `AutoParam`,
`ParamManager`, `NoteRow`. Remove `AutoParam::cloneFrom` and `ParamSet::cloneFrom`/
`MIDIParamCollection::cloneFrom` if Step 1 showed no callers (some may still be used by
`copyOverridingFrom`-style paths — keep those).

- [ ] **Step 4: Prune `OrderedPosVector` idiom methods**

If Step 1 confirms no callers, delete `init`, `cloneFrom`, `beenCloned` from `ordered_pos_vector.h`. Keep the
copy ctor / copy assignment / `clear` / `span`. Update the class doc comment to drop the byte-copy-idiom
mention.

- [ ] **Step 5: VERIFY + commit**

Full VERIFY recipe **including a full ARM golden pass** (this is a phase boundary — per the golden-arm-targeted
policy, run the whole ARM sweep here, not just kernel cases). Commit:
```bash
git add -A && git commit -m "clone: delete byte-copy idiom (beenCloned/init/cloneFrom); base clone() pure virtual"
```

---

## Hardware Gate (MANDATORY — golden-blind)

The golden round-trips load→save and never edits, so it cannot see clone/undo regressions. After Task 4,
before merge, test on-device (or request the user to):
- **Clip duplicate** (instrument + kit clips): duplicated clip plays identically, automation intact.
- **Reversed-clip flatten**: duplicate a REVERSE-direction clip with flattening — notes and automation
  reverse correctly (exercises `flattenReverse`).
- **Kit row add/remove**: NoteRow copy path under kit editing.
- **Undo/redo across clones**: undo a duplicate, redo, undo an edit on a cloned clip — no corruption
  (exercises the ParamManager backup/swap clone path).
- **Patch-cable-heavy preset**: duplicate a Sound clip with several patch cables + patched automation; the
  clone's cables and destinations are independent (exercises PatchCableSet deep-copy).
- **Save → reload** after each of the above.

Do not consider the branch done until this passes.

## Risks / reality check

- **NoteRow copy ctor miss-a-member (Task 3)** is the top risk — silent corruption invisible to the golden.
  Mitigated by the explicit member audit checklist + hardware gate. Consider a `static_assert` on
  `sizeof(NoteRow)` near the ctor so a future added member forces a re-review (does not catch a missed copy of
  an existing member — the audit does).
- **Transient allocation on `!copyAutomation`** (Tasks 1/2): copy-then-strip allocates nodes it immediately
  frees. Usually free (automation usually empty); accepted for idiom uniformity. Revisit only if profiling
  shows a preset-swap RAM spike.
- **Move-then-rebuild in `flattenReverse`**: `std::move(nodes())` then `clear()` relies on the moved-from
  OrderedPosVector being safely `clear()`-able. Verified pattern; keep the `clear()` before `insertAt`.
- **PatchCable implicit copy-assign**: if a future member makes it non-assignable, the copy ctor breaks at
  compile time — do not paper over with memcpy; fix the member.
- **Stop-anywhere**: Tasks 1+2 are self-contained and independently valuable (collections fully retired). If
  the NoteRow work (Task 3) proves too risky on hardware, Tasks 1+2 can ship and NoteRow can stay on a
  narrowed legacy path — but then Task 4's idiom deletion is partial (keep `NoteRow::beenCloned` + the
  memcpy `cloneFrom` only). Decide after Task 2's hardware check if splitting the merge is warranted.
