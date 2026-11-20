#include "instruments/deluge/cables.h"

#include <cppspec.hpp>

#include <string_view>

using namespace deluge::instrument;

// clang-format off
describe cable_polarity("which cables have a polarity, and the polarity a new cable takes", ${
	it("gives X and Y no polarity and every other source one", _ {
		expect(has_polarity(PatchSource::X)).to_be_false();
		expect(has_polarity(PatchSource::Y)).to_be_false();
		expect(has_polarity(PatchSource::LFO_GLOBAL_1)).to_be_true();
		expect(has_polarity(PatchSource::AFTERTOUCH)).to_be_true();
		expect(has_polarity(PatchSource::SIDECHAIN)).to_be_true();
		expect(has_polarity(PatchSource::NONE)).to_be_true();
	});

	it("fixes aftertouch unipolar and X, Y and the sidechain bipolar", _ {
		expect(*fixed_polarity(PatchSource::AFTERTOUCH)).to_equal(Polarity::UNIPOLAR);
		expect(*fixed_polarity(PatchSource::SIDECHAIN)).to_equal(Polarity::BIPOLAR);
		expect(*fixed_polarity(PatchSource::X)).to_equal(Polarity::BIPOLAR);
		expect(*fixed_polarity(PatchSource::Y)).to_equal(Polarity::BIPOLAR);
	});

	it("leaves every other source to the device's default", _ {
		expect(fixed_polarity(PatchSource::ENVELOPE_0).has_value()).to_be_false();
		expect(fixed_polarity(PatchSource::LFO_LOCAL_2).has_value()).to_be_false();
		expect(fixed_polarity(PatchSource::VELOCITY).has_value()).to_be_false();
		expect(fixed_polarity(PatchSource::NONE).has_value()).to_be_false();
	});

	it("shows a polarity in four letters for the menu's column", _ {
		expect(to_short_string(Polarity::BIPOLAR)).to_equal(std::string_view("BPLR"));
		expect(to_short_string(Polarity::UNIPOLAR)).to_equal(std::string_view("UPLR"));
	});
});
// clang-format on

CPPSPEC_SPEC(cable_polarity)
