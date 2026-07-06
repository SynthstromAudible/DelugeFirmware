// Lean differential "record-playback" harness (Movement 2 M2-D/E gate-retirement).
//
//   param_record_diff <song.XML>
//
// The load->save golden and song_edit_roundtrip cannot see the ENGINE's live recording path
// (setCurrentValueInResponseToUserInput + processCurrentPos + undo) — the exact code M2-D.2/.3 forwards to the
// DOM kernels. This harness drives that path natively and deterministically, then dumps a canonical snapshot of
// the resulting automation. Because the refactor's contract is behaviour-preservation, the oracle is free: a
// baseline captured from HEAD *before* a forward must be reproduced bit-for-bit after it (param_record_run.py
// diffs stdout against a committed baseline). No real audio scheduler is driven — "time" is controlled by
// writing AudioEngine::audioSampleTimer and passing an explicit livePos.
//
// Target: the song-global unpatched param UNPATCHED_VOLUME (timelineCounter = the Song; the same param family
// song_edit_roundtrip already exercises). Recording is armed via RecordingMode::ARRANGEMENT, which also makes
// Song::isPlayingAutomationNow() true and satisfies the whole `isPlaying && recording && armed` gate.
//
// Exit: 0 = ran + dumped; 1 = load error; 2 = usage/IO; 3 = setup precondition failed.
//
// The dump is deterministic, so param_record_run.py byte-compares stdout against a committed baseline. The
// baseline is captured from engine-native recording; M2-D.3 (which forwards the recording node write to the
// recordLiveValue kernel) must reproduce it exactly — that is how this harness retires the on-device gate for
// the recording seam.
//
// PROVENANCE: this harness already earned its keep — driving a live Song timeline (which the
// param_projection_test oracle's NoteRow stack never does) surfaced a real firmware bug in the M2-C
// homogenizeRegion forward (an unconditional Song->Clip cast for recordingLinearly; ASan global-buffer-overflow),
// fixed in 0ec566d82.
//
// SCOPE (current): a scripted live-record sequence on a song-global param. Documented extensions (follow-ups):
// undo (swapState) — limited here since getNewAction returns null in ARRANGEMENT mode; save/reload persistence;
// and a finite-loop InstrumentClip target (the other representative recording scenario).

#include "deluge-project/param_automation.h"
#include "deluge-project/serialization.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace params = deluge::modulation::params;

// A live scheduler global (playback_handler.cpp). When true, PlaybackHandler::expectEvent() no-ops instead of
// calling scheduleSwungTick() — which would reach into the RT scheduler that this harness deliberately does not
// run. Setting it true is exactly right here: we drive "ticks" manually, so there is no next event to schedule.
extern bool currentlyActioningSwungTickOrResettingPlayPos;

// Same native load as song_edit_roundtrip: build a Song, deserialize, set currentSong. `xml` must be writable
// (the reader null-terminates in place) and outlive all uses.
static Song* load_song(char* xml, size_t xmlLen, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);

	XMLDeserializer reader((uint8_t*)xml, xmlLen);
	activeDeserializer = &reader;
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

// Print a canonical, deterministic snapshot of one AutoParam's automation state. Order + formatting are fixed so
// the differ can byte-compare across builds/arches.
static void dumpParam(const char* label, AutoParam& ap) {
	printf("%s cv=%ld nodes=%d:", label, (long)ap.getCurrentValue(), (int)std::ssize(ap.nodes()));
	for (int32_t i = 0; i < std::ssize(ap.nodes()); i++) {
		const ParamNode& n = ap.nodes()[i];
		printf(" (%ld,%ld,%c)", (long)n.pos, (long)n.value, n.interpolated ? 'i' : 's');
	}
	printf("\n");
}

// Save `song` to an in-memory XML buffer, reload it into a fresh Song, re-fetch the song-global param, and dump
// it under "after-reload". Proves the recorded automation survives the DOM serialization round-trip.
static void dumpAfterSaveReload(Song* song) {
	const uint32_t cap = 16u * 1024 * 1024;
	char* buf = (char*)malloc(cap);
	smSerializer.setMemoryBuffer(buf, cap);
	deluge::project::saveSong(*song, smSerializer);
	long n = smSerializer.bytesWritten();

	char* copy = (char*)malloc(n + 1);
	memcpy(copy, buf, n);
	copy[n] = 0;

	Error e = Error::NONE;
	Song* reloaded = load_song(copy, n, &e);
	if (!reloaded) {
		printf("after-reload LOAD-FAILED err=%d\n", (int)e);
		free(buf);
		return;
	}
	char msMem[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* ms3 = reloaded->setupModelStackWithSongAsTimelineCounter(msMem);
	ModelStackWithAutoParam* msap = ms3->getUnpatchedAutoParamFromId(params::UNPATCHED_VOLUME);
	if (!msap || !msap->autoParam) {
		printf("after-reload NO-PARAM\n");
		free(buf);
		return;
	}
	dumpParam("after-reload", *msap->autoParam);
	free(buf);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}

	FILE* f = fopen(argv[1], "rb");
	if (!f) {
		fprintf(stderr, "cannot open %s\n", argv[1]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* xml = (char*)malloc(sz + 1);
	if (fread(xml, 1, sz, f) != (size_t)sz) {
		fprintf(stderr, "read error\n");
		return 2;
	}
	xml[sz] = 0;
	fclose(f);

	Error loadErr = Error::NONE;
	Song* song = load_song(xml, sz, &loadErr);
	if (!song) {
		fprintf(stderr, "load error %d\n", (int)loadErr);
		return 1;
	}

	// --- Drive the recording gate deterministically -------------------------------------------------------
	// RecordingMode::ARRANGEMENT makes Song::isPlayingAutomationNow() true AND satisfies `recording != OFF`;
	// armedForRecording defaults true on the TimelineCounter (the Song). Internal clock active + no count-in.
	playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
	playbackHandler.recording = RecordingMode::ARRANGEMENT;
	playbackHandler.ticksLeftInCountIn = 0;
	currentlyActioningSwungTickOrResettingPlayPos =
	    true; // suppress expectEvent()'s scheduleSwungTick (no RT scheduler)
	AudioEngine::audioSampleTimer = 0;

	// Build the song-global unpatched-param ModelStackWithAutoParam via the engine's own helper.
	char msMem[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* ms3 = song->setupModelStackWithSongAsTimelineCounter(msMem);
	ModelStackWithAutoParam* msap = ms3->getUnpatchedAutoParamFromId(params::UNPATCHED_VOLUME);
	if (!msap || !msap->autoParam) {
		fprintf(stderr, "could not build ModelStackWithAutoParam for UNPATCHED_VOLUME\n");
		return 3;
	}
	AutoParam& ap = *msap->autoParam;

	// --- Scripted live recording sequence -----------------------------------------------------------------
	// A deterministic series of "knob turns" at explicit play positions (livePos passed directly, bypassing the
	// play-position plumbing). Between events we advance AudioEngine::audioSampleTimer to model elapsed time,
	// which the override-latch logic reads.
	struct Event {
		int32_t livePos;
		int32_t value;
		uint32_t timerAdvance;
	};
	static const Event script[] = {
	    {0, 400000000, 100},   {12, 900000000, 100}, {24, -300000000, 100},
	    {36, 1500000000, 100}, {48, 200000000, 100}, {60, -800000000, 100},
	};

	printf("# param_record_diff: %s\n", argv[1]);
	dumpParam("initial", ap);

	// Capture the pre-record state (empty nodes + initial value) for the undo test below. AutoParamState is the
	// snapshot an Action holds for undo; swapState(&state) swaps it with the live param.
	AutoParamState undoState;
	undoState.value = ap.getCurrentValue();

	for (const Event& e : script) {
		AudioEngine::audioSampleTimer += e.timerAdvance;
		ap.setCurrentValueInResponseToUserInput(e.value, msap, /*shouldLogAction=*/true, /*livePos=*/e.livePos,
		                                        /*mayDeleteNodesInLinearRun=*/true, /*isMPE=*/false);
	}
	dumpParam("after-record", ap);

	// Undo (swapState with the pre-record snapshot) must revert to empty/initial; redo (swap again) must restore
	// the recorded automation exactly — exercising the AutoParam::swapState node/value swap on the DOM store.
	ap.swapState(&undoState, msap);
	dumpParam("after-undo", ap);
	ap.swapState(&undoState, msap);
	dumpParam("after-redo", ap);

	// Persistence: save the song and reload it; the recorded automation must survive the DOM serialization
	// round-trip. Re-fetch the param from the fresh Song and dump it.
	dumpAfterSaveReload(song);

	free(xml);
	return 0;
}
