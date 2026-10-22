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
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/edits/set_param_value.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <functional>
#include <optional>
#include <vector>

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

Addresses on_synth(const spec::SongFixture& synth) {
	return {.slot = deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()},
	        .clip = deluge::project::ClipAddress{synth.clip->id()}};
}

ParamAddress synth_lpf(const spec::SongFixture& synth) {
	return {.owner = deluge::project::OwnerId::instrument(*synth.instrument->id()),
	        .space = deluge::project::ParamSpace::ENGINE,
	        .id = kSoundLpf.param,
	        .clip_id = synth.clip->id()};
}

/// The audio sample time MIDI follow stamps on the LPF frequency's CC while @p write runs on @p synth's active
/// context, as legacy stamps it on every feedback send; nullopt where no CC carries the param.
std::optional<uint32_t> feedback_stamp(spec::SongFixture& synth, const std::function<void()>& write) {
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
	write();
	const uint32_t sent = cc != MIDI_CC_NONE ? midiFollow.timeLastCCSent[cc] : 0;
	AudioEngine::audioSampleTimer = timer;
	view.activeModControllableModelStack = saved_active;
	midiEngine.midiFollowFeedbackChannelType = saved_type;
	midiEngine.midiFollowChannelType[0] = saved_a;
	return cc != MIDI_CC_NONE ? std::optional{sent} : std::nullopt;
}

/// @p synth's cable from @p source to @p destination as a CABLE target.
Target cable_target(PatchSource source, int32_t destination) {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(destination);
	return {.kind = Target::Kind::CABLE, .param = deluge::bridge::cable_depth(source, descriptor)};
}

/// The engine's cable from @p source to @p destination on @p synth, or null.
PatchCable* engine_cable(const spec::SongFixture& synth, PatchSource source, int32_t destination) {
	PatchCableSet& set = *synth.clip->paramManager.getPatchCableSet();
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(destination);
	const uint8_t index = set.getPatchCableIndex(source, descriptor, nullptr, false);
	return index == 255 ? nullptr : &set.patchCables[index];
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
		const std::optional<uint32_t> sent = feedback_stamp(synth, [&] {
			menu::host_write_sink().write({.target = kSoundLpf, .where = on_synth(synth), .param_value = 0x12345678});
		});
		expect(sent).to_equal(std::optional{987654u});
	});

	it("creates and sets a cable through a CABLE request", _ {
		spec::SongFixture synth = spec::make_synth_song();
		const Target cable = cable_target(PatchSource::ENVELOPE_1, deluge::modulation::params::LOCAL_LPF_RESONANCE);
		expect(menu::host_write_sink().write({.target = cable,
		                                      .where = on_synth(synth),
		                                      .param_value = 0x12345678,
		                                      .polarity = deluge::instrument::Polarity::BIPOLAR}))
		    .to_be_true();
		PatchCable* made = engine_cable(synth, PatchSource::ENVELOPE_1, deluge::modulation::params::LOCAL_LPF_RESONANCE);
		expect(made != nullptr && made->polarity == Polarity::BIPOLAR && made->param.getCurrentValue() == 0x12345678)
		    .to_be_true();
	});

	it("flips an existing cable's polarity through a polarity-only CABLE request, leaving its strength", _ {
		spec::SongFixture synth = spec::make_synth_song();
		const Target cable = cable_target(PatchSource::ENVELOPE_1, deluge::modulation::params::LOCAL_LPF_RESONANCE);
		(void)menu::host_write_sink().write({.target = cable,
		                                     .where = on_synth(synth),
		                                     .param_value = 0x12345678,
		                                     .polarity = deluge::instrument::Polarity::BIPOLAR});
		expect(menu::host_write_sink().write({.target = cable,
		                                      .where = on_synth(synth),
		                                      .polarity = deluge::instrument::Polarity::UNIPOLAR,
		                                      .polarity_only = true}))
		    .to_be_true();
		PatchCable* existing =
		    engine_cable(synth, PatchSource::ENVELOPE_1, deluge::modulation::params::LOCAL_LPF_RESONANCE);
		expect(existing != nullptr && existing->polarity == Polarity::UNIPOLAR
		       && existing->param.getCurrentValue() == 0x12345678)
		    .to_be_true();
	});

	it("writes a param by address over a region without sending MIDI-follow feedback", _ {
		spec::SongFixture synth = spec::make_synth_song();
		const ParamAddress lpf = synth_lpf(synth);
		bool taken = false;
		const std::optional<uint32_t> sent =
		    feedback_stamp(synth, [&] { taken = menu::host_write_sink().write_param(lpf, 0x10000000, 0, 24); });
		expect(taken).to_be_true();
		expect(sent).to_equal(std::optional{0u});
		const deluge::project::DocAutoParam* record = synth.song->project.resolveParam(lpf);
		expect(record != nullptr && !record->nodes.empty()).to_be_true();
	});

	it("deletes a param's automation by address, one undo step", _ {
		spec::SongFixture synth = spec::make_synth_song();
		const ParamAddress lpf = synth_lpf(synth);
		expect(menu::host_write_sink().write_param(lpf, 0x10000000, 0, 24)).to_be_true();
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		actionLogger.deleteAllLogs();
		expect(menu::host_write_sink().delete_automation(lpf)).to_be_true();
		expect(synth.song->project.resolveParam(lpf)->nodes.empty()).to_be_true();
		Action* action = actionLogger.firstAction[BEFORE];
		expect(action != nullptr && action->nextAction == nullptr).to_be_true();
		expect(actionLogger.revert(BEFORE, false, false)).to_be_true();
		expect(synth.song->project.resolveParam(lpf)->nodes.empty()).to_be_false();
		actionLogger.deleteAllLogs();
		nullifyUIs();
	});

	it("refuses each request on an owner that does not resolve, and says so", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Addresses nowhere = on_synth(synth);
		nowhere.slot->id += 1000;
		ParamAddress lpf = synth_lpf(synth);
		lpf.owner.id += 1000;
		const Target cable = cable_target(PatchSource::ENVELOPE_1, deluge::modulation::params::LOCAL_LPF_RESONANCE);
		expect(menu::host_write_sink().write({.target = cable, .where = nowhere, .param_value = 1})).to_be_false();
		expect(menu::host_write_sink().write_param(lpf, 0x10000000, 0, 0)).to_be_false();
		expect(menu::host_write_sink().delete_automation(lpf)).to_be_false();
	});

	it("keeps one sound's cables when another's are read", _ {
		spec::SongFixture a = spec::make_synth_song();
		spec::SongFixture b = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = a.song;
		(void)menu::host_write_sink().write({.target = cable_target(PatchSource::ENVELOPE_1,
		                                                            deluge::modulation::params::LOCAL_LPF_RESONANCE),
		                                     .where = on_synth(a),
		                                     .param_value = 0x01000000});
		const bind::RuntimeFacts& facts = menu::host_runtime_facts();
		const auto first = facts.cables(synth_lpf(a));
		std::vector<deluge::engine::ParamId> held;
		for (const bind::CableFact& cable : first) {
			held.push_back(cable.depth);
		}
		currentSong = b.song;
		ParamDescriptor other;
		other.setToHaveParamOnly(deluge::modulation::params::LOCAL_HPF_RESONANCE);
		(void)menu::host_write_sink().write({.target = cable_target(PatchSource::RANDOM,
		                                                            deluge::modulation::params::LOCAL_HPF_RESONANCE),
		                                     .where = on_synth(b),
		                                     .param_value = 0x01000000});
		(void)facts.cables(synth_lpf(b));
		currentSong = saved;
		expect(held.empty()).to_be_false();
		expect(first.size()).to_equal(held.size());
		bool same = first.size() == held.size();
		for (size_t i = 0; same && i < held.size(); ++i) {
			same = first[i].depth == held[i];
		}
		expect(same).to_be_true();
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
