/// @file
/// @brief The owner of a stack's params, typed: what each kind of owner is, and the effects it answers for.

#include "model/fx/effects_owner.h"
#include "model/fx/master_channel.h"
#include "model/instrument/kit.h"
#include "model/instrument/non_audio_instrument.h"
#include "model/lane_owner.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <variant>

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace model = deluge::model;

// clang-format off
describe lane_owner("a stack's typed owner", ${
	context("its effects", _ {
		it("are the synth's own for a synth", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			expect(model::effects_owner_of(model::owner_of(*f.instrument)).has_value()).to_be_true();
			spec::make_current(nullptr);
		});

		it("are the kit's own for a kit, and the drum's sound's for a sound drum", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto* drum = spec::add_drum<SoundDrum>(f);
			expect(model::effects_owner_of(model::owner_of(*f.instrument)).has_value()).to_be_true();
			expect(model::effects_owner_of(model::owner_of(*drum)).has_value()).to_be_true();
			spec::make_current(nullptr);
		});

		it("are the master channel's for the song, and the output's for an audio track", _ {
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			spec::make_current(f.song);
			expect(model::effects_owner_of(model::LaneOwner{&f.song->live.master_channel}).has_value()).to_be_true();
			expect(model::effects_owner_of(model::owner_of(*f.output)).has_value()).to_be_true();
			spec::make_current(nullptr);
		});

		it("are none for a MIDI track and for no owner", _ {
			spec::SongFixture f = spec::make_midi_song();
			spec::make_current(f.song);
			expect(model::effects_owner_of(model::owner_of(*f.instrument)).has_value()).to_be_false();
			expect(model::effects_owner_of(model::LaneOwner{}).has_value()).to_be_false();
			spec::make_current(nullptr);
		});
	});
});
// clang-format on

CPPSPEC_SPEC(lane_owner)
