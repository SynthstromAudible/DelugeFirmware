#include "plugin-api/types/arp_rhythm.h"

#include <cppspec.hpp>

#include <string>

using namespace deluge::plugin;

// clang-format off
describe arp_rhythm("arp rhythms", ${
	it("holds one entry per menu step", _ {
		expect(kArpRhythms.size()).to_equal(static_cast<size_t>(kMaxMenuSteps + 1));
		expect(kArpRhythms.size()).to_equal(static_cast<size_t>(51));
	});

	it("starts with the rhythm that plays every note", _ {
		expect(std::string(kArpRhythms[0].name)).to_equal(std::string("None"));
		expect(arp_rhythm_plays(0, 0)).to_be_true();
		expect(arp_rhythm_plays(0, 7)).to_be_true();
	});

	it("reads the first three-step rhythm", _ {
		expect(std::string(kArpRhythms[1].name)).to_equal(std::string("0--"));
		expect(static_cast<int>(kArpRhythms[1].length)).to_equal(3);
		expect(kArpRhythms[1].steps[1]).to_be_false();
	});

	it("keeps every length between one and six", _ {
		for (const auto& r : kArpRhythms) {
			expect(static_cast<int>(r.length)).to_be_between(1, 6);
		}
	});

	it("wraps the step at the pattern's length", _ {
		expect(arp_rhythm_plays(1, 0)).to_be_true();
		expect(arp_rhythm_plays(1, 1)).to_be_false();
		expect(arp_rhythm_plays(1, 3)).to_be_true();
		expect(arp_rhythm_plays(1, 4)).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(arp_rhythm)
