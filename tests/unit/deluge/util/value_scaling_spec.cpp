#include "definitions_cxx.hpp"
#include "modulation/arpeggiator_rhythms.h"
#include "util/value_scaling.h"

#include "cppspec.hpp"
#include "support/spec_main.h"
#include <vector>

// clang-format off
describe value_scaling_group("value scaling", ${
	it("consistent arp and menu max values", _ {
		// See comment above definition of kMaxPresetArpRhythm
		expect(kMaxPresetArpRhythm).to_equal(kMaxMenuValue);
		expect(kMaxPresetArpRhythm).to_equal(50);
	});

	it("unsigned menu item value scaling", _ {
		for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
			int32_t finalValue = computeFinalValueForUnsignedMenuItem(i);
			int32_t currentValue = computeCurrentValueForUnsignedMenuItem(finalValue);
			expect(currentValue).to_equal(i);
		}
		expect(computeFinalValueForUnsignedMenuItem(0)).to_equal(0);
		expect(computeFinalValueForUnsignedMenuItem(25)).to_equal(UINT32_MAX / 2 - 22);
		expect(computeFinalValueForUnsignedMenuItem(50)).to_equal(UINT32_MAX - 45);
		// while 50 doesn't quite get to UINT32_MAX, make sure the current value math
		// behaves well on the whole range
		expect(computeCurrentValueForUnsignedMenuItem(UINT32_MAX)).to_equal(50);
	});

	it("transpose", _ {
		// One expect over the values that fail to round-trip: CppSpec keeps a result per expect.
		std::vector<int> unlike;
		for (int i = -9600; i <= 9600; i++) {
			int32_t transpose, cents;
			computeFinalValuesForTranspose(i, &transpose, &cents);
			if (computeCurrentValueForTranspose(transpose, cents) != i) {
				unlike.push_back(i);
			}
		}
		expect(unlike).to_equal(std::vector<int>{});
		expect(computeCurrentValueForTranspose(0, 0)).to_equal(0);
		expect(computeCurrentValueForTranspose(1, 10)).to_equal(110);
		int32_t transpose, cents;
		computeFinalValuesForTranspose(110, &transpose, &cents);
		expect(transpose).to_equal(1);
		expect(cents).to_equal(10);
	});
});

DELUGE_SPEC(value_scaling_spec, value_scaling_group)
