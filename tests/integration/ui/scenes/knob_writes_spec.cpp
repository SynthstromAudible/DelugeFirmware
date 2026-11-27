// A param write by address that a gold knob makes: it makes the cable or the CC lane it turns where the owner has none,
// writes an arranger row's backed-up params where the row has no clip, writes the take while the arrangement records
// (and a read follows the take without making one), and marks the instrument edited.

#include "model/edits/set_param_value.h"

#include "gui/ui/shell_host/host_ports.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/fx/song_stutter.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include "ui/scenes/bind/knob_focus.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include "support/spec_main.h"

#include <optional>

namespace {

using deluge::edits::SetParamValue;
using deluge::project::OwnerId;
using deluge::project::ParamAddress;
using deluge::project::ParamSpace;

/// @brief Apply a knob's write of @p value at @p address on @p song.
bool turn(::Song& song, const ParamAddress& address, int32_t value) {
	deluge::edits::Context ctx{song, song.project};
	SetParamValue write{address, value, 0, 0, SetParamValue::Resolve::WRITE};
	return deluge::edits::apply(ctx, write) == Error::NONE;
}

/// @brief The current value at @p address, where it resolves without making anything.
std::optional<int32_t> value_at(::Song& song, const ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(song, address, memory);
	return stack != nullptr ? std::optional{stack->autoParam->getCurrentValue()} : std::nullopt;
}

} // namespace

// clang-format off
describe knob_writes("a gold knob's write by address", ${
	it("makes a cable the sound lacks, with its source's default polarity", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		const std::optional<deluge::plugin::ParamId> depth = deluge::instruments::classic::cable_depth_id(
		    deluge::instruments::classic::PatchSource::LFO_GLOBAL_2, deluge::instruments::classic::param::kLpfFrequency);
		expect(depth.has_value()).to_be_true();
		if (depth) {
			const ParamAddress cable{.owner = OwnerId::instrument(*f.instrument->id()), .space = ParamSpace::ENGINE,
			                         .id = *depth, .clip_id = f.clip->id()};
			expect(value_at(*f.song, cable).has_value()).to_be_false();
			expect(turn(*f.song, cable, 1 << 28)).to_be_true();
			expect(value_at(*f.song, cable)).to_equal(std::optional<int32_t>{1 << 28});
			deluge::synth::CableTarget destination;
			PatchSource source{};
			PatchCableSet::dissectParamId(deluge::bridge::cableWordOf(*depth), &destination, &source);
			PatchCableSet* set = f.clip->paramManager.getPatchCableSetAllowJibberish();
			const uint8_t index = set != nullptr ? set->getPatchCableIndex(source, destination) : 255;
			expect(index).not_().to_equal(255);
			if (index != 255) {
				expect(set->patchCables[index].polarity).to_equal(PatchCable::getDefaultPolarity(source));
			}
		}
		currentSong = before;
	});

	it("makes a CC lane the MIDI clip lacks", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_midi_song();
		Song* const before = currentSong;
		currentSong = f.song;
		const ParamAddress cc{.owner = OwnerId::clip(f.clip->id()), .space = ParamSpace::MIDI_CC, .id = {20}};
		expect(value_at(*f.song, cc).has_value()).to_be_false();
		expect(turn(*f.song, cc, 1 << 29)).to_be_true();
		expect(value_at(*f.song, cc)).to_equal(std::optional<int32_t>{1 << 29});
		currentSong = before;
	});

	it("writes an arranger row's backed-up params where the row has no clip", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		ParamManagerForTimeline params;
		auto* row = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		row->setupAsDefaultSynth(&params);
		f.song->live.param_backup.backUp(*row, nullptr, &params, true);
		f.song->addOutput(row);
		expect(row->id().has_value()).to_be_true();
		const deluge::plugin::ParamId lpf = deluge::instruments::classic::param::kLpfFrequency;
		const ParamAddress address{.owner = OwnerId::instrument(*row->id()), .space = ParamSpace::ENGINE, .id = lpf};
		expect(turn(*f.song, address, 1 << 27)).to_be_true();
		ParamManager* backed = f.song->live.param_backup.getPreferablyWithClip(*row, nullptr);
		expect(backed).not_().to_be_null();
		if (backed != nullptr) {
			const std::optional<int32_t> index = deluge::bridge::runtimeIndexOf(deluge::modulation::params::Kind::PATCHED, lpf);
			expect(index.has_value()).to_be_true();
			if (index.has_value()) {
				expect(backed->getPatchedParamSet()->getValue(*index)).to_equal(1 << 27);
			}
		}
		currentSong = before;
	});

	it("writes the take while the arrangement records, and a read follows the take", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		InstrumentClip* take = deluge::ui::scenes::spec::detail::make_session_clip(f.song);
		ParamManagerForTimeline params;
		expect(params.cloneParamCollectionsFrom(&f.clip->paramManager, false)).to_equal(Error::NONE);
		take->setAudioInstrument(synth, f.song, true, &params);
		take->beingRecordedFromClip = f.clip;
		alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
		expect(synth->setActiveClip(setupModelStackWithSong(memory, f.song)->addTimelineCounter(take), PgmChangeSend::NEVER))
		    .to_be_true();
		f.clip->activeIfNoSolo = true;
		const deluge::plugin::ParamId lpf = deluge::instruments::classic::param::kLpfFrequency;
		const auto on = [&](const Clip& clip) {
			return ParamAddress{.owner = OwnerId::instrument(*synth->id()), .space = ParamSpace::ENGINE, .id = lpf,
			                    .clip_id = clip.id()};
		};
		const std::optional<int32_t> session_before = value_at(*f.song, on(*f.clip));
		const size_t arranged_before = f.song->live.clips.arrangement_only_clips.size();

		const auto mode = playbackHandler.recording;
		const auto state = playbackHandler.playbackState;
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		expect(turn(*f.song, on(*f.clip), 1 << 26)).to_be_true();
		const std::optional<int32_t> read = deluge::gui::shell_host::host_ports().controls->param_value(on(*f.clip));
		playbackHandler.recording = mode;
		playbackHandler.playbackState = state;

		expect(value_at(*f.song, on(*take))).to_equal(std::optional<int32_t>{1 << 26});
		expect(value_at(*f.song, on(*f.clip))).to_equal(session_before);
		expect(read).to_equal(std::optional<int32_t>{1 << 26});
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged_before);
		currentSong = before;
	});

	it("reads an arranger row's backed-up params where the row has no clip", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		ParamManagerForTimeline params;
		auto* row = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		row->setupAsDefaultSynth(&params);
		f.song->live.param_backup.backUp(*row, nullptr, &params, true);
		f.song->addOutput(row);
		const ParamAddress address{.owner = OwnerId::instrument(*row->id()), .space = ParamSpace::ENGINE,
		                           .id = deluge::instruments::classic::param::kLpfFrequency};
		expect(turn(*f.song, address, 1 << 27)).to_be_true();
		expect(deluge::gui::shell_host::host_ports().controls->param_value(address))
		    .to_equal(std::optional<int32_t>{1 << 27});
		currentSong = before;
	});

	it("deletes, reads at a position and stutters the take while the arrangement records, the session clip left", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		InstrumentClip* take = deluge::ui::scenes::spec::detail::make_session_clip(f.song);
		ParamManagerForTimeline params;
		expect(params.cloneParamCollectionsFrom(&f.clip->paramManager, false)).to_equal(Error::NONE);
		take->setAudioInstrument(synth, f.song, true, &params);
		take->beingRecordedFromClip = f.clip;
		alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
		expect(synth->setActiveClip(setupModelStackWithSong(memory, f.song)->addTimelineCounter(take), PgmChangeSend::NEVER))
		    .to_be_true();
		f.clip->activeIfNoSolo = true;
		const auto on = [&](const Clip& clip, deluge::plugin::ParamId id) {
			return ParamAddress{.owner = OwnerId::instrument(*synth->id()), .space = ParamSpace::ENGINE, .id = id,
			                    .clip_id = clip.id()};
		};
		const deluge::plugin::ParamId lpf = deluge::instruments::classic::param::kLpfFrequency;
		// Each clip holds its own automation of the filter, and its own stutter rate.
		const auto region = [&](const ParamAddress& address, int32_t value) {
			deluge::edits::Context ctx{*f.song, f.song->project};
			SetParamValue write{address, value, 0, 48};
			return deluge::edits::apply(ctx, write) == Error::NONE;
		};
		expect(region(on(*f.clip, lpf), 1 << 24)).to_be_true();
		expect(region(on(*take, lpf), 1 << 28)).to_be_true();
		expect(turn(*f.song, on(*f.clip, deluge::instruments::classic::param::kStutterRate), -(1 << 29))).to_be_true();
		expect(turn(*f.song, on(*take, deluge::instruments::classic::param::kStutterRate), 1 << 29)).to_be_true();
		const auto automated = [&](const ParamAddress& address) {
			const deluge::project::DocAutoParam* param = f.song->project.resolveParam(address);
			return param != nullptr && !param->nodes.empty();
		};

		const auto mode = playbackHandler.recording;
		const auto state = playbackHandler.playbackState;
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		const std::optional<int32_t> at_pos =
		    deluge::gui::shell_host::host_ports().controls->param_value_at(on(*f.clip, lpf), 0);
		const deluge::ui::scenes::bind::KnobFocus focus{
		    .owner = deluge::ui::scenes::bind::SoundFocus{.slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth->id()},
		                                              .clip = deluge::project::ClipAddress{f.clip->id()}}};
		(void)deluge::gui::shell_host::host_ports().controls_sink->stutter(focus, true);
		const int32_t before_stutter = f.song->live.stutter.valueBeforeStuttering;
		(void)deluge::gui::shell_host::host_ports().controls_sink->stutter(focus, false);
		const bool deleted = deluge::gui::shell_host::host_ports().controls_sink->delete_knob_automation(on(*f.clip, lpf));
		playbackHandler.recording = mode;
		playbackHandler.playbackState = state;

		expect(at_pos).to_equal(std::optional<int32_t>{1 << 28}); // the take's automation
		expect(before_stutter).to_equal(1 << 29);                       // the take's stutter rate
		expect(deleted).to_be_true();
		expect(automated(on(*take, lpf))).to_be_false();
		expect(automated(on(*f.clip, lpf))).to_be_true();
		currentSong = before;
	});

	it("makes the take once while the arrangement records, and a read makes none", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		f.clip->activeIfNoSolo = true;
		// The arrangement's instance of the session clip, where the recording is.
		const int32_t at = synth->clipInstances.insertSorted(0).value_or(-1);
		to_be_at_least(expect(at), 0);
		if (at >= 0) {
			synth->clipInstances[at].clip = f.clip;
			synth->clipInstances[at].length = f.clip->loopLength();
		}
		const ParamAddress session{.owner = OwnerId::instrument(*synth->id()), .space = ParamSpace::ENGINE,
		                           .id = deluge::instruments::classic::param::kLpfFrequency, .clip_id = f.clip->id()};
		const std::optional<int32_t> session_before = value_at(*f.song, session);
		const size_t arranged = f.song->live.clips.arrangement_only_clips.size();

		const auto mode = playbackHandler.recording;
		const auto state = playbackHandler.playbackState;
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		(void)deluge::gui::shell_host::host_ports().controls->param_value(session);
		const size_t after_read = f.song->live.clips.arrangement_only_clips.size();
		expect(turn(*f.song, session, 1 << 26)).to_be_true();
		const size_t after_turn = f.song->live.clips.arrangement_only_clips.size();
		expect(turn(*f.song, session, 1 << 25)).to_be_true();
		const size_t after_second = f.song->live.clips.arrangement_only_clips.size();
		Clip* const recorded = synth->getActiveClip();
		playbackHandler.recording = mode;
		playbackHandler.playbackState = state;

		expect(after_read).to_equal(arranged);
		expect(after_turn).to_equal(arranged + 1);
		expect(after_second).to_equal(arranged + 1);
		expect(recorded).not_().to_be_null();
		if (recorded != nullptr) {
			expect(recorded).not_().to_equal(f.clip);
			expect(recorded->beingRecordedFromClip).to_equal(f.clip);
		}
		if (recorded != nullptr && recorded != f.clip) {
			ParamAddress in_take = session;
			in_take.clip_id = recorded->id();
			expect(value_at(*f.song, in_take)).to_equal(std::optional<int32_t>{1 << 25});
		}
		expect(value_at(*f.song, session)).to_equal(session_before);
		currentSong = before;
	});

	it("marks the instrument edited", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		f.instrument->editedByUser = false;
		const ParamAddress address{.owner = OwnerId::instrument(*f.instrument->id()), .space = ParamSpace::ENGINE,
		                           .id = deluge::instruments::classic::param::kLpfFrequency, .clip_id = f.clip->id()};
		expect(turn(*f.song, address, 1 << 25)).to_be_true();
		expect(f.instrument->editedByUser).to_be_true();
		currentSong = before;
	});
});
// clang-format on

CPPSPEC_SPEC(knob_writes)
