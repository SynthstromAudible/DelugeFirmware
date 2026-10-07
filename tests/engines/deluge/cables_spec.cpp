#include "engines/deluge/cables.h"

#include <cppspec.hpp>

using namespace deluge::instrument;
using deluge::engine::ParamId;

// clang-format off
describe cables("the Deluge instrument's patch-cable words and depth ids", ${
	it("names every source as the file does, and reads an unknown one as none", _ {
		expect(to_string(PatchSource::LFO_GLOBAL_2)).to_equal(std::string_view("lfo3"));
		expect(to_string(PatchSource::LFO_LOCAL_1)).to_equal(std::string_view("lfo2"));
		expect(to_string(PatchSource::SIDECHAIN)).to_equal(std::string_view("compressor"));
		expect(to_string(PatchSource::ENVELOPE_0)).to_equal(std::string_view("envelope1"));
		for (uint8_t s = 0; s <= static_cast<uint8_t>(PatchSource::NONE); ++s) {
			expect(static_cast<int>(parse_patch_source(to_string(static_cast<PatchSource>(s))))).to_equal(static_cast<int>(s));
		}
		expect(to_string(parse_patch_source("wobble"))).to_equal(std::string_view("none"));
	});

	it("reads a polarity, unknown as bipolar", _ {
		expect(to_string(parse_polarity("unipolar"))).to_equal(std::string_view("unipolar"));
		expect(to_string(parse_polarity("x"))).to_equal(std::string_view("bipolar"));
		expect(to_string(Polarity::UNIPOLAR)).to_equal(std::string_view("unipolar"));
	});

	it("pins the depth id of a cable to a param, and of cables controlling depths", _ {
		// LFO1 (0) -> pitch (25): flag | 25 | (0+1)<<8
		auto pitchDepth = cable_depth_id(PatchSource::LFO_GLOBAL_1, ParamId{25});
		expect(pitchDepth->value).to_equal(0x4000'0119u);
		// envelope2 (4) controlling that depth: | (4+1)<<14
		auto nested = cable_depth_id(PatchSource::ENVELOPE_1, *pitchDepth);
		expect(nested->value).to_equal(0x4001'4119u);
		// a third level: velocity (12) | (12+1)<<20
		auto third = cable_depth_id(PatchSource::VELOCITY, *nested);
		expect(third->value).to_equal(0x40D1'4119u);
		// no fourth
		expect(cable_depth_id(PatchSource::NOTE, *third)).not_().to_have_value();
	});

	it("routes a depth id back to its source and destination", _ {
		auto nested = *cable_depth_id(PatchSource::ENVELOPE_1, *cable_depth_id(PatchSource::LFO_GLOBAL_1, ParamId{25}));
		auto route = cable_route(nested);
		expect(to_string(route->source)).to_equal(std::string_view("envelope2"));
		expect(route->destination.value).to_equal(cable_depth_id(PatchSource::LFO_GLOBAL_1, ParamId{25})->value);
		auto inner = cable_route(route->destination);
		expect(to_string(inner->source)).to_equal(std::string_view("lfo1"));
		expect(inner->destination.value).to_equal(25u);
		expect(cable_route(ParamId{25})).not_().to_have_value();
	});

	it("keeps depth ids apart from param ids, unresolved destinations included", _ {
		expect(is_cable_depth(ParamId{116})).to_be_false();
		expect(is_cable_depth(*cable_depth_id(PatchSource::NONE, kUnknownDestination))).to_be_true();
		expect(cable_route(*cable_depth_id(PatchSource::X, kUnknownDestination))->destination.value)
		    .to_equal(kUnknownDestination.value);
		expect(cable_depth_id(PatchSource::X, ParamId{300})).not_().to_have_value();
	});
});

CPPSPEC_SPEC(cables)
