#include "engines/deluge/words.h"

#include <cppspec.hpp>
#include <string>

using namespace deluge::instrument;

// clang-format off
describe words("the Deluge instrument's words and their file names", ${
	it("names every oscillator type as files do, and reads unknown names as a triangle", _ {
		for (OscType t : {OscType::SINE, OscType::TRIANGLE, OscType::SQUARE, OscType::ANALOG_SQUARE, OscType::SAW,
		                  OscType::ANALOG_SAW_2, OscType::WAVETABLE, OscType::SAMPLE, OscType::DX7, OscType::INPUT_L,
		                  OscType::INPUT_R, OscType::INPUT_STEREO}) {
			expect(parse_osc_type(to_string(t))).to_equal(t);
		}
		expect(std::string(to_string(OscType::ANALOG_SAW_2))).to_equal(std::string("analogSaw"));
		expect(parse_osc_type("nonsense")).to_equal(OscType::TRIANGLE);
	});

	it("reads polyphony only by its words: the pre-2017 numbers and anything else are poly", _ {
		expect(parse_polyphony_mode("0")).to_equal(PolyphonyMode::POLY);
		expect(parse_polyphony_mode("2")).to_equal(PolyphonyMode::POLY);
		expect(parse_polyphony_mode("anything")).to_equal(PolyphonyMode::POLY);
		expect(std::string(to_string(PolyphonyMode::POLY))).to_equal(std::string("poly"));
	});

	it("names synth modes and LFO shapes, defaulting as files have always read", _ {
		expect(parse_synth_mode(to_string(SynthMode::FM))).to_equal(SynthMode::FM);
		expect(parse_synth_mode("other")).to_equal(SynthMode::SUBTRACTIVE);
		expect(std::string(to_string(LFOType::SAMPLE_AND_HOLD))).to_equal(std::string("sah"));
		expect(parse_lfo_type("rwalk")).to_equal(LFOType::RANDOM_WALK);
		expect(parse_lfo_type("other")).to_equal(LFOType::TRIANGLE);
	});
});

CPPSPEC_SPEC(words)
