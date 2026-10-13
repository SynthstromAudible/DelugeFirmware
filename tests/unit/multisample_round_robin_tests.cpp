#include "CppUTest/TestHarness.h"
#include "storage/multi_range/multisample_range.h"

TEST_GROUP(MultisampleRoundRobinTest){};

TEST(MultisampleRoundRobinTest, fourSlotsSequenceIsReadThenAdvance) {
	uint8_t rrCount = 3;
	uint8_t rrIndex = 0;

	for (int i = 0; i < 16; i++) {
		uint8_t slot = MultisampleRange::resolveNextSlotIndex(rrCount, rrIndex);
		CHECK_EQUAL(i % 4, slot);
	}
}

TEST(MultisampleRoundRobinTest, zeroOrOneAlternateMatchesLegacyAndSimpleCycle) {
	uint8_t rrIndex = 77;
	CHECK_EQUAL(0, MultisampleRange::resolveNextSlotIndex(0, rrIndex));
	CHECK_EQUAL(0, rrIndex);
	CHECK_EQUAL(0, MultisampleRange::resolveNextSlotIndex(0, rrIndex));
	CHECK_EQUAL(0, rrIndex);

	rrIndex = 0;
	CHECK_EQUAL(0, MultisampleRange::resolveNextSlotIndex(1, rrIndex));
	CHECK_EQUAL(1, rrIndex);
	CHECK_EQUAL(1, MultisampleRange::resolveNextSlotIndex(1, rrIndex));
	CHECK_EQUAL(0, rrIndex);
	CHECK_EQUAL(0, MultisampleRange::resolveNextSlotIndex(1, rrIndex));
	CHECK_EQUAL(1, rrIndex);
}

TEST(MultisampleRoundRobinTest, indexStaysStableOverThousandsOfCalls) {
	uint8_t rrCount = 3;
	uint8_t rrIndex = 200;

	for (int i = 0; i < 10000; i++) {
		uint8_t slot = MultisampleRange::resolveNextSlotIndex(rrCount, rrIndex);
		CHECK_EQUAL(i % 4, slot);
		CHECK_EQUAL((i + 1) % 4, rrIndex);
	}
}

// ---------------------------------------------------------------------------
// Random mode — verify all slots are reachable over many calls
// ---------------------------------------------------------------------------

TEST_GROUP(MultisampleRoundRobinRandomMode){};

TEST(MultisampleRoundRobinRandomMode, allSlotsReachableInFourSlotPool) {
	uint8_t rrCount = 3; // 4-slot pool: slots 0..3
	uint8_t rrIndex = 0;
	bool seen[4] = {false, false, false, false};

	// Exhaust all possible random input values for a 4-slot pool
	for (uint8_t r = 0; r <= rrCount; r++) {
		uint8_t slot = MultisampleRange::resolveRandomSlotIndex(rrCount, rrIndex, r);
		CHECK(slot < 4);
		seen[slot] = true;
	}
	CHECK_TRUE(seen[0] && seen[1] && seen[2] && seen[3]);
}

TEST(MultisampleRoundRobinRandomMode, rrIndexIsUpdatedToChosenSlot) {
	uint8_t rrCount = 3;
	uint8_t rrIndex = 99;
	uint8_t slot = MultisampleRange::resolveRandomSlotIndex(rrCount, rrIndex, 2);
	CHECK_EQUAL(2, slot);
	CHECK_EQUAL(2, rrIndex);
}

TEST(MultisampleRoundRobinRandomMode, singleSlotPoolAlwaysReturnsZero) {
	uint8_t rrCount = 0;
	uint8_t rrIndex = 7;
	uint8_t slot = MultisampleRange::resolveRandomSlotIndex(rrCount, rrIndex, 0);
	CHECK_EQUAL(0, slot);
	CHECK_EQUAL(0, rrIndex);
}

// ---------------------------------------------------------------------------
// NoRepeat mode — verify consecutive picks never match
// ---------------------------------------------------------------------------

TEST_GROUP(MultisampleRoundRobinNoRepeatMode){};

TEST(MultisampleRoundRobinNoRepeatMode, neverRepeatConsecutiveSlots) {
	uint8_t rrCount = 3; // 4-slot pool
	uint8_t lastResolved = 0;
	uint8_t results[50];

	for (int i = 0; i < 50; i++) {
		// Cycle randomChoice through [0, rrCount-1] for deterministic coverage
		uint8_t randomChoice = (uint8_t)(i % rrCount);
		results[i] = MultisampleRange::resolveNoRepeatSlotIndex(rrCount, lastResolved, randomChoice);
		CHECK(results[i] < 4);
		lastResolved = results[i];
	}

	for (int i = 1; i < 50; i++) {
		CHECK(results[i] != results[i - 1]);
	}
}

TEST(MultisampleRoundRobinNoRepeatMode, allSlotsReachableFromExclusion) {
	uint8_t rrCount = 3; // 4-slot pool: excluding slot 2, we want {0,1,3}
	bool seen[4] = {false, false, false, false};
	for (uint8_t r = 0; r < rrCount; r++) {
		uint8_t slot = MultisampleRange::resolveNoRepeatSlotIndex(rrCount, 2, r);
		CHECK(slot != 2);
		CHECK(slot < 4);
		seen[slot] = true;
	}
	CHECK_TRUE(seen[0] && seen[1] && !seen[2] && seen[3]);
}

TEST(MultisampleRoundRobinNoRepeatMode, twoSlotPoolAlternates) {
	uint8_t rrCount = 1; // 2-slot pool
	// Excluding slot 0 → must return 1
	CHECK_EQUAL(1, MultisampleRange::resolveNoRepeatSlotIndex(rrCount, 0, 0));
	// Excluding slot 1 → must return 0
	CHECK_EQUAL(0, MultisampleRange::resolveNoRepeatSlotIndex(rrCount, 1, 0));
}

TEST(MultisampleRoundRobinNoRepeatMode, singleSlotPoolAlwaysReturnsZero) {
	CHECK_EQUAL(0, MultisampleRange::resolveNoRepeatSlotIndex(0, 0, 0));
}

// ---------------------------------------------------------------------------
// Variant slot query tests — use static overloads, no object construction needed
// ---------------------------------------------------------------------------

TEST_GROUP(MultisampleRangeVariantSlots){};

TEST(MultisampleRangeVariantSlots, noAlternatesLoadedWhenCountIsZero) {
	CHECK_FALSE(MultisampleRange::hasAlternateLoaded(0, 0, nullptr));
}

TEST(MultisampleRangeVariantSlots, hasAlternateLoadedRequiresNonNullPointerInSlot) {
	RoundRobinAlternates alts{};
	CHECK_FALSE(MultisampleRange::hasAlternateLoaded(0, 1, &alts)); // slot ptr is null

	alts.slots[0] = reinterpret_cast<SampleHolderForVoice*>(1); // fake non-null
	CHECK_TRUE(MultisampleRange::hasAlternateLoaded(0, 1, &alts));
	CHECK_FALSE(MultisampleRange::hasAlternateLoaded(1, 1, &alts)); // idx >= count
}

TEST(MultisampleRangeVariantSlots, nextSlotToPopulateStartsAtZeroAndCapsAtMax) {
	CHECK_EQUAL(0u, MultisampleRange::getNextAlternateSlotToPopulate(0));
	CHECK_EQUAL(2u, MultisampleRange::getNextAlternateSlotToPopulate(2));
	CHECK_EQUAL(3u, MultisampleRange::getNextAlternateSlotToPopulate(3)); // caps at kMaxRoundRobinAlternates
}

TEST(MultisampleRangeVariantSlots, canPopulateSlotEnforcesSequentialFill) {
	CHECK_TRUE(MultisampleRange::canPopulateAlternateSlot(0, 0));  // first slot always OK
	CHECK_FALSE(MultisampleRange::canPopulateAlternateSlot(1, 0)); // can't skip to slot 2
	CHECK_TRUE(MultisampleRange::canPopulateAlternateSlot(1, 1));  // next in sequence
	CHECK_FALSE(MultisampleRange::canPopulateAlternateSlot(3, 3)); // OOB: kMax = 3
}

// ---------------------------------------------------------------------------
// Robustness against stale state - indices left behind after slots were
// cleared or after switching selection modes must never escape the pool.
// ---------------------------------------------------------------------------

TEST_GROUP(MultisampleRoundRobinStaleState){};

TEST(MultisampleRoundRobinStaleState, cycleClampsOutOfRangeIndexOnFirstCall) {
	// A slot was cleared while rrIndex pointed past the new pool size.
	uint8_t rrIndex = 3;
	uint8_t rrCount = 1; // pool is now {0, 1}
	CHECK_EQUAL(0, MultisampleRange::resolveNextSlotIndex(rrCount, rrIndex));
	CHECK_EQUAL(1, rrIndex);
}

TEST(MultisampleRoundRobinStaleState, noRepeatWithStaleLastResolvedStaysInPool) {
	// lastResolved can be stale (e.g. 3) after slots were cleared down to a 3-slot pool.
	uint8_t rrCount = 2; // pool {0, 1, 2}
	for (uint8_t r = 0; r < rrCount; r++) {
		uint8_t slot = MultisampleRange::resolveNoRepeatSlotIndex(rrCount, /*lastResolved=*/7, r);
		CHECK(slot <= rrCount);
	}
}

TEST(MultisampleRoundRobinStaleState, cycleAfterRandomModeStaysInPool) {
	// Random mode writes the picked slot into rrIndex; switching back to Cycle must keep cycling
	// correctly from there.
	uint8_t rrCount = 3;
	uint8_t rrIndex = 0;
	MultisampleRange::resolveRandomSlotIndex(rrCount, rrIndex, 3); // rrIndex = 3
	uint8_t slot = MultisampleRange::resolveNextSlotIndex(rrCount, rrIndex);
	CHECK_EQUAL(3, slot);
	CHECK_EQUAL(0, rrIndex); // wrapped
}

// ---------------------------------------------------------------------------
// Velocity mode — verify slot selection by incoming note velocity
// ---------------------------------------------------------------------------

TEST_GROUP(MultisampleRoundRobinVelocityMode){};

TEST(MultisampleRoundRobinVelocityMode, allDefaultFullRangeCyclesEverySlot) {
	// Every slot defaults to the full 1-127 range, so every slot matches at any velocity and the
	// whole pool round-robins - the same thing an MPC pad does before you narrow any layer, except
	// it stacks them where we alternate.
	RoundRobinAlternates alts{};
	uint8_t rrIndex = 0;
	CHECK_EQUAL(0, MultisampleRange::resolveVelocitySlotIndex(3, 1, &alts, rrIndex));
	CHECK_EQUAL(1, MultisampleRange::resolveVelocitySlotIndex(3, 64, &alts, rrIndex));
	CHECK_EQUAL(2, MultisampleRange::resolveVelocitySlotIndex(3, 127, &alts, rrIndex));
	CHECK_EQUAL(3, MultisampleRange::resolveVelocitySlotIndex(3, 100, &alts, rrIndex));
	CHECK_EQUAL(0, MultisampleRange::resolveVelocitySlotIndex(3, 100, &alts, rrIndex)); // wrapped
}

TEST(MultisampleRoundRobinVelocityMode, velocityInsideExactlyOneSlotRangeMatchesThatSlot) {
	RoundRobinAlternates alts{};
	alts.velocityRangeMin[0] = 1;
	alts.velocityRangeMax[0] = 40;
	alts.velocityRangeMin[1] = 41;
	alts.velocityRangeMax[1] = 80;
	alts.velocityRangeMin[2] = 81;
	alts.velocityRangeMax[2] = 127;

	uint8_t rrIndex = 0;
	CHECK_EQUAL(0, MultisampleRange::resolveVelocitySlotIndex(2, 20, &alts, rrIndex));
	CHECK_EQUAL(1, MultisampleRange::resolveVelocitySlotIndex(2, 60, &alts, rrIndex));
	CHECK_EQUAL(2, MultisampleRange::resolveVelocitySlotIndex(2, 100, &alts, rrIndex));
	// Non-overlapping bands hold exactly one slot each, so repeats stay put rather than cycling.
	CHECK_EQUAL(1, MultisampleRange::resolveVelocitySlotIndex(2, 60, &alts, rrIndex));
	CHECK_EQUAL(1, MultisampleRange::resolveVelocitySlotIndex(2, 60, &alts, rrIndex));
}

TEST(MultisampleRoundRobinVelocityMode, velocityInGapFallsBackToSlotZero) {
	// Slots cover 1-40 and 61-127; 41-60 is an unconfigured gap.
	RoundRobinAlternates alts{};
	alts.velocityRangeMin[0] = 1;
	alts.velocityRangeMax[0] = 40;
	alts.velocityRangeMin[1] = 61;
	alts.velocityRangeMax[1] = 127;

	uint8_t rrIndex = 0;
	CHECK_EQUAL(0, MultisampleRange::resolveVelocitySlotIndex(1, 50, &alts, rrIndex));
}

TEST(MultisampleRoundRobinVelocityMode, overlappingRangesCycleBetweenTheMatchingSlots) {
	RoundRobinAlternates alts{};
	alts.velocityRangeMin[0] = 1;
	alts.velocityRangeMax[0] = 100;
	alts.velocityRangeMin[1] = 50;
	alts.velocityRangeMax[1] = 127;

	// Velocity 70 is inside both slots' ranges, so repeated hits alternate between them rather
	// than one silently shadowing the other.
	uint8_t rrIndex = 0;
	CHECK_EQUAL(0, MultisampleRange::resolveVelocitySlotIndex(1, 70, &alts, rrIndex));
	CHECK_EQUAL(1, MultisampleRange::resolveVelocitySlotIndex(1, 70, &alts, rrIndex));
	CHECK_EQUAL(0, MultisampleRange::resolveVelocitySlotIndex(1, 70, &alts, rrIndex));

	// Velocity 120 is inside slot 1's range only, whatever the cycle position was.
	CHECK_EQUAL(1, MultisampleRange::resolveVelocitySlotIndex(1, 120, &alts, rrIndex));
	CHECK_EQUAL(1, MultisampleRange::resolveVelocitySlotIndex(1, 120, &alts, rrIndex));
}

TEST(MultisampleRoundRobinVelocityMode, velocityLayersEachRoundRobinTheirOwnTakes) {
	// The combination this buys us over a plain velocity switch: two layers of two takes each.
	// Slots 0-1 cover soft, slots 2-3 cover hard.
	RoundRobinAlternates alts{};
	alts.velocityRangeMin[0] = 1;
	alts.velocityRangeMax[0] = 64;
	alts.velocityRangeMin[1] = 1;
	alts.velocityRangeMax[1] = 64;
	alts.velocityRangeMin[2] = 65;
	alts.velocityRangeMax[2] = 127;
	alts.velocityRangeMin[3] = 65;
	alts.velocityRangeMax[3] = 127;

	uint8_t rrIndex = 0;

	// Soft hits alternate between the two soft takes...
	uint8_t soft1 = MultisampleRange::resolveVelocitySlotIndex(3, 30, &alts, rrIndex);
	uint8_t soft2 = MultisampleRange::resolveVelocitySlotIndex(3, 30, &alts, rrIndex);
	uint8_t soft3 = MultisampleRange::resolveVelocitySlotIndex(3, 30, &alts, rrIndex);
	CHECK_EQUAL(0, soft1);
	CHECK_EQUAL(1, soft2);
	CHECK_EQUAL(0, soft3);

	// ...and hard hits stay inside the hard pair, which is the guarantee that matters. Which of the
	// two you land on first depends on where the cycle had got to, since rrIndex is a position
	// carried across bands rather than per-band state. That is the useful way round: alternating
	// soft and hard hits keeps walking through each band's takes, where resetting per band would
	// pin you to the first take of each forever.
	uint8_t hard1 = MultisampleRange::resolveVelocitySlotIndex(3, 100, &alts, rrIndex);
	uint8_t hard2 = MultisampleRange::resolveVelocitySlotIndex(3, 100, &alts, rrIndex);
	uint8_t hard3 = MultisampleRange::resolveVelocitySlotIndex(3, 100, &alts, rrIndex);
	CHECK_EQUAL(3, hard1);
	CHECK_EQUAL(2, hard2);
	CHECK_EQUAL(3, hard3);
}

TEST(MultisampleRoundRobinVelocityMode, sentinel128ClampsToMaxVelocity) {
	// voice.cpp treats velocity == 128 as an internal "max" sentinel, never a real pad-hit
	// velocity. It must clamp to 127 rather than fail to match any slot's (max-127) range.
	RoundRobinAlternates alts{};
	alts.velocityRangeMin[0] = 1;
	alts.velocityRangeMax[0] = 100;
	alts.velocityRangeMin[1] = 101;
	alts.velocityRangeMax[1] = 127;

	uint8_t rrIndex = 0;
	CHECK_EQUAL(1, MultisampleRange::resolveVelocitySlotIndex(1, 128, &alts, rrIndex));
}

TEST(MultisampleRoundRobinVelocityMode, nullAlternatesFallsBackToDefaultFullRange) {
	// Defensive case: a hand-edited or corrupted song could claim RRMode::Velocity with rrCount > 0
	// but no alternates ever allocated. Every slot behaves as if its range is the 1-127 default, so
	// every slot matches and the pool round-robins.
	uint8_t rrIndex = 0;
	CHECK_EQUAL(0, MultisampleRange::resolveVelocitySlotIndex(2, 1, nullptr, rrIndex));
	CHECK_EQUAL(1, MultisampleRange::resolveVelocitySlotIndex(2, 127, nullptr, rrIndex));
	CHECK_EQUAL(2, MultisampleRange::resolveVelocitySlotIndex(2, 64, nullptr, rrIndex));
	CHECK_EQUAL(0, MultisampleRange::resolveVelocitySlotIndex(2, 64, nullptr, rrIndex)); // wrapped
}

TEST_GROUP(MultisampleVelocityRangeCompaction){};

namespace {
/// The bands a folder import of vel20/vel50/vel90/vel127 produces.
void setUpFourVelocityLayers(RoundRobinAlternates& alts) {
	const uint8_t mins[kMaxRoundRobinSlots] = {1, 36, 71, 109};
	const uint8_t maxs[kMaxRoundRobinSlots] = {35, 70, 108, 127};
	for (int i = 0; i < kMaxRoundRobinSlots; i++) {
		alts.velocityRangeMin[i] = mins[i];
		alts.velocityRangeMax[i] = maxs[i];
	}
}
} // namespace

TEST(MultisampleVelocityRangeCompaction, bandsFollowTheSamplesThatMovedDown) {
	RoundRobinAlternates alts;
	setUpFourVelocityLayers(alts);

	// Clear UI "SLOT 2" - alternate index 0 - out of a full four-slot zone.
	MultisampleRange::compactVelocityRangesAfterClear(alts, 0, 3);

	// Slot 0 (the primary) never moves.
	CHECK_EQUAL(1, alts.velocityRangeMin[0]);
	CHECK_EQUAL(35, alts.velocityRangeMax[0]);
	// The takes that slid down keep their own bands rather than inheriting the slot's.
	CHECK_EQUAL(71, alts.velocityRangeMin[1]);
	CHECK_EQUAL(108, alts.velocityRangeMax[1]);
	CHECK_EQUAL(109, alts.velocityRangeMin[2]);
	CHECK_EQUAL(127, alts.velocityRangeMax[2]);
	// The vacated top slot is back at the documented default.
	CHECK_EQUAL(MultisampleRange::kDefaultVelocityMin, alts.velocityRangeMin[3]);
	CHECK_EQUAL(MultisampleRange::kDefaultVelocityMax, alts.velocityRangeMax[3]);
}

TEST(MultisampleVelocityRangeCompaction, everyVelocityStillResolvesToALoadedSlot) {
	RoundRobinAlternates alts;
	setUpFourVelocityLayers(alts);
	MultisampleRange::compactVelocityRangesAfterClear(alts, 0, 3);

	uint8_t rrCount = 2; // one primary + two surviving alternates
	uint8_t rrIndex = 0;
	for (int v = 1; v <= 127; v++) {
		uint8_t slot = MultisampleRange::resolveVelocitySlotIndex(rrCount, (uint8_t)v, &alts, rrIndex);
		CHECK(slot <= rrCount);
	}
	// Before the fix nothing covered 109-127 any more, so the hardest hits fell back to slot 0 -
	// a full-force strike played the softest sample.
	CHECK_EQUAL(2, MultisampleRange::resolveVelocitySlotIndex(rrCount, 127, &alts, rrIndex));
	CHECK_EQUAL(2, MultisampleRange::resolveVelocitySlotIndex(rrCount, 120, &alts, rrIndex));
}

TEST(MultisampleVelocityRangeCompaction, clearingTheTopSlotOnlyResetsThatSlot) {
	RoundRobinAlternates alts;
	setUpFourVelocityLayers(alts);

	// Clear UI "SLOT 4" - the last alternate. Nothing shifts; only its band resets.
	MultisampleRange::compactVelocityRangesAfterClear(alts, 2, 3);

	CHECK_EQUAL(1, alts.velocityRangeMin[0]);
	CHECK_EQUAL(36, alts.velocityRangeMin[1]);
	CHECK_EQUAL(71, alts.velocityRangeMin[2]);
	CHECK_EQUAL(108, alts.velocityRangeMax[2]);
	CHECK_EQUAL(MultisampleRange::kDefaultVelocityMin, alts.velocityRangeMin[3]);
	CHECK_EQUAL(MultisampleRange::kDefaultVelocityMax, alts.velocityRangeMax[3]);
}
