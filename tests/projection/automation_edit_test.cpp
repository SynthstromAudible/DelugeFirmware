// Automation-edit harness — param/automation DOM Phase 5c R2, the "hardware test" run in the sim.
//
// R2 relocated the song-global unpatched param node storage out of AutoParam into an owner-held store
// (GlobalEffects::param_node_store) and made the live AutoParams *views* over it (AutoParam::redirectNodeStorage).
// The golden round-trip can't see this: it only load/saves, never edits. This harness drives the exact
// device-UI edit path headless — draw automation, play it back (interpolate), undo/redo, and project for save —
// against the relocated storage, and asserts every operation lands in and reads from the owner store. It is the
// deterministic, CI-repeatable substitute for the on-device test R2's plan demanded, and a stronger one: it also
// runs a differential proving an AutoParam-as-view behaves identically to one with embedded storage.
//
// The engine here is the real one (real AutoParam, ParamManager, Song-as-TimelineCounter, actionLogger, host
// allocator) — the same headless engine the golden harness stands up. Only audio/display/USB/SD are gated.
//
// Exit: 0 = all checks pass, 1 = a failure.

#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "deluge-project/serialization.h"
#include "hid/display/display.h"
#include "hid/display/seven_segment.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/global_effectable/global_effectable.h"
#include "model/global_effectable/global_effectable_for_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node_vector.h"
#include "modulation/params/param_set.h"
#include "storage/storage_manager.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <utility>

namespace params = deluge::modulation::params;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

// Build a headless Song and apply the exact Phase 5c R2 redirect from Song::readFromFile: move each
// song-global unpatched AutoParam's nodes into the owner-held store and redirect the live param to view it.
Song* makeRedirectedSong() {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);

	// --- mirror of the readFromFile tail (song.cpp ~1905) ---
	if (UnpatchedParamSet* unpatchedSet = song->paramManager.getUnpatchedParamSet(); unpatchedSet != nullptr) {
		int32_t numParams = unpatchedSet->getNumParams();
		for (int32_t p = 0; p < numParams; p++) {
			ParamNodeVector& store = song->project.global_fx.param_node_store[p];
			store = std::move(unpatchedSet->params[p].nodes()); // move loaded nodes (embedded) into the store
			unpatchedSet->params[p].redirectNodeStorage(&store);
		}
	}
	song->project.global_fx.params = deluge::project::toDocParamManager(song->paramManager);
	song->paramManager.setAuthoritativeParams(&song->project.global_fx.params);
	// --------------------------------------------------------

	currentSong = song; // setupModelStackWithSongAsTimelineCounter + several deep write paths consult the global
	return song;
}

} // namespace

int main() {
	// Defensive display->popup targets so headless edit paths (e.g. automation-cleared popups) don't deref null.
	display = new deluge::hid::display::SevenSegment;

	Song* song = makeRedirectedSong();

	// The device drives a song-global unpatched param exactly like Song::setBPM does: song-as-timeline-counter
	// three-main-things stack, then resolve the unpatched AutoParam by id.
	constexpr int32_t kParam = params::UNPATCHED_VOLUME;
	char msMem[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* three = song->setupModelStackWithSongAsTimelineCounter(msMem);
	ModelStackWithAutoParam* msap = song->getModelStackWithParam(three, kParam);
	check(msap != nullptr && msap->autoParam != nullptr, "model stack resolves the song-global param");
	if (msap == nullptr || msap->autoParam == nullptr) {
		std::fprintf(stderr, "automation edit: cannot resolve param — aborting\n");
		_exit(1);
	}
	AutoParam* ap = msap->autoParam;
	ParamNodeVector& store = song->project.global_fx.param_node_store[kParam];

	// (1) Redirect is live: the live param's nodes ARE the owner-held store, not embedded storage.
	check(&ap->nodes() == &store, "redirect live: the live param views the owner store");

	// (2) Draw automation — the automation-draw primitive (setNodeAtPos) lands in the owner store.
	ap->setNodeAtPos(0, 100, /*interpolate=*/true);
	ap->setNodeAtPos(48, -200, /*interpolate=*/true);
	ap->setNodeAtPos(96, 300, /*interpolate=*/false);
	check(store.size() == 3, "draw: 3 nodes landed in the owner store");
	check(ap->nodes().size() == 3, "draw: the view sees all 3 nodes");
	check(store.size() == ap->nodes().size() && &ap->nodes()[0] == &store[0],
	      "draw: view and store are the same physical storage");
	check(ap->isAutomated(), "draw: param now reports automated");

	// (3) Playback — interpolation reads through the redirected store.
	check(ap->getValueAtPos(0, msap) == 100, "playback: exact value at node 0");
	check(ap->getValueAtPos(48, msap) == -200, "playback: exact value at node 48");
	int32_t mid = ap->getValueAtPos(24, msap); // between node@0 (100, interp) and node@48 (-200)
	check(mid < 100 && mid > -200, "playback: interpolates between nodes read from the store");
	check(ap->getValueAtPos(72, msap) == -200, "playback: holds non-interpolated node@96's left value");

	// (4) Undo / redo — swapState (what the undo Action ultimately calls) operates on the redirected store.
	AutoParamState snap;
	ap->swapState(&snap, msap); // undo: store nodes -> snapshot, store cleared
	check(store.empty(), "undo: the owner store itself is emptied");
	check(ap->nodes().empty(), "undo: the view reflects the emptied store");
	check(snap.nodes.size() == 3, "undo: automation captured into the snapshot");
	check(!ap->isAutomated(), "undo: param no longer automated");
	ap->swapState(&snap, msap); // redo: snapshot -> store
	check(store.size() == 3, "redo: automation restored into the owner store");
	check(&ap->nodes()[0] == &store[0], "redo: view still backed by the owner store");

	// (5) Save path — the projection that save reads (toDocParamManager over the live manager, whose unpatched
	// params view the store) reflects the store-resident automation.
	deluge::project::DocParamManager doc = deluge::project::toDocParamManager(song->paramManager);
	check(doc.unpatched.has_value() && doc.unpatched->params[kParam].nodes.size() == 3,
	      "save: projection reads the drawn automation out of the store");

	// ...and a full serialize doesn't crash and emits the song (the store feeds the real save path).
	static char outBuf[1u << 20];
	smSerializer.setMemoryBuffer(outBuf, sizeof(outBuf));
	song->setCurrentClip(nullptr); // drop transient view selection (as the golden harness does before save)
	deluge::project::saveSong(*song, smSerializer);
	check(smSerializer.bytesWritten() > 0, "save: serialize produced output");

	// (6) Differential — an AutoParam that is a *view* over external storage behaves identically to one with
	// embedded storage for an identical op sequence. Proves the storage indirection is behaviourally transparent.
	{
		AutoParam embedded; // own storage
		AutoParam viewed;   // redirected to an external store
		ParamNodeVector ext;
		viewed.redirectNodeStorage(&ext);
		struct Op {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		const Op ops[] = {{0, 100, true}, {48, -200, false}, {24, 50, true}, {96, 300, true}, {48, -201, false}};
		for (const Op& o : ops) {
			embedded.setNodeAtPos(o.pos, o.value, o.interpolated);
			viewed.setNodeAtPos(o.pos, o.value, o.interpolated);
		}
		check(embedded.nodes().size() == viewed.nodes().size(), "differential: identical node count");
		check(viewed.nodes().size() == ext.size() && &viewed.nodes()[0] == &ext[0],
		      "differential: the view's ops all landed in the external store");
		bool identical = embedded.nodes().size() == viewed.nodes().size();
		for (size_t i = 0; i < embedded.nodes().size() && i < viewed.nodes().size(); i++) {
			identical &= embedded.nodes()[i].pos == viewed.nodes()[i].pos;
			identical &= embedded.nodes()[i].value == viewed.nodes()[i].value;
			identical &= embedded.nodes()[i].interpolated == viewed.nodes()[i].interpolated;
		}
		check(identical, "differential: view produces node-identical results to embedded storage");
	}

	// (7) Clone-safety (5c R3 foundation) — the clone/backup/steal frontier. Every ParamManager
	// clone/backup/steal funnels through cloneParamCollectionsFrom → memcpy → AutoParam::beenCloned. When the
	// source param is a VIEW over an owner store, the memcpy carries the redirect pointer; beenCloned must sever
	// it so the clone owns INDEPENDENT embedded storage. Otherwise clip-duplicate / undo-backup / note-steal
	// would alias (and corrupt) the owner's nodes. This is what makes relocating cloned owners' storage safe.
	{
		ParamNodeVector ownerStore; // stand-in for a future per-owner store; outlives both managers
		ParamManagerForTimeline srcPm;
		check(srcPm.setupWithPatching() == Error::NONE, "R3 clone: src setup");
		AutoParam& srcParam = srcPm.getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME];
		srcParam.redirectNodeStorage(&ownerStore); // make the source a view over the owner store
		srcParam.setNodeAtPos(0, 111, true);
		srcParam.setNodeAtPos(48, -222, false);
		check(ownerStore.size() == 2, "R3 clone: source draws into the owner store");

		ParamManagerForTimeline clonePm;
		check(clonePm.cloneParamCollectionsFrom(&srcPm, /*copyAutomation=*/true) == Error::NONE,
		      "R3 clone: cloneParamCollectionsFrom succeeds");
		AutoParam& cloneParam = clonePm.getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME];

		check(cloneParam.nodes().size() == 2, "R3 clone: clone copied the automation");
		check(&cloneParam.nodes() != &ownerStore, "R3 clone: clone owns its storage, not a view over the source store");
		// Mutating the clone must not touch the owner store...
		cloneParam.setNodeAtPos(24, 99, true);
		check(cloneParam.nodes().size() == 3 && ownerStore.size() == 2,
		      "R3 clone: clone edit leaves the owner store intact");
		// ...and mutating the owner store (via the source view) must not touch the clone.
		srcParam.setNodeAtPos(96, 7, false);
		check(ownerStore.size() == 3 && cloneParam.nodes().size() == 3,
		      "R3 clone: source-store edit leaves the clone intact");
	}

	// (8) Re-redirect a cloned owner to its OWN store (5c R3b — keeping clones DOM-owned). After a clone (which
	// de-flips to embedded per R3a), the new owner can be redirected to a fresh per-owner store — moving its
	// nodes in — so it becomes a DOM-view again, fully independent of the source. This is the mechanism a cloned
	// owner (e.g. AudioClip::clone relocating into its own per-clip store) uses to keep its params DOM-owned.
	{
		ParamNodeVector storeA; // owner A's store
		ParamManagerForTimeline pmA;
		check(pmA.setupWithPatching() == Error::NONE, "R3b re-redirect: A setup");
		AutoParam& aParam = pmA.getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME];
		aParam.redirectNodeStorage(&storeA);
		aParam.setNodeAtPos(0, 10, true);
		aParam.setNodeAtPos(48, 20, false);

		ParamManagerForTimeline pmB;
		check(pmB.cloneParamCollectionsFrom(&pmA, /*copyAutomation=*/true) == Error::NONE,
		      "R3b re-redirect: clone A→B");
		AutoParam& bParam = pmB.getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME];
		check(&bParam.nodes() != &storeA, "R3b re-redirect: clone starts embedded (R3a de-flip)");

		// Relocate B into its own store (move the cloned nodes in, then redirect) — the clone re-flip pattern.
		ParamNodeVector storeB; // owner B's store
		storeB = std::move(bParam.nodes());
		bParam.redirectNodeStorage(&storeB);
		check(&bParam.nodes() == &storeB, "R3b re-redirect: B is now a view over its OWN store");
		check(storeB.size() == 2 && storeA.size() == 2, "R3b re-redirect: B's nodes moved into B's store, A untouched");

		// Full independence: edits on either store never touch the other.
		bParam.setNodeAtPos(24, 30, true);
		check(storeB.size() == 3 && storeA.size() == 2, "R3b re-redirect: B edit isolated from A's store");
		aParam.setNodeAtPos(96, 40, false);
		check(storeA.size() == 3 && storeB.size() == 3, "R3b re-redirect: A edit isolated from B's store");
	}

	// (9) The real AudioClip pilot (5c R3b) — exercise the actual per-clip store + redirectParamsToOwnStore on
	// the real type, through the exact operations AudioClip::clone performs, and assert clones stay independent.
	{
		AudioClip clipA;
		clipA.paramManager.setupUnpatched();
		GlobalEffectableForClip::initParams(&clipA.paramManager);
		clipA.redirectParamsToOwnStore(); // load-redirect: params become views over clipA's own store
		AutoParam& aVol = clipA.paramManager.getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME];
		check(&aVol.nodes() == &clipA.paramNodeStoreEntryForTest(params::UNPATCHED_VOLUME),
		      "R3b AudioClip: param is a view over the clip's OWN store (load-redirect)");
		aVol.setNodeAtPos(0, 100, true);
		aVol.setNodeAtPos(48, -50, false);
		check(aVol.nodes().size() == 2, "R3b AudioClip: draw lands (via the clip store)");

		// Reproduce AudioClip::clone's param steps: clone the collections (R3a de-flips to embedded), then
		// redirect the clone into its OWN clip store.
		AudioClip clipB;
		check(clipB.paramManager.cloneParamCollectionsFrom(&clipA.paramManager, /*copyAutomation=*/true) == Error::NONE,
		      "R3b AudioClip: clone collections");
		clipB.redirectParamsToOwnStore();
		AutoParam& bVol = clipB.paramManager.getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME];
		check(&bVol.nodes() == &clipB.paramNodeStoreEntryForTest(params::UNPATCHED_VOLUME),
		      "R3b AudioClip: clone param is a view over the CLONE's own store");
		check(bVol.nodes().size() == 2, "R3b AudioClip: clone carried the automation");

		// Independence between the two clips' stores (the whole point of R3b on a cloned owner).
		bVol.setNodeAtPos(24, 7, true);
		check(bVol.nodes().size() == 3 && aVol.nodes().size() == 2, "R3b AudioClip: clone edit isolated from source");
		aVol.setNodeAtPos(96, 9, false);
		check(aVol.nodes().size() == 3 && bVol.nodes().size() == 3, "R3b AudioClip: source edit isolated from clone");
	}

	// (10) Steal-safety (5c R3) — stealParamCollectionsFrom is a pointer-move that bypasses beenCloned. A
	// redirected param moved to another manager (e.g. into a Song param backup when a clip detaches) must
	// de-flip to embedded so it doesn't dangle against the store of the owner it's leaving.
	{
		ParamNodeVector store;
		ParamManagerForTimeline srcPm;
		check(srcPm.setupWithPatching() == Error::NONE, "R3 steal: src setup");
		AutoParam& sParam = srcPm.getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME];
		sParam.redirectNodeStorage(&store);
		sParam.setNodeAtPos(0, 5, true);
		sParam.setNodeAtPos(48, 6, false);
		check(store.size() == 2, "R3 steal: source draws into the store");

		ParamManagerForTimeline destPm; // fresh, no store of its own — like a Song param-backup entry
		destPm.stealParamCollectionsFrom(&srcPm);
		AutoParam& dParam = destPm.getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME];
		check(dParam.nodes().size() == 2, "R3 steal: stolen param kept its automation");
		check(&dParam.nodes() != &store,
		      "R3 steal: stolen param de-flipped to embedded (no longer a dangling view of the old store)");
		// Edits on the moved param, and the old store, are now fully independent.
		dParam.setNodeAtPos(24, 7, true);
		check(dParam.nodes().size() == 3 && store.size() == 2,
		      "R3 steal: moved-param edit isolated from the old store");
	}

	// (11) The InstrumentClip pilot (5c R3b) — a synth clip (patched+unpatched+cables paramManager, backed
	// up/stolen on instrument change) flips BOTH its unpatched AND patched param sets to per-clip stores, safely,
	// through the same clone path. Patch cables stay embedded (setupPatching rebuilds them); safe on the
	// R3a/steal-safety floor.
	{
		InstrumentClip iclipA;
		check(iclipA.paramManager.setupWithPatching() == Error::NONE, "R3b InstrumentClip: A setup");
		iclipA.redirectParamsToOwnStore();
		AutoParam& aPorta = iclipA.paramManager.getUnpatchedParamSet()->params[params::UNPATCHED_PORTAMENTO];
		check(&aPorta.nodes() == &iclipA.paramNodeStoreEntryForTest(params::UNPATCHED_PORTAMENTO),
		      "R3b InstrumentClip: unpatched param is a view over the clip's own store");
		aPorta.setNodeAtPos(0, 42, true);
		aPorta.setNodeAtPos(48, -7, false);
		check(aPorta.nodes().size() == 2, "R3b InstrumentClip: unpatched draw lands via the clip store");

		// The PATCHED set flips too (the real synth automation — filter cutoff etc.).
		AutoParam& aCut = iclipA.paramManager.getPatchedParamSet()->params[params::LOCAL_LPF_FREQ];
		check(&aCut.nodes() == &iclipA.patchedNodeStoreEntryForTest(params::LOCAL_LPF_FREQ),
		      "R3b InstrumentClip: patched param is a view over the clip's own patched store");
		aCut.setNodeAtPos(0, 1000, true);
		aCut.setNodeAtPos(64, -1000, false);
		check(aCut.nodes().size() == 2, "R3b InstrumentClip: patched draw lands via the clip patched store");

		InstrumentClip iclipB;
		check(iclipB.paramManager.cloneParamCollectionsFrom(&iclipA.paramManager, /*copyAutomation=*/true)
		          == Error::NONE,
		      "R3b InstrumentClip: clone collections");
		iclipB.redirectParamsToOwnStore();
		AutoParam& bPorta = iclipB.paramManager.getUnpatchedParamSet()->params[params::UNPATCHED_PORTAMENTO];
		check(&bPorta.nodes() == &iclipB.paramNodeStoreEntryForTest(params::UNPATCHED_PORTAMENTO),
		      "R3b InstrumentClip: clone param is a view over the CLONE's own store");
		check(bPorta.nodes().size() == 2, "R3b InstrumentClip: clone carried the automation");
		bPorta.setNodeAtPos(24, 1, true);
		check(bPorta.nodes().size() == 3 && aPorta.nodes().size() == 2,
		      "R3b InstrumentClip: clone unpatched edit isolated from source");
		aPorta.setNodeAtPos(96, 2, false);
		check(aPorta.nodes().size() == 3 && bPorta.nodes().size() == 3,
		      "R3b InstrumentClip: source unpatched edit isolated from clone");

		// Patched set: clone is a view over the CLONE's own patched store, carried the automation, independent.
		AutoParam& bCut = iclipB.paramManager.getPatchedParamSet()->params[params::LOCAL_LPF_FREQ];
		check(&bCut.nodes() == &iclipB.patchedNodeStoreEntryForTest(params::LOCAL_LPF_FREQ),
		      "R3b InstrumentClip: clone patched param is a view over the CLONE's own patched store");
		check(bCut.nodes().size() == 2, "R3b InstrumentClip: clone carried the patched automation");
		bCut.setNodeAtPos(32, 5, true);
		check(bCut.nodes().size() == 3 && aCut.nodes().size() == 2,
		      "R3b InstrumentClip: clone patched edit isolated from source");
	}

	if (failures == 0) {
		std::fprintf(stdout, "automation edit (5c R2+R3): OK\n");
		std::fflush(stdout);
		// _exit (not return) to skip deluge_app's global static destructors, which assume a fully set-up app
		// (non-null display etc.) — same reason the golden + projection harnesses use _exit.
		_exit(0);
	}
	std::fprintf(stderr, "automation edit (5c R2): %d failure(s)\n", failures);
	std::fflush(stderr);
	_exit(1);
}
