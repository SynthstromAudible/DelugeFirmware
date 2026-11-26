/// @file
/// @brief What a change to an expression lane or a MIDI CC lane does, driven as a user edit of the lane
///        (`AutoParam::setCurrentValueInResponseToUserInput`): which instrument or drum hears it, which changes are
///        too small to matter, and the MIDI a MIDI instrument or a sound with MIDI out sends.
///
/// Everything is observed from outside the dispatch: an instrument's or drum's own expression state (a drum's arp
/// note), and the DIN bytes the app sent (`support/midi_out_tap.h`).

#include "extern.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <cstdint>
#include <string>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/midi_out_tap.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace midi_out = deluge::ui::scenes::spec::midi_out;
namespace params = deluge::modulation::params;

namespace {

/// @brief The note every per-note example plays and edits.
constexpr int32_t kNote = 60;

/// @brief The stack of expression dimension @p dim on the clip's own params (no note row): a channel lane.
ModelStackWithAutoParam* channelLane(spec::SongFixture& f, int32_t dim, void* memory) {
	ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
	return stack->addOtherTwoThingsButNoNoteRow(deluge::model::owner_of(*f.clip->output), &f.clip->paramManager)
	    ->getExpressionAutoParamFromID(dim);
}

/// @brief The stack of expression dimension @p dim on the note row at @p yNote (created if absent): a per-note lane.
ModelStackWithAutoParam* noteLane(spec::SongFixture& f, int32_t yNote, int32_t dim, void* memory) {
	ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
	ModelStackWithNoteRow* row = f.clip->getOrCreateNoteRowForYNote(yNote, stack);
	return row->addOtherTwoThingsAutomaticallyGivenNoteRow()->getExpressionAutoParamFromID(dim);
}

/// @brief A kit row's lane: row @p index, whose drum the fixture added.
ModelStackWithAutoParam* kitRowLane(spec::SongFixture& f, int32_t index, int32_t dim, void* memory) {
	ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
	NoteRow* row = &f.clip->noteRows[index];
	return stack->addNoteRow(index, row)
	    ->addOtherTwoThingsAutomaticallyGivenNoteRow()
	    ->getExpressionAutoParamFromID(dim);
}

/// @brief A MIDI instrument's CC @p cc lane on its clip.
ModelStackWithAutoParam* ccLane(spec::SongFixture& f, int32_t cc, void* memory) {
	ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
	return static_cast<MIDIInstrument*>(f.instrument)
	    ->getModelStackWithParam(stack, f.clip, cc, params::Kind::MIDI, /*affectEntire=*/false,
	                             /*useMenuStack=*/false, /*forEdit=*/true);
}

/// @brief @p f with its clip made its output's active clip, as playing or selecting it does: only an active clip's
///        lanes reach the instrument.
spec::SongFixture active(spec::SongFixture f) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	(void)f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip), PgmChangeSend::NEVER);
	return f;
}

/// @brief Move a lane as a user edit does, with no undo step.
void set(ModelStackWithAutoParam* lane, int32_t value) {
	lane->autoParam->setCurrentValueInResponseToUserInput(value, lane, /*shouldLogAction=*/false);
}

/// @brief Hold @p note on @p f's instrument as an audition does, so per-note events find it.
void hold(spec::SongFixture& f, int32_t note) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStack* stack = setupModelStackWithSong(memory, f.song);
	static_cast<MelodicInstrument*>(f.instrument)->beginAuditioningForNote(stack, note, 64, zeroMPEValues);
}

/// @brief Release @p note.
void release(spec::SongFixture& f, int32_t note) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStack* stack = setupModelStackWithSong(memory, f.song);
	static_cast<MelodicInstrument*>(f.instrument)->endAuditioningForNote(stack, note);
}

} // namespace

// clang-format off
describe instrument_events_characterisation("instrument events characterisation", ${
	before_each([] { midi_out::clear(); });

	context("a synth clip", _ {
		it("takes its channel lane as monophonic expression", _ {
			spec::SongFixture f = active(spec::make_synth_song());
			auto* synth = static_cast<SoundInstrument*>(f.instrument);
			expect(f.clip->isActiveOnOutput()).to_be_true();
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			set(channelLane(f, Y_SLIDE_TIMBRE, memory), 0x20000000);
			expect(synth->monophonicExpressionValues[Y_SLIDE_TIMBRE]).to_equal(0x20000000);
			expect(static_cast<bool>(synth->expressionSourcesChangedAtSynthLevel[Y_SLIDE_TIMBRE])).to_be_true();
		});

		it("sends a held note's per-note pressure as poly aftertouch on its MIDI out channel", _ {
			spec::SongFixture f = active(spec::make_synth_song());
			auto* synth = static_cast<SoundInstrument*>(f.instrument);
			synth->outputMidiChannel = 3;
			hold(f, kNote);
			midi_out::clear();
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			set(noteLane(f, kNote, Z_PRESSURE, memory), 0x40000000);
			std::string sent = midi_out::take();
			release(f, kNote);
			synth->outputMidiChannel = MIDI_CHANNEL_NONE;
			midi_out::clear();
			expect(sent).to_equal(std::string{"a3 3c 40"});
		});
	});

	context("a MIDI clip", _ {
		it("sends its channel pitch bend lane", _ {
			spec::SongFixture f = active(spec::make_midi_song());
			auto* midi = static_cast<MIDIInstrument*>(f.instrument);
			midi->setChannel(2);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			set(channelLane(f, X_PITCH_BEND, memory), 0x01000000);
			expect(midi_out::take()).to_equal(std::string{"e2 40 40"});
		});

		it("sends a pressure change only when its 7-bit value moves", _ {
			spec::SongFixture f = active(spec::make_midi_song());
			static_cast<MIDIInstrument*>(f.instrument)->setChannel(2);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* lane = channelLane(f, Z_PRESSURE, memory);
			set(lane, 0x10000000);
			expect(midi_out::take()).to_equal(std::string{"d2 10"});
			set(lane, 0x10FFFFFF);
			expect(midi_out::take()).to_equal(std::string{""});
			set(lane, 0x11000000);
			expect(midi_out::take()).to_equal(std::string{"d2 11"});
		});

		it("sends a pitch bend change only when its 14-bit value moves", _ {
			spec::SongFixture f = active(spec::make_midi_song());
			static_cast<MIDIInstrument*>(f.instrument)->setChannel(2);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* lane = channelLane(f, X_PITCH_BEND, memory);
			set(lane, 0x00040000);
			expect(midi_out::take()).to_equal(std::string{"e2 01 40"});
			set(lane, 0x0007FFFF);
			expect(midi_out::take()).to_equal(std::string{""});
			set(lane, 0x00080000);
			expect(midi_out::take()).to_equal(std::string{"e2 02 40"});
		});

		it("sends a CC lane's change only when its CC value moves", _ {
			spec::SongFixture f = active(spec::make_midi_song());
			static_cast<MIDIInstrument*>(f.instrument)->setChannel(2);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* lane = ccLane(f, 74, memory);
			expect(lane).not_().to_be_null();
			if (lane == nullptr) {
				return;
			}
			expect(lane->autoParam).not_().to_be_null();
			if (lane->autoParam == nullptr) {
				return;
			}
			set(lane, 0x20000000);
			expect(midi_out::take()).to_equal(std::string{"b2 4a 50"});
			set(lane, 0x20000001);
			expect(midi_out::take()).to_equal(std::string{""});
		});
	});

	context("a CV clip", _ {
		it("takes its channel pitch bend lane", _ {
			spec::SongFixture f = active(spec::make_cv_song());
			auto* cv = static_cast<CVInstrument*>(f.instrument);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			set(channelLane(f, X_PITCH_BEND, memory), 0x01000000);
			expect(cv->monophonicPitchBendValue).to_equal(0x01000000);
		});
	});

	context("a kit", _ {
		it("gives a sound drum's row lane to the drum", _ {
			spec::SongFixture f = active(spec::make_kit_song());
			auto* drum = spec::add_drum<SoundDrum>(f);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			set(kitRowLane(f, 0, Z_PRESSURE, memory), 0x40000000);
			expect(static_cast<int32_t>(drum->arpeggiator.active_note.mpeValues[Z_PRESSURE])).to_equal(0x4000);
		});

		it("gives a MIDI drum's row lane to the drum", _ {
			spec::SongFixture f = active(spec::make_kit_song());
			(void)spec::add_drum<MIDIDrum>(f);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			set(kitRowLane(f, 0, Z_PRESSURE, memory), 0x40000000);
			// Poly aftertouch on the drum's channel for the note its arp has on: none here, so the note byte is 0xff.
			expect(midi_out::take()).to_equal(std::string{"a0 ff 40"});
		});

		it("lets a gate drum's row lane change nothing", _ {
			spec::SongFixture f = active(spec::make_kit_song());
			auto* drum = spec::add_drum<GateDrum>(f);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			set(kitRowLane(f, 0, Z_PRESSURE, memory), 0x40000000);
			expect(static_cast<int32_t>(drum->arpeggiator.active_note.mpeValues[Z_PRESSURE])).to_equal(0);
			expect(midi_out::take()).to_equal(std::string{""});
		});
	});
});
// clang-format on

CPPSPEC_SPEC(instrument_events_characterisation)
