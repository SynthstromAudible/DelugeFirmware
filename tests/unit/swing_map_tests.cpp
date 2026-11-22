#include "CppUTest/TestHarness.h"
#include "playback/swing_map.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr SwingFeel kAllFeels[] = {SwingFeel::CLASSIC, SwingFeel::NESTED, SwingFeel::EVEN};

void checkLevel(const SwingLevels& levels, int32_t j, uint32_t halfTicks, int32_t amount) {
	CHECK_EQUAL(halfTicks, levels.level[j].halfTicks);
	CHECK_EQUAL(amount, levels.level[j].amount);
}

// Calls fn(levels) for every interval 1..9, amount -49..49 (non-zero) and feel
template <typename Fn>
void forAllSwings(Fn fn) {
	for (SwingFeel feel : kAllFeels) {
		for (int32_t interval = 1; interval <= 9; interval++) {
			for (int32_t amount = -49; amount <= 49; amount++) {
				if (amount != 0) {
					fn(computeSwingLevels(amount, interval, feel));
				}
			}
		}
	}
}

// Legacy (pre swing feel) internal-clock forward formula, from scheduleSwungTickFromInternalClock()
uint64_t legacyForwardTimeBig(uint32_t s, int32_t amount, int32_t interval, uint64_t tptBig, uint64_t timeLastBig,
                              uint64_t timeNextBig) {
	int32_t leftShift = std::max(9 - interval, 0);
	uint32_t swingInterval = 3 << (leftShift);
	if (s <= swingInterval) {
		uint64_t timeInBig = tptBig * s * (uint32_t)(50 + amount) / 50;
		return timeLastBig + timeInBig;
	}
	uint32_t swungTicksTilEnd = (swingInterval << 1) - s;
	uint64_t timeTilEndBig = tptBig * swungTicksTilEnd * (uint32_t)(50 - amount) / 50;
	return timeNextBig - timeTilEndBig;
}

// Legacy internal-clock inverse, from getNumSwungTicksInSinceLastTimerTick(). Returns -1 for its "return 1" quirk.
int32_t legacyInverseFromTimes(uint32_t timePassed, uint32_t timeTilNext, int32_t amount, int32_t interval,
                               uint32_t timePerTimerTick) {
	int32_t leftShift = std::max(9 - interval, 0);
	uint32_t swingInterval = 3 << (leftShift);
	uint32_t timePassedFiddledWith = (uint32_t)(timePassed * 50) / (uint32_t)(50 + amount);
	uint32_t ticksIn = timePassedFiddledWith / timePerTimerTick;
	if (ticksIn < swingInterval) {
		return ticksIn;
	}
	uint32_t timeTilNextFiddledWith = (uint32_t)(timeTilNext * 50) / (uint32_t)(50 - amount);
	if (!timeTilNextFiddledWith) {
		return -1;
	}
	int32_t ticksTilEnd = (uint32_t)(timeTilNextFiddledWith - 1) / (uint32_t)timePerTimerTick + 1;
	return (swingInterval << 1) - ticksTilEnd;
}

// Legacy external-clock inverse, from getActualSwungTickCount(), relative to the block start
float legacyInverseFloat(float posWithinSwingBlock, int32_t amount, int32_t interval) {
	int32_t leftShift = std::max(9 - interval, 0);
	uint32_t swingInterval = 3 << (leftShift);
	uint32_t doubleSwingInterval = swingInterval << 1;
	float swungTicksIn = (uint32_t)(posWithinSwingBlock * 50) / (uint32_t)(50 + amount);
	if (swungTicksIn < swingInterval) {
		return swungTicksIn;
	}
	float posTilEndOfSwingBlock = (float)doubleSwingInterval - posWithinSwingBlock;
	float swungTicksTilEnd = (uint32_t)(posTilEndOfSwingBlock * 50) / (uint32_t)(50 - amount);
	return doubleSwingInterval - (swungTicksTilEnd + 1);
}

} // namespace

TEST_GROUP(SwingMapLevels){};

TEST(SwingMapLevels, Classic) {
	SwingLevels levels = computeSwingLevels(8, 7, SwingFeel::CLASSIC);
	CHECK_EQUAL(1, levels.numLevels);
	checkLevel(levels, 0, 12, 8);
	CHECK_EQUAL(24, levels.blockTicks());
}

TEST(SwingMapLevels, NestedHalving) {
	SwingLevels levels = computeSwingLevels(8, 4, SwingFeel::NESTED);
	CHECK_EQUAL(3, levels.numLevels);
	checkLevel(levels, 0, 96, 8);
	checkLevel(levels, 1, 48, 4);
	checkLevel(levels, 2, 24, 2);
}

TEST(SwingMapLevels, NestedOddAndNegative) {
	SwingLevels levels = computeSwingLevels(7, 4, SwingFeel::NESTED);
	CHECK_EQUAL(3, levels.numLevels);
	checkLevel(levels, 1, 48, 3);
	checkLevel(levels, 2, 24, 1);

	levels = computeSwingLevels(-7, 4, SwingFeel::NESTED);
	CHECK_EQUAL(3, levels.numLevels);
	checkLevel(levels, 0, 96, -7);
	checkLevel(levels, 1, 48, -3);
	checkLevel(levels, 2, 24, -1);

	levels = computeSwingLevels(3, 4, SwingFeel::NESTED); // 3 -> 1 -> 0, so the last level is dropped
	CHECK_EQUAL(2, levels.numLevels);
	checkLevel(levels, 1, 48, 1);

	levels = computeSwingLevels(1, 4, SwingFeel::NESTED);
	CHECK_EQUAL(1, levels.numLevels);
}

TEST(SwingMapLevels, NestedDepthCutoff) {
	SwingLevels levels = computeSwingLevels(49, 8, SwingFeel::NESTED);
	CHECK_EQUAL(2, levels.numLevels);
	checkLevel(levels, 0, 6, 49);
	checkLevel(levels, 1, 3, 24);

	levels = computeSwingLevels(49, 9, SwingFeel::NESTED);
	CHECK_EQUAL(1, levels.numLevels);
	checkLevel(levels, 0, 3, 49);

	levels = computeSwingLevels(49, 1, SwingFeel::NESTED);
	CHECK_EQUAL(3, levels.numLevels);
	checkLevel(levels, 0, 768, 49);
	checkLevel(levels, 1, 384, 24);
	checkLevel(levels, 2, 192, 12);
}

TEST(SwingMapLevels, Even) {
	SwingLevels levels = computeSwingLevels(-20, 5, SwingFeel::EVEN);
	CHECK_EQUAL(3, levels.numLevels);
	checkLevel(levels, 0, 48, -20);
	checkLevel(levels, 1, 24, -20);
	checkLevel(levels, 2, 12, -20);
}

TEST(SwingMapLevels, ZeroAmount) {
	for (SwingFeel feel : kAllFeels) {
		CHECK_EQUAL(0, computeSwingLevels(0, 5, feel).numLevels);
	}
}

TEST(SwingMapLevels, IntervalAboveMaxBehavesLikeNine) {
	SwingLevels levels = computeSwingLevels(10, 12, SwingFeel::EVEN);
	CHECK_EQUAL(1, levels.numLevels);
	checkLevel(levels, 0, 3, 10);
}

TEST_GROUP(SwingMapProperties){};

TEST(SwingMapProperties, ForwardIsStrictlyMonotonic) {
	forAllSwings([](const SwingLevels& levels) {
		SwingBlockPos prev = swingForwardInBlock(levels, 0);
		for (uint32_t s = 1; s <= levels.blockTicks(); s++) {
			SwingBlockPos pos = swingForwardInBlock(levels, s);
			CHECK_EQUAL(prev.den, pos.den);
			CHECK(pos.num > prev.num);
			prev = pos;
		}
	});
}

TEST(SwingMapProperties, ForwardFixesBlockEdges) {
	forAllSwings([](const SwingLevels& levels) {
		uint64_t expectedDen = 1;
		for (int32_t j = 0; j < levels.numLevels; j++) {
			expectedDen *= 50;
		}
		SwingBlockPos start = swingForwardInBlock(levels, 0);
		SwingBlockPos end = swingForwardInBlock(levels, levels.blockTicks());
		CHECK_EQUAL(expectedDen, start.den);
		CHECK_EQUAL(0, start.num);
		CHECK_EQUAL((uint64_t)levels.blockTicks() * expectedDen, end.num);
	});
}

TEST(SwingMapProperties, InverseRoundTripsForward) {
	forAllSwings([](const SwingLevels& levels) {
		for (uint32_t s = 0; s <= levels.blockTicks(); s++) {
			SwingBlockPos pos = swingForwardInBlock(levels, s);
			CHECK_EQUAL(s, swingInverseInBlock(levels, pos.num, pos.den));
			// Same position with a different denominator
			CHECK_EQUAL(s, swingInverseInBlock(levels, pos.num * 7, pos.den * 7));
			if (s > 0) {
				CHECK_EQUAL(s - 1, swingInverseInBlock(levels, pos.num - 1, pos.den));
			}
		}
	});
}

TEST(SwingMapProperties, InverseClampsPastBlockEnd) {
	SwingLevels levels = computeSwingLevels(20, 7, SwingFeel::NESTED);
	CHECK_EQUAL(24, swingInverseInBlock(levels, 25, 1));
	CHECK_EQUAL(24, swingInverseInBlock(levels, 1000000, 1));
}

// At the midpoint of each level's block, that level's ratio (50 + amount) / 100 shows up unchanged
TEST(SwingMapProperties, PerLevelRatioPreserved) {
	forAllSwings([](const SwingLevels& levels) {
		uint32_t blockTicks = levels.blockTicks();
		for (int32_t j = 0; j < levels.numLevels; j++) {
			const SwingLevel& level = levels.level[j];
			// Blocks at the start and end of the primary block lie within one half of every coarser level
			for (uint32_t start : {0u, blockTicks - 2 * level.halfTicks}) {
				SwingBlockPos a = swingForwardInBlock(levels, start);
				SwingBlockPos mid = swingForwardInBlock(levels, start + level.halfTicks);
				SwingBlockPos b = swingForwardInBlock(levels, start + 2 * level.halfTicks);
				CHECK_EQUAL((mid.num - a.num) * 100, (b.num - a.num) * (uint64_t)(50 + level.amount));
			}
		}
	});
}

// 8th-note swing of 58 nested: the 16ths land at 54 within each swung 8th
TEST(SwingMapProperties, NestedExample58_54) {
	SwingLevels levels = computeSwingLevels(8, 4, SwingFeel::NESTED);
	const uint32_t swung[] = {0, 24, 48, 96, 144, 192};
	const uint64_t expected[] = {0, 3908736, 7516800, 13920000, 19363200, 24000000};
	for (int32_t i = 0; i < 6; i++) {
		SwingBlockPos pos = swingForwardInBlock(levels, swung[i]);
		CHECK_EQUAL(125000, pos.den);
		CHECK_EQUAL(expected[i], pos.num);
	}
	// 0, 60.1344, 111.36, 154.9056 internal ticks
	DOUBLES_EQUAL(60.1344, (double)swingForwardInBlock(levels, 48).num / 125000, 1e-9);
	DOUBLES_EQUAL(111.36, (double)swingForwardInBlock(levels, 96).num / 125000, 1e-9);
	DOUBLES_EQUAL(154.9056, (double)swingForwardInBlock(levels, 144).num / 125000, 1e-9);
}

TEST(SwingMapProperties, EvenAllLevelsSameAmount) {
	SwingLevels levels = computeSwingLevels(10, 4, SwingFeel::EVEN);
	SwingBlockPos pos = swingForwardInBlock(levels, 48);
	// Primary: 48 swung = first quarter of the block; the 16th level moves it to 48 * 60/50 = 57.6 inside the
	// first half of the primary block, which then scales it by 60/50 again: 69.12
	DOUBLES_EQUAL(69.12, (double)pos.num / pos.den, 1e-9);
}

TEST(SwingMapProperties, ScaleByPos) {
	SwingBlockPos pos = {7516800, 125000};
	const uint64_t values[] = {230ull << 32, (1000ull << 32) | 0x9ABCDEF0, 44100ull << 32, 1ull << 40};
	for (uint64_t value : values) {
		double exact = (double)value * 7516800.0 / 125000.0;
		double got = (double)swingScaleByPos(value, pos);
		CHECK(std::fabs(got - exact) < 65536.0 * 2 + exact * 1e-15);
		CHECK(swingScaleByPos(value, pos) <= (uint64_t)exact + 1);
	}
	CHECK_EQUAL(0, swingScaleByPos(12345, {0, 50}));
	CHECK_EQUAL(12345ull * 24, swingScaleByPos(12345, {24 * 50, 50}));
}

TEST_GROUP(SwingMapClassicNearLegacy){};

TEST(SwingMapClassicNearLegacy, ForwardTimeWithinOneTick) {
	const uint64_t tpts[] = {230ull << 32, (1000ull << 32) | 0x9ABCDEF0, 44100ull << 32};
	for (uint64_t tpt : tpts) {
		for (int32_t interval = 1; interval <= 9; interval++) {
			for (int32_t amount = -49; amount <= 49; amount++) {
				if (amount == 0) {
					continue;
				}
				SwingLevels levels = computeSwingLevels(amount, interval, SwingFeel::CLASSIC);
				uint64_t timeLastBig = 1000ull << 32;
				uint64_t timeNextBig = timeLastBig + tpt * levels.blockTicks();
				for (uint32_t s = 0; s <= levels.blockTicks(); s++) {
					uint64_t legacy = legacyForwardTimeBig(s, amount, interval, tpt, timeLastBig, timeNextBig);
					uint64_t ours = timeLastBig + swingScaleByPos(tpt, swingForwardInBlock(levels, s));
					uint64_t diff = legacy > ours ? legacy - ours : ours - legacy;
					CHECK(diff < tpt);
				}
			}
		}
	}
}

// The firmware feeds the inverse timePassed / timePerTick in 1/256 samples
TEST(SwingMapClassicNearLegacy, InverseFromTimesWithinOneTick) {
	const uint32_t tptrs[] = {230, 1000, 44100};
	for (uint32_t tptr : tptrs) {
		for (int32_t interval = 1; interval <= 9; interval++) {
			for (int32_t amount = -49; amount <= 49; amount++) {
				if (amount == 0) {
					continue;
				}
				SwingLevels levels = computeSwingLevels(amount, interval, SwingFeel::CLASSIC);
				uint32_t blockTicks = levels.blockTicks();
				uint32_t blockSamples = blockTicks * tptr;
				uint32_t stride = std::max<uint32_t>(blockSamples / 2000, 1) | 1;
				for (uint32_t t = 0; t < blockSamples; t += stride) {
					int32_t legacy = legacyInverseFromTimes(t, blockSamples - t, amount, interval, tptr);
					if (legacy < 0) {
						continue; // Legacy returned 1 in the last few samples of the block
					}
					int32_t ours = swingInverseInBlock(levels, (uint64_t)t << 8, ((uint64_t)tptr << 32) >> 24);
					CHECK(std::abs(ours - legacy) <= 1);
				}
			}
		}
	}
}

TEST(SwingMapClassicNearLegacy, InverseFromFloatWithinOneTick) {
	for (int32_t interval = 1; interval <= 9; interval++) {
		for (int32_t amount = -49; amount <= 49; amount++) {
			if (amount == 0) {
				continue;
			}
			SwingLevels levels = computeSwingLevels(amount, interval, SwingFeel::CLASSIC);
			float blockTicks = levels.blockTicks();
			for (float pos = 0; pos < blockTicks; pos += 0.125f) {
				float legacy = legacyInverseFloat(pos, amount, interval);
				uint32_t ours = swingInverseInBlock(levels, (uint64_t)(pos * 65536.f), 65536);
				CHECK(std::fabs((float)ours - legacy) <= 1.f);
			}
		}
	}
}
