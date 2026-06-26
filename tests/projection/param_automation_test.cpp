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
#include "model/global_effectable/global_effectable.h"
#include "model/mod_controllable/mod_controllable_audio.h"
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
	int32_t i = ap.nodes.insertSorted(pos, /*isDefinitelyLast=*/true).value_or(-1);
	if (i >= 0) {
		ap.nodes[i].pos = pos;
		ap.nodes[i].value = value;
		ap.nodes[i].interpolated = interpolated;
	}
}

bool cableMatches(const deluge::project::DocPatchCable& a, const deluge::project::DocPatchCable& b) {
	return a.from == b.from && a.destination.data == b.destination.data && a.polarity == b.polarity
	       && a.amount.current_value == b.amount.current_value && a.amount.nodes.size() == b.amount.nodes.size();
}

void expectSameNodes(const AutoParam& a, const AutoParam& b, const char* name) {
	check(a.currentValue == b.currentValue, name);
	check(a.nodes.size() == b.nodes.size(), name);
	for (size_t i = 0; i < a.nodes.size() && i < b.nodes.size(); i++) {
		check(a.nodes[i].pos == b.nodes[i].pos, name);
		check(a.nodes[i].value == b.nodes[i].value, name);
		check(a.nodes[i].interpolated == b.nodes[i].interpolated, name);
	}
}

// Build a live AutoParam from a value + node list, round-trip it through the document form, and assert
// the rebuilt AutoParam is identical.
void roundTrip(const char* name, int32_t currentValue, const std::vector<Node>& nodes) {
	AutoParam ap;
	ap.currentValue = currentValue;
	for (const Node& n : nodes) {
		int32_t i = ap.nodes.insertSorted(n.pos, /*isDefinitelyLast=*/true).value_or(-1);
		check(i >= 0, "insertSorted (test setup)");
		ap.nodes[i].pos = n.pos;
		ap.nodes[i].value = n.value;
		ap.nodes[i].interpolated = n.interpolated;
	}

	DocAutoParam doc = toDocAutoParam(ap);
	check(doc.current_value == currentValue, name);
	check(doc.nodes.size() == nodes.size(), name);

	AutoParam rebuilt;
	fromDocAutoParam(doc, rebuilt);
	check(rebuilt.currentValue == currentValue, name);
	check(rebuilt.nodes.size() == nodes.size(), name);
	for (size_t i = 0; i < nodes.size() && i < rebuilt.nodes.size(); i++) {
		check(rebuilt.nodes[i].pos == nodes[i].pos, name);
		check(rebuilt.nodes[i].value == nodes[i].value, name);
		check(rebuilt.nodes[i].interpolated == nodes[i].interpolated, name);
	}
}

} // namespace

int main() {
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
