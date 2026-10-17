/// @file
/// @brief `LearnMapping` and `UnlearnMapping` over a booted song: each writes the song's mapping table, then projects
///        the target's entry onto the engine field that plays it; a target whose owner the song does not hold is
///        refused with the table as it was.

#include "deluge-project/arrangement.h"
#include "deluge-project/mapping_table.h"
#include "model/clip/clip.h"
#include "model/drum/midi_drum.h"
#include "model/edits/learn_mapping.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/song/song.h"
#include "modulation/knob.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace spec = deluge::scenes::spec;
namespace params = deluge::modulation::params;

using deluge::project::ClipMuteTarget;
using deluge::project::ControlsAddress;
using deluge::project::DrumMuteTarget;
using deluge::project::LaneInputTarget;
using deluge::project::Mapping;
using deluge::project::MappingTable;
using deluge::project::MappingTarget;
using Command = deluge::project::MidiCommand;
using deluge::project::ParamTarget;
using deluge::project::SectionLaunchTarget;
using deluge::project::TrackInputTarget;

namespace {

/// @brief A note command on @p channel.
Command note(uint8_t channel, uint8_t noteNumber) {
	return Command{.channel_or_zone = channel, .note_or_cc = noteNumber};
}

/// @brief A CC command on @p channel.
Command cc(uint8_t channel, uint8_t number) {
	return Command{.channel_or_zone = static_cast<uint8_t>(channel + IS_A_CC), .note_or_cc = number};
}

/// @brief The source the table holds for @p target, or an empty command.
Command held(::Song& song, const MappingTarget& target) {
	const Mapping* entry = song.project.mappings.find(target);
	return entry != nullptr ? entry->source : Command{};
}

/// @brief A patched param of a sound, as the table names it.
deluge::engine::ParamId patched(int32_t p) {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(p);
	return deluge::bridge::knobTargetOf(descriptor, params::Kind::UNPATCHED_SOUND);
}

/// @brief Apply @p edit to the current song.
Error applyEdit(::Song& song, deluge::edits::Edit& edit) {
	deluge::edits::Context ctx{song, song.project};
	return deluge::edits::apply(ctx, edit);
}

} // namespace

// clang-format off
describe mapping_edit("LearnMapping", $ {
	it("learns a synth's knob into the table and its knob list", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const ParamTarget target{owner, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(cc(2, 74), target);
		expect(held(*f.song, target) == cc(2, 74)).to_be_true();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		expect(static_cast<int>(sound.midi_knobs.size())).to_equal(1);
		expect(sound.midi_knobs[0].midiInput.equalsNoteOrCC(nullptr, 2 + IS_A_CC, 74)).to_be_true();
		expect(sound.midi_knobs[0].paramDescriptor.isSetToParamWithNoSource(params::LOCAL_VOLUME)).to_be_true();
	});

	it("keeps an unchanged knob whole, with its takeover state", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		deluge::edits::learnMappingFor(cc(0, 10), ParamTarget{owner, patched(params::LOCAL_VOLUME)});
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		sound.midi_knobs[0].previousPositionSaved = true;
		sound.midi_knobs[0].previousPosition = 1234;
		deluge::edits::learnMappingFor(cc(0, 11), ParamTarget{owner, patched(params::LOCAL_PAN)});
		expect(static_cast<int>(sound.midi_knobs.size())).to_equal(2);
		expect(sound.midi_knobs[0].previousPositionSaved).to_be_true();
		expect(static_cast<int>(sound.midi_knobs[0].previousPosition)).to_equal(1234);
		expect(sound.midi_knobs[1].midiInput.equalsNoteOrCC(nullptr, 0 + IS_A_CC, 11)).to_be_true();
	});

	it("learns a clip's mute command", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ClipMuteTarget target{f.clip->id()};
		deluge::edits::learnMappingFor(note(3, 40), target);
		expect(held(*f.song, target) == note(3, 40)).to_be_true();
		expect(f.clip->muteMIDICommand.equalsNoteOrCC(nullptr, 3, 40)).to_be_true();
	});

	it("learns a kit row's mute command and its input", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		deluge::edits::learnMappingFor(note(1, 50), DrumMuteTarget{*drum->id()});
		deluge::edits::learnMappingFor(note(4, 51), LaneInputTarget{*drum->id()});
		expect(held(*f.song, DrumMuteTarget{*drum->id()}) == note(1, 50)).to_be_true();
		expect(held(*f.song, LaneInputTarget{*drum->id()}) == note(4, 51)).to_be_true();
		expect(drum->muteMIDICommand.equalsNoteOrCC(nullptr, 1, 50)).to_be_true();
		expect(drum->midiInput.equalsNoteOrCC(nullptr, 4, 51)).to_be_true();
	});

	it("learns a kit's input with its root note", _ {
		spec::SongFixture f = spec::make_kit_song();
		const TrackInputTarget target{*f.instrument->id()};
		deluge::edits::learnMappingFor(note(5, 36), target);
		expect(held(*f.song, target) == note(5, 36)).to_be_true();
		expect(f.instrument->midiInput.equalsNoteOrCC(nullptr, 5, 36)).to_be_true();
	});

	it("learns a synth's input channel", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::learnMappingFor(Command{.channel_or_zone = 7}, TrackInputTarget{*f.instrument->id()});
		expect(static_cast<int>(f.instrument->midiInput.channelOrZone)).to_equal(7);
	});

	it("learns a section's launch command into the table alone", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::learnMappingFor(note(0, 60), SectionLaunchTarget{3});
		expect(held(*f.song, SectionLaunchTarget{3}) == note(0, 60)).to_be_true();
	});

	it("learns a hibernating kit row's mute command", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		f.song->moveInstrumentToHibernationList(f.instrument);
		deluge::edits::learnMappingFor(note(2, 30), DrumMuteTarget{*drum->id()});
		expect(drum->muteMIDICommand.equalsNoteOrCC(nullptr, 2, 30)).to_be_true();
	});

	it("refuses a target whose owner the song does not hold and leaves the table as it was", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		deluge::edits::learnMappingFor(note(1, 50), DrumMuteTarget{*drum->id()});
		const MappingTable before = f.song->project.mappings;
		const MappingTarget orphans[] = {
		    ParamTarget{{ControlsAddress::Kind::OUTPUT, 9999}, patched(params::LOCAL_VOLUME)},
		    ParamTarget{{ControlsAddress::Kind::DRUM, 9999}, patched(params::LOCAL_VOLUME)},
		    ClipMuteTarget{9999},
		    DrumMuteTarget{9999},
		    LaneInputTarget{9999},
		    TrackInputTarget{9999},
		    SectionLaunchTarget{static_cast<uint8_t>(kMaxNumSections)},
		};
		for (const MappingTarget& orphan : orphans) {
			deluge::edits::LearnMapping edit{Mapping{.source = note(9, 9), .target = orphan}};
			expect(static_cast<int>(applyEdit(*f.song, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
			expect(f.song->project.mappings == before).to_be_true();
		}
	});
});

describe mapping_unlearn("UnlearnMapping", $ {
	it("unlearns a clip's mute command from the table and the clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ClipMuteTarget target{f.clip->id()};
		deluge::edits::learnMappingFor(note(3, 40), target);
		deluge::edits::unlearnMappingFor(target);
		expect(f.song->project.mappings.find(target) == nullptr).to_be_true();
		expect(f.clip->muteMIDICommand.containsSomething()).to_be_false();
	});

	it("unlearns every knob of a param and empties the knob list", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const ParamTarget target{owner, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(cc(0, 10), target);
		deluge::edits::learnMappingFor(cc(0, 12), target);
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		expect(static_cast<int>(sound.midi_knobs.size())).to_equal(2);
		deluge::edits::unlearnMappingFor(target);
		expect(f.song->project.mappings.find(target) == nullptr).to_be_true();
		expect(static_cast<int>(sound.midi_knobs.size())).to_equal(0);
	});

	it("unlearns a kit row's input", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		deluge::edits::learnMappingFor(note(4, 51), LaneInputTarget{*drum->id()});
		deluge::edits::unlearnMappingFor(LaneInputTarget{*drum->id()});
		expect(drum->midiInput.containsSomething()).to_be_false();
	});

	it("succeeds on a target the table does not hold", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MappingTable before = f.song->project.mappings;
		deluge::edits::UnlearnMapping edit{ClipMuteTarget{f.clip->id()}};
		expect(static_cast<int>(applyEdit(*f.song, edit))).to_equal(static_cast<int>(Error::NONE));
		expect(f.song->project.mappings == before).to_be_true();
	});

	it("refuses an owner the song does not hold and leaves the table as it was", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::learnMappingFor(note(0, 60), SectionLaunchTarget{3});
		const MappingTable before = f.song->project.mappings;
		deluge::edits::UnlearnMapping edit{ClipMuteTarget{9999}};
		expect(static_cast<int>(applyEdit(*f.song, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
		expect(f.song->project.mappings == before).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(mapping_edit_spec, mapping_edit, mapping_unlearn)
