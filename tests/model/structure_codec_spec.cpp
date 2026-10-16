#include "codec_support.h"
#include "deluge-project/structure_codec.h"
#include "serialization/parse_xml.h"

#include <cppspec.hpp>
#include <string>
#include <vector>

using namespace deluge::project;
using deluge::project::test::both_shapes;
using deluge::serialization::Value;

// clang-format off
describe structure_codec("the structure records", ${
	it("round-trips a clip and reads its legacy length and clamps", _ {
		DocClip c{.loop_length = 384, .colour_offset = -12, .launch_style = LaunchStyle::FILL, .section = 4};
		DocClip back;
		expect(decode(encode(c), back)).to_have_value();
		expect(back).to_equal(c);
		both_shapes(R"(<song trackLength="0" section="40" colourOffset="3"/>)",
		            R"({"song": {"trackLength": 0, "section": 40, "colourOffset": 3}})", [&](const Value& clip) {
			DocClip read;
			(void)decode(clip, read);
			expect(read.loop_length).to_equal(1);
			expect(static_cast<int>(read.section)).to_equal(23);
			expect(static_cast<int>(read.colour_offset)).to_equal(3);
		});
	});

	it("round-trips a note row and keeps rows in rising y", _ {
		DocNoteRow r{.y = 60, .loop_length_if_independent = 96, .colour_offset = 0,
		             .sequence_direction = SequenceDirection::PINGPONG, .muted = true};
		DocNoteRow back{.sequence_direction = SequenceDirection::OBEY_PARENT};
		int32_t minY = -32768;
		expect(decode(encode(r, false), back, minY)).to_have_value();
		expect(back).to_equal(r);
		expect(minY).to_equal(61);
		both_shapes(R"(<song y="10"/>)", R"({"song": {"y": 10}})", [&](const Value& row) {
			DocNoteRow read;
			int32_t floor = 20;
			(void)decode(row, read, floor);
			expect(static_cast<int>(read.y)).to_equal(20);
			expect(floor).to_equal(21);
		});
	});

	it("round-trips an audio clip, its sample positions as the int32 the file holds", _ {
		DocAudioClip a{.file_path = "SAMPLES/A \"b\".WAV", .start_pos = 10, .end_pos = 0x90000000u, .transpose = -3,
		               .cents = 7, .interpolation_mode = deluge::engine::InterpolationMode::LINEAR,
		               .pitch_and_speed_independent = false, .reversed = true, .attack = 5,
		               .voice_priority = deluge::engine::VoicePriority::HIGH, .overdubs_should_clone = false};
		DocAudioClip back;
		(void)decode(encode(a), back);
		expect(back.end_pos).to_equal(uint64_t{0xFFFFFFFF90000000}); // sign-extended, as the reader does
		back.end_pos = a.end_pos;
		expect(back).to_equal(a);
	});

	it("round-trips the identity of every output type", _ {
		DocOutput synth{.name = "PAD", .colour = 5, .mod_knob_mode = 2, .muted_in_arrangement = true,
		                .dir_path = "SYNTHS/X", .default_velocity = 90};
		DocOutput midi{.name = "", .colour = 1, .default_velocity = 64, .channel = 16, .channel_suffix = 2,
		               .device_definition_file_name = "MIDI_DEVICES/A.XML", .collapse_aftertouch = true,
		               .collapse_mpe = false, .output_mpe_y = 74};
		DocOutput cv{.channel = 1, .cv2_source = CVMode::OFF};
		DocOutput audio{.name = "AUDIO1", .colour = 9, .armed_for_recording = true};
		for (auto [element, record] : {std::pair{"sound", synth}, std::pair{"midi", midi}, std::pair{"cvChannel", cv},
		                               std::pair{"audioTrack", audio}}) {
			DocOutput back;
			(void)decode_identity(encode_identity(record, element), element, back);
			expect(back).to_equal(record);
		}
	});

	it("keeps the record default velocity when the file holds none or an invalid one", _ {
		both_shapes(R"(<song defaultVelocity="0"/>)", R"({"song": {"defaultVelocity": 0}})", [&](const Value& out) {
			DocOutput read{.default_velocity = 77};
			(void)decode_identity(out, "sound", read);
			expect(static_cast<int>(read.default_velocity)).to_equal(77);
		});
	});

	it("reads a MIDI output channel as a zone, an internal destination or a number", _ {
		both_shapes(R"(<song zone="upper"/>)", R"({"song": {"zone": "upper"}})", [&](const Value& out) {
			DocOutput read;
			(void)decode_identity(out, "midi", read);
			expect(read.channel).to_equal(17);
		});
		both_shapes(R"(<song internalDest="transpose"/>)", R"({"song": {"internalDest": "transpose"}})",
		            [&](const Value& out) {
			DocOutput read;
			(void)decode_identity(out, "midi", read);
			expect(read.channel).to_equal(kMidiChannelTranspose);
		});
	});

	it("starts a new output from its type defaults, so a MIDI output that never set its MPE Y CC sends CC 74", _ {
		both_shapes(R"(<song channel="3"/>)", R"({"song": {"channel": 3}})", [&](const Value& out) {
			DocOutput read = new_output("midi");
			(void)decode_identity(out, "midi", read);
			expect(static_cast<int>(read.output_mpe_y)).to_equal(74);
			expect(read.collapse_mpe).to_be_true();
		});
	});

	it("round-trips the identity of every drum type", _ {
		for (auto [element, record] :
		     {std::pair{"sound", DocDrum{.drum_type = DrumType::SOUND, .name = "KICK"}},
		      std::pair{"midiOutput", DocDrum{.drum_type = DrumType::MIDI, .channel = 3, .note = 36, .name = "M"}},
		      std::pair{"gateOutput", DocDrum{.drum_type = DrumType::GATE, .channel = 2, .name = "G"}}}) {
			DocDrum back;
			(void)decode_identity(encode_identity(record), element, back);
			expect(back).to_equal(record);
		}
	});

	it("keeps everything in a synth track's element the model does not own, in order, as the track's instrument", _ {
		auto doc = deluge::serialization::parseXml(R"(<sound id="3" presetName="Bass" colour="5" polyphonic="poly")"
		                                           R"( mode="fm"><osc1 type="sine"/><unison num="1"/></sound>)");
		InstrumentSlot slot = slot_of_output(*doc->find("sound"), 2);
		expect(slot.engine).to_equal(std::string("deluge.instrument"));
		expect(slot.tick_magnitude).to_equal(2);
		std::vector<std::string> keys;
		for (const auto& [key, value] : slot.state.entries()) {
			keys.push_back(key);
		}
		expect(keys).to_equal(std::vector<std::string>{"polyphonic", "mode", "osc1", "unison"});
	});

	it("keeps a sound drum's sound, without its id and name", _ {
		auto doc = deluge::serialization::parseXml(
		    R"(<sound id="9" name="KICK" mode="subtractive"><osc1 type="sample"/></sound>)");
		InstrumentSlot slot = slot_of_drum(*doc->find("sound"), 3);
		expect(slot.tick_magnitude).to_equal(3);
		expect(slot.state.find("name")).to_be_null();
		expect(slot.state.find("id")).to_be_null();
		expect(slot.state.find("osc1")).not_().to_be_null();
	});

	it("leaves a sound drum's arp out of its sound, which its lane record holds", _ {
		auto doc = deluge::serialization::parseXml(
		    R"(<sound id="9" name="KICK" mode="subtractive"><osc1 type="sample"/><arpeggiator arpMode="arp"/></sound>)");
		InstrumentSlot slot = slot_of_drum(*doc->find("sound"), 3);
		expect(slot.state.find("arpeggiator")).to_be_null();
		expect(slot.state.find("osc1")).not_().to_be_null();
	});
});

CPPSPEC_SPEC(structure_codec)
