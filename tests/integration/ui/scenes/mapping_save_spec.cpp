/// @file
/// @brief A song's save writes every MIDI binding from its mapping table: an owner's learned knobs, a clip's and a
///        kit row's mute command, a kit row's and a track's input. What the engine field holds plays no part: a
///        binding the table holds is written even when the engine has lost it, and one it does not hold is not. An
///        owner the song does not hold writes the knobs it holds outside any table, if any.

#include "io/midi/learned_midi.h"
#include "io/midi/midi_device_manager.h"
#include "model/clip/clip.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_instrument.h"
#include "project/mapping_table.h"
#include "serialization/value.h"
#include "storage/storage_manager.h"
#include "storage/value_serializer.h"

#include <optional>
#include <string>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
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
using deluge::project::MidiDeviceRef;
using Command = deluge::project::MidiCommand;
using deluge::project::OwnerParamTarget;
using deluge::project::TrackInputTarget;
using deluge::serialization::Value;

namespace {

/// @brief A command on @p channel (a CC is told apart by its encoded channel, a knob's by its key).
Command command(uint8_t channel, uint8_t noteOrCC) {
	return Command{.channel_or_zone = channel, .note_or_cc = noteOrCC};
}

/// @brief A patched param of a sound, as the table names it.
deluge::project::ParamTarget patched(int32_t p) {
	return deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(p), params::Kind::UNPATCHED_SOUND).id.value);
}

/// @brief The element one write produced: the one item of the list it was written into, unboxed.
std::optional<Value> elementOf(std::optional<Value> items) {
	if (!items || !items->isArray() || items->items().size() != 1) {
		return std::nullopt;
	}
	const Value& boxed = items->items().front();
	if (!boxed.isMap() || boxed.entries().size() != 1) {
		return std::nullopt;
	}
	return boxed.entries().front().second;
}

/// @brief @p output's element as a song save writes it.
std::optional<Value> savedOutput(Output& output, ::Song& song) {
	ValueSerializer writer(ValueSerializer::Writes::ITEMS);
	{
		ActiveWriterScope scope(writer);
		output.writeToFile(nullptr, &song);
	}
	return elementOf(writer.finish());
}

/// @brief @p clip's MIDI commands as a song save writes them into its element.
std::optional<Value> savedClip(Clip& clip, ::Song& song) {
	ValueSerializer writer(ValueSerializer::Writes::ITEMS);
	{
		ActiveWriterScope scope(writer);
		writer.writeOpeningTagBeginning("clip", true);
		writer.writeAttribute("id", clip.id());
		writer.writeOpeningTagEnd();
		clip.writeMidiCommandsToFile(writer, &song);
		writer.writeClosingTag("clip", true, true);
	}
	return elementOf(writer.finish());
}

/// @brief The text of @p key in @p element, or empty.
std::string text(const Value* element, std::string_view key) {
	const Value* value = element != nullptr ? element->find(key) : nullptr;
	return value != nullptr && value->isScalar() ? value->str() : std::string{};
}

/// @brief The @p index-th knob of @p element's `midiKnobs`, unboxed, or nullptr.
const Value* knobAt(const Value& element, size_t index) {
	const Value* knobs = element.find("midiKnobs");
	if (knobs == nullptr || !knobs->isArray() || knobs->items().size() <= index) {
		return nullptr;
	}
	return knobs->items()[index].find("midiKnob");
}

/// @brief The kit row element of @p kit's save, the first in `soundSources`, unboxed, or nullptr.
const Value* firstRow(const Value& kit) {
	const Value* sources = kit.find("soundSources");
	if (sources == nullptr || !sources->isArray() || sources->items().empty()) {
		return nullptr;
	}
	const Value& boxed = sources->items().front();
	return boxed.isMap() && boxed.entries().size() == 1 ? &boxed.entries().front().second : nullptr;
}

} // namespace

// clang-format off
describe mapping_save("a song's save of its MIDI bindings", $ {
	it("saves a knob the table holds: its channel, CC, flag and param", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		f.song->project.mappings.learn(
		    Mapping{.source = command(2, 74), .target = OwnerParamTarget{owner, patched(params::LOCAL_VOLUME)}});
		std::optional<Value> saved = savedOutput(*f.instrument, *f.song);
		expect(saved.has_value()).to_be_true();
		const Value* knob = knobAt(*saved, 0);
		expect(knob).not_().to_be_null();
		expect(text(knob, "channel")).to_equal(std::string{"2"});
		expect(text(knob, "ccNumber")).to_equal(std::string{"74"});
		expect(text(knob, "relative")).to_equal(std::string{"1"});
		expect(text(knob, "controlsParam")).to_equal(std::string{"volume"});
	});

	it("saves no knob for a param outside the engine's, and no empty list when it is the only one", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		OwnerParamTarget outside{owner, patched(params::LOCAL_VOLUME)};
		outside.param.space = deluge::project::ParamSpace::MIDI_CC;
		f.song->project.mappings.learn(Mapping{.source = command(2, 74), .target = outside});
		std::optional<Value> saved = savedOutput(*f.instrument, *f.song);
		expect(saved.has_value()).to_be_true();
		if (saved.has_value()) {
			expect(saved->find("midiKnobs")).to_be_null();
		}
		f.song->project.mappings.learn(
		    Mapping{.source = command(0, 10), .target = OwnerParamTarget{owner, patched(params::LOCAL_PAN)}});
		saved = savedOutput(*f.instrument, *f.song);
		expect(saved.has_value()).to_be_true();
		if (saved.has_value()) {
			const Value* knobs = saved->find("midiKnobs");
			expect(knobs).not_().to_be_null();
			if (knobs != nullptr) {
				expect(knobs->items().size()).to_equal(size_t{1}, "the saved knob count");
			}
		}
	});

	it("saves an owner's knobs in table order, and none it holds outside the table while the song holds it", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		f.song->project.mappings.learn(
		    Mapping{.source = command(0, 11), .target = OwnerParamTarget{owner, patched(params::LOCAL_PAN)}});
		f.song->project.mappings.learn(
		    Mapping{.source = command(0, 10), .target = OwnerParamTarget{owner, patched(params::LOCAL_VOLUME)}});
		sound.effectsOwner()->knobs->loaded.push_back(Mapping{.source = command(5, 99), // as a file read it, which no table took
		                                     .target = OwnerParamTarget{{}, patched(params::LOCAL_PITCH_ADJUST)}});
		std::optional<Value> saved = savedOutput(*f.instrument, *f.song);
		expect(saved.has_value()).to_be_true();
		expect(text(knobAt(*saved, 0), "ccNumber")).to_equal(std::string{"11"});
		expect(text(knobAt(*saved, 1), "ccNumber")).to_equal(std::string{"10"});
		expect(knobAt(*saved, 2)).to_be_null();
	});

	it("saves the name of a device that is not connected", _ {
		spec::SongFixture f = spec::make_synth_song();
		Command mute = command(3, 40);
		mute.device = MidiDeviceRef{
		    .port = MidiDeviceRef::Port::HOSTED, .name = "No Such Device", .vendor_id = 0x1234, .product_id = 0x5678};
		f.song->project.mappings.learn(Mapping{.source = mute, .target = ClipMuteTarget{f.clip->id()}});
		f.clip->muteMIDICommand.clear();
		std::optional<Value> saved = savedClip(*f.clip, *f.song);
		expect(saved.has_value()).to_be_true();
		const Value* written = saved->find("muteMidiCommand");
		expect(text(written, "channel")).to_equal(std::string{"3"});
		expect(text(written, "note")).to_equal(std::string{"40"});
		const Value* device = written != nullptr ? written->find("device") : nullptr;
		expect(text(device, "name")).to_equal(std::string{"No Such Device"});
		expect(text(device, "vendorId")).to_equal(std::string{"0x1234"});
	});

	it("saves no clip mute the table does not hold", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.clip->muteMIDICommand.channelOrZone = 3;
		f.clip->muteMIDICommand.noteOrCC = 40;
		std::optional<Value> saved = savedClip(*f.clip, *f.song);
		expect(saved.has_value()).to_be_true();
		expect(saved->find("muteMidiCommand")).to_be_null();
	});

	it("saves a kit row's mute and input, and the kit's input with its root note", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		Command input = command(4, 51);
		input.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		f.song->project.mappings.learn(Mapping{.source = command(1, 50), .target = DrumMuteTarget{*drum->id()}});
		f.song->project.mappings.learn(Mapping{.source = input, .target = LaneInputTarget{*drum->id()}});
		f.song->project.mappings.learn(
		    Mapping{.source = command(5, 36), .target = TrackInputTarget{*f.instrument->id()}});
		drum->muteMIDICommand.clear();
		drum->midiInput.clear();
		f.instrument->midiInput.clear();
		std::optional<Value> saved = savedOutput(*f.instrument, *f.song);
		expect(saved.has_value()).to_be_true();
		const Value* kitInput = saved->find("MIDIInput");
		expect(text(kitInput, "channel")).to_equal(std::string{"5"});
		expect(text(kitInput, "note")).to_equal(std::string{"36"});
		const Value* row = firstRow(*saved);
		expect(row).not_().to_be_null();
		expect(text(row != nullptr ? row->find("midiMuteCommand") : nullptr, "note")).to_equal(std::string{"50"});
		const Value* rowInput = row != nullptr ? row->find("midiInput") : nullptr;
		expect(text(rowInput, "note")).to_equal(std::string{"51"});
		expect(text(rowInput != nullptr ? rowInput->find("device") : nullptr, "port")).to_equal(std::string{"din"});
	});

	it("saves a knob from the table of the song being saved when another song is current", _ {
		spec::SongFixture f = spec::make_synth_song();
		const spec::SongFixture current = spec::make_synth_song(); // the most recently built song is current
		expect(currentSong).to_equal(current.song);
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *f.instrument->id()};
		f.song->project.mappings.learn(
		    Mapping{.source = command(2, 74), .target = OwnerParamTarget{owner, patched(params::LOCAL_VOLUME)}});
		std::optional<Value> saved = savedOutput(*f.instrument, *f.song);
		expect(saved.has_value()).to_be_true();
		expect(text(saved ? knobAt(*saved, 0) : nullptr, "ccNumber")).to_equal(std::string{"74"});
	});

	it("saves a kit row's mute and input from the table of the song being saved when another song is current", _ {
		spec::SongFixture f = spec::make_kit_song();
		MIDIDrum* drum = spec::add_drum<MIDIDrum>(f);
		const spec::SongFixture current = spec::make_kit_song();
		expect(currentSong).to_equal(current.song);
		f.song->project.mappings.learn(Mapping{.source = command(1, 50), .target = DrumMuteTarget{*drum->id()}});
		f.song->project.mappings.learn(Mapping{.source = command(4, 51), .target = LaneInputTarget{*drum->id()}});
		drum->muteMIDICommand.clear();
		drum->midiInput.clear();
		std::optional<Value> saved = savedOutput(*f.instrument, *f.song);
		expect(saved.has_value()).to_be_true();
		const Value* row = saved ? firstRow(*saved) : nullptr;
		expect(text(row != nullptr ? row->find("midiMuteCommand") : nullptr, "note")).to_equal(std::string{"50"});
		expect(text(row != nullptr ? row->find("midiInput") : nullptr, "note")).to_equal(std::string{"51"});
	});

	it("saves no knob, and its own input, for an owner the song being saved does not hold and that holds no knob", _ {
		spec::SongFixture saving = spec::make_synth_song();
		spec::SongFixture other = spec::make_synth_song(); // its output is the song-less one
		auto& sound = *static_cast<SoundInstrument*>(other.instrument);
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *other.instrument->id()};
		// The saving song's table names the same id, with other bindings: none of them is this owner's to write.
		saving.song->project.mappings.learn(
		    Mapping{.source = command(0, 11), .target = OwnerParamTarget{owner, patched(params::LOCAL_PAN)}});
		saving.song->project.mappings.learn(Mapping{.source = command(3, 36), .target = TrackInputTarget{owner.id}});
		expect(sound.effectsOwner()->knobs->loaded.empty()).to_be_true(); // it holds no knob outside a table
		other.instrument->midiInput.channelOrZone = 6;
		std::optional<Value> saved = savedOutput(*other.instrument, *saving.song);
		expect(saved.has_value()).to_be_true();
		if (saved.has_value()) {
			expect(saved->find("midiKnobs")).to_be_null();
		}
		expect(text(&*saved, "inputMidiChannel")).to_equal(std::string{"6"});
	});

	it("saves the knobs an owner the song being saved does not hold keeps outside the table", _ {
		spec::SongFixture saving = spec::make_synth_song();
		spec::SongFixture other = spec::make_synth_song(); // its output is one the saving song does not hold
		auto& sound = *static_cast<SoundInstrument*>(other.instrument);
		sound.effectsOwner()->knobs->loaded.push_back(Mapping{.source = command(5, 99), // as its file read it, before a table took it
		                                     .target = OwnerParamTarget{{}, patched(params::LOCAL_PITCH_ADJUST)},
		                                     .relative = true});
		std::optional<Value> saved = savedOutput(*other.instrument, *saving.song);
		expect(saved.has_value()).to_be_true();
		const Value* knob = saved ? knobAt(*saved, 0) : nullptr;
		expect(text(knob, "channel")).to_equal(std::string{"5"});
		expect(text(knob, "ccNumber")).to_equal(std::string{"99"});
		expect(text(knob, "relative")).to_equal(std::string{"1"});
		expect(text(knob, "controlsParam")).to_equal(std::string{"pitch"});
		if (saved.has_value()) {
			expect(knobAt(*saved, 1)).to_be_null();
		}
	});

	it("saves a synth's input zone and device", _ {
		spec::SongFixture f = spec::make_synth_song();
		Command input = command(MIDI_CHANNEL_MPE_LOWER_ZONE, Command::kNone);
		input.device = MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_2};
		f.song->project.mappings.learn(Mapping{.source = input, .target = TrackInputTarget{*f.instrument->id()}});
		f.instrument->midiInput.clear();
		std::optional<Value> saved = savedOutput(*f.instrument, *f.song);
		expect(saved.has_value()).to_be_true();
		expect(text(&*saved, "inputMPEZone")).to_equal(std::string{"lower"});
		expect(text(saved->find("inputMidiDevice"), "port")).to_equal(std::string{"upstreamUSB2"});
	});
});
// clang-format on

DELUGE_SPEC(mapping_save_spec, mapping_save)
