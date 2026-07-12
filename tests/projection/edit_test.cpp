// Unit harness for the Edit substrate (M1a/M1b, docs/dev/project_model_migration_plan.md):
// dispatcher wiring, then the SetParamValue pilot, its Consequence production, coalescence, and
// undo. Runs in the sim environment (real UnpatchedParamSet + ActionLogger + host allocator) on
// x86 (build-sim) and ARM (build-sim-arm via qemu).
//
// Exit: 0 = all checks pass, 1 = a failure.

#include "definitions_cxx.hpp"
#include "model/edits/edit.h"
#include <cstdio>

#include "deluge-project/param_edit.h" // setNodeAtPos
#include "deluge-project/song.h"       // deluge::project::Song::resolveParam (equivalence spec)
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence.h"
#include "model/edits/delete_param_automation.h"
#include "model/edits/param_address_for.h"
#include "model/edits/paste_param_automation.h"
#include "model/edits/set_param_value.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/snapshot/param_snapshot.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
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

// M1c coda: DeleteParamAutomation funnels the AUTOMATION_DELETE sites. Seed automation, delete via the Edit,
// assert the lane cleared and an AUTOMATION_DELETE Action holds the (stolen-nodes) undo Consequence.
void testDeleteParamAutomation(deluge::edits::Context& ctx) {
	constexpr int32_t kParam = 5;
	// Leak-reset (crash-safe: no consequence freeing -> no headless audio pump).
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);

	deluge::project::setNodeAtPos(*ms->doc(), 0, 100, false);
	deluge::project::setNodeAtPos(*ms->doc(), 32, -200, true);
	check(!ms->doc()->nodes.empty(), "delete: seeded automation");

	deluge::edits::DeleteParamAutomation edit{deluge::edits::addressFor(*ms), ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "delete: apply succeeds");
	check(ms->doc()->nodes.empty(), "delete: automation cleared");

	Action* a = actionLogger.firstAction[BEFORE];
	check(a != nullptr && a->type == ActionType::AUTOMATION_DELETE, "delete: AUTOMATION_DELETE Action opened");
	check(a != nullptr && a->firstConsequence != nullptr, "delete: undo Consequence recorded (stolen nodes)");
}

// M1c coda: PasteParamAutomation funnels the automation-paste site. Seed a clipboard, paste via the Edit,
// assert nodes written and an AUTOMATION_PASTE Action opened.
void testPasteParamAutomation(deluge::edits::Context& ctx) {
	constexpr int32_t kParam = 6;
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);
	// paste needs getLoopLength(); a no-timeline stack gets it from the noteRow's independent loop length.
	// song stays null (makeStack memset) so addressFor short-circuits to nullopt -> collection store.
	NoteRow nr;
	nr.setLoopLengthIfIndependent(128);
	ms->setNoteRow(&nr);
	// paste's automated-now path calls paramManager->notifyParamModifiedInSomeWay -> expectEvent.
	ms->paramManager = &pm;

	// Clipboard: two nodes on their own timeline; the first MUST be at pos 0.
	ParamNode copiedNodes[2];
	copiedNodes[0].pos = 0;
	copiedNodes[0].value = 500000000;
	copiedNodes[0].interpolated = false;
	copiedNodes[1].pos = 64;
	copiedNodes[1].value = -500000000;
	copiedNodes[1].interpolated = true;
	CopiedParamAutomation copied;
	copied.width = 128;
	copied.nodes = copiedNodes;
	copied.numNodes = 2;

	deluge::edits::PasteParamAutomation edit{deluge::edits::addressFor(*ms), 0, 128, 1.0f, &copied,
	                                         /*isPatchCable=*/false,         ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "paste: apply succeeds");
	check(!ms->doc()->nodes.empty(), "paste: nodes written");

	Action* a = actionLogger.firstAction[BEFORE];
	check(a != nullptr && a->type == ActionType::AUTOMATION_PASTE, "paste: AUTOMATION_PASTE Action opened");
}

// M4a Task 1 — characterize what the in-line poke does to the engine cursor AND the model store
// (deluge-project DocAutoParam) for a SCALAR edit and a REGION edit, driven through the real
// deluge::edits::apply. This PINS the {engine cursor, model current_value, model nodes} transition the
// event-patch (Tasks 2-3) must reproduce. `regionTimelineCounter` is a real Clip (its
// getCurrentlyRecordingLinearly() cast in homogenizeRegion must be valid; the value is inert here).
void testInlinePokeCharacterization(deluge::edits::Context& ctx, TimelineCounter* regionTimelineCounter) {
	// Leak-reset (crash-safe: no consequence freeing -> no headless audio pump).
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	constexpr int32_t kParam = 3;
	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);

	// (a) SCALAR edit (length 0): setValuePossiblyForRegion -> setCurrentValueInResponseToUserInput. song stays
	//     null so addressFor short-circuits to nullopt and doc() resolves to the collection's own store.
	{
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), 0x40000000, 0, 0, ms};
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "characterize/scalar: apply succeeds");
	}
	check(ms->autoParam->getCurrentValue() == 0x40000000, "characterize/scalar: engine cursor == value");
	// FINDING: the in-line SCALAR poke keeps the model store's current_value in sync (also asserted by the pilot's
	// step 1). So applyPatch's scalar path is a plain current_value write; no nodes are created for an
	// unautomated param.
	check(ms->doc()->current_value == 0x40000000, "characterize/scalar: model current_value synced");
	check(ms->doc()->nodes.empty(), "characterize/scalar: unautomated -> no nodes created");

	// (b) REGION edit (nonzero length + timeline counter): setValuePossiblyForRegion -> setValueForRegion, whose
	//     node/current_value mutation is a proven-equivalent DOM kernel deluge::project::setValueForRegion (see the
	//     exhaustive engine==DOM matrix in param_automation_test.cpp). The region dispatch needs
	//     timelineCounterIsSet(); getLoopLength() is satisfied by a NoteRow with an independent loop length (no
	//     Song deref). song stays null so addressFor -> nullopt -> the same collection store the scalar half used.
	NoteRow nr;
	nr.setLoopLengthIfIndependent(128);
	ms->setNoteRow(&nr);
	ms->setTimelineCounter(regionTimelineCounter);
	ms->paramManager = &pm;

	{
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), 0x20000000, 0, 96, ms};
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "characterize/region: apply succeeds");
	}
	deluge::project::DocAutoParam* doc = ms->doc();
	check(ms->autoParam->getCurrentValue() == 0x20000000, "characterize/region: engine cursor == value");
	check(doc->current_value == 0x20000000, "characterize/region: model current_value synced");
	// Region write homogenizes [0,96) to a flat segment. PIN the exact node vector the event-patch must reproduce
	// (values below filled from the actual run — this spec locks the true current transition).
	check(doc->nodes.size() == 2, "characterize/region: node count");
	check(doc->nodes[0].pos == 0 && doc->nodes[0].value == 0x20000000, "characterize/region: node0 = (0, value)");
	// node1 restores the value just PAST the region — the flat current_value the param carried before this edit,
	// i.e. the 0x40000000 the scalar edit above left on the same param — so [96,128) plays unchanged.
	check(doc->nodes[1].pos == 96 && doc->nodes[1].value == 0x40000000,
	      "characterize/region: node1 = (96, pre-region value)");
}

// M4a Task 2/3 — ParamSnapshot::applyPatch (the model-first event-patch) must land the model store in the SAME
// state Task 1 pinned for the in-line poke. `clipTC` is a real Clip with an id and a loop length, so the edit's
// ParamAddress resolves (song set) and doc() relocates the touched store into the model tree — the realistic
// on-device situation where the engine cursor and the model store are the same DocAutoParam.
void testParamSnapshotEquivalence(deluge::edits::Context& ctx, ::Song* engineSong, TimelineCounter* clipTC) {
	// Leak-reset (crash-safe: no consequence freeing -> no headless audio pump).
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	constexpr int32_t kParam = 4;
	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	// An audio clip's params are UNPATCHED_GLOBAL; the bare setupUnpatched leaves the set's kind NONE (it is
	// normally stamped by GlobalEffectable::initParams). Set it so the address resolves in the model tree
	// (slotInManager keys the unpatched slot by UNPATCHED_SOUND/UNPATCHED_GLOBAL).
	pm.getUnpatchedParamSet()->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);
	ms->song = engineSong;
	ms->setTimelineCounter(clipTC);
	ms->paramManager = &pm;

	// The edit's durable address, and the store it resolves to. First doc() touch relocates pm's store into the
	// model's clip_params tree (the S5-b lazy relocation) so resolveParam and doc() return the same DocAutoParam.
	std::optional<deluge::project::ParamAddress> addrOpt = deluge::edits::addressFor(*ms);
	check(addrOpt.has_value(), "equivalence: audio-clip edit has a resolvable ParamAddress");
	deluge::project::ParamAddress addr = *addrOpt;
	deluge::project::DocAutoParam* store = ms->doc();
	check(store != nullptr && ctx.model.resolveParam(addr) == store,
	      "equivalence: address resolves to the same store doc() uses");

	// SCALAR via applyPatch (region absent): reproduces Task 1's pinned scalar transition.
	deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, addr, 0x40000000, 0, 0, std::nullopt);
	check(store->current_value == 0x40000000, "applyPatch scalar: model current_value == pinned");
	check(store->nodes.empty(), "applyPatch scalar: no nodes created");

	// REGION via applyPatch: context mirrors Task 1's region edit (effLen 128, no interpolation, clock inactive).
	// Asserts the SAME {nodes, current_value} Task 1 pinned for the in-line region poke.
	deluge::snapshot::RegionContext rc{/*effectiveLength*/ 128,
	                                   /*interpolateLeftNode*/ false,
	                                   /*interpolateRightNode*/ false,
	                                   /*recordingLinearly*/ false,
	                                   /*clockActive*/ false,
	                                   /*livePos*/ 0,
	                                   /*currentlyPlayingReversed*/ false};
	deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, addr, 0x20000000, 0, 96, rc);
	check(store->nodes.size() == 2, "applyPatch region: node count == pinned");
	check(store->nodes[0].pos == 0 && store->nodes[0].value == 0x20000000, "applyPatch region: node0 == pinned");
	check(store->nodes[1].pos == 96 && store->nodes[1].value == 0x40000000, "applyPatch region: node1 == pinned");
	check(store->current_value == 0x20000000, "applyPatch region: model current_value == pinned");
}

// M4a Task 3 — SetParamValue::emitEvent drives ParamSnapshot::applyPatch behind the A/B toggle. Isolates the
// event path (calls emitEvent directly, on a store seeded to a baseline the in-line poke did NOT write) so the
// patch's own effect is what's asserted: toggle OFF leaves the store untouched; toggle ON reproduces Task 1's
// pinned {current_value, nodes} for a scalar AND a region edit.
void testEmitEventDrivesPatch(deluge::edits::Context& ctx, ::Song* engineSong, TimelineCounter* clipTC) {
	// Leak-reset (crash-safe: no consequence freeing -> no headless audio pump).
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	constexpr int32_t kParam = 5;
	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	pm.getUnpatchedParamSet()->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);
	ms->song = engineSong;
	ms->setTimelineCounter(clipTC);
	ms->paramManager = &pm;

	std::optional<deluge::project::ParamAddress> addrOpt = deluge::edits::addressFor(*ms);
	check(addrOpt.has_value(), "emitEvent: edit has a resolvable ParamAddress");
	deluge::project::ParamAddress addr = *addrOpt;
	deluge::project::DocAutoParam* store = ms->doc();
	check(store != nullptr && ctx.model.resolveParam(addr) == store, "emitEvent: address resolves to the model store");

	auto seedBaseline = [&] {
		store->nodes.erase(store->nodes.begin(), store->nodes.end());
		store->current_value = 0x11111111; // a value neither edit writes, so any change is the patch's doing
	};

	// A) toggle OFF (the default / firmware build): emitEvent must NOT patch the model store — the in-line poke in
	//    apply() is the sole path, which is what keeps goldens bit-identical.
	seedBaseline();
	deluge::edits::m4EventPatchToggle() = false;
	{
		deluge::edits::SetParamValue edit{addr, 0x40000000, 0, 0, ms};
		edit.emitEvent(ctx);
	}
	check(store->current_value == 0x11111111 && store->nodes.empty(), "emitEvent OFF: store untouched");

	// B) toggle ON, SCALAR: emitEvent -> applyPatch(scalar) reaches the pinned current_value.
	seedBaseline();
	deluge::edits::m4EventPatchToggle() = true;
	{
		deluge::edits::SetParamValue edit{addr, 0x40000000, 0, 0, ms};
		edit.emitEvent(ctx);
	}
	check(store->current_value == 0x40000000 && store->nodes.empty(), "emitEvent ON scalar: current_value == pinned");

	// C) toggle ON, REGION: emitEvent gathers the region context from the engine stack (effLen 128 from the clip's
	//    loop length; clock inactive; not on the automation view) and drives applyPatch(region) -> the pinned node
	//    vector. node1 restores the pre-region value (the 0x11111111 baseline current_value).
	seedBaseline();
	{
		deluge::edits::SetParamValue edit{addr, 0x20000000, 0, 96, ms};
		edit.emitEvent(ctx);
	}
	check(store->nodes.size() == 2, "emitEvent ON region: node count == pinned");
	check(store->nodes[0].pos == 0 && store->nodes[0].value == 0x20000000, "emitEvent ON region: node0 == pinned");
	check(store->nodes[1].pos == 96 && store->nodes[1].value == 0x11111111, "emitEvent ON region: node1 == pinned");
	check(store->current_value == 0x20000000, "emitEvent ON region: current_value == pinned");

	deluge::edits::m4EventPatchToggle() =
	    false; // restore the default so the rest of the harness / default build is unpatched
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
	auto* audioClip = new AudioClip(*song);
	audioClip->setLoopLength(128); // give the region-edit timeline counter a loop length (getLoopLength())
	(void)song->project.clips.session_clips.insertClipAt(audioClip, 0);

	deluge::edits::Context ctx{*song, song->project};
	testDispatcher(ctx);
	testSetParamValuePilot(ctx);
	testSetParamValueRegionAndFlag(ctx);
	testDeleteParamAutomation(ctx);
	testPasteParamAutomation(ctx);
	testInlinePokeCharacterization(ctx, audioClip);
	testParamSnapshotEquivalence(ctx, song, audioClip);
	testEmitEventDrivesPatch(ctx, song, audioClip);

	if (failures == 0) {
		std::fprintf(stdout, "edit_test: OK\n");
	}
	return failures ? 1 : 0;
}
