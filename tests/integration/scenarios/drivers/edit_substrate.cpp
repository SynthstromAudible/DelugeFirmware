// Unit harness for the Edit substrate:
// dispatcher wiring, then the SetParamValue pilot, its Consequence production, coalescence, and
// undo. Runs as the edit_substrate scenario (scenarios/drivers/driver.h): the real UnpatchedParamSet,
// ActionLogger and allocator, on x86 and, through the qemu-arm build of golden_vt_render, on ARM.
//
// Exit: 0 = all checks pass, 1 = a failure.

#include "definitions_cxx.hpp"
#include "model/edits/edit.h"
#include <cstdio>

#include "deluge-project/param_edit.h" // setNodeAtPos
#include "deluge-project/song.h"       // deluge::project::Song::resolveParam (equivalence spec)
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/consequence/consequence.h"
#include "model/edits/delete_param_automation.h"
#include "model/edits/param_address_for.h"
#include "model/edits/paste_param_automation.h"
#include "model/edits/set_param_value.h"
#include "model/global_effectable/global_effectable.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/snapshot/param_snapshot.h"
#include "model/song/song.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include <cstring>

#include "deluge-project/instrument_slot.h"
#include "gui/menu_item/setting_edit.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "model/drum/drum.h"
#include "model/edits/set_controls.h"
#include "model/edits/set_dx_patch.h"
#include "model/edits/set_effect_setting.h"
#include "model/edits/set_engine_setting.h"
#include "model/edits/set_source_ranges.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "processing/audio_output.h"
#include "processing/dx7/dx_patch.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/slot_coherence.h"
#include "serialization/tree.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"

namespace {

using deluge::scenarios::driver::Io;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};

namespace {

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(io.err, "FAIL: %s\n", what);
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
// nullopt; doc() falls back to the collection's own store) — the param_projection pattern,
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
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE]{};
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
	// Invariant: the Edit's pre-log recorded exactly one ConsequenceParamChange, and the
	// interior log at auto_param.cpp did NOT add a second (containsConsequenceParamChange dedupe).
	// This is why the interior log is redundant for every Edit path — only the recorded-input MPE
	// producer still relies on it (see the interior log site).
	int32_t paramConsequences = 0;
	for (Consequence* c = action->firstConsequence; c != nullptr; c = c->next) {
		if (c->type == Consequence::PARAM_CHANGE) {
			paramConsequences++;
		}
	}
	check(paramConsequences == 1, "pilot/S6: interior log did not double-record (dedupe holds)");

	// 4. Undo restores the ORIGINAL value (the single snapshot is the pre-first-turn state). Undo runs
	// ConsequenceParamChange::revert, which swaps back the engine value + automation nodes; it does not
	// restore the DOM store scalar (step 1 pins the forward store sync).
	actionLogger.revert(BEFORE, /*updateVisually=*/false, /*doNavigation=*/false);
	check(ms->autoParam->getCurrentValue() == oldValue, "pilot: undo restores engine value");
	check(actionLogger.firstAction[BEFORE] == nullptr, "pilot: undo emptied the do-queue");

	// 5. No-op guard: re-applying the restored current value changes nothing, so no interior
	//    log fires and no new Action opens. (No deleteAllLogs here: freeing an Action's consequences
	//    pumps AudioEngine::routine, which this bare unit harness does not initialise; the redo-queue
	//    Action is simply left to leak at process exit.)
	{
		deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), ms->autoParam->getCurrentValue(), 0, 0, ms};
		deluge::edits::apply(ctx, edit);
	}
	check(actionLogger.firstAction[BEFORE] == nullptr, "pilot: value-unchanged apply opens no Action");
}

// The extended ctor (pos/length region args + the mayDelete funnel field). The song-less
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
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE]{};
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);

	int32_t target = ms->autoParam->getCurrentValue() + 250;
	deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), target, 0, 0, ms, /*mayDelete=*/false};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "region/flag: apply succeeds");
	check(ms->autoParam->getCurrentValue() == target, "region/flag: value applied");
}

// DeleteParamAutomation funnels the AUTOMATION_DELETE sites. Seed automation, delete via the Edit,
// assert the lane cleared and an AUTOMATION_DELETE Action holds the (stolen-nodes) undo Consequence.
void testDeleteParamAutomation(deluge::edits::Context& ctx) {
	constexpr int32_t kParam = 5;
	// Leak-reset (crash-safe: no consequence freeing -> no headless audio pump).
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE]{};
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

// PasteParamAutomation funnels the automation-paste site. Seed a clipboard, paste via the Edit,
// assert nodes written and an AUTOMATION_PASTE Action opened.
void testPasteParamAutomation(deluge::edits::Context& ctx) {
	constexpr int32_t kParam = 6;
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE]{};
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

// Characterizes what the in-line poke does to the engine cursor AND the model store
// (deluge-project DocAutoParam) for a SCALAR edit and a REGION edit, driven through the real
// deluge::edits::apply. This PINS the {engine cursor, model current_value, model nodes} transition
// ParamSnapshot::applyPatch must reproduce. `regionTimelineCounter` is a real Clip (its
// getCurrentlyRecordingLinearly() cast in homogenizeRegion must be valid; the value is inert here).
void testInlinePokeCharacterization(deluge::edits::Context& ctx, TimelineCounter* regionTimelineCounter) {
	// Leak-reset (crash-safe: no consequence freeing -> no headless audio pump).
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	constexpr int32_t kParam = 3;
	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE]{};
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
	//     exhaustive engine==DOM matrix in param_projection.cpp). The region dispatch needs
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

// ParamSnapshot::applyPatch (the model-first event-patch) must land the model store in the SAME
// state the in-line poke does. `clipTC` is a real Clip with an id and a loop length, so the edit's
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
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE]{};
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);
	ms->song = engineSong;
	ms->setTimelineCounter(clipTC);
	ms->paramManager = &pm;

	// The edit's durable address, and the store it resolves to. First doc() touch relocates pm's store into the
	// model's clip_params tree (the lazy relocation) so resolveParam and doc() return the same DocAutoParam.
	std::optional<deluge::project::ParamAddress> addrOpt = deluge::edits::addressFor(*ms);
	check(addrOpt.has_value(), "equivalence: audio-clip edit has a resolvable ParamAddress");
	deluge::project::ParamAddress addr = *addrOpt;
	deluge::project::DocAutoParam* store = ms->doc();
	check(store != nullptr && ctx.model.resolveParam(addr) == store,
	      "equivalence: address resolves to the same store doc() uses");

	// SCALAR via applyPatch (region absent): reproduces the in-line poke's scalar transition.
	deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, addr, 0x40000000, 0, 0, std::nullopt);
	check(store->current_value == 0x40000000, "applyPatch scalar: model current_value == pinned");
	check(store->nodes.empty(), "applyPatch scalar: no nodes created");

	// REGION via applyPatch: context mirrors the in-line region edit (effLen 128, no interpolation, clock inactive).
	// Asserts the SAME {nodes, current_value} as the in-line region poke.
	deluge::snapshot::RegionContext rc{/*effectiveLength*/ 128,
	                                   /*interpolateLeftNode*/ false,
	                                   /*interpolateRightNode*/ false,
	                                   /*recordingLinearly*/ false,
	                                   /*clockActive*/ false,
	                                   /*livePos*/ 0,
	                                   /*currentlyPlayingReversed*/ false};
	std::optional<deluge::project::RegionWriteResult> regionResult =
	    deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, addr, 0x20000000, 0, 96, rc);
	check(regionResult.has_value() && regionResult->automationChanged
	          && regionResult->currentValueEffect == deluge::project::RegionCurrentValueEffect::Set,
	      "applyPatch region: automationChanged==true, effect Set (normal homogenize, clock inactive)");
	check(store->nodes.size() == 2, "applyPatch region: node count == pinned");
	check(store->nodes[0].pos == 0 && store->nodes[0].value == 0x20000000, "applyPatch region: node0 == pinned");
	check(store->nodes[1].pos == 96 && store->nodes[1].value == 0x40000000, "applyPatch region: node1 == pinned");
	check(store->current_value == 0x20000000, "applyPatch region: model current_value == pinned");

	// projectScalarFromStore derives the engine cursor from the store. Clear the nodes so
	// this is the scalar shape, write a fresh store current_value, then project — the engine cursor must track it.
	store->nodes.erase(store->nodes.begin(), store->nodes.end());
	int32_t prev = ms->autoParam->getCurrentValue();
	store->current_value = 0x33333333;
	ms->autoParam->projectScalarFromStore(ms, prev, /*isPlaying=*/false);
	check(ms->autoParam->getCurrentValue() == 0x33333333, "projectScalarFromStore: engine cursor synced from store");
}

// SetParamValue::apply writes the STOPPED-TRANSPORT region store model-first: it
// opens the same Action + snapshots the pre-state, calls ParamSnapshot::applyPatch (nodes + current_value via the DOM
// kernel), then projectRegionFromStore to sync the engine cursor. Asserts the homogenized node vector + synced
// cursor, and that undo (the snapshot taken before applyPatch) restores the pre-mutation state.
void testRegionFlipViaApply(deluge::edits::Context& ctx, ::Song* engineSong, TimelineCounter* clipTC) {
	// Leak-reset (crash-safe): orphan any prior actions rather than freeing them here.
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	constexpr int32_t kParam = 7;
	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	pm.getUnpatchedParamSet()->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE]{};
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);
	ms->song = engineSong;
	ms->setTimelineCounter(clipTC);
	ms->paramManager = &pm;

	std::optional<deluge::project::ParamAddress> addrOpt = deluge::edits::addressFor(*ms);
	check(addrOpt.has_value(), "region-flip: resolvable address");
	deluge::project::ParamAddress addr = *addrOpt;
	deluge::project::DocAutoParam* store = ms->doc(); // relocate the lane into the model tree
	check(store != nullptr && ctx.model.resolveParam(addr) == store, "region-flip: store resolves (model-first path)");

	// Seed a CONSISTENT scalar baseline (engine cursor AND store, as they'd be on-device) so the region's far-edge
	// node value and the undo target are predictable.
	store->nodes.erase(store->nodes.begin(), store->nodes.end());
	ms->autoParam->setCurrentValueBasicForSetup(*store, 0x40000000);

	{
		deluge::edits::SetParamValue edit{addr, 0x20000000, 0, 96, ms};
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "region-flip: apply succeeds");
	}
	// Model-first: applyPatch homogenized [0,96); projectRegionFromStore synced the cursor (clock inactive).
	check(store->nodes.size() == 2, "region-flip: node count (homogenized)");
	check(store->nodes[0].pos == 0 && store->nodes[0].value == 0x20000000, "region-flip: node0 == value");
	check(store->nodes[1].pos == 96 && store->nodes[1].value == 0x40000000, "region-flip: node1 == pre-region value");
	check(store->current_value == 0x20000000, "region-flip: model current_value synced");
	check(ms->autoParam->getCurrentValue() == 0x20000000, "region-flip: engine cursor synced");

	// Undo restores the pre-mutation state (the snapshot was captured before applyPatch): nodes cleared.
	actionLogger.revert(BEFORE, /*updateVisually=*/false, /*doNavigation=*/false);
	check(store->nodes.empty(), "region-flip: undo cleared the nodes");
	check(ms->autoParam->getCurrentValue() == 0x40000000, "region-flip: undo restored engine value");
}

// SetParamValue::apply writes the SCALAR store model-first: on a resolvable lane it calls
// ParamSnapshot::applyPatch (the sole scalar-store writer) then projectScalarFromStore to sync the engine cursor.
// Asserts both the model store and the engine cursor track the edit, and undo (the
// pre-log path) restores the engine value.
void testScalarFlipViaApply(deluge::edits::Context& ctx, ::Song* engineSong, TimelineCounter* clipTC) {
	// Leak-reset (crash-safe): orphan any prior actions rather than freeing them here.
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;

	constexpr int32_t kParam = 6;
	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	pm.getUnpatchedParamSet()->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE]{};
	ModelStackWithAutoParam* ms = makeStack(buf, pm, kParam);
	ms->song = engineSong;
	ms->setTimelineCounter(clipTC);
	ms->paramManager = &pm;

	std::optional<deluge::project::ParamAddress> addrOpt = deluge::edits::addressFor(*ms);
	check(addrOpt.has_value(), "scalar-flip: resolvable address");
	deluge::project::ParamAddress addr = *addrOpt;
	deluge::project::DocAutoParam* store = ms->doc(); // relocate the lane into the model tree
	check(store != nullptr && ctx.model.resolveParam(addr) == store, "scalar-flip: store resolves (model-first path)");

	int32_t oldValue = ms->autoParam->getCurrentValue();
	int32_t newValue = oldValue + 12345;

	{
		deluge::edits::SetParamValue edit{addr, newValue, 0, 0, ms};
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "scalar-flip: apply succeeds");
	}
	// Model-first: applyPatch wrote the store; projectScalarFromStore synced the engine cursor from it.
	check(store->current_value == newValue,
	      "scalar-flip: model store current_value updated (applyPatch is the writer)");
	check(ms->autoParam->getCurrentValue() == newValue, "scalar-flip: engine cursor synced (projectScalarFromStore)");
	check(store->nodes.empty(), "scalar-flip: unautomated scalar -> no nodes");

	// Undo path: the Edit's pre-log recorded the before-state, so revert restores the engine value.
	actionLogger.revert(BEFORE, /*updateVisually=*/false, /*doNavigation=*/false);
	check(ms->autoParam->getCurrentValue() == oldValue, "scalar-flip: undo restores engine value");
}

} // namespace

// Song construction; the Context members are stored, and the pilot dereferences currentSong via the
// engine mutation path (ActionLogger snapshots off currentSong->live.clips).

// A synth added to the song is bound to its slot; a setting Edit names the slot. A mapped path's Edit changes the sound
// and its slot together; a path the schema does not hold, a structured one and a value out of range are refused and
// leave the slot.
void testSetEngineSetting(deluge::edits::Context& ctx) {
	using deluge::serialization::Value;
	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	check(synth != nullptr, "setting: a new synth");
	if (synth == nullptr) {
		return;
	}
	ctx.song.addOutput(synth);
	Sound& sound = *static_cast<SoundInstrument*>(synth);
	const std::optional<deluge::project::SlotAddress> slot = deluge::bridge::slotAddressOf(sound);
	check(slot.has_value(), "setting: the added synth has a slot address");
	if (!slot) {
		return;
	}
	check(deluge::bridge::soundAt(ctx.song, *slot) == &sound, "setting: the slot address names the synth");

	deluge::edits::SetEngineSetting edit{*slot, "voicePriority", Value::makeNumber(2)};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "setting: a mapped path is applied");
	check(sound.snapshot().voice_priority == VoicePriority::HIGH, "setting: the sound takes the value");
	const std::string* held = deluge::serialization::text_of(ctx.model.resolveSlot(*slot)->state.find("voicePriority"));
	check(held != nullptr && *held == "2", "setting: the slot holds the value");

	const Value before = ctx.model.resolveSlot(*slot)->state;
	deluge::edits::SetEngineSetting unknown{*slot, "noSuchSetting", Value::makeNumber(3)};
	check(deluge::edits::apply(ctx, unknown) != Error::NONE, "setting: a path the schema does not hold is refused");
	deluge::edits::SetEngineSetting structured{*slot, "osc1/fileName", Value::makeScalar("SAMPLES/A.WAV")};
	check(deluge::edits::apply(ctx, structured) != Error::NONE, "setting: a structured path is refused");
	deluge::edits::SetEngineSetting outOfRange{*slot, "voicePriority", Value::makeNumber(9)};
	check(deluge::edits::apply(ctx, outOfRange) != Error::NONE, "setting: a value out of range is refused");
	check(deluge::serialization::first_difference(ctx.model.resolveSlot(*slot)->state, before).empty(),
	      "setting: a refused Edit leaves the slot");
	check(sound.snapshot().voice_priority == VoicePriority::HIGH, "setting: a refused Edit leaves the sound");

	// An LFO's sync level is the level itself, whatever the song's tick magnitude: every level the sync menu offers
	// is taken and lands as chosen.
	auto& transport = ctx.model.transport;
	const int32_t oldMagnitude = transport.inside_world_tick_magnitude;
	const int32_t oldOffset = transport.inside_world_tick_magnitude_offset_from_bpm;
	transport.inside_world_tick_magnitude_offset_from_bpm = 0;
	for (int32_t magnitude = 0; magnitude <= 3; magnitude++) {
		transport.inside_world_tick_magnitude = magnitude;
		for (int32_t level = 0; level <= 9; level++) {
			deluge::edits::SetEngineSetting sync{*slot, "lfo1/syncLevel", Value::makeNumber(level)};
			const bool taken = deluge::edits::apply(ctx, sync) == Error::NONE;
			const bool landed = static_cast<int32_t>(sound.snapshot().lfo_config[0].sync_level) == level;
			if (!taken || !landed) {
				std::fprintf(io.err, "setting: lfo1/syncLevel=%d at magnitude %d %s\n", level, magnitude,
				             taken ? "landed elsewhere" : "refused");
				check(false, "setting: every LFO sync level is taken as chosen");
			}
		}
	}
	// A slot written in part keeps the magnitude the whole of it was written at: its effects' sync levels, which only
	// a whole write re-encodes, are relative to it too.
	const int32_t stamped = ctx.model.resolveSlot(*slot)->tick_magnitude;
	transport.inside_world_tick_magnitude = stamped + 1;
	deluge::edits::SetEngineSetting partial{*slot, "voicePriority", Value::makeNumber(1)};
	check(deluge::edits::apply(ctx, partial) == Error::NONE && ctx.model.resolveSlot(*slot)->tick_magnitude == stamped
	          && deluge::harness::soundIncoherence(sound, ctx.song).empty(),
	      "setting: a slot written in part keeps its magnitude");
	transport.inside_world_tick_magnitude = oldMagnitude;
	transport.inside_world_tick_magnitude_offset_from_bpm = oldOffset;

	// A live-input oscillator interpolates too: the interpolation menu offers it there.
	deluge::edits::SetEngineSetting input{*slot, "osc1/type", Value::makeScalar("inStereo")};
	check(deluge::edits::apply(ctx, input) == Error::NONE, "setting: an oscillator becomes live input");
	deluge::edits::SetEngineSetting linear{*slot, "osc1/linearInterpolation", Value::makeNumber(1)};
	check(deluge::edits::apply(ctx, linear) == Error::NONE
	          && sound.sources[0].sampleControls.interpolationMode == InterpolationMode::LINEAR,
	      "setting: a live-input oscillator takes linear interpolation");
}

// A source's ranges change in place through an Edit; a list that would assign a file or reshape the list is refused
// and changes nothing.
void testSetSourceRanges(deluge::edits::Context& ctx) {
	using deluge::serialization::Value;
	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	check(synth != nullptr, "ranges: a new synth");
	if (synth == nullptr) {
		return;
	}
	ctx.song.addOutput(synth);
	Sound& sound = *static_cast<SoundInstrument*>(synth);
	const auto slot = deluge::bridge::slotAddressOf(sound);
	check(slot.has_value(), "ranges: the synth has a slot");
	if (!slot) {
		return;
	}
	deluge::edits::SetEngineSetting sample{*slot, "osc1/type", Value::makeScalar("sample")};
	check(deluge::edits::apply(ctx, sample) == Error::NONE, "ranges: osc1 plays samples");
	sound.sources[0].getOrCreateFirstRange();
	std::vector<deluge::instrument::RangeState> ranges = deluge::bridge::rangesOf(sound.sources[0]);
	check(ranges.size() == 1, "ranges: one range");
	if (ranges.size() != 1) {
		return;
	}

	ranges[0].start_pos = 100;
	ranges[0].end_pos = 900;
	ranges[0].transpose = 5;
	deluge::edits::SetSourceRanges zone{*slot, 0, ranges};
	check(deluge::edits::apply(ctx, zone) == Error::NONE, "ranges: an in-place zone is applied");
	auto* holder = &static_cast<MultisampleRange*>(sound.sources[0].ranges.getElement(0))->sampleHolder;
	check(holder->startPos == 100 && holder->endPos == 900 && holder->transpose == 5, "ranges: the runtime takes it");
	check(deluge::harness::soundIncoherence(sound, ctx.song).empty(), "ranges: the sound stays coherent");

	// A gesture that changed the runtime itself records the result: the Edit carries the runtime's own list.
	holder->endPos = 800;
	deluge::edits::SetSourceRanges record{*slot, 0, deluge::bridge::rangesOf(sound.sources[0])};
	check(deluge::edits::apply(ctx, record) == Error::NONE, "ranges: the runtime's own list is taken");
	check(deluge::harness::soundIncoherence(sound, ctx.song).empty(), "ranges: recording a gesture's result reseeds");
	ranges = deluge::bridge::rangesOf(sound.sources[0]);

	const Value before = ctx.model.resolveSlot(*slot)->state;
	auto withFile = ranges;
	withFile[0].file_path = "SAMPLES/OTHER.WAV";
	deluge::edits::SetSourceRanges file{*slot, 0, withFile};
	check(deluge::edits::apply(ctx, file) != Error::NONE, "ranges: assigning a file is refused");
	check(deluge::harness::soundIncoherence(sound, ctx.song).empty(), "ranges: a refused list leaves it coherent");

	check(static_cast<MultisampleRange*>(sound.sources[0].ranges.getElement(0))->sampleHolder.filePath
	          != "SAMPLES/OTHER.WAV",
	      "ranges: a refused list leaves the runtime's file");
	auto longer = ranges;
	longer.insert(longer.begin(), deluge::instrument::RangeState{.top_note = 40});
	deluge::edits::SetSourceRanges reshape{*slot, 0, longer};
	check(deluge::edits::apply(ctx, reshape) != Error::NONE, "ranges: another length is refused");
	check(deluge::serialization::first_difference(ctx.model.resolveSlot(*slot)->state, before).empty(),
	      "ranges: a refusal leaves the slot");

	// A slot that says something the sound cannot take (another file) beside a setting: the setting is taken, the rest
	// is refused, and the sound and its slot agree afterwards.
	deluge::project::InstrumentSlot& held = *ctx.model.resolveSlot(*slot);
	held.state = deluge::bridge::withLeaf(held.state, "osc1/fileName", Value::makeScalar("SAMPLES/OTHER.WAV"));
	deluge::edits::SetEngineSetting priority{*slot, "voicePriority", Value::makeNumber(0)};
	check(deluge::edits::apply(ctx, priority) == Error::NONE && sound.snapshot().voice_priority == VoicePriority::LOW,
	      "ranges: a setting beside what the sound cannot take is taken");
	check(deluge::harness::soundIncoherence(sound, ctx.song).empty(), "ranges: and the sound stays coherent");

	// A malformed legacy file can leave two ranges on one top note: a change to one of them still takes, judged only
	// where it changes.
	for (int32_t i : {0, 1}) {
		MultiRange* range = sound.sources[0].ranges.insertMultiRange(i);
		check(range != nullptr, "ranges: a range inserted");
		if (range == nullptr) {
			return;
		}
		range->topNote = 60;
	}
	sound.sources[0].seedDocRanges();
	deluge::bridge::syncSlotState(sound, *ctx.model.resolveSlot(*slot), ctx.song);
	ranges = deluge::bridge::rangesOf(sound.sources[0]);
	ranges[2].end_pos = 700;
	deluge::edits::SetSourceRanges legacy{*slot, 0, ranges};
	check(deluge::edits::apply(ctx, legacy) == Error::NONE, "ranges: a change beside a duplicate top note is taken");
	ranges[0].top_note = 70;
	deluge::edits::SetSourceRanges outOfOrder{*slot, 0, ranges};
	check(deluge::edits::apply(ctx, outOfOrder) != Error::NONE, "ranges: a changed top note out of order is refused");

	// A source that plays no samples still holds its sample ranges, which a change of their zone reaches.
	deluge::edits::SetEngineSetting saw{*slot, "osc1/type", Value::makeScalar("saw")};
	check(deluge::edits::apply(ctx, saw) == Error::NONE, "ranges: osc1 becomes a saw");
	ranges = deluge::bridge::rangesOf(sound.sources[0]);
	ranges[2].start_pos = 123;
	deluge::edits::SetSourceRanges hidden{*slot, 0, ranges};
	check(deluge::edits::apply(ctx, hidden) == Error::NONE
	          && static_cast<MultisampleRange*>(sound.sources[0].ranges.getElement(2))->sampleHolder.startPos == 123,
	      "ranges: a saw's sample range takes a zone change");
	check(deluge::harness::soundIncoherence(sound, ctx.song).empty(), "ranges: the saw stays coherent");
}

// A new drum's id is one no drum record of the song holds: a drum whose id the counter has not passed (a hibernating
// kit's, one bound outside the load) keeps its record to itself.
void testDrumIdsAreUnique(deluge::edits::Context& ctx) {
	const uint32_t next = ctx.song.generateNewDrumId();
	ctx.model.drum_structure.getOrCreate(next + 1);
	const uint32_t minted = ctx.song.generateNewDrumId();
	check(minted != next + 1, "drum ids: a new id is not one a record already holds");
	check(ctx.model.drum_structure.find(minted) == nullptr, "drum ids: the new id has no record yet");
	ctx.model.drum_structure.remove(next + 1);

	const uint32_t nextOutput = ctx.song.generateNewOutputId();
	ctx.model.output_structure.getOrCreate(nextOutput + 1);
	const uint32_t mintedOutput = ctx.song.generateNewOutputId();
	check(mintedOutput != nextOutput + 1, "output ids: a new id is not one a record already holds");
	ctx.model.output_structure.remove(nextOutput + 1);
}

// Removing a drum or an output from a song drops its record only when the record is its own: an object of another
// song (the outgoing one, torn down after a swap) that shares an id leaves this song's record alone.
void testRemovalDropsOnlyOwnRecords(deluge::edits::Context& ctx) {
	const uint32_t id = ctx.song.generateNewDrumId();
	deluge::project::DocDrum& ours = ctx.model.drum_structure.getOrCreate(id);
	deluge::project::DocDrum theirs;
	Drum* stranger = StorageManager::createNewDrum(DrumType::SOUND);
	stranger->setId(id);
	stranger->bindDocDrum(&theirs);
	ctx.song.removeDocDrum(stranger);
	check(ctx.model.drum_structure.find(id) == &ours, "removal: another song's drum leaves this song's record");
	stranger->bindDocDrum(nullptr);
	ctx.model.drum_structure.remove(id);
	AudioEngine::retire(static_cast<SoundDrum*>(stranger));

	// A drum adopted into a running song with an id another drum's record already holds (a preset that carries ids)
	// gets an id of its own, so the two never share a record.
	Drum* first = StorageManager::createNewDrum(DrumType::SOUND);
	Drum* second = StorageManager::createNewDrum(DrumType::SOUND);
	const uint32_t shared = ctx.song.generateNewDrumId();
	first->setId(shared);
	second->setId(shared);
	ctx.song.adoptDrum(first);
	ctx.song.adoptDrum(second);
	check(second->docDrum_ != first->docDrum_, "adoption: a taken id is not shared");
	check(second->id() != first->id(), "adoption: the second drum gets an id of its own");

	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	const uint32_t outputId = ctx.song.generateNewOutputId();
	deluge::project::DocOutput& ourOutput = ctx.model.output_structure.getOrCreate(outputId);
	deluge::project::DocOutput theirOutput;
	synth->setId(outputId);
	synth->bindDocOutput(&theirOutput);
	ctx.song.removeDocOutput(synth);
	check(ctx.model.output_structure.find(outputId) == &ourOutput,
	      "removal: another song's output leaves this song's record");
	ctx.model.output_structure.remove(outputId);
}

// A DX7 source's patch changes through an Edit, its slot following; a source that is not a DX7 oscillator refuses one.
void testSetDxPatch(deluge::edits::Context& ctx) {
	using deluge::serialization::Value;
	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	ctx.song.addOutput(synth);
	Sound& sound = *static_cast<SoundInstrument*>(synth);
	const auto slot = deluge::bridge::slotAddressOf(sound);
	check(slot.has_value(), "dx: the synth has a slot");
	if (!slot) {
		return;
	}
	deluge::edits::SetEngineSetting dx7{*slot, "osc1/type", Value::makeScalar("dx7")};
	check(deluge::edits::apply(ctx, dx7) == Error::NONE, "dx: osc1 is a DX7");
	std::optional<deluge::instrument::DxPatchState> patch = deluge::bridge::dxPatchOf(sound.sources[0]);
	check(patch.has_value(), "dx: the DX7 source has a patch");
	if (!patch) {
		return;
	}
	patch->params[3] = static_cast<uint8_t>(patch->params[3] == 7 ? 8 : 7);
	patch->random_detune = 5;
	deluge::edits::SetDxPatch edit{*slot, 0, *patch};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "dx: a patch is applied");
	check(sound.sources[0].dxPatch->params[3] == patch->params[3] && sound.sources[0].dxPatch->random_detune == 5,
	      "dx: the runtime takes it");
	check(deluge::harness::soundIncoherence(sound, ctx.song).empty(), "dx: the sound stays coherent");

	deluge::edits::SetEngineSetting saw{*slot, "osc2/type", Value::makeScalar("saw")};
	(void)deluge::edits::apply(ctx, saw);
	deluge::edits::SetDxPatch refused{*slot, 1, *patch};
	check(deluge::edits::apply(ctx, refused) != Error::NONE, "dx: a saw oscillator refuses a patch");
}

// A knob's runtime descriptor and its mapping's target name the same param, both ways.
void testKnobTargets(deluge::edits::Context& /*ctx*/) {
	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	Sound& sound = *static_cast<SoundInstrument*>(synth);
	namespace params = deluge::modulation::params;
	for (int32_t button = 0; button < kNumModButtons; button++) {
		for (int32_t knob = 0; knob < kNumPhysicalModKnobs; knob++) {
			const ParamDescriptor d = sound.modKnobs[button][knob].paramDescriptor;
			check(deluge::bridge::descriptorOf(deluge::bridge::knobTargetOf(d, params::Kind::UNPATCHED_SOUND),
			                                   params::Kind::UNPATCHED_SOUND)
			          == d,
			      "knobs: a default knob's target leads back to its descriptor");
		}
	}
	ParamDescriptor cable;
	cable.setToHaveParamAndSource(params::LOCAL_LPF_FREQ, PatchSource::LFO_LOCAL_1);
	check(deluge::bridge::descriptorOf(deluge::bridge::knobTargetOf(cable, params::Kind::UNPATCHED_SOUND),
	                                   params::Kind::UNPATCHED_SOUND)
	          == cable,
	      "knobs: a cable depth's target leads back to its descriptor");
}

// A sound's knobs change through an Edit on its controller mappings: the runtime's mod knobs and learned MIDI knobs
// follow the record, and a learned knob keeps its takeover state through an edit of another knob.
void testSetControls(deluge::edits::Context& ctx) {
	namespace params = deluge::modulation::params;
	using deluge::project::ControllerMapping;
	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	ctx.song.addOutput(synth);
	Sound& sound = *static_cast<SoundInstrument*>(synth);
	const deluge::project::ControlsAddress address =
	    deluge::bridge::controlsAddressOf(sound).value_or(deluge::project::ControlsAddress{});
	std::vector<ControllerMapping>* record = ctx.model.resolveControls(address);
	check(record != nullptr && *record == deluge::bridge::controlsOf(sound),
	      "controls: an added synth's record holds its knobs");
	if (record == nullptr) {
		return;
	}

	ParamDescriptor resonance;
	resonance.setToHaveParamOnly(params::LOCAL_LPF_RESONANCE);
	std::vector<ControllerMapping> controls = *record;
	for (ControllerMapping& mapping : controls) {
		if (const auto* knob = std::get_if<deluge::project::ModKnobControl>(&mapping.control);
		    knob != nullptr && knob->button == 2 && knob->knob == 1) {
			mapping.target = deluge::bridge::knobTargetOf(resonance, params::Kind::UNPATCHED_SOUND);
		}
	}
	deluge::project::MidiCommand cc;
	cc.channel_or_zone = 0;
	cc.note_or_cc = 74;
	controls.push_back({deluge::project::MidiInputControl{cc, false},
	                    deluge::bridge::knobTargetOf(resonance, params::Kind::UNPATCHED_SOUND)});
	deluge::edits::SetControls edit{address, controls};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "controls: a mapping list is applied");
	check(sound.modKnobs[2][1].paramDescriptor == resonance, "controls: the runtime's mod knob follows");
	check(sound.midi_knobs.size() == 1, "controls: the learned knob is made");
	check(*ctx.model.resolveControls(address) == controls, "controls: the record holds the list");

	if (sound.midi_knobs.size() == 1) {
		sound.midi_knobs[0].previousPositionSaved = true;
		sound.midi_knobs[0].previousPosition = 77;
	}
	ParamDescriptor cutoff;
	cutoff.setToHaveParamOnly(params::LOCAL_LPF_FREQ);
	controls[2 * 2 + 1].target = deluge::bridge::knobTargetOf(cutoff, params::Kind::UNPATCHED_SOUND);
	deluge::edits::SetControls other{address, controls};
	check(deluge::edits::apply(ctx, other) == Error::NONE, "controls: another knob's edit is applied");
	check(sound.midi_knobs.size() == 1 && sound.midi_knobs[0].previousPositionSaved
	          && sound.midi_knobs[0].previousPosition == 77,
	      "controls: the learned knob keeps its takeover state");

	// A learned knob tied to a device keeps that very device through a record of its owner's knobs (a gold knob
	// pressed): one the name lookup would not find again (a second controller of the same name) included.
	static MIDICableUSBHosted twin;
	twin.name = "Twin";
	if (sound.midi_knobs.size() == 1) {
		sound.midi_knobs[0].midiInput.cable = &twin;
		deluge::edits::SetControls again{address, deluge::bridge::controlsOf(sound)};
		check(deluge::edits::apply(ctx, again) == Error::NONE, "controls: the owner's own knobs are recorded");
		check(sound.midi_knobs.size() == 1 && sound.midi_knobs[0].midiInput.cable == &twin,
		      "controls: a learned knob keeps its device through a record of its owner's knobs");
	}
}

// The knob gestures record their owner's mappings: a mod knob learned in the sound editor, and a gold knob pressed to
// cycle LPF, HPF and EQ, each leave the sound's controls record what its knobs are.
void testKnobGesturesRecord(deluge::edits::Context& ctx) {
	namespace params = deluge::modulation::params;
	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	ctx.song.addOutput(synth);
	Sound& sound = *static_cast<SoundInstrument*>(synth);
	const auto address = deluge::bridge::controlsAddressOf(sound);
	check(address.has_value(), "knob gestures: the synth has controls");
	if (!address) {
		return;
	}

	ParamDescriptor drive;
	drive.setToHaveParamOnly(params::LOCAL_LPF_RESONANCE);
	check(sound.learnKnob(nullptr, drive, 1, 3, 16, &ctx.song), "knob gestures: a mod knob is learned");
	deluge::gui::menu_item::recordControlsOf(sound);
	check(deluge::harness::soundIncoherence(sound, ctx.song).empty(), "knob gestures: a learn is recorded");

	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* ms =
	    setupModelStackWithThreeMainThingsButNoNoteRow(memory, &ctx.song, &sound, nullptr, &paramManager);
	for (int32_t press = 0; press < 3; press++) {
		(void)sound.modEncoderButtonAction(1, true, ms);
		deluge::gui::menu_item::recordControlsOf(sound);
		check(deluge::harness::soundIncoherence(sound, ctx.song).empty(), "knob gestures: a knob cycle is recorded");
	}
}

// A preset change hands the old instrument's learned knobs to the new one: both owners' controls records follow.
void testPresetChangeHandsOverKnobs(deluge::edits::Context& ctx) {
	namespace params = deluge::modulation::params;
	ParamManagerForTimeline oldParams;
	Instrument* oldSynth = StorageManager::createNewInstrument(OutputType::SYNTH, &oldParams);
	// Their params go to the song's backups, as an instrument with no clip keeps them.
	ctx.song.live.param_backup.backUp(static_cast<ModControllableAudio*>(oldSynth->toModControllable()), nullptr,
	                                  &oldParams, true);
	ctx.song.addOutput(oldSynth);
	Sound& oldSound = *static_cast<SoundInstrument*>(oldSynth);
	const auto oldAddress = deluge::bridge::controlsAddressOf(oldSound);
	if (!oldAddress) {
		check(false, "handover: the old synth has controls");
		return;
	}
	std::vector<deluge::project::ControllerMapping> controls = deluge::bridge::controlsOf(oldSound);
	deluge::project::MidiCommand cc;
	cc.channel_or_zone = 0;
	cc.note_or_cc = 71;
	ParamDescriptor resonance;
	resonance.setToHaveParamOnly(params::LOCAL_LPF_RESONANCE);
	controls.push_back({deluge::project::MidiInputControl{cc, false},
	                    deluge::bridge::knobTargetOf(resonance, params::Kind::UNPATCHED_SOUND)});
	deluge::edits::SetControls learn{*oldAddress, controls};
	check(deluge::edits::apply(ctx, learn) == Error::NONE, "handover: the old synth learns a knob");

	ParamManagerForTimeline newParams;
	Instrument* newSynth = StorageManager::createNewInstrument(OutputType::SYNTH, &newParams);
	ctx.song.live.param_backup.backUp(static_cast<ModControllableAudio*>(newSynth->toModControllable()), nullptr,
	                                  &newParams, true);
	oldSynth->editedByUser = true; // so it hibernates, keeping its record
	ctx.song.replaceInstrument(oldSynth, newSynth);
	check(ctx.song.live.media.first_hibernating_instrument == oldSynth, "handover: the old synth hibernates");
	check(static_cast<SoundInstrument*>(newSynth)->midi_knobs.size() == 1, "handover: the new synth has the knob");
	check(deluge::harness::soundIncoherence(*static_cast<SoundInstrument*>(newSynth), ctx.song).empty(),
	      "handover: the new synth's record holds the knob");
	if (oldSynth->docOutput_ != nullptr) {
		check(deluge::harness::soundIncoherence(oldSound, ctx.song).empty(),
		      "handover: the old synth's record no longer holds it");
	}
}

// An audio track cloned from another takes its learned knobs; recording the clone's controls makes its record hold
// them.
void testClonedAudioTrackRecordsKnobs(deluge::edits::Context& ctx) {
	namespace params = deluge::modulation::params;
	AudioOutput* source = ctx.song.createNewAudioOutput();
	AudioOutput* clone = ctx.song.createNewAudioOutput();
	if (source == nullptr || clone == nullptr) {
		check(false, "clone: two audio tracks");
		return;
	}
	const auto address = deluge::bridge::controlsAddressOf(*source, ctx.song);
	check(address.has_value(), "clone: the source track has controls");
	if (!address) {
		return;
	}
	deluge::project::MidiCommand cc;
	cc.channel_or_zone = 0;
	cc.note_or_cc = 72;
	ParamDescriptor volume;
	volume.setToHaveParamOnly(params::UNPATCHED_START + params::UNPATCHED_VOLUME);
	deluge::edits::SetControls learn{*address,
	                                 {{deluge::project::MidiInputControl{cc, false},
	                                   deluge::bridge::knobTargetOf(volume, params::Kind::UNPATCHED_GLOBAL)}}};
	check(deluge::edits::apply(ctx, learn) == Error::NONE, "clone: the source track learns a knob");
	clone->cloneFrom(source);
	check(deluge::harness::expectSongCoherent(ctx.song, "clone-before") != 0,
	      "clone: a cloned track's record lags until recorded");
	deluge::bridge::recordOutputControls(*clone, ctx.song);
	check(deluge::harness::expectSongCoherent(ctx.song, "clone") == 0,
	      "clone: the cloned track's record holds the knob");
}

/// A host owner's effect setting changes by its Edit: the owner, its render copy and its chain's slot follow; a refused
/// value changes nothing.
void testSetEffectSetting(deluge::edits::Context& ctx) {
	using deluge::project::ChainAddress;
	using deluge::serialization::Value;
	ParamManagerForTimeline kitParams;
	Instrument* kit = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	ctx.song.addOutput(kit);
	struct Owner {
		ModControllableAudio* owner;
		ChainAddress address;
		const char* name;
	};
	const Owner owners[] = {
	    {static_cast<Kit*>(kit), {.kind = ChainAddress::Kind::OUTPUT, .id = kit->id().value_or(0)}, "a kit"},
	    {&ctx.song.live.global_effectable, {.kind = ChainAddress::Kind::SONG}, "the master"},
	};
	for (const Owner& o : owners) {
		const std::string name = o.name;
		const auto applied = [&](const char* path, Value value) {
			deluge::edits::SetEffectSetting edit{o.address, path, std::move(value)};
			return deluge::edits::apply(ctx, edit);
		};
		check(applied("lpfMode", Value::makeScalar("SVF_Notch")) == Error::NONE, (name + ": lpfMode applied").c_str());
		check(o.owner->effects().filter.lpf_mode == FilterMode::SVF_NOTCH, (name + ": the owner's filter").c_str());
		check(o.owner->modControllableSnapshot().effects == o.owner->effects(), (name + ": its render copy").c_str());
		const deluge::project::FxChain* chain = ctx.model.resolveChain(o.address);
		const deluge::project::FxSlot* filter = nullptr;
		if (chain != nullptr) {
			for (const deluge::project::FxSlot& slot : chain->slots) {
				if (slot.effect == "filter") {
					filter = &slot;
				}
			}
		}
		check(filter != nullptr && filter->state.find("lpfMode") != nullptr
		          && filter->state.find("lpfMode")->str() == "SVF_Notch",
		      (name + ": the filter slot holds it").c_str());
		check(deluge::harness::chainIncoherence(ctx.song).empty(), (name + ": the chain is coherent").c_str());

		const deluge::effects::EffectStates before = o.owner->effects();
		check(applied("lpfMode", Value::makeScalar("sideways")) != Error::NONE, (name + ": a refused value").c_str());
		check(o.owner->effects() == before, (name + ": a refused value changes nothing").c_str());

		const std::optional<deluge::project::FxChain> chainBefore =
		    chain != nullptr ? std::optional{*chain} : std::nullopt;
		const int32_t level = static_cast<int32_t>(o.owner->effects().delay.sync_level);
		check(applied("delay/syncLevel", Value::makeNumber(level)) == Error::NONE,
		      (name + ": the delay's sync level applied").c_str());
		check(chain != nullptr && chainBefore && *chain == *chainBefore,
		      (name + ": the same sync level leaves the chain as it was").c_str());

		check(applied("modFXType", Value::makeScalar("chorus")) == Error::NONE, (name + ": modFXType applied").c_str());
		check(o.owner->effects().mod_fx.type == ModFXType::CHORUS, (name + ": the owner renders chorus").c_str());
		check(deluge::harness::chainIncoherence(ctx.song).empty(), (name + ": coherent after mod FX").c_str());
	}
}

int edit_substrate_main(int /*argc*/, char** /*argv*/, Io driverIo) {
	io = driverIo;

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
	(void)song->live.clips.session_clips.insertClipAt(audioClip, 0);

	deluge::edits::Context ctx{*song, song->project};
	testDispatcher(ctx);
	testSetParamValuePilot(ctx);
	testSetParamValueRegionAndFlag(ctx);
	testDeleteParamAutomation(ctx);
	testPasteParamAutomation(ctx);
	testInlinePokeCharacterization(ctx, audioClip);
	testParamSnapshotEquivalence(ctx, song, audioClip);
	testScalarFlipViaApply(ctx, song, audioClip);
	testRegionFlipViaApply(ctx, song, audioClip);
	testSetEngineSetting(ctx);
	testSetSourceRanges(ctx);
	testDrumIdsAreUnique(ctx);
	testRemovalDropsOnlyOwnRecords(ctx);
	testSetDxPatch(ctx);
	testKnobTargets(ctx);
	testSetControls(ctx);
	testKnobGesturesRecord(ctx);
	testPresetChangeHandsOverKnobs(ctx);
	testClonedAudioTrackRecordsKnobs(ctx);
	testSetEffectSetting(ctx);

	if (failures == 0) {
		std::fprintf(io.out, "edit_substrate: OK\n");
	}
	return failures ? 1 : 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(edit_substrate, edit_substrate_main)
