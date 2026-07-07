// Edit-aware round-trip driver (Phase 2 / DOM-live-authoritative automation): load a song, apply a DOM-first
// edit to a song-global unpatched param, save, reload, and assert the edit persisted through the DOM path.
//
//   song_edit_roundtrip <song.XML>
//
// This exercises the whole DOM-authoritative edit loop off-hardware — the piece the golden load->save harness
// structurally cannot see (it never edits). We:
//   1. load(original)                                             -> song + populated DOM store
//   2. setUnpatchedValueDomFirst(store, p, SENTINEL, engineParam) -> write DOM store lane + reproject engine
//   3. save                                                       -> gen1
//   4. load(gen1)                                                 -> reload
//   5. require reloaded store[p] == SENTINEL AND reloaded engine[p] == SENTINEL
//
// It passes today with save reading the engine (the DOM-first edit reprojects into it) and will keep passing
// after 5c-3 flips save to read the store — so it is the safety net for that flip.
//
// Exit: 0 = edit persisted; 1 = load error; 2 = usage/IO; 3 = edit not applied / not persisted.

#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h"
#include "deluge-project/serialization.h"
#include "hid/display/display.h"
#include "hid/display/seven_segment.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/global_effectable/global_effectable.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "storage/storage_manager.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace params = deluge::modulation::params;

// load(xml) -> fully-loaded Song (currentSong set, view cleared), or nullptr on load error (code in *loadErr).
// `xml` must be writable (the reader null-terminates tokens in place) and outlive all uses of the Song.
static Song* load_song(char* xml, size_t xmlLen, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);

	XMLDeserializer reader((uint8_t*)xml, xmlLen);
	activeDeserializer = &reader; // gate AudioEngine yields off (callRoutines == false)
	Error e = reader.openXMLFile(nullptr, "song");
	if (e == Error::NONE) {
		e = deluge::project::loadSong(*song, reader);
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr); // drop transient view state (see song_roundtrip.cpp)
	return song;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	const char* songPath = argv[1];

	// Two song-global unpatched params: one gets a distinctive scalar value, the other gets automation nodes.
	// Distinctive non-zero values so they're always serialized and survive the hex round-trip verbatim.
	const int32_t pVal = params::UNPATCHED_REVERB_SEND_AMOUNT;
	const int32_t kSentinel = 0x12345678;
	const int32_t pAuto = params::UNPATCHED_VOLUME;

	FILE* f = std::fopen(songPath, "rb");
	if (!f) {
		std::fprintf(stderr, "cannot open %s\n", songPath);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long fileLen = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);

	const uint32_t kCap = 16u * 1024 * 1024;
	char* orig = (char*)std::malloc(fileLen + 1);
	char* gen1 = (char*)std::malloc(kCap);
	char* gen1copy = (char*)std::malloc(kCap);
	fileLen = (long)std::fread(orig, 1, fileLen, f);
	orig[fileLen] = 0;
	std::fclose(f);

	display = new deluge::hid::display::SevenSegment;

	// 1+2. Load, then DOM-first edit the song-global param.
	Error e1;
	Song* song = load_song(orig, fileLen, &e1);
	if (!song) {
		std::fprintf(stderr, "LOADFAIL original -> Error %d\n", (int)e1);
		_exit(1);
	}

	// Phase C gate: every loaded AudioClip's unpatched set must be BOUND to a DOM store (bindDomStore ran in
	// the post-load loop), and the borrow must be live — engine nodes() aliases the store lane. Run against a
	// song with audio clips (Sampling.XML) to exercise this; songs without audio clips just report seen=0.
	{
		auto isLiveBound = [](ParamSet* s) {
			return s->domStore() != nullptr && s->docForParam(0) == &s->domStore()->params[0];
		};
		auto bothBound = [&](ParamManagerForTimeline& pm) {
			return isLiveBound(pm.getUnpatchedParamSet()) && isLiveBound(pm.getPatchedParamSet());
		};
		// M2-F: patch cables are NOT bound to a shared positional store (they reorder by index at runtime); the
		// cable owns its node store inline (amount_) and owner-resolution returns it.
		auto cablesBound = [](ParamManagerForTimeline& pm) {
			PatchCableSet* pcs = pm.getPatchCableSet();
			return pcs->docForParam(0) == &pcs->patchCables[0].amount_;
		};
		int32_t audioSeen = 0, audioBound = 0, synthSeen = 0, synthBound = 0;
		int32_t kitSeen = 0, kitBound = 0, drumSeen = 0, drumBound = 0, cableSeen = 0, cableBound = 0;
		std::array<ClipArray*, 2> clipArrays{&song->project.clips.session_clips,
		                                     &song->project.clips.arrangement_only_clips};
		for (ClipArray* arr : clipArrays) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (!clip->paramManager.containsAnyMainParamCollections()) {
					continue;
				}
				if (clip->type == ClipType::AUDIO) {
					audioSeen++;
					audioBound += isLiveBound(clip->paramManager.getUnpatchedParamSet()) ? 1 : 0;
				}
				else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
				         && clip->output->type == OutputType::SYNTH) {
					synthSeen++; // a synth clip binds BOTH its unpatched and patched sets
					synthBound += bothBound(clip->paramManager) ? 1 : 0;
					if (clip->paramManager.getPatchCableSet()->numPatchCables > 0) {
						cableSeen++; // ...and its patch cables
						cableBound += cablesBound(clip->paramManager) ? 1 : 0;
					}
				}
				else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
				         && clip->output->type == OutputType::KIT) {
					kitSeen++; // kit clip-level global FX = unpatched only
					kitBound += isLiveBound(clip->paramManager.getUnpatchedParamSet()) ? 1 : 0;
					InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
					for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
						NoteRow& nr = ic->noteRows[n];
						if (nr.drum != nullptr && nr.drum->type == DrumType::SOUND
						    && nr.paramManager.containsAnyMainParamCollections()) {
							drumSeen++; // a SoundDrum row binds unpatched + patched
							drumBound += bothBound(nr.paramManager) ? 1 : 0;
							if (nr.paramManager.getPatchCableSet()->numPatchCables > 0) {
								cableSeen++; // ...and its patch cables
								cableBound += cablesBound(nr.paramManager) ? 1 : 0;
							}
						}
					}
				}
			}
		}
		std::fprintf(stderr, "audio: %d/%d | synth: %d/%d | kit: %d/%d | drums: %d/%d | patchCables: %d/%d\n",
		             audioBound, audioSeen, synthBound, synthSeen, kitBound, kitSeen, drumBound, drumSeen, cableBound,
		             cableSeen);
		if (audioSeen != audioBound || synthSeen != synthBound || kitSeen != kitBound || drumSeen != drumBound
		    || cableSeen != cableBound) {
			std::fprintf(stderr, "PHASE C: a clip/drum loaded UNBOUND (borrow not live)\n");
			_exit(3);
		}
	}

	// The store is populated by the <songParams> read; seed it from the engine if the song had none.
	if (!song->project.global_fx.params.unpatched) {
		song->project.global_fx.params.unpatched =
		    deluge::project::toDocParamSet(*song->paramManager.getUnpatchedParamSet());
	}
	deluge::project::DocParamSet& store = *song->project.global_fx.params.unpatched;
	if (pVal >= (int32_t)store.params.size() || pAuto >= (int32_t)store.params.size()) {
		std::fprintf(stderr, "STORE TOO SMALL (%zu params)\n", store.params.size());
		_exit(3);
	}
	// DOM-first edits on the store: a scalar value + an automation lane (two nodes).
	store.params[pVal].current_value = kSentinel;
	deluge::project::setNodeAtPos(store.params[pAuto], 0, 111, true);
	deluge::project::setNodeAtPos(store.params[pAuto], 48, -222, false);
	// Reproject the store into the engine (+ refresh automation summary flags) so save serializes the edits.
	deluge::project::fromDocParamManager(song->project.global_fx.params, song->paramManager);

	// The edits must be visible in the engine immediately.
	AutoParam& engVal = song->paramManager.getUnpatchedParamSet()->params[pVal];
	AutoParam& engAuto = song->paramManager.getUnpatchedParamSet()->params[pAuto];
	if (engVal.currentValue != kSentinel
	    || song->paramManager.getUnpatchedParamSet()->docForParam(pAuto)->nodes.size() != 2) {
		std::fprintf(stderr, "EDIT DID NOT LAND engVal=%d autoNodes=%zu\n", engVal.currentValue,
		             song->paramManager.getUnpatchedParamSet()->docForParam(pAuto)->nodes.size());
		_exit(3);
	}

	// 3. Save.
	smSerializer.setMemoryBuffer(gen1, kCap);
	deluge::project::saveSong(*song, smSerializer);
	long n1 = smSerializer.bytesWritten();

	// 4. Reload the saved bytes.
	std::memcpy(gen1copy, gen1, n1);
	Error e2;
	Song* reloaded = load_song(gen1copy, n1, &e2);
	if (!reloaded) {
		std::fprintf(stderr, "LOADFAIL gen1 -> Error %d\n", (int)e2);
		_exit(1);
	}

	// M2-G S2b: the save (gen1) assigns serialIds, so on reload every audio clip's unpatched params must be
	// TREE-resident — its store is the project.clip_params entry keyed by serialId, not an engine-owned domStore_.
	// (The corpus files carry no ids on first load, so this only fires post-save — proving the tree path is live.)
	{
		int32_t audioClips = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::AUDIO || !clip->paramManager.containsAnyMainParamCollections()) {
					continue;
				}
				audioClips++;
				if (std::optional<uint32_t> id = clip->serialId()) {
					deluge::project::DocParamManager* mgr = reloaded->project.clip_params.find(*id);
					if (mgr != nullptr && mgr->unpatched
					    && clip->paramManager.getUnpatchedParamSet()->domStore() == &*mgr->unpatched) {
						treeResident++;
					}
				}
			}
		}
		std::fprintf(stderr, "audio clips tree-resident: %d/%d\n", treeResident, audioClips);
		if (audioClips > 0 && treeResident != audioClips) {
			std::fprintf(stderr, "S2b FAIL: an audio clip is not tree-resident after reload\n");
			_exit(3);
		}
	}

	// M2-G S3c: every reloaded SYNTH clip's unpatched (+ patched) automation must be TREE-resident — its store is
	// the project.backed_params entry keyed by (OwnerId::instrument(output serialId), clip serialId), not an
	// engine-owned domStore_. Proves the instrument-shared relocation is live (again only post-save, once ids exist).
	{
		int32_t synthClips = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || clip->output->type != OutputType::SYNTH
				    || !clip->paramManager.containsAnyMainParamCollections()) {
					continue;
				}
				synthClips++;
				std::optional<uint32_t> clipId = clip->serialId();
				std::optional<uint32_t> outId = clip->output->serialId();
				if (clipId.has_value() && outId.has_value()) {
					deluge::project::DocParamManager* mgr =
					    reloaded->project.backed_params.find(deluge::project::OwnerId::instrument(*outId), *clipId);
					if (mgr != nullptr && mgr->unpatched
					    && clip->paramManager.getUnpatchedParamSet()->domStore() == &*mgr->unpatched) {
						treeResident++;
					}
				}
			}
		}
		std::fprintf(stderr, "synth clips tree-resident: %d/%d\n", treeResident, synthClips);
		if (synthClips > 0 && treeResident != synthClips) {
			std::fprintf(stderr, "S3c FAIL: a synth clip is not tree-resident after reload\n");
			_exit(3);
		}
	}

	// 5. Both edits must have persisted into the reloaded DOM store (the authority) and the reloaded engine.
	if (!reloaded->project.global_fx.params.unpatched) {
		std::fprintf(stderr, "reloaded store EMPTY\n");
		_exit(3);
	}
	deluge::project::DocParamSet& rstore = *reloaded->project.global_fx.params.unpatched;
	int32_t storeVal = rstore.params[pVal].current_value;
	int32_t engineVal = reloaded->paramManager.getUnpatchedParamSet()->params[pVal].currentValue;

	const auto& storeNodes = rstore.params[pAuto].nodes;
	const auto& engNodes = reloaded->paramManager.getUnpatchedParamSet()->docForParam(pAuto)->nodes;

	bool valOk = (storeVal == kSentinel) && (engineVal == kSentinel);
	bool autoStoreOk = storeNodes.size() == 2 && storeNodes[0].pos == 0 && storeNodes[0].value == 111
	                   && storeNodes[0].interpolated && storeNodes[1].pos == 48 && storeNodes[1].value == -222
	                   && !storeNodes[1].interpolated;
	bool autoEngOk = engNodes.size() == 2 && engNodes[0].value == 111 && engNodes[1].value == -222;

	bool persisted = valOk && autoStoreOk && autoEngOk;
	std::fprintf(stderr,
	             "value p=%d sentinel=0x%08X reloaded store=0x%08X engine=0x%08X | automation p=%d storeNodes=%zu "
	             "engNodes=%zu | %s\n",
	             pVal, kSentinel, storeVal, engineVal, pAuto, storeNodes.size(), engNodes.size(),
	             persisted ? "PERSISTED" : "LOST");
	_exit(persisted ? 0 : 3);
}
