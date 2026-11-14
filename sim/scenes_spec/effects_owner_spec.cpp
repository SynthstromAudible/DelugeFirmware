/// @file
/// @brief `deluge::model::EffectsOwner` and the functions over it: what each owner kind's `effectsOwner()` names, and
///        how a store, a bind and a projection reach the settings and the render's copy.

#include "effects/state.h"
#include "effects/words.h"
#include "model/fx/effects_owner.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_instrument.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace spec = deluge::scenes::spec;
namespace params = deluge::modulation::params;
using deluge::model::EffectsOwner;

// clang-format off
describe effects_owner("an owner's EffectsOwner", ${
	context("as each owner kind names it", _ {
		it("names a synth's chain and binding, and the sound, with no view", _ {
			spec::SongFixture f = spec::make_synth_song();
			auto& synth = *static_cast<SoundInstrument*>(f.instrument);
			const std::optional<EffectsOwner> owner = synth.effectsOwner();
			expect(owner.has_value()).to_be_true();
			expect(owner->core == &synth.core()).to_be_true();
			expect(owner->sound == static_cast<Sound*>(&synth)).to_be_true();
			expect(owner->view == nullptr).to_be_true();
			expect(owner->unpatchedKind() == params::Kind::UNPATCHED_SOUND).to_be_true();
		});

		it("names a kit's and an audio track's channel, with its view", _ {
			spec::SongFixture kit = spec::make_kit_song();
			auto& k = *static_cast<Kit*>(kit.instrument);
			const std::optional<EffectsOwner> ofKit = k.effectsOwner();
			expect(ofKit->core == &k.channel().core()).to_be_true();
			expect(ofKit->binding == &k.channel().binding()).to_be_true();
			expect(ofKit->view == &k.channel().view()).to_be_true();
			expect(ofKit->sound == nullptr).to_be_true();
			expect(ofKit->unpatchedKind() == params::Kind::UNPATCHED_GLOBAL).to_be_true();

			spec::AudioSongFixture audio = spec::make_audio_clip_song();
			const std::optional<EffectsOwner> ofAudio = audio.output->effectsOwner();
			expect(ofAudio->core == &audio.output->channel().core()).to_be_true();
			expect(ofAudio->view == &audio.output->channel().view()).to_be_true();
		});

		it("names the song's master channel through the song's owner slot", _ {
			spec::SongFixture f = spec::make_synth_song();
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* stack = f.song->setupModelStackWithSongAsTimelineCounter(memory);
			const std::optional<EffectsOwner> owner = stack->modControllable->effectsOwner();
			expect(owner->core == &f.song->live.master_channel.core()).to_be_true();
			expect(owner->binding == &f.song->live.master_channel.binding()).to_be_true();
			expect(owner->view == &f.song->live.master_channel.view()).to_be_true();
		});

		it("names nothing for a MIDI instrument", _ {
			spec::SongFixture f = spec::make_midi_song();
			expect(static_cast<MIDIInstrument*>(f.instrument)->effectsOwner().has_value()).to_be_false();
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
			expect(owner.effects() == next).to_be_true();
			expect(owner.binding->snapshot().effects == next).to_be_true();
		});

		it("projects a host chain's selected mod FX knob", _ {
			spec::SongFixture f = spec::make_kit_song();
			auto& kit = *static_cast<Kit*>(f.instrument);
			const EffectsOwner owner = *kit.effectsOwner();
			kit.channel().view().write(ModFXParam::DEPTH, kit.channel().view().filterType());
			deluge::model::projectEffects(owner);
			expect(owner.binding->snapshot().mod_fx_param == ModFXParam::DEPTH).to_be_true();
		});

		it("keeps the settings in a bound record, which takes those held, until unbound", _ {
			spec::SongFixture f = spec::make_synth_song();
			const EffectsOwner owner = *static_cast<SoundInstrument*>(f.instrument)->effectsOwner();
			deluge::effects::EffectStates held = owner.effects();
			held.delay.ping_pong = !held.delay.ping_pong;
			deluge::model::storeEffects(owner, held);

			deluge::effects::EffectStates record{};
			deluge::model::bindEffects(owner, &record);
			expect(record == held).to_be_true();
			deluge::effects::EffectStates next = held;
			next.stutter.quantized = !next.stutter.quantized;
			deluge::model::storeEffects(owner, next);
			expect(record == next).to_be_true();

			deluge::model::bindEffects(owner, nullptr);
			expect(owner.effects() == next).to_be_true();
		});
	});
});

CPPSPEC_SPEC(effects_owner)
