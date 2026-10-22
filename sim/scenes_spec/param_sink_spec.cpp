// A param menu's writes go through the app's sink as their Edit, and its reads come from the app's facts: a write lands
// in the engine and the record on each owner a menu edits; with affect-entire held on a kit row it reaches every sound
// drum with a row; it sends MIDI-follow feedback as legacy's param items do; and the fact gives the value the param
// plays where automation moves it.

#include "scenes/bind/dispatch.h"
#include "scenes/bind/target.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"

#include "deluge-project/param_address.h"
#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"
#include "gui/views/view.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/edits/set_param_value.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <optional>

using namespace deluge::scenes;
using bind::Addresses;
using bind::ChainOwner;
using bind::Target;
using deluge::project::ParamAddress;

namespace {

constexpr Target kSoundLpf{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kLpfFrequency};
constexpr Target kChainLpf{.kind = Target::Kind::PARAM,
                           .param = deluge::global_effectable::param::kLpfFrequency,
                           .param_table = bind::ParamTable::CHAIN};

/// The value the engine's param at @p address plays.
std::optional<int32_t> playing(Song& song, const ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(song, address, memory);
	return stack != nullptr ? std::optional{stack->autoParam->getCurrentValue()} : std::nullopt;
}

/// Writes @p word through the app's sink at @p where; whether the engine and the record then hold it.
bool sink_lands(Song& song, const Target& target, const Addresses& where, int32_t word) {
	bind::WriteRequest request{.target = target, .where = where, .param_value = word};
	menu::host_write_sink().write(request);
	const std::optional<ParamAddress> address = bind::param_address(target, where);
	if (!address || playing(song, *address) != word) {
		return false;
	}
	const deluge::project::DocAutoParam* record = song.project.resolveParam(*address);
	return record == nullptr || record->current_value == word;
}

} // namespace

// clang-format off
describe param_sink("a param menu's writes and reads through the app", ${
	it("writes each owner's param through its Edit", _ {
		spec::SongFixture synth = spec::make_synth_song();
		expect(sink_lands(*synth.song, kSoundLpf,
		                  {.slot = deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::OUTPUT,
		                                                        *synth.instrument->id()},
		                   .clip = deluge::project::ClipAddress{synth.clip->id()}},
		                  0x11111111))
		    .to_be_true();

		spec::SongFixture kit = spec::make_kit_song();
		auto* drum = spec::add_drum<SoundDrum>(kit);
		const deluge::project::ClipAddress kitClip{kit.clip->id()};
		expect(sink_lands(*kit.song, kSoundLpf,
		                  {.slot = deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::DRUM, *drum->id()},
		                   .clip = kitClip},
		                  0x22222222))
		    .to_be_true();
		expect(sink_lands(*kit.song, kChainLpf,
		                  {.chain = deluge::project::ChainAddress{deluge::project::ChainAddress::Kind::OUTPUT,
		                                                          *kit.instrument->id()},
		                   .chain_owner = ChainOwner::KIT,
		                   .clip = kitClip},
		                  0x33333333))
		    .to_be_true();
		expect(sink_lands(*kit.song, kChainLpf,
		                  {.chain = deluge::project::ChainAddress{deluge::project::ChainAddress::Kind::SONG},
		                   .chain_owner = ChainOwner::SONG},
		                  0x44444444))
		    .to_be_true();

		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		expect(sink_lands(*audio.song, kChainLpf,
		                  {.chain = deluge::project::ChainAddress{deluge::project::ChainAddress::Kind::OUTPUT,
		                                                          *audio.output->id()},
		                   .chain_owner = ChainOwner::AUDIO_TRACK,
		                   .clip = deluge::project::ClipAddress{audio.clip->id()}},
		                  0x55555555))
		    .to_be_true();
	});

	it("writes every sound drum with a row under affect-entire, as one Edit", _ {
		spec::SongFixture kit = spec::make_kit_song();
		auto* first = spec::add_drum<SoundDrum>(kit);
		auto* second = spec::add_drum<SoundDrum>(kit);
		const Addresses where{.slot = deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::DRUM, *first->id()},
		                      .kit_output = *kit.instrument->id(),
		                      .affect_entire = true,
		                      .clip = deluge::project::ClipAddress{kit.clip->id()}};
		menu::host_write_sink().write({.target = kSoundLpf, .where = where, .param_value = 0x66666666});
		const auto at = [&](Drum& d) {
			return playing(*kit.song, ParamAddress{.owner = deluge::project::OwnerId::drum(*d.id()),
			                                       .space = deluge::project::ParamSpace::ENGINE,
			                                       .id = kSoundLpf.param,
			                                       .clip_id = kit.clip->id()});
		};
		expect(at(*first)).to_equal(std::optional{0x66666666});
		expect(at(*second)).to_equal(std::optional{0x66666666});
	});

	it("sends MIDI-follow feedback for the param it writes", _ {
		spec::SongFixture synth = spec::make_synth_song();
		synth.song->setCurrentClip(synth.clip);
		const auto saved_type = midiEngine.midiFollowFeedbackChannelType;
		const LearnedMIDI saved_a = midiEngine.midiFollowChannelType[0];
		midiEngine.midiFollowFeedbackChannelType = MIDIFollowFeedbackChannelType::A;
		midiEngine.midiFollowChannelType[0].channelOrZone = 0;
		const ModelStackWithThreeMainThings saved_active = view.activeModControllableModelStack;
		view.activeModControllableModelStack.song = synth.song;
		view.activeModControllableModelStack.setTimelineCounter(synth.clip);
		view.activeModControllableModelStack.modControllable = static_cast<SoundInstrument*>(synth.instrument);
		const int32_t cc = midiFollow.getCCFromParam(deluge::modulation::params::Kind::PATCHED,
		                                             deluge::modulation::params::LOCAL_LPF_FREQ);
		const uint32_t timer = AudioEngine::audioSampleTimer;
		AudioEngine::audioSampleTimer = 987654;
		if (cc != MIDI_CC_NONE) {
			midiFollow.timeLastCCSent[cc] = 0;
		}
		menu::host_write_sink().write(
		    {.target = kSoundLpf,
		     .where = {.slot = deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::OUTPUT,
		                                                    *synth.instrument->id()},
		               .clip = deluge::project::ClipAddress{synth.clip->id()}},
		     .param_value = 0x12345678});
		const uint32_t sent = cc != MIDI_CC_NONE ? midiFollow.timeLastCCSent[cc] : 0;
		AudioEngine::audioSampleTimer = timer;
		view.activeModControllableModelStack = saved_active;
		midiEngine.midiFollowFeedbackChannelType = saved_type;
		midiEngine.midiFollowChannelType[0] = saved_a;
		expect(cc != MIDI_CC_NONE).to_be_true();
		expect(sent).to_equal(987654u);
	});

	it("reads the value the param plays where automation moves it", _ {
		spec::SongFixture synth = spec::make_synth_song();
		const ParamAddress lpf{.owner = deluge::project::OwnerId::instrument(*synth.instrument->id()),
		                       .space = deluge::project::ParamSpace::ENGINE,
		                       .id = kSoundLpf.param,
		                       .clip_id = synth.clip->id()};
		deluge::edits::Context ctx{*synth.song, synth.song->project};
		deluge::edits::SetParamValue early{lpf, 0x10000000, 0, 24};
		deluge::edits::SetParamValue late{lpf, 0x70000000, 48, 24};
		(void)deluge::edits::apply(ctx, early);
		(void)deluge::edits::apply(ctx, late);
		const bind::RuntimeFacts& facts = menu::host_runtime_facts();
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		std::optional<int32_t> read[2];
		int i = 0;
		for (const uint32_t pos : {4u, 52u}) {
			ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(*synth.song, lpf, memory);
			stack->autoParam->setPlayPos(pos, stack, false);
			read[i] = facts.param_value(lpf);
			expect(read[i]).to_equal(std::optional{stack->autoParam->getValueAtPos(pos, stack)});
			++i;
		}
		expect(read[0] != read[1]).to_be_true(); // the playhead moved the value
		const ParamAddress nowhere{.owner = deluge::project::OwnerId::instrument(*synth.instrument->id() + 1000),
		                           .space = deluge::project::ParamSpace::ENGINE,
		                           .id = kSoundLpf.param,
		                           .clip_id = synth.clip->id()};
		expect(facts.param_value(nowhere).has_value()).to_be_false();
	});
});

CPPSPEC_SPEC(param_sink)
