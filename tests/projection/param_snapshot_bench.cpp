// M4a Task 4 — host-sim cost microbench. Compares the two snapshot-update granularities on the param cursor:
//   A. incremental patch — ParamSnapshot::applyPatch, which touches ONE addressed lane (resolveParam + write);
//   B. full rebuild      — write the store then ParamSet::projectFromStoreAfterRead, which re-derives the WHOLE
//                          owner's param set (getNumParams() lanes), the fallback the north-star says the
//                          incremental patch replaces.
// It prints a comparison table (total_ms / per_edit_ns / ratio) at several automation densities. This is a
// MEASUREMENT artifact, not a pass/fail gate — main() always returns 0. The table feeds the north-star's open
// incremental-vs-rebuild granularity question (docs/dev/m4a_snapshot_measurement.md).
//
// std::chrono is fine here: the Date.now/Math.random restriction is a Workflow-script rule, not a C++ one.

#include "definitions_cxx.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>

#include "deluge-project/param_edit.h" // setNodeAtPos
#include "deluge-project/song.h"       // deluge::project::Song::resolveParam
#include "model/edits/param_address_for.h"
#include "model/model_stack.h"
#include "model/snapshot/param_snapshot.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include <cstring>

#include "hid/display/seven_segment.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/global_effectable/global_effectable.h"
#include "model/song/song.h"

namespace {
using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point a, Clock::time_point b) {
	return std::chrono::duration<double, std::milli>(b - a).count();
}

// Build a ModelStackWithAutoParam over `pm`'s unpatched set, bound to `song` + `clip` so its address resolves and
// doc() relocates the set into the model tree (the on-device situation). Mirrors edit_test's makeStack + setup.
ModelStackWithAutoParam* makeResolvableStack(char* buf, ParamManagerForTimeline& pm, int32_t paramId, ::Song* song,
                                             TimelineCounter* clip) {
	std::memset(buf, 0, MODEL_STACK_MAX_SIZE);
	auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
	ms->paramCollection = pm.getUnpatchedParamSet();
	ms->summary = pm.getUnpatchedParamSetSummary();
	ms->kind = pm.getUnpatchedParamSet()->getParamKind();
	ms->paramId = paramId;
	ms->autoParam = &pm.getUnpatchedParamSet()->params[paramId];
	ms->song = song;
	ms->setTimelineCounter(clip);
	ms->paramManager = &pm;
	return ms;
}
} // namespace

int main() {
	display = new deluge::hid::display::SevenSegment;

	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	currentSong = song;

	auto* audioClip = new AudioClip(*song);
	audioClip->setLoopLength(128);
	(void)song->project.clips.session_clips.insertClipAt(audioClip, 0);

	// A resolvable param lane: relocate the engine set into the model tree (as on-device), so applyPatch's
	// resolveParam and projectFromStoreAfterRead operate on the SAME DocParamSet.
	constexpr int32_t kParam = 4;
	ParamManagerForTimeline pm;
	pm.setupUnpatched();
	pm.getUnpatchedParamSet()->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* ms = makeResolvableStack(buf, pm, kParam, song, audioClip);

	std::optional<deluge::project::ParamAddress> addrOpt = deluge::edits::addressFor(*ms);
	if (!addrOpt.has_value()) {
		std::fprintf(stderr, "param_snapshot_bench: could not build a resolvable ParamAddress\n");
		return 0;
	}
	deluge::project::ParamAddress addr = *addrOpt;
	(void)ms->doc(); // relocate the set into the model tree

	UnpatchedParamSet* engineSet = pm.getUnpatchedParamSet();
	ParamCollectionSummary* summary = pm.getUnpatchedParamSetSummary();
	deluge::project::DocParamSet* storeSet = engineSet->domStore();
	const int32_t numParams = engineSet->getNumParams();

	std::fprintf(stdout,
	             "param_snapshot_bench: incremental applyPatch (1 lane) vs full projectFromStoreAfterRead (%d lanes)\n",
	             numParams);
	std::fprintf(stdout,
	             "  each strategy applies the same number of single-param edits; ratio = full / incremental\n\n");

	constexpr int32_t kEdits = 200000;
	const int32_t nodeCounts[] = {0, 8, 64};

	for (int32_t nodeCount : nodeCounts) {
		// Seed `nodeCount` automation nodes into EVERY lane (raises the density the full rebuild must re-scan;
		// isAutomated() then reports true, so projectFromStoreAfterRead does its paramHasAutomationNow work).
		for (int32_t p = 0; p < numParams; p++) {
			deluge::project::DocAutoParam& dp = storeSet->params[p];
			dp.nodes.erase(dp.nodes.begin(), dp.nodes.end());
			for (int32_t n = 0; n < nodeCount; n++) {
				deluge::project::setNodeAtPos(dp, n, n * 1000, false); // positions 0..nodeCount-1 (< loop 128)
			}
		}

		// Strategy A — incremental scalar patch on one addressed lane.
		Clock::time_point a0 = Clock::now();
		for (int32_t i = 0; i < kEdits; i++) {
			deluge::snapshot::ParamSnapshot::applyPatch(song->project, addr, i, 0, 0, std::nullopt);
		}
		Clock::time_point a1 = Clock::now();

		// Strategy B — write the store then re-project the whole set (the fallback the incremental patch replaces).
		Clock::time_point b0 = Clock::now();
		for (int32_t i = 0; i < kEdits; i++) {
			storeSet->params[kParam].current_value = i;
			engineSet->projectFromStoreAfterRead(summary);
		}
		Clock::time_point b1 = Clock::now();

		double aMs = elapsedMs(a0, a1);
		double bMs = elapsedMs(b0, b1);
		double aNs = aMs * 1e6 / kEdits;
		double bNs = bMs * 1e6 / kEdits;

		std::fprintf(stdout, "edits=%d nodes/lane=%d\n", kEdits, nodeCount);
		std::fprintf(stdout, "  %-20s %10.2f ms   %10.1f ns/edit\n", "incremental_patch", aMs, aNs);
		std::fprintf(stdout, "  %-20s %10.2f ms   %10.1f ns/edit\n", "full_rebuild", bMs, bNs);
		std::fprintf(stdout, "  %-20s %9.1fx\n\n", "ratio (full/incr)", aMs > 0.0 ? bMs / aMs : 0.0);
	}

	// Also report the incremental REGION patch cost (scales with nodes, unlike the scalar patch) for reference.
	deluge::snapshot::RegionContext rc{/*effectiveLength*/ 128,
	                                   /*interpolateLeftNode*/ false,
	                                   /*interpolateRightNode*/ false,
	                                   /*recordingLinearly*/ false,
	                                   /*clockActive*/ false,
	                                   /*livePos*/ 0,
	                                   /*currentlyPlayingReversed*/ false};
	Clock::time_point r0 = Clock::now();
	for (int32_t i = 0; i < kEdits; i++) {
		deluge::snapshot::ParamSnapshot::applyPatch(song->project, addr, i, 0, 96, rc);
	}
	Clock::time_point r1 = Clock::now();
	std::fprintf(stdout, "reference: incremental REGION patch (homogenize [0,96), effLen 128)\n");
	std::fprintf(stdout, "  %-20s %10.2f ms   %10.1f ns/edit\n", "region_patch", elapsedMs(r0, r1),
	             elapsedMs(r0, r1) * 1e6 / kEdits);

	return 0;
}
