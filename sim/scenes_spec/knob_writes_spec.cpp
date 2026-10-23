// A param write by address that a gold knob makes: it makes the cable or the CC lane it turns where the owner has none,
// writes an arranger row's backed-up params where the row has no clip, writes the take while the arrangement records
// (and a read follows the take without making one), and marks the instrument edited.

#include "model/edits/set_param_value.h"

#include "engines/deluge/cables.h"
#include "engines/deluge/params.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
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
#include "scenes/menu/bind_host.h"
#include "storage/storage_manager.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

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
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		const std::optional<deluge::engine::ParamId> depth = deluge::instrument::cable_depth_id(
		    deluge::instrument::PatchSource::LFO_GLOBAL_2, deluge::instrument::param::kLpfFrequency);
		expect(depth.has_value()).to_be_true();
		if (depth) {
			const ParamAddress cable{.owner = OwnerId::instrument(*f.instrument->id()), .space = ParamSpace::ENGINE,
			                         .id = *depth, .clip_id = f.clip->id()};
			expect(value_at(*f.song, cable).has_value()).to_be_false();
			expect(turn(*f.song, cable, 1 << 28)).to_be_true();
			expect(value_at(*f.song, cable) == std::optional<int32_t>{1 << 28}).to_be_true();
			ParamDescriptor destination;
			PatchSource source{};
			PatchCableSet::dissectParamId(deluge::bridge::cableWordOf(*depth), &destination, &source);
			PatchCableSet* set = f.clip->paramManager.getPatchCableSetAllowJibberish();
			const uint8_t index = set != nullptr ? set->getPatchCableIndex(source, destination, nullptr, false) : 255;
			expect(index != 255).to_be_true();
			if (index != 255) {
				expect(set->patchCables[index].polarity == PatchCable::getDefaultPolarity(source)).to_be_true();
			}
		}
		currentSong = before;
	});

	it("makes a CC lane the MIDI clip lacks", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_midi_song();
		Song* const before = currentSong;
		currentSong = f.song;
		const ParamAddress cc{.owner = OwnerId::clip(f.clip->id()), .space = ParamSpace::MIDI_CC, .id = {20}};
		expect(value_at(*f.song, cc).has_value()).to_be_false();
		expect(turn(*f.song, cc, 1 << 29)).to_be_true();
		expect(value_at(*f.song, cc) == std::optional<int32_t>{1 << 29}).to_be_true();
		currentSong = before;
	});

	it("writes an arranger row's backed-up params where the row has no clip", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		ParamManagerForTimeline params;
		auto* row = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		row->setupAsDefaultSynth(&params);
		f.song->live.param_backup.backUp(*row, nullptr, &params, true);
		f.song->addOutput(row);
		expect(row->id().has_value()).to_be_true();
		const deluge::engine::ParamId lpf = deluge::instrument::param::kLpfFrequency;
		const ParamAddress address{.owner = OwnerId::instrument(*row->id()), .space = ParamSpace::ENGINE, .id = lpf};
		expect(turn(*f.song, address, 1 << 27)).to_be_true();
		ParamManager* backed = f.song->live.param_backup.getPreferablyWithClip(*row, nullptr);
		expect(backed != nullptr).to_be_true();
		if (backed != nullptr) {
			const std::optional<int32_t> index = deluge::bridge::runtimeIndexOf(deluge::modulation::params::Kind::PATCHED, lpf);
			expect(index.has_value() && backed->getPatchedParamSet()->getValue(*index) == (1 << 27)).to_be_true();
		}
		currentSong = before;
	});

	it("writes the take while the arrangement records, and a read follows the take", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		InstrumentClip* take = deluge::scenes::spec::detail::make_session_clip(f.song);
		ParamManagerForTimeline params;
		expect(params.cloneParamCollectionsFrom(&f.clip->paramManager, false) == Error::NONE).to_be_true();
		take->setAudioInstrument(synth, f.song, true, &params);
		take->beingRecordedFromClip = f.clip;
		alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
		expect(synth->setActiveClip(setupModelStackWithSong(memory, f.song)->addTimelineCounter(take), PgmChangeSend::NEVER))
		    .to_be_true();
		f.clip->activeIfNoSolo = true;
		const deluge::engine::ParamId lpf = deluge::instrument::param::kLpfFrequency;
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
		const std::optional<int32_t> read = deluge::scenes::menu::host_runtime_facts().param_value(on(*f.clip));
		playbackHandler.recording = mode;
		playbackHandler.playbackState = state;

		expect(value_at(*f.song, on(*take)) == std::optional<int32_t>{1 << 26}).to_be_true();
		expect(value_at(*f.song, on(*f.clip)) == session_before).to_be_true();
		expect(read == std::optional<int32_t>{1 << 26}).to_be_true();
		expect(f.song->live.clips.arrangement_only_clips.size()).to_equal(arranged_before);
		currentSong = before;
	});

	it("marks the instrument edited", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		f.instrument->editedByUser = false;
		const ParamAddress address{.owner = OwnerId::instrument(*f.instrument->id()), .space = ParamSpace::ENGINE,
		                           .id = deluge::instrument::param::kLpfFrequency, .clip_id = f.clip->id()};
		expect(turn(*f.song, address, 1 << 25)).to_be_true();
		expect(f.instrument->editedByUser).to_be_true();
		currentSong = before;
	});
});
// clang-format on

CPPSPEC_SPEC(knob_writes)
