// Clip-clone INTEGRATION test: loads a real song and duplicates an InstrumentClip through the actual
// InstrumentClip::clone() path — the golden-blind edit path the load->save golden structurally cannot see, and
// that the unit harness (param_clone_test) cannot reach because clone() needs a live Song + ModelStack.
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
#include "deluge-project/serialization.h"
#include "hid/display/display.h"
#include "hid/display/seven_segment.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

int failures = 0;
static void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

// load(xml) -> fully-loaded Song (currentSong set), or nullptr on load error. Mirrors song_edit_roundtrip.cpp.
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
	song->setCurrentClip(nullptr);
	return song;
}

// Find the first InstrumentClip that has a note row containing at least one note.
static InstrumentClip* findClipWithNotes(Song* song, int32_t* rowWithNotes) {
	auto& clips = song->project.clips.session_clips;
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

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	FILE* f = std::fopen(argv[1], "rb");
	if (!f) {
		std::fprintf(stderr, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long fileLen = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	char* orig = (char*)std::malloc(fileLen + 1);
	fileLen = (long)std::fread(orig, 1, fileLen, f);
	orig[fileLen] = 0;
	std::fclose(f);

	display = new deluge::hid::display::SevenSegment;

	Error e1;
	Song* song = load_song(orig, fileLen, &e1);
	if (!song) {
		std::fprintf(stderr, "LOADFAIL -> Error %d\n", (int)e1);
		_exit(1);
	}

	// Marker: the song loaded. Anything that aborts before this is a load-path issue, not a clone defect;
	// the runner uses this to attribute crashes correctly.
	std::fprintf(stdout, "LOADED\n");
	std::fflush(stdout);

	// Phase C: if the song has an AudioClip, clone it and assert the clone's unpatched params are DOM-bound
	// (AudioClip::clone runs bindDomStore) with a live borrow — the golden-blind audio-clip clone path. Run
	// before the InstrumentClip section so songs with audio clips but no note-clips still exercise it; exit
	// non-zero immediately on failure so the later SKIP can't mask it.
	{
		AudioClip* srcAudio = nullptr;
		int32_t audioClipsSeen = 0;
		for (ClipArray* arr : {&song->project.clips.session_clips, &song->project.clips.arrangement_only_clips}) {
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
			std::fprintf(stderr, "audio clips: seen=%d, cloning one\n", audioClipsSeen);
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
			}
			if (failures != 0) {
				std::fprintf(stderr, "clip_clone (audio): %d failure(s)\n", failures);
				std::fflush(stderr);
				_exit(4);
			}
		}
	}

	// Phase C (kit): if the song has a kit clip, clone it and assert the clone's clip-level FX set and each
	// cloned SoundDrum row's manager are DOM-bound with a live borrow (InstrumentClip::clone binds them).
	{
		InstrumentClip* srcKit = nullptr;
		for (ClipArray* arr : {&song->project.clips.session_clips, &song->project.clips.arrangement_only_clips}) {
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
				int32_t drumRows = 0;
				for (int32_t r = 0; r < std::ssize(kClone->noteRows); r++) {
					NoteRow& nr = kClone->noteRows[r];
					if (nr.drum != nullptr && nr.drum->type == DrumType::SOUND
					    && nr.paramManager.containsAnyMainParamCollections()) {
						drumRows++;
						check(liveBound(nr.paramManager.getUnpatchedParamSet())
						          && liveBound(nr.paramManager.getPatchedParamSet()),
						      "kit clone: SoundDrum row DOM-bound (unpatched + patched)");
					}
				}
				// drumRows may legitimately be 0 (a kit whose drum managers are all parked in ParamBackup / empty
				// rows — those bind on restore via NoteRow::setDrum, not at clone). Non-vacuous coverage comes
				// from kits that DO have live drum rows (e.g. Kitchen Clave: 17).
				std::fprintf(stderr, "kit clone: sound-drum rows=%d\n", drumRows);
			}
			if (failures != 0) {
				std::fprintf(stderr, "clip_clone (kit): %d failure(s)\n", failures);
				std::fflush(stderr);
				_exit(4);
			}
		}
	}

	int32_t row = 0;
	InstrumentClip* src = findClipWithNotes(song, &row);
	if (!src) {
		std::fprintf(stderr, "SKIP: no instrument clip with notes in %s\n", argv[1]);
		_exit(3);
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
			// Phase C (InstrumentClip, synth): a cloned synth clip's unpatched + patched sets are DOM-bound
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
	// preserve the note count. (Exact reversed positions are unit-tested at the AutoParam level in param_clone_test.)
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
		std::fprintf(stdout, "clip_clone: OK (%s, rows=%d, notes=%d)\n", argv[1], srcRowCount, srcNoteCount);
		std::fflush(stdout);
		_exit(0);
	}
	std::fprintf(stderr, "clip_clone: %d failure(s)\n", failures);
	std::fflush(stderr);
	_exit(4);
}
