/// @file
/// @brief Who hears a change to an expression lane, by the lane's address: a melodic clip's output for the clip's
///        channel lanes and its rows' per-note lanes, a kit row's drum for that row's lanes, and nobody for a kit's
///        own lanes or a row without a drum.

#include "model/clip/instrument_clip.h"
#include "model/instrument/expression_receiver.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
using deluge::model::expressionReceiverOf;

// clang-format off
describe expression_receiver("expressionReceiverOf", ${
	it("names a melodic clip's output for its channel and per-note lanes", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto* synth = static_cast<MelodicInstrument*>(f.instrument);
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
		NoteRow* row = f.clip->getOrCreateNoteRowForYNote(60, stack)->getNoteRow();

		auto channel = expressionReceiverOf(*f.clip, nullptr);
		expect(channel.melodic).to_equal(synth);
		expect(channel.drum).to_be_null();

		auto note = expressionReceiverOf(*f.clip, row);
		expect(note.melodic).to_equal(synth);
		expect(note.drum).to_be_null();
	});

	it("names a kit row's drum", _ {
		spec::SongFixture f = spec::make_kit_song();
		auto* drum = spec::add_drum<SoundDrum>(f);
		auto row = expressionReceiverOf(*f.clip, &f.clip->noteRows[0]);
		expect(row.drum).to_equal(static_cast<Drum*>(drum));
		expect(row.melodic).to_be_null();
	});

	it("names nobody for a kit's own lanes or a row without a drum", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		auto kit_own = expressionReceiverOf(*f.clip, nullptr);
		expect(kit_own.drum).to_be_null();
		expect(kit_own.melodic).to_be_null();

		NoteRow& row = f.clip->noteRows[0];
		Drum* saved = row.drum;
		row.drum = nullptr;
		auto drumless = expressionReceiverOf(*f.clip, &row);
		row.drum = saved;
		expect(drumless.drum).to_be_null();
		expect(drumless.melodic).to_be_null();
	});
});
// clang-format on

CPPSPEC_SPEC(expression_receiver)
