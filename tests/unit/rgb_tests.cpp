#include "CppUTest/TestHarness.h"
#include "gui/colour/rgb.h"
#include "util/functions.h"

namespace {
constexpr RGB kRed{255, 0, 0};
constexpr RGB kGreen{0, 255, 0};
constexpr RGB kBlack{0, 0, 0};

void CHECK_RGB_NEAR(RGB expected, RGB actual, int32_t tolerance) {
	CHECK_COMPARE(std::abs((int32_t)expected.r - (int32_t)actual.r), <=, tolerance);
	CHECK_COMPARE(std::abs((int32_t)expected.g - (int32_t)actual.g), <=, tolerance);
	CHECK_COMPARE(std::abs((int32_t)expected.b - (int32_t)actual.b), <=, tolerance);
}
} // namespace

TEST_GROUP(RGBTests){};

// blend(a, b, index) slides from b at index 0 to a at index 65535. Both endpoints must be a source colour:
// an index of 0 used to overflow the weight for b (65536) into a uint16_t and return black, which made
// transition animations flash black on any frame whose blend amount landed on zero.
TEST(RGBTests, blendAtZeroIndexReturnsSourceB) {
	CHECK_RGB_NEAR(kGreen, RGB::blend(kRed, kGreen, 0), 1);
}

TEST(RGBTests, blendAtMaxIndexReturnsSourceA) {
	CHECK_RGB_NEAR(kRed, RGB::blend(kRed, kGreen, 65535), 1);
}

// blend2() takes the two weights separately, and drawSquare() passes it a weight of 65536 ("keep all of the colour
// already in this square"). That does not fit a uint16_t either, so the square used to be wiped to black in exactly
// the case where every bit of it was meant to be kept.
TEST(RGBTests, blend2KeepsSourceAAtFullWeight) {
	CHECK_RGB_NEAR(kRed, RGB::blend2(kRed, kGreen, 65536, 0), 1);
}

TEST(RGBTests, blend2SumsBothSourcesAtFullWeight) {
	// Full weight on both is the saturating case: each channel clamps at its own max rather than wrapping.
	CHECK_RGB_NEAR(RGB(255, 255, 0), RGB::blend2(kRed, kGreen, 65536, 65536), 1);
}

TEST(RGBTests, blendIsMonotonicAcrossTheRange) {
	// Sweeping the index must never dip to black in the middle: r rises as g falls.
	int32_t previousR = -1;
	for (uint32_t index = 0; index <= 65535; index += 257) {
		RGB blended = RGB::blend(kRed, kGreen, index);
		CHECK_COMPARE((int32_t)blended.r, >=, previousR);
		CHECK_COMPARE((int32_t)blended.r + (int32_t)blended.g, >=, 250);
		previousR = blended.r;
	}
}

// drawSquare() composites one animated source square onto a destination pad. Every pad animation - clip collapse and
// expand, explode, fade, zoom, scroll - composites through it, so these pin the accumulation contract it relies on.
TEST_GROUP(DrawSquareTests){};

TEST(DrawSquareTests, drawingOntoAnEmptySquareGivesTheColour) {
	uint8_t occupancy = 0;
	CHECK_RGB_NEAR(kRed, drawSquare(kRed, 65535, kBlack, &occupancy, 64), 1);
	CHECK_EQUAL(64, occupancy);
}

TEST(DrawSquareTests, drawingAtZeroIntensityLeavesTheSquareAlone) {
	// A source row landing exactly on a pad boundary contributes zero intensity to the neighbouring pad. That must
	// leave the pad as it was; it used to wipe it to black.
	uint8_t occupancy = 64;
	CHECK_RGB_NEAR(kRed, drawSquare(kGreen, 0, kRed, &occupancy, 64), 1);
}

TEST(DrawSquareTests, overlappingContributionsAccumulate) {
	// Two half-intensity sources landing on one pad must both show up. Discarding what was already there would
	// leave only the last writer (pure green), which is what made animations drop colour mid-flight.
	uint8_t occupancy = 0;
	RGB square = drawSquare(kRed, 32768, kBlack, &occupancy, 64);
	square = drawSquare(kGreen, 32768, square, &occupancy, 64);

	CHECK_RGB_NEAR(RGB(128, 128, 0), square, 1);
	CHECK_EQUAL(64, occupancy);
}

TEST(DrawSquareTests, occupancySaturatesRatherThanWrapping) {
	uint8_t occupancy = 0;
	RGB square = kBlack;
	for (int32_t i = 0; i < 8; i++) {
		square = drawSquare(kRed, 65535, square, &occupancy, 64);
	}
	CHECK_EQUAL(64, occupancy);
	CHECK_RGB_NEAR(kRed, square, 1);
}
