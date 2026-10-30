#include "instruments/deluge/codec.h"
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
constexpr deluge::plugin::CodecContext kNewSong{.input_tick_magnitude = 0};

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
	it("reads none of the sound forms no release writes", _ {
		auto doc = deluge::serialization::parseXml(
		    R"(<sound ringMod="1" mode="fm" oscillatorReset="0" polyphonic="2"><oscillatorA type="square" note="72"/>)"
		    R"(<fileName>SAMPLES/A.WAV</fileName></sound>)");
		SoundState state = new_synth_state();
		expect(decode(*doc->find("sound"), state, kNewSong)).to_have_value();
		expect(state.synth_mode).to_equal(SynthMode::FM);
		expect(state.osc_retrigger_phase[1]).to_equal(uint32_t{0}); // as every decode starts it: not freed
		expect(state.polyphonic).to_equal(PolyphonyMode::POLY);
		expect(state.sources[0].transpose).to_equal(int16_t{0});
	});
	it("reads a zone only in samples", _ {
		auto doc = deluge::serialization::parseXml(
		    R"(<sound mode="subtractive"><continuous>1</continuous><reversed>1</reversed>)"
		    R"(<osc1 type="sample" fileName="SAMPLES/B.WAV"><zone startSeconds="1" startMilliseconds="20" )"
		    R"(endMilliseconds="80" startSamplePos="5" endSamplePos="900"/></osc1></sound>)");
		SoundState state = new_synth_state();
		expect(decode(*doc->find("sound"), state, kNewSong)).to_have_value();
		expect(state.sources[0].ranges.size()).to_equal(size_t{1});
		expect(state.sources[0].ranges[0].file_path).to_equal(std::string("SAMPLES/B.WAV"));
		expect(state.sources[0].ranges[0].start_pos).to_equal(uint64_t{5});
		expect(state.sources[0].ranges[0].end_pos).to_equal(uint64_t{900});
		expect(state.sources[0].reversed).to_be_false();
	});
	it("clamps unison to what a sound can play", _ {
		auto doc = deluge::serialization::parseXml(R"(<sound><unison num="20" detune="70" spread="-3"/></sound>)");
		SoundState state = new_synth_state();
		(void)decode(*doc->find("sound"), state, kNewSong);
		expect(static_cast<int32_t>(state.num_unison)).to_equal(8);
		expect(static_cast<int32_t>(state.unison_detune)).to_equal(50);
		expect(static_cast<int32_t>(state.unison_stereo_spread)).to_equal(0);
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
	it("reads a factory preset's compressor tag as the sidechain", _ {
		auto doc = deluge::serialization::parseXml(
		    R"(<sound mode="subtractive"><compressor attack="11" release="22" syncLevel="0"/></sound>)");
		SoundState state = new_synth_state();
		(void)decode(*doc->find("sound"), state, kNewSong);
		expect(state.effects.sidechain.attack).to_equal(11);
		expect(state.effects.sidechain.release).to_equal(22);
		expect(is_state_key("compressor")).to_be_false();
	});
	it("reads a filter mode and mod-FX type only where the latest release writes them", _ {
		auto doc = deluge::serialization::parseXml(R"(<sound mode="subtractive"><lpf mode="12dB"/><fx type="flanger"/></sound>)");
		SoundState state = new_synth_state();
		const SoundState fresh = state;
		(void)decode(*doc->find("sound"), state, kNewSong);
		expect(state.effects.filter.lpf_mode).to_equal(fresh.effects.filter.lpf_mode);
		expect(state.effects.mod_fx.type).to_equal(fresh.effects.mod_fx.type);
	});
	it("refuses two ranges with the same top note", _ {
		auto doc = deluge::serialization::parseXml(read("duplicate_range.XML"));
		SoundState state = new_synth_state();
		auto decoded = decode(*doc->find("sound"), state, kNewSong);
		expect(decoded).to_have_error();
		expect(decoded.error_or(deluge::plugin::StateError::NOT_A_MAP)).to_equal(deluge::plugin::StateError::DUPLICATE_RANGE);
	});
});

CPPSPEC_SPEC(codec_decode)
