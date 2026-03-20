#include "CppUTest/TestHarness.h"
#include "playback/clock_output_scheduler.h"

// Default test ratios matching common Deluge configuration:
// Analog: internalTicksPer=24, outTicksPer=24 (1:1 at magnitude 1)
// Input:  inputTicksPer=3, internalTicksPerInput=6 (1:2 at magnitude 1)
namespace {
constexpr uint32_t kDefaultInternalTicksPer = 24;
constexpr uint32_t kDefaultOutTicksPer = 24;
constexpr uint32_t kDefaultInputTicksPer = 3;
constexpr uint32_t kDefaultInternalTicksPerInput = 6;
constexpr uint32_t kDefaultTempo = 20000; // ~20ms per input tick ≈ 125 BPM
constexpr uint32_t kDefaultInputTickTime = 100000;

// --- Group 1: Not behind — just schedule ---

TEST_GROUP(ClockScheduleNotBehind){};

TEST(ClockScheduleNotBehind, SchedulesNextTick) {
	// currentInternalTick = 0 * 6 / 3 = 0
	// nextOutTick = 1, internalTickForNextOut = 1
	// 1 > 0, not behind → schedule
	auto result =
	    computeExternalClockSchedule(0, 0, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
	                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

	CHECK_FALSE(result.shouldEmitTick);
	CHECK_FALSE(result.shouldResync);
	CHECK_TRUE(result.shouldSchedule);
	CHECK(result.scheduledTime > kDefaultInputTickTime);
}

TEST(ClockScheduleNotBehind, ComputesCorrectTime) {
	// currentInternalTick = 2 * 6 / 3 = 4
	// nextOutTick = 5, internalTickForNextOut = 5
	// 5 > 4, not behind. internalTicksUntilNext = 1
	// timeUntilNext = 1 * 20000 * 3 / 6 = 10000
	// scheduledTime = 100000 + 10000 = 110000
	auto result =
	    computeExternalClockSchedule(4, 2, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
	                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

	CHECK_FALSE(result.shouldEmitTick);
	CHECK_TRUE(result.shouldSchedule);
	CHECK_EQUAL(110000u, result.scheduledTime);
}

TEST(ClockScheduleNotBehind, AllFalseWhenZeroTempo) {
	auto result = computeExternalClockSchedule(0, 0, kDefaultInternalTicksPer, kDefaultOutTicksPer,
	                                           kDefaultInputTicksPer, kDefaultInternalTicksPerInput,
	                                           0, // timePerInputTickMovingAverage = 0
	                                           kDefaultInputTickTime);

	CHECK_FALSE(result.shouldEmitTick);
	CHECK_FALSE(result.shouldResync);
	CHECK_FALSE(result.shouldSchedule);
}

// --- Group 2: Behind by one — emit + schedule ---

TEST_GROUP(ClockScheduleBehindByOne){};

TEST(ClockScheduleBehindByOne, EmitsOneTick) {
	// currentInternalTick = 2 * 6 / 3 = 4
	// nextOutTick = 4, internalTickForNextOut = 4
	// 4 <= 4, behind (boundary) → emit
	// tickAfterEmit = 5, internalTickForTickAfterEmit = 5
	// 5 > 4 → schedule (not resync)
	auto result =
	    computeExternalClockSchedule(3, 2, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
	                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

	CHECK_TRUE(result.shouldEmitTick);
	CHECK_FALSE(result.shouldResync);
	CHECK_TRUE(result.shouldSchedule);
}

TEST(ClockScheduleBehindByOne, SchedulesBasedOnPostEmitState) {
	// After emit: nextOutTick=5, internalTickForNextOut=5
	// currentInternalTick=4, internalTicksUntilNext = 1
	// timeUntilNext = 1 * 20000 * 3 / 6 = 10000
	// scheduledTime = 100000 + 10000 = 110000
	auto result =
	    computeExternalClockSchedule(3, 2, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
	                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

	CHECK_EQUAL(110000u, result.scheduledTime);
}

// --- Group 3: Behind by many — emit + resync ---

TEST_GROUP(ClockScheduleBehindByMany){};

TEST(ClockScheduleBehindByMany, ResyncsWhenFarBehind) {
	// currentInternalTick = 10 * 6 / 3 = 20
	// nextOutTick=1, internalTickForNextOut=1
	// 1 <= 20, behind → emit
	// tickAfterEmit=2, internalTickForTickAfterEmit=2
	// 2 <= 20, still behind → resync
	auto result =
	    computeExternalClockSchedule(0, 10, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
	                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

	CHECK_TRUE(result.shouldEmitTick);
	CHECK_TRUE(result.shouldResync);
	CHECK_FALSE(result.shouldSchedule);
}

// --- Group 4: Edge cases ---

TEST_GROUP(ClockScheduleEdgeCases){};

TEST(ClockScheduleEdgeCases, ExactlyOnBoundary) {
	// currentInternalTick = 4, nextOutTick=4, internalTickForNextOut=4
	// 4 <= 4 → behind (boundary), should emit
	auto result =
	    computeExternalClockSchedule(3, 2, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
	                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

	CHECK_TRUE(result.shouldEmitTick);
}

TEST(ClockScheduleEdgeCases, ZeroInputTicks) {
	auto result =
	    computeExternalClockSchedule(0, 0, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
	                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

	// currentInternalTick=0, nextOutTick=1, internalTickForNextOut=1
	// 1 > 0, not behind → just schedule
	CHECK_FALSE(result.shouldEmitTick);
	CHECK_TRUE(result.shouldSchedule);
}

TEST(ClockScheduleEdgeCases, NegativeLastInputTick) {
	// lastInputTickReceived=-1 is a real initialization value
	auto result =
	    computeExternalClockSchedule(-1, -1, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
	                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

	// currentInternalTick = -1 * 6 / 3 = -2
	// nextOutTick = 0, internalTickForNextOut = 0
	// 0 > -2, not behind → schedule
	CHECK_FALSE(result.shouldEmitTick);
	CHECK_FALSE(result.shouldResync);
	CHECK_TRUE(result.shouldSchedule);
}

TEST(ClockScheduleEdgeCases, NegativeLastTickDone) {
	// lastTickDone=-1 (initial state) with a normal input tick
	auto result =
	    computeExternalClockSchedule(-1, 1, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
	                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

	// currentInternalTick = 1 * 6 / 3 = 2
	// nextOutTick = 0, internalTickForNextOut = 0
	// 0 <= 2, behind → emit
	// tickAfterEmit = 1, internalTickForTickAfterEmit = 1
	// 1 <= 2 → still behind → resync
	CHECK_TRUE(result.shouldEmitTick);
	CHECK_TRUE(result.shouldResync);
}

TEST(ClockScheduleEdgeCases, LargeTickCounts) {
	// Millions of ticks — verify no overflow
	auto result = computeExternalClockSchedule(1000000, 500001, kDefaultInternalTicksPer, kDefaultOutTicksPer,
	                                           kDefaultInputTicksPer, kDefaultInternalTicksPerInput, kDefaultTempo,
	                                           kDefaultInputTickTime);

	// currentInternalTick = 500001 * 6 / 3 = 1000002
	// nextOutTick = 1000001, internalTickForNextOut = 1000001
	// 1000001 <= 1000002, behind → emit
	// tickAfterEmit = 1000002, internalTickForTickAfterEmit = 1000002
	// 1000002 < 1000002? NO (boundary) → schedule at time=now for ISR
	CHECK_TRUE(result.shouldEmitTick);
	CHECK_FALSE(result.shouldResync);
	CHECK_TRUE(result.shouldSchedule);
	CHECK_EQUAL(kDefaultInputTickTime, result.scheduledTime);
}

TEST(ClockScheduleEdgeCases, OneToOneRatio) {
	auto result = computeExternalClockSchedule(5, 3, 1, 1, // 1:1 out-to-internal ratio
	                                           1, 2,       // 1:2 input-to-internal ratio
	                                           kDefaultTempo, kDefaultInputTickTime);

	// currentInternalTick = 3 * 2 / 1 = 6
	// nextOutTick = 6, internalTickForNextOut = 6
	// 6 <= 6, behind → emit
	// tickAfterEmit = 7, internalTickForTickAfterEmit = 7
	// 7 > 6 → schedule
	CHECK_TRUE(result.shouldEmitTick);
	CHECK_FALSE(result.shouldResync);
	CHECK_TRUE(result.shouldSchedule);
}

TEST(ClockScheduleEdgeCases, HighMultiplierRatio) {
	auto result = computeExternalClockSchedule(0, 2, 1, 4, // 1:4 ratio
	                                           3, 6, kDefaultTempo, kDefaultInputTickTime);

	// currentInternalTick = 2 * 6 / 3 = 4
	// nextOutTick=1, internalTickForNextOut = 1 * 1 / 4 = 0 (integer division)
	// 0 <= 4, behind → emit
	// tickAfterEmit=2, internalTickForTickAfterEmit = 2 * 1 / 4 = 0
	// 0 <= 4, still behind → resync
	CHECK_TRUE(result.shouldEmitTick);
	CHECK_TRUE(result.shouldResync);
}

// --- Group 5: Ratio math and overflow ---

TEST_GROUP(ClockScheduleRatioMath){};

TEST(ClockScheduleRatioMath, DefaultAnalogRatio) {
	auto result = computeExternalClockSchedule(9, 5, 24, 24, 3, 6, kDefaultTempo, kDefaultInputTickTime);

	// currentInternalTick = 10, nextOutTick=10, internalTickForNextOut=10
	// 10 <= 10, behind → emit
	// tickAfterEmit=11, internalTickForTickAfterEmit=11
	// 11 > 10 → schedule
	CHECK_TRUE(result.shouldEmitTick);
	CHECK_TRUE(result.shouldSchedule);
	CHECK_FALSE(result.shouldResync);
}

TEST(ClockScheduleRatioMath, DefaultMIDIRatio) {
	auto result = computeExternalClockSchedule(4, 5, 2, 1, 3, 6, kDefaultTempo, kDefaultInputTickTime);

	// currentInternalTick = 10, nextOutTick=5, internalTickForNextOut=10
	// 10 <= 10, behind → emit
	// tickAfterEmit=6, internalTickForTickAfterEmit=12
	// 12 > 10 → schedule
	CHECK_TRUE(result.shouldEmitTick);
	CHECK_TRUE(result.shouldSchedule);
}

TEST(ClockScheduleRatioMath, NonPowerOfTwoRatio) {
	auto result = computeExternalClockSchedule(2, 3, 3, 2, 3, 6, kDefaultTempo, kDefaultInputTickTime);

	// currentInternalTick = 6
	// nextOutTick=3, internalTickForNextOut = 9/2 = 4 (truncated)
	// 4 <= 6, behind → emit
	// tickAfterEmit=4, internalTickForTickAfterEmit = 12/2 = 6
	// 6 < 6? NO (boundary) → schedule at time=now for ISR
	CHECK_TRUE(result.shouldEmitTick);
	CHECK_FALSE(result.shouldResync);
	CHECK_TRUE(result.shouldSchedule);
	CHECK_EQUAL(kDefaultInputTickTime, result.scheduledTime);
}

TEST(ClockScheduleRatioMath, TimeComputationAccuracy) {
	auto result = computeExternalClockSchedule(9, 5, 24, 24, 3, 6, 20000, 100000);

	// After emit: nextOutTick=11, internalTickForNextOut=11
	// internalTicksUntilNext = 11 - 10 = 1
	// timeUntilNext = 1 * 20000 * 3 / 6 = 10000
	// scheduledTime = 100000 + 10000 = 110000
	CHECK_TRUE(result.shouldEmitTick);
	CHECK_TRUE(result.shouldSchedule);
	CHECK_EQUAL(110000u, result.scheduledTime);
}

TEST(ClockScheduleRatioMath, HighMagnitudeNoOverflow) {
	// internalTicksPer = 24 << 4 = 384, internalTicksPerInput = 6 << 4 = 96
	auto result = computeExternalClockSchedule(100, 50, 384, 24, 3, 96, 20000, 100000);

	// currentInternalTick = 50 * 96 / 3 = 1600
	// nextOutTick=101, internalTickForNextOut = 101 * 384 / 24 = 1616
	// 1616 > 1600, not behind → schedule
	// internalTicksUntilNext = 16
	// timeUntilNext = 16 * 20000 * 3 / 96 = 10000
	// scheduledTime = 100000 + 10000 = 110000
	CHECK_FALSE(result.shouldEmitTick);
	CHECK_TRUE(result.shouldSchedule);
	CHECK_EQUAL(110000u, result.scheduledTime);
}

} // namespace
