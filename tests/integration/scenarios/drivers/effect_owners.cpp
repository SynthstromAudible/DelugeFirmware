// Every owner of effects — a synth's sound, a kit, an audio track and the song master — keeps its effect settings in
// its own state, and the render's copy follows each change. Runs as the effect_owners scenario
// (scenarios/drivers/driver.h), on x86 and on ARM.
//
// Exit: 0 = all checks pass, 1 = a failure.
//
// Gate effect-owners:
//     every owner of effects (a synth's sound, a kit, an audio track, the song master) keeps its effect settings in its
//     own state, and the render's copy follows each change (self-checking)
//
// Gate app-asan-effect-owners:
//     the effect-owners gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap
//     poisoning: a kit, an audio track and the master play their chain records' effects, so a record freed under one
//     would read freed memory, and that is fatal here

#include "definitions_cxx.hpp"
#include "effects/chain.h"
#include "effects/state.h"
#include "memory/general_memory_allocator.h"
#include "model/edits/change_effect.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/fx/chain_params.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/gold_knob_gestures.h"
#include "scenarios/slot_coherence.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

#include "model/mod_controllable/filters/filter_config.h"
#include <cstdio>
#include <new>
#include <span>
#include <string>

using namespace deluge;

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
void checkOwner(deluge::model::LaneOwner owner, const char* name) {
	using deluge::serialization::Value;
	deluge::edits::changeEffect(owner, "delay/pingPong", Value::makeNumber(0));
	deluge::edits::changeEffect(owner, "delay/syncLevel", Value::makeNumber(static_cast<int32_t>(SyncLevel::EIGHTH)));
	deluge::edits::changeEffect(owner, "sidechain/release", Value::makeNumber(4242));
	deluge::edits::changeEffect(owner, "audioCompressor/ratio", Value::makeNumber(7 << 24));
	deluge::edits::changeEffect(owner, "stutter/reverse", Value::makeNumber(1));
	deluge::edits::changeEffect(owner, "lpfMode", Value::makeScalar("SVF_Band"));
	const deluge::effects::EffectStates& record = deluge::model::effects_owner_of(owner)->effects();
	check(!record.delay.ping_pong && record.delay.sync_level == SyncLevel::EIGHTH && record.sidechain.release == 4242
	          && record.compressor.ratio == (7 << 24) && record.stutter.reversed
	          && record.filter.lpf_mode == FilterMode::SVF_BAND,
	      name);
	check(deluge::model::effects_owner_of(owner)->binding->renderEffects() == record, name);
}

/// The chain of @p owner (a kit or the master) in @p song, or null.
const deluge::project::FxChain* chainOf(deluge::model::LaneOwner owner, Song& song) {
	const std::optional<deluge::project::ChainAddress> address =
	    deluge::bridge::chainAddressOf(*deluge::model::effects_owner_of(owner), song);
	return address ? song.project.resolveChain(*address) : nullptr;
}

/// Whether @p chain's view holds @p value at @p key.
bool viewHolds(const deluge::project::FxChain* chain, const char* key, const char* value) {
	if (chain == nullptr) {
		return false;
	}
	const deluge::serialization::Value words =
	    deluge::effects::view_value({.mod_fx_param = chain->mod_fx_param, .filter_type = chain->filter_type});
	const deluge::serialization::Value* entry = words.find(key);
	return entry != nullptr && entry->str() == value;
}

/// What a gesture left on the owner's effects and view.
struct Expected {
	int32_t threshold;
	int32_t attack;
	int32_t sidechain_sync_level;
	int32_t mod_fx_type;
	int32_t mod_fx_param;
	int32_t filter_type;
};

/// @p owner's effects and view now.
Expected stateOf(deluge::model::LaneOwner owner) {
	const deluge::effects::EffectStates& state = deluge::model::effects_owner_of(owner)->effects();
	return {state.compressor.threshold,
	        state.compressor.attack,
	        static_cast<int32_t>(state.sidechain.sync_level),
	        static_cast<int32_t>(state.mod_fx.type),
	        static_cast<int32_t>(deluge::model::effects_owner_of(owner)->view->modFXParam()),
	        static_cast<int32_t>(deluge::model::effects_owner_of(owner)->view->filterType())};
}

/// @p state as text.
std::string describe(const Expected& state) {
	return "threshold " + std::to_string(state.threshold) + ", attack " + std::to_string(state.attack)
	       + ", sidechain sync " + std::to_string(state.sidechain_sync_level) + ", mod FX "
	       + std::to_string(state.mod_fx_type) + "/" + std::to_string(state.mod_fx_param) + ", filter "
	       + std::to_string(state.filter_type);
}

/// Each knob and button gesture of @p owner that changes an effect or the view leaves its chain coherent, through the
/// gold knobs' own service on @p focus, and leaves the owner's effects and view as @p expected says.
/// @param expected The state after each gesture in order: the compressor button's upper knob turn, its lower knob
///                 turn, the sidechain sync push (a clip chain's only; the song's has none), the mod-FX type push,
///                 the mod-FX param push and the filter type push.
void checkGestures(deluge::model::LaneOwner owner, Song& song, const deluge::ui::scenes::bind::KnobFocus& focus,
                   const deluge::edits::KnobModeOwner& modeOwner, const char* name, bool clipChain,
                   std::span<const Expected> expected) {
	const std::string who = name;
	size_t gesture = 0;
	const auto coherent = [&](const char* what) {
		const std::string why = deluge::harness::chainIncoherence(song);
		if (!why.empty()) {
			std::fprintf(io.err, "FAIL: %s, %s: %s\n", name, what, why.c_str());
			failures++;
		}
		const Expected now = stateOf(owner);
		const bool known = gesture < expected.size();
		const Expected& want = known ? expected[gesture] : now;
		gesture++;
		if (!known || now.threshold != want.threshold || now.attack != want.attack
		    || now.sidechain_sync_level != want.sidechain_sync_level || now.mod_fx_type != want.mod_fx_type
		    || now.mod_fx_param != want.mod_fx_param || now.filter_type != want.filter_type) {
			std::fprintf(io.err, "FAIL: %s, %s: the state the engine left: %s; now %s\n", name, what,
			             known ? describe(want).c_str() : "none recorded", describe(now).c_str());
			failures++;
		}
	};

	deluge::scenarios::GoldKnobGestures knobs{song, focus};

	// The compressor button: the upper knob turns the threshold. On the song the upper knob's push edits the
	// compressor's setting and the lower's cycles it from the ratio to the attack, which the lower knob then turns; a
	// clip chain's knobs there are its own params, and its upper knob's push steps the sidechain's sync.
	(void)deluge::edits::modKnobModeFor(modeOwner, 4);
	knobs.turn(1, 5);
	coherent(clipChain ? "the compressor button's upper knob (sidechain volume)" : "the compressor's threshold knob");
	if (!clipChain) {
		knobs.push(1, true);
		knobs.push(0, true);
	}
	knobs.turn(0, 5);
	coherent(clipChain ? "the compressor button's lower knob (reverb amount)" : "the compressor's attack knob");
	if (clipChain) {
		knobs.push(1, true);
		coherent("the sidechain sync button");
	}

	(void)deluge::edits::modKnobModeFor(modeOwner, 5);
	knobs.push(1, true);
	coherent("the mod-FX type button");
	knobs.push(0, true);
	coherent("the mod-FX param button");
	check(viewHolds(chainOf(owner, song), "modFXCurrentParam",
	                modFXParamToString(deluge::model::effects_owner_of(owner)->view->modFXParam())),
	      (who + ": the view holds the mod-FX param").c_str());

	(void)deluge::edits::modKnobModeFor(modeOwner, 1);
	knobs.push(1, true);
	coherent("the filter type button");
	check(viewHolds(chainOf(owner, song), "currentFilterType",
	                filterTypeToString(deluge::model::effects_owner_of(owner)->view->filterType())),
	      (who + ": the view holds the filter type").c_str());
	(void)deluge::edits::modKnobModeFor(modeOwner, 0);
}

int effect_owners_main(int /*argc*/, char** /*argv*/, Io driverIo) {
	io = driverIo;

	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&song->paramManager);
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
		static_cast<Kit*>(kit)->effectsOwner()->core->compressor().dsp().set_threshold(
		    deluge::dsp::Q<31>::from_raw(0x12345678));
		const std::string afterRender = deluge::harness::chainIncoherence(*song);
		if (!afterRender.empty()) {
			std::fprintf(io.err, "FAIL: a kit after a render moved its threshold: %s\n", afterRender.c_str());
			failures++;
		}
		const deluge::ui::scenes::bind::KnobFocus focus{
		    .owner =
		        deluge::ui::scenes::bind::ChainFocus{.chain = {deluge::project::ChainAddress::Kind::OUTPUT, *kit->id()},
		                                             .owner = deluge::ui::scenes::bind::ChainOwner::KIT}};
		// The sidechain sync step (7 to 9), the mod-FX type (none to the first type) and the filter view (low-pass to
		// high-pass) are recorded from main's `GlobalEffectableForClip::modEncoderButtonAction` and
		// `GlobalEffectable::modEncoderButtonAction` on the same kit. The compressor setting columns are not from the
		// engine: no production path turns its compressor-setting arm for a kit (its compressor button's knobs are
		// chain params), so these are what the gold knobs leave, the setting where it was.
		constexpr Expected kKit[] = {{0, 83886080, 7, 0, 1, 0}, {0, 83886080, 7, 0, 1, 0}, {0, 83886080, 9, 0, 1, 0},
		                             {0, 83886080, 9, 1, 1, 0}, {0, 83886080, 9, 1, 1, 0}, {0, 83886080, 9, 1, 1, 1}};
		checkGestures(static_cast<Kit*>(kit), *song, focus, deluge::project::TrackAddress{*kit->id()}, "a kit", true,
		              kKit);
	}
	{
		// Recorded from main's `GlobalEffectable::modEncoderActionForNonExistentParam`
		// (`origin/main:src/deluge/model/global_effectable/global_effectable.cpp`) and `modEncoderButtonAction` on the
		// song master.
		constexpr Expected kSong[] = {{83886080, 83886080, 7, 0, 1, 0},
		                              {83886080, 167772160, 7, 0, 1, 0},
		                              {83886080, 167772160, 7, 1, 1, 0},
		                              {83886080, 167772160, 7, 1, 1, 0},
		                              {83886080, 167772160, 7, 1, 1, 1}};
		checkGestures(&song->live.master_channel, *song, {.owner = deluge::ui::scenes::bind::SongFocus{}},
		              deluge::project::SongAddress{}, "the song master", false, kSong);
	}

	if (synth != nullptr) {
		checkOwner(static_cast<SoundInstrument*>(synth), "a synth's sound");
	}
	if (kit != nullptr) {
		checkOwner(static_cast<Kit*>(kit), "a kit's affect-entire");
	}
	if (audio != nullptr) {
		checkOwner(audio, "an audio track");
	}
	checkOwner(&song->live.master_channel, "the song master");

	if (failures == 0) {
		std::fprintf(io.out, "effect_owners: OK\n");
	}
	return failures ? 1 : 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(effect_owners, effect_owners_main)
