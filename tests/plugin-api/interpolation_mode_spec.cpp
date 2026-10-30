#include "plugin-api/types/interpolation_mode.h"

#include "cppspec.hpp"

#include <utility>

using deluge::plugin::InterpolationMode;

// clang-format off
describe interpolation_mode("InterpolationMode", $ {
	it("keeps the values records and songs store", _ {
		expect(std::to_underlying(InterpolationMode::LINEAR)).to_equal(0);
		expect(std::to_underlying(InterpolationMode::SMOOTH)).to_equal(1);
	});
});

CPPSPEC_SPEC(interpolation_mode)
