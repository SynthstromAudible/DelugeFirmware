#include "CppUTest/TestHarness.h"
#include <cstdint>

namespace {

// Standalone reimplementation of the Bresenham accumulator for testing.
// This mirrors the logic in Clip::scaleGlobalToClipTicks() and Clip::clipTicksToGlobalTicks().
struct TempoRatio {
	uint16_t numerator{1};
	uint16_t denominator{1};
	int32_t accumulator{0};

	int32_t scaleGlobalToClipTicks(int32_t globalTicks) {
		if (numerator == denominator) {
			return globalTicks;
		}
		bool neg = (globalTicks < 0);
		int32_t absTicks = neg ? -globalTicks : globalTicks;
		int32_t total = absTicks * numerator + accumulator;
		int32_t result = total / denominator;
		accumulator = total % denominator;
		return neg ? -result : result;
	}

	int32_t clipTicksToGlobalTicks(int32_t clipTicks) const {
		if (numerator == denominator) {
			return clipTicks;
		}
		return (clipTicks * denominator + numerator - 1) / numerator;
	}

	bool hasTempoRatio() const { return numerator != denominator; }
};

TEST_GROUP(TempoRatio){};

// 1:1 ratio should pass through unchanged
TEST(TempoRatio, OneToOnePassthrough) {
	TempoRatio r;
	CHECK_EQUAL(5, r.scaleGlobalToClipTicks(5));
	CHECK_EQUAL(0, r.accumulator);
	CHECK_EQUAL(10, r.clipTicksToGlobalTicks(10));
	CHECK_FALSE(r.hasTempoRatio());
}

// 7:8 ratio: 8 global ticks should produce exactly 7 clip ticks total
TEST(TempoRatio, SevenEighths_ExactOver8Ticks) {
	TempoRatio r{7, 8, 0};
	int32_t totalClipTicks = 0;
	for (int i = 0; i < 8; i++) {
		totalClipTicks += r.scaleGlobalToClipTicks(1);
	}
	CHECK_EQUAL(7, totalClipTicks);
	CHECK_EQUAL(0, r.accumulator); // Accumulator should be exactly 0 after full cycle
}

// 7:8 ratio: verify no drift over many cycles
TEST(TempoRatio, SevenEighths_NoDriftOverManyCycles) {
	TempoRatio r{7, 8, 0};
	int32_t totalClipTicks = 0;
	// 100 full cycles = 800 global ticks should produce exactly 700 clip ticks
	for (int i = 0; i < 800; i++) {
		totalClipTicks += r.scaleGlobalToClipTicks(1);
	}
	CHECK_EQUAL(700, totalClipTicks);
	CHECK_EQUAL(0, r.accumulator);
}

// 2:1 ratio (double speed): each global tick produces 2 clip ticks
TEST(TempoRatio, DoubleSpeed) {
	TempoRatio r{2, 1, 0};
	CHECK_EQUAL(2, r.scaleGlobalToClipTicks(1));
	CHECK_EQUAL(6, r.scaleGlobalToClipTicks(3));
}

// 1:2 ratio (half speed): every 2 global ticks produces 1 clip tick
TEST(TempoRatio, HalfSpeed) {
	TempoRatio r{1, 2, 0};
	CHECK_EQUAL(0, r.scaleGlobalToClipTicks(1)); // First tick: 0 (accumulator = 1)
	CHECK_EQUAL(1, r.scaleGlobalToClipTicks(1)); // Second tick: 1 (accumulator = 0)
	CHECK_EQUAL(0, r.accumulator);
}

// 3:4 ratio over 4 ticks
TEST(TempoRatio, ThreeFourths) {
	TempoRatio r{3, 4, 0};
	int32_t total = 0;
	for (int i = 0; i < 4; i++) {
		total += r.scaleGlobalToClipTicks(1);
	}
	CHECK_EQUAL(3, total);
	CHECK_EQUAL(0, r.accumulator);
}

// Multi-tick increment should match single-tick sequence
TEST(TempoRatio, MultiTickMatchesSingleTick) {
	TempoRatio r1{7, 8, 0};
	TempoRatio r2{7, 8, 0};

	// r1: 8 single-tick increments
	int32_t total1 = 0;
	for (int i = 0; i < 8; i++) {
		total1 += r1.scaleGlobalToClipTicks(1);
	}

	// r2: one 8-tick increment
	int32_t total2 = r2.scaleGlobalToClipTicks(8);

	CHECK_EQUAL(total1, total2);
	CHECK_EQUAL(r1.accumulator, r2.accumulator);
}

// Negative ticks (reverse playback)
TEST(TempoRatio, NegativeTicks) {
	TempoRatio r{2, 1, 0};
	CHECK_EQUAL(-4, r.scaleGlobalToClipTicks(-2));
}

// clipTicksToGlobalTicks ceiling division
TEST(TempoRatio, CeilingDivision_78) {
	TempoRatio r{7, 8, 0};
	// 7 clip ticks at 7:8 = ceil(7*8/7) = 8 global ticks
	CHECK_EQUAL(8, r.clipTicksToGlobalTicks(7));
	// 3 clip ticks = ceil(3*8/7) = ceil(3.43) = 4
	CHECK_EQUAL(4, r.clipTicksToGlobalTicks(3));
	// 1 clip tick = ceil(1*8/7) = ceil(1.14) = 2
	CHECK_EQUAL(2, r.clipTicksToGlobalTicks(1));
}

// clipTicksToGlobalTicks for 2:1 (double speed)
TEST(TempoRatio, CeilingDivision_DoubleSpeed) {
	TempoRatio r{2, 1, 0};
	// 4 clip ticks at 2:1 = ceil(4*1/2) = 2 global ticks
	CHECK_EQUAL(2, r.clipTicksToGlobalTicks(4));
	// 3 clip ticks = ceil(3*1/2) = ceil(1.5) = 2
	CHECK_EQUAL(2, r.clipTicksToGlobalTicks(3));
}

// clipTicksToGlobalTicks for 1:1 passthrough
TEST(TempoRatio, CeilingDivision_Passthrough) {
	TempoRatio r{1, 1, 0};
	CHECK_EQUAL(42, r.clipTicksToGlobalTicks(42));
}

// Accumulator reset doesn't affect long-term accuracy
TEST(TempoRatio, AccumulatorReset) {
	TempoRatio r{7, 8, 0};
	// Run 4 ticks, reset accumulator (simulating setPos), run 8 more
	for (int i = 0; i < 4; i++) {
		r.scaleGlobalToClipTicks(1);
	}
	r.accumulator = 0; // Reset
	int32_t total = 0;
	for (int i = 0; i < 8; i++) {
		total += r.scaleGlobalToClipTicks(1);
	}
	// After reset, a fresh full cycle should produce exactly 7
	CHECK_EQUAL(7, total);
	CHECK_EQUAL(0, r.accumulator);
}

// Large ratio values
TEST(TempoRatio, LargeRatio) {
	TempoRatio r{255, 256, 0};
	int32_t total = 0;
	for (int i = 0; i < 256; i++) {
		total += r.scaleGlobalToClipTicks(1);
	}
	CHECK_EQUAL(255, total);
	CHECK_EQUAL(0, r.accumulator);
}

} // namespace
