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
			expect(parse_osc_type(to_string(t)) == t).to_equal(true);
		}
		expect(std::string(to_string(OscType::ANALOG_SAW_2))).to_equal(std::string("analogSaw"));
		expect(parse_osc_type("nonsense") == OscType::TRIANGLE).to_equal(true);
	});

	it("reads the pre-2017 polyphony numbers", _ {
		expect(parse_polyphony_mode("0") == PolyphonyMode::AUTO).to_equal(true);
		expect(parse_polyphony_mode("2") == PolyphonyMode::CHOKE).to_equal(true);
		expect(parse_polyphony_mode("anything") == PolyphonyMode::POLY).to_equal(true);
		expect(std::string(to_string(PolyphonyMode::POLY))).to_equal(std::string("poly"));
	});

	it("names synth modes and LFO shapes, defaulting as files have always read", _ {
		expect(parse_synth_mode(to_string(SynthMode::FM)) == SynthMode::FM).to_equal(true);
		expect(parse_synth_mode("other") == SynthMode::SUBTRACTIVE).to_equal(true);
		expect(std::string(to_string(LFOType::SAMPLE_AND_HOLD))).to_equal(std::string("sah"));
		expect(parse_lfo_type("rwalk") == LFOType::RANDOM_WALK).to_equal(true);
		expect(parse_lfo_type("other") == LFOType::TRIANGLE).to_equal(true);
	});
});

CPPSPEC_SPEC(words)
