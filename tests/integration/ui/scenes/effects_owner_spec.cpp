/// @file
/// @brief `deluge::model::EffectsOwner` and the functions over it: what each owner kind's `effectsOwner()` names, and
///        how a store, a bind and a projection reach the settings and the render's copy.

#include "effects/plugin.h"
#include "effects/state.h"
#include "effects/words.h"
#include "gui/ui/ui.h"
#include "model/fx/effects_owner.h"
#include "model/fx/stutter_gesture.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/setting_apply.h"
#include "model/song/song.h"
#include "plugin/types/sync.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"

#include <functional>
#include <string>
#include <vector>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "model/mod_controllable/filters/filter_config.h"
#include "support/printers/effect_states_printer.h"
#include "support/spec_main.h"

using namespace deluge;

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;
using deluge::model::EffectsOwner;
using deluge::serialization::Value;

namespace {

/// @brief One effect setting as a menu writes it, and the change a direct write of its field makes.
struct SettingCase {
	std::string path;
	Value value;
	std::function<void(deluge::effects::EffectStates&)> field;
};

Value word(std::string_view text) {
	return Value::makeScalar(std::string{text});
}

std::vector<SettingCase> settingCases() {
	namespace fx = deluge::effects;
	using E = fx::EffectStates;
	return {
	    {"modFXType", word(fx::to_string(ModFXType::CHORUS)), [](E& e) { e.mod_fx.type = ModFXType::CHORUS; }},
	    {"lpfMode", word(fx::to_string(FilterMode::SVF_NOTCH)),
	     [](E& e) { e.filter.lpf_mode = FilterMode::SVF_NOTCH; }},
	    {"hpfMode", word(fx::to_string(FilterMode::SVF_BAND)), [](E& e) { e.filter.hpf_mode = FilterMode::SVF_BAND; }},
	    {"filterRoute", word(fx::to_string(FilterRoute::PARALLEL)),
	     [](E& e) { e.filter.route = FilterRoute::PARALLEL; }},
	    {"clippingAmount", Value::makeNumber(7), [](E& e) { e.saturation.clipping_amount = 7; }},
	    {"delay/pingPong", Value::makeNumber(0), [](E& e) { e.delay.ping_pong = false; }},
	    {"delay/type", word(fx::to_string(fx::DelayType::ANALOG)), [](E& e) { e.delay.type = fx::DelayType::ANALOG; }},
	    {"delay/syncType", Value::makeNumber(10), [](E& e) { e.delay.sync_type = SyncType::TRIPLET; }},
	    {"delay/syncLevel", Value::makeNumber(5),
	     [](E& e) { e.delay.sync_level = deluge::plugin::sync_level_from_index(5); }},
	    {"sidechain/attack", Value::makeNumber(12345), [](E& e) { e.sidechain.attack = 12345; }},
	    {"sidechain/release", Value::makeNumber(23456), [](E& e) { e.sidechain.release = 23456; }},
	    {"sidechain/syncType", Value::makeNumber(19), [](E& e) { e.sidechain.sync_type = SyncType::DOTTED; }},
	    {"sidechain/syncLevel", Value::makeNumber(3),
	     [](E& e) { e.sidechain.sync_level = deluge::plugin::sync_level_from_index(3); }},
	    {"audioCompressor/attack", Value::makeNumber(9 << 24), [](E& e) { e.compressor.attack = 9 << 24; }},
	    {"audioCompressor/release", Value::makeNumber(10 << 24), [](E& e) { e.compressor.release = 10 << 24; }},
	    {"audioCompressor/thresh", Value::makeNumber(11 << 24), [](E& e) { e.compressor.threshold = 11 << 24; }},
	    {"audioCompressor/ratio", Value::makeNumber(12 << 24), [](E& e) { e.compressor.ratio = 12 << 24; }},
	    {"audioCompressor/compHPF", Value::makeNumber(13 << 24), [](E& e) { e.compressor.sidechain_hpf = 13 << 24; }},
	    {"audioCompressor/compBlend", Value::makeNumber(14 << 24), [](E& e) { e.compressor.blend = 14 << 24; }},
	    {"stutter/quantized", Value::makeNumber(0), [](E& e) { e.stutter.quantized = false; }},
	    {"stutter/reverse", Value::makeNumber(1), [](E& e) { e.stutter.reversed = true; }},
	    {"stutter/pingPong", Value::makeNumber(1), [](E& e) { e.stutter.ping_pong = true; }},
	    {"stutter/useSongStutter", Value::makeNumber(0), [](E& e) { e.stutter.use_song_stutter = false; }},
	};
}

} // namespace

// clang-format off
describe effects_owner("an owner's EffectsOwner", ${
	context("as each owner kind names it", _ {
		it("names a synth's chain and binding, and the sound, with no view", _ {
			spec::SongFixture f = spec::make_synth_song();
			auto& synth = *static_cast<SoundInstrument*>(f.instrument);
			const std::optional<EffectsOwner> owner = synth.effectsOwner();
			expect(owner.has_value()).to_be_true();
			expect(owner->core).to_equal(&synth.core(), "the synth owner's chain core");
			expect(owner->sound).to_equal(static_cast<Sound*>(&synth), "the synth owner's sound");
			expect(owner->view).to_be_null("the synth owner's view");
			expect(int(owner->unpatchedKind())).to_equal(int(params::Kind::UNPATCHED_SOUND), "the synth owner's unpatched kind");
		});

		it("names a kit's and an audio track's channel, with its view", _ {
			spec::SongFixture kit = spec::make_kit_song();
			auto& k = *static_cast<Kit*>(kit.instrument);
			const std::optional<EffectsOwner> ofKit = k.effectsOwner();
			expect(ofKit->core).to_equal(&k.channel().core(), "the kit owner's chain core");
			expect(ofKit->binding).to_equal(&k.channel().binding(), "the kit owner's binding");
			expect(ofKit->view).to_equal(&k.channel().view(), "the kit owner's view");
			expect(ofKit->sound).to_be_null("the kit owner's sound");
			expect(int(ofKit->unpatchedKind())).to_equal(int(params::Kind::UNPATCHED_GLOBAL), "the kit owner's unpatched kind");

			spec::AudioSongFixture audio = spec::make_audio_clip_song();
			const std::optional<EffectsOwner> ofAudio = audio.output->effectsOwner();
			expect(ofAudio->core).to_equal(&audio.output->channel().core(), "the audio owner's chain core");
			expect(ofAudio->view).to_equal(&audio.output->channel().view(), "the audio owner's view");
		});

		it("names the song's master channel through the song's owner slot", _ {
			spec::SongFixture f = spec::make_synth_song();
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* stack = f.song->setupModelStackWithSongAsTimelineCounter(memory);
			const std::optional<EffectsOwner> owner = deluge::model::effects_owner_of(stack->owner);
			expect(owner->core).to_equal(&f.song->live.master_channel.core(), "the song owner's chain core");
			expect(owner->binding).to_equal(&f.song->live.master_channel.binding(), "the song owner's binding");
			expect(owner->view).to_equal(&f.song->live.master_channel.view(), "the song owner's view");
		});

		it("names nothing for a MIDI instrument", _ {
			spec::SongFixture f = spec::make_midi_song();
			expect(static_cast<MIDIInstrument*>(f.instrument)->effectsOwner().has_value()).to_be_false();
		});
	});

	context("an effect setting applied to an owner outside the song", _ {
		it("writes the one field its menu sets, for each effect setting", _ {
			spec::SongFixture f = spec::make_synth_song();
			ParamManagerForTimeline params;
			auto* loose = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
			loose->setupAsDefaultSynth(&params);
			const EffectsOwner owner = *loose->effectsOwner();
			const deluge::effects::EffectStates base = owner.effects();
			for (const SettingCase& c : settingCases()) {
				deluge::model::storeEffects(owner, base);
				deluge::effects::EffectStates expected = base;
				c.field(expected);
				expect(c.path + (deluge::bridge::applyEffectSetting(owner, c.path, c.value) == Error::NONE ? " ok" : " refused"))
				    .to_equal(c.path + " ok");
				expect(c.path + (owner.effects() == expected ? " matches" : " differs")).to_equal(c.path + " matches");
			}
		});
	});

	context("a host chain's view and record", _ {
		it("sets the view in the bound record and the render's copy", _ {
			spec::SongFixture f = spec::make_kit_song();
			const EffectsOwner owner = *static_cast<Kit*>(f.instrument)->effectsOwner();
			const deluge::project::FxChain* record = owner.view->bound();
			expect(record).not_().to_be_null("the bound record");
			deluge::model::setChainView(owner, ModFXParam::OFFSET, FilterType::HPF);
			if (record != nullptr) {
				expect(int(record->mod_fx_param)).to_equal(int(ModFXParam::OFFSET), "the record's mod FX view");
				expect(int(record->filter_type)).to_equal(int(FilterType::HPF), "the record's filter view");
			}
			expect(int(owner.binding->renderModFXParam())).to_equal(int(ModFXParam::OFFSET), "the render's mod FX view");
		});

		it("seeds a record it binds from what it plays, and takes it back when unbound", _ {
			spec::SongFixture f = spec::make_kit_song();
			const EffectsOwner owner = *static_cast<Kit*>(f.instrument)->effectsOwner();
			deluge::model::unbindChain(owner);
			expect(owner.view->bound()).to_be_null("the record bound before binding");
			deluge::model::setChainView(owner, ModFXParam::DEPTH, FilterType::LPF);
			deluge::model::changeEffects(owner, [](deluge::effects::EffectStates& e) { e.filter.lpf_mode = FilterMode::SVF_BAND; });

			deluge::project::FxChain chain{};
			deluge::model::bindChain(owner, chain);
			expect(owner.view->bound()).to_equal(static_cast<const deluge::project::FxChain*>(&chain), "the bound record");
			expect(int(chain.mod_fx_param)).to_equal(int(ModFXParam::DEPTH), "the seeded mod FX view");
			const auto* seeded = chain.effects.as<deluge::effects::DelugeEffects>();
			expect(seeded).not_().to_be_null("the seeded effects");
			if (seeded != nullptr) {
				expect(int(seeded->filter.lpf_mode)).to_equal(int(FilterMode::SVF_BAND), "the seeded LPF mode");
			}
			deluge::model::changeEffects(owner, [](deluge::effects::EffectStates& e) { e.filter.lpf_mode = FilterMode::SVF_NOTCH; });
			deluge::model::setChainView(owner, ModFXParam::FEEDBACK, FilterType::HPF);

			deluge::model::unbindChain(owner);
			expect(owner.view->bound()).to_be_null("the record bound after unbinding");
			expect(int(owner.effects().filter.lpf_mode)).to_equal(int(FilterMode::SVF_NOTCH), "the LPF mode taken back");
			expect(int(owner.view->modFXParam())).to_equal(int(ModFXParam::FEEDBACK), "the mod FX view taken back");
			expect(int(owner.view->filterType())).to_equal(int(FilterType::HPF), "the filter view taken back");
		});
	});

	context("chains by address", _ {
		it("finds the master, a kit's and an audio track's chain, and nothing for a synth or a missing id", _ {
			spec::SongFixture kit = spec::make_kit_song();
			using deluge::project::ChainAddress;
			const auto song = deluge::bridge::chainAt({.kind = ChainAddress::Kind::SONG}, *kit.song);
			expect(song.has_value()).to_be_true("the master chain is found");
			if (song.has_value()) {
				expect(song->core).to_equal(&kit.song->live.master_channel.core(), "the master chain's core");
			}
			auto& k = *static_cast<Kit*>(kit.instrument);
			const auto ofKit = deluge::bridge::chainAt({.kind = ChainAddress::Kind::OUTPUT, .id = *k.id()}, *kit.song);
			expect(ofKit.has_value()).to_be_true("the kit's chain is found");
			if (ofKit.has_value()) {
				expect(ofKit->core).to_equal(&k.channel().core(), "the kit chain's core");
			}
			expect(deluge::bridge::chainAt({.kind = ChainAddress::Kind::OUTPUT, .id = 0xFFFFFF}, *kit.song).has_value())
			    .to_be_false();

			spec::AudioSongFixture audio = spec::make_audio_clip_song();
			const auto ofAudio =
			    deluge::bridge::chainAt({.kind = ChainAddress::Kind::OUTPUT, .id = *audio.output->id()}, *audio.song);
			expect(ofAudio.has_value()).to_be_true("the audio track's chain is found");
			if (ofAudio.has_value()) {
				expect(ofAudio->core).to_equal(&audio.output->channel().core(), "the audio chain's core");
			}

			spec::SongFixture synth = spec::make_synth_song();
			expect(deluge::bridge::chainAt({.kind = ChainAddress::Kind::OUTPUT, .id = *synth.instrument->id()},
			                               *synth.song)
			           .has_value())
			    .to_be_false();
		});
	});

	context("the song's stutter and an owner leaving", _ {
		it("ends the stutter only for the chain that plays it", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto& kit = *static_cast<Kit*>(f.instrument);
			const uint32_t saved = currentUIMode;
			currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
			deluge::model::beginStutter(*kit.effectsOwner(), f.clip->paramManager);
			expect(f.song->live.stutter.isStuttering(kit.channel().core())).to_be_true();
			deluge::model::endStutterIfStuttering(f.song->live.master_channel.core());
			expect(f.song->live.stutter.isStuttering(kit.channel().core())).to_be_true();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
			deluge::model::endStutterIfStuttering(kit.channel().core());
			expect(f.song->live.stutter.active()).to_be_false();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
			currentUIMode = saved;
			spec::make_current(nullptr);
		});
	});

	context("owners by controls address", _ {
		it("finds a sound only for a synth or a sound row, and each owner's effects and slot", _ {
			using deluge::project::ControlsAddress;
			spec::SongFixture kit = spec::make_kit_song();
			auto* drum = spec::add_drum<SoundDrum>(kit);
			auto& k = *static_cast<Kit*>(kit.instrument);
			const ControlsAddress song{ControlsAddress::Kind::SONG, 0};
			const ControlsAddress ofKit{ControlsAddress::Kind::OUTPUT, *k.id()};
			const ControlsAddress ofRow{ControlsAddress::Kind::DRUM, *drum->id()};
			expect(deluge::bridge::soundAt(*kit.song, song)).to_be_null("the song's sound");
			expect(deluge::bridge::soundAt(*kit.song, ofKit)).to_be_null("the kit's sound");
			expect(deluge::bridge::soundAt(*kit.song, ofRow)).to_equal(static_cast<Sound*>(drum), "the sound row's sound");
			expect(deluge::bridge::effectsOwnerAt(*kit.song, song)->core)
			    .to_equal(&kit.song->live.master_channel.core(), "the song owner's chain core");
			expect(deluge::bridge::effectsOwnerAt(*kit.song, ofKit)->core)
			    .to_equal(&k.channel().core(), "the kit owner's chain core");
			expect(deluge::bridge::effectsOwnerAt(*kit.song, ofRow)->sound)
			    .to_equal(static_cast<Sound*>(drum), "the sound row owner's sound");
			expect(std::get<Kit*>(deluge::bridge::controlsSlotAt(*kit.song, ofKit))).to_equal(&k, "the kit's controls slot");

			spec::SongFixture synth = spec::make_synth_song();
			const ControlsAddress ofSynth{ControlsAddress::Kind::OUTPUT, *synth.instrument->id()};
			expect(deluge::bridge::soundAt(*synth.song, ofSynth))
			    .to_equal(static_cast<Sound*>(static_cast<SoundInstrument*>(synth.instrument)), "the synth's sound");

			spec::SongFixture midi = spec::make_midi_song();
			const ControlsAddress ofMidi{ControlsAddress::Kind::OUTPUT, *midi.instrument->id()};
			expect(deluge::bridge::effectsOwnerAt(*midi.song, ofMidi).has_value()).to_be_false();
		});
	});

	context("storing, binding and projecting", _ {
		it("stores a sound's settings and projects them into the render's copy", _ {
			spec::SongFixture f = spec::make_synth_song();
			const EffectsOwner owner = *static_cast<SoundInstrument*>(f.instrument)->effectsOwner();
			deluge::effects::EffectStates next = owner.effects();
			next.filter.lpf_mode = FilterMode::SVF_NOTCH;
			next.stutter.reversed = true;
			deluge::model::storeEffects(owner, next);
			expect(owner.effects()).to_equal(next, "the stored settings");
			expect(owner.binding->renderEffects()).to_equal(next, "the render's settings");
		});

		it("projects a host chain's selected mod FX knob", _ {
			spec::SongFixture f = spec::make_kit_song();
			auto& kit = *static_cast<Kit*>(f.instrument);
			const EffectsOwner owner = *kit.effectsOwner();
			kit.channel().view().write(ModFXParam::DEPTH, kit.channel().view().filterType());
			deluge::model::projectEffects(owner);
			expect(int(owner.binding->renderModFXParam())).to_equal(int(ModFXParam::DEPTH), "the render's mod FX view");
		});

		it("plays a bound record's settings, keeps stores in it, and takes them back when unbound", _ {
			spec::SongFixture f = spec::make_synth_song();
			const EffectsOwner owner = *static_cast<SoundInstrument*>(f.instrument)->effectsOwner();
			deluge::effects::EffectStates held = owner.effects();
			held.delay.ping_pong = !held.delay.ping_pong;

			deluge::effects::EffectStates record = held;
			deluge::model::bindEffects(owner, &record);
			expect(owner.effects()).to_equal(held, "the settings it plays");
			expect(owner.binding->renderEffects()).to_equal(held, "the render's copy");
			deluge::effects::EffectStates next = held;
			next.stutter.quantized = !next.stutter.quantized;
			deluge::model::storeEffects(owner, next);
			expect(record).to_equal(next, "the bound record after a store");

			deluge::model::bindEffects(owner, nullptr);
			expect(owner.effects()).to_equal(next, "the settings taken back");
		});
	});
});

CPPSPEC_SPEC(effects_owner)
