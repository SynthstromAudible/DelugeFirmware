#include "engines/deluge/cables.h"

#include <cppspec.hpp>

using namespace deluge::instrument;
using deluge::engine::ParamId;

// clang-format off
describe cables("the Deluge instrument's patch-cable words and depth ids", ${
	it("names every source as the file does, and reads an unknown one as none", _ {
		expect(to_string(PatchSource::LFO_GLOBAL_2) == "lfo3").to_equal(true);
		expect(to_string(PatchSource::LFO_LOCAL_1) == "lfo2").to_equal(true);
		expect(to_string(PatchSource::SIDECHAIN) == "compressor").to_equal(true);
		expect(to_string(PatchSource::ENVELOPE_0) == "envelope1").to_equal(true);
		for (uint8_t s = 0; s <= static_cast<uint8_t>(PatchSource::NONE); ++s) {
			expect(parse_patch_source(to_string(static_cast<PatchSource>(s))) == static_cast<PatchSource>(s))
			    .to_equal(true);
		}
		expect(parse_patch_source("wobble") == PatchSource::NONE).to_equal(true);
	});

	it("reads a polarity, unknown as bipolar", _ {
		expect(parse_polarity("unipolar") == Polarity::UNIPOLAR).to_equal(true);
		expect(parse_polarity("x") == Polarity::BIPOLAR).to_equal(true);
		expect(to_string(Polarity::UNIPOLAR) == "unipolar").to_equal(true);
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
		expect(cable_depth_id(PatchSource::NOTE, *third).has_value()).to_equal(false);
	});

	it("routes a depth id back to its source and destination", _ {
		auto nested = *cable_depth_id(PatchSource::ENVELOPE_1, *cable_depth_id(PatchSource::LFO_GLOBAL_1, ParamId{25}));
		auto route = cable_route(nested);
		expect(route->source == PatchSource::ENVELOPE_1).to_equal(true);
		expect(route->destination == *cable_depth_id(PatchSource::LFO_GLOBAL_1, ParamId{25})).to_equal(true);
		auto inner = cable_route(route->destination);
		expect(inner->source == PatchSource::LFO_GLOBAL_1).to_equal(true);
		expect(inner->destination == ParamId{25}).to_equal(true);
		expect(cable_route(ParamId{25}).has_value()).to_equal(false);
	});

	it("keeps depth ids apart from param ids, unresolved destinations included", _ {
		expect(is_cable_depth(ParamId{116})).to_equal(false);
		expect(is_cable_depth(*cable_depth_id(PatchSource::NONE, kRangePlaceholder))).to_equal(true);
		expect(cable_route(*cable_depth_id(PatchSource::X, kUnknownDestination))->destination == kUnknownDestination)
		    .to_equal(true);
		expect(cable_depth_id(PatchSource::X, ParamId{300}).has_value()).to_equal(false);
	});
});

CPPSPEC_SPEC(cables)
