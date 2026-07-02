// Projection round-trip test for the param/automation DOM leaf, DocAutoParam
// (docs/dev/param_automation_dom.md, Phase 1).
//
// Runs in the sim environment (real AutoParam + the host allocator), so it exercises the projection
// against the actual firmware engine container — and runs on x86 (build-sim) and ARM (build-sim-arm via
// qemu), the dual-arch coverage the design doc calls for. (A CppSpec unit spec can't host this: AutoParam
// pulls in the whole app, and the firmware containers don't build under the bare spec toolchain.)
//
// Exit: 0 = all round-trips faithful, 1 = a mismatch.

#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h"
#include "model/global_effectable/global_effectable.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound_drum.h"
#include "storage/storage_manager.h"

namespace params = deluge::modulation::params;

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>

using deluge::project::DocAutoParam;
using deluge::project::fromDocAutoParam;
using deluge::project::toDocAutoParam;

namespace {

struct Node {
	int32_t pos;
	int32_t value;
	bool interpolated;
};

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

void setNode(AutoParam& ap, int32_t pos, int32_t value, bool interpolated) {
	int32_t i = ap.nodes().insertSorted(pos, /*isDefinitelyLast=*/true).value_or(-1);
	if (i >= 0) {
		ap.nodes()[i].pos = pos;
		ap.nodes()[i].value = value;
		ap.nodes()[i].interpolated = interpolated;
	}
}

bool cableMatches(const deluge::project::DocPatchCable& a, const deluge::project::DocPatchCable& b) {
	return a.from == b.from && a.destination.data == b.destination.data && a.polarity == b.polarity
	       && a.amount.current_value == b.amount.current_value && a.amount.nodes.size() == b.amount.nodes.size();
}

void expectSameNodes(const AutoParam& a, const AutoParam& b, const char* name) {
	check(a.currentValue == b.currentValue, name);
	check(a.nodes().size() == b.nodes().size(), name);
	for (size_t i = 0; i < a.nodes().size() && i < b.nodes().size(); i++) {
		check(a.nodes()[i].pos == b.nodes()[i].pos, name);
		check(a.nodes()[i].value == b.nodes()[i].value, name);
		check(a.nodes()[i].interpolated == b.nodes()[i].interpolated, name);
	}
}

// Build a live AutoParam from a value + node list, round-trip it through the document form, and assert
// the rebuilt AutoParam is identical.
void roundTrip(const char* name, int32_t currentValue, const std::vector<Node>& nodes) {
	AutoParam ap;
	ap.currentValue = currentValue;
	for (const Node& n : nodes) {
		int32_t i = ap.nodes().insertSorted(n.pos, /*isDefinitelyLast=*/true).value_or(-1);
		check(i >= 0, "insertSorted (test setup)");
		ap.nodes()[i].pos = n.pos;
		ap.nodes()[i].value = n.value;
		ap.nodes()[i].interpolated = n.interpolated;
	}

	DocAutoParam doc = toDocAutoParam(ap);
	check(doc.current_value == currentValue, name);
	check(doc.nodes.size() == nodes.size(), name);

	AutoParam rebuilt;
	fromDocAutoParam(doc, rebuilt);
	check(rebuilt.currentValue == currentValue, name);
	check(rebuilt.nodes().size() == nodes.size(), name);
	for (size_t i = 0; i < nodes.size() && i < rebuilt.nodes().size(); i++) {
		check(rebuilt.nodes()[i].pos == nodes[i].pos, name);
		check(rebuilt.nodes()[i].value == nodes[i].value, name);
		check(rebuilt.nodes()[i].interpolated == nodes[i].interpolated, name);
	}
}

} // namespace

int main() {
	// Phase 1 (DOM-live split): setNodeAtPos operates directly on DocAutoParam (pure data, no engine).
	{
		using deluge::project::DocAutoParam;
		DocAutoParam p{}; // current_value 0, empty nodes
		int32_t i0 = deluge::project::setNodeAtPos(p, 48, 100, true);
		check(p.nodes.size() == 1 && i0 == 0, "setNodeAtPos: first insert");
		check(p.nodes[0].pos == 48 && p.nodes[0].value == 100 && p.nodes[0].interpolated, "setNodeAtPos: fields");
		int32_t i1 = deluge::project::setNodeAtPos(p, 0, -50, false); // inserts BEFORE (sorted)
		check(p.nodes.size() == 2 && i1 == 0 && p.nodes[0].pos == 0, "setNodeAtPos: sorted insert at front");
		int32_t i2 = deluge::project::setNodeAtPos(p, 48, 7, false); // modifies existing node at 48
		check(p.nodes.size() == 2 && i2 == 1 && p.nodes[1].pos == 48 && p.nodes[1].value == 7
		          && !p.nodes[1].interpolated,
		      "setNodeAtPos: modify existing");
	}

	// Phase 1 (DOM-live split): getValueAtPos interpolates through DocAutoParam nodes (pure data, no engine).
	{
		using deluge::project::DocAutoParam;
		DocAutoParam p{};
		deluge::project::setNodeAtPos(p, 0, 100, true);
		deluge::project::setNodeAtPos(p, 48, -200, false); // stepped node
		check(deluge::project::getValueAtPos(p, 0, 96) == 100, "getValueAtPos: at node 0");
		check(deluge::project::getValueAtPos(p, 48, 96) == -200, "getValueAtPos: at node 48");
		// Node at 48 is non-interpolated ⇒ the span [0,48) holds the left value (100), no ramp.
		check(deluge::project::getValueAtPos(p, 24, 96) == 100, "getValueAtPos: stepped span holds left value");
		DocAutoParam empty{};
		empty.current_value = 777;
		check(deluge::project::getValueAtPos(empty, 10, 96) == 777, "getValueAtPos: empty -> current_value");

		// Interpolated right node ⇒ linear ramp between the two nodes.
		DocAutoParam ramp{};
		deluge::project::setNodeAtPos(ramp, 0, 0, true);
		deluge::project::setNodeAtPos(ramp, 48, 4800, true);
		check(deluge::project::getValueAtPos(ramp, 24, 96) == 2400, "getValueAtPos: linear ramp midpoint");
	}

	// Phase 1 fidelity guard: the DOM getValueAtPos must be bit-identical to the engine AutoParam::getValueAtPos
	// across the full position range and both directions, including the loop-wrap branches (which the engine
	// reaches via ModelStack::getLoopLength). We build a live AutoParam with the same nodes and diff every
	// position; getLoopLength is satisfied by a NoteRow with an independent loop length (no Song needed).
	{
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		auto engineValueAt = [](AutoParam& ap, uint32_t pos, int32_t loopLength, bool reversed) -> int32_t {
			// Only AutoParam::getValueAtPos's getLoopLength() call touches the ModelStack; a NoteRow with a
			// non-zero loopLengthIfIndependent short-circuits it, so no Song/TimelineCounter is required.
			alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
			auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
			ms->song = nullptr;
			ms->setTimelineCounter(nullptr);
			NoteRow nr;
			nr.loopLengthIfIndependent = loopLength;
			ms->setNoteRow(&nr);
			return ap.getValueAtPos(pos, ms, reversed);
		};

		const std::vector<std::vector<N>> configs = {
		    {{0, 100, true}, {48, -200, true}, {96, 300, false}}, // interpolated spans + a stepped node
		    {{10, -1000, true}, {80, 1000, true}},                // wrap before first / after last
		    {{50, 777, true}},                                    // single node
		    {{0, -1073741824, true}, {64, 1073741823, false}},    // patch-cable value range
		};
		// Realistic timeline inputs only: every node pos and every query pos is strictly within the loop
		// ([0, loopLength)). Feeding a node at pos == loopLength makes the wrap span collapse to zero ticks,
		// which divides by zero in BOTH the engine and this port (identically) — an out-of-range input the
		// engine never sees at runtime, so probing it would test nothing about the port's fidelity.
		const int32_t loopLengths[] = {128, 2147483647}; // finite (> every node pos) + infinite
		const uint32_t positions[] = {0, 1, 5, 10, 24, 47, 48, 49, 50, 63, 64, 80, 95, 96, 120, 127};

		for (const auto& cfg : configs) {
			AutoParam ap;
			DocAutoParam doc{};
			for (const N& n : cfg) {
				setNode(ap, n.pos, n.value, n.interpolated);
				deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
			}
			for (int32_t loopLength : loopLengths) {
				for (uint32_t pos : positions) {
					for (bool reversed : {false, true}) {
						int32_t want = engineValueAt(ap, pos, loopLength, reversed);
						int32_t got = deluge::project::getValueAtPos(doc, pos, loopLength, reversed);
						check(want == got, "getValueAtPos: matches engine oracle");
					}
				}
			}
		}
	}

	// Phase 1: getDistanceToNextNode (pure query) — functional + engine-oracle diff over the full position
	// range and both directions (the engine reads the loop length from the ModelStack as effectiveLength).
	{
		using deluge::project::DocAutoParam;
		DocAutoParam p{};
		deluge::project::setNodeAtPos(p, 10, 0, true);
		deluge::project::setNodeAtPos(p, 80, 0, true);
		check(deluge::project::getDistanceToNextNode(p, 0, 96, false) == 10, "getDistanceToNextNode: fwd to first");
		check(deluge::project::getDistanceToNextNode(p, 10, 96, false) == 70, "getDistanceToNextNode: fwd from a node");
		check(deluge::project::getDistanceToNextNode(p, 80, 96, false) == 26,
		      "getDistanceToNextNode: fwd wraps to first");
		check(deluge::project::getDistanceToNextNode(p, 0, 96, true) == 16, "getDistanceToNextNode: rev wraps to last");
		DocAutoParam none{};
		check(deluge::project::getDistanceToNextNode(none, 5, 96, false) == 96, "getDistanceToNextNode: empty -> loop");

		auto engineDistance = [](AutoParam& ap, int32_t pos, int32_t loopLength, bool reversed) -> int32_t {
			alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
			auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
			ms->song = nullptr;
			ms->setTimelineCounter(nullptr);
			NoteRow nr;
			nr.loopLengthIfIndependent = loopLength;
			ms->setNoteRow(&nr);
			return ap.getDistanceToNextNode(ms, pos, reversed);
		};

		struct N {
			int32_t pos;
			bool interpolated;
		};
		const std::vector<std::vector<N>> configs = {
		    {{0, true}, {48, true}, {96, false}},
		    {{10, true}, {80, true}},
		    {{50, true}},
		};
		const int32_t loopLengths[] = {128, 2147483647};
		const int32_t positions[] = {0, 1, 5, 10, 24, 47, 48, 49, 50, 63, 64, 80, 95, 96, 120, 127};
		for (const auto& cfg : configs) {
			AutoParam ap;
			DocAutoParam doc{};
			for (const N& n : cfg) {
				setNode(ap, n.pos, 0, n.interpolated);
				deluge::project::setNodeAtPos(doc, n.pos, 0, n.interpolated);
			}
			for (int32_t loopLength : loopLengths) {
				for (int32_t pos : positions) {
					for (bool reversed : {false, true}) {
						int32_t want = engineDistance(ap, pos, loopLength, reversed);
						int32_t got = deluge::project::getDistanceToNextNode(doc, pos, loopLength, reversed);
						check(want == got, "getDistanceToNextNode: matches engine oracle");
					}
				}
			}
		}
	}

	// Unautomated: current value only, no nodes.
	roundTrip("unautomated", 123456, {});

	// Automation with interpolated + non-interpolated nodes.
	roundTrip("automated", 777, {{0, 100, false}, {48, -250000000, true}, {96, 250000000, false}});

	// Full patch-cable value range (nodes stored in [-2^30, 2^30-1]).
	roundTrip("patch-cable range", 0, {{0, -1073741824, true}, {64, 1073741823, false}});

	// Extreme current value (full int32 range), no automation.
	roundTrip("int32 extremes", 2147483647, {});
	roundTrip("int32 min", -2147483647 - 1, {});

	// Phase 2: a parameter collection (DocParamSet) round-trips its kind + every param (a mix of
	// unautomated, automated, and patch-cable-range), preserving array order and count.
	{
		constexpr int kNumParams = 3;
		AutoParam set[kNumParams];
		set[0].currentValue = 111; // unautomated
		set[1].currentValue = 222; // automated
		setNode(set[1], 0, 100, false);
		setNode(set[1], 48, -250000000, true);
		set[2].currentValue = 333; // patch-cable range
		setNode(set[2], 0, -1073741824, true);

		using deluge::modulation::params::Kind;
		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(Kind::PATCHED, set, kNumParams);
		check(doc.kind == Kind::PATCHED, "set kind");
		check(static_cast<int>(doc.params.size()) == kNumParams, "set size");

		AutoParam out[kNumParams];
		deluge::project::fromDocParamSet(doc, out, kNumParams);
		for (int p = 0; p < kNumParams; p++) {
			expectSameNodes(out[p], set[p], "set param");
		}
	}

	// Phase 3: a live ParamManager round-trips its present collections (patched + unpatched) through
	// DocParamManager and projects faithfully into another already-set-up manager.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "pm setupWithPatching");
		pm.getPatchedParamSet()->params[5].currentValue = 12345;
		setNode(pm.getPatchedParamSet()->params[5], 0, 100, true);
		setNode(pm.getPatchedParamSet()->params[5], 32, -500, false);
		pm.getUnpatchedParamSet()->params[2].currentValue = 678;

		// A patch cable (Phase 3b): source -> destination param, with an automatable amount.
		PatchCableSet* pcs = pm.getPatchCableSet();
		pcs->numPatchCables = 1;
		pcs->patchCables[0].from = PatchSource::ENVELOPE_0;
		pcs->patchCables[0].destinationParamDescriptor.setToHaveParamOnly(7);
		pcs->patchCables[0].polarity = Polarity::BIPOLAR;
		pcs->patchCables[0].param.currentValue = 536870912; // amount, patch-cable range
		setNode(pcs->patchCables[0].param, 0, -536870912, true);

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);
		check(doc.patched.has_value(), "pm patched present");
		check(doc.unpatched.has_value(), "pm unpatched present");
		check(doc.patch_cables.size() == 1, "pm patch cable present");

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "pm2 setupWithPatching");
		deluge::project::fromDocParamManager(doc, pm2);
		check(pm2.getPatchedParamSet()->params[5].currentValue == 12345, "pm patched currentValue");
		expectSameNodes(pm2.getPatchedParamSet()->params[5], pm.getPatchedParamSet()->params[5], "pm patched nodes");
		check(pm2.getUnpatchedParamSet()->params[2].currentValue == 678, "pm unpatched currentValue");

		PatchCableSet* pcs2 = pm2.getPatchCableSet();
		check(pcs2->numPatchCables == 1, "pm2 patch cable count");
		check(pcs2->patchCables[0].from == PatchSource::ENVELOPE_0, "pm2 cable source");
		check(pcs2->patchCables[0].destinationParamDescriptor.data
		          == pcs->patchCables[0].destinationParamDescriptor.data,
		      "pm2 cable destination");
		check(pcs2->patchCables[0].polarity == Polarity::BIPOLAR, "pm2 cable polarity");
		expectSameNodes(pcs2->patchCables[0].param, pcs->patchCables[0].param, "pm2 cable amount");
	}

	// Phase 5c-1: the DOM can *drive* the live manager — projecting a captured DocParamManager into a
	// manager that holds DIFFERENT/stale state fully restores it, including the summary automation flags
	// (set for newly-automated params, cleared for params that were automated only in the target).
	{
		ParamManagerForTimeline src;
		check(src.setupWithPatching() == Error::NONE, "5c1 src setup");
		src.getPatchedParamSet()->params[5].currentValue = 999;
		setNode(src.getPatchedParamSet()->params[5], 0, 100, true); // automated in src
		setNode(src.getPatchedParamSet()->params[5], 48, -200, false);
		src.getUnpatchedParamSet()->params[3].currentValue = 4242; // not automated in src

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(src);

		// Target starts with conflicting state: param 5 NOT automated, but param 9 IS automated (must clear).
		ParamManagerForTimeline dst;
		check(dst.setupWithPatching() == Error::NONE, "5c1 dst setup");
		setNode(dst.getPatchedParamSet()->params[9], 0, 7, true);
		dst.getPatchedParamSet()->paramHasAutomationNow(dst.getPatchedParamSetSummary(), 9);
		dst.getPatchedParamSet()->params[5].currentValue = -1; // stale value

		deluge::project::fromDocParamManager(doc, dst);

		ParamCollectionSummary* psum = dst.getPatchedParamSetSummary();
		// param 5 restored + its automation flag set
		expectSameNodes(dst.getPatchedParamSet()->params[5], src.getPatchedParamSet()->params[5],
		                "5c1 param5 restored");
		check((psum->whichParamsAreAutomated[5 >> 5] & (1u << (5 & 31))) != 0, "5c1 param5 flag set");
		// param 9's stale automation cleared (nodes + flag)
		check(dst.getPatchedParamSet()->params[9].nodes().empty(), "5c1 param9 nodes cleared");
		check((psum->whichParamsAreAutomated[9 >> 5] & (1u << (9 & 31))) == 0, "5c1 param9 flag cleared");
		// unpatched value restored
		check(dst.getUnpatchedParamSet()->params[3].currentValue == 4242, "5c1 unpatched restored");
	}

	// Phase 4 brick: the generic patch-cable serializer must be byte-identical to the engine's
	// PatchCableSet::writePatchCablesToFile, including the range-cable (depthControlledBy) nesting.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "pc-ser setup");
		PatchCableSet* pcs = pm.getPatchCableSet();
		pcs->numPatchCables = 3;
		// flat cable 0: ENVELOPE_0 -> param 7
		pcs->patchCables[0].from = PatchSource::ENVELOPE_0;
		pcs->patchCables[0].destinationParamDescriptor.setToHaveParamOnly(7);
		pcs->patchCables[0].polarity = Polarity::BIPOLAR;
		pcs->patchCables[0].param.currentValue = 536870912;
		setNode(pcs->patchCables[0].param, 0, -536870912, true);
		// flat cable 1: LFO_LOCAL_1 -> param 3
		pcs->patchCables[1].from = PatchSource::LFO_LOCAL_1;
		pcs->patchCables[1].destinationParamDescriptor.setToHaveParamOnly(3);
		pcs->patchCables[1].polarity = Polarity::UNIPOLAR;
		pcs->patchCables[1].param.currentValue = -100000;
		// range cable 2: LFO_LOCAL_2 controls the depth of cable 0 (param 7 + source ENVELOPE_0)
		pcs->patchCables[2].from = PatchSource::LFO_LOCAL_2;
		pcs->patchCables[2].destinationParamDescriptor.setToHaveParamAndSource(7, PatchSource::ENVELOPE_0);
		pcs->patchCables[2].polarity = Polarity::BIPOLAR;
		pcs->patchCables[2].param.currentValue = 12345;

		static char engineBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engineBuf, sizeof(engineBuf));
		smSerializer.reset();
		pcs->writePatchCablesToFile(smSerializer, true);
		int32_t engineLen = smSerializer.bytesWritten();

		deluge::project::DocParamManager docpm = deluge::project::toDocParamManager(pm);
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializePatchCables(docpm.patch_cables, smSerializer, true);
		int32_t domLen = smSerializer.bytesWritten();

		check(engineLen == domLen, "patch-cable serialize length matches engine");
		check(engineLen == domLen && std::memcmp(engineBuf, domBuf, engineLen) == 0,
		      "patch-cable serialize byte-identical to engine");

		// And the DOM serialize -> deserialize round-trip recovers the same cables (as a set; the
		// engine/DOM read orders range cables before the cable they control).
		const std::vector<deluge::project::DocPatchCable>& original = docpm.patch_cables;
		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("song");
		smSerializer.writeOpeningTagEnd();
		deluge::project::serializePatchCables(original, smSerializer, true);
		smSerializer.writeClosingTag("song");
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		std::vector<deluge::project::DocPatchCable> recovered;
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "song");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tag, "patchCables")) {
				deluge::project::deserializePatchCables(recovered, reader, 2147483647);
			}
			reader.exitTag();
		}

		check(recovered.size() == original.size(), "patch-cable round-trip count");
		for (const auto& oc : original) {
			bool found = false;
			for (const auto& rc : recovered) {
				if (cableMatches(oc, rc)) {
					found = true;
					break;
				}
			}
			check(found, "patch-cable round-trip recovers cable");
		}
	}

	// Phase 4: writeDocParamAsAttribute (the owner-block building block) is byte-identical to
	// ParamSet::writeParamAsAttribute — automated param, plain param, and the onlyIfContainsSomething
	// skip (empty vs non-empty).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "attr setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[2].currentValue = 424242; // automated
		setNode(unp->params[2], 0, 10, true);
		setNode(unp->params[2], 40, -20, false);
		unp->params[5].currentValue = -7; // plain, non-empty
		unp->params[8].currentValue = 0;  // empty

		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(*unp);

		struct Case {
			const char* name;
			int p;
			bool onlyIf;
		};
		const Case cases[] = {{"alpha", 2, false}, {"beta", 5, false}, {"gamma", 8, true}, {"delta", 5, true}};
		for (const Case& cs : cases) {
			static char attrEngine[8192];
			static char attrDom[8192];
			smSerializer.setMemoryBuffer(attrEngine, sizeof(attrEngine));
			smSerializer.reset();
			unp->writeParamAsAttribute(smSerializer, cs.name, cs.p, true, cs.onlyIf);
			int32_t el = smSerializer.bytesWritten();
			smSerializer.setMemoryBuffer(attrDom, sizeof(attrDom));
			smSerializer.reset();
			deluge::project::writeDocParamAsAttribute(doc, cs.name, cs.p, smSerializer, true, cs.onlyIf);
			int32_t dl = smSerializer.bytesWritten();
			check(el == dl && std::memcmp(attrEngine, attrDom, el) == 0, "writeParamAsAttribute byte-identical");
		}
	}

	// Phase 4a: the ModControllableAudio shared param block (flat attrs + <equalizer>) serializes
	// byte-identical to the engine's static writeParamAttributesToFile / writeParamTagsToFile.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "mca setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[params::UNPATCHED_STUTTER_RATE].currentValue = 111;
		unp->params[params::UNPATCHED_BITCRUSHING].currentValue = 222;
		setNode(unp->params[params::UNPATCHED_ARP_GATE], 0, 50, true);
		unp->params[params::UNPATCHED_BASS].currentValue = -333; // equalizer group

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		ModControllableAudio::writeParamAttributesToFile(smSerializer, &pm, true);
		int32_t engAttrLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeModControllableParamAttributes(*doc.unpatched, smSerializer, true);
		int32_t domAttrLen = smSerializer.bytesWritten();
		check(engAttrLen == domAttrLen && std::memcmp(engBuf, domBuf, engAttrLen) == 0,
		      "ModControllableAudio attributes byte-identical");

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		ModControllableAudio::writeParamTagsToFile(smSerializer, &pm, true);
		int32_t engTagLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeModControllableParamTags(*doc.unpatched, smSerializer, true);
		int32_t domTagLen = smSerializer.bytesWritten();
		check(engTagLen == domTagLen && std::memcmp(engBuf, domBuf, engTagLen) == 0,
		      "ModControllableAudio tags byte-identical");
	}

	// Phase 5b-1: the ModControllableAudio block round-trips through the DOM reader — serialize via the DOM
	// into a wrapped <params> element, DOM-read it back into a fresh manager, and confirm values + the
	// automation summary flag match.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "mca-rt setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[params::UNPATCHED_STUTTER_RATE].currentValue = 111;
		unp->params[params::UNPATCHED_BITCRUSHING].currentValue = 222;
		setNode(unp->params[params::UNPATCHED_ARP_GATE], 0, 50, true); // automated
		setNode(unp->params[params::UNPATCHED_ARP_GATE], 48, -99, false);
		unp->params[params::UNPATCHED_BASS].currentValue = -333; // equalizer group
		unp->params[params::UNPATCHED_ARP_SPREAD_OCTAVE].currentValue = 444;

		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(*unp);

		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("params");
		deluge::project::serializeModControllableParamAttributes(doc, smSerializer, true);
		smSerializer.writeOpeningTagEnd();
		deluge::project::serializeModControllableParamTags(doc, smSerializer, true);
		smSerializer.writeClosingTag("params");
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "mca-rt setup2");
		UnpatchedParamSet* unp2 = pm2.getUnpatchedParamSet();
		ParamCollectionSummary* sum2 = pm2.getUnpatchedParamSetSummary();
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "params");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!deluge::project::readModControllableParamTag(reader, tag, *unp2, *sum2, 2147483647)) {
				reader.exitTag();
			}
		}

		const int32_t ids[] = {params::UNPATCHED_STUTTER_RATE, params::UNPATCHED_BITCRUSHING,
		                       params::UNPATCHED_ARP_GATE, params::UNPATCHED_BASS, params::UNPATCHED_ARP_SPREAD_OCTAVE};
		for (int32_t id : ids) {
			expectSameNodes(unp2->params[id], unp->params[id], "mca-rt param");
		}
		check(
		    (sum2->whichParamsAreAutomated[params::UNPATCHED_ARP_GATE >> 5] & (1u << (params::UNPATCHED_ARP_GATE & 31)))
		        != 0,
		    "mca-rt automation flag set");
	}

	// Phase 4b: the GlobalEffectable param block (global flat attrs wrapping the shared ModControllableAudio
	// attrs + the <delay>/<lpf>/<hpf> groups) serializes byte-identical to the engine's static
	// GlobalEffectable::writeParamAttributesToFile / writeParamTagsToFile, including the conditional
	// pitchAdjust / sidechainCompressorVolume attrs.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "ge setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[params::UNPATCHED_REVERB_SEND_AMOUNT].currentValue = 123;
		unp->params[params::UNPATCHED_VOLUME].currentValue = 456;
		unp->params[params::UNPATCHED_PITCH_ADJUST].currentValue = 789;         // exercises the conditional attr
		setNode(unp->params[params::UNPATCHED_SIDECHAIN_VOLUME], 0, 99, false); // automated ⇒ conditional fires
		unp->params[params::UNPATCHED_LPF_FREQ].currentValue = -111;            // <lpf> group
		unp->params[params::UNPATCHED_DELAY_RATE].currentValue = 222;           // <delay> group
		unp->params[params::UNPATCHED_STUTTER_RATE].currentValue = 333;         // shared block
		unp->params[params::UNPATCHED_LPF_MORPH].currentValue = 444;            // community attr

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		GlobalEffectable::writeParamAttributesToFile(smSerializer, &pm, true);
		int32_t engAttrLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeGlobalEffectableParamAttributes(*doc.unpatched, smSerializer, true);
		int32_t domAttrLen = smSerializer.bytesWritten();
		check(engAttrLen == domAttrLen && std::memcmp(engBuf, domBuf, engAttrLen) == 0,
		      "GlobalEffectable attributes byte-identical");

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		GlobalEffectable::writeParamTagsToFile(smSerializer, &pm, true);
		int32_t engTagLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeGlobalEffectableParamTags(*doc.unpatched, smSerializer, true);
		int32_t domTagLen = smSerializer.bytesWritten();
		check(engTagLen == domTagLen && std::memcmp(engBuf, domBuf, engTagLen) == 0,
		      "GlobalEffectable tags byte-identical");
	}

	// Phase 5b-2: the GlobalEffectable param block round-trips through the DOM reader (flat attrs +
	// delay/lpf/hpf groups + shared MCA), and the read path still accepts the legacy nested <lpf morph>.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "ge-rt setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[params::UNPATCHED_REVERB_SEND_AMOUNT].currentValue = 123;
		unp->params[params::UNPATCHED_PITCH_ADJUST].currentValue = 789;  // conditional attr
		setNode(unp->params[params::UNPATCHED_LPF_FREQ], 0, -111, true); // <lpf> group, automated
		unp->params[params::UNPATCHED_DELAY_RATE].currentValue = 222;    // <delay> group
		unp->params[params::UNPATCHED_HPF_RES].currentValue = -55;       // <hpf> group
		unp->params[params::UNPATCHED_LPF_MORPH].currentValue = 444;     // flat community attr
		unp->params[params::UNPATCHED_STUTTER_RATE].currentValue = 333;  // shared MCA attr
		unp->params[params::UNPATCHED_BASS].currentValue = -333;         // equalizer group

		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(*unp);

		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("params");
		deluge::project::serializeGlobalEffectableParamAttributes(doc, smSerializer, true);
		smSerializer.writeOpeningTagEnd();
		deluge::project::serializeGlobalEffectableParamTags(doc, smSerializer, true);
		smSerializer.writeClosingTag("params");
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "ge-rt setup2");
		UnpatchedParamSet* unp2 = pm2.getUnpatchedParamSet();
		ParamCollectionSummary* sum2 = pm2.getUnpatchedParamSetSummary();
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "params");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!deluge::project::readGlobalEffectableParamTag(reader, tag, *unp2, *sum2, 2147483647)) {
				reader.exitTag();
			}
		}

		const int32_t ids[] = {
		    params::UNPATCHED_REVERB_SEND_AMOUNT, params::UNPATCHED_PITCH_ADJUST, params::UNPATCHED_LPF_FREQ,
		    params::UNPATCHED_DELAY_RATE,         params::UNPATCHED_HPF_RES,      params::UNPATCHED_LPF_MORPH,
		    params::UNPATCHED_STUTTER_RATE,       params::UNPATCHED_BASS};
		for (int32_t id : ids) {
			expectSameNodes(unp2->params[id], unp->params[id], "ge-rt param");
		}

		// Legacy back-compat: a hand-crafted <lpf morph="..."> (the writer emits flat lpfMorph, but old
		// files nested it) must still load into UNPATCHED_LPF_MORPH.
		deluge::project::DocParamSet morphDoc = deluge::project::toDocParamSet(*unp);
		morphDoc.params[params::UNPATCHED_LPF_MORPH].current_value = 0x5A5A;
		static char legacyBuf[4096];
		smSerializer.setMemoryBuffer(legacyBuf, sizeof(legacyBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("params");
		smSerializer.writeOpeningTagEnd();
		const deluge::project::DocParamAttr lpfMorphMember[] = {{"morph", params::UNPATCHED_LPF_MORPH}};
		deluge::project::writeDocParamGroup(smSerializer, "lpf", lpfMorphMember, morphDoc, true);
		smSerializer.writeClosingTag("params");
		int32_t legacyLen = smSerializer.bytesWritten();
		legacyBuf[legacyLen] = '\0';

		ParamManagerForTimeline pm3;
		check(pm3.setupWithPatching() == Error::NONE, "ge-rt setup3");
		UnpatchedParamSet* unp3 = pm3.getUnpatchedParamSet();
		ParamCollectionSummary* sum3 = pm3.getUnpatchedParamSetSummary();
		XMLDeserializer legacyReader(reinterpret_cast<uint8_t*>(legacyBuf), legacyLen);
		legacyReader.openXMLFile(nullptr, "params");
		while (*(tag = legacyReader.readNextTagOrAttributeName())) {
			if (!deluge::project::readGlobalEffectableParamTag(legacyReader, tag, *unp3, *sum3, 2147483647)) {
				legacyReader.exitTag();
			}
		}
		check(unp3->params[params::UNPATCHED_LPF_MORPH].currentValue == 0x5A5A, "ge-rt legacy <lpf morph> loads");
	}

	// Phase 4c: the ExpressionParamSet (MPE) block serializes byte-identical to the engine's
	// ExpressionParamSet::writeToFile (the <expressionData> group with pitchBend/yExpression/pressure).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "expr setup");
		ExpressionParamSet* e = pm.getOrCreateExpressionParamSet();
		check(e != nullptr, "expr param set created");
		e->params[0].currentValue = 12345;    // pitchBend
		setNode(e->params[2], 0, 6789, true); // pressure, automated
		// yExpression (index 1) left neutral ⇒ exercises the per-dimension skip.

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);
		check(doc.expression.has_value(), "expr present in doc");

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		bool engWrote = e->writeToFile(smSerializer, false);
		int32_t engLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		bool domWrote = deluge::project::serializeExpressionParams(*doc.expression, smSerializer, false, true);
		int32_t domLen = smSerializer.bytesWritten();
		check(engWrote == domWrote, "expression written-flag matches");
		check(engLen == domLen && std::memcmp(engBuf, domBuf, engLen) == 0, "ExpressionParamSet byte-identical");
	}

	// Phase 5b-4: the ExpressionParamSet (MPE) block round-trips through the DOM reader, and the read path
	// still accepts the legacy `channelPressure` alias (pressure / dimension 2).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "expr-rt setup");
		ExpressionParamSet* e = pm.getOrCreateExpressionParamSet();
		check(e != nullptr, "expr-rt set created");
		e->params[0].currentValue = 12345;    // pitchBend
		setNode(e->params[2], 0, 6789, true); // pressure, automated

		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(*e);

		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("song");
		smSerializer.writeOpeningTagEnd();
		deluge::project::serializeExpressionParams(doc, smSerializer, false, true);
		smSerializer.writeClosingTag("song");
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "expr-rt setup2");
		ExpressionParamSet* e2 = pm2.getOrCreateExpressionParamSet();
		ParamCollectionSummary* esum2 = pm2.getExpressionParamSetSummary();
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "song");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tag, "expressionData")) {
				reader.match('{');
				const char* inner;
				while (*(inner = reader.readNextTagOrAttributeName())) {
					if (!deluge::project::readExpressionParamTag(reader, inner, *e2, *esum2, 2147483647)) {
						reader.exitTag();
					}
				}
				reader.exitTag("expressionData", true);
			}
			else {
				reader.exitTag();
			}
		}
		expectSameNodes(e2->params[0], e->params[0], "expr-rt pitchBend");
		expectSameNodes(e2->params[2], e->params[2], "expr-rt pressure");
		check((esum2->whichParamsAreAutomated[2 >> 5] & (1u << (2 & 31))) != 0, "expr-rt pressure automation flag");

		// Legacy back-compat: <channelPressure> loads into dimension 2 via the engine tail in readFromFile.
		ParamManagerForTimeline pm3;
		check(pm3.setupWithPatching() == Error::NONE, "expr-rt setup3");
		ExpressionParamSet* e3 = pm3.getOrCreateExpressionParamSet();
		ParamCollectionSummary* esum3 = pm3.getExpressionParamSetSummary();
		static char legacyBuf[2048];
		const char* legacyXml = "<expressionData\n channelPressure=\"0x00001A2B\" />\n";
		std::strcpy(legacyBuf, legacyXml);
		int legacyLen = static_cast<int>(std::strlen(legacyBuf));
		XMLDeserializer legacyReader(reinterpret_cast<uint8_t*>(legacyBuf), legacyLen);
		legacyReader.openXMLFile(nullptr, "expressionData");
		e3->readFromFile(legacyReader, esum3, 2147483647);
		check(e3->params[2].currentValue == 0x00001A2B, "expr-rt legacy channelPressure loads");
	}

	// Phase 4d: the Sound param block serializes byte-identical to the engine's Sound::writeParamsToFile
	// (patched + unpatched flat attrs, the shared ModControllableAudio attrs, the four <envelope*> groups,
	// the <patchCables> block, and the shared tags — including the mid-block writeOpeningTagEnd).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "sound setup");
		PatchedParamSet* pp = pm.getPatchedParamSet();
		UnpatchedParamSet* up = pm.getUnpatchedParamSet();
		pp->params[params::LOCAL_OSC_A_VOLUME].currentValue = 12345;
		pp->params[params::LOCAL_PITCH_ADJUST].currentValue = 678;      // conditional attr — written
		setNode(pp->params[params::LOCAL_ENV_0_ATTACK], 0, 4242, true); // envelope group, automated
		pp->params[params::GLOBAL_REVERB_AMOUNT].currentValue = -999;
		pp->params[params::LOCAL_LPF_MORPH].currentValue = 111; // community attr
		up->params[params::UNPATCHED_PORTAMENTO].currentValue = 222;
		up->params[params::UNPATCHED_STUTTER_RATE].currentValue = 333; // shared block

		// A patch cable so the <patchCables> block is exercised within the Sound layout.
		PatchCableSet* pcs = pm.getPatchCableSet();
		pcs->numPatchCables = 1;
		pcs->patchCables[0].from = PatchSource::LFO_GLOBAL_1;
		pcs->patchCables[0].destinationParamDescriptor.setToHaveParamOnly(params::LOCAL_LPF_FREQ);
		pcs->patchCables[0].polarity = Polarity::BIPOLAR;
		pcs->patchCables[0].param.currentValue = 268435456;

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);
		check(doc.patched.has_value() && doc.unpatched.has_value(), "sound collections present");

		SoundDrum drum;

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		drum.writeParamsToFile(smSerializer, &pm, true);
		int32_t engLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeSoundParams(*doc.patched, *doc.unpatched, doc.patch_cables, smSerializer, true);
		int32_t domLen = smSerializer.bytesWritten();
		check(engLen == domLen && std::memcmp(engBuf, domBuf, engLen) == 0, "Sound params byte-identical");
	}

	// Phase 5b-3: the Sound param block round-trips through the DOM reader (patched + unpatched flat attrs,
	// the <envelope*> groups, the shared MCA block). Patch cables are excluded here — they keep the engine
	// reader (pre-V3.2 back-compat) — so no cables are set.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "sound-rt setup");
		PatchedParamSet* pp = pm.getPatchedParamSet();
		UnpatchedParamSet* up = pm.getUnpatchedParamSet();
		pp->params[params::LOCAL_OSC_A_VOLUME].currentValue = 12345;
		setNode(pp->params[params::LOCAL_ENV_0_ATTACK], 0, 4242, true); // envelope group, automated
		pp->params[params::GLOBAL_REVERB_AMOUNT].currentValue = -999;
		pp->params[params::LOCAL_LPF_MORPH].currentValue = 111; // community attr
		pp->params[params::LOCAL_PITCH_ADJUST].currentValue = 678;
		up->params[params::UNPATCHED_PORTAMENTO].currentValue = 222;
		up->params[params::UNPATCHED_STUTTER_RATE].currentValue = 333; // shared MCA attr

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);

		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("soundParams");
		deluge::project::serializeSoundParams(*doc.patched, *doc.unpatched, {}, smSerializer, true);
		smSerializer.writeClosingTag("soundParams", true);
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "sound-rt setup2");
		PatchedParamSet* pp2 = pm2.getPatchedParamSet();
		UnpatchedParamSet* up2 = pm2.getUnpatchedParamSet();
		ParamCollectionSummary* psum2 = pm2.getPatchedParamSetSummary();
		ParamCollectionSummary* usum2 = pm2.getUnpatchedParamSetSummary();
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "soundParams");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!deluge::project::readSoundParamTag(reader, tag, *pp2, *psum2, *up2, *usum2, 2147483647)) {
				reader.exitTag();
			}
		}

		const int32_t patchedIds[] = {params::LOCAL_OSC_A_VOLUME, params::LOCAL_ENV_0_ATTACK,
		                              params::GLOBAL_REVERB_AMOUNT, params::LOCAL_LPF_MORPH,
		                              params::LOCAL_PITCH_ADJUST};
		for (int32_t id : patchedIds) {
			expectSameNodes(pp2->params[id], pp->params[id], "sound-rt patched");
		}
		expectSameNodes(up2->params[params::UNPATCHED_PORTAMENTO], up->params[params::UNPATCHED_PORTAMENTO],
		                "sound-rt portamento");
		expectSameNodes(up2->params[params::UNPATCHED_STUTTER_RATE], up->params[params::UNPATCHED_STUTTER_RATE],
		                "sound-rt mca");
		check((psum2->whichParamsAreAutomated[params::LOCAL_ENV_0_ATTACK >> 5]
		       & (1u << (params::LOCAL_ENV_0_ATTACK & 31)))
		          != 0,
		      "sound-rt envelope automation flag set");
	}

	// Phase 4e: the Song value-override path. Song serializes its global-effectable block with a
	// valuesForOverride array (the "params in automation mode" snapshot); for an automated param the
	// override value is written in place of currentValue. The DOM serializer must match byte-for-byte.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "override setup");
		UnpatchedParamSet* up = pm.getUnpatchedParamSet();
		setNode(up->params[params::UNPATCHED_VOLUME], 0, 4242, true); // automated ⇒ override applies
		up->params[params::UNPATCHED_PAN].currentValue = 555;         // not automated ⇒ override ignored
		setNode(up->params[params::UNPATCHED_BASS], 0, 7, false);     // automated, equalizer group
		// An automated ModControllableAudio arp attr: the engine writes it WITHOUT the override (only the
		// first six MCA attrs honour valuesForOverride), so the override value must be ignored here.
		setNode(up->params[params::UNPATCHED_ARP_GATE], 0, 13, true);
		setNode(up->params[params::UNPATCHED_STUTTER_RATE], 0, 21, true); // override-eligible MCA attr

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);
		check(doc.unpatched.has_value(), "override unpatched present");

		std::vector<int32_t> ovr(doc.unpatched->params.size(), 0);
		ovr[params::UNPATCHED_VOLUME] = -123456; // distinct from currentValue (0) so the path is observable
		ovr[params::UNPATCHED_PAN] = 999;        // would differ, but PAN isn't automated ⇒ must be ignored
		ovr[params::UNPATCHED_BASS] = -77;
		ovr[params::UNPATCHED_ARP_GATE] = -555;     // must be ignored (arp attr, no override)
		ovr[params::UNPATCHED_STUTTER_RATE] = -999; // must apply (override-eligible MCA attr)

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		GlobalEffectable::writeParamAttributesToFile(smSerializer, &pm, true, ovr.data());
		int32_t engAttrLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeGlobalEffectableParamAttributes(*doc.unpatched, smSerializer, true, ovr.data());
		int32_t domAttrLen = smSerializer.bytesWritten();
		check(engAttrLen == domAttrLen && std::memcmp(engBuf, domBuf, engAttrLen) == 0,
		      "GlobalEffectable override attributes byte-identical");

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		GlobalEffectable::writeParamTagsToFile(smSerializer, &pm, true, ovr.data());
		int32_t engTagLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeGlobalEffectableParamTags(*doc.unpatched, smSerializer, true, ovr.data());
		int32_t domTagLen = smSerializer.bytesWritten();
		check(engTagLen == domTagLen && std::memcmp(engBuf, domBuf, engTagLen) == 0,
		      "GlobalEffectable override tags byte-identical");
	}

	if (failures == 0) {
		std::fprintf(stdout, "param_automation projection: OK\n");
		std::fflush(stdout);
		// _exit (not return) to skip deluge_app's global static destructors, which assume a fully
		// set-up app (e.g. a non-null display) — the same reason the golden harness uses _exit.
		_exit(0);
	}
	std::fprintf(stderr, "param_automation projection: %d failure(s)\n", failures);
	std::fflush(stderr);
	_exit(1);
}
