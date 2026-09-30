#include "CppUTest/TestHarness.h"
#include "io/stall_bound.h"

using deluge::io::StallBound;

TEST_GROUP(StallBound){};

TEST(StallBound, DefaultsToTheChannelBound) {
	StallBound bound;
	CHECK_EQUAL(deluge::io::kChannelStallTicks, bound.limit());
	CHECK_EQUAL(600, bound.limit());
}

TEST(StallBound, ExpiresOnTheLimitTickAndNotBefore) {
	StallBound bound{3};
	CHECK_FALSE(bound.tick());
	CHECK_FALSE(bound.tick());
	CHECK_FALSE(bound.expired());
	CHECK_TRUE(bound.tick());
	CHECK_TRUE(bound.expired());
	CHECK_EQUAL(3, bound.ticks());
}

TEST(StallBound, StaysExpiredAndStopsCounting) {
	StallBound bound{1};
	CHECK_TRUE(bound.tick());
	CHECK_TRUE(bound.tick());
	CHECK_EQUAL(1, bound.ticks());
}

TEST(StallBound, ResetStartsOver) {
	StallBound bound{2};
	bound.tick();
	bound.tick();
	bound.reset();
	CHECK_FALSE(bound.expired());
	CHECK_EQUAL(0, bound.ticks());
}

TEST(StallBound, AStillProgressMarkCountsEveryTick) {
	StallBound bound{3};
	CHECK_FALSE(bound.tick(7u));
	CHECK_FALSE(bound.tick(7u));
	CHECK_TRUE(bound.tick(7u));
}

TEST(StallBound, AMovedProgressMarkStartsTheCountOver) {
	StallBound bound{3};
	CHECK_FALSE(bound.tick(7u));
	CHECK_FALSE(bound.tick(7u));
	// The channel finished something since the last tick: this tick is the first of a new count.
	CHECK_FALSE(bound.tick(8u));
	CHECK_EQUAL(1, bound.ticks());
	CHECK_FALSE(bound.tick(8u));
	CHECK_TRUE(bound.tick(8u));
}

TEST(StallBound, AChannelThatKeepsProgressingNeverExpires) {
	StallBound bound{2};
	for (uint32_t mark = 0; mark < 1000; ++mark) {
		CHECK_FALSE(bound.tick(mark));
	}
}

TEST(StallBound, ProgressMovingEveryFewTicksNeverExpires) {
	StallBound bound{3};
	for (uint32_t mark = 0; mark < 100; ++mark) {
		CHECK_FALSE(bound.tick(mark));
		CHECK_FALSE(bound.tick(mark));
	}
}

TEST(StallBound, TheFirstTickAfterAResetCountsWhateverTheMark) {
	StallBound bound{2};
	CHECK_FALSE(bound.tick(5u));
	bound.reset();
	// No reading survives a reset, so the first tick of a new wait counts rather than comparing against the last one.
	CHECK_FALSE(bound.tick(5u));
	CHECK_EQUAL(1, bound.ticks());
	CHECK_TRUE(bound.tick(5u));
}

TEST(StallBound, AWrappedMarkIsStillProgress) {
	StallBound bound{2};
	CHECK_FALSE(bound.tick(UINT32_MAX));
	CHECK_FALSE(bound.tick(0u));
	CHECK_EQUAL(1, bound.ticks());
}
