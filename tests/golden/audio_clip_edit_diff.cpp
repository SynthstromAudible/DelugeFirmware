// Differential edit/round-trip harness for the AudioClip descriptor (M3 owner-4).
//
//   audio_clip_edit_diff <song.XML>
//
// The load->save golden (song_roundtrip) only proves an AudioClip's descriptor round-trips file->file. It sees
// NEITHER the runtime edit path (the menus / waveform gestures that mutate startPos / attack / reversed / ...)
// NOR the derive-at-load seeding of the render cache. Owner-4's inversion (make DocAudioClip authoritative:
// load->Doc->derive engine, save<-Doc, retire the native cold fields) touches exactly those two uncovered
// surfaces, so a golden-only gate is necessary but not sufficient. This harness closes the gap: it loads a real
// song, drives a deterministic set of "menu-style" edits over the 11 descriptor fields, saves + reloads, and
// dumps a canonical snapshot of each audio clip's engine-observable state (what render reads and what save
// wrote). Because the inversion's contract is behaviour-preservation, the oracle is free: a baseline captured
// from HEAD *before* the flip must be reproduced bit-for-bit after it (audio_clip_edit_run.py diffs stdout
// against a committed baseline). It also asserts (to stderr) that the DocAudioClip matches the engine cache
// after load -- an invariant satisfied trivially today (sync-at-load) and, after the flip, only if the
// derive-at-load seeding is correct.
//
// Exit: 0 = ran + dumped; 1 = load error; 2 = usage/IO; 3 = setup precondition failed (no audio clip / Doc
// mismatch after load).
//
// No real audio scheduler or file IO is driven; edits poke the engine fields the way today's menus do. When the
// owner-4 flip rewires those edits through the DocAudioClip, this file's edit section is updated to the new API
// but the scripted VALUES -- and therefore the committed baseline -- stay fixed.

#include "definitions_cxx.hpp"
#include "deluge-project/doc_audio_clip.h"
#include "deluge-project/serialization.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/song/song.h"
#include "storage/parse_xml.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"
#include "tree_load.h"
#include "util/misc.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>

// Same native load as param_record_diff / song_edit_roundtrip: build a Song, deserialize via the record-first
// Value tree, set currentSong. `xml` must be writable and outlive all uses.
static Song* load_song(char* xml, size_t xmlLen, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);

	std::optional<deluge::storage::Value> tree =
	    deluge::storage::parseXml(std::string_view(reinterpret_cast<const char*>(xml), static_cast<size_t>(xmlLen)));
	Error e = Error::FILE_CORRUPTED;
	if (tree) {
		deluge::storage::ValueTreeDeserializer adapter(*tree, deluge::storage::SourceFormat::Xml);
		activeReader = &adapter;
		e = openTreeFile(adapter, "song");
		if (e == Error::NONE) {
			e = deluge::project::loadSong(*song, adapter);
		}
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	return song;
}

static const char* clipFilePath(const AudioClip* c) {
	// Headless: no sample is ever loaded (loadSample is never called), so audioFile is always null and the portable
	// path lives in sampleHolder.filePath -- exactly the branch writeDataToFile / syncDocAudioClip take here.
	return c->sampleHolder.filePath.c_str();
}

// Canonical, deterministic one-line snapshot of one audio clip's engine-observable descriptor state -- the fields
// the render path reads and writeDataToFile serialises. Order + formatting are fixed for byte-comparison.
static void dumpClip(const char* label, int32_t idx, const AudioClip* c) {
	printf("%s[%d] path=%s start=%llu end=%llu tr=%d cents=%d interp=%d psi=%d rev=%d attack=%ld prio=%d overdub=%d\n",
	       label, idx, clipFilePath(c), (unsigned long long)c->sampleHolder.startPos,
	       (unsigned long long)c->sampleHolder.endPos, (int)c->sampleHolder.transpose, (int)c->sampleHolder.cents,
	       (int)util::to_underlying(c->sampleControls.interpolationMode),
	       (int)c->sampleControls.pitchAndSpeedAreIndependent, (int)c->sampleControls.isCurrentlyReversed(),
	       (long)c->attack(), (int)util::to_underlying(c->voicePriority()), (int)c->overdubsShouldCloneOutput);
}

// Dump every audio clip in the song's session list, in order, under `label`.
static void dumpAllAudioClips(const char* label, Song* song) {
	ClipArray& arr = song->project.clips.session_clips;
	for (int32_t i = 0; i < std::ssize(arr); i++) {
		if (arr[i]->type == ClipType::AUDIO) {
			dumpClip(label, i, static_cast<AudioClip*>(arr[i]));
		}
	}
}

// The DocAudioClip descriptor must agree with the engine cache after a load. Today that holds because
// syncDocAudioClip rebuilds the Doc from the engine at load; after the owner-4 flip it holds only if the
// derive-at-load seeding of sampleHolder / sampleControls from the Doc is correct. Reported to stderr so the
// stdout baseline is unaffected. Returns false on any mismatch.
static bool checkDocMatchesEngine(Song* song) {
	ClipArray& arr = song->project.clips.session_clips;
	bool ok = true;
	for (int32_t i = 0; i < std::ssize(arr); i++) {
		if (arr[i]->type != ClipType::AUDIO) {
			continue;
		}
		AudioClip* c = static_cast<AudioClip*>(arr[i]);
		const deluge::project::DocAudioClip* d = c->docAudioClip_;
		if (d == nullptr) {
			fprintf(stderr, "DOC-CHECK clip[%d]: docAudioClip_ is null\n", i);
			ok = false;
			continue;
		}
		// attack + voice_priority are DOM-owned (retired native fields) — no engine cache to disagree with, so they
		// are not part of the doc-vs-engine cross-check. The remaining fields are still engine caches derived from the
		// record, so they must match.
		if (d->file_path != clipFilePath(c) || d->start_pos != c->sampleHolder.startPos
		    || d->end_pos != c->sampleHolder.endPos || d->transpose != c->sampleHolder.transpose
		    || d->cents != c->sampleHolder.cents || d->overdubs_should_clone != c->overdubsShouldCloneOutput
		    || d->reversed != c->sampleControls.reversed) {
			fprintf(stderr, "DOC-CHECK clip[%d]: DocAudioClip disagrees with engine cache\n", i);
			ok = false;
		}
	}
	return ok;
}

// Save the song to an in-memory buffer, reload it, dump every audio clip under "after-reload", and assert the
// Doc/engine invariant on the reloaded song.
static Error saveReloadDump(Song* song) {
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
		return Error::FILE_CORRUPTED;
	}
	dumpAllAudioClips("after-reload", reloaded);
	Error result = checkDocMatchesEngine(reloaded) ? Error::NONE : Error::FILE_CORRUPTED;
	free(buf);
	return result;
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

	printf("# audio_clip_edit_diff: %s\n", argv[1]);
	dumpAllAudioClips("initial", song);
	if (!checkDocMatchesEngine(song)) {
		fprintf(stderr, "initial DOC-CHECK failed\n");
		return 3;
	}

	// Target the first audio clip for the scripted edits.
	AudioClip* clip = nullptr;
	{
		ClipArray& arr = song->project.clips.session_clips;
		for (int32_t i = 0; i < std::ssize(arr) && clip == nullptr; i++) {
			if (arr[i]->type == ClipType::AUDIO) {
				clip = static_cast<AudioClip*>(arr[i]);
			}
		}
	}
	if (clip == nullptr) {
		fprintf(stderr, "no audio clip in %s\n", argv[1]);
		return 3;
	}

	// --- Scripted "menu-style" edits over the 11 descriptor fields. Poked the way today's audio-clip menus /
	// gestures do (direct engine writes); the owner-4 flip rewires these through the DocAudioClip, but the values
	// -- and the committed baseline -- stay fixed. Deliberately distinct, non-default values.
	clip->sampleHolder.startPos = 1000;
	clip->sampleHolder.endPos = 500000;
	clip->sampleHolder.transpose = 7;
	clip->sampleHolder.cents = -13;
	clip->sampleControls.reversed = !clip->sampleControls.reversed;
	clip->sampleControls.interpolationMode = InterpolationMode::LINEAR;
	clip->sampleControls.pitchAndSpeedAreIndependent = true;
	clip->setAttack(0x30000000);
	clip->setVoicePriority(VoicePriority::HIGH);
	clip->overdubsShouldCloneOutput = !clip->overdubsShouldCloneOutput;
	dumpClip("after-edit", 0, clip);

	// Persistence: save + reload; the edited descriptor must survive the round-trip, and the reloaded engine
	// cache must agree with the DocAudioClip.
	if (saveReloadDump(song) != Error::NONE) {
		return 3;
	}

	free(xml);
	return 0;
}
