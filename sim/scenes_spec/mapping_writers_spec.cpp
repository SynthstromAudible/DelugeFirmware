/// @file
/// @brief The song's mapping table through the runtime's own structural steps: a preset change, a clone, a delete, an
///        owner arriving in a running song, a kit change, and a knob found not to be relative. After each, the table
///        holds what the engine plays.

#include "deluge-project/mapping_table.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/learned_midi.h"
#include "io/midi/midi_device_manager.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/midi_drum.h"
#include "model/edits/learn_mapping.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"

#include <vector>

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
using deluge::project::MappingTarget;
using deluge::project::MidiDeviceRef;
using Command = deluge::project::MidiCommand;
using deluge::project::ParamTarget;
using deluge::project::TrackInputTarget;

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
deluge::engine::ParamId patched(int32_t p) {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(p);
	return deluge::bridge::knobTargetOf(descriptor, params::Kind::UNPATCHED_SOUND);
}

/// @brief An unpatched param of @p kind, as the table names it.
deluge::engine::ParamId unpatched(params::Kind kind, int32_t p) {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(params::UNPATCHED_START + p);
	return deluge::bridge::knobTargetOf(descriptor, kind);
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
	GlobalEffectableForClip::initParams(&params);
	auto* kit = static_cast<Kit*>(StorageManager::createNewInstrument(OutputType::KIT, &params));
	song.live.param_backup.backUp(kit, nullptr, &params, true);
	return kit;
}

/// @brief Make @p f's clip its instrument's active clip, as a song's own load does: a preset change reads the
///        replaced instrument's params through it.
void activate(spec::SongFixture& f) {
	char memory[MODEL_STACK_MAX_SIZE];
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
		const ParamTarget knob{{ControlsAddress::Kind::OUTPUT, oldId}, patched(params::LOCAL_VOLUME)};
		deluge::edits::learnMappingFor(knobCc(1, 74), knob);
		deluge::edits::learnMappingFor(note(2, 60), TrackInputTarget{oldId});

		SoundInstrument* preset = makeLooseSynth(*f.song);
		f.song->replaceInstrument(f.instrument, preset);
		expect(preset->id().has_value()).to_be_true();
		const uint32_t newId = *preset->id();

		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, newId});
		expect(static_cast<int>(knobs.size())).to_equal(1);
		expect(knobs.size() == 1 && knobs[0].source == knobCc(1, 74)).to_be_true();
		expect(held(*f.song, TrackInputTarget{newId}) == note(2, 60)).to_be_true();
		expect(f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, oldId}).empty()).to_be_true();
		expect(f.song->project.mappings.find(TrackInputTarget{oldId}) == nullptr).to_be_true();
		expect(static_cast<int>(preset->midi_knobs.size())).to_equal(1);
		expect(preset->midiInput.equalsNoteOrCC(nullptr, 2, 60)).to_be_true();
	});

	it("keeps a preset's own knobs when the track it replaces had none", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(f);
		SoundInstrument* preset = makeLooseSynth(*f.song);
		ParamDescriptor volume;
		volume.setToHaveParamOnly(params::LOCAL_VOLUME);
		MIDIKnob& read = preset->midi_knobs.emplace_back(); // as the preset's file read it
		read.midiInput.channelOrZone = 3;
		read.midiInput.noteOrCC = 21;
		read.relative = true;
		read.paramDescriptor = volume;
		f.song->replaceInstrument(f.instrument, preset);
		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *preset->id()});
		expect(static_cast<int>(knobs.size())).to_equal(1);
		expect(knobs.size() == 1 && knobs[0].source == knobCc(3, 21)).to_be_true();
	});

	it("records the input and knobs a track brings into a running song", _ {
		spec::SongFixture f = spec::make_synth_song();
		SoundInstrument* synth = makeLooseSynth(*f.song);
		synth->midiInput.channelOrZone = 5; // as its preset's file read them
		synth->midiInput.noteOrCC = 255;
		ParamDescriptor pan;
		pan.setToHaveParamOnly(params::LOCAL_PAN);
		MIDIKnob& read = synth->midi_knobs.emplace_back();
		read.midiInput.channelOrZone = 4;
		read.midiInput.noteOrCC = 10;
		read.relative = true;
		read.paramDescriptor = pan;
		f.song->addOutput(synth);
		const uint32_t id = *synth->id();
		expect(fieldMatches(*f.song, TrackInputTarget{id}, synth->midiInput)).to_be_true();
		expect(f.song->project.mappings.find(TrackInputTarget{id}) != nullptr).to_be_true();
		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, id});
		expect(knobs.size() == 1 && knobs[0].source == knobCc(4, 10)).to_be_true();
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
		expect(held(*f.song, DrumMuteTarget{id}) == note(6, 30)).to_be_true();
		expect(held(*f.song, LaneInputTarget{id}) == note(7, 31)).to_be_true();
	});

	it("copies an audio track's knobs to its clone, device included", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const ParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.output->id()},
		                       unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		deluge::edits::learnMappingFor(fromAbsentDevice(knobCc(0, 7)), knob);
		AudioOutput* clone = f.song->createNewAudioOutput();
		clone->cloneFrom(f.output);
		deluge::bridge::cloneOutputMappings(*f.output, *clone, *f.song);
		const auto knobs = f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *clone->id()});
		expect(static_cast<int>(knobs.size())).to_equal(1);
		expect(knobs.size() == 1 && knobs[0].source == fromAbsentDevice(knobCc(0, 7))).to_be_true();
		expect(static_cast<int>(clone->midi_knobs.size())).to_equal(1);
	});

	it("copies a clip's mute command to the audio clip that replaces it", _ {
		spec::SongFixture f = spec::make_synth_song();
		const uint32_t oldId = f.clip->id();
		deluge::edits::learnMappingFor(note(3, 40), ClipMuteTarget{oldId});
		Clip* audio = f.song->replaceInstrumentClipWithAudioClip(f.clip, 0);
		expect(audio != nullptr).to_be_true();
		if (audio == nullptr) {
			return;
		}
		expect(held(*f.song, ClipMuteTarget{audio->id()}) == note(3, 40)).to_be_true();
		expect(audio->muteMIDICommand.equalsNoteOrCC(nullptr, 3, 40)).to_be_true();
		expect(f.song->project.mappings.find(ClipMuteTarget{oldId}) == nullptr).to_be_true();
	});

	it("forgets a deleted clip's mute command", _ {
		spec::SongFixture f = spec::make_synth_song();
		const uint32_t id = f.clip->id();
		deluge::edits::learnMappingFor(note(3, 41), ClipMuteTarget{id});
		ClipArray& clips = f.song->live.clips.session_clips;
		clips.erase(clips.begin() + clips.getIndexForClip(f.clip));
		f.song->deleteClipObject(f.clip, false, InstrumentRemoval::NONE);
		expect(f.song->project.mappings.find(ClipMuteTarget{id}) == nullptr).to_be_true();
	});

	it("forgets a deleted kit row's mute and input", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		const uint32_t id = *drum->id();
		deluge::edits::learnMappingFor(note(1, 50), DrumMuteTarget{id});
		deluge::edits::learnMappingFor(note(1, 51), LaneInputTarget{id});
		auto* kit = static_cast<Kit*>(f.instrument);
		NoteRow* row = f.clip->getNoteRowForDrum(drum);
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
		row->setDrum(nullptr, kit, stack->addNoteRow(f.clip->getNoteRowId(row, 0), row));
		kit->removeDrum(drum);
		expect(f.song->project.mappings.find(DrumMuteTarget{id}) == nullptr).to_be_true();
		expect(f.song->project.mappings.find(LaneInputTarget{id}) == nullptr).to_be_true();
	});

	it("forgets a deleted track's knobs and input", _ {
		spec::SongFixture f = spec::make_synth_song();
		SoundInstrument* synth = makeLooseSynth(*f.song);
		f.song->addOutput(synth);
		const uint32_t id = *synth->id();
		deluge::edits::learnMappingFor(knobCc(0, 70), ParamTarget{{ControlsAddress::Kind::OUTPUT, id},
		                                                            patched(params::LOCAL_VOLUME)});
		deluge::edits::learnMappingFor(note(0, 60), TrackInputTarget{id});
		f.song->deleteOutputThatIsInMainList(synth);
		expect(f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, id}).empty()).to_be_true();
		expect(f.song->project.mappings.find(TrackInputTarget{id}) == nullptr).to_be_true();
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
		expect(held(*f.song, DrumMuteTarget{*rows[0]->id()}) == fromAbsentDevice(note(1, 36))).to_be_true();
		expect(held(*f.song, ClipMuteTarget{f.clip->id()}) == note(1, 36)).to_be_true();
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
		char memory[MODEL_STACK_MAX_SIZE];
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
		const ParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_PAN)};
		Command source = knobCc(2, 9);
		source.device = MidiDeviceRef{
		    .port = MidiDeviceRef::Port::HOSTED, .name = "MW", .vendor_id = 0x0001, .product_id = 0x0002};
		deluge::edits::learnMappingFor(source, knob);
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		expect(sound.midi_knobs.size() == 1 && sound.midi_knobs[0].midiInput.cable == &cable).to_be_true();

		char memory[MODEL_STACK_MAX_SIZE];
		sound.offerReceivedCCToLearnedParamsForClip(knob.owner, cable, 2, 9, 64,
		                                            setupModelStackWithTimelineCounter(memory, f.song, f.clip), -1);
		expect(sound.midi_knobs[0].relative).to_be_false();
		expect(f.song->project.mappings.find(knob)->relative).to_be_false();
		expect(held(*f.song, knob) == source).to_be_true();
	});

	it("learns and unlearns a menu's MIDI knob through the table", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const ParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		ParamDescriptor volume;
		volume.setToHaveParamOnly(params::LOCAL_VOLUME);
		expect(deluge::edits::learnKnobFor(sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1)).to_be_true();
		Command source = knobCc(1, 74);
		source.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		expect(held(*f.song, knob) == source).to_be_true();
		expect(f.song->project.mappings.find(knob)->relative).to_be_true();
		expect(static_cast<int>(sound.midi_knobs.size())).to_equal(1);
		expect(sound.midi_knobs.size() == 1 && sound.midi_knobs[0].midiInput.cable == &MIDIDeviceManager::dinMIDIPorts)
		    .to_be_true();

		expect(deluge::edits::unlearnKnobsFor(sound, volume)).to_be_true();
		expect(f.song->project.mappings.find(knob) == nullptr).to_be_true();
		expect(sound.midi_knobs.empty()).to_be_true();
		expect(deluge::edits::unlearnKnobsFor(sound, volume)).to_be_false();
	});

	it("rebinds a menu's knob heard again from another device while inputs are not told apart", _ {
		const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
		MIDIDeviceManager::differentiatingInputsByDevice = false;
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		ParamDescriptor volume;
		volume.setToHaveParamOnly(params::LOCAL_VOLUME);
		ParamDescriptor pan;
		pan.setToHaveParamOnly(params::LOCAL_PAN);
		const bool learned = deluge::edits::learnKnobFor(sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1)
		                     && deluge::edits::learnKnobFor(sound, pan, &MIDIDeviceManager::dinMIDIPorts, 75, 1)
		                     && deluge::edits::learnKnobFor(sound, volume, &MIDIDeviceManager::upstreamUSBMIDICable1,
		                                                    74, 1);
		MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
		expect(learned).to_be_true();
		const std::vector<Mapping> knobs = f.song->project.mappings.params_of(owner);
		Command fromUsb = knobCc(1, 74);
		fromUsb.device = MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_1};
		expect(static_cast<int>(knobs.size())).to_equal(2);
		expect(knobs.size() == 2 && knobs[0].source == fromUsb
		       && std::get<ParamTarget>(knobs[0].target).param == patched(params::LOCAL_VOLUME))
		    .to_be_true();
		expect(static_cast<int>(sound.midi_knobs.size())).to_equal(2);
		expect(sound.midi_knobs.size() == 2
		       && sound.midi_knobs[0].midiInput.cable == &MIDIDeviceManager::upstreamUSBMIDICable1)
		    .to_be_true();
	});

	it("rebinds a menu's knob learned from any device to the device it is heard from", _ {
		const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
		MIDIDeviceManager::differentiatingInputsByDevice = true;
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const ParamTarget knob{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_VOLUME)};
		ParamDescriptor volume;
		volume.setToHaveParamOnly(params::LOCAL_VOLUME);
		deluge::edits::learnMappingFor(knobCc(1, 74), knob);
		const bool learned = deluge::edits::learnKnobFor(sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1);
		MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
		expect(learned).to_be_true();
		Command fromDin = knobCc(1, 74);
		fromDin.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		const std::vector<Mapping> knobs =
		    f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *f.instrument->id()});
		expect(static_cast<int>(knobs.size())).to_equal(1);
		expect(knobs.size() == 1 && knobs[0].source == fromDin).to_be_true();
		expect(sound.midi_knobs.size() == 1 && sound.midi_knobs[0].midiInput.cable == &MIDIDeviceManager::dinMIDIPorts)
		    .to_be_true();
	});

	it("keeps a menu's knobs from two devices apart while inputs are told apart", _ {
		const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
		MIDIDeviceManager::differentiatingInputsByDevice = true;
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		ParamDescriptor volume;
		volume.setToHaveParamOnly(params::LOCAL_VOLUME);
		const bool learned = deluge::edits::learnKnobFor(sound, volume, &MIDIDeviceManager::dinMIDIPorts, 74, 1)
		                     && deluge::edits::learnKnobFor(sound, volume, &MIDIDeviceManager::upstreamUSBMIDICable1,
		                                                    74, 1);
		MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
		expect(learned).to_be_true();
		expect(static_cast<int>(
		           f.song->project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *f.instrument->id()}).size()))
		    .to_equal(2);
		expect(static_cast<int>(sound.midi_knobs.size())).to_equal(2);
	});

	it("clears a knob's relative flag in the table when a CC between 16 and 111 shows it is absolute", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ParamTarget knob{{ControlsAddress::Kind::SONG, 0},
		                       unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		Command source = knobCc(0, 7);
		source.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		deluge::edits::learnMappingFor(source, knob);
		expect(f.song->project.mappings.find(knob)->relative).to_be_true();

		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = f.song->setupModelStackWithSongAsTimelineCounter(memory);
		f.song->live.global_effectable.offerReceivedCCToLearnedParamsForSong(MIDIDeviceManager::dinMIDIPorts, 0, 7,
		                                                                     64, stack);
		expect(f.song->live.global_effectable.midi_knobs[0].relative).to_be_false();
		expect(f.song->project.mappings.find(knob)->relative).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(mapping_writers_spec, mapping_writers)
