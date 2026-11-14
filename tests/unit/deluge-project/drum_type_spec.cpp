#include "deluge-project/drum_type.h"

#include "cppspec.hpp"

#include <utility>

using deluge::project::DrumType;

// clang-format off
describe drum_type("DrumType", $ {
	it("keeps the values records store", _ {
		expect(std::to_underlying(DrumType::SOUND)).to_equal(0);
		expect(std::to_underlying(DrumType::MIDI)).to_equal(1);
		expect(std::to_underlying(DrumType::GATE)).to_equal(2);
	});
});

CPPSPEC_SPEC(drum_type)
