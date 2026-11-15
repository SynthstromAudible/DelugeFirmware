#include "gui/colour/rgb.h"
#include "hid/led/draw_square.h"
#include "util/functions.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

namespace {
constexpr RGB kRed{255, 0, 0};
constexpr RGB kGreen{0, 255, 0};
constexpr RGB kBlack{0, 0, 0};

void expectRgbNear(auto& self, RGB expected, RGB actual, int32_t tolerance) {
	expect(static_cast<int32_t>(actual.r)).to_be_within(tolerance).of(static_cast<int32_t>(expected.r));
	expect(static_cast<int32_t>(actual.g)).to_be_within(tolerance).of(static_cast<int32_t>(expected.g));
	expect(static_cast<int32_t>(actual.b)).to_be_within(tolerance).of(static_cast<int32_t>(expected.b));
}
} // namespace

// clang-format off
describe rgb_group("RGB", ${
	// blend(a, b, index) slides from b at index 0 to a at index 65535. Both endpoints must be a source colour: at
	// index 0 the weight for b is 65536, which does not fit a uint16_t, and a truncated weight returns black, making
	// transition animations flash black on any frame whose blend amount lands on zero.
	it("blend at zero index returns source b", _ {
		expectRgbNear(self, kGreen, RGB::blend(kRed, kGreen, 0), 1);
	});

	it("blend at max index returns source a", _ {
		expectRgbNear(self, kRed, RGB::blend(kRed, kGreen, 65535), 1);
	});

	// blend2() takes the two weights separately, and drawSquare() passes it a weight of 65536 ("keep all of the colour
	// already in this square"). That does not fit a uint16_t either; truncated, it wipes the square to black in exactly
	// the case where every bit of it is meant to be kept.
	it("blend2 keeps source a at full weight", _ {
		expectRgbNear(self, kRed, RGB::blend2(kRed, kGreen, 65536, 0), 1);
	});

	it("blend2 sums both sources at full weight", _ {
		// Full weight on both is the saturating case: each channel clamps at its own max rather than wrapping.
		expectRgbNear(self, RGB(255, 255, 0), RGB::blend2(kRed, kGreen, 65536, 65536), 1);
	});

	it("blend is monotonic across the range", _ {
		// Sweeping the index must never dip to black in the middle: r rises as g falls.
		int32_t previousR = -1;
		for (uint32_t index = 0; index <= 65535; index += 257) {
			RGB blended = RGB::blend(kRed, kGreen, index);
			expect(static_cast<int32_t>(blended.r)).not_().to_be_less_than(previousR);
			expect(static_cast<int32_t>(blended.r) + static_cast<int32_t>(blended.g)).not_().to_be_less_than(250);
			previousR = blended.r;
		}
	});
});

// drawSquare() composites one animated source square onto a destination pad. Every pad animation - clip collapse and
// expand, explode, fade, zoom, scroll - composites through it, so these pin the accumulation contract it relies on.
describe draw_square_group("draw square", ${
	it("drawing onto an empty square gives the colour", _ {
		uint8_t occupancy = 0;
		expectRgbNear(self, kRed, drawSquare(kRed, 65535, kBlack, &occupancy), 1);
		expect(occupancy).to_equal(64);
	});

	it("drawing at zero intensity leaves the square alone", _ {
		// A source row landing exactly on a pad boundary contributes zero intensity to the neighbouring pad. That must
		// leave the pad as it was, not wipe it to black.
		uint8_t occupancy = 64;
		expectRgbNear(self, kRed, drawSquare(kGreen, 0, kRed, &occupancy), 1);
	});

	it("overlapping contributions accumulate", _ {
		// Two half-intensity sources landing on one pad must both show up. Discarding what is already there leaves only
		// the last writer (pure green), and animations drop colour mid-flight.
		uint8_t occupancy = 0;
		RGB square = drawSquare(kRed, 32768, kBlack, &occupancy);
		square = drawSquare(kGreen, 32768, square, &occupancy);

		expectRgbNear(self, RGB(128, 128, 0), square, 1);
		expect(occupancy).to_equal(64);
	});

	it("occupancy saturates rather than wrapping", _ {
		uint8_t occupancy = 0;
		RGB square = kBlack;
		for (int32_t i = 0; i < 8; i++) {
			square = drawSquare(kRed, 65535, square, &occupancy);
		}
		expect(occupancy).to_equal(64);
		expectRgbNear(self, kRed, square, 1);
	});
});

DELUGE_SPEC(rgb_spec, rgb_group, draw_square_group)
