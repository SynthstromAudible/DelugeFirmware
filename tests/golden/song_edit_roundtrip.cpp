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
#include "deluge-project/serialization.h"
#include "hid/display/display.h"
#include "hid/display/seven_segment.h"
#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "storage/storage_manager.h"

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

	// A song-global unpatched param and a distinctive, non-zero value (so it's always serialized) that differs
	// from any plausible stored value and survives the hex round-trip verbatim.
	const int32_t p = params::UNPATCHED_REVERB_SEND_AMOUNT;
	const int32_t kSentinel = 0x12345678;

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
	// The store is populated by the <defaultParams> read; seed it from the engine if the song had none.
	if (!song->project.global_fx.params.unpatched) {
		song->project.global_fx.params.unpatched =
		    deluge::project::toDocParamSet(*song->paramManager.getUnpatchedParamSet());
	}
	bool applied = deluge::project::setUnpatchedValueDomFirst(song->project.global_fx.params, p, kSentinel,
	                                                          song->paramManager.getUnpatchedParamSet()->params[p]);
	if (!applied) {
		std::fprintf(stderr, "EDIT NOT APPLIED (store unpopulated / p out of range)\n");
		_exit(3);
	}
	// The edit must be visible in BOTH the store lane and the reprojected engine param immediately.
	int32_t storeNow = song->project.global_fx.params.unpatched->params[p].current_value;
	int32_t engineNow = song->paramManager.getUnpatchedParamSet()->params[p].currentValue;
	if (storeNow != kSentinel || engineNow != kSentinel) {
		std::fprintf(stderr, "EDIT DID NOT LAND store=%d engine=%d\n", storeNow, engineNow);
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

	// 5. The edit must have persisted into both the reloaded DOM store and the reloaded engine.
	int32_t storeVal = reloaded->project.global_fx.params.unpatched
	                       ? reloaded->project.global_fx.params.unpatched->params[p].current_value
	                       : INT32_MIN;
	int32_t engineVal = reloaded->paramManager.getUnpatchedParamSet()->params[p].currentValue;

	bool persisted = (storeVal == kSentinel) && (engineVal == kSentinel);
	std::fprintf(stderr, "edited p=%d sentinel=0x%08X reloaded store=0x%08X engine=0x%08X %s\n", p, kSentinel, storeVal,
	             engineVal, persisted ? "PERSISTED" : "LOST");
	_exit(persisted ? 0 : 3);
}
