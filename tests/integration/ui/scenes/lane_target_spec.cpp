/// @file
/// @brief A lane's resolution as one flat value: its owner typed, and the engine stack it stands for.

#include "model/clip/instrument_clip.h"
#include "model/drum/midi_drum.h"
#include "model/fx/master_channel.h"
#include "model/instrument/kit.h"
#include "model/instrument/non_audio_instrument.h"
#include "model/model_stack.h"
#include "model/project_bridge/lane_target.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <variant>

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;
namespace project = deluge::project;
namespace bridge = deluge::bridge;

namespace {

/// @brief Whether @p a and @p b name the same engine lane, field by field.
bool same_stack(const ModelStackWithAutoParam& a, const ModelStackWithAutoParam& b) {
	return a.song == b.song && a.getTimelineCounterAllowNull() == b.getTimelineCounterAllowNull()
	       && a.noteRowId == b.noteRowId && a.getNoteRowAllowNull() == b.getNoteRowAllowNull()
	       && a.modControllable == b.modControllable && a.paramManager == b.paramManager
	       && a.paramCollection == b.paramCollection && a.summary == b.summary && a.kind == b.kind
	       && a.paramId == b.paramId && a.autoParam == b.autoParam;
}

} // namespace

// clang-format off
describe lane_target("a lane target", ${
	context("its owner", _ {
		it("is the sound for a synth, matching the output's own owner", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			const bridge::LaneOwner owner = bridge::owner_of(*f.instrument);
			expect(std::holds_alternative<Sound*>(owner)).to_be_true();
			expect(bridge::mod_controllable_of(owner)).to_equal(f.instrument->toModControllable());
			spec::make_current(nullptr);
		});

		it("is the kit for a kit, and the sound for a sound drum", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto* drum = spec::add_drum<SoundDrum>(f);
			expect(std::holds_alternative<Kit*>(bridge::owner_of(*f.instrument))).to_be_true();
			expect(bridge::mod_controllable_of(bridge::owner_of(*f.instrument))).to_equal(f.instrument->toModControllable());
			expect(std::holds_alternative<Sound*>(bridge::owner_of(*drum))).to_be_true();
			expect(bridge::mod_controllable_of(bridge::owner_of(*drum))).to_equal(drum->toModControllable());
			spec::make_current(nullptr);
		});

		it("is the non-audio drum for a MIDI drum", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto* drum = spec::add_drum<MIDIDrum>(f);
			expect(std::holds_alternative<NonAudioDrum*>(bridge::owner_of(*drum))).to_be_true();
			expect(bridge::mod_controllable_of(bridge::owner_of(*drum))).to_equal(drum->toModControllable());
			spec::make_current(nullptr);
		});

		it("is the non-audio instrument for MIDI and CV tracks, and the output for an audio track", _ {
			spec::SongFixture midi = spec::make_midi_song();
			spec::make_current(midi.song);
			expect(std::holds_alternative<NonAudioInstrument*>(bridge::owner_of(*midi.instrument))).to_be_true();
			spec::make_current(nullptr);
			spec::SongFixture cv = spec::make_cv_song();
			spec::make_current(cv.song);
			expect(std::holds_alternative<NonAudioInstrument*>(bridge::owner_of(*cv.instrument))).to_be_true();
			spec::make_current(nullptr);
			spec::AudioSongFixture audio = spec::make_audio_clip_song();
			spec::make_current(audio.song);
			expect(std::holds_alternative<AudioOutput*>(bridge::owner_of(*audio.output))).to_be_true();
			spec::make_current(nullptr);
		});
	});

	context("as a stack", _ {
		it("stands for the stack it was taken from", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			alignas(ModelStackWithAutoParam) char b[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* original =
			    setupModelStackWithThreeMainThingsButNoNoteRow(a, f.song, f.instrument->toModControllable(), f.clip,
			                                                   &f.clip->paramManager)
			        ->getPatchedAutoParamFromId(params::LOCAL_LPF_FREQ);
			const bridge::LaneTarget target =
			    bridge::target_of(*original, bridge::owner_of(*f.instrument), std::nullopt);
			expect(same_stack(*original, *bridge::stack_of(target, b))).to_be_true();
			spec::make_current(nullptr);
		});
	});
	context("resolved by address", _ {
		it("names what paramStackAt names, for a synth's param in its clip", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			const project::ParamAddress address = bridge::addressOf(
			    project::OwnerId::instrument(*f.instrument->id()), params::Kind::PATCHED, params::LOCAL_LPF_FREQ,
			    f.clip->id());
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			alignas(ModelStackWithAutoParam) char b[MODEL_STACK_MAX_SIZE];
			const std::optional<bridge::LaneTarget> lane = bridge::lane_at(*f.song, address);
			expect(lane.has_value()).to_be_true();
			if (lane) {
				ModelStackWithAutoParam* original = bridge::paramStackAt(*f.song, address, a);
				expect(std::holds_alternative<Sound*>(lane->owner)).to_be_true();
				expect(bridge::mod_controllable_of(lane->owner)).to_equal(original->modControllable);
				expect(same_stack(*original, *bridge::stack_of(*lane, b))).to_be_true();
			}
			spec::make_current(nullptr);
		});

		it("names the song's own param, owned by the master channel", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			const project::ParamAddress address =
			    bridge::addressOf(project::OwnerId::song(), params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME);
			const std::optional<bridge::LaneTarget> lane = bridge::lane_at(*f.song, address);
			expect(lane.has_value()).to_be_true();
			if (lane) {
				expect(std::holds_alternative<deluge::model::MasterChannel*>(lane->owner)).to_be_true();
				expect(lane->timeline).to_equal(static_cast<TimelineCounter*>(f.song));
			}
			spec::make_current(nullptr);
		});

		it("names a sound drum's param in its row, owned by the drum's sound", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto* drum = spec::add_drum<SoundDrum>(f);
			const project::ParamAddress address = bridge::addressOf(
			    project::OwnerId::drum(*drum->id()), params::Kind::PATCHED, params::LOCAL_LPF_FREQ, f.clip->id());
			alignas(ModelStackWithAutoParam) char a[MODEL_STACK_MAX_SIZE];
			alignas(ModelStackWithAutoParam) char b[MODEL_STACK_MAX_SIZE];
			const std::optional<bridge::LaneTarget> lane = bridge::lane_exact(*f.song, address);
			expect(lane.has_value()).to_be_true();
			if (lane) {
				expect(lane->note_row).not_().to_be_null();
				expect(std::get<Sound*>(lane->owner)).to_equal(static_cast<Sound*>(drum));
				expect(same_stack(*bridge::paramStackExact(*f.song, address, a), *bridge::stack_of(*lane, b)))
				    .to_be_true();
			}
			spec::make_current(nullptr);
		});

		it("names a MIDI clip's CC lane, made for a write, owned by the MIDI instrument", _ {
			spec::SongFixture f = spec::make_midi_song();
			spec::make_current(f.song);
			project::ParamAddress address{.owner = project::OwnerId::clip(f.clip->id()),
			                              .space = project::ParamSpace::MIDI_CC,
			                              .id = deluge::plugin::ParamId{74}};
			const std::optional<bridge::LaneTarget> lane = bridge::lane_for_write(*f.song, address);
			expect(lane.has_value()).to_be_true();
			if (lane) {
				expect(lane->auto_param).not_().to_be_null();
				expect(std::holds_alternative<NonAudioInstrument*>(lane->owner)).to_be_true();
			}
			spec::make_current(nullptr);
		});

		it("finds nothing for an owner the song does not hold", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			const project::ParamAddress address = bridge::addressOf(
			    project::OwnerId::instrument(0xFFFF'FFF0U), params::Kind::PATCHED, params::LOCAL_LPF_FREQ,
			    f.clip->id());
			expect(bridge::lane_at(*f.song, address).has_value()).to_be_false();
			spec::make_current(nullptr);
		});
	});
});
// clang-format on

CPPSPEC_SPEC(lane_target)
