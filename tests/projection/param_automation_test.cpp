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
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "storage/storage_manager.h"

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
	}

	if (failures == 0) {
		std::printf("param_automation projection: OK\n");
		std::fflush(stdout);
		// _exit (not return) to skip deluge_app's global static destructors, which assume a fully
		// set-up app (e.g. a non-null display) — the same reason the golden harness uses _exit.
		_exit(0);
	}
	std::fprintf(stderr, "param_automation projection: %d failure(s)\n", failures);
	std::fflush(stderr);
	_exit(1);
}
