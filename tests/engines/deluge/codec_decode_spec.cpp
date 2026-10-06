#include "engines/deluge/codec.h"
#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <fstream>
#include <sstream>
#include <string>

using namespace deluge::instrument;

namespace {
std::string read(const std::string& name) {
	std::ifstream in(std::string(DELUGE_INSTRUMENT_FIXTURES) + "/" + name);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

/// The fixtures' expected states were recorded from the app's reader reading each fixture as a preset into a new song,
/// whose input tick magnitude is 0.
constexpr deluge::engine::CodecContext kNewSong{.input_tick_magnitude = 0};

/// Decode the fixture's sound into the state a new synth starts with (as recorded beside the fixture), and give the
/// first path where it differs from the state the app's reader produced from the same file.
std::string differenceFor(const std::string& fixture) {
	auto doc = deluge::serialization::parseXml(read(fixture + ".XML"));
	auto expected = deluge::serialization::parseJson(read(fixture + ".expected.json"));
	if (!doc || !expected) {
		return "the fixture does not parse";
	}
	SoundState state = new_synth_state();
	if (!decode(*doc->find("sound"), state, kNewSong)) {
		return "decode failed";
	}
	return deluge::serialization::first_difference(dump_state(state), *expected);
}
} // namespace

// clang-format off
describe codec_decode("the Deluge instrument reads every format a sound was saved in", ${
	it("reads the pre-mid-2016 oscillators", _ {
		expect(differenceFor("old_oscillators")).to_equal(std::string{});
	});
	it("keeps ring modulation over a later mode", _ {
		expect(differenceFor("ringmod_then_mode")).to_equal(std::string{});
	});
	it("frees every oscillator when reset is off", _ {
		expect(differenceFor("oscillator_reset_off")).to_equal(std::string{});
	});
	it("reads an early-2016 sample sound, its zone in seconds and milliseconds", _ {
		expect(differenceFor("early2016_sample")).to_equal(std::string{});
	});
	it("reads the old polyphony numbers and clamps unison", _ {
		expect(differenceFor("polyphony_numbers")).to_equal(std::string{});
	});
	it("reads back a wavetable split into ranges", _ {
		SoundState written = new_synth_state();
		written.modulator_transpose[1] = 0; // a subtractive sound's file holds no modulators; a read starts them at 0
		written.sources[0].osc_type = OscType::WAVETABLE;
		written.sources[0].ranges = {RangeState{.top_note = 48, .file_path = "WAVETABLES/A.WAV"},
		                             RangeState{.top_note = 32767, .file_path = "WAVETABLES/B.WAV"}};
		SoundState read = new_synth_state();
		(void)decode(encode(written, kNewSong), read, kNewSong);
		expect(deluge::serialization::first_difference(dump_state(read), dump_state(written))).to_equal(std::string{});
	});
	it("reads an old compressor tag as the sidechain, and leaves a delay's old feedback to the params", _ {
		auto doc = deluge::serialization::parseXml(
		    R"(<sound mode="subtractive"><compressor attack="11" release="22" syncLevel="0"/>)"
		    R"(<delay pingPong="0" feedback="5"/></sound>)");
		SoundState state = new_synth_state();
		(void)decode(*doc->find("sound"), state, kNewSong);
		expect(state.effects.sidechain.attack).to_equal(11);
		expect(state.effects.sidechain.release).to_equal(22);
		expect(state.effects.delay.ping_pong).to_be_false();
		expect(is_state_key("compressor")).to_be_false();
		expect(is_state_key("audioCompressor")).to_be_true();
	});
	it("refuses two ranges with the same top note", _ {
		auto doc = deluge::serialization::parseXml(read("duplicate_range.XML"));
		SoundState state = new_synth_state();
		auto decoded = decode(*doc->find("sound"), state, kNewSong);
		expect(decoded).to_have_error();
		expect(decoded.error_or(deluge::engine::StateError::NOT_A_MAP)).to_equal(deluge::engine::StateError::DUPLICATE_RANGE);
	});
});

CPPSPEC_SPEC(codec_decode)
