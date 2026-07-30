#include "hid/display/rainfall.h"

#include "cppspec.hpp"
#include <cmath>
#include <cstddef>

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

	it("steps every streak down-right at 45 degrees, in the logo's sizes", _ {
		Rainfall field;
		for (int32_t frame = 0; frame < 500; frame++) {
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				const size_t length = field.lengthOf(drop);
				expect(length >= 3 && length <= 4).to_be_true();

				const Rainfall::Block head = field.cellAt(drop, 0);
				expect(head.size >= 1 && head.size <= 3).to_be_true();

				for (size_t cell = 1; cell < length; cell++) {
					const Rainfall::Block block = field.cellAt(drop, cell);
					const int32_t step = static_cast<int32_t>(cell) * head.size;
					expect(block.size).to_equal(head.size);
					expect(block.x).to_equal(head.x - step);
					expect(block.y).to_equal(head.y - step);
				}
			}
			field.advance();
		}
	});

	it("respawns drops that leave the panel, so none run away", _ {
		// A drop is replaced within the same advance() that carries its tail off the panel, so
		// after any advance() every head sits close to the panel. The margins are loose: the
		// longest streak reaches 9px behind its head, and a spawn sits one pixel outside the edge.
		Rainfall field;
		for (int32_t frame = 0; frame < 10000; frame++) {
			field.advance();
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				const Rainfall::Block head = field.cellAt(drop, 0);
				expect(head.x).to_be_less_than(Rainfall::kWidth + 16);
				expect(head.y).to_be_less_than(Rainfall::kHeight + 16);
				expect(head.x).to_be_greater_than(-16);
				expect(head.y).to_be_greater_than(-16);
			}
		}
	});

	it("is deterministic, so the visual harness is reproducible", _ {
		Rainfall a;
		Rainfall b;
		for (int32_t frame = 0; frame < 200; frame++) {
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				expect(a.lengthOf(drop)).to_equal(b.lengthOf(drop));
				expect(a.cellAt(drop, 0).x).to_equal(b.cellAt(drop, 0).x);
				expect(a.cellAt(drop, 0).y).to_equal(b.cellAt(drop, 0).y);
				expect(a.cellAt(drop, 0).size).to_equal(b.cellAt(drop, 0).size);
			}
			a.advance();
			b.advance();
		}
	});

	it("re-scatters without reseeding, so activations differ", _ {
		Rainfall field;
		int32_t firstX[Rainfall::kNumDrops];
		for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
			firstX[drop] = field.cellAt(drop, 0).x;
		}

		field.scatter();

		bool anyDifferent = false;
		for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
			if (field.cellAt(drop, 0).x != firstX[drop]) {
				anyDifferent = true;
			}
		}
		expect(anyDifferent).to_be_true();
	});
});

CPPSPEC_SPEC(rainfall)
