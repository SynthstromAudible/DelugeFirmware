/// @file
/// @brief The owner of a stack's params, typed: what each kind of owner is, and the effects it answers for.

#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/fx/effects_owner.h"
#include "model/fx/master_channel.h"
#include "model/instrument/kit.h"
#include "model/instrument/non_audio_instrument.h"
#include "model/lane_owner.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/packed_owner.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <cstddef>
#include <variant>

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace model = deluge::model;

namespace {

/// @brief The object @p owner names, untyped; null for none.
const void* pointer_of(const model::LaneOwner& owner) {
	return std::visit(
	    []<typename T>(T held) -> const void* {
		    if constexpr (std::is_same_v<T, std::monostate>) {
			    return nullptr;
		    }
		    else {
			    return held;
		    }
	    },
	    owner);
}

} // namespace

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

	context("an engine stack's owner", _ {
		it("is the master channel for the song's own stack", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* stack = f.song->setupModelStackWithSongAsTimelineCounter(memory);
			expect(std::holds_alternative<model::MasterChannel*>(stack->owner)).to_be_true();
			spec::make_current(nullptr);
		});

		it("is the synth's sound for its clip", _ {
			spec::SongFixture f = spec::make_synth_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* stack =
			    setupModelStackWithTimelineCounter(memory, f.song, f.clip)
			        ->addOtherTwoThingsButNoNoteRow(model::owner_of(*f.instrument), &f.clip->paramManager);
			Sound* const sound = static_cast<SoundInstrument*>(f.instrument);
			expect(std::get<Sound*>(stack->owner)).to_equal(sound);
			spec::make_current(nullptr);
		});

		it("is the drum's sound for a sound drum's row, found from the row", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto* drum = spec::add_drum<SoundDrum>(f);
			int32_t index = 0;
			NoteRow* row = f.clip->getNoteRowForDrum(drum, &index);
			expect(row).not_().to_be_null();
			if (row != nullptr) {
				alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* stack =
				    setupModelStackWithTimelineCounter(memory, f.song, f.clip)->addNoteRowAndExtraStuff(index, row);
				Sound* const sound = drum;
				expect(std::get<Sound*>(stack->owner)).to_equal(sound);
			}
			spec::make_current(nullptr);
		});

		it("is the MIDI instrument for a MIDI clip", _ {
			spec::SongFixture f = spec::make_midi_song();
			spec::make_current(f.song);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* stack =
			    setupModelStackWithTimelineCounter(memory, f.song, f.clip)
			        ->addOtherTwoThingsButNoNoteRow(model::owner_of(*f.instrument), &f.clip->paramManager);
			expect(std::holds_alternative<NonAudioInstrument*>(stack->owner)).to_be_true();
			spec::make_current(nullptr);
		});
	});

	context("its kind", _ {
		it("is the non-audio drum for a gate drum", _ {
			spec::SongFixture f = spec::make_kit_song();
			spec::make_current(f.song);
			auto* drum = spec::add_drum<GateDrum>(f);
			NonAudioDrum* const non_audio = drum;
			expect(std::get<NonAudioDrum*>(model::owner_of(*drum))).to_equal(non_audio);
			expect(model::effects_owner_of(model::owner_of(*drum)).has_value()).to_be_false();
			spec::make_current(nullptr);
		});
	});

	context("packed as one word, as the sound menu publishes it", _ {
		it("comes back as itself for each kind of owner, and for none", _ {
			// The same alternative, the same object.
			const auto expect_round_trip = [&](const model::LaneOwner& owner) {
				const model::LaneOwner back = model::unpack_owner(model::pack_owner(owner));
				expect(back.index()).to_equal(owner.index());
				expect(pointer_of(back)).to_equal(pointer_of(owner));
			};
			spec::SongFixture synth = spec::make_synth_song();
			spec::make_current(synth.song);
			expect_round_trip(model::LaneOwner{});
			expect_round_trip(model::owner_of(*synth.instrument));
			expect_round_trip(model::LaneOwner{&synth.song->live.master_channel});

			spec::SongFixture kit = spec::make_kit_song();
			spec::make_current(kit.song);
			expect_round_trip(model::owner_of(*kit.instrument));
			expect_round_trip(model::owner_of(*spec::add_drum<GateDrum>(kit)));

			spec::SongFixture midi = spec::make_midi_song();
			spec::make_current(midi.song);
			expect_round_trip(model::owner_of(*midi.instrument));

			spec::AudioSongFixture audio = spec::make_audio_clip_song();
			spec::make_current(audio.song);
			expect_round_trip(model::owner_of(*audio.output));
			spec::make_current(nullptr);
		});

		it("is none for the zero word", _ {
			expect(model::is_none(model::unpack_owner(0))).to_be_true();
		});
	});
});
// clang-format on

CPPSPEC_SPEC(lane_owner)
