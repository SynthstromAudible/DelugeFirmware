#include "hid/display/rainfall.h"

#include "cppspec.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

using deluge::hid::display::Rainfall;

namespace {

/// @brief Every panel pixel a drop currently lights, clipped to the visible rows.
std::set<std::pair<int32_t, int32_t>> painted(const Rainfall& field, size_t drop) {
	std::set<std::pair<int32_t, int32_t>> pixels;
	for (size_t cell = 0; cell < field.lengthOf(drop); cell++) {
		const Rainfall::Block block = field.cellAt(drop, cell);
		for (int32_t dy = 0; dy < block.size; dy++) {
			for (int32_t dx = 0; dx < block.size; dx++) {
				const int32_t x = block.x + dx;
				const int32_t y = block.y + dy;
				if (x >= 0 && x < Rainfall::kWidth && y >= Rainfall::kTopmost && y < Rainfall::kHeight) {
					pixels.emplace(x, y);
				}
			}
		}
	}
	return pixels;
}

} // namespace

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

	it("moves every drop equally on both axes, so streaks never skid sideways", _ {
		// "steps every streak down-right at 45 degrees" above only checks the layout of cells
		// within a single streak at a single instant -- it would not notice advance() adding
		// different increments to x and y. This checks the thing that test cannot: that a drop's
		// head actually travels the same distance on both axes, across time.
		//
		// Exact equality per window will not do, even over a long span: cellAt() truncates float
		// positions to int32_t, and a drop's x and y generally start with different fractional
		// parts (they come from independent random draws), so the two axes cross pixel boundaries
		// at different frames. That truncation offset is bounded to one pixel -- structurally
		// guaranteed for a window that starts at a respawn, since place() pins one axis to an exact
		// integer there; only the very first window after scatter() is probabilistic, since scatter()
		// draws both axes continuously. It does not shrink with a longer window either way, so a
		// tolerance of one pixel is required and sufficient. A real per-axis
		// speed asymmetry instead grows with the window: over 50 frames it is already several
		// pixels, well outside that tolerance, so it cannot hide behind the truncation noise.
		//
		// Measured per drop over many 50-frame windows rather than one long one, because most
		// drops respawn well before 50*kNumDrops frames have passed; a single window per drop
		// starting at the first frame would mostly get discarded as a mid-window respawn, leaving
		// nothing measured.
		constexpr int32_t kFrames = 5000;
		constexpr int32_t kWindow = 50;
		Rainfall field;
		std::array<int32_t, Rainfall::kNumDrops> baseX{};
		std::array<int32_t, Rainfall::kNumDrops> baseY{};
		std::array<int32_t, Rainfall::kNumDrops> prevX{};
		std::array<int32_t, Rainfall::kNumDrops> prevY{};
		std::array<int32_t, Rainfall::kNumDrops> sinceBase{};
		for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
			const Rainfall::Block head = field.cellAt(drop, 0);
			baseX[drop] = head.x;
			baseY[drop] = head.y;
			prevX[drop] = head.x;
			prevY[drop] = head.y;
		}

		int32_t windowsMeasured = 0;
		for (int32_t frame = 0; frame < kFrames; frame++) {
			field.advance();
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				const Rainfall::Block head = field.cellAt(drop, 0);
				// A respawn teleports the drop backwards (up and/or left) rather than down-right;
				// restart the window there instead of letting the teleport masquerade as skid.
				if (head.x < prevX[drop] || head.y < prevY[drop]) {
					baseX[drop] = head.x;
					baseY[drop] = head.y;
					sinceBase[drop] = 0;
				}
				else {
					sinceBase[drop]++;
					if (sinceBase[drop] == kWindow) {
						const int32_t dx = head.x - baseX[drop];
						const int32_t dy = head.y - baseY[drop];
						const int32_t diff = (dx > dy) ? (dx - dy) : (dy - dx);
						expect(diff).to_be_less_than(2);
						windowsMeasured++;
						baseX[drop] = head.x;
						baseY[drop] = head.y;
						sinceBase[drop] = 0;
					}
				}
				prevX[drop] = head.x;
				prevY[drop] = head.y;
			}
		}
		// Guards against the assertion above passing vacuously because nothing was ever measured.
		expect(windowsMeasured).to_be_greater_than(100);
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

	it("lights every column, which is the whole point of a screensaver", _ {
		// A screensaver that never touches column 3 burns column 3. This is the only test that
		// checks the feature's actual purpose.
		Rainfall field;
		std::array<bool, Rainfall::kWidth> lit{};
		for (int32_t frame = 0; frame < 4000; frame++) {
			field.advance();
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				for (const auto& [x, y] : painted(field, drop)) {
					lit[static_cast<size_t>(x)] = true;
				}
			}
		}
		for (size_t x = 0; x < static_cast<size_t>(Rainfall::kWidth); x++) {
			expect(lit[x]).to_be_true();
		}
	});

	it("lights every row, so a place() regression can't leave the bottom dark", _ {
		// Mirrors "lights every column" above, but for rows. A regression in place()'s handling of
		// + kTopmost could leave the bottom rows permanently dark while every drop still spawns,
		// travels, and respawns normally -- drop counts and column coverage would stay healthy and
		// every other spec would still pass, so this is the only test that would catch it.
		Rainfall field;
		std::array<bool, Rainfall::kVisibleHeight> lit{};
		for (int32_t frame = 0; frame < 4000; frame++) {
			field.advance();
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				for (const auto& [x, y] : painted(field, drop)) {
					lit[static_cast<size_t>(y - Rainfall::kTopmost)] = true;
				}
			}
		}
		for (size_t y = 0; y < static_cast<size_t>(Rainfall::kVisibleHeight); y++) {
			expect(lit[y]).to_be_true();
		}
	});

	it("never lets the field drain", _ {
		// The failure mode if the left-edge spawn range or kSpawnFromTop is wrong: drops leave
		// faster than they arrive and the panel slowly empties. Measured worst case is 22 of 34.
		Rainfall field;
		for (int32_t frame = 0; frame < 10000; frame++) {
			field.advance();
			size_t visible = 0;
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				if (!painted(field, drop).empty()) {
					visible++;
				}
			}
			expect(visible).to_be_greater_than(static_cast<size_t>(15));
		}
	});

	it("keeps same-size drops from merging into blobs", _ {
		// Same-size drops may differ in speed, so a faster one can catch a slower one after spawn.
		// The spawn-time spacing rule keeps that to brief glances -- measured at 8.8% of frames
		// and 2.6 overlapping pixels. These bounds are loose around those figures, so this catches
		// a regression to permanent blobs without tripping every time a constant is retuned.
		constexpr int32_t kFrames = 2000;
		Rainfall field;
		int32_t framesWithOverlap = 0;
		int32_t totalOverlapPixels = 0;

		for (int32_t frame = 0; frame < kFrames; frame++) {
			field.advance();

			std::vector<std::set<std::pair<int32_t, int32_t>>> pixels;
			pixels.reserve(Rainfall::kNumDrops);
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				pixels.push_back(painted(field, drop));
			}

			int32_t overlap = 0;
			for (size_t a = 0; a < Rainfall::kNumDrops; a++) {
				for (size_t b = a + 1; b < Rainfall::kNumDrops; b++) {
					if (field.cellAt(a, 0).size != field.cellAt(b, 0).size) {
						continue;
					}
					for (const auto& pixel : pixels[b]) {
						if (pixels[a].contains(pixel)) {
							overlap++;
						}
					}
				}
			}

			if (overlap > 0) {
				framesWithOverlap++;
				totalOverlapPixels += overlap;
			}
		}

		expect(framesWithOverlap).to_be_less_than(kFrames / 5);
		expect(totalOverlapPixels).to_be_less_than(framesWithOverlap * 8 + 1);
	});
});

CPPSPEC_SPEC(rainfall)
