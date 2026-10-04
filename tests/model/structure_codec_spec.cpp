#include "codec_support.h"
#include "deluge-project/structure_codec.h"

#include <cppspec.hpp>

using namespace deluge::project;
using deluge::project::test::both_shapes;
using deluge::serialization::Value;

// clang-format off
describe structure_codec("the structure records", ${
	it("round-trips a clip and reads its legacy length and clamps", _ {
		DocClip c{.loop_length = 384, .colour_offset = -12, .launch_style = LaunchStyle::FILL, .section = 4};
		DocClip back;
		expect(decode(encode(c), back).has_value()).to_equal(true);
		expect(back == c).to_equal(true);
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
		expect(decode(encode(r, false), back, minY).has_value()).to_equal(true);
		expect(back == r).to_equal(true);
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
		expect(back.end_pos == uint64_t{0xFFFFFFFF90000000}).to_equal(true); // sign-extended, as the reader does
		back.end_pos = a.end_pos;
		expect(back == a).to_equal(true);
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
			expect(back == record).to_equal(true);
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

	it("round-trips the identity of every drum type", _ {
		for (auto [element, record] :
		     {std::pair{"sound", DocDrum{.drum_type = DrumType::SOUND, .name = "KICK"}},
		      std::pair{"midiOutput", DocDrum{.drum_type = DrumType::MIDI, .channel = 3, .note = 36, .name = "M"}},
		      std::pair{"gateOutput", DocDrum{.drum_type = DrumType::GATE, .channel = 2, .name = "G"}}}) {
			DocDrum back;
			(void)decode_identity(encode_identity(record), element, back);
			expect(back == record).to_equal(true);
		}
	});
});

CPPSPEC_SPEC(structure_codec)
