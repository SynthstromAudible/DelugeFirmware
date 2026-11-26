/// @file
/// @brief What the effects owners do on the paths that reach them through a ModelStack's owner slot, a stutter, a
///        hibernation, a destructor and a clone, for each owner kind: the song's master, a kit, an audio track, a
///        synth, and a sound the song does not hold.
///
/// Every case goes through the entry point the app uses (a param stack's collection, `bridge::stutterAt`, an Edit or
/// `edits::changeEffect`, the owner's own lifecycle call) and observes from outside the owner: the song's stutter, the
/// UI mode, the param values, the delay's reserve and the settings an owner plays.

#include "effects/words.h"
#include "gui/ui/ui.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/edits/edit.h"
#include "model/edits/set_effect_setting.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_instrument.h"
#include "project/fx_chain.h"
#include "project/mapping_table.h"
#include "storage/storage_manager.h"

#include <cstdint>
#include <string>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/printers/effect_sections_printer.h"
#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;
namespace project = deluge::project;
using deluge::serialization::Value;

namespace {

/// @brief The settings @p owner plays.
const deluge::effects::EffectStates& effectsOf(deluge::model::LaneOwner owner) {
	return deluge::model::effects_owner_of(owner)->effects();
}

/// @brief The song's master stutter set quantized or free, by the Edit its menu makes.
void setSongStutterQuantized(Song& song, bool quantized) {
	deluge::edits::Context ctx{song, song.project};
	deluge::edits::SetEffectSetting edit{project::ChainAddress{.kind = project::ChainAddress::Kind::SONG},
	                                     "stutter/quantized", Value::makeNumber(quantized ? 1 : 0)};
	(void)deluge::edits::apply(ctx, edit);
}

/// @brief Whether the stutter-rate knob of the owner @p stack holds shows its middle position.
bool middleShown(ModelStackWithThreeMainThings* stack) {
	ModelStackWithAutoParam* rate = stack->getUnpatchedAutoParamFromId(params::UNPATCHED_STUTTER_RATE);
	return rate->paramCollection->shouldParamIndicateMiddleValue(rate);
}

/// @brief A synth built as a preset preview builds one: its own params, never in the song.
struct LooseSynth {
	ParamManagerForTimeline params;
	SoundInstrument* synth = nullptr;
};

LooseSynth* makeLooseSynth() {
	auto* loose = new LooseSynth();
	loose->synth =
	    static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &loose->params));
	loose->synth->setupAsDefaultSynth(&loose->params);
	return loose;
}

/// @brief The loose synth's patched param @p p, as a gold knob on it reaches it.
ModelStackWithAutoParam* looseParam(Song& song, LooseSynth& loose, int32_t p, void* memory) {
	return setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, loose.synth, nullptr, &loose.params)
	    ->getPatchedAutoParamFromId(p);
}

/// @brief Saves and restores the UI mode around a case.
struct UiMode {
	uint32_t saved = currentUIMode;
	explicit UiMode(uint32_t mode) { currentUIMode = mode; }
	~UiMode() { currentUIMode = saved; }
};

project::ParamAddress songAddress() {
	return project::ParamAddress{.owner = project::OwnerId::song()};
}

project::ParamAddress instrumentAddress(const spec::SongFixture& f) {
	return project::ParamAddress{.owner = project::OwnerId::instrument(*f.instrument->id()), .clip_id = f.clip->id()};
}

project::ParamAddress audioAddress(const spec::AudioSongFixture& f) {
	return project::ParamAddress{.owner = project::OwnerId::clip(f.clip->id())};
}

} // namespace

// clang-format off
describe effects_owner_characterisation("the effects owners, through the paths that reach them", ${
	context("the stutter-rate knob's middle position", _ {
		it("follows the master's quantize setting on the song's own params", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			setSongStutterQuantized(*f.song, false);
			expect(middleShown(f.song->setupModelStackWithSongAsTimelineCounter(memory))).to_be_true();
			setSongStutterQuantized(*f.song, true);
			expect(middleShown(f.song->setupModelStackWithSongAsTimelineCounter(memory))).to_be_false();
			{
				UiMode stuttering{UI_MODE_STUTTERING};
				expect(middleShown(f.song->setupModelStackWithSongAsTimelineCounter(memory))).to_be_true();
			}
			spec::make_current(nullptr);
		});

		it("follows a kit's own quantize setting on the kit's params", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto& kit = *static_cast<Kit*>(f.instrument);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			auto stack = [&] {
				return setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, &kit, f.clip,
				                                                      &f.clip->paramManager);
			};
			deluge::edits::changeEffect(*f.song, &kit, "stutter/useSongStutter", Value::makeNumber(0));
			deluge::edits::changeEffect(*f.song, &kit, "stutter/quantized", Value::makeNumber(1));
			expect(middleShown(stack())).to_be_false();
			{
				UiMode stuttering{UI_MODE_STUTTERING};
				expect(middleShown(stack())).to_be_true();
			}
			deluge::edits::changeEffect(*f.song, &kit, "stutter/quantized", Value::makeNumber(0));
			expect(middleShown(stack())).to_be_true();
			spec::make_current(nullptr);
		});

		it("follows the master's setting for a synth that uses the song's stutter", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			auto& synth = *static_cast<SoundInstrument*>(f.instrument);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			auto stack = [&] {
				return setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, &synth, f.clip,
				                                                      &f.clip->paramManager);
			};
			deluge::edits::changeEffect(*f.song, &synth, "stutter/useSongStutter", Value::makeNumber(1));
			deluge::edits::changeEffect(*f.song, &synth, "stutter/quantized", Value::makeNumber(0));
			setSongStutterQuantized(*f.song, true);
			expect(middleShown(stack())).to_be_false();
			setSongStutterQuantized(*f.song, false);
			expect(middleShown(stack())).to_be_true();
			spec::make_current(nullptr);
		});
	});

	context("the delay of a sound the song does not hold", _ {
		it("keeps its reserve when the rate knob moves (the walk finds no owner of its chain)", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			LooseSynth* loose = makeLooseSynth();
			deluge::edits::changeEffect(*f.song, loose->synth, "delay/type",
			                            Value::makeScalar(std::string(deluge::effects::to_string(
			                                deluge::effects::DelayType::DIGITAL))));
			const std::size_t before = loose->synth->core().delay().reserve();
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* rate = looseParam(*f.song, *loose, params::GLOBAL_DELAY_RATE, memory);
			rate->autoParam->setCurrentValueInResponseToUserInput(INT32_MIN, rate);
			expect(loose->synth->core().delay().reserve()).to_equal(before);
			spec::make_current(nullptr);
		});

		it("turns the delay on when its feedback lifts from nothing", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			LooseSynth* loose = makeLooseSynth();
			deluge::edits::changeEffect(*f.song, loose->synth, "delay/type",
			                            Value::makeScalar(std::string(deluge::effects::to_string(
			                                deluge::effects::DelayType::OFF))));
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* feedback = looseParam(*f.song, *loose, params::GLOBAL_DELAY_FEEDBACK, memory);
			feedback->autoParam->setCurrentValueInResponseToUserInput(INT32_MIN, feedback);
			expect(int(effectsOf(loose->synth).delay.type)).to_equal(int(deluge::effects::DelayType::OFF), "the delay type at no feedback");
			feedback->autoParam->setCurrentValueInResponseToUserInput(0, feedback);
			expect(int(effectsOf(loose->synth).delay.type))
			    .not_()
			    .to_equal(int(deluge::effects::DelayType::OFF), "the delay type with feedback");
			spec::make_current(nullptr);
		});
	});

	context("the delay of a synth in the song", _ {
		it("takes the reserve a slower rate needs when the rate knob moves", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			auto& synth = *static_cast<SoundInstrument*>(f.instrument);
			deluge::edits::changeEffect(*f.song, &synth, "delay/type",
			                            Value::makeScalar(std::string(deluge::effects::to_string(
			                                deluge::effects::DelayType::DIGITAL))));
			const std::size_t before = synth.core().delay().reserve();
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* rate =
			    setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, &synth, f.clip, &f.clip->paramManager)
			        ->getPatchedAutoParamFromId(params::GLOBAL_DELAY_RATE);
			rate->autoParam->setCurrentValueInResponseToUserInput(INT32_MIN, rate);
			expect(synth.core().delay().reserve()).to_be_greater_than(before);
			spec::make_current(nullptr);
		});
	});

	context("a stutter begun and ended by address", _ {
		it("begins and ends on the song's master", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			UiMode pressed{UI_MODE_CLIP_PRESSED_IN_SONG_VIEW};
			expect(deluge::bridge::stutterAt(*f.song, songAddress(), true)).to_be_true();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
			expect(f.song->live.stutter.isStuttering(f.song->live.master_channel.core())).to_be_true();
			expect(deluge::bridge::stutterAt(*f.song, songAddress(), false)).to_be_true();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
			expect(f.song->live.stutter.active()).to_be_false();
			spec::make_current(nullptr);
		});

		it("begins and ends on a kit, restoring a quantized rate", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto& kit = *static_cast<Kit*>(f.instrument);
			deluge::edits::changeEffect(*f.song, &kit, "stutter/useSongStutter", Value::makeNumber(0));
			deluge::edits::changeEffect(*f.song, &kit, "stutter/quantized", Value::makeNumber(1));
			UnpatchedParamSet* unpatched = f.clip->paramManager.getUnpatchedParamSet();
			const int32_t began_at = unpatched->knobPosToParamValue(20, nullptr);
			unpatched->params[params::UNPATCHED_STUTTER_RATE].setPlayed(began_at);
			UiMode pressed{UI_MODE_CLIP_PRESSED_IN_SONG_VIEW};
			expect(deluge::bridge::stutterAt(*f.song, instrumentAddress(f), true)).to_be_true();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
			expect(f.song->live.stutter.isStuttering(kit.channel().core())).to_be_true();
			unpatched->params[params::UNPATCHED_STUTTER_RATE].setPlayed(0);
			expect(deluge::bridge::stutterAt(*f.song, instrumentAddress(f), false)).to_be_true();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
			expect(unpatched->getValue(params::UNPATCHED_STUTTER_RATE)).to_equal(began_at);
			spec::make_current(nullptr);
		});

		it("begins and ends on an audio track", _ {
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			spec::make_current(f.song);
			UiMode pressed{UI_MODE_CLIP_PRESSED_IN_SONG_VIEW};
			expect(deluge::bridge::stutterAt(*f.song, audioAddress(f), true)).to_be_true();
			expect(f.song->live.stutter.isStuttering(f.output->channel().core())).to_be_true();
			expect(deluge::bridge::stutterAt(*f.song, audioAddress(f), false)).to_be_true();
			expect(f.song->live.stutter.active()).to_be_false();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
			spec::make_current(nullptr);
		});

		it("begins and ends on a synth", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			auto& synth = *static_cast<SoundInstrument*>(f.instrument);
			deluge::edits::changeEffect(*f.song, &synth, "stutter/useSongStutter", Value::makeNumber(0));
			UiMode pressed{UI_MODE_CLIP_PRESSED_IN_SONG_VIEW};
			expect(deluge::bridge::stutterAt(*f.song, instrumentAddress(f), true)).to_be_true();
			expect(f.song->live.stutter.isStuttering(synth.core())).to_be_true();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_true();
			expect(deluge::bridge::stutterAt(*f.song, instrumentAddress(f), false)).to_be_true();
			expect(f.song->live.stutter.active()).to_be_false();
			spec::make_current(nullptr);
		});

		it("does not begin outside the modes a stutter begins in", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			UiMode scrolling{UI_MODE_HORIZONTAL_SCROLL};
			(void)deluge::bridge::stutterAt(*f.song, songAddress(), true);
			expect(f.song->live.stutter.active()).to_be_false();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
			spec::make_current(nullptr);
		});
	});

	context("an owner leaving the render", _ {
		it("ends a kit's stutter when the kit hibernates", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto& kit = *static_cast<Kit*>(f.instrument);
			UiMode pressed{UI_MODE_CLIP_PRESSED_IN_SONG_VIEW};
			expect(deluge::bridge::stutterAt(*f.song, instrumentAddress(f), true)).to_be_true();
			kit.prepareForHibernationOrDeletion();
			expect(f.song->live.stutter.active()).to_be_false();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
			spec::make_current(nullptr);
		});

		it("ends an audio track's stutter when the track is destroyed", _ {
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			spec::make_current(f.song);
			auto* other = spec::detail::place<AudioOutput>();
			UiMode pressed{UI_MODE_CLIP_PRESSED_IN_SONG_VIEW};
			expect(int(f.song->live.stutter.begin(other->channel().core(), effectsOf(other).stutter, 1 << 24)))
			    .to_equal(int(Error::NONE), "the stutter's start");
			enterUIMode(UI_MODE_STUTTERING);
			other->~AudioOutput();
			expect(f.song->live.stutter.active()).to_be_false();
			expect(isUIModeActive(UI_MODE_STUTTERING)).to_be_false();
			spec::make_current(nullptr);
		});
	});

	context("an audio track's clone", _ {
		it("takes the other's effects, view and held knobs, keeping its own compressor", _ {
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			spec::make_current(f.song);
			AudioOutput* source = f.output;
			AudioOutput* clone = f.song->createNewAudioOutput();
			deluge::edits::changeEffect(*f.song, source, "lpfMode", Value::makeScalar("SVF_Notch"));
			deluge::edits::changeEffect(*f.song, source, "delay/pingPong", Value::makeNumber(0));
			deluge::edits::changeEffect(*f.song, source, "stutter/reverse", Value::makeNumber(1));
			deluge::edits::changeEffect(*f.song, source, "audioCompressor/ratio", Value::makeNumber(7 << 24));
			deluge::edits::changeView(source, "modFXCurrentParam", modFXParamToString(ModFXParam::DEPTH)); // not the default, so a clone that skips the view shows
			source->effectsOwner()->knobs->loaded.emplace_back();
			const deluge::effects::CompressorState compressor = effectsOf(clone).compressor;

			clone->cloneFrom(source);

			const deluge::effects::EffectStates& got = effectsOf(clone);
			const deluge::effects::EffectStates& from = effectsOf(source);
			expect(got.filter).to_equal(from.filter, "the clone's filter");
			expect(got.saturation).to_equal(from.saturation, "the clone's saturation");
			expect(got.mod_fx).to_equal(from.mod_fx, "the clone's mod FX");
			expect(got.sidechain).to_equal(from.sidechain, "the clone's sidechain");
			// DelayState's printer belongs to the effects unit specs' driver, so its fields are judged one by one here.
			expect(got.delay.ping_pong).to_equal(from.delay.ping_pong, "the clone's delay ping-pong");
			expect(int(got.delay.type)).to_equal(int(from.delay.type), "the clone's delay type");
			expect(int(got.delay.sync_type)).to_equal(int(from.delay.sync_type), "the clone's delay sync type");
			expect(int(got.delay.sync_level)).to_equal(int(from.delay.sync_level), "the clone's delay sync level");
			expect(got.stutter).to_equal(from.stutter, "the clone's stutter");
			expect(got.compressor).to_equal(compressor, "the clone's own compressor");
			expect(int(clone->channel().view().modFXParam())).to_equal(int(ModFXParam::DEPTH), "the clone's mod FX view");
			expect(clone->effectsOwner()->knobs->loaded.size()).to_equal(static_cast<std::size_t>(1));
			source->effectsOwner()->knobs->loaded.clear();
			clone->effectsOwner()->knobs->loaded.clear();
			spec::make_current(nullptr);
		});
	});
});

CPPSPEC_SPEC(effects_owner_characterisation)
