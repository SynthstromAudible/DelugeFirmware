// Every owner of effects — a synth's sound, a kit, an audio track and the song master — keeps its effect settings in
// its own state, and the render's copy follows each change. Runs as the effect_owners scenario
// (scenarios/drivers/driver.h), on x86 and on ARM.
//
// Exit: 0 = all checks pass, 1 = a failure.

#include "definitions_cxx.hpp"
#include "engines/effects/state.h"
#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/slot_coherence.h"
#include "storage/storage_manager.h"

#include <cstdio>
#include <new>
#include <string>

namespace {

using deluge::scenarios::driver::Io;

Io io{};
int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(io.err, "FAIL: %s\n", what);
		failures++;
	}
}

/// Every effect setting set on @p owner lands in its state, and the render's copy holds the same.
void checkOwner(ModControllableAudio& owner, const char* name) {
	owner.setDelayPingPong(false);
	owner.setDelaySyncLevel(SyncLevel::EIGHTH);
	owner.setSidechainRelease(4242);
	owner.setCompressorRatio(7 << 24);
	owner.setStutterReversed(true);
	owner.setLpfMode(FilterMode::SVF_BAND);
	const deluge::effects::EffectStates& record = owner.effects();
	check(!record.delay.ping_pong && record.delay.sync_level == SyncLevel::EIGHTH && record.sidechain.release == 4242
	          && record.compressor.ratio == (7 << 24) && record.stutter.reversed
	          && record.filter.lpf_mode == FilterMode::SVF_BAND,
	      name);
	check(owner.modControllableSnapshot().effects == record, name);
}

int effect_owners_main(int /*argc*/, char** /*argv*/, Io driverIo) {
	io = driverIo;

	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	currentSong = song;

	ParamManagerForTimeline synthParams;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &synthParams);
	check(synth != nullptr, "a new synth");
	if (synth != nullptr) {
		song->addOutput(synth);
	}
	ParamManagerForTimeline kitParams;
	Instrument* kit = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	check(kit != nullptr, "a new kit");
	if (kit != nullptr) {
		song->addOutput(kit);
	}
	AudioOutput* audio = song->createNewAudioOutput();
	check(audio != nullptr, "a new audio track");

	// A song's host owners each have their chain from the moment they are part of it.
	const std::string incoherent = deluge::harness::chainIncoherence(*song);
	if (!incoherent.empty()) {
		std::fprintf(io.err, "FAIL: a new song's chains: %s\n", incoherent.c_str());
		failures++;
	}

	if (synth != nullptr) {
		checkOwner(*static_cast<SoundInstrument*>(synth), "a synth's sound");
	}
	if (kit != nullptr) {
		checkOwner(*static_cast<Kit*>(kit), "a kit's affect-entire");
	}
	if (audio != nullptr) {
		checkOwner(*audio, "an audio track");
	}
	checkOwner(song->live.global_effectable, "the song master");

	if (failures == 0) {
		std::fprintf(io.out, "effect_owners: OK\n");
	}
	return failures ? 1 : 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(effect_owners, effect_owners_main)
