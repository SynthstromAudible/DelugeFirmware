/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "deluge-project/mapping_codec.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/song.h"
#include "engines/deluge/cables.h"
#include "engines/deluge/naming.h"
#include "engines/deluge/params.h"
#include "engines/global_effectable/naming.h"
#include "engines/global_effectable/params.h"
#include "serialization/parse_json.h"

#include <cppspec.hpp>

using namespace deluge::project;

namespace {
MidiCommand cc(uint8_t channel, uint8_t number) {
	return MidiCommand{.channel_or_zone = channel, .note_or_cc = number};
}
ParamTarget out_param(uint32_t output, uint32_t param) {
	return ParamTarget{{ControlsAddress::Kind::OUTPUT, output}, deluge::engine::ParamId{param}};
}
Mapping knob(MidiCommand source, ParamTarget target) {
	return Mapping{.source = source, .target = target};
}
deluge::serialization::Value json(std::string_view text) {
	return *deluge::serialization::parseJson(text);
}
/// What an owner's element decodes into, and whether that encodes back to the same element.
struct Trip {
	size_t count{0};
	bool same{false};
	Mapping first;
};
Trip round_trip(std::string_view text, MappingOwner owner, const deluge::engine::ParamNaming* naming = nullptr) {
	const deluge::serialization::Value element = json(text);
	MappingTable table;
	decode_mappings(element, owner, table, naming);
	auto again = deluge::serialization::Value::makeMap();
	encode_mappings(table, owner, again, naming);
	return {.count = table.entries().size(),
	        .same = again == element,
	        .first = table.entries().empty() ? Mapping{} : table.entries()[0]};
}
} // namespace

// clang-format off
describe mapping_table("the song's mapping table", ${
	it("replaces a knob learned again on the same source and param", _ {
		MappingTable table;
		table.learn(knob(cc(1, 5), out_param(1, 2)));
		table.learn(knob(cc(1, 5), out_param(1, 2)));
		expect(table.entries().size()).to_equal(size_t{1});
	});

	it("keeps two sources on one param until the param is unlearned", _ {
		MappingTable table;
		table.learn(knob(cc(1, 5), out_param(1, 2)));
		table.learn(knob(cc(1, 6), out_param(1, 2)));
		table.learn(knob(cc(1, 5), out_param(1, 3)));
		expect(table.entries().size()).to_equal(size_t{3});
		expect(table.find(out_param(1, 2))->source).to_equal(cc(1, 5));
		expect(table.unlearn(out_param(1, 2))).to_equal(size_t{2});
		expect(table.entries().size()).to_equal(size_t{1});
		expect(table.find(out_param(1, 2))).to_equal(static_cast<const Mapping*>(nullptr));
	});

	it("compares a learned knob's device exactly, leaving how MIDI is heard to relearn's caller", _ {
		MappingTable table;
		MidiCommand other = cc(1, 5);
		other.device = MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_1};
		table.learn(knob(cc(1, 5), out_param(1, 2)));
		table.learn(knob(other, out_param(1, 2)));
		expect(table.entries().size()).to_equal(size_t{2});
	});

	it("relearns a knob from another device in its place", _ {
		MappingTable table;
		MidiCommand din = cc(1, 5);
		din.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		MidiCommand usb = cc(1, 5);
		usb.device = MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_1};
		table.learn(knob(din, out_param(1, 2)));
		table.learn(knob(cc(1, 6), out_param(1, 3)));
		expect(table.relearn(din, knob(usb, out_param(1, 2))).relative).to_be_true();
		expect(table.entries().size()).to_equal(size_t{2});
		expect(table.entries()[0].source).to_equal(usb);
		expect(table.entries()[1].source).to_equal(cc(1, 6));
	});

	it("learns a relearned knob whose old source it does not hold", _ {
		MappingTable table;
		table.learn(knob(cc(1, 5), out_param(1, 2)));
		table.relearn(cc(1, 5), knob(cc(1, 7), out_param(1, 3)));
		expect(table.entries().size()).to_equal(size_t{2});
		expect(table.find(out_param(1, 3))->source).to_equal(cc(1, 7));
	});

	it("holds one source per clip mute, the latest", _ {
		MappingTable table;
		table.learn(Mapping{.source = cc(1, 5), .target = ClipMuteTarget{7}});
		table.learn(Mapping{.source = cc(2, 9), .target = ClipMuteTarget{7}});
		expect(table.entries().size()).to_equal(size_t{1});
		expect(table.find(ClipMuteTarget{7})->source).to_equal(cc(2, 9));
		expect(table.find(ClipMuteTarget{8})).to_equal(static_cast<const Mapping*>(nullptr));
	});

	it("calls a CC knob relative and a pitch bend not", _ {
		MappingTable table;
		expect(table.learn(knob(cc(0, 5), out_param(1, 2))).relative).to_be_true();
		expect(table.learn(knob(cc(0, 128), out_param(1, 3))).relative).to_be_false();
	});

	it("moves an output's params and input, dropping the target's own", _ {
		MappingTable table;
		table.learn(knob(cc(0, 1), out_param(1, 2)));
		table.learn(Mapping{.source = cc(0, 2), .target = TrackInputTarget{1}});
		table.learn(knob(cc(0, 3), out_param(2, 9)));
		table.learn(Mapping{.source = cc(0, 4), .target = TrackInputTarget{2}});
		table.learn(knob(cc(0, 5), out_param(3, 9)));
		table.retarget_output(1, 2);
		expect(table.entries().size()).to_equal(size_t{3});
		expect(table.find(out_param(2, 2))->source).to_equal(cc(0, 1));
		expect(table.find(TrackInputTarget{2})->source).to_equal(cc(0, 2));
		expect(table.find(out_param(2, 9))).to_equal(static_cast<const Mapping*>(nullptr));
		expect(table.find(out_param(3, 9))->source).to_equal(cc(0, 5));
		expect(table.find(out_param(1, 2))).to_equal(static_cast<const Mapping*>(nullptr));
	});

	it("copies an output's params and a clip's mute", _ {
		MappingTable table;
		table.learn(knob(cc(0, 1), out_param(1, 2)));
		table.learn(knob(cc(0, 2), out_param(1, 3)));
		table.learn(Mapping{.source = cc(0, 3), .target = TrackInputTarget{1}});
		table.learn(Mapping{.source = cc(0, 4), .target = ClipMuteTarget{5}});
		table.copy_params(1, 4);
		table.copy_clip_mute(5, 6);
		expect(table.entries().size()).to_equal(size_t{7});
		expect(table.find(out_param(4, 3))->source).to_equal(cc(0, 2));
		expect(table.find(TrackInputTarget{4})).to_equal(static_cast<const Mapping*>(nullptr));
		expect(table.find(ClipMuteTarget{6})->source).to_equal(cc(0, 4));
		expect(table.find(ClipMuteTarget{5})->source).to_equal(cc(0, 4));
	});

	it("replaces the copy target's own params, and copies nothing onto itself", _ {
		MappingTable table;
		table.learn(knob(cc(0, 1), out_param(1, 2)));
		table.learn(knob(cc(0, 2), out_param(4, 2)));
		table.learn(knob(cc(0, 3), out_param(4, 7)));
		table.copy_params(1, 4);
		table.copy_params(1, 1);
		expect(table.entries().size()).to_equal(size_t{2});
		expect(table.find(out_param(4, 2))->source).to_equal(cc(0, 1));
		expect(table.find(out_param(4, 7))).to_equal(static_cast<const Mapping*>(nullptr));
	});

	it("copies a clip's absent mute as none", _ {
		MappingTable table;
		table.learn(Mapping{.source = cc(0, 4), .target = ClipMuteTarget{6}});
		table.copy_clip_mute(5, 6);
		expect(table.find(ClipMuteTarget{6})).to_equal(static_cast<const Mapping*>(nullptr));
	});

	it("forgets only the entries of what was deleted", _ {
		MappingTable table;
		table.learn(Mapping{.source = cc(0, 1), .target = ClipMuteTarget{1}});
		table.learn(Mapping{.source = cc(0, 2), .target = DrumMuteTarget{1}});
		table.learn(Mapping{.source = cc(0, 3), .target = LaneInputTarget{1}});
		table.learn(knob(cc(0, 4), ParamTarget{{ControlsAddress::Kind::DRUM, 1}, deluge::engine::ParamId{1}}));
		table.learn(knob(cc(0, 5), out_param(1, 1)));
		table.learn(Mapping{.source = cc(0, 6), .target = TrackInputTarget{1}});
		table.learn(Mapping{.source = cc(0, 7), .target = SectionLaunchTarget{1}});
		table.forget_clip(1);
		expect(table.entries().size()).to_equal(size_t{6});
		table.forget_drum(1);
		expect(table.entries().size()).to_equal(size_t{3});
		table.forget_output(1);
		expect(table.entries().size()).to_equal(size_t{1});
		expect(table.find(SectionLaunchTarget{1})->source).to_equal(cc(0, 7));
	});

	it("compares equal by entries, and lives on the song", _ {
		MappingTable a;
		MappingTable b;
		expect(a).to_equal(b);
		a.learn(knob(cc(0, 1), out_param(1, 2)));
		expect(a).not_().to_equal(b);
		b.learn(knob(cc(0, 1), out_param(1, 2)));
		expect(a).to_equal(b);
		Song song;
		song.mappings = a;
		expect(song.mappings).to_equal(b);
	});

	it("replaces an owner's knobs where they stand, and lists them", _ {
		MappingTable table;
		table.learn(knob(cc(0, 1), out_param(1, 2)));
		table.learn(Mapping{.source = cc(0, 2), .target = ClipMuteTarget{1}});
		table.learn(knob(cc(0, 3), out_param(1, 4)));
		table.learn(knob(cc(0, 4), out_param(2, 4)));
		const std::array<Mapping, 2> knobs{Mapping{cc(0, 9), out_param(1, 7), true}, Mapping{cc(0, 8), out_param(1, 6)}};
		table.set_params({ControlsAddress::Kind::OUTPUT, 1}, knobs);
		expect(table.entries().size()).to_equal(size_t{4});
		expect(table.entries()[0]).to_equal(knobs[0]);
		expect(table.entries()[1]).to_equal(knobs[1]);
		expect(table.entries()[2].target).to_equal(MappingTarget{ClipMuteTarget{1}});
		expect(table.params_of({ControlsAddress::Kind::OUTPUT, 1}).size()).to_equal(size_t{2});
		table.set_params({ControlsAddress::Kind::OUTPUT, 1}, {});
		expect(table.params_of({ControlsAddress::Kind::OUTPUT, 1}).empty()).to_be_true();
		expect(table.entries().size()).to_equal(size_t{2});
		const std::array<Mapping, 1> drum{
		    Mapping{cc(0, 5), ParamTarget{{ControlsAddress::Kind::DRUM, 5}, deluge::engine::ParamId{1}}}};
		table.set_params({ControlsAddress::Kind::DRUM, 5}, drum);
		expect(table.entries().size()).to_equal(size_t{3});
		expect(table.entries()[2]).to_equal(drum[0]);
	});

	context("a binding as its owner's element holds it", _ {
	it("reads and writes a sound's knob learned on an MPE zone, on a cable's depth", _ {
		const Trip trip = round_trip(
		    R"({"midiKnobs": [{"midiKnob": {"mpeZone": "lower", "ccNumber": 74, "relative": 1, "controlsParam": "pitch", "patchAmountFromSource": "lfo1"}}]})",
		    ControlsAddress{ControlsAddress::Kind::DRUM, 3}, &deluge::instrument::kNaming);
		expect(trip.count).to_equal(size_t{1});
		expect(trip.same).to_be_true();
		const Mapping& m = trip.first;
		expect(static_cast<int>(m.source.channel_or_zone)).to_equal(16);
		expect(static_cast<int>(m.source.note_or_cc)).to_equal(74);
		expect(m.relative).to_be_true();
		expect(m.target).to_equal(MappingTarget{ParamTarget{
		    {ControlsAddress::Kind::DRUM, 3},
		    *deluge::instrument::cable_depth_id(deluge::instrument::PatchSource::LFO_GLOBAL_1,
		                                        deluge::instrument::param::kPitch)}});
	});

	it("reads and writes a kit's knob from a port", _ {
		const Trip trip = round_trip(
		    R"({"midiKnobs": [{"midiKnob": {"channel": 2, "ccNumber": 7, "relative": 0, "controlsParam": "lpfFrequency", "device": {"port": "din"}}}]})",
		    ControlsAddress{ControlsAddress::Kind::OUTPUT, 5}, &deluge::global_effectable::kNaming);
		expect(trip.count).to_equal(size_t{1});
		expect(trip.same).to_be_true();
		const Mapping& m = trip.first;
		expect(m.source.device->port).to_equal(MidiDeviceRef::Port::DIN);
		expect(m.relative).to_be_false();
		expect(m.target).to_equal(MappingTarget{
		    ParamTarget{{ControlsAddress::Kind::OUTPUT, 5}, deluge::global_effectable::param::kLpfFrequency}});
	});

	it("reads and writes the song's knob", _ {
		const Trip trip = round_trip(
		    R"({"midiKnobs": [{"midiKnob": {"channel": 0, "ccNumber": 1, "relative": 0, "controlsParam": "volume"}}]})",
		    ControlsAddress{ControlsAddress::Kind::SONG, 0}, &deluge::global_effectable::kNaming);
		expect(trip.count).to_equal(size_t{1});
		expect(trip.same).to_be_true();
		const Mapping& m = trip.first;
		expect(std::get<ParamTarget>(m.target).owner.kind).to_equal(ControlsAddress::Kind::SONG);
	});

	it("reads and writes a clip's mute command", _ {
		const Trip trip = round_trip(R"({"muteMidiCommand": {"channel": 3, "note": 40}})", ClipOwner{7});
		expect(trip.count).to_equal(size_t{1});
		expect(trip.same).to_be_true();
		const Mapping& m = trip.first;
		expect(m.source).to_equal(cc(3, 40));
		expect(m.target).to_equal(MappingTarget{ClipMuteTarget{7}});
	});

	it("reads and writes a kit row's mute command on an MPE zone", _ {
		const Trip trip = round_trip(R"({"midiMuteCommand": {"mpeZone": "upper", "note": 37}})", DrumOwner{9});
		expect(trip.count).to_equal(size_t{1});
		expect(trip.same).to_be_true();
		const Mapping& m = trip.first;
		expect(m.source).to_equal(cc(17, 37));
		expect(m.target).to_equal(MappingTarget{DrumMuteTarget{9}});
	});

	it("reads and writes a kit row's input from a port", _ {
		const Trip trip =
		    round_trip(R"({"midiInput": {"channel": 1, "note": 36, "device": {"port": "upstreamUSB"}}})", DrumOwner{9});
		expect(trip.count).to_equal(size_t{1});
		expect(trip.same).to_be_true();
		const Mapping& m = trip.first;
		expect(m.source.device->port).to_equal(MidiDeviceRef::Port::UPSTREAM_USB_1);
		expect(m.target).to_equal(MappingTarget{LaneInputTarget{9}});
	});

	it("reads and writes a track's input from a hosted device", _ {
		const Trip trip = round_trip(
		    R"({"inputMidiChannel": 4, "inputMidiDevice": {"name": "Keys", "vendorId": "0x1234", "productId": "0x0001"}})",
		    TrackOwner{2, false});
		expect(trip.count).to_equal(size_t{1});
		expect(trip.same).to_be_true();
		const Mapping& m = trip.first;
		expect(static_cast<int>(m.source.channel_or_zone)).to_equal(4);
		expect(m.source.note_or_cc).to_equal(MidiCommand::kNone);
		expect(m.source.device->name).to_equal(std::string("Keys"));
		expect(static_cast<int>(m.source.device->vendor_id)).to_equal(0x1234);
		expect(m.target).to_equal(MappingTarget{TrackInputTarget{2}});
	});

	it("reads and writes a track's input on an MPE zone", _ {
		const Trip trip = round_trip(R"({"inputMPEZone": "lower"})", TrackOwner{2, false});
		expect(trip.count).to_equal(size_t{1});
		expect(trip.same).to_be_true();
		const Mapping& m = trip.first;
		expect(static_cast<int>(m.source.channel_or_zone)).to_equal(16);
	});

	it("reads and writes a kit's input with its root note", _ {
		const Trip trip = round_trip(R"({"MIDIInput": {"channel": 9, "note": 36}})", TrackOwner{4, true});
		expect(trip.count).to_equal(size_t{1});
		expect(trip.same).to_be_true();
		const Mapping& m = trip.first;
		expect(m.source).to_equal(cc(9, 36));
		expect(m.target).to_equal(MappingTarget{TrackInputTarget{4}});
	});

	it("writes only its owner's entries, and reads nothing an owner does not hold", _ {
		MappingTable table;
		table.learn(Mapping{.source = cc(0, 1), .target = ClipMuteTarget{1}});
		table.learn(Mapping{.source = cc(0, 2), .target = ClipMuteTarget{2}});
		table.learn(Mapping{.source = cc(0, 3), .target = DrumMuteTarget{1}});
		auto clip = deluge::serialization::Value::makeMap();
		encode_mappings(table, ClipOwner{2}, clip);
		expect(clip == json(R"({"muteMidiCommand": {"channel": 0, "note": 2}})")).to_be_true();
		MappingTable read;
		decode_mappings(json(R"({"muteMidiCommand": {"note": 2}, "midiInput": {"channel": 1, "note": 3}})"), ClipOwner{1},
		                read);
		expect(read.entries().empty()).to_be_true();
	});
	});
});

CPPSPEC_SPEC(mapping_table)
