#include "CppUTest/TestHarness.h"
#include <cstdint>
#include <cstdlib>

// Metronome position math — extracted logic matching playback_handler.cpp
// These test the pure math that determines beat/bar boundaries from clip position.

namespace {

bool isAtBeat(int32_t lastProcessedPos, uint32_t beatLen) {
	uint32_t metroPos = static_cast<uint32_t>(std::abs(lastProcessedPos));
	return (metroPos % beatLen) == 0;
}

bool isAtBarStart(int32_t lastProcessedPos, uint32_t barLen) {
	uint32_t metroPos = static_cast<uint32_t>(std::abs(lastProcessedPos));
	return (metroPos % barLen) == 0;
}

// Convert clip-local ticks-til-next-beat to global ticks (ceiling division)
int32_t clipTicksToGlobalTicks(int32_t clipTicks, uint16_t num, uint16_t den) {
	if (num == den) {
		return clipTicks;
	}
	return static_cast<int32_t>((static_cast<int64_t>(clipTicks) * den + num - 1) / num);
}

} // namespace

TEST_GROUP(PolymeterMetronome){};

// ===== Beat/bar detection from clip position =====

TEST(PolymeterMetronome, fourFourDownbeatAtZero) {
	// 4/4: barLen = 96, beatLen = 24 (at base tick magnitude)
	CHECK(isAtBarStart(0, 96));
	CHECK(isAtBeat(0, 24));
}

TEST(PolymeterMetronome, fourFourBeatBoundaries) {
	// Beats at 0, 24, 48, 72; bar at 0, 96
	CHECK(isAtBeat(24, 24));
	CHECK(isAtBeat(48, 24));
	CHECK(isAtBeat(72, 24));
	CHECK(!isAtBeat(12, 24));     // not a beat
	CHECK(!isAtBarStart(24, 96)); // beat but not bar
	CHECK(!isAtBarStart(48, 96));
	CHECK(isAtBarStart(96, 96)); // next bar
}

TEST(PolymeterMetronome, sevenEightTimeSig) {
	// 7/8: barLen = 84 (7 * 12), beatLen = 12
	CHECK(isAtBarStart(0, 84));
	CHECK(isAtBeat(12, 12));
	CHECK(isAtBeat(24, 12));
	CHECK(!isAtBarStart(12, 84));
	CHECK(isAtBarStart(84, 84)); // next bar
	CHECK(isAtBeat(84, 12));     // also a beat
}

TEST(PolymeterMetronome, fiveFourTimeSig) {
	// 5/4: barLen = 120 (5 * 24), beatLen = 24
	CHECK(isAtBarStart(0, 120));
	CHECK(isAtBeat(24, 24));       // beat boundary
	CHECK(!isAtBarStart(24, 120)); // not a bar boundary
	CHECK(isAtBarStart(120, 120)); // next bar
}

TEST(PolymeterMetronome, sixEightTimeSig) {
	// 6/8: barLen = 72 (6 * 12), beatLen = 12
	CHECK(isAtBarStart(0, 72));
	CHECK(isAtBeat(12, 12));
	CHECK(isAtBarStart(72, 72));
}

TEST(PolymeterMetronome, negativePositionReverse) {
	// Reverse playback: lastProcessedPos is negative
	// abs(-24) = 24, which is a beat boundary in 4/4
	CHECK(isAtBeat(-24, 24));
	CHECK(!isAtBarStart(-24, 96));
	CHECK(isAtBarStart(-96, 96)); // abs(-96) % 96 == 0
}

TEST(PolymeterMetronome, negativePositionNotOnBeat) {
	CHECK(!isAtBeat(-13, 24));
	CHECK(!isAtBarStart(-50, 96));
}

TEST(PolymeterMetronome, positionBeyondLoopLength) {
	// Transient overrun: pos > loopLength before wrap
	// abs(100) % 24 == 4, not a beat
	CHECK(!isAtBeat(100, 24));
	// abs(192) % 96 == 0, would be a bar start
	CHECK(isAtBarStart(192, 96));
}

// ===== Scheduling conversion: clip ticks to global ticks =====

TEST(PolymeterMetronome, schedulingNoRatio) {
	// 1:1 ratio — no conversion needed
	CHECK_EQUAL(24, clipTicksToGlobalTicks(24, 1, 1));
	CHECK_EQUAL(96, clipTicksToGlobalTicks(96, 1, 1));
}

TEST(PolymeterMetronome, schedulingThreeToTwo) {
	// 3:2 ratio: clip runs 1.5x. 24 clip ticks = ceil(24 * 2 / 3) = 16 global ticks
	CHECK_EQUAL(16, clipTicksToGlobalTicks(24, 3, 2));
	// 12 clip ticks = ceil(12 * 2 / 3) = 8 global ticks
	CHECK_EQUAL(8, clipTicksToGlobalTicks(12, 3, 2));
}

TEST(PolymeterMetronome, schedulingCeilingDivision) {
	// 3:2 ratio: 1 clip tick = ceil(1 * 2 / 3) = ceil(0.67) = 1 global tick
	CHECK_EQUAL(1, clipTicksToGlobalTicks(1, 3, 2));
	// 2 clip ticks = ceil(2 * 2 / 3) = ceil(1.33) = 2 global ticks
	CHECK_EQUAL(2, clipTicksToGlobalTicks(2, 3, 2));
}

TEST(PolymeterMetronome, schedulingTwoToThree) {
	// 2:3 ratio: clip runs 0.67x. 24 clip ticks = ceil(24 * 3 / 2) = 36 global ticks
	CHECK_EQUAL(36, clipTicksToGlobalTicks(24, 2, 3));
}

TEST(PolymeterMetronome, schedulingFourToThree) {
	// 4:3 ratio: 24 clip ticks = ceil(24 * 3 / 4) = 18 global ticks
	CHECK_EQUAL(18, clipTicksToGlobalTicks(24, 4, 3));
}
