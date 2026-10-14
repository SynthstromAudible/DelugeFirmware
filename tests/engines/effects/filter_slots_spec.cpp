#include "engines/effects/filter_slots.h"

#include <cppspec.hpp>

using namespace deluge::effects;
using deluge::dsp::fixed::HighPassSlot;
using deluge::dsp::fixed::LowPassLadderMode;
using deluge::dsp::fixed::LowPassSlot;
using deluge::dsp::fixed::SvfMode;
namespace fixed = deluge::dsp::fixed;

// clang-format off
describe filter_slots("the effects' filter modes and routes as the filter set's slots", ${
	it("runs each low-pass mode in its slot", _ {
		expect(low_pass_slot(FilterMode::TRANSISTOR_12DB).kind).to_equal(LowPassSlot::Kind::LADDER);
		expect(low_pass_slot(FilterMode::TRANSISTOR_12DB).ladder).to_equal(LowPassLadderMode::TRANSISTOR_12DB);
		expect(low_pass_slot(FilterMode::TRANSISTOR_24DB).kind).to_equal(LowPassSlot::Kind::LADDER);
		expect(low_pass_slot(FilterMode::TRANSISTOR_24DB).ladder).to_equal(LowPassLadderMode::TRANSISTOR_24DB);
		expect(low_pass_slot(FilterMode::TRANSISTOR_24DB_DRIVE).kind).to_equal(LowPassSlot::Kind::LADDER);
		expect(low_pass_slot(FilterMode::TRANSISTOR_24DB_DRIVE).ladder)
		    .to_equal(LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(low_pass_slot(FilterMode::SVF_BAND).kind).to_equal(LowPassSlot::Kind::SVF);
		expect(low_pass_slot(FilterMode::SVF_BAND).svf).to_equal(SvfMode::BAND);
		expect(low_pass_slot(FilterMode::SVF_NOTCH).kind).to_equal(LowPassSlot::Kind::SVF);
		expect(low_pass_slot(FilterMode::SVF_NOTCH).svf).to_equal(SvfMode::NOTCH);
		expect(low_pass_slot(FilterMode::OFF).kind).to_equal(LowPassSlot::Kind::OFF);
	});
	it("runs no low-pass for the high-pass ladder's mode", _ {
		expect(low_pass_slot(FilterMode::HPLADDER).kind).to_equal(LowPassSlot::Kind::OFF);
	});
	it("runs each high-pass mode in its slot", _ {
		expect(high_pass_slot(FilterMode::HPLADDER).kind).to_equal(HighPassSlot::Kind::LADDER);
		expect(high_pass_slot(FilterMode::SVF_BAND).kind).to_equal(HighPassSlot::Kind::SVF);
		expect(high_pass_slot(FilterMode::SVF_BAND).svf).to_equal(SvfMode::BAND);
		expect(high_pass_slot(FilterMode::SVF_NOTCH).kind).to_equal(HighPassSlot::Kind::SVF);
		expect(high_pass_slot(FilterMode::SVF_NOTCH).svf).to_equal(SvfMode::NOTCH);
		expect(high_pass_slot(FilterMode::OFF).kind).to_equal(HighPassSlot::Kind::OFF);
	});
	it("runs no high-pass for the low-pass ladders' modes", _ {
		expect(high_pass_slot(FilterMode::TRANSISTOR_12DB).kind).to_equal(HighPassSlot::Kind::OFF);
		expect(high_pass_slot(FilterMode::TRANSISTOR_24DB).kind).to_equal(HighPassSlot::Kind::OFF);
		expect(high_pass_slot(FilterMode::TRANSISTOR_24DB_DRIVE).kind).to_equal(HighPassSlot::Kind::OFF);
	});
	it("keeps each route's order", _ {
		expect(filter_route(FilterRoute::HIGH_TO_LOW)).to_equal(fixed::FilterRoute::HIGH_TO_LOW);
		expect(filter_route(FilterRoute::LOW_TO_HIGH)).to_equal(fixed::FilterRoute::LOW_TO_HIGH);
		expect(filter_route(FilterRoute::PARALLEL)).to_equal(fixed::FilterRoute::PARALLEL);
	});
});
// clang-format on

CPPSPEC_SPEC(filter_slots)
