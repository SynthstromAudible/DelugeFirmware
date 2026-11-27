/// @file
/// @brief `LearnMapping` and `UnlearnMapping` over a booted song: each writes the song's mapping table, then projects
///        the target's entry onto the engine field that plays it (a learned knob plays from the table, through the
///        mapping index); a target whose owner the song does not hold is refused with the table as it was.

#include "io/midi/mapping_index.h"
#include "io/midi/midi_device_manager.h"
#include "model/clip/clip.h"
#include "model/drum/midi_drum.h"
#include "model/edits/learn_mapping.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/param_ids.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/arrangement.h"
#include "project/mapping_table.h"

#include <optional>
#include <utility>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/indexed_knobs.h"
#include "support/printers/mapping_printer.h"
#include "support/printers/param_id_printer.h"
#include "support/printers/param_target_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;

using deluge::project::ClipMuteTarget;
using deluge::project::ControlsAddress;
using deluge::project::DrumMuteTarget;
using deluge::project::LaneInputTarget;
using deluge::project::Mapping;
using deluge::project::MappingTable;
using deluge::project::MappingTarget;
using deluge::project::MidiDeviceRef;
using Command = deluge::project::MidiCommand;
using deluge::project::OwnerParamTarget;
using deluge::project::SectionLaunchTarget;
using deluge::project::TrackInputTarget;
using deluge::ui::scenes::spec::indexedKnobs;
using deluge::ui::scenes::spec::indexPlays;

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
deluge::project::ParamTarget patched(int32_t p) {
	return deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(p), params::Kind::UNPATCHED_SOUND).id.value);
}

/// @brief An unpatched param of @p kind (UNPATCHED_SOUND or UNPATCHED_GLOBAL), as the table names it.
deluge::project::ParamTarget unpatched(params::Kind kind, int32_t p) {
	return deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(params::UNPATCHED_START + p), kind).id.value);
}

/// @brief @p command, told apart by the device on @p port.
Command from(Command command, MidiDeviceRef::Port port) {
	command.device = MidiDeviceRef{.port = port};
	return command;
}

/// @brief Apply @p edit to the current song.
Error applyEdit(::Song& song, deluge::edits::Edit& edit) {
	deluge::edits::Context ctx{song, song.project};
	return deluge::edits::apply(ctx, edit);
}

} // namespace

// clang-format off
describe mapping_edit("LearnMapping", $ {
	it("learns a synth's knob into the table, which the index plays", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const OwnerParamTarget target{owner, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(cc(2, 74), target);
		expect(held(*f.song, target)).to_equal(cc(2, 74));
		expect(indexPlays(*f.song, target, 2 + IS_A_CC, 74)).to_be_true();
		expect(deluge::bridge::knobOfControl(target.param, params::Kind::UNPATCHED_SOUND))
		    .to_equal(deluge::modulation::knobParam(params::LOCAL_VOLUME));
	});

	it("keeps an unchanged knob's takeover state past another learn", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const OwnerParamTarget volume{owner, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(cc(0, 10), volume);
		deluge::midi::takeoverStore().at(volume, cc(0, 10)).previous = 1234;
		const OwnerParamTarget pan{owner, patched(params::LOCAL_PAN)};
		deluge::edits::learnMappingFor(cc(0, 11), pan);
		expect(static_cast<int>(f.song->project.mappings.params_of(owner).size())).to_equal(2);
		deluge::bridge::projectOutputMappings(*f.song, *f.instrument->id()); // projected again, nothing changed
		const deluge::ui::controls::TakeoverState* held = std::as_const(deluge::midi::takeoverStore()).find(volume, cc(0, 10));
		expect(held).not_().to_be_null("the volume knob's takeover state");
		if (held != nullptr) {
			expect(held->previous).to_equal(std::optional<int32_t>{1234}, "the volume knob's previous position");
		}
		expect(indexPlays(*f.song, volume, IS_A_CC, 10)).to_be_true();
		expect(indexPlays(*f.song, pan, IS_A_CC, 11)).to_be_true();
	});

	it("learns a clip's mute command", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ClipMuteTarget target{f.clip->id()};
		deluge::edits::learnMappingFor(note(3, 40), target);
		expect(held(*f.song, target)).to_equal(note(3, 40));
		expect(f.clip->muteMIDICommand.equalsNoteOrCC(nullptr, 3, 40)).to_be_true();
	});

	it("learns a kit row's mute command and its input", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		deluge::edits::learnMappingFor(note(1, 50), DrumMuteTarget{*drum->id()});
		deluge::edits::learnMappingFor(note(4, 51), LaneInputTarget{*drum->id()});
		expect(held(*f.song, DrumMuteTarget{*drum->id()})).to_equal(note(1, 50));
		expect(held(*f.song, LaneInputTarget{*drum->id()})).to_equal(note(4, 51));
		expect(drum->muteMIDICommand.equalsNoteOrCC(nullptr, 1, 50)).to_be_true();
		expect(drum->midiInput.equalsNoteOrCC(nullptr, 4, 51)).to_be_true();
	});

	it("learns a kit's input with its root note", _ {
		spec::SongFixture f = spec::make_kit_song();
		const TrackInputTarget target{*f.instrument->id()};
		deluge::edits::learnMappingFor(note(5, 36), target);
		expect(held(*f.song, target)).to_equal(note(5, 36));
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
		expect(held(*f.song, SectionLaunchTarget{3})).to_equal(note(0, 60));
	});

	it("learns a hibernating kit row's mute command", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		f.song->moveInstrumentToHibernationList(f.instrument);
		deluge::edits::learnMappingFor(note(2, 30), DrumMuteTarget{*drum->id()});
		expect(drum->muteMIDICommand.equalsNoteOrCC(nullptr, 2, 30)).to_be_true();
	});

	it("keeps a knob learned from DIN or upstream USB on its cable, with its takeover state, past another learn", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const OwnerParamTarget volume{owner, patched(params::LOCAL_VOLUME)};
		const OwnerParamTarget pan{owner, patched(params::LOCAL_PAN)};
		deluge::edits::learnMappingFor(from(cc(0, 10), MidiDeviceRef::Port::DIN), volume);
		deluge::edits::learnMappingFor(from(cc(0, 11), MidiDeviceRef::Port::UPSTREAM_USB_1), pan);
		const auto cableOf = [&](int32_t number) {
			const std::vector<deluge::midi::IndexedEntry> hits = indexedKnobs(*f.song, owner, IS_A_CC, number);
			return hits.size() == 1 ? hits[0].cable : nullptr;
		};
		expect(cableOf(10)).to_equal(&MIDIDeviceManager::dinMIDIPorts, "CC 10's cable");
		expect(cableOf(11)).to_equal(&MIDIDeviceManager::upstreamUSBMIDICable1, "CC 11's cable");
		deluge::midi::takeoverStore().at(volume, from(cc(0, 10), MidiDeviceRef::Port::DIN)).previous = 111;
		deluge::midi::takeoverStore().at(pan, from(cc(0, 11), MidiDeviceRef::Port::UPSTREAM_USB_1)).previous = 222;
		deluge::edits::learnMappingFor(cc(0, 12), OwnerParamTarget{owner, patched(params::LOCAL_OSC_A_VOLUME)});
		expect(static_cast<int>(f.song->project.mappings.params_of(owner).size())).to_equal(3);
		expect(cableOf(10)).to_equal(&MIDIDeviceManager::dinMIDIPorts, "CC 10's cable");
		expect(cableOf(11)).to_equal(&MIDIDeviceManager::upstreamUSBMIDICable1, "CC 11's cable");
		const auto& store = std::as_const(deluge::midi::takeoverStore());
		const auto* first = store.find(volume, from(cc(0, 10), MidiDeviceRef::Port::DIN));
		const auto* second = store.find(pan, from(cc(0, 11), MidiDeviceRef::Port::UPSTREAM_USB_1));
		expect(first).not_().to_be_null("CC 10's takeover state");
		if (first != nullptr) {
			expect(first->previous).to_equal(std::optional<int32_t>{111}, "CC 10's previous position");
		}
		expect(second).not_().to_be_null("CC 11's takeover state");
		if (second != nullptr) {
			expect(second->previous).to_equal(std::optional<int32_t>{222}, "CC 11's previous position");
		}
	});

	it("projects a command's device onto its cable", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		deluge::edits::learnMappingFor(from(note(3, 40), MidiDeviceRef::Port::DIN), ClipMuteTarget{f.clip->id()});
		deluge::edits::learnMappingFor(from(note(4, 41), MidiDeviceRef::Port::UPSTREAM_USB_2),
		                               LaneInputTarget{*drum->id()});
		expect(f.clip->muteMIDICommand.cable).to_equal(&MIDIDeviceManager::dinMIDIPorts);
		expect(f.clip->muteMIDICommand.equalsNoteOrCC(&MIDIDeviceManager::dinMIDIPorts, 3, 40)).to_be_true();
		expect(drum->midiInput.cable).to_equal(&MIDIDeviceManager::upstreamUSBMIDICable2);
	});

	it("projects a hosted device the Deluge does not know onto an unconnected entry made for it, keeping it in the "
	   "table",
	   _ {
		   spec::SongFixture f = spec::make_synth_song();
		   Command command = note(3, 40);
		   command.device = MidiDeviceRef{
		       .port = MidiDeviceRef::Port::HOSTED, .name = "No Such Device", .vendor_id = 0x1234, .product_id = 0x5678};
		   const ClipMuteTarget target{f.clip->id()};
		   deluge::edits::learnMappingFor(command, target);
		   expect(held(*f.song, target)).to_equal(command);
		   expect(f.clip->muteMIDICommand.containsSomething()).to_be_true();
		   // Not no cable, which would match every device while inputs are told apart by device.
		   MIDICable* entry = deluge::bridge::find_cable(*command.device);
		   expect(entry).not_().to_be_null();
		   expect(f.clip->muteMIDICommand.cable).to_equal(entry);
		   expect(entry->connectionFlags).to_equal(0);
	   });

	it("learns a sound kit row's knob as the row's, a sound's param, and not the kit's", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		const ControlsAddress owner{ControlsAddress::Kind::DRUM, *drum->id()};
		const OwnerParamTarget target{owner, unpatched(params::Kind::UNPATCHED_SOUND, params::UNPATCHED_PORTAMENTO)};
		deluge::edits::learnMappingFor(cc(1, 20), target);
		expect(held(*f.song, target)).to_equal(cc(1, 20));
		expect(indexPlays(*f.song, target, 1 + IS_A_CC, 20)).to_be_true();
		expect(deluge::bridge::knobOfControl(target.param, params::Kind::UNPATCHED_SOUND))
		    .to_equal(deluge::modulation::knobParam(params::UNPATCHED_START + params::UNPATCHED_PORTAMENTO));
		const ControlsAddress kit{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		expect(static_cast<int>(indexedKnobs(*f.song, kit, 1 + IS_A_CC, 20).size())).to_equal(0, "the kit's knobs");
	});

	it("learns the song's knob as a global param", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::SONG, 0};
		// Some indices are shared by a sound's and a global's param (their ids coincide); the LPF frequency is a
		// global's alone, so a sound's reading of its id finds no param.
		const OwnerParamTarget target{owner, unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ)};
		expect(deluge::bridge::runtimeIndexOf(params::Kind::UNPATCHED_SOUND, target.param.id).has_value()).to_be_false();
		deluge::edits::learnMappingFor(cc(2, 7), target);
		expect(held(*f.song, target)).to_equal(cc(2, 7));
		expect(indexPlays(*f.song, target, 2 + IS_A_CC, 7)).to_be_true();
		// Read as the global's, the entry names the LPF frequency.
		expect(deluge::bridge::knobOfControl(target.param, params::Kind::UNPATCHED_GLOBAL))
		    .to_equal(deluge::modulation::knobParam(params::UNPATCHED_START + params::UNPATCHED_LPF_FREQ));
	});

	it("refuses a target whose owner the song does not hold and leaves the table as it was", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		deluge::edits::learnMappingFor(note(1, 50), DrumMuteTarget{*drum->id()});
		const MappingTable before = f.song->project.mappings;
		const MappingTarget orphans[] = {
		    OwnerParamTarget{{ControlsAddress::Kind::OUTPUT, 9999}, patched(params::LOCAL_VOLUME)},
		    OwnerParamTarget{{ControlsAddress::Kind::DRUM, 9999}, patched(params::LOCAL_VOLUME)},
		    ClipMuteTarget{9999},
		    DrumMuteTarget{9999},
		    LaneInputTarget{9999},
		    TrackInputTarget{9999},
		    SectionLaunchTarget{static_cast<uint8_t>(kMaxNumSections)},
		};
		for (const MappingTarget& orphan : orphans) {
			deluge::edits::LearnMapping edit{Mapping{.source = note(9, 9), .target = orphan}};
			expect(static_cast<int>(applyEdit(*f.song, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
			expect(f.song->project.mappings).to_equal(before);
		}
	});

	it("rebuilds the index when it refuses a target, so the table it puts back is the one the index holds", _ {
		spec::SongFixture f = spec::make_synth_song();
		// A write beneath the edit leaves the index without it.
		f.song->project.mappings.learn(Mapping{.source = note(2, 50), .target = SectionLaunchTarget{1}});
		deluge::midi::mappingIndex().clear();
		const auto sectionHits = [&] {
			int32_t hits = 0;
			deluge::midi::mappingIndex().forEachCommand(MIDIDeviceManager::upstreamUSBMIDICable1, 2, 50,
			                                            [&](const deluge::midi::IndexedEntry&) { hits++; });
			return hits;
		};
		expect(sectionHits()).to_equal(0);
		deluge::edits::LearnMapping edit{Mapping{.source = note(9, 9), .target = ClipMuteTarget{9999}}};
		expect(static_cast<int>(applyEdit(*f.song, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
		expect(sectionHits()).to_equal(1);
	});
});

describe mapping_unlearn("UnlearnMapping", $ {
	it("unlearns a clip's mute command from the table and the clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ClipMuteTarget target{f.clip->id()};
		deluge::edits::learnMappingFor(note(3, 40), target);
		deluge::edits::unlearnMappingFor(target);
		expect(f.song->project.mappings.find(target)).to_be_null();
		expect(f.clip->muteMIDICommand.containsSomething()).to_be_false();
	});

	it("unlearns every knob of a param, which the index then plays no more", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const OwnerParamTarget target{owner, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(cc(0, 10), target);
		deluge::edits::learnMappingFor(cc(0, 12), target);
		expect(indexPlays(*f.song, target, IS_A_CC, 10)).to_be_true("the index plays CC 10");
		expect(indexPlays(*f.song, target, IS_A_CC, 12)).to_be_true("the index plays CC 12");
		deluge::edits::unlearnMappingFor(target);
		expect(f.song->project.mappings.find(target)).to_be_null();
		expect(static_cast<int>(indexedKnobs(*f.song, owner, IS_A_CC, 10).size())).to_equal(0, "CC 10's knobs");
		expect(static_cast<int>(indexedKnobs(*f.song, owner, IS_A_CC, 12).size())).to_equal(0, "CC 12's knobs");
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
		expect(f.song->project.mappings).to_equal(before);
	});

	it("refuses an owner the song does not hold and leaves the table as it was", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::learnMappingFor(note(0, 60), SectionLaunchTarget{3});
		const MappingTable before = f.song->project.mappings;
		deluge::edits::UnlearnMapping edit{ClipMuteTarget{9999}};
		expect(static_cast<int>(applyEdit(*f.song, edit))).to_equal(static_cast<int>(Error::UNSPECIFIED));
		expect(f.song->project.mappings).to_equal(before);
	});
});
// clang-format on

DELUGE_SPEC(mapping_edit_spec, mapping_edit, mapping_unlearn)
