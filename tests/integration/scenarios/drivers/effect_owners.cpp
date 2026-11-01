// Every owner of effects — a synth's sound, a kit, an audio track and the song master — keeps its effect settings in
// its own state, and the render's copy follows each change. Runs as the effect_owners scenario
// (scenarios/drivers/driver.h), on x86 and on ARM.
//
// Exit: 0 = all checks pass, 1 = a failure.

#include "definitions_cxx.hpp"
#include "effects/state.h"
#include "memory/general_memory_allocator.h"
#include "model/edits/change_effect.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/slot_coherence.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

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

/// Every effect setting set on @p owner, as a gesture sets it (by its Edit when the owner is in the song), lands in its
/// state, and the render's copy holds the same.
void checkOwner(ModControllableAudio& owner, const char* name) {
	using deluge::serialization::Value;
	deluge::edits::changeEffect(owner, "delay/pingPong", Value::makeNumber(0));
	deluge::edits::changeEffect(owner, "delay/syncLevel", Value::makeNumber(static_cast<int32_t>(SyncLevel::EIGHTH)));
	deluge::edits::changeEffect(owner, "sidechain/release", Value::makeNumber(4242));
	deluge::edits::changeEffect(owner, "audioCompressor/ratio", Value::makeNumber(7 << 24));
	deluge::edits::changeEffect(owner, "stutter/reverse", Value::makeNumber(1));
	deluge::edits::changeEffect(owner, "lpfMode", Value::makeScalar("SVF_Band"));
	const deluge::effects::EffectStates& record = owner.effects();
	check(!record.delay.ping_pong && record.delay.sync_level == SyncLevel::EIGHTH && record.sidechain.release == 4242
	          && record.compressor.ratio == (7 << 24) && record.stutter.reversed
	          && record.filter.lpf_mode == FilterMode::SVF_BAND,
	      name);
	check(owner.modControllableSnapshot().effects == record, name);
}

/// The chain of @p owner (a kit or the master) in @p song, or null.
const deluge::project::FxChain* chainOf(GlobalEffectable& owner, Song& song) {
	const std::optional<deluge::project::ChainAddress> address = deluge::bridge::chainAddressOf(owner, song);
	return address ? song.project.resolveChain(*address) : nullptr;
}

/// Whether @p chain's view holds @p value at @p key.
bool viewHolds(const deluge::project::FxChain* chain, const char* key, const char* value) {
	const deluge::serialization::Value* entry = chain != nullptr ? chain->view.find(key) : nullptr;
	return entry != nullptr && entry->str() == value;
}

/// Each knob and button gesture of @p owner that changes an effect or the view leaves its chain coherent.
void checkGestures(GlobalEffectable& owner, ParamManagerForTimeline& paramManager, Song& song, const char* name,
                   bool clipChain) {
	const std::string who = name;
	const auto coherent = [&](const char* gesture) {
		const std::string what = deluge::harness::chainIncoherence(song);
		if (!what.empty()) {
			std::fprintf(io.err, "FAIL: %s, %s: %s\n", name, gesture, what.c_str());
			failures++;
		}
	};
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* stack =
	    setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, &owner, nullptr, &paramManager);

	*owner.getModKnobMode() = 4;
	(void)owner.modEncoderActionForNonExistentParam(5, 1, nullptr);
	coherent("the compressor's threshold knob");
	owner.currentCompParam = CompParam::ATTACK;
	(void)owner.modEncoderActionForNonExistentParam(5, 0, nullptr);
	coherent("the compressor's attack knob");
	if (clipChain) {
		(void)owner.modEncoderButtonAction(1, true, stack);
		coherent("the sidechain sync button");
	}

	*owner.getModKnobMode() = 5;
	(void)owner.modEncoderButtonAction(1, true, stack);
	coherent("the mod-FX type button");
	(void)owner.modEncoderButtonAction(0, true, stack);
	coherent("the mod-FX param button");
	check(viewHolds(chainOf(owner, song), "modFXCurrentParam", modFXParamToString(owner.modFXParam())),
	      (who + ": the view holds the mod-FX param").c_str());

	*owner.getModKnobMode() = 1;
	(void)owner.modEncoderButtonAction(1, true, stack);
	coherent("the filter type button");
	check(viewHolds(chainOf(owner, song), "currentFilterType", filterTypeToString(owner.filterType())),
	      (who + ": the view holds the filter type").c_str());
	*owner.getModKnobMode() = 0;
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

	if (kit != nullptr) {
		// The render takes a clip chain's threshold from its param each block: the chain holds the owner's state,
		// which that does not move.
		static_cast<Kit*>(kit)->core().compressor().dsp().set_threshold(0x12345678);
		const std::string afterRender = deluge::harness::chainIncoherence(*song);
		if (!afterRender.empty()) {
			std::fprintf(io.err, "FAIL: a kit after a render moved its threshold: %s\n", afterRender.c_str());
			failures++;
		}
		checkGestures(*static_cast<Kit*>(kit), kitParams, *song, "a kit", true);
	}
	checkGestures(song->live.global_effectable, song->paramManager, *song, "the song master", false);

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
