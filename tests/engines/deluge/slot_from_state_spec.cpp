#include "engines/deluge/codec.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"

#include <cppspec.hpp>

using namespace deluge::instrument;
using deluge::serialization::Value;

namespace {
constexpr deluge::engine::CodecContext kContext{.input_tick_magnitude = 0};
std::vector<std::string> keys(const Value& map) {
	std::vector<std::string> out;
	for (const auto& [key, value] : map.entries()) {
		out.push_back(key);
	}
	return out;
}
} // namespace

// clang-format off
describe slot_from_state("a slot rebuilt from a state", ${
	it("replaces the instrument's keys and keeps every other entry in place", _ {
		const Value slot = *deluge::serialization::parseXml(R"(<sound polyphonic="mono" lpfMode="24dB"
			mode="subtractive"><defaultParams volume="0x00000000" /><unison num="1" detune="8" spread="0" />
			<arpeggiator mode="off" /></sound>)")->find("sound");
		SoundState state = new_synth_state();
		state.polyphonic = PolyphonyMode::POLY;
		state.num_unison = 3;
		const Value rebuilt = with_state(slot, state, kContext);
		expect(*deluge::serialization::text_of(rebuilt.find("polyphonic"))).to_equal(std::string("poly"));
		expect(*deluge::serialization::text_of(rebuilt.find("unison")->find("num"))).to_equal(std::string("3"));
		expect(*deluge::serialization::text_of(rebuilt.find("lpfMode"))).to_equal(std::string("24dB"));
		expect(rebuilt.find("defaultParams")).not_().to_be_null();
		expect(rebuilt.find("arpeggiator")).not_().to_be_null();
		// the kept entries stay in their order around the instrument's
		const auto order = keys(rebuilt);
		expect(order[0]).to_equal(std::string("polyphonic"));
		expect(order[1]).to_equal(std::string("lpfMode"));
		expect(order[2]).to_equal(std::string("mode"));
	});

	it("drops an instrument key the state no longer writes, and appends one it now does", _ {
		const Value slot = *deluge::serialization::parseXml(R"(<sound mode="fm" transpose="3">
			<modulator1 transpose="0" /><modulator2 transpose="0" /></sound>)")->find("sound");
		SoundState state = new_synth_state(); // subtractive, transpose 0: no modulators, no transpose
		const Value rebuilt = with_state(slot, state, kContext);
		expect(rebuilt.find("modulator1")).to_be_null();
		expect(rebuilt.find("transpose")).to_be_null();
		expect(rebuilt.find("osc1")).not_().to_be_null(); // appended
	});

	it("drops an old file's instrument keys, so a re-decode reads the state it holds", _ {
		// an early file: a ring-mod sound, its oscillators by their old names, its one sample at the top level
		const Value slot = *deluge::serialization::parseXml(R"(<sound ringMod="1" oscillatorReset="0"
			fileName="SAMPLES/OLD.WAV" continuous="1" reversed="1" lpfMode="24dB">
			<oscillatorA type="saw" /><oscillatorB type="square" /><zone startSamplePos="10" endSamplePos="99" />
			</sound>)")->find("sound");
		SoundState state = new_synth_state();
		expect(decode(slot, state, kContext).has_value()).to_be_true();
		state.synth_mode = SynthMode::SUBTRACTIVE;
		const Value rebuilt = with_state(slot, state, kContext);
		for (std::string_view old : {"ringMod", "oscillatorReset", "fileName", "continuous", "reversed",
		                             "oscillatorA", "oscillatorB", "zone"}) {
			expect(rebuilt.find(old)).to_be_null();
		}
		expect(rebuilt.find("lpfMode")).not_().to_be_null();
		// the slot reads back as its encoding does: nothing of the old form is read after it
		SoundState again = new_synth_state();
		expect(decode(rebuilt, again, kContext).has_value()).to_be_true();
		SoundState saved = new_synth_state();
		expect(decode(encode(state, kContext), saved, kContext).has_value()).to_be_true();
		expect(deluge::serialization::first_difference(dump_state(again), dump_state(saved))).to_equal(std::string{});
		expect(again.synth_mode).to_equal(SynthMode::SUBTRACTIVE);
	});

	it("leaves the encoding's keys equal to a fresh encode", _ {
		SoundState state = new_synth_state();
		state.unison_detune = 12;
		const Value rebuilt = with_state(Value::makeMap(), state, kContext);
		expect(deluge::serialization::first_difference(rebuilt, encode(state, kContext))).to_equal(std::string{});
	});
});

CPPSPEC_SPEC(slot_from_state)
