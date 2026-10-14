// Clip-clone INTEGRATION test: loads a real song and duplicates an InstrumentClip through the actual
// InstrumentClip::clone() path — the golden-blind edit path the load->save golden structurally cannot see, and
// that the unit driver (param_clone) cannot reach because clone() needs a live Song + ModelStack.
//
//   clip_clone_roundtrip <song.XML>
//
// It asserts:
//   * a plain clone is a distinct clip with the same note-row/notes structure,
//   * the clone is DEEP-INDEPENDENT (mutating the source's notes doesn't leak into the clone),
//   * the clip-level param collections are freshly allocated (distinct pointers) — real copy, not aliasing,
//   * the drum pointer is shared (clones point at the same Drum),
//   * a shouldFlattenReversing clone of a REVERSE clip runs end-to-end, flips direction to FORWARD, and keeps
//     the note count (the reverse note math itself is unit-tested at the AutoParam level).
//
// Exit: 0 = all good, 1 = load error, 2 = usage/IO, 3 = no suitable clip in this song (skip), 4 = assertion failed.

#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/param_address_for.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound_drum.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/param_stores.h"
#include "scenarios/drivers/tree_load.h"
#include "storage/value_tree_deserializer.h"
#include <optional>
#include <string_view>

namespace {

using deluge::scenarios::driver::patchedStore;
using deluge::scenarios::driver::unpatchedStore;

using deluge::scenarios::driver::Io;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};

int failures = 0;
static void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(io.err, "FAIL: %s\n", what);
		failures++;
	}
}

// A clone's DOM store carries the source's authoritative current_value verbatim (ParamSet
// copy-ctor deep-copies domStore_), and its engine cursors are copied too — so store scalar must equal engine
// cursor for every param, exactly as post-load. Locks that clone preserves scalar read-authority.
static bool scalarOk(ParamSet* s) {
	if (s->domStore() == nullptr) {
		return false;
	}
	for (int32_t p = 0; p < s->getNumParams() && p < (int32_t)s->domStore()->params.size(); p++) {
		if (s->domStore()->params[p].current_value != s->params[p].currentValue) {
			return false;
		}
	}
	return true;
}

// Totality sweep: drive doc() over EVERY live automatable owner (clip-level synth/kit/audio unpatched +
// synth patched, and each SoundDrum row's unpatched+patched). doc() lazily relocates any not-yet-tree-resident
// owner (relocateAndResolve), so after the sweep deluge::edits::docFallbackHits must still be 0 — no owner reached
// the engine fallback. This makes the fallback-is-dead claim an exhaustive, corpus-wide host assertion (every
// song via the runner), covering the runtime-created owners the clone phases produce as well as every loaded one.
// Returns the number of (owner, collection) pairs swept, so a vacuous run is visible.
static int32_t sweepDoc(Song* song) {
	int32_t swept = 0;
	auto touch = [&](ModelStackWithAutoParam* msap) {
		if (msap != nullptr && msap->autoParam != nullptr) {
			(void)msap->doc(); // lazily relocates if needed; increments docFallbackHits iff it hits the fallback
			swept++;
		}
	};
	for (ClipArray* arr : {&song->live.clips.session_clips, &song->live.clips.arrangement_only_clips}) {
		for (int32_t i = 0; i < std::ssize(*arr); i++) {
			Clip* clip = (*arr)[i];
			if (clip->output == nullptr || !clip->paramManager.containsAnyMainParamCollections()) {
				continue;
			}
			ModControllable* mc = clip->output->toModControllable();
			if (mc == nullptr) {
				continue;
			}
			char buf[MODEL_STACK_MAX_SIZE]{};
			ModelStackWithThreeMainThings* m3 =
			    setupModelStackWithThreeMainThingsButNoNoteRow(buf, song, mc, clip, &clip->paramManager);
			touch(m3->getUnpatchedAutoParamFromId(0));
			if (clip->output->type == OutputType::SYNTH) {
				touch(m3->getPatchedAutoParamFromId(0));
			}
			if (clip->output->type == OutputType::KIT) {
				InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
				for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
					NoteRow& nr = ic->noteRows[n];
					if (nr.drum == nullptr || nr.drum->type != DrumType::SOUND
					    || !nr.paramManager.containsAnyMainParamCollections()) {
						continue;
					}
					char dbuf[MODEL_STACK_MAX_SIZE]{};
					ModelStackWithThreeMainThings* dm3 = setupModelStackWithThreeMainThingsIncludingNoteRow(
					    dbuf, song, clip, n, &nr, static_cast<SoundDrum*>(nr.drum)->toModControllable(),
					    &nr.paramManager);
					touch(dm3->getUnpatchedAutoParamFromId(0));
					touch(dm3->getPatchedAutoParamFromId(0));
				}
			}
		}
	}
	return swept;
}

// load(xml) -> fully-loaded Song (currentSong set), or nullptr on load error. Mirrors song_edit_roundtrip.cpp.
static Song* load_song(char* xml, size_t xmlLen, deluge::storage::DocumentFormat format, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);

	// Through the Value tree, as a song load is: parse the whole XML, then replay it. The tree reader's
	// shouldCallRoutines() is false, which gates the AudioEngine yields off for the load.
	std::optional<deluge::storage::Value> tree =
	    parseDocument(std::string_view(reinterpret_cast<const char*>(xml), xmlLen), format);
	Error e = Error::FILE_CORRUPTED;
	if (tree) {
		deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
		activeReader = &adapter;
		e = openTreeFile(adapter, "song", format);
		if (e == Error::NONE) {
			e = deluge::bridge::loadSong(*song, adapter);
		}
		activeReader = &smDeserializer;
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	return song;
}

// Find the first InstrumentClip that has a note row containing at least one note.
static InstrumentClip* findClipWithNotes(Song* song, int32_t* rowWithNotes) {
	auto& clips = song->live.clips.session_clips;
	for (size_t i = 0; i < clips.size(); i++) {
		if (clips[i]->type != ClipType::INSTRUMENT) {
			continue;
		}
		auto* ic = static_cast<InstrumentClip*>(clips[i]);
		for (int32_t r = 0; r < std::ssize(ic->noteRows); r++) {
			if (ic->noteRows[r].notes.size() > 0) {
				*rowWithNotes = r;
				return ic;
			}
		}
	}
	return nullptr;
}

int clip_clone_roundtrip_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 2) {
		std::fprintf(io.err, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	FILE* f = std::fopen(argv[1], "rb");
	if (!f) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long fileLen = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	char* orig = (char*)std::malloc(fileLen + 1);
	fileLen = (long)std::fread(orig, 1, fileLen, f);
	orig[fileLen] = 0;
	std::fclose(f);

	Error e1;
	Song* song = load_song(orig, fileLen, deluge::storage::formatOfPath(argv[1]), &e1);
	if (!song) {
		std::fprintf(io.err, "LOADFAIL -> Error %d\n", (int)e1);
		return 1;
	}

	// Marker: the song loaded. Anything that aborts before this is a load-path issue, not a clone defect;
	// the runner uses this to attribute crashes correctly.
	std::fprintf(io.out, "LOADED\n");
	std::fflush(io.out);

	// If the song has an AudioClip, clone it and assert the clone's unpatched params are DOM-bound
	// (AudioClip::clone runs bindDomStore) with a live borrow — the golden-blind audio-clip clone path. Run
	// before the InstrumentClip section so songs with audio clips but no note-clips still exercise it; exit
	// non-zero immediately on failure so the later SKIP can't mask it.
	{
		AudioClip* srcAudio = nullptr;
		int32_t audioClipsSeen = 0;
		for (ClipArray* arr : {&song->live.clips.session_clips, &song->live.clips.arrangement_only_clips}) {
			for (int32_t i = 0; i < std::ssize(*arr); i++) {
				Clip* c = (*arr)[i];
				if (c->type == ClipType::AUDIO && c->paramManager.containsAnyMainParamCollections()) {
					audioClipsSeen++;
					if (srcAudio == nullptr) {
						srcAudio = static_cast<AudioClip*>(c);
					}
				}
			}
		}
		if (srcAudio != nullptr) {
			std::fprintf(io.err, "audio clips: seen=%d, cloning one\n", audioClipsSeen);
			char acMem[256];
			ModelStackWithTimelineCounter* acStack = setupModelStackWithTimelineCounter(acMem, song, srcAudio);
			Error ace = srcAudio->clone(acStack, /*shouldFlattenReversing=*/false);
			check(ace == Error::NONE, "audio clone: returned NONE");
			auto* acClone = static_cast<AudioClip*>(acStack->getTimelineCounter());
			check(acClone != nullptr && acClone != srcAudio, "audio clone: distinct clip object");
			if (acClone != nullptr && acClone != srcAudio) {
				UnpatchedParamSet* cloneUnp = acClone->paramManager.getUnpatchedParamSet();
				check(cloneUnp != srcAudio->paramManager.getUnpatchedParamSet(),
				      "audio clone: unpatched set is a distinct object");
				check(cloneUnp->domStore() != nullptr, "audio clone: unpatched params DOM-bound");
				check(cloneUnp->domStore() != nullptr && cloneUnp->docForParam(0) == &cloneUnp->domStore()->params[0],
				      "audio clone: borrow is live (nodes alias the store lane)");
				check(scalarOk(cloneUnp), "audio clone: store scalar == engine (read-authority preserved)");
			}
			if (failures != 0) {
				std::fprintf(io.err, "clip_clone (audio): %d failure(s)\n", failures);
				std::fflush(io.err);
				return 4;
			}
		}
	}

	// If the song has a kit clip, clone it and assert the clone's clip-level FX set and each
	// cloned SoundDrum row's manager are DOM-bound with a live borrow (InstrumentClip::clone binds them).
	{
		InstrumentClip* srcKit = nullptr;
		for (ClipArray* arr : {&song->live.clips.session_clips, &song->live.clips.arrangement_only_clips}) {
			for (int32_t i = 0; i < std::ssize(*arr) && srcKit == nullptr; i++) {
				Clip* c = (*arr)[i];
				if (c->type == ClipType::INSTRUMENT && c->output != nullptr && c->output->type == OutputType::KIT) {
					srcKit = static_cast<InstrumentClip*>(c);
				}
			}
		}
		if (srcKit != nullptr) {
			char kMem[256];
			ModelStackWithTimelineCounter* kStack = setupModelStackWithTimelineCounter(kMem, song, srcKit);
			Error ke = srcKit->clone(kStack, /*shouldFlattenReversing=*/false);
			check(ke == Error::NONE, "kit clone: returned NONE");
			auto* kClone = static_cast<InstrumentClip*>(kStack->getTimelineCounter());
			check(kClone != nullptr && kClone != srcKit, "kit clone: distinct clip object");
			if (kClone != nullptr && kClone != srcKit) {
				auto liveBound = [](ParamSet* s) {
					return s->domStore() != nullptr && s->docForParam(0) == &s->domStore()->params[0];
				};
				check(liveBound(kClone->paramManager.getUnpatchedParamSet()),
				      "kit clone: clip-level FX DOM-bound with live borrow");
				check(scalarOk(kClone->paramManager.getUnpatchedParamSet()),
				      "kit clone: clip-level FX store scalar == engine");
				int32_t drumRows = 0;
				for (int32_t r = 0; r < std::ssize(kClone->noteRows); r++) {
					NoteRow& nr = kClone->noteRows[r];
					if (nr.drum != nullptr && nr.drum->type == DrumType::SOUND
					    && nr.paramManager.containsAnyMainParamCollections()) {
						drumRows++;
						check(liveBound(nr.paramManager.getUnpatchedParamSet())
						          && liveBound(nr.paramManager.getPatchedParamSet()),
						      "kit clone: SoundDrum row DOM-bound (unpatched + patched)");
						check(scalarOk(nr.paramManager.getUnpatchedParamSet())
						          && scalarOk(nr.paramManager.getPatchedParamSet()),
						      "kit clone: SoundDrum row store scalar == engine");
					}
				}
				// drumRows may legitimately be 0 (a kit whose drum managers are all parked in ParamBackup / empty
				// rows — those bind on restore via NoteRow::setDrum, not at clone). Non-vacuous coverage comes
				// from kits that DO have live drum rows (e.g. Kitchen Clave: 17).
				std::fprintf(io.err, "kit clone: sound-drum rows=%d\n", drumRows);

				// Runtime relocation: the clone is a RUNTIME-created clip, so its clip-level FX still live in
				// an engine-owned INLINE store — doc() must relocate that store into the tree (relocateAndResolve),
				// keyed by the clone's durable id, making the owner id-addressable instead of hitting the
				// paramCollection fallback. Distinguish relocation from the fallback by asserting the store became
				// TREE-resident (backed_params entry), not merely bound.
				{
					// The clone shares the source Kit's output; give it an id if the song never saved one, so the
					// owner has an address.
					if (!kClone->output->id().has_value()) {
						kClone->output->setId(0x5EED0001);
					}
					ParamSet* fx = kClone->paramManager.getUnpatchedParamSet();
					char sbuf[MODEL_STACK_MAX_SIZE]{};
					ModelStackWithThreeMainThings* sm3 = setupModelStackWithThreeMainThingsButNoNoteRow(
					    sbuf, song, static_cast<Kit*>(kClone->output)->toModControllable(), kClone,
					    &kClone->paramManager);
					ModelStackWithAutoParam* msap = sm3->getUnpatchedAutoParamFromId(0);
					if (msap != nullptr && msap->autoParam != nullptr) {
						deluge::project::DocAutoParam* resolved = msap->doc(); // triggers lazy relocation
						uint32_t cid = kClone->id();
						check(cid != 0, "kit clone: the runtime clip carries a durable id");
						deluge::project::OwnerId owner = deluge::project::OwnerId::instrument(*kClone->output->id());
						deluge::project::DocParamManager* mgr = song->project.backed_params.find(owner, cid);
						check(mgr != nullptr && unpatchedStore(*mgr).has_value(),
						      "kit clone: doc() relocated clip-level FX into the tree (not the fallback)");
						if (mgr != nullptr && unpatchedStore(*mgr).has_value()) {
							check(fx->domStore() == &unpatchedStore(*mgr).value(),
							      "kit clone: engine FX set rebound at the tree slot");
							check(resolved == &unpatchedStore(*mgr)->params[0],
							      "kit clone: doc() id-resolves the relocated store");
							// doc() falls back to the set's own store, which is this same lane once bound, so resolve
							// through the model by address: the lane must be found by its id, not by the fallback.
							std::optional<deluge::project::ParamAddress> address = deluge::edits::addressFor(*msap);
							check(address.has_value()
							          && song->project.resolveParam(*address) == &unpatchedStore(*mgr)->params[0],
							      "kit clone: the model resolves the relocated lane by its id");
						}
						std::fprintf(io.err, "kit clone: runtime relocation OK (clip id=%u)\n", cid);
					}
				}
			}
			if (failures != 0) {
				std::fprintf(io.err, "clip_clone (kit): %d failure(s)\n", failures);
				std::fflush(io.err);
				return 4;
			}
		}
	}

	// Totality gate: after loading AND the runtime clone mutations above, doc() must id-resolve EVERY live
	// owner — none may reach the engine fallback. Placed before the note-clone SKIP so it runs for every corpus song.
	{
		int32_t swept = sweepDoc(song);
		std::fprintf(io.err, "doc() totality sweep: %d (owner,collection) pairs, fallback hits=%u\n", swept,
		             deluge::edits::docFallbackHits);
		check(deluge::edits::docFallbackHits == 0,
		      "doc() totality: an owner reached the engine fallback (not id-addressed)");
		if (failures != 0) {
			std::fflush(io.err);
			return 4;
		}
	}

	int32_t row = 0;
	InstrumentClip* src = findClipWithNotes(song, &row);
	if (!src) {
		std::fprintf(io.err, "SKIP: no instrument clip with notes in %s\n", argv[1]);
		return 3;
	}

	int32_t srcRowCount = std::ssize(src->noteRows);
	int32_t srcNoteCount = src->noteRows[row].notes.size();
	int32_t srcNote0Pos = src->noteRows[row].notes[0].pos;
	int32_t srcNote0Vel = src->noteRows[row].notes[0].getVelocity();
	Drum* srcDrum = src->noteRows[row].drum;
	// MIDI clips have no main param collections (getUnpatchedParamSet() would freeze); only capture it when present.
	bool hasMainParams = src->paramManager.containsAnyMainParamCollections();
	auto* srcUnpatched = hasMainParams ? src->paramManager.getUnpatchedParamSet() : nullptr;

	// --- 1. Plain clone (no flatten) ------------------------------------------------------------------
	char modelStackMemory[256];
	ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, song, src);
	Error e = src->clone(modelStack, /*shouldFlattenReversing=*/false);
	check(e == Error::NONE, "plain clone: returned NONE");
	auto* clone = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
	check(clone != nullptr && clone != src, "plain clone: distinct clip object");

	if (clone != nullptr && clone != src) {
		check(std::ssize(clone->noteRows) == srcRowCount, "plain clone: note-row count matches");
		check(clone->noteRows[row].notes.size() == srcNoteCount, "plain clone: note count matches");
		check(clone->noteRows[row].notes[0].pos == srcNote0Pos, "plain clone: note copied");
		// Distinct storage: the clone's notes live in a different buffer than the source's.
		check(&clone->noteRows[row].notes[0] != &src->noteRows[row].notes[0], "plain clone: notes in distinct buffer");
		// Drum pointer shared by design.
		check(clone->noteRows[row].drum == srcDrum, "plain clone: shares drum pointer");
		// Clip-level param collections are freshly allocated (real copy, not aliasing). Only meaningful for
		// clips that have main param collections (synth/audio); MIDI clips clone a MIDIParamCollection instead.
		if (hasMainParams) {
			check(clone->paramManager.getUnpatchedParamSet() != srcUnpatched,
			      "plain clone: unpatched param set is a distinct object");
			// A cloned synth clip's unpatched + patched sets are DOM-bound
			// (InstrumentClip::clone runs bindDomStore) with a live borrow.
			if (src->output != nullptr && src->output->type == OutputType::SYNTH) {
				UnpatchedParamSet* cu = clone->paramManager.getUnpatchedParamSet();
				PatchedParamSet* cp = clone->paramManager.getPatchedParamSet();
				check(cu->domStore() != nullptr && cu->docForParam(0) == &cu->domStore()->params[0],
				      "plain clone (synth): unpatched set DOM-bound with live borrow");
				check(cp->domStore() != nullptr && cp->docForParam(0) == &cp->domStore()->params[0],
				      "plain clone (synth): patched set DOM-bound with live borrow");
			}
		}

		// Deep independence: mutate the source's note, the clone must not move.
		src->noteRows[row].notes[0].setVelocity(srcNote0Vel ^ 0x3F);
		check(clone->noteRows[row].notes[0].getVelocity() == srcNote0Vel,
		      "plain clone: note velocity independent of source mutation");
		src->noteRows[row].notes[0].setVelocity(srcNote0Vel); // restore for the reverse clone below
	}

	// --- 2. Reverse-flatten clone ---------------------------------------------------------------------
	// Force the source into REVERSE and clone with flattening: the flatten path (NoteRow::flattenReverse +
	// ParamManager::flattenReverseAutomation) must run end-to-end, flip the clone's direction to FORWARD, and
	// preserve the note count. (Exact reversed positions are unit-tested at the AutoParam level in param_clone.)
	src->sequenceDirectionMode = SequenceDirection::REVERSE;
	char modelStackMemory2[256];
	ModelStackWithTimelineCounter* modelStack2 = setupModelStackWithTimelineCounter(modelStackMemory2, song, src);
	Error e2 = src->clone(modelStack2, /*shouldFlattenReversing=*/true);
	check(e2 == Error::NONE, "reverse clone: returned NONE");
	auto* rclone = static_cast<InstrumentClip*>(modelStack2->getTimelineCounter());
	check(rclone != nullptr && rclone != src, "reverse clone: distinct clip object");
	if (rclone != nullptr && rclone != src) {
		check(rclone->sequenceDirectionMode == SequenceDirection::FORWARD,
		      "reverse clone: direction flattened to FORWARD");
		check(rclone->noteRows[row].notes.size() == srcNoteCount, "reverse clone: note count preserved");
	}

	if (failures == 0) {
		std::fprintf(io.out, "clip_clone: OK (%s, rows=%d, notes=%d)\n", argv[1], srcRowCount, srcNoteCount);
		std::fflush(io.out);
		return 0;
	}
	std::fprintf(io.err, "clip_clone: %d failure(s)\n", failures);
	std::fflush(io.err);
	return 4;
}

} // namespace

DELUGE_DRIVER_SCENARIO(clip_clone_roundtrip, clip_clone_roundtrip_main)
