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
