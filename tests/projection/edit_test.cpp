// Unit harness for the Edit substrate (M1a/M1b, docs/dev/project_model_migration_plan.md):
// dispatcher wiring, then the SetParamValue pilot, its Consequence production, coalescence, and
// undo. Runs in the sim environment (real UnpatchedParamSet + ActionLogger + host allocator) on
// x86 (build-sim) and ARM (build-sim-arm via qemu).
//
// Exit: 0 = all checks pass, 1 = a failure.

#include "definitions_cxx.hpp"
#include "model/edits/edit.h"
#include <cstdio>

#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
#include "model/model_stack.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include <cstring>

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

// Build a ModelStackWithAutoParam over a real UnpatchedParamSet, song-less (=> addressFor()
// nullopt; doc() falls back to the collection's own store) — the param_automation_test pattern,
// but with a real collection so remotelySwapParamState/undo is the real engine path.
ModelStackWithAutoParam* makeStack(char* buf, ParamManagerForTimeline& pm, int32_t paramId) {
	std::memset(buf, 0, MODEL_STACK_MAX_SIZE);
	auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
	ms->paramCollection = pm.getUnpatchedParamSet();
	ms->summary = pm.getUnpatchedParamSetSummary();
	ms->kind = pm.getUnpatchedParamSet()->getParamKind(); // keep the threaded kind consistent (doc() ALPHA-asserts)
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

	// 2. Undo record: one open Action of the right type, holding a param Consequence. (The param
	//    consequence lives on the generic firstConsequence list — the firstParamConsequence member
	//    is vestigial/never populated; containsConsequenceParamChange is what the engine searches.)
	Action* action = actionLogger.firstAction[BEFORE];
	check(action != nullptr, "pilot: an Action was opened");
	check(action->type == ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE, "pilot: Action type is param change");
	check(action->containsConsequenceParamChange(ms->paramCollection, ms->paramId),
	      "pilot: ConsequenceParamChange recorded");
	check(action->nextAction == nullptr, "pilot: exactly one Action");
	Consequence* firstCons = action->firstConsequence;
	check(firstCons != nullptr, "pilot: consequence present on the Action");

	// 3. Coalescence + dedupe: a second turn of the same knob merges into the SAME Action and
	//    does NOT re-snapshot (containsConsequenceParamChange dedupe) — pointer-equality pins both.
	{
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), newValue + 100, 0, 0, ms};
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "pilot: second apply succeeds");
	}
	check(actionLogger.firstAction[BEFORE] == action, "pilot: second turn coalesced into same Action");
	check(action->nextAction == nullptr, "pilot: still exactly one Action");
	check(action->firstConsequence == firstCons, "pilot: before-state not re-snapshotted");
	check(ms->autoParam->getCurrentValue() == newValue + 100, "pilot: second value applied");
	// M1c S6 invariant: the Edit's pre-log recorded exactly one ConsequenceParamChange, and the
	// interior log at auto_param.cpp did NOT add a second (containsConsequenceParamChange dedupe).
	// This is why the interior log is redundant for every Edit path — only the recorded-input MPE
	// producer still relies on it (see the M4 dependency at the interior log site).
	int32_t paramConsequences = 0;
	for (Consequence* c = action->firstConsequence; c != nullptr; c = c->next) {
		if (c->type == Consequence::PARAM_CHANGE) {
			paramConsequences++;
		}
	}
	check(paramConsequences == 1, "pilot/S6: interior log did not double-record (dedupe holds)");

	// 4. Undo restores the ORIGINAL value (the single snapshot is the pre-first-turn state). This
	//    pilot is behaviour-preserving: undo runs the exact same ConsequenceParamChange::revert as
	//    before, which swaps back the engine value + automation nodes. (Restoring the DOM store
	//    scalar through undo is model-authority work — M3+, docs/dev/project_model_migration_plan.md —
	//    not this funnel; step 1 already pins the forward store sync.)
	actionLogger.revert(BEFORE, /*updateVisually=*/false, /*doNavigation=*/false);
	check(ms->autoParam->getCurrentValue() == oldValue, "pilot: undo restores engine value");
	check(actionLogger.firstAction[BEFORE] == nullptr, "pilot: undo emptied the do-queue");

	// 5. No-op guard: re-applying the (now restored) current value changes nothing, so no interior
	//    log fires and no new Action opens. (No deleteAllLogs here: freeing an Action's consequences
	//    pumps AudioEngine::routine, which this bare unit harness does not initialise; the redo-queue
	//    Action is simply left to leak at process exit.)
	{
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), ms->autoParam->getCurrentValue(), 0, 0, ms};
		deluge::edits::apply(ctx, edit);
	}
	check(actionLogger.firstAction[BEFORE] == nullptr, "pilot: value-unchanged apply opens no Action");
}

// M1c S2: the extended ctor (pos/length region args + the mayDelete funnel field). The song-less
// standalone stack has no timeline counter, so setValuePossiblyForRegion routes to the scalar branch
// regardless; this pins that the 6-arg ctor with mayDelete=false compiles, applies, and mutates.
void testSetParamValueRegionAndFlag(deluge::edits::Context& ctx) {
	// UNPATCHED_TEMPO is the one param shouldRecordUnautomatedParamChange skips, so apply() opens no
	// Action here — deliberately: this case asserts only ctor + value plumbing, and opening an Action
	// would make getNewAction free a prior test's redo-queue Action, pumping AudioEngine::routine
	// (uninitialised in this bare harness — see testSetParamValuePilot).
	constexpr int32_t kParam = deluge::modulation::params::UNPATCHED_TEMPO;

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);

	int32_t target = ms->autoParam->getCurrentValue() + 250;
	deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), target, 0, 0, ms, /*mayDelete=*/false};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "region/flag: apply succeeds");
	check(ms->autoParam->getCurrentValue() == target, "region/flag: value applied");
}

} // namespace

// Song construction; the Context members are stored, and the pilot dereferences currentSong via the
// engine mutation path (ActionLogger snapshots off currentSong->project.clips).
#include "hid/display/seven_segment.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/global_effectable/global_effectable.h"
#include "model/song/song.h"

int main() {
	display = new deluge::hid::display::SevenSegment;

	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	currentSong = song;

	// ActionLogger::getNewAction snapshots an ActionClipState per clip and sizes that allocation to the clip
	// count — a zero-clip song makes it a 0-byte alloc (=> nullptr => no Action opens). A real song always has
	// clips; give the harness one AUDIO clip (grabFromClip is a no-op for non-instrument clips) so the undo path
	// the pilot exercises behaves as it does on-device.
	(void)song->project.clips.session_clips.insertClipAt(new AudioClip(), 0);

	deluge::edits::Context ctx{*song, song->project};
	testDispatcher(ctx);
	testSetParamValuePilot(ctx);
	testSetParamValueRegionAndFlag(ctx);

	if (failures == 0) {
		std::fprintf(stdout, "edit_test: OK\n");
	}
	return failures ? 1 : 0;
}
