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
