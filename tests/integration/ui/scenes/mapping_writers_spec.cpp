/// @file
/// @brief The song's mapping table through the runtime's own structural steps: a preset change, a clone, a delete, an
///        owner arriving in a running song, a kit change, and a knob found not to be relative. After each, the table
///        holds what the engine plays, and the mapping index plays the table's knobs.

#include "effects/master_chain.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/learned_dispatch.h"
#include "io/midi/learned_midi.h"
#include "io/midi/mapping_index.h"
#include "io/midi/midi_device_manager.h"
#include "memory/heaps.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/midi_drum.h"
#include "model/edits/learn_mapping.h"
#include "model/fx/chain_params.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "model/model_stack.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/mapping_table.h"
#include "storage/storage_manager.h"

#include <optional>
#include <utility>
#include <vector>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/indexed_knobs.h"
#include "support/printers/mapping_printer.h"
#include "support/printers/param_id_printer.h"
#include "support/printers/param_target_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;

using deluge::project::ClipMuteTarget;
using deluge::project::ControlsAddress;
using deluge::project::DrumMuteTarget;
using deluge::project::LaneInputTarget;
using deluge::project::Mapping;
using deluge::project::MappingTarget;
using deluge::project::MidiDeviceRef;
using Command = deluge::project::MidiCommand;
using deluge::project::OwnerParamTarget;
using deluge::project::TrackInputTarget;
using deluge::ui::controls::TakeoverState;
using deluge::ui::scenes::spec::indexedKnobs;
using deluge::ui::scenes::spec::indexPlays;

namespace {

/// @brief A note command on @p channel.
Command note(uint8_t channel, uint8_t noteNumber) {
	return Command{.channel_or_zone = channel, .note_or_cc = noteNumber};
}

/// @brief A knob's CC on @p channel, as a knob's learn spells it (no CC bias on the channel).
Command knobCc(uint8_t channel, uint8_t number) {
	return Command{.channel_or_zone = channel, .note_or_cc = number};
}

/// @brief @p command, only from a hosted device that is not connected.
Command fromAbsentDevice(Command command) {
	command.device = MidiDeviceRef{
	    .port = MidiDeviceRef::Port::HOSTED, .name = "No Such Device", .vendor_id = 0x1234, .product_id = 0x5678};
	return command;
}

/// @brief The source the table holds for @p target, or an empty command.
Command held(::Song& song, const MappingTarget& target) {
	const Mapping* entry = song.project.mappings.find(target);
	return entry != nullptr ? entry->source : Command{};
}

/// @brief Whether @p field plays what the table holds for @p target.
bool fieldMatches(::Song& song, const MappingTarget& target, const ::LearnedMIDI& field) {
	return deluge::bridge::command_of(field) == held(song, target);
}

/// @brief A patched param of a sound, as the table names it.
deluge::project::ParamTarget patched(int32_t p) {
	return deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(p), params::Kind::UNPATCHED_SOUND).id.value);
}

/// @brief An unpatched param of @p kind, as the table names it.
deluge::project::ParamTarget unpatched(params::Kind kind, int32_t p) {
	return deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(params::UNPATCHED_START + p), kind).id.value);
}

/// @brief A synth built the way a preset load builds one, its params backed up with the song, not yet in it.
SoundInstrument* makeLooseSynth(::Song& song) {
	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->setupAsDefaultSynth(&params);
	song.live.param_backup.backUp(synth, nullptr, &params, true);
	return synth;
}

/// @brief A kit built the way a preset load builds one, its params backed up with the song, not yet in it.
Kit* makeLooseKit(::Song& song) {
	ParamManagerForTimeline params;
	params.setupUnpatched();
	deluge::model::initGlobalParams(&params);
	auto* kit = static_cast<Kit*>(StorageManager::createNewInstrument(OutputType::KIT, &params));
	song.live.param_backup.backUp(kit, nullptr, &params, true);
	return kit;
}

/// @brief Make @p f's clip its instrument's active clip, as a song's own load does: a preset change reads the
///        replaced instrument's params through it.
void activate(spec::SongFixture& f) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip));
}

/// @brief A hosted cable this spec owns, registered with the device manager so a command naming it finds it.
struct SpecCable final : ::MIDICableUSBHosted {
	SpecCable(const char* n, uint16_t vendor, uint16_t product) {
		name = n;
		vendorId = vendor;
		productId = product;
	}
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return name.c_str(); }
	void sendMessage(MIDIMessage) override {}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

/// @brief A connected controller called "MW" whose USB ids are not the ones a song saved for it: found by its name.
SpecCable& renamedController() {
	static SpecCable cable{"MW", 0x9999, 0x8888};
	static const bool registered = [] {
		MIDIDeviceManager::hostedMIDIDevices.push_back(&cable);
		return true;
	}();
	(void)registered;
	return cable;
}

/// @brief A kit built the way a preset load builds one, holding @p rows MIDI drums, not yet in @p f's song.
Kit* makeKitWithRows(spec::SongFixture& f, int rows, std::vector<MIDIDrum*>* drums = nullptr) {
	Kit* kit = makeLooseKit(*f.song);
	for (int i = 0; i < rows; i++) {
		void* memory = deluge::memory::alloc_fast(sizeof(MIDIDrum));
		auto* drum = new (memory) MIDIDrum();
		kit->addDrum(drum);
		if (drums != nullptr) {
			drums->push_back(drum);
		}
	}
	return kit;
}

/// @brief Whether every row of @p kit plays what the table holds for it.
bool rowsMatch(::Song& song, Kit& kit) {
	for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		if (!drum->id() || !fieldMatches(song, DrumMuteTarget{*drum->id()}, drum->muteMIDICommand)
		    || !fieldMatches(song, LaneInputTarget{*drum->id()}, drum->midiInput)) {
			return false;
		}
	}
	return true;
}

/// @brief Scroll @p f's clip to its first row, as a user looking at it would: a row on screen keeps what it holds
///        when the clip enters kit mode.
void scrollToFirstRow(spec::SongFixture& f) {
	f.clip->yScroll = f.clip->getYVisualFromYNote(f.clip->noteRows[0].y(), f.song);
}

/// @brief A kit's clip changed to a synth: its rows give their commands up to the clip's rows, which then hold them.
SoundInstrument* toSynth(spec::SongFixture& f, Kit* kit) {
	kit->editedByUser = true; // so it hibernates, its rows with it
	SoundInstrument* synth = makeLooseSynth(*f.song);
	f.song->replaceInstrument(kit, synth);
	synth->editedByUser = true;
	return synth;
}

/// @brief Whether the store holds a state for @p source on @p target at all.
bool storeHolds(const OwnerParamTarget& target, const Command& source) {
	return std::as_const(deluge::midi::takeoverStore()).find(target, source) != nullptr;
}

/// @brief Whether the store holds @p position for the knob @p source plays on @p target.
bool storeSaw(const OwnerParamTarget& target, const Command& source, int32_t position) {
	const TakeoverState* held = std::as_const(deluge::midi::takeoverStore()).find(target, source);
	return held != nullptr && held->previous == std::optional<int32_t>{position};
}

/// @brief A MIDI drum, not in any kit.
MIDIDrum* makeLooseMidiDrum() {
	void* memory = deluge::memory::alloc_fast(sizeof(MIDIDrum));
	return memory != nullptr ? new (memory) MIDIDrum() : nullptr;
}

} // namespace

// clang-format off
describe mapping_writers("the mapping table through the runtime's structural steps", $ {
	it("keeps a track's input and knobs through a preset change", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(f);
		const uint32_t oldId = *f.instrument->id();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, oldId}, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(knobCc(1, 74), knob);
		deluge::edits::learnMappingFor(note(2, 60), TrackInputTarget{oldId});

		SoundInstrument* preset = makeLooseSynth(*f.song);
		f.song->replaceInstrument(f.instrument, preset);
		expect(preset->id().has_value()).to_be_true();
		const uint32_t newId = *preset->id();

		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, newId});
		expect(static_cast<int>(knobs.size())).to_equal(1);
		expect(knobs.size()).to_equal(1);
		if (knobs.size() == 1) {
			expect(knobs[0].source).to_equal(knobCc(1, 74));
		}
		expect(held(*f.song, TrackInputTarget{newId})).to_equal(note(2, 60));
		expect(f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, oldId}).empty()).to_be_true();
		expect(f.song->project.mappings.find(TrackInputTarget{oldId})).to_be_null();
		OwnerParamTarget handed = knob;
		handed.owner.id = newId;
		expect(indexPlays(*f.song, handed, 1, 74)).to_be_true(); // the preset plays it
		expect(preset->midiInput.equalsNoteOrCC(nullptr, 2, 60)).to_be_true();
	});

	it("keeps a preset's own knobs when the track it replaces had none", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(f);
		SoundInstrument* preset = makeLooseSynth(*f.song);
		preset->effectsOwner()->knobs->loaded.push_back(Mapping{.source = knobCc(3, 21), // as the preset's file read it
		                                       .target = OwnerParamTarget{{}, patched(params::LOCAL_VOLUME)},
		                                       .relative = true});
		f.song->replaceInstrument(f.instrument, preset);
		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *preset->id()});
		expect(static_cast<int>(knobs.size())).to_equal(1);
		expect(knobs.size()).to_equal(1);
		if (knobs.size() == 1) {
			expect(knobs[0].source).to_equal(knobCc(3, 21));
		}
	});

	it("records the input and knobs a track brings into a running song", _ {
		spec::SongFixture f = spec::make_synth_song();
		SoundInstrument* synth = makeLooseSynth(*f.song);
		synth->midiInput.channelOrZone = 5; // as its preset's file read them
		synth->midiInput.noteOrCC = 255;
		synth->effectsOwner()->knobs->loaded.push_back(
		    Mapping{.source = knobCc(4, 10), .target = OwnerParamTarget{{}, patched(params::LOCAL_PAN)}, .relative = true});
		f.song->addOutput(synth);
		const uint32_t id = *synth->id();
		expect(fieldMatches(*f.song, TrackInputTarget{id}, synth->midiInput)).to_be_true();
		expect(f.song->project.mappings.find(TrackInputTarget{id})).not_().to_be_null();
		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, id});
		expect(knobs.size()).to_equal(size_t{1});
		if (knobs.size() == 1) {
			expect(knobs[0].source).to_equal(knobCc(4, 10));
		}
		expect(synth->effectsOwner()->knobs->loaded.empty()).to_be_true(); // the table holds them now
		expect(indexPlays(*f.song, OwnerParamTarget{{ControlsAddress::Kind::OUTPUT, id}, patched(params::LOCAL_PAN)}, 4, 10))
		    .to_be_true();
	});

	it("records the mute and input a kit row brings into a running song", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = makeLooseMidiDrum();
		drum->muteMIDICommand.channelOrZone = 6; // as its preset's file read them
		drum->muteMIDICommand.noteOrCC = 30;
		drum->midiInput.channelOrZone = 7;
		drum->midiInput.noteOrCC = 31;
		static_cast<Kit*>(f.instrument)->addDrum(drum);
		const uint32_t id = *drum->id();
		expect(held(*f.song, DrumMuteTarget{id})).to_equal(note(6, 30));
		expect(held(*f.song, LaneInputTarget{id})).to_equal(note(7, 31));
	});

	it("copies an audio track's knobs to its clone, device included", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.output->id()},
		                       unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		deluge::edits::learnMappingFor(fromAbsentDevice(knobCc(0, 7)), knob);
		deluge::midi::takeoverStore().at(knob, fromAbsentDevice(knobCc(0, 7))).previous = 515;
		AudioOutput* clone = f.song->createNewAudioOutput();
		clone->cloneFrom(f.output);
		deluge::bridge::cloneOutputMappings(*f.output, *clone, *f.song);
		OwnerParamTarget cloneKnob = knob;
		cloneKnob.owner.id = *clone->id();
		expect(storeSaw(cloneKnob, fromAbsentDevice(knobCc(0, 7)), 515)).to_be_true();
		expect(storeSaw(knob, fromAbsentDevice(knobCc(0, 7)), 515)).to_be_true();
		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *clone->id()});
		expect(static_cast<int>(knobs.size())).to_equal(1);
		expect(knobs.size()).to_equal(size_t{1});
		if (knobs.size() == 1) {
			expect(knobs[0].source).to_equal(fromAbsentDevice(knobCc(0, 7)));
		}
		// The clone's knob listens to an entry made for the device, which no message arrives on until it is plugged in.
		MIDICable* entry = deluge::bridge::find_cable(*fromAbsentDevice(knobCc(0, 7)).device);
		const std::vector<deluge::midi::IndexedEntry> played = indexedKnobs(*f.song, cloneKnob.owner, 0, 7);
		expect(entry).not_().to_be_null("the absent device's entry");
		expect(played.size()).to_equal(size_t{1}, "the clone's knobs on CC 7");
		if (played.size() == 1) {
			expect(played[0].cable).to_equal(entry);
		}
	});

	it("copies a clip's mute command to the audio clip that replaces it", _ {
		spec::SongFixture f = spec::make_synth_song();
		const uint32_t oldId = f.clip->id();
		deluge::edits::learnMappingFor(note(3, 40), ClipMuteTarget{oldId});
		Clip* audio = f.song->replaceInstrumentClipWithAudioClip(f.clip, 0);
		expect(audio).not_().to_be_null();
		if (audio == nullptr) {
			return;
		}
		expect(held(*f.song, ClipMuteTarget{audio->id()})).to_equal(note(3, 40));
		expect(audio->muteMIDICommand.equalsNoteOrCC(nullptr, 3, 40)).to_be_true();
		expect(f.song->project.mappings.find(ClipMuteTarget{oldId})).to_be_null();
	});

	it("forgets a deleted clip's mute command", _ {
		spec::SongFixture f = spec::make_synth_song();
		const uint32_t id = f.clip->id();
		deluge::edits::learnMappingFor(note(3, 41), ClipMuteTarget{id});
		ClipArray& clips = f.song->live.clips.session_clips;
		clips.erase(clips.begin() + clips.getIndexForClip(f.clip));
		f.song->deleteClipObject(f.clip, false, InstrumentRemoval::NONE);
		expect(f.song->project.mappings.find(ClipMuteTarget{id})).to_be_null();
	});

	it("forgets a deleted kit row's mute and input", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		const uint32_t id = *drum->id();
		deluge::edits::learnMappingFor(note(1, 50), DrumMuteTarget{id});
		deluge::edits::learnMappingFor(note(1, 51), LaneInputTarget{id});
		auto* kit = static_cast<Kit*>(f.instrument);
		NoteRow* row = f.clip->getNoteRowForDrum(drum);
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
		row->setDrum(nullptr, kit, stack->addNoteRow(f.clip->getNoteRowId(row, 0), row));
		kit->removeDrum(drum);
		expect(f.song->project.mappings.find(DrumMuteTarget{id})).to_be_null();
		expect(f.song->project.mappings.find(LaneInputTarget{id})).to_be_null();
	});

	it("forgets a deleted track's knobs and input", _ {
		spec::SongFixture f = spec::make_synth_song();
		SoundInstrument* synth = makeLooseSynth(*f.song);
		f.song->addOutput(synth);
		const uint32_t id = *synth->id();
		deluge::edits::learnMappingFor(knobCc(0, 70), OwnerParamTarget{{ControlsAddress::Kind::OUTPUT, id},
		                                                            patched(params::LOCAL_VOLUME)});
		deluge::edits::learnMappingFor(note(0, 60), TrackInputTarget{id});
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, id}, patched(params::LOCAL_VOLUME)};
		deluge::midi::takeoverStore().at(knob, knobCc(0, 70)).previous = 9;
		f.song->deleteOutputThatIsInMainList(synth);
		expect(storeSaw(knob, knobCc(0, 70), 9)).to_be_false();
		expect(f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, id}).empty()).to_be_true();
		expect(f.song->project.mappings.find(TrackInputTarget{id})).to_be_null();
	});

	it("keeps each kit row's table entry and field the same through a kit change", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* oldDrum = spec::add_drum<MIDIDrum>(f);
		activate(f);
		deluge::edits::learnMappingFor(note(1, 36), DrumMuteTarget{*oldDrum->id()});
		deluge::edits::learnMappingFor(note(2, 37), LaneInputTarget{*oldDrum->id()});
		auto* oldKit = static_cast<Kit*>(f.instrument);
		oldKit->editedByUser = true; // so it hibernates, its rows with it

		Kit* newKit = makeLooseKit(*f.song);
		MIDIDrum* newDrum = makeLooseMidiDrum();
		newKit->addDrum(newDrum);
		f.song->replaceInstrument(oldKit, newKit);

		for (MIDIDrum* drum : {oldDrum, newDrum}) {
			expect(drum->id().has_value()).to_be_true();
			expect(fieldMatches(*f.song, DrumMuteTarget{*drum->id()}, drum->muteMIDICommand)).to_be_true();
			expect(fieldMatches(*f.song, LaneInputTarget{*drum->id()}, drum->midiInput)).to_be_true();
		}
		expect(newDrum->muteMIDICommand.equalsNoteOrCC(nullptr, 1, 36)).to_be_true();
	});

	it("keeps an absent device's name on a row's mute through a kit change", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* oldDrum = spec::add_drum<MIDIDrum>(f);
		activate(f);
		// A clip mute on the same channel and note, from any device: the runtime plays both alike.
		deluge::edits::learnMappingFor(note(1, 36), ClipMuteTarget{f.clip->id()});
		deluge::edits::learnMappingFor(fromAbsentDevice(note(1, 36)), DrumMuteTarget{*oldDrum->id()});
		static_cast<Kit*>(f.instrument)->editedByUser = true;

		std::vector<MIDIDrum*> rows;
		Kit* newKit = makeKitWithRows(f, 1, &rows);
		f.song->replaceInstrument(f.instrument, newKit);
		expect(held(*f.song, DrumMuteTarget{*rows[0]->id()})).to_equal(fromAbsentDevice(note(1, 36)));
		expect(held(*f.song, ClipMuteTarget{f.clip->id()})).to_equal(note(1, 36));
	});

	it("keeps each row's entry and field the same through a change to a kit with fewer rows", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* first = spec::add_drum<MIDIDrum>(f);
		MIDIDrum* second = spec::add_drum<MIDIDrum>(f);
		activate(f);
		deluge::edits::learnMappingFor(note(1, 36), DrumMuteTarget{*first->id()});
		deluge::edits::learnMappingFor(note(1, 37), DrumMuteTarget{*second->id()});
		deluge::edits::learnMappingFor(note(2, 38), LaneInputTarget{*second->id()});
		auto* oldKit = static_cast<Kit*>(f.instrument);
		oldKit->editedByUser = true;

		Kit* newKit = makeKitWithRows(f, 1);
		f.song->replaceInstrument(oldKit, newKit);
		expect(rowsMatch(*f.song, *oldKit)).to_be_true();
		expect(rowsMatch(*f.song, *newKit)).to_be_true();
		expect(newKit->firstDrum->muteMIDICommand.containsSomething()).to_be_true();
	});

	it("keeps each row's entry and field the same through a kit, a synth and a kit again", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		activate(f);
		deluge::edits::learnMappingFor(note(1, 36), DrumMuteTarget{*drum->id()});
		auto* oldKit = static_cast<Kit*>(f.instrument);
		SoundInstrument* synth = toSynth(f, oldKit);
		expect(rowsMatch(*f.song, *oldKit)).to_be_true();

		scrollToFirstRow(f);
		Kit* newKit = makeKitWithRows(f, 1);
		f.song->replaceInstrument(synth, newKit);
		expect(newKit->firstDrum->muteMIDICommand.equalsNoteOrCC(nullptr, 1, 36)).to_be_true();
		expect(rowsMatch(*f.song, *newKit)).to_be_true();
	});

	it("records the row commands a clip's rows give a kit its clip moves to", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		activate(f);
		deluge::edits::learnMappingFor(note(1, 36), DrumMuteTarget{*drum->id()});
		(void)toSynth(f, static_cast<Kit*>(f.instrument)); // the clip's rows now hold the mute

		scrollToFirstRow(f);
		Kit* other = makeKitWithRows(f, 1);
		f.song->addOutput(other);
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		const Error error = f.clip->changeInstrument(setupModelStackWithTimelineCounter(memory, f.song, f.clip), other,
		                                             nullptr, InstrumentRemoval::NONE);
		expect(static_cast<int>(error)).to_equal(static_cast<int>(Error::NONE));
		expect(other->firstDrum->muteMIDICommand.equalsNoteOrCC(nullptr, 1, 36)).to_be_true();
		expect(rowsMatch(*f.song, *other)).to_be_true();
	});

	it("clears a clip knob's relative flag in the table when its device's saved ids are not the cable's", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(f);
		SpecCable& cable = renamedController();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_PAN)};
		Command source = knobCc(2, 9);
		source.device = MidiDeviceRef{
		    .port = MidiDeviceRef::Port::HOSTED, .name = "MW", .vendor_id = 0x0001, .product_id = 0x0002};
		deluge::edits::learnMappingFor(source, knob);
		const std::vector<deluge::midi::IndexedEntry> played = indexedKnobs(*f.song, knob.owner, 2, 9);
		expect(played.size()).to_equal(size_t{1}, "the knobs on CC 9");
		if (played.size() == 1) {
			expect(played[0].cable).to_equal(&cable);
			expect(played[0].relative).to_be_true();
		}

		deluge::midi::learnedCcToOutput(*f.instrument, cable, 2, 9, 64);
		expect(f.song->project.mappings.find(knob)->relative).to_be_false();
		const std::vector<deluge::midi::IndexedEntry> after = indexedKnobs(*f.song, knob.owner, 2, 9);
		expect(after.size()).to_equal(size_t{1}, "the knobs on CC 9");
		if (after.size() == 1) {
			expect(after[0].relative).to_be_false();
		}
		expect(held(*f.song, knob)).to_equal(source);
	});

	it("learns a menu's MIDI knob through the table, and unlearns it there", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		const deluge::modulation::KnobTarget volume = deluge::modulation::knobParam(params::LOCAL_VOLUME);
		expect(deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1)).to_be_true();
		Command source = knobCc(1, 74);
		source.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		expect(held(*f.song, knob)).to_equal(source);
		expect(f.song->project.mappings.find(knob)->relative).to_be_true();
		const std::vector<deluge::midi::IndexedEntry> played = indexedKnobs(*f.song, knob.owner, 1, 74);
		expect(played.size()).to_equal(size_t{1}, "the knobs on CC 74");
		if (played.size() == 1) {
			expect(played[0].cable).to_equal(&MIDIDeviceManager::dinMIDIPorts);
		}

		// The menu unlearns through the song's table (`HostLearnSink::learn` in `shell_host/learn_sink.cpp`, with no command).
		deluge::midi::takeoverStore().at(knob, source).previous = 31;
		deluge::edits::unlearnMappingFor(knob);
		expect(storeSaw(knob, source, 31)).to_be_false();
		expect(f.song->project.mappings.find(knob)).to_be_null();
		expect(indexedKnobs(*f.song, knob.owner, 1, 74).size()).to_equal(size_t{0}, "the knobs on CC 74");
	});

	it("rebinds a menu's knob heard again from another device while inputs are not told apart", _ {
		const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
		MIDIDeviceManager::differentiatingInputsByDevice = false;
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const deluge::modulation::KnobTarget volume = deluge::modulation::knobParam(params::LOCAL_VOLUME);
		const deluge::modulation::KnobTarget pan = deluge::modulation::knobParam(params::LOCAL_PAN);
		const bool learned = deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1)
		                     && deluge::edits::learnKnobFor(&sound, pan, &MIDIDeviceManager::dinMIDIPorts, 75, 1)
		                     && deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::upstreamUSBMIDICable1,
		                                                    74, 1);
		MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
		expect(learned).to_be_true();
		const std::vector<Mapping> knobs = f.song->project.mappings.params_of(owner);
		Command fromUsb = knobCc(1, 74);
		fromUsb.device = MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_1};
		expect(static_cast<int>(knobs.size())).to_equal(2);
		expect(knobs.size()).to_equal(size_t{2});
		if (knobs.size() == 2) {
			expect(knobs[0].source).to_equal(fromUsb);
			expect(std::get<OwnerParamTarget>(knobs[0].target).param).to_equal(patched(params::LOCAL_VOLUME));
		}
		const std::vector<deluge::midi::IndexedEntry> played = indexedKnobs(*f.song, owner, 1, 74);
		expect(played.size()).to_equal(size_t{1}, "the knobs on CC 74");
		if (played.size() == 1) {
			expect(played[0].cable).to_equal(&MIDIDeviceManager::upstreamUSBMIDICable1);
		}
	});

	it("rebinds a menu's knob learned from any device to the device it is heard from", _ {
		const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
		MIDIDeviceManager::differentiatingInputsByDevice = true;
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		const deluge::modulation::KnobTarget volume = deluge::modulation::knobParam(params::LOCAL_VOLUME);
		deluge::edits::learnMappingFor(knobCc(1, 74), knob);
		const bool learned = deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1);
		MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
		expect(learned).to_be_true();
		Command fromDin = knobCc(1, 74);
		fromDin.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		const std::vector<Mapping> knobs =
		    f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *f.instrument->id()});
		expect(static_cast<int>(knobs.size())).to_equal(1);
		expect(knobs.size()).to_equal(size_t{1});
		if (knobs.size() == 1) {
			expect(knobs[0].source).to_equal(fromDin);
		}
		const std::vector<deluge::midi::IndexedEntry> played = indexedKnobs(*f.song, knob.owner, 1, 74);
		expect(played.size()).to_equal(size_t{1}, "the knobs on CC 74");
		if (played.size() == 1) {
			expect(played[0].cable).to_equal(&MIDIDeviceManager::dinMIDIPorts);
		}
	});

	it("keeps a menu's knobs from two devices apart while inputs are told apart", _ {
		const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
		MIDIDeviceManager::differentiatingInputsByDevice = true;
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const deluge::modulation::KnobTarget volume = deluge::modulation::knobParam(params::LOCAL_VOLUME);
		const bool learned = deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1)
		                     && deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::upstreamUSBMIDICable1,
		                                                    74, 1);
		MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
		expect(learned).to_be_true();
		expect(static_cast<int>(
		           f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *f.instrument->id()}).size()))
		    .to_equal(2);
		const std::vector<deluge::midi::IndexedEntry> played =
		    indexedKnobs(*f.song, {ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, 1, 74);
		expect(played.size()).to_equal(size_t{2}, "the knobs on CC 74");
		if (played.size() == 2) {
			expect(played[0].cable).to_equal(&MIDIDeviceManager::dinMIDIPorts);
			expect(played[1].cable).to_equal(&MIDIDeviceManager::upstreamUSBMIDICable1);
		}
	});

	it("keeps a menu knob's takeover state when it is learned again from another device", _ {
		const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
		MIDIDeviceManager::differentiatingInputsByDevice = false;
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const deluge::modulation::KnobTarget volume = deluge::modulation::knobParam(params::LOCAL_VOLUME);
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		Command fromDin = knobCc(1, 74);
		fromDin.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		Command fromUsb = knobCc(1, 74);
		fromUsb.device = MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_1};
		bool learned = deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1);
		if (learned) {
			deluge::midi::takeoverStore().at(knob, fromDin).previous = 12345;
		}
		learned = learned
		          && deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::upstreamUSBMIDICable1, 74, 1);
		MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
		expect(learned).to_be_true();
		const std::vector<deluge::midi::IndexedEntry> played = indexedKnobs(*f.song, knob.owner, 1, 74);
		expect(played.size()).to_equal(size_t{1}, "the knobs on CC 74");
		if (played.size() == 1) {
			expect(played[0].cable).to_equal(&MIDIDeviceManager::upstreamUSBMIDICable1);
		}
		expect(storeSaw(knob, fromUsb, 12345)).to_be_true();
		expect(deluge::midi::takeoverStore().size()).to_equal(size_t{1});
	});

	it("keeps a menu knob's takeover state when it is learned again after it showed itself absolute", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		const deluge::modulation::KnobTarget volume = deluge::modulation::knobParam(params::LOCAL_VOLUME);
		expect(deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1)).to_be_true();
		Command fromDin = knobCc(1, 74);
		fromDin.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		deluge::bridge::mappingNoLongerRelative(knob, fromDin, *f.song); // as an absolute controller's CC shows it
		expect(f.song->project.mappings.find(knob)->relative).to_be_false();
		deluge::midi::takeoverStore().at(knob, fromDin).previous = 777;

		expect(deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1)).to_be_true();
		expect(f.song->project.mappings.find(knob)->relative).to_be_true();
		const std::vector<deluge::midi::IndexedEntry> played = indexedKnobs(*f.song, knob.owner, 1, 74);
		expect(played.size()).to_equal(size_t{1}, "the knobs on CC 74");
		if (played.size() == 1) {
			expect(played[0].relative).to_be_true();
		}
		expect(storeSaw(knob, fromDin, 777)).to_be_true();
	});

	it("collapses a menu knob relearned onto a device another knob of the param already plays from", _ {
		const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
		MIDIDeviceManager::differentiatingInputsByDevice = true;
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const OwnerParamTarget knob{owner, patched(params::LOCAL_VOLUME)};
		const deluge::modulation::KnobTarget volume = deluge::modulation::knobParam(params::LOCAL_VOLUME);
		Command fromDin = knobCc(1, 74);
		fromDin.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		deluge::edits::learnMappingFor(knobCc(1, 74), knob);
		deluge::edits::learnMappingFor(fromDin, knob);
		expect(static_cast<int>(f.song->project.mappings.params_of(owner).size())).to_equal(2);
		// The knob relearned never saw a position, the one it collapses onto did: the survivor starts over.
		deluge::midi::takeoverStore().at(knob, fromDin).previous = 5;
		const bool learned = deluge::edits::learnKnobFor(&sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1);
		MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
		expect(learned).to_be_true();
		const std::vector<Mapping> knobs = f.song->project.mappings.params_of(owner);
		expect(static_cast<int>(knobs.size())).to_equal(1);
		expect(knobs.size()).to_equal(size_t{1});
		if (knobs.size() == 1) {
			expect(knobs[0].source).to_equal(fromDin);
		}
		expect(indexedKnobs(*f.song, owner, 1, 74).size()).to_equal(size_t{1});
		expect(storeHolds(knob, fromDin)).to_be_false();
	});

	it("keeps a knob's takeover state when its device's saved name differs in case from the cable's", _ {
		spec::SongFixture f = spec::make_synth_song();
		SpecCable& cable = renamedController();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_PAN)};
		Command source = knobCc(2, 11);
		source.device = MidiDeviceRef{
		    .port = MidiDeviceRef::Port::HOSTED, .name = "mw", .vendor_id = 0x9999, .product_id = 0x8888};
		deluge::edits::learnMappingFor(source, knob);
		const std::vector<deluge::midi::IndexedEntry> played = indexedKnobs(*f.song, knob.owner, 2, 11);
		expect(played.size()).to_equal(size_t{1}, "the knobs on CC 11");
		if (played.size() == 1) {
			expect(played[0].cable).to_equal(&cable);
		}
		deluge::midi::takeoverStore().at(knob, source).previous = 4242;
		deluge::bridge::projectDeviceMappings(*f.song); // the devices changed
		expect(storeSaw(knob, source, 4242)).to_be_true();
		expect(held(*f.song, knob)).to_equal(source);
	});

	it("lands a write the render held after a kit change's table as it was, after the rows' moves", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		const uint32_t id = *drum->id();
		deluge::edits::learnMappingFor(note(1, 36), DrumMuteTarget{id});
		auto* kit = static_cast<Kit*>(f.instrument);
		deluge::bridge::applyPendingMappingOps(*f.song);
		const deluge::project::MappingTable before = f.song->project.mappings;
		deluge::memory::noteRenderBlock(true); // the song's tick, on the render
		deluge::bridge::forgetDrumMappings(*f.song, id);
		deluge::memory::noteRenderBlock(false);
		deluge::bridge::recordMovedDrumCommands(*kit, before.entries(), {}, *f.song);
		deluge::bridge::applyPendingMappingOps(*f.song);
		expect(f.song->project.mappings.find(DrumMuteTarget{id})).to_be_null();
	});

	it("holds the render's writes while any row-command guard is open, and lands them when the last closes", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		const uint32_t id = *drum->id();
		deluge::edits::learnMappingFor(note(1, 36), DrumMuteTarget{id});
		deluge::bridge::applyPendingMappingOps(*f.song);
		{
			deluge::bridge::RowCommandMoves outer(*f.song);
			deluge::memory::noteRenderBlock(true); // the song's tick, on the render
			deluge::bridge::forgetDrumMappings(*f.song, id);
			deluge::memory::noteRenderBlock(false);
			deluge::bridge::applyPendingMappingOps(*f.song);
			expect(f.song->project.mappings.find(DrumMuteTarget{id})).not_().to_be_null();
			{
				deluge::bridge::RowCommandMoves inner(*f.song);
				expect(inner.before().size()).to_equal(size_t{1}); // the table as the forget found it
			}
			expect(f.song->project.mappings.find(DrumMuteTarget{id})).not_().to_be_null(); // the outer still open
		}
		expect(f.song->project.mappings.find(DrumMuteTarget{id})).to_be_null();
	});

	it("plays a learned knob through the index for its CC and param, and for no other", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const OwnerParamTarget volume{owner, patched(params::LOCAL_VOLUME)};
		const OwnerParamTarget pan{owner, patched(params::LOCAL_PAN)};
		deluge::edits::learnMappingFor(knobCc(1, 74), volume);
		expect(indexPlays(*f.song, volume, 1, 74)).to_be_true();
		expect(indexPlays(*f.song, pan, 1, 74)).to_be_false();
		expect(indexedKnobs(*f.song, owner, 1, 75).empty()).to_be_true();
		expect(indexedKnobs(*f.song, owner, 2, 74).empty()).to_be_true();

		const OwnerParamTarget songVolume{{ControlsAddress::Kind::SONG, 0},
		                             unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		deluge::edits::learnMappingFor(knobCc(0, 7), songVolume);
		expect(indexPlays(*f.song, songVolume, 0, 7)).to_be_true();
	});

	it("plays a sound row's knob and a kit's global knob through the index, each for its own CC", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		const OwnerParamTarget rowParam{{ControlsAddress::Kind::DRUM, *drum->id()},
		                           unpatched(params::Kind::UNPATCHED_SOUND, params::UNPATCHED_PORTAMENTO)};
		deluge::edits::learnMappingFor(knobCc(1, 20), rowParam);
		expect(indexPlays(*f.song, rowParam, 1, 20)).to_be_true();
		auto* kit = static_cast<Kit*>(f.instrument);
		const OwnerParamTarget kitParam{{ControlsAddress::Kind::OUTPUT, *kit->id()},
		                           unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ)};
		deluge::edits::learnMappingFor(knobCc(2, 21), kitParam);
		expect(indexPlays(*f.song, kitParam, 2, 21)).to_be_true();
		expect(indexedKnobs(*f.song, kitParam.owner, 2, 22).size()).to_equal(size_t{0}, "the kit's knobs on CC 22");
		// The kit's knob is not its row's.
		expect(indexedKnobs(*f.song, rowParam.owner, 2, 21).size()).to_equal(size_t{0}, "the row's knobs on CC 21");
	});

	it("names no engine knob for a param outside the engine's, and plays an engine param's knob as that param's", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const OwnerParamTarget volume{owner, patched(params::LOCAL_VOLUME)};
		OwnerParamTarget outside = volume;
		outside.param.space = deluge::project::ParamSpace::MIDI_CC;
		expect(deluge::bridge::knobOfControl(volume.param, params::Kind::UNPATCHED_SOUND).has_value()).to_be_true();
		expect(deluge::bridge::knobOfControl(outside.param, params::Kind::UNPATCHED_SOUND).has_value()).to_be_false();
		deluge::edits::learnMappingFor(knobCc(1, 74), volume);
		expect(indexPlays(*f.song, volume, 1, 74)).to_be_true();
		f.song->project.mappings.learn(Mapping{.source = knobCc(1, 74), .target = outside});
		// The index plays each knob as its own target: the one outside the engine's is not taken for volume.
		const std::vector<deluge::midi::IndexedEntry> played = indexedKnobs(*f.song, owner, 1, 74);
		expect(static_cast<int>(played.size())).to_equal(2);
		if (played.size() == 2) {
			expect(played[0].target).to_equal(MappingTarget{volume});
			expect(played[1].target).to_equal(MappingTarget{outside});
		}
	});

	it("accepts a param outside the engine's in its owner, and lends it no other knob's takeover state", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		const OwnerParamTarget volume{owner, patched(params::LOCAL_VOLUME)};
		OwnerParamTarget outside = volume;
		outside.param.space = deluge::project::ParamSpace::MIDI_CC;
		deluge::edits::learnMappingFor(knobCc(1, 75), volume);
		deluge::midi::takeoverStore().at(volume, knobCc(1, 75)).previous = 77;
		f.song->project.mappings.learn(Mapping{.source = knobCc(1, 75), .target = outside});
		expect(static_cast<int>(deluge::bridge::projectMapping(*f.song, outside))).to_equal(static_cast<int>(Error::NONE));
		expect(storeSaw(volume, knobCc(1, 75), 77)).to_be_true();
		// A CC on the source moves volume's knob and passes over the one outside, which keeps no state of its own.
		activate(f);
		deluge::midi::refreshMappingIndex();
		deluge::midi::learnedCcToOutput(*f.instrument, MIDIDeviceManager::dinMIDIPorts, 1, 75, 100);
		expect(storeHolds(volume, knobCc(1, 75))).to_be_true();
		expect(storeHolds(outside, knobCc(1, 75))).to_be_false();
		OwnerParamTarget elsewhere = outside;
		elsewhere.owner.id = 0xBEEF; // an output the song does not hold
		expect(static_cast<int>(deluge::bridge::projectMapping(*f.song, elsewhere)))
		    .to_equal(static_cast<int>(Error::UNSPECIFIED));
	});

	it("leaves the index fresh when a song that is not current gains a session clip or a kit row", _ {
		spec::SongFixture background = spec::make_kit_song();
		const spec::SongFixture f = spec::make_synth_song();
		deluge::midi::refreshMappingIndex();
		expect(deluge::mappingIndexStale()).to_be_false();
		(void)spec::detail::make_session_clip(background.song);
		expect(deluge::mappingIndexStale()).to_be_false();
		static_cast<Kit*>(background.instrument)->addDrum(makeLooseMidiDrum(), background.song);
		expect(deluge::mappingIndexStale()).to_be_false();
		(void)spec::detail::make_session_clip(f.song); // the current song's still marks it
		expect(deluge::mappingIndexStale()).to_be_true();
	});

	it("clears a knob's relative flag in the table when a CC between 16 and 111 shows it is absolute", _ {
		spec::SongFixture f = spec::make_synth_song();
		const OwnerParamTarget knob{{ControlsAddress::Kind::SONG, 0},
		                       unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		Command source = knobCc(0, 7);
		source.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		deluge::edits::learnMappingFor(source, knob);
		expect(f.song->project.mappings.find(knob)->relative).to_be_true();

		deluge::midi::refreshMappingIndex();
		deluge::midi::learnedCcToSong(MIDIDeviceManager::dinMIDIPorts, 0, 7, 64);
		expect(f.song->project.mappings.find(knob)->relative).to_be_false();
	});

	it("leaves a lost write's reconcile, and the index's stale mark, to the next drain when the drain is masked", _ {
		spec::SongFixture f = spec::make_synth_song();
		const TrackInputTarget gone{0xBEEF}; // an output the song does not hold
		f.song->project.mappings.learn(Mapping{.source = note(1, 40), .target = gone});
		deluge::midi::refreshMappingIndex();
		deluge::memory::noteRenderBlock(true); // the song's tick, on the render, loses a write past the queue's room
		for (size_t i = 0; i <= deluge::bridge::kPendingMappingOps; i++) {
			deluge::bridge::forgetClipMappings(*f.song, 0xF00D);
		}
		deluge::memory::noteRenderBlock(false);
		expect(deluge::bridge::applyPendingMappingOps(*f.song, deluge::bridge::OverflowReconcile::DEFER)).to_be_true();
		expect(f.song->project.mappings.find(gone)).not_().to_be_null();
		expect(deluge::mappingIndexStale()).to_be_true();
		deluge::midi::refreshMappingIndex();
		expect(f.song->project.mappings.find(gone)).to_be_null();
		expect(deluge::mappingIndexStale()).to_be_false();
	});

	it("drops the takeover state of a knob learned again from the same source once its relative flag changes", _ {
		spec::SongFixture f = spec::make_synth_song();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(knobCc(1, 74), knob);
		deluge::midi::takeoverStore().at(knob, knobCc(1, 74)).previous = 40;
		deluge::bridge::mappingNoLongerRelative(knob, knobCc(1, 74), *f.song); // an absolute controller's value showed it
		expect(f.song->project.mappings.find(knob)->relative).to_be_false();
		expect(storeSaw(knob, knobCc(1, 74), 40)).to_be_true(); // the flip itself keeps it
		deluge::edits::learnMappingFor(knobCc(1, 74), knob); // learned again: a CC reads as relative once more
		expect(f.song->project.mappings.find(knob)->relative).to_be_true();
		expect(storeHolds(knob, knobCc(1, 74))).to_be_false();
	});

	it("keeps the takeover state of a knob learned again from the same source with its flag unchanged", _ {
		spec::SongFixture f = spec::make_synth_song();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(knobCc(1, 74), knob);
		deluge::midi::takeoverStore().at(knob, knobCc(1, 74)).previous = 40;
		deluge::edits::learnMappingFor(knobCc(1, 74), knob);
		expect(storeSaw(knob, knobCc(1, 74), 40)).to_be_true();
	});

	it("carries no takeover state through a relearn of a param outside the engine's", _ {
		spec::SongFixture f = spec::make_synth_song();
		OwnerParamTarget outside{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		outside.param.space = deluge::project::ParamSpace::MIDI_CC;
		f.song->project.mappings.learn(Mapping{.source = knobCc(1, 75), .target = outside});
		deluge::midi::takeoverStore().at(outside, knobCc(1, 75)).previous = 8;
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::LearnMapping edit{Mapping{.source = knobCc(1, 76), .target = outside}, knobCc(1, 75), std::nullopt};
		expect(static_cast<int>(deluge::edits::apply(ctx, edit))).to_equal(static_cast<int>(Error::NONE));
		expect(storeHolds(outside, knobCc(1, 76))).to_be_false();
	});

	it("gives a clone whose held copy the render lost its source's knobs and the position they last saw", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.output->id()},
		                            unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		deluge::edits::learnMappingFor(fromAbsentDevice(knobCc(0, 7)), knob);
		deluge::midi::takeoverStore().at(knob, fromAbsentDevice(knobCc(0, 7))).previous = 321;
		AudioOutput* clone = f.song->createNewAudioOutput();
		deluge::midi::refreshMappingIndex();
		deluge::memory::noteRenderBlock(true); // the song's tick fills the queue's room, then clones
		for (size_t i = 0; i < deluge::bridge::kPendingMappingOps; i++) {
			deluge::bridge::forgetClipMappings(*f.song, 0xF00D);
		}
		clone->cloneFrom(f.output);
		deluge::bridge::cloneOutputMappings(*f.output, *clone, *f.song);
		deluge::memory::noteRenderBlock(false);
		(void)deluge::bridge::applyPendingMappingOps(*f.song); // the clone's copy is among those lost: the reconcile's
		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *clone->id()});
		expect(static_cast<int>(knobs.size())).to_equal(1);
		OwnerParamTarget cloneKnob = knob;
		cloneKnob.owner.id = *clone->id();
		if (knobs.size() == 1) {
			expect(knobs[0].source).to_equal(fromAbsentDevice(knobCc(0, 7)));
		}
		expect(storeSaw(cloneKnob, fromAbsentDevice(knobCc(0, 7)), 321)).to_be_true();
		expect(clone->effectsOwner()->knobs->cloned_from.has_value()).to_be_false();
	});

	it("gives a lost clone its source's knobs when a forget of the source lands before the deferred reconcile", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const uint32_t source = *f.output->id();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, source},
		                            unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		deluge::edits::learnMappingFor(knobCc(0, 7), knob);
		AudioOutput* clone = f.song->createNewAudioOutput();
		deluge::midi::refreshMappingIndex();
		deluge::memory::noteRenderBlock(true); // the song's tick fills the queue's room, then clones
		for (size_t i = 0; i < deluge::bridge::kPendingMappingOps; i++) {
			deluge::bridge::forgetClipMappings(*f.song, 0xF00D);
		}
		clone->cloneFrom(f.output);
		deluge::bridge::cloneOutputMappings(*f.output, *clone, *f.song);
		deluge::memory::noteRenderBlock(false);
		// A drain that holds the render off defers the reconcile; then the tick's forget of the source is held.
		expect(deluge::bridge::applyPendingMappingOps(*f.song, deluge::bridge::OverflowReconcile::DEFER)).to_be_true();
		deluge::memory::noteRenderBlock(true);
		deluge::bridge::forgetOutputMappings(*f.song, source);
		deluge::memory::noteRenderBlock(false);
		(void)deluge::bridge::applyPendingMappingOps(*f.song);
		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *clone->id()});
		expect(knobs.size()).to_equal(size_t{1});
		if (knobs.size() == 1) {
			expect(knobs[0].source).to_equal(knobCc(0, 7));
		}
		expect(f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, source}).empty()).to_be_true();
	});

	it("copies a clone's knobs once when the render's held copy lands", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.output->id()},
		                            unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		deluge::edits::learnMappingFor(knobCc(0, 7), knob);
		AudioOutput* clone = f.song->createNewAudioOutput();
		deluge::memory::noteRenderBlock(true);
		clone->cloneFrom(f.output);
		deluge::bridge::cloneOutputMappings(*f.output, *clone, *f.song);
		deluge::memory::noteRenderBlock(false);
		expect(clone->effectsOwner()->knobs->cloned_from).to_equal(std::optional<uint32_t>{*f.output->id()});
		(void)deluge::bridge::applyPendingMappingOps(*f.song);
		expect(clone->effectsOwner()->knobs->cloned_from.has_value()).to_be_false();
		expect(static_cast<int>(f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *clone->id()}).size()))
		    .to_equal(1);
	});

	it("copies nothing, and makes no device entry, for a clone that left the song before its held copy lands", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.output->id()},
		                            unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		Command source = knobCc(0, 7);
		source.device = MidiDeviceRef{
		    .port = MidiDeviceRef::Port::HOSTED, .name = "Gone Clone Device", .vendor_id = 0x4321, .product_id = 0x8765};
		f.song->project.mappings.learn(Mapping{.source = source, .target = knob}); // no device entry made for it
		deluge::midi::takeoverStore().at(knob, source).previous = 99;
		AudioOutput* clone = f.song->createNewAudioOutput();
		deluge::memory::noteRenderBlock(true);
		clone->cloneFrom(f.output);
		deluge::bridge::cloneOutputMappings(*f.output, *clone, *f.song);
		deluge::memory::noteRenderBlock(false);
		const uint32_t gone = *clone->id();
		to_be_at_least(expect(f.song->removeOutputFromMainList(clone, false)), 0);
		(void)deluge::bridge::applyPendingMappingOps(*f.song);
		expect(f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, gone}).empty()).to_be_true();
		OwnerParamTarget cloneKnob = knob;
		cloneKnob.owner.id = gone;
		expect(storeHolds(cloneKnob, source)).to_be_false();
		expect(deluge::bridge::find_cable(*source.device)).to_be_null();
	});

	it("starts a song's knobs over when another becomes the current song, whatever its address", _ {
		spec::SongFixture f = spec::make_synth_song();
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(knobCc(1, 74), knob);
		deluge::midi::takeoverStore().at(knob, knobCc(1, 74)).previous = 40;
		expect(storeSaw(knob, knobCc(1, 74), 40)).to_be_true();
		// What deleteOldSongBeforeLoadingNew, the load's swap and a song swap on the tick do at each assignment of the
		// current song: a song built in the memory the old one gave back is told apart by this alone.
		deluge::markSongChanged();
		expect(storeHolds(knob, knobCc(1, 74))).to_be_false();
	});

});
// clang-format on

DELUGE_SPEC(mapping_writers_spec, mapping_writers)
