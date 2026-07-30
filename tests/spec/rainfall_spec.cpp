#include "hid/display/rainfall.h"

#include "cppspec.hpp"
#include <cmath>

using deluge::hid::display::Rainfall;

// clang-format off
describe rainfall("Rainfall", $ {
	context("the spacing rule", _ {
		it("measures across travel when two streaks overlap along it", _ {
			// Same position along travel, offset only across it. a and b both span u in [-12, 0],
			// and differ by dt = 8, so the true distance is 8/sqrt(2) and its square is 32.
			const Rainfall::Streak a{.x = 0.0f, .y = 0.0f, .size = 3, .length = 3};
			const Rainfall::Streak b{.x = 4.0f, .y = -4.0f, .size = 3, .length = 3};
			expect(std::abs(Rainfall::separationSquared(a, b) - 32.0f) < 0.01f).to_be_true();
		});

		it("adds the gap along travel when two streaks do not overlap along it", _ {
			// Identical track (dt = 0). a spans u in [-4, 0], b in [16, 20], so the gap along
			// travel is 16. Checked against the geometry: a's head is at (0, 0) and b's tail at
			// (8, 8), which are sqrt(128) apart.
			const Rainfall::Streak a{.x = 0.0f, .y = 0.0f, .size = 1, .length = 3};
			const Rainfall::Streak b{.x = 10.0f, .y = 10.0f, .size = 1, .length = 3};
			expect(std::abs(Rainfall::separationSquared(a, b) - 128.0f) < 0.01f).to_be_true();
		});

		it("reports no distance between a streak and itself", _ {
			const Rainfall::Streak a{.x = 12.0f, .y = 7.0f, .size = 2, .length = 4};
			expect(Rainfall::separationSquared(a, a)).to_equal(0.0f);
		});

		it("demands more room between larger blocks", _ {
			expect(Rainfall::minGapFor(3)).to_be_greater_than(Rainfall::minGapFor(2));
			expect(Rainfall::minGapFor(2)).to_be_greater_than(Rainfall::minGapFor(1));
		});
	});
});

CPPSPEC_SPEC(rainfall)
