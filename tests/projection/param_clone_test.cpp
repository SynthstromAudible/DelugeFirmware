// Clone/undo deep-independence harness for the sequencer clone-idiom retirement
// (docs/superpowers/plans/2026-07-02-sequencer-clone-idiom-retirement.md, Task A0).
//
// The golden harness is BLIND to cloning — it does load->save and never duplicates a clip / kit row or
// runs undo. This test is the safety net: it clones the real engine containers (AutoParam,
// ParamManager with patched + unpatched + patch-cable collections) against the host allocator, mutates
// the source, and asserts the clone is DEEP-INDEPENDENT (the source mutation does not leak into the
// clone). It runs on x86 (build-sim) and ARM (build-sim-arm via qemu).
//
// It is written to pass on the CURRENT byte-copy clone idiom, then guards the retirement to real copy
// semantics: any task in the retirement that breaks deep independence fails here.
//
// Exit: 0 = all clones deep-independent, 1 = a failure.

#include "definitions_cxx.hpp"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "storage/storage_manager.h"

#include <cstdint>
#include <cstdio>
#include <unistd.h>

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

// Insert-or-append a node (matches tests/projection/param_automation_test.cpp's helper).
void setNode(AutoParam& ap, int32_t pos, int32_t value, bool interpolated) {
	int32_t i = ap.nodes().insertSorted(pos, /*isDefinitelyLast=*/true).value_or(-1);
	if (i >= 0) {
		ap.nodes()[i].pos = pos;
		ap.nodes()[i].value = value;
		ap.nodes()[i].interpolated = interpolated;
	}
}

int main() {
	// (a) AutoParam clone: nodes are deep-copied and independent of the source.
	{
		AutoParam src;
		setNode(src, 0, 111, false);
		setNode(src, 48, -222, true);

		AutoParam dst;
		dst.cloneFrom(&src, /*copyAutomation=*/true);
		check(dst.nodes().size() == 2, "autoparam clone: node count copied");
		check(dst.nodes()[0].value == 111 && dst.nodes()[1].value == -222, "autoparam clone: node values copied");

		src.nodes()[0].value = 999; // mutate the source
		src.nodes()[1].value = 999;
		check(dst.nodes()[0].value == 111 && dst.nodes()[1].value == -222,
		      "autoparam clone: dst independent of src mutation");
	}

	// (b) ParamManager clone: exercises the polymorphic per-collection clone chain
	//     (ParamManager::cloneParamCollectionsFrom -> the memcpy@217 -> each collection's beenCloned).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "pm setupWithPatching");

		pm.getUnpatchedParamSet()->params[2].currentValue = 678;
		setNode(pm.getUnpatchedParamSet()->params[2], 16, 321, false);
		setNode(pm.getPatchedParamSet()->params[5], 0, 100, true);

		PatchCableSet* pcs = pm.getPatchCableSet();
		pcs->numPatchCables = 1;
		pcs->numUsablePatchCables = 1; // beenCloned() only fixes up cables < numUsablePatchCables; the rest it init()s
		pcs->patchCables[0].from = PatchSource::ENVELOPE_0;
		pcs->patchCables[0].destinationParamDescriptor.setToHaveParamOnly(7);
		pcs->patchCables[0].param.currentValue = 536870912;
		setNode(pcs->patchCables[0].param, 0, -536870912, true);

		ParamManagerForTimeline clone;
		check(clone.cloneParamCollectionsFrom(&pm, /*copyAutomation=*/true) == Error::NONE,
		      "pm clone: cloneParamCollectionsFrom");
		check(clone.getUnpatchedParamSet()->params[2].nodes().size() == 1, "pm clone: unpatched node copied");
		check(clone.getPatchedParamSet()->params[5].nodes().size() == 1, "pm clone: patched node copied");
		check(clone.getPatchCableSet()->patchCables[0].param.nodes().size() == 1, "pm clone: patch-cable node copied");

		// Mutate every collection in the source; the clone must not move.
		pm.getUnpatchedParamSet()->params[2].nodes()[0].value = 999;
		pm.getPatchedParamSet()->params[5].nodes()[0].value = 999;
		pcs->patchCables[0].param.nodes()[0].value = 999;

		check(clone.getUnpatchedParamSet()->params[2].nodes()[0].value == 321, "pm clone: unpatched node independent");
		check(clone.getPatchedParamSet()->params[5].nodes()[0].value == 100, "pm clone: patched node independent");
		check(clone.getPatchCableSet()->patchCables[0].param.nodes()[0].value == -536870912,
		      "pm clone: patch-cable node independent");
	}

	// (c) Undo swap round-trip (the undo path uses swap, not the byte-copy idiom — regression guard).
	{
		AutoParam a, b;
		setNode(a, 0, 10, false);
		setNode(b, 0, 20, false);
		setNode(b, 8, 30, false);

		a.nodes().swap(b.nodes());
		check(a.nodes().size() == 2 && a.nodes()[1].value == 30, "swap: a took b's nodes");
		check(b.nodes().size() == 1 && b.nodes()[0].value == 10, "swap: b took a's nodes");

		a.nodes().swap(b.nodes()); // swap back
		check(a.nodes().size() == 1 && a.nodes()[0].value == 10, "swap back: a restored");
		check(b.nodes().size() == 2 && b.nodes()[1].value == 30, "swap back: b restored");
	}

	if (failures == 0) {
		std::fprintf(stdout, "param_clone: OK\n");
		std::fflush(stdout);
		// _exit (not return) to skip deluge_app's global static destructors, which assume a fully
		// set-up app — the same reason the projection/golden harnesses use _exit.
		_exit(0);
	}
	std::fprintf(stderr, "param_clone: %d failure(s)\n", failures);
	std::fflush(stderr);
	_exit(1);
}
