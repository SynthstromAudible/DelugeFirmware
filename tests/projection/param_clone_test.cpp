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
#include "memory/general_memory_allocator.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
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
	int32_t i = ap.ownDocForFallback()->nodes.insertSorted(pos, /*isDefinitelyLast=*/true).value_or(-1);
	if (i >= 0) {
		ap.ownDocForFallback()->nodes[i].pos = pos;
		ap.ownDocForFallback()->nodes[i].value = value;
		ap.ownDocForFallback()->nodes[i].interpolated = interpolated;
	}
}

int main() {
	// (a) AutoParam copy assignment: nodes are deep-copied and independent of the source.
	{
		AutoParam src;
		setNode(src, 0, 111, false);
		setNode(src, 48, -222, true);

		AutoParam dst;
		dst = src; // copy assign (replaces the retired cloneFrom(&src, true))
		check(dst.ownDocForFallback()->nodes.size() == 2, "autoparam clone: node count copied");
		check(dst.ownDocForFallback()->nodes[0].value == 111 && dst.ownDocForFallback()->nodes[1].value == -222,
		      "autoparam clone: node values copied");

		src.ownDocForFallback()->nodes[0].value = 999; // mutate the source
		src.ownDocForFallback()->nodes[1].value = 999;
		check(dst.ownDocForFallback()->nodes[0].value == 111 && dst.ownDocForFallback()->nodes[1].value == -222,
		      "autoparam clone: dst independent of src mutation");
	}

	// (a2) AutoParam copy constructor: deep-copies nodes + value.
	{
		AutoParam src;
		setNode(src, 0, 111, false);
		setNode(src, 48, -222, true);
		src.currentValue = 55;

		AutoParam dst(src); // copy ctor
		check(dst.ownDocForFallback()->nodes.size() == 2 && dst.currentValue == 55,
		      "autoparam copy ctor: nodes + value copied");
		src.ownDocForFallback()->nodes[0].value = 999;
		check(dst.ownDocForFallback()->nodes[0].value == 111, "autoparam copy ctor: dst independent");

		AutoParam asn;
		asn = src; // copy assign
		check(asn.ownDocForFallback()->nodes.size() == 2 && asn.currentValue == 55,
		      "autoparam copy assign: nodes + value copied");
		src.ownDocForFallback()->nodes[1].value = 888;
		check(asn.ownDocForFallback()->nodes[1].value == -222, "autoparam copy assign: dst independent");
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
		check(clone.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes.size() == 1,
		      "pm clone: unpatched node copied");
		check(clone.getPatchedParamSet()->params[5].ownDocForFallback()->nodes.size() == 1,
		      "pm clone: patched node copied");
		check(clone.getPatchCableSet()->patchCables[0].param.ownDocForFallback()->nodes.size() == 1,
		      "pm clone: patch-cable node copied");

		// Mutate every collection in the source; the clone must not move.
		pm.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes[0].value = 999;
		pm.getPatchedParamSet()->params[5].ownDocForFallback()->nodes[0].value = 999;
		pcs->patchCables[0].param.ownDocForFallback()->nodes[0].value = 999;

		check(clone.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes[0].value == 321,
		      "pm clone: unpatched node independent");
		check(clone.getPatchedParamSet()->params[5].ownDocForFallback()->nodes[0].value == 100,
		      "pm clone: patched node independent");
		check(clone.getPatchCableSet()->patchCables[0].param.ownDocForFallback()->nodes[0].value == -536870912,
		      "pm clone: patch-cable node independent");
	}

	// (b2) ParamCollection::clone() direct (A3): base impl now, overridden per-subclass later; the harness
	//      re-verifies deep independence through each override.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "clone(): pm setup");
		setNode(pm.getUnpatchedParamSet()->params[2], 16, 321, false);

		ParamCollection* cloned = nullptr;
		Error e = pm.getUnpatchedParamSet()->clone(cloned, /*copyAutomation=*/true);
		check(e == Error::NONE && cloned != nullptr, "clone(): unpatched clone ok");
		if (cloned != nullptr) {
			ParamSet* clonedSet = static_cast<ParamSet*>(cloned);
			check(clonedSet->params[2].ownDocForFallback()->nodes.size() == 1, "clone(): unpatched node copied");
			pm.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes[0].value = 999;
			check(clonedSet->params[2].ownDocForFallback()->nodes[0].value == 321,
			      "clone(): unpatched node independent");
		}
	}

	// (b3) clone(copyAutomation=false): the clone copies structure/values but drops automation (stripAutomation).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "clone(strip): pm setup");
		pm.getUnpatchedParamSet()->params[2].currentValue = 678;
		setNode(pm.getUnpatchedParamSet()->params[2], 16, 321, false);

		ParamCollection* cloned = nullptr;
		Error e = pm.getUnpatchedParamSet()->clone(cloned, /*copyAutomation=*/false);
		check(e == Error::NONE && cloned != nullptr, "clone(strip): clone ok");
		if (cloned != nullptr) {
			ParamSet* clonedSet = static_cast<ParamSet*>(cloned);
			check(clonedSet->params[2].ownDocForFallback()->nodes.size() == 0, "clone(strip): automation dropped");
			check(pm.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes.size() == 1,
			      "clone(strip): source automation intact");
		}
	}

	// (b4) clone(reverse): the clone's nodes are the reversed image of the source; source unchanged.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "clone(reverse): pm setup");
		// Two nodes: pos 0 and pos 48 in a length-96 loop.
		setNode(pm.getUnpatchedParamSet()->params[2], 0, 111, false);
		setNode(pm.getUnpatchedParamSet()->params[2], 48, 222, true);

		ParamCollection* cloned = nullptr;
		Error e = pm.getUnpatchedParamSet()->clone(cloned, /*copyAutomation=*/true, /*reverseWithLength=*/96);
		check(e == Error::NONE && cloned != nullptr, "clone(reverse): clone ok");
		if (cloned != nullptr) {
			ParamSet* clonedSet = static_cast<ParamSet*>(cloned);
			// Reversed: newPos = -oldPos (+96 if <0). Node at 0 stays 0; node at 48 -> 48.
			check(clonedSet->params[2].ownDocForFallback()->nodes.size() == 2, "clone(reverse): node count preserved");
			int32_t p0 = clonedSet->params[2].ownDocForFallback()->nodes[0].pos;
			int32_t p1 = clonedSet->params[2].ownDocForFallback()->nodes[1].pos;
			check(p0 == 0 && p1 == 48, "clone(reverse): node positions reflected");
			// Source untouched by the clone's reverse.
			check(pm.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes[0].pos == 0
			          && pm.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes[1].pos == 48,
			      "clone(reverse): source unchanged");
		}
	}

	// (b5) PatchCableSet::clone() deep-copies the destinations[] pointer arrays (distinct heap, equal contents).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "pcs clone: pm setup");
		PatchCableSet* pcs = pm.getPatchCableSet();
		pcs->numPatchCables = 1;
		pcs->numUsablePatchCables = 1;
		pcs->patchCables[0].from = PatchSource::ENVELOPE_0;
		pcs->patchCables[0].destinationParamDescriptor.setToHaveParamOnly(7);

		// Build a sentinel-terminated (sources==0) Destination array on the allocator so the dtor can free it.
		auto* dests = static_cast<Destination*>(deluge::memory::alloc_fast(sizeof(Destination) * 2));
		check(dests != nullptr, "pcs clone: dests alloc");
		if (dests != nullptr) {
			dests[0].sources = 0b101;
			dests[0].firstCable = 0;
			dests[0].endCable = 1;
			dests[1].sources = 0; // terminator
			pcs->destinations[0 /*GLOBALITY_LOCAL*/] = dests;

			ParamCollection* cloned = nullptr;
			Error e = pcs->clone(cloned, /*copyAutomation=*/true);
			check(e == Error::NONE && cloned != nullptr, "pcs clone: clone ok");
			if (cloned != nullptr) {
				PatchCableSet* clonedPcs = static_cast<PatchCableSet*>(cloned);
				check(clonedPcs->destinations[0 /*GLOBALITY_LOCAL*/] != nullptr, "pcs clone: dests present");
				check(clonedPcs->destinations[0 /*GLOBALITY_LOCAL*/] != pcs->destinations[0 /*GLOBALITY_LOCAL*/],
				      "pcs clone: dests distinct pointer");
				check(clonedPcs->destinations[0 /*GLOBALITY_LOCAL*/][0].sources == 0b101,
				      "pcs clone: dests contents copied");
				// Mutate source; clone must not move.
				pcs->destinations[0 /*GLOBALITY_LOCAL*/][0].sources = 0b111;
				check(clonedPcs->destinations[0 /*GLOBALITY_LOCAL*/][0].sources == 0b101,
				      "pcs clone: dests independent");
			}
		}
	}

	// (d) NoteRow deep-copy ctor: notes + param manager deep-copied and independent; drum pointer shared;
	//     the owned drum-name list is NOT shared (reset to empty). This guards the highest-risk conversion.
	{
		NoteRow src(5);
		check(src.paramManager.setupWithPatching() == Error::NONE, "noterow copy: pm setup");
		setNode(src.paramManager.getUnpatchedParamSet()->params[2], 16, 321, false);

		(void)src.notes.insertAt(0);
		src.notes[0].pos = 10;

		Drum* fakeDrum = reinterpret_cast<Drum*>(0xDEAD0000); // pointer is only copied, never dereferenced
		src.drum = fakeDrum;

		NoteRow dst(src); // copy ctor
		check(dst.y == 5, "noterow copy: y copied");
		check(dst.drum == fakeDrum, "noterow copy: drum pointer shared");
		check(static_cast<DrumName*>(dst.firstOldDrumName) == nullptr, "noterow copy: drum-name list reset");
		check(dst.notes.size() == 1 && dst.notes[0].pos == 10, "noterow copy: notes copied");
		check(dst.paramManager.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes.size() == 1,
		      "noterow copy: param automation copied");

		// Mutate the source; the copy must not move.
		src.notes[0].pos = 20;
		src.paramManager.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes[0].value = 999;
		check(dst.notes[0].pos == 10, "noterow copy: notes independent");
		check(dst.paramManager.getUnpatchedParamSet()->params[2].ownDocForFallback()->nodes[0].value == 321,
		      "noterow copy: param automation independent");
	}

	// (c) Undo swap round-trip (the undo path uses swap, not the byte-copy idiom — regression guard).
	{
		AutoParam a, b;
		setNode(a, 0, 10, false);
		setNode(b, 0, 20, false);
		setNode(b, 8, 30, false);

		a.ownDocForFallback()->nodes.swap(b.ownDocForFallback()->nodes);
		check(a.ownDocForFallback()->nodes.size() == 2 && a.ownDocForFallback()->nodes[1].value == 30,
		      "swap: a took b's nodes");
		check(b.ownDocForFallback()->nodes.size() == 1 && b.ownDocForFallback()->nodes[0].value == 10,
		      "swap: b took a's nodes");

		a.ownDocForFallback()->nodes.swap(b.ownDocForFallback()->nodes); // swap back
		check(a.ownDocForFallback()->nodes.size() == 1 && a.ownDocForFallback()->nodes[0].value == 10,
		      "swap back: a restored");
		check(b.ownDocForFallback()->nodes.size() == 2 && b.ownDocForFallback()->nodes[1].value == 30,
		      "swap back: b restored");
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
