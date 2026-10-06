#include "engines/effects/words.h"

#include <cppspec.hpp>
#include <string_view>

using namespace deluge::effects;

// clang-format off
describe effect_words("the effects' words as a file writes them", ${
	it("names every filter mode and reads each name back", _ {
		for (FilterMode mode : {FilterMode::TRANSISTOR_12DB, FilterMode::TRANSISTOR_24DB, FilterMode::TRANSISTOR_24DB_DRIVE,
		                        FilterMode::SVF_BAND, FilterMode::SVF_NOTCH, FilterMode::HPLADDER, FilterMode::OFF}) {
			expect(parse_filter_mode(to_string(mode))).to_equal(mode);
		}
		expect(to_string(FilterMode::TRANSISTOR_24DB_DRIVE)).to_equal(std::string_view("24dBDrive"));
		expect(parse_filter_mode("nonsense")).to_equal(FilterMode::OFF);
	});
	it("names every filter route", _ {
		expect(to_string(FilterRoute::LOW_TO_HIGH)).to_equal(std::string_view("L2H"));
		expect(to_string(FilterRoute::PARALLEL)).to_equal(std::string_view("PARA"));
		expect(to_string(FilterRoute::HIGH_TO_LOW)).to_equal(std::string_view("H2L"));
		expect(parse_filter_route("nonsense")).to_equal(FilterRoute::PARALLEL);
	});
	it("names every mod-FX type, as old files spell them", _ {
		expect(to_string(ModFXType::WARBLE)).to_equal(std::string_view("TapeWarble"));
		expect(to_string(ModFXType::CHORUS_STEREO)).to_equal(std::string_view("StereoChorus"));
		expect(to_string(ModFXType::GRAIN)).to_equal(std::string_view("grainFX"));
		expect(to_string(ModFXType::NONE)).to_equal(std::string_view("none"));
		for (ModFXType type : {ModFXType::NONE, ModFXType::FLANGER, ModFXType::CHORUS, ModFXType::PHASER,
		                       ModFXType::CHORUS_STEREO, ModFXType::WARBLE, ModFXType::DIMENSION, ModFXType::GRAIN}) {
			expect(parse_mod_fx_type(to_string(type))).to_equal(type);
		}
		expect(parse_mod_fx_type("nonsense")).to_equal(ModFXType::NONE);
	});
});

CPPSPEC_SPEC(effect_words)
