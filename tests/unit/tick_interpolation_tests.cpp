#include "CppUTest/TestHarness.h"
#include "modulation/params/tick_interpolation.h"

TEST_GROUP(TickInterpolation){};

TEST(TickInterpolation, EveryTickAtNormalTempos) {
	// 120 BPM at the default magnitude is ~230 samples per tick
	CHECK_EQUAL(0, ticksTilNextTickInterpolation(230));
	CHECK_EQUAL(0, ticksTilNextTickInterpolation(kMinSamplesBetweenTickInterpolations));
}

TEST(TickInterpolation, SpacesOutUpdatesAtFastTempos) {
	// "FAST" (10000 BPM) is under 3 samples per tick - updating every tick there renders audio a few samples at a time
	int32_t ticks = ticksTilNextTickInterpolation(3);
	CHECK(ticks * 3 >= kMinSamplesBetweenTickInterpolations);
	CHECK((ticks - 1) * 3 < kMinSamplesBetweenTickInterpolations);
}

TEST(TickInterpolation, HandlesSubSampleTicks) {
	// Above ~27500 BPM a tick rounds down to 0 samples
	CHECK_EQUAL(kMinSamplesBetweenTickInterpolations, ticksTilNextTickInterpolation(0));
	CHECK_EQUAL(kMinSamplesBetweenTickInterpolations, ticksTilNextTickInterpolation(1));
}
