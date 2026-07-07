# M1 Edit Substrate + Pilot Param Edit — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land M1a + M1b of `docs/dev/project_model_migration_plan.md`: the `Edit` substrate (the reified forward-mutation object + single dispatch point with the future event-emission hook) and the pilot `SetParamValue` Edit that funnels the gold-knob param turn, owning its own undo `Consequence`.

**Architecture:** An `Edit` is an id-addressed, reified mutation of persistent state (`deluge::edits::Edit`, app tree — it may touch engine objects during the funnel phase). A free `deluge::edits::apply(Context&, Edit&)` is the single dispatch point; the model-changed event hook lives there as a no-op until M4. The pilot reifies `View::modEncoderAction_existentParam`'s mutation (view.cpp:927) behaviour-identically, then takes ownership of producing its `ConsequenceParamChange` via idempotent pre-logging (the `Action` machinery's `containsConsequenceParamChange` dedupe makes the interior log a no-op).

**Tech Stack:** C++23 (firmware app tree), sim harness tests (`tests/projection/`, plain check-style, registered in `sim/CMakeLists.txt`, run via ctest on x86 `build-sim` and ARM `build-sim-arm`/qemu), golden serialization nets (`tests/golden/songs_run.py`, `param_record_run.py`).

## Global Constraints

- **Naming decision (M1a, confirmed):** the forward-reified per-operation object is named **`Edit`** (`Command` collides with MIDI/HID command handling; `Action` is the transaction).
- New code is idiomatic C++23 (`enum class`, designated init, `std::optional`, value semantics); behaviour-preserving changes keep logic faithful.
- Every task is behaviour-preserving and independently gated: sim ctest suite green, `songs_run.py` green, `param_record_run.py` green, firmware builds (`./dbt build Debug`).
- No new engine-seeded persistent state; references follow the address rule (ids at boundaries, ephemeral resolved refs otherwise, never stored beyond one operation).
- `src/deluge/CMakeLists.txt` GLOBs sources (`GLOB_RECURSE ... CONFIGURE_DEPENDS`) — new files under `src/deluge/model/edits/` need **no** CMake change for the firmware/app; only new *test executables* touch `sim/CMakeLists.txt`.
- Commit at the end of each task. Never add Co-Authored-By trailers.
- Key semantics discovered up front (do not rediscover): `AutoParam::setCurrentValueInResponseToUserInput`'s `shouldLogAction` parameter is **dead** (never read in the body — `auto_param.cpp:192-372`); the unautomated-change undo log at `auto_param.cpp:358-362` fires iff `(nodes.empty() || !isPlaying) && value != currentValue`, evaluated **after** the playing/recording branch; the recording-armed branch does its own `ActionType::RECORD` / `AUTOMATION_DELETE` logging and can `goto getOut` (clip-too-short) or early-return (shift-held) without reaching line 358.

---

### Task 1: Edit substrate skeleton (`deluge::edits::Edit` + dispatcher + event hook)

**Files:**
- Create: `src/deluge/model/edits/edit.h`
- Create: `src/deluge/model/edits/edit.cpp`
- Create: `tests/projection/edit_test.cpp`
- Modify: `sim/CMakeLists.txt` (after the `param_clone_test` block ending line 554; plus `add_test` next to line 579)

**Interfaces:**
- Produces: `deluge::edits::Context{::Song& song; deluge::project::Song& model;}`; abstract `deluge::edits::Edit` with `virtual Error apply(Context&) = 0;`; free `Error deluge::edits::apply(Context& ctx, Edit& edit)` (dispatch point). Task 3 subclasses `Edit` and calls `apply()` from `view.cpp`.

- [ ] **Step 1: Write the failing test**

Create `tests/projection/edit_test.cpp`:

```cpp
// Unit harness for the Edit substrate (M1a/M1b, docs/dev/project_model_migration_plan.md):
// dispatcher wiring, then (Task 3+) the SetParamValue pilot, its Consequence production,
// coalescence, and undo. Runs in the sim environment (real UnpatchedParamSet + ActionLogger +
// host allocator) on x86 (build-sim) and ARM (build-sim-arm via qemu).
//
// Exit: 0 = all checks pass, 1 = a failure.

#include "definitions_cxx.hpp"
#include "model/edits/edit.h"
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

// Pins the dispatcher contract: apply() runs the edit exactly once and passes its result through.
struct FakeEdit final : deluge::edits::Edit {
	int32_t applied = 0;
	Error result = Error::NONE;
	Error apply(deluge::edits::Context&) override {
		applied++;
		return result;
	}
};

void testDispatcher(deluge::edits::Context& ctx) {
	{
		FakeEdit edit;
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "dispatcher: success passes through");
		check(edit.applied == 1, "dispatcher: edit applied exactly once");
	}
	{
		FakeEdit edit;
		edit.result = Error::UNSPECIFIED;
		check(deluge::edits::apply(ctx, edit) == Error::UNSPECIFIED, "dispatcher: error passes through");
		check(edit.applied == 1, "dispatcher: failing edit still applied exactly once");
	}
}

} // namespace

// Song construction (Task 3 fills this in properly); for Task 1 the Context members are only
// stored, never dereferenced by FakeEdit, but Context takes references so we need a real Song.
#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/song/song.h"

int main() {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	currentSong = song;

	deluge::edits::Context ctx{*song, song->project};
	testDispatcher(ctx);

	if (failures == 0) {
		std::fprintf(stdout, "edit_test: OK\n");
	}
	return failures ? 1 : 0;
}
```

Register the executable in `sim/CMakeLists.txt`, immediately after the `param_clone_test` block (line 554), mirroring it exactly:

```cmake
# Edit substrate unit harness (M1a/M1b, docs/dev/project_model_migration_plan.md): dispatcher
# contract, the SetParamValue pilot's mutation + Consequence/coalescence/undo behaviour.
add_executable(edit_test
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_platform.c
    ${REPO_ROOT}/src/bsp/host/host_audio.c
    ${REPO_ROOT}/src/bsp/host/host_link.c
    ${REPO_ROOT}/src/bsp/host/host_pcm.c
    ${REPO_ROOT}/src/bsp/host/host_midi.c
    ${REPO_ROOT}/src/bsp/host/host_debug.cpp
    ${REPO_ROOT}/tests/projection/edit_test.cpp
)
target_include_directories(edit_test PRIVATE ${HOST_INCLUDE_DIRS} ${REPO_ROOT}/src/deluge)
target_link_libraries(edit_test PRIVATE deluge_app deluge_scheduler ${SIM_ALLOC_LINK})
if(RT_LIB)
    target_link_libraries(edit_test PRIVATE ${RT_LIB})
endif()
```

And next to the existing `add_test` lines (after line 579):

```cmake
add_test(NAME edit_test COMMAND edit_test)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sim -j --target edit_test`
Expected: FAIL — `model/edits/edit.h: No such file or directory`

- [ ] **Step 3: Write the substrate**

Create `src/deluge/model/edits/edit.h`:

```cpp
#pragma once

#include "definitions_cxx.hpp" // Error

class Song;
namespace deluge::project {
class Song;
}

namespace deluge::edits {

/// What an Edit executes against. `model` is the authoritative project model; `song` is the
/// legacy engine-side controller, present for the funnel phase only (Edits that still delegate
/// their mutation to engine objects) — it goes away as owners flip to model authority (M3,
/// docs/dev/project_model_migration_plan.md).
struct Context {
	::Song& song;
	deluge::project::Song& model;
};

/// A reified forward mutation of persistent state (M1 of the migration plan; the "Command" of
/// docs/dev/project_model_north_star.md, named Edit to avoid colliding with MIDI/HID command
/// handling). An Edit addresses its target by stable id, resolves at apply time, mutates, and
/// produces the Consequence(s) that invert it into the open Action — the do-site and the
/// undo-record-site are the same object. Edits are UI/off-RT constructs: audio-thread-originated
/// writes (recorded automation) stay on the reconcile path and are NOT Edits.
class Edit {
public:
	virtual ~Edit() = default;

	/// Perform the mutation (and record its undo Consequence(s)). Any resolved pointer lives only
	/// for the duration of this call — an ephemeral borrow, never a stored member.
	virtual Error apply(Context& ctx) = 0;
};

/// The single dispatch point every Edit goes through. Today: applies the edit. From M4: also the
/// emission point for the model-changed event the engine turns into a snapshot update.
Error apply(Context& ctx, Edit& edit);

} // namespace deluge::edits
```

Create `src/deluge/model/edits/edit.cpp`:

```cpp
#include "model/edits/edit.h"

namespace deluge::edits {

namespace {
/// M4 (snapshot boundary) lands here: today each Edit's delegated engine call updates the engine
/// in-line, so this is a no-op; when the executor reads snapshots, this becomes the event feed.
void emitModelChanged(Edit& /*edit*/) {
}
} // namespace

Error apply(Context& ctx, Edit& edit) {
	Error error = edit.apply(ctx);
	if (error == Error::NONE) {
		emitModelChanged(edit);
	}
	return error;
}

} // namespace deluge::edits
```

- [ ] **Step 4: Build and run the test**

Run: `cmake --build build-sim -j --target edit_test && ctest --test-dir build-sim -R edit_test --output-on-failure`
Expected: PASS (`edit_test: OK`)

- [ ] **Step 5: Verify the firmware still builds**

Run: `./dbt build Debug`
Expected: clean build (new files are GLOBbed into `deluge_app`)

- [ ] **Step 6: Commit**

```bash
git add src/deluge/model/edits/ tests/projection/edit_test.cpp sim/CMakeLists.txt
git commit -m "edits: M1a — Edit substrate (reified mutation base, dispatch point, event hook)"
```

---

### Task 2: `addressFor()` — reusable ParamAddress construction (extracted from `doc()`)

The owner-classification that builds a `ParamAddress` from a resolved engine ModelStack currently lives inline in `ModelStackWithAutoParam::doc()` (`auto_param.cpp:111-162`). Edits need it at construction time (view.cpp), so extract it as a free function and rebase `doc()` on it. **Pure behaviour-preserving refactor** — the regression net is the existing suites.

**Files:**
- Create: `src/deluge/model/edits/param_address_for.h`
- Create: `src/deluge/model/edits/param_address_for.cpp`
- Modify: `src/deluge/modulation/automation/auto_param.cpp:103-162` (`doc()` rebased onto `addressFor`)

**Interfaces:**
- Consumes: `deluge::project::ParamAddress` / `OwnerId` (`src/deluge-project/param_address.h`), `ModelStackWithAutoParam`.
- Produces: `std::optional<deluge::project::ParamAddress> deluge::edits::addressFor(const ModelStackWithAutoParam&)` — Task 3's call site uses this.

- [ ] **Step 1: Confirm the current suites are green (pre-refactor baseline)**

Run: `cmake --build build-sim -j && ctest --test-dir build-sim --output-on-failure`
Expected: PASS (includes `param_projection_test`, `param_clone_test`, `edit_test`)

- [ ] **Step 2: Extract `addressFor`**

Create `src/deluge/model/edits/param_address_for.h`:

```cpp
#pragma once

#include "deluge-project/param_address.h"
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::edits {

/// The stable id-address of the param lane a resolved engine ModelStack names — the owner
/// classification extracted (behaviour-identical) from ModelStackWithAutoParam::doc(). Returns
/// nullopt for the one unaddressable case: a never-saved owner with no serialId; callers fall
/// back to engine resolution, exactly as doc() does.
std::optional<deluge::project::ParamAddress> addressFor(const ModelStackWithAutoParam& modelStack);

} // namespace deluge::edits
```

Create `src/deluge/model/edits/param_address_for.cpp` — the body is `doc()`'s classification (`auto_param.cpp:112-155`) moved verbatim, with `return std::nullopt` replacing the `haveOwner=false` fallthrough (keep the S3c-S3g classification comments with it):

```cpp
#include "model/edits/param_address_for.h"

#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/params/param_collection.h"

namespace deluge::edits {

std::optional<deluge::project::ParamAddress> addressFor(const ModelStackWithAutoParam& modelStack) {
	if (modelStack.song == nullptr) {
		return std::nullopt;
	}
	TimelineCounter* tc = modelStack.getTimelineCounterAllowNull();
	deluge::project::OwnerId owner{};
	std::optional<uint32_t> clipSerial{}; // for INSTRUMENT/DRUM owners: which clip's override this lane is
	if (tc == static_cast<TimelineCounter*>(modelStack.song)) {
		owner = deluge::project::OwnerId::song();
	}
	else if (tc != nullptr) {
		// Song and Clip are the only TimelineCounters, so a non-song tc is a Clip. Classify the owner by the
		// ModelStack context (S3c-S3g): a set noteRow ⇒ a per-drum param (OwnerId::drum, keyed by the drum's
		// serialId); else a synth/kit InstrumentClip ⇒ clip-level instrument-shared (OwnerId::instrument(output));
		// else an AudioClip ⇒ its own params (OwnerId::clip). noteRow is a RELIABLE drum signal — the ModelStack
		// builders always set it explicitly (addNoteRow(0, nullptr) for the no-noteRow path), never stale. Any
		// uncertain case (non-Sound drum, missing serialId) is unaddressable → nullopt (engine fallback).
		Clip* clip = static_cast<Clip*>(tc);
		std::optional<uint32_t> clipId = clip->serialId();
		if (!clipId) {
			return std::nullopt;
		}
		NoteRow* noteRow = modelStack.getNoteRowAllowNull();
		if (noteRow != nullptr) {
			if (noteRow->drum == nullptr || noteRow->drum->type != DrumType::SOUND) {
				return std::nullopt;
			}
			std::optional<uint32_t> drumId = noteRow->drum->serialId();
			if (!drumId) {
				return std::nullopt;
			}
			owner = deluge::project::OwnerId::drum(*drumId);
			clipSerial = clipId;
		}
		else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
		         && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT)) {
			std::optional<uint32_t> outId = clip->output->serialId();
			if (!outId) {
				return std::nullopt;
			}
			owner = deluge::project::OwnerId::instrument(*outId);
			clipSerial = clipId;
		}
		else {
			owner = deluge::project::OwnerId::clip(*clipId);
		}
	}
	else {
		return std::nullopt; // no timeline counter at all — nothing to key an owner off
	}
	return deluge::project::ParamAddress{owner, modelStack.paramCollection->getParamKind(),
	                                     modelStack.paramId, clipSerial};
}

} // namespace deluge::edits
```

**Faithfulness watch-point:** the original computes `haveOwner` and falls through; this version early-returns. The one structural difference to preserve: in the original, `tc == nullptr` leaves `haveOwner == false` → fallback. The `else return nullopt` above covers it. There is **one deliberate ordering change**: the original checks `noteRow->drum != nullptr && type == SOUND` *inside* the noteRow branch and falls through to the collection fallback on failure **without** trying the instrument/clip branches — the early-returns above reproduce exactly that (a set noteRow never reaches the instrument/clip classification). Verify against the original side-by-side before committing.

- [ ] **Step 3: Rebase `doc()` onto it**

In `src/deluge/modulation/automation/auto_param.cpp`, replace the body of `ModelStackWithAutoParam::doc()` (lines 111-162) with:

```cpp
deluge::project::DocAutoParam* ModelStackWithAutoParam::doc() const {
	if (std::optional<deluge::project::ParamAddress> address = deluge::edits::addressFor(*this)) {
		if (deluge::project::DocAutoParam* resolved = song->project.resolveParam(*address)) {
			return resolved;
		}
	}
	return paramCollection->docParamForId(paramId);
}
```

Add `#include "model/edits/param_address_for.h"` to auto_param.cpp's includes. Keep the explanatory comment block above `doc()` (lines 103-110), trimming the classification detail that moved into `addressFor`.

- [ ] **Step 4: Run the regression net**

Run: `cmake --build build-sim -j && ctest --test-dir build-sim --output-on-failure`
Expected: PASS (all tests, unchanged)

Run: `python3 tests/golden/songs_run.py`
Expected: all fixtured songs OK (exit 0; exit 77 SKIP only if `DELUGE_SONG_CORPUS` unset)

Run: `./dbt build Debug`
Expected: clean build

- [ ] **Step 5: Commit**

```bash
git add src/deluge/model/edits/param_address_for.h src/deluge/model/edits/param_address_for.cpp src/deluge/modulation/automation/auto_param.cpp
git commit -m "edits: extract addressFor() — reusable ParamAddress construction from a resolved ModelStack"
```

---

### Task 3: `SetParamValue` pilot Edit — funnel the knob-turn call site (behaviour-identical)

Reify the mutation at `view.cpp:927` as an Edit. In this task the Edit is a pure funnel: it delegates to the exact same engine call; the undo record is still produced by the interior logging (that flips in Task 4). The unit test pins mutation, Action type, Consequence presence, coalescence, dedupe, and undo — all behaviour the funnel must not disturb.

**Files:**
- Create: `src/deluge/model/edits/set_param_value.h`
- Create: `src/deluge/model/edits/set_param_value.cpp`
- Modify: `src/deluge/gui/views/view.cpp:926-927` (the call-site swap) + includes
- Modify: `tests/projection/edit_test.cpp` (add the pilot cases)

**Interfaces:**
- Consumes: `deluge::edits::Edit`/`Context`/`apply` (Task 1), `deluge::edits::addressFor` (Task 2), `AutoParam::setValuePossiblyForRegion(int32_t value, ModelStackWithAutoParam const*, int32_t pos, int32_t length, bool mayDeleteNodesInLinearRun = true)`.
- Produces: `deluge::edits::SetParamValue` — constructed `SetParamValue(std::optional<project::ParamAddress>, int32_t newValue, int32_t pos, int32_t length, ModelStackWithAutoParam*)`. Task 4 rewrites only its `apply` body.

- [ ] **Step 1: Write the failing tests**

Append to `tests/projection/edit_test.cpp` (new includes at the top of the file):

```cpp
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
#include "model/model_stack.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include <cstring>
```

And the test body (call from `main` after `testDispatcher`; the display global must be created first — add `display = new deluge::hid::display::SevenSegment;` plus `#include "hid/display/seven_segment.h"` in `main` before any test runs, mirroring `clip_clone_roundtrip.cpp:118`):

```cpp
// Build a ModelStackWithAutoParam over a real UnpatchedParamSet, song-less (=> addressFor()
// nullopt; doc() falls back to the collection's own store) — the param_automation_test pattern,
// but with a real collection so remotelySwapParamState/undo is the real engine path.
ModelStackWithAutoParam* makeStack(char* buf, ParamManagerForTimeline& pm, int32_t paramId) {
	std::memset(buf, 0, MODEL_STACK_MAX_SIZE);
	auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
	ms->paramCollection = pm.getUnpatchedParamSet();
	ms->paramCollectionSummary = pm.getUnpatchedParamSetSummary();
	ms->paramId = paramId;
	ms->autoParam = &pm.getUnpatchedParamSet()->params[paramId];
	return ms;
}

void testSetParamValuePilot(deluge::edits::Context& ctx) {
	constexpr int32_t kParam = 2;

	actionLogger.deleteAllLogs();

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);

	int32_t oldValue = ms->autoParam->getCurrentValue();
	int32_t newValue = oldValue + 100;

	// 1. Mutation: engine value + owner store scalar both updated; scalar path (length 0).
	{
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), newValue, 0, 0, ms};
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "pilot: apply succeeds");
	}
	check(ms->autoParam->getCurrentValue() == newValue, "pilot: engine currentValue updated");
	check(ms->doc()->current_value == newValue, "pilot: DOM store scalar updated");

	// 2. Undo record: one open Action of the right type, holding a param Consequence.
	Action* action = actionLogger.firstAction[BEFORE];
	check(action != nullptr, "pilot: an Action was opened");
	check(action->type == ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE, "pilot: Action type is param change");
	check(action->firstParamConsequence != nullptr, "pilot: ConsequenceParamChange recorded");
	check(action->nextAction == nullptr, "pilot: exactly one Action");
	Consequence* firstCons = (Consequence*)action->firstParamConsequence;

	// 3. Coalescence + dedupe: a second turn of the same knob merges into the SAME Action and
	//    does NOT re-snapshot (containsConsequenceParamChange dedupe) — pointer-equality pins both.
	{
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), newValue + 100, 0, 0, ms};
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "pilot: second apply succeeds");
	}
	check(actionLogger.firstAction[BEFORE] == action, "pilot: second turn coalesced into same Action");
	check(action->nextAction == nullptr, "pilot: still exactly one Action");
	check((Consequence*)action->firstParamConsequence == firstCons, "pilot: before-state not re-snapshotted");
	check(ms->autoParam->getCurrentValue() == newValue + 100, "pilot: second value applied");

	// 4. Undo restores the ORIGINAL value (the single snapshot is the pre-first-turn state).
	actionLogger.revert(BEFORE, /*updateVisually=*/false, /*doNavigation=*/false);
	check(ms->autoParam->getCurrentValue() == oldValue, "pilot: undo restores engine value");
	check(ms->doc()->current_value == oldValue, "pilot: undo restores DOM store scalar");

	// 5. No-op guard: applying the current value opens no new Action.
	actionLogger.deleteAllLogs();
	{
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), ms->autoParam->getCurrentValue(), 0, 0, ms};
		deluge::edits::apply(ctx, edit);
	}
	check(actionLogger.firstAction[BEFORE] == nullptr, "pilot: value-unchanged apply opens no Action");
}
```

**Harness watch-points** (surface these at the Task-3 review checkpoint rather than working around silently): `ActionLogger::getNewAction` snapshots view/clip state off globals (`getCurrentUI()`, `currentSong`) — `currentSong` is the test's empty Song, fine; if a null current-UI crashes it, fix the *test setup* (there will be a sim-harness way the other harnesses use), not the production guard. `BEFORE` is the `TimeType` constant used to index `firstAction` exactly as `action_logger.cpp` does.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-sim -j --target edit_test`
Expected: FAIL — `model/edits/set_param_value.h: No such file or directory`

- [ ] **Step 3: Implement the pilot Edit (pure funnel)**

Create `src/deluge/model/edits/set_param_value.h`:

```cpp
#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::edits {

/// Pilot Edit (M1b): set a param's value from a user knob-turn — the reification of the mutation
/// in View::modEncoderAction_existentParam. `pos`/`length` mirror the view's modPos/modLength
/// (length 0 = plain scalar turn; nonzero = held-pad region edit in automation view).
class SetParamValue final : public Edit {
public:
	SetParamValue(std::optional<project::ParamAddress> address, int32_t newValue, int32_t pos, int32_t length,
	              ModelStackWithAutoParam* engineStack)
	    : address_(address), newValue_(newValue), pos_(pos), length_(length), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	/// Id-identity of the lane (nullopt = never-saved owner, engine-resolved only). Unused by the
	/// funnel-phase body; it is the durable name the M4 event feed and later model-first writes key on.
	std::optional<project::ParamAddress> address_;
	int32_t newValue_;
	int32_t pos_;
	int32_t length_;
	/// Funnel-phase borrow (migration plan, module topology): the engine-side resolution the
	/// mutation still delegates to. Valid only for the duration of apply(); never stored beyond
	/// it. Retired when the engine consumes model events (M4) instead of being poked in-line.
	ModelStackWithAutoParam* engineStack_;
};

} // namespace deluge::edits
```

Create `src/deluge/model/edits/set_param_value.cpp`:

```cpp
#include "model/edits/set_param_value.h"

#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error SetParamValue::apply(Context& /*ctx*/) {
	// Funnel phase: delegate to the exact engine mutation the call site used, behaviour-identical.
	// Task 4 moves ownership of the undo Consequence up here.
	engineStack_->autoParam->setValuePossiblyForRegion(newValue_, engineStack_, pos_, length_);
	return Error::NONE;
}

} // namespace deluge::edits
```

- [ ] **Step 4: Swap the call site**

In `src/deluge/gui/views/view.cpp`, replace lines 926-927:

```cpp
	// Perform the actual change
	modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos, modLength);
```

with:

```cpp
	// Perform the actual change, reified as the pilot Edit (M1b, docs/dev/project_model_migration_plan.md).
	deluge::edits::Context editContext{*currentSong, currentSong->project};
	deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), newValue,
	                                  static_cast<int32_t>(modPos), static_cast<int32_t>(modLength),
	                                  modelStackWithParam};
	deluge::edits::apply(editContext, edit);
```

Add to view.cpp's includes:

```cpp
#include "model/edits/edit.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
```

(`modPos`/`modLength` are `uint32_t` view members passed today into `int32_t` parameters by implicit conversion — the `static_cast<int32_t>` keeps the identical value flow, made explicit.)

- [ ] **Step 5: Run tests + full regression net**

Run: `cmake --build build-sim -j && ctest --test-dir build-sim --output-on-failure`
Expected: PASS (edit_test pilot cases + all existing tests)

Run: `python3 tests/golden/songs_run.py`
Expected: OK (serialization untouched)

Run: `python3 tests/golden/param_record_run.py`
Expected: OK (the param-edit-during-playback net — the closest coverage to this call site)

Run: `./dbt build Debug`
Expected: clean build

- [ ] **Step 6: Commit**

```bash
git add src/deluge/model/edits/set_param_value.h src/deluge/model/edits/set_param_value.cpp src/deluge/gui/views/view.cpp tests/projection/edit_test.cpp
git commit -m "edits: M1b — SetParamValue pilot Edit funnels the knob-turn call site (behaviour-identical)"
```

---

### Task 4: The Edit owns its Consequence (idempotent pre-log)

Move the production of the undo record to the Edit — the do-site and the undo-record-site become the same object. Mechanism: the Edit pre-logs via `actionLogger.recordUnautomatedParamChange` **before** delegating; the interior log at `auto_param.cpp:358-362` then hits `Action::containsConsequenceParamChange`'s (paramCollection, paramId) dedupe and is a no-op. The interior log stays until the M1c sweep retires it (other callers still rely on it). Do **not** wire the dead `shouldLogAction` parameter — making it live would change behaviour for existing callers that pass `false` and today get logging anyway.

**Files:**
- Modify: `src/deluge/model/edits/set_param_value.cpp` (the `apply` body)
- Test: `tests/projection/edit_test.cpp` (existing cases are the net; the pointer-equality dedupe check is what pins idempotence)

**Interfaces:**
- Consumes: `ActionLogger::recordUnautomatedParamChange(ModelStackWithAutoParam const*)`, `playbackHandler` globals, `TimelineCounter::isPlayingAutomationNow()` / `armedForRecording`.
- Produces: no interface change — `SetParamValue`'s signature is untouched.

- [ ] **Step 1: Encode the faithful guard**

The pre-log must fire in **exactly** the cases the interior log fires, evaluated pre-mutation. Derivation from `auto_param.cpp:192-372` (verify against the source while implementing):

- Region path (`length != 0 && timelineCounterIsSet()`) → interior `setValueForRegion` manages its own Action → **never pre-log**.
- Scalar path, `value == currentValue` → interior logs nothing → don't pre-log.
- Scalar, not playing → interior logs → pre-log.
- Scalar, playing, **recording-armed** → the recording branch handles logging (RECORD / AUTOMATION_DELETE), and its exits (`goto getOut`, shift early-return) skip line 358; post-mutation `nodes` is non-empty anyway → **never pre-log** (pre-logging here would create a spurious `PARAM_UNAUTOMATED_VALUE_CHANGE` Action — a behaviour change).
- Scalar, playing, not recording-armed → that branch doesn't touch nodes, so the interior's post-branch `nodes.empty()` equals the pre-mutation check → pre-log iff `nodes.empty()`.

Replace `SetParamValue::apply` in `src/deluge/model/edits/set_param_value.cpp`:

```cpp
#include "model/action/action_logger.h"
#include "model/timeline_counter.h"
#include "playback/playback_handler.h"

namespace deluge::edits {

Error SetParamValue::apply(Context& /*ctx*/) {
	// The Edit owns its undo record (migration plan M1: do-site == undo-record-site): snapshot the
	// before-state into the open Action up front, replicating the guard at the interior log site
	// (auto_param.cpp, end of setCurrentValueInResponseToUserInput) evaluated pre-mutation. The
	// interior log then dedupes via containsConsequenceParamChange — idempotent until the M1c sweep
	// retires it.
	bool scalarPath = !(length_ != 0 && engineStack_->timelineCounterIsSet());
	if (scalarPath && newValue_ != engineStack_->autoParam->getCurrentValue()) {
		bool isPlaying = playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn
		                 && (!engineStack_->timelineCounterIsSet()
		                     || engineStack_->getTimelineCounter()->isPlayingAutomationNow());
		bool recordingArmed = playbackHandler.recording != RecordingMode::OFF
		                      && engineStack_->timelineCounterIsSet()
		                      && engineStack_->getTimelineCounter()->armedForRecording;
		if (!(isPlaying && recordingArmed) && (engineStack_->doc()->nodes.empty() || !isPlaying)) {
			actionLogger.recordUnautomatedParamChange(engineStack_);
		}
	}

	// Funnel phase: delegate to the exact engine mutation the call site used.
	engineStack_->autoParam->setValuePossiblyForRegion(newValue_, engineStack_, pos_, length_);
	return Error::NONE;
}

} // namespace deluge::edits
```

(Adjust include paths/member names against the real headers if they differ — e.g. `RecordingMode`'s home and `armedForRecording`'s exact spelling; the guard's *logic* is the contract, taken from `auto_param.cpp:202-210` and `358-362`.)

- [ ] **Step 2: Run the full net**

Run: `cmake --build build-sim -j && ctest --test-dir build-sim --output-on-failure`
Expected: PASS — in particular `pilot: before-state not re-snapshotted` (the pointer-equality check), which fails if the pre-log double-records, and `pilot: value-unchanged apply opens no Action`, which fails if the guard over-fires.

Run: `python3 tests/golden/param_record_run.py`
Expected: OK — this is the net for the playing/recording cases the guard must exclude.

Run: `python3 tests/golden/songs_run.py` and `./dbt build Debug`
Expected: OK / clean build

- [ ] **Step 3: Commit**

```bash
git add src/deluge/model/edits/set_param_value.cpp
git commit -m "edits: M1b — SetParamValue owns its undo Consequence (idempotent pre-log)"
```

---

## Out of scope (planned next, separately)

- **M1c** — sweep the remaining param mutation call-sites into Edits (menus, automation view, patch-cable amounts, MIDI-CC, `param_edit` paths); retire the interior log once all callers are Edits. Plan it after this pilot lands — the pilot teaches the recipe.
- **M1d** — reduced scope: the save/load round-trip gate **already exists** (`tests/golden/songs_run.py` settled-fixed-point + committed hashes); what remains is the **undo/redo scripting hook** over a loaded song (script edit→undo→compare, edit→undo→redo→compare), which also supplies the loaded-song, DOM-resolved-address coverage this plan's unit harness doesn't reach.
- Hardware knob-turn smoke test rides the next flash (M1 milestone boundary), not per-task.
