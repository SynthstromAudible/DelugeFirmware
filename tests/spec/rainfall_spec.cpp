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
		// Exact equality, not a tolerance: cellAt() derives both axes from the same floored x, so a
		// drop cannot accumulate any offset between them however long it runs. A per-axis speed
		// asymmetry would grow with the window and show up here immediately.
		//
		// Measured per drop over many 50-frame windows rather than one long one, because most
		// drops respawn well before a single long window could close, which would leave nothing
		// measured at all.
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
						expect(diff).to_equal(0);
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

	it("steps on a repeating cadence, so slow drops move rather than stutter", _ {
		// A drop advances only when its accumulated position crosses a pixel boundary, so what the
		// eye reads as smooth is a REGULAR crossing schedule, not a fast one -- sub-pixel motion is
		// impossible on a 1-bit panel either way. Snapping every speed to a multiple of kSpeedStep
		// makes that schedule a short repeating cycle. An arbitrary real speed instead stalls a
		// drop for one frame at unpredictable moments, which reads as judder.
		//
		// kSpeedStep is a negative power of two, so the snapped speeds and the positions they
		// accumulate into are exactly representable: the cadence cannot drift however long the
		// screensaver runs.
		Rainfall field;
		for (int32_t frame = 0; frame < 3000; frame++) {
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				const float speed = field.speedOf(drop);
				const float steps = speed / Rainfall::kSpeedStep;
				expect(steps == std::floor(steps)).to_be_true();
				expect(speed).to_be_greater_than(0.0f);
			}
			field.advance();
		}
	});

	it("crosses pixel boundaries on both axes together, so drops never wiggle", _ {
		// The window test above measures net travel; this one measures every single step, which is
		// where the artefact it cannot see would live. If x and y cross pixel boundaries on
		// different frames, a drop advances right-only on one frame and down-only on the next,
		// oscillating about its 45 degree axis rather than travelling along it -- net travel still
		// comes out equal, but on a 3x3 block the motion reads as a wiggle.
		//
		// cellAt() derives both axes from the same floored x, so they cannot cross on different
		// frames whatever the float arithmetic does. The requirement is therefore absolute, not
		// statistical: every frame a drop moves at all, it moves equally on both axes.
		constexpr int32_t kFrames = 4000;
		Rainfall field;
		std::array<int32_t, Rainfall::kNumDrops> prevX{};
		std::array<int32_t, Rainfall::kNumDrops> prevY{};
		for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
			prevX[drop] = field.cellAt(drop, 0).x;
			prevY[drop] = field.cellAt(drop, 0).y;
		}

		int32_t moved = 0;
		int32_t skewed = 0;
		for (int32_t frame = 0; frame < kFrames; frame++) {
			field.advance();
			for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
				const Rainfall::Block head = field.cellAt(drop, 0);
				const int32_t dx = head.x - prevX[drop];
				const int32_t dy = head.y - prevY[drop];
				prevX[drop] = head.x;
				prevY[drop] = head.y;
				// A respawn teleports the drop backwards; it is not a step.
				if (dx < 0 || dy < 0) {
					continue;
				}
				if (dx != 0 || dy != 0) {
					moved++;
					if (dx != dy) {
						skewed++;
					}
				}
			}
		}

		expect(skewed).to_equal(0);
		// Guards against the assertion above passing vacuously because nothing ever moved.
		expect(moved).to_be_greater_than(10000);
	});

	context("logo emission", _ {
		it("carries the bootloader's own logo, as seven diagonal runs of three or four", _ {
			// The cell table is transcribed from the bootloader's logoPixels. A single wrong nibble
			// would be invisible in review and wrong on the panel, so decode all 25 and check the
			// shape they make: exactly seven runs stepping down-right, each 3 or 4 cells long --
			// which is what lets the logo be built from the rain's own vocabulary.
			Rainfall field;
			field.forceLogo();
			field.advance();
			expect(field.logoActive()).to_be_true();

			const int32_t scale = field.logoScale();
			std::set<std::pair<int32_t, int32_t>> cells;
			const Rainfall::Block origin = field.logoCellAt(0);
			for (size_t cell = 0; cell < Rainfall::kLogoCells; cell++) {
				const Rainfall::Block block = field.logoCellAt(cell);
				expect(block.size).to_equal(scale);
				// Recover the grid coordinate this cell sits at.
				const int32_t gx = (block.x - origin.x) / scale;
				const int32_t gy = (block.y - origin.y) / scale;
				cells.emplace(gx, gy);
			}
			expect(cells.size()).to_equal(Rainfall::kLogoCells);

			// Peel maximal down-right runs; every cell must belong to one of length 3 or 4.
			int32_t runs = 0;
			for (const auto& [x, y] : cells) {
				if (cells.contains({x - 1, y - 1})) {
					continue; // not the start of a run
				}
				int32_t length = 0;
				int32_t cx = x;
				int32_t cy = y;
				while (cells.contains({cx, cy})) {
					length++;
					cx++;
					cy++;
				}
				expect(length >= 3 && length <= 4).to_be_true();
				runs++;
			}
			expect(runs).to_equal(7);
		});

		it("keeps its scale and speed paired, like any drop", _ {
			// The logo is never speed-special-cased: having picked a cell scale it takes that
			// scale's native speed, so a big logo flashes past and a small one lingers. That is
			// correct parallax, and it is the reason every scale is allowed.
			Rainfall field;
			for (int32_t emission = 0; emission < 60; emission++) {
				field.forceLogo();
				while (!field.logoActive()) {
					field.advance();
				}
				const int32_t scale = field.logoScale();
				const float speed = field.logoSpeed();
				expect(scale >= 1 && scale <= 3).to_be_true();
				expect(speed).to_be_greater_than(0.0f);
				const float steps = speed / Rainfall::kSpeedStep;
				expect(steps == std::floor(steps)).to_be_true();
				// Larger scale is nearer, so never slower.
				expect(speed >= Rainfall::kSpeedFar && speed <= Rainfall::kSpeedNear).to_be_true();
				while (field.logoActive()) {
					field.advance();
				}
			}
		});

		it("holds its formation rigid and never wiggles", _ {
			// Every cell keeps a constant offset from the first, so the mark cannot shear as it
			// falls; and the whole logo obeys the same equal-step invariant the drops do, since it
			// shares their mechanism -- one integrated axis, the other derived.
			Rainfall field;
			field.forceLogo();
			while (!field.logoActive()) {
				field.advance();
			}

			std::array<int32_t, Rainfall::kLogoCells> offsetX{};
			std::array<int32_t, Rainfall::kLogoCells> offsetY{};
			const Rainfall::Block first = field.logoCellAt(0);
			for (size_t cell = 0; cell < Rainfall::kLogoCells; cell++) {
				offsetX[cell] = field.logoCellAt(cell).x - first.x;
				offsetY[cell] = field.logoCellAt(cell).y - first.y;
			}

			int32_t prevX = first.x;
			int32_t prevY = first.y;
			int32_t moved = 0;
			while (field.logoActive()) {
				field.advance();
				if (!field.logoActive()) {
					break;
				}
				const Rainfall::Block head = field.logoCellAt(0);
				const int32_t dx = head.x - prevX;
				const int32_t dy = head.y - prevY;
				expect(dx).to_equal(dy);
				if (dx != 0) {
					moved++;
				}
				prevX = head.x;
				prevY = head.y;
				for (size_t cell = 0; cell < Rainfall::kLogoCells; cell++) {
					expect(field.logoCellAt(cell).x - head.x).to_equal(offsetX[cell]);
					expect(field.logoCellAt(cell).y - head.y).to_equal(offsetY[cell]);
				}
			}
			expect(moved).to_be_greater_than(10);
		});

		it("retires and re-arms, so sightings keep coming", _ {
			// The failure this guards is a countdown that never restarts: one logo ever, then rain
			// for the rest of the session, which nobody would notice was broken.
			constexpr int32_t kFrames = 60000;
			Rainfall field;
			int32_t emissions = 0;
			int32_t lastEnd = 0;
			int32_t shortestGap = kFrames;
			int32_t longestGap = 0;
			bool wasActive = false;
			for (int32_t frame = 0; frame < kFrames; frame++) {
				field.advance();
				const bool isActive = field.logoActive();
				if (isActive && !wasActive) {
					emissions++;
					if (lastEnd > 0) {
						const int32_t gap = frame - lastEnd;
						shortestGap = (gap < shortestGap) ? gap : shortestGap;
						longestGap = (gap > longestGap) ? gap : longestGap;
					}
				}
				if (!isActive && wasActive) {
					lastEnd = frame;
				}
				wasActive = isActive;
			}
			expect(emissions).to_be_greater_than(5);
			expect(shortestGap).to_be_greater_than(Rainfall::kLogoIntervalMinFrames - 2);
			expect(longestGap).to_be_less_than(Rainfall::kLogoIntervalMaxFrames + 2);
		});

		it("weights the scale away from the camouflaged smallest one", _ {
			// At scale 1 the logo is single pixels at the far layer's own speed, so the drizzle
			// around it hides it. It stays possible, but must not be a third of sightings.
			Rainfall field;
			std::array<int32_t, 4> counts{};
			for (int32_t emission = 0; emission < 300; emission++) {
				field.forceLogo();
				while (!field.logoActive()) {
					field.advance();
				}
				counts[field.logoScale()]++;
				while (field.logoActive()) {
					field.advance();
				}
			}
			expect(counts[1]).to_be_greater_than(0);
			expect(counts[1]).to_be_less_than(counts[2]);
			expect(counts[1]).to_be_less_than(counts[3]);
			expect(counts[2]).to_be_greater_than(50);
			expect(counts[3]).to_be_greater_than(50);
		});

		it("does not emit one on activation, so a sighting stays a surprise", _ {
			Rainfall field;
			expect(field.logoActive()).to_be_false();
			field.scatter();
			expect(field.logoActive()).to_be_false();
		});
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
		// The spawn-time spacing rule keeps that to brief glances -- measured at 6.3% of frames
		// and 2.2 overlapping pixels. These bounds are loose around those figures, so this catches
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
