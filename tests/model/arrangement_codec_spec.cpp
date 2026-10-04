#include "codec_support.h"
#include "deluge-project/arrangement_codec.h"
#include "serialization/parse_json.h"
#include "serialization/write_json.h"

#include <cppspec.hpp>

using namespace deluge::project;
using deluge::project::test::both_shapes;
using deluge::serialization::Value;

namespace {
Value saved(const Arrangement& arrangement) {
	auto song = Value::makeMap();
	song.set("sections", encode_sections(arrangement));
	song.set("sessionMacros", encode_macros(arrangement));
	auto root = Value::makeMap();
	root.set("song", std::move(song));
	return *deluge::serialization::parseJson(deluge::serialization::write_json(root))->find("song");
}

MidiCommand command(uint8_t channel, uint8_t note, std::optional<MidiDeviceRef> device) {
	return MidiCommand{.channel_or_zone = channel, .note_or_cc = note, .device = std::move(device)};
}
} // namespace

// clang-format off
describe arrangement_codec("the arrangement", ${
	it("round-trips sections with every kind of device and macros of every kind", _ {
		Arrangement a;
		a.sections[0].num_repetitions = 3;
		a.sections[1].launch_midi_command = command(2, 60, MidiDeviceRef{.port = MidiDeviceRef::Port::DIN});
		a.sections[2].launch_midi_command = command(16, 61, MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_3});
		a.sections[3].launch_midi_command =
		    command(20, 62, MidiDeviceRef{.port = MidiDeviceRef::Port::HOSTED, .name = "Key \"bed\" \\ 2",
		                                  .vendor_id = 0x00A1, .product_id = 0x0B02});
		a.sections[4].launch_midi_command = command(5, 7, std::nullopt);
		a.session_macros[0] = {.kind = SessionMacroKind::CLIP_LAUNCH, .clip_id = 11};
		a.session_macros[1] = {.kind = SessionMacroKind::OUTPUT_CYCLE, .output_id = 4};
		a.session_macros[3] = {.kind = SessionMacroKind::SECTION, .section = 5};
		Arrangement back;
		expect(decode(saved(a), back).has_value()).to_equal(true);
		expect(back == a).to_equal(true);
	});

	it("drops a learned program change and a note out of range, keeps the repeats", _ {
		both_shapes(
		    R"(<song><sections><section id="1" numRepeats="2" midiCommandChannel="40" midiCommandNote="60"/><section id="2" midiCommandChannel="3" midiCommandNote="200"/></sections></song>)",
		    R"({"song": {"sections": [{"section": {"id": 1, "numRepeats": 2, "midiCommandChannel": 40, "midiCommandNote": 60}}, {"section": {"id": 2, "midiCommandChannel": 3, "midiCommandNote": 200}}]}})",
		    [&](const Value& song) {
			Arrangement a;
			(void)decode(song, a);
			expect(a.sections[1].launch_midi_command.contains_something()).to_equal(false);
			expect(a.sections[2].launch_midi_command.contains_something()).to_equal(false);
			expect(static_cast<int>(a.sections[1].num_repetitions)).to_equal(2);
		});
	});

	it("reads repeats out of range as none", _ {
		both_shapes(R"(<song><sections><section id="0" numRepeats="12000"/></sections></song>)",
		            R"({"song": {"sections": [{"section": {"id": 0, "numRepeats": 12000}}]}})", [&](const Value& song) {
			Arrangement a;
			a.sections[0].num_repetitions = 9;
			(void)decode(song, a);
			expect(static_cast<int>(a.sections[0].num_repetitions)).to_equal(0);
		});
	});

	it("reads a device by port, or by hosted name and hex ids", _ {
		both_shapes(
		    R"(<song><sections><section id="0" midiCommandChannel="1" midiCommandNote="2"><midiCommandDevice port="upstreamUSB2"/></section><section id="1" midiCommandChannel="1" midiCommandNote="2"><midiCommandDevice name="Pads" vendorId="0x09E8" productId="0x0028"/></section></sections></song>)",
		    R"({"song": {"sections": [{"section": {"id": 0, "midiCommandChannel": 1, "midiCommandNote": 2, "midiCommandDevice": {"port": "upstreamUSB2"}}}, {"section": {"id": 1, "midiCommandChannel": 1, "midiCommandNote": 2, "midiCommandDevice": {"name": "Pads", "vendorId": "0x09E8", "productId": "0x0028"}}}]}})",
		    [&](const Value& song) {
			Arrangement a;
			(void)decode(song, a);
			expect(a.sections[0].launch_midi_command.device->port == MidiDeviceRef::Port::UPSTREAM_USB_2).to_equal(true);
			const MidiDeviceRef& hosted = *a.sections[1].launch_midi_command.device;
			expect(hosted.port == MidiDeviceRef::Port::HOSTED).to_equal(true);
			expect(hosted.name).to_equal(std::string("Pads"));
			expect(static_cast<int>(hosted.vendor_id)).to_equal(0x09E8);
			expect(static_cast<int>(hosted.product_id)).to_equal(0x0028);
		});
	});

	it("reports a legacy positional target for the host and keeps the macro", _ {
		both_shapes(
		    R"(<song><sessionMacros><macro kind="output_cycle" output="2"/><macro kind="clip_launch" clip="0"/><macro kind="clip_launch"/></sessionMacros></song>)",
		    R"({"song": {"sessionMacros": [{"macro": {"kind": "output_cycle", "output": 2}}, {"macro": {"kind": "clip_launch", "clip": 0}}, {"macro": {"kind": "clip_launch"}}]}})",
		    [&](const Value& song) {
			Arrangement a;
			LegacyMacroTargets legacy;
			(void)decode(song, a, &legacy);
			expect(a.session_macros[0].kind == SessionMacroKind::OUTPUT_CYCLE).to_equal(true);
			expect(*legacy.output_index[0]).to_equal(2);
			expect(a.session_macros[1].kind == SessionMacroKind::CLIP_LAUNCH).to_equal(true);
			expect(*legacy.clip_index[1]).to_equal(0);
			expect(a.session_macros[2].kind == SessionMacroKind::NO_MACRO).to_equal(true);
		});
	});
});

CPPSPEC_SPEC(arrangement_codec)
