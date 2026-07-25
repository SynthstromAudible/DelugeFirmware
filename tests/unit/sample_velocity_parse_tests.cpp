#include "CppUTest/TestHarness.h"
#include "util/sample_velocity_parse.h"

TEST_GROUP(SampleVelocityParse){};

TEST(SampleVelocityParse, parsesShortVelPrefix) {
	uint8_t velocity = 0;
	CHECK_TRUE(tryParseVelocityFromFilename("Kick_C1_vel100.wav", &velocity));
	CHECK_EQUAL(100, velocity);
}

TEST(SampleVelocityParse, caseInsensitiveAndDashSeparated) {
	uint8_t velocity = 0;
	CHECK_TRUE(tryParseVelocityFromFilename("Snare-VEL64.wav", &velocity));
	CHECK_EQUAL(64, velocity);
}

TEST(SampleVelocityParse, matchesFullWordVelocity) {
	uint8_t velocity = 0;
	CHECK_TRUE(tryParseVelocityFromFilename("HatVelocity1.wav", &velocity));
	CHECK_EQUAL(1, velocity);
}

TEST(SampleVelocityParse, spaceSeparated) {
	uint8_t velocity = 0;
	CHECK_TRUE(tryParseVelocityFromFilename("Clap vel 127.wav", &velocity));
	CHECK_EQUAL(127, velocity);
}

TEST(SampleVelocityParse, noVelTokenReturnsFalse) {
	uint8_t velocity = 42;
	CHECK_FALSE(tryParseVelocityFromFilename("Kick_C1.wav", &velocity));
	CHECK_EQUAL(42, velocity); // untouched
}

TEST(SampleVelocityParse, bareVPrefixIsNotAMatch) {
	// A bare "v" collides with common version-tag conventions ("Kick_v2.wav" = version 2).
	uint8_t velocity = 0;
	CHECK_FALSE(tryParseVelocityFromFilename("Kick_v2.wav", &velocity));
}

TEST(SampleVelocityParse, valueAboveRealVelocityRangeIsRejected) {
	uint8_t velocity = 0;
	CHECK_FALSE(tryParseVelocityFromFilename("Tom_vel200.wav", &velocity));
}

TEST(SampleVelocityParse, zeroIsRejected) {
	uint8_t velocity = 0;
	CHECK_FALSE(tryParseVelocityFromFilename("Tom_vel000.wav", &velocity));
}

TEST(SampleVelocityParse, fourDigitRunIsRejectedNotTruncated) {
	// Looks like it could parse as "127" but the 4th digit means this is some other number
	// (e.g. a date or catalogue id), not a velocity - must not be silently truncated.
	uint8_t velocity = 0;
	CHECK_FALSE(tryParseVelocityFromFilename("Kick_vel1270.wav", &velocity));
}

TEST(SampleVelocityParse, lettersAfterVelWithNoDigitsIsNotAMatch) {
	uint8_t velocity = 0;
	CHECK_FALSE(tryParseVelocityFromFilename("Ride_velvet.wav", &velocity));
}

TEST(SampleVelocityParse, lastOccurrenceWins) {
	uint8_t velocity = 0;
	CHECK_TRUE(tryParseVelocityFromFilename("velocitySampleFolder_Kick_vel90.wav", &velocity));
	CHECK_EQUAL(90, velocity);
}

TEST(SampleVelocityParse, boundaryValuesAreAccepted) {
	uint8_t velocity = 0;
	CHECK_TRUE(tryParseVelocityFromFilename("A_vel1.wav", &velocity));
	CHECK_EQUAL(1, velocity);
	CHECK_TRUE(tryParseVelocityFromFilename("A_vel127.wav", &velocity));
	CHECK_EQUAL(127, velocity);
}

TEST_GROUP(VelocityLayerBoundaries){};

TEST(VelocityLayerBoundaries, twoSlotsAlreadyInOrder) {
	uint8_t velocities[] = {40, 100};
	uint8_t min[2], max[2];
	computeVelocityLayerBoundaries(velocities, 2, min, max);

	CHECK_EQUAL(1, min[0]);
	CHECK_EQUAL(70, max[0]); // (40+100)/2 = 70
	CHECK_EQUAL(71, min[1]);
	CHECK_EQUAL(127, max[1]);
}

TEST(VelocityLayerBoundaries, coversFullRangeWithNoGapsOrOverlaps) {
	uint8_t velocities[] = {40, 100};
	uint8_t min[2], max[2];
	computeVelocityLayerBoundaries(velocities, 2, min, max);

	CHECK_EQUAL(1, min[0]);
	CHECK_EQUAL(127, max[1]);
	CHECK_EQUAL(max[0] + 1, min[1]); // adjacent, no gap
}

TEST(VelocityLayerBoundaries, outputIsIndexedBySlotNotSortRank) {
	// Slot 0 (index 0) has the *highest* velocity here - output must still be indexed by the
	// original slot position, not by sorted rank.
	uint8_t velocities[] = {100, 40};
	uint8_t min[2], max[2];
	computeVelocityLayerBoundaries(velocities, 2, min, max);

	CHECK_EQUAL(71, min[0]); // slot 0 (velocity 100) got the upper half
	CHECK_EQUAL(127, max[0]);
	CHECK_EQUAL(1, min[1]); // slot 1 (velocity 40) got the lower half
	CHECK_EQUAL(70, max[1]);
}

TEST(VelocityLayerBoundaries, fourSlotsPartitionCleanly) {
	uint8_t velocities[] = {10, 50, 90, 120};
	uint8_t min[4], max[4];
	computeVelocityLayerBoundaries(velocities, 4, min, max);

	CHECK_EQUAL(1, min[0]);
	CHECK_EQUAL(127, max[3]);
	for (int i = 0; i < 3; i++) {
		CHECK_EQUAL(max[i] + 1, min[i + 1]); // every adjacent pair touches with no gap
	}
	for (int i = 0; i < 4; i++) {
		CHECK(min[i] <= velocities[i] && velocities[i] <= max[i]); // each slot's own value is in its own range
	}
}

TEST(VelocityLayerBoundaries, tiedValuesStillProduceNonOverlappingPartition) {
	uint8_t velocities[] = {64, 64};
	uint8_t min[2], max[2];
	computeVelocityLayerBoundaries(velocities, 2, min, max);

	CHECK(max[0] < min[1]); // no overlap, even for an exact tie
	CHECK_EQUAL(1, min[0]);
	CHECK_EQUAL(127, max[1]);
}

TEST(VelocityLayerBoundaries, singleSlotIsNoOp) {
	uint8_t velocities[] = {64};
	uint8_t min[1] = {99};
	uint8_t max[1] = {99};
	computeVelocityLayerBoundaries(velocities, 1, min, max);

	// count <= 1 is documented as a no-op - untouched.
	CHECK_EQUAL(99, min[0]);
	CHECK_EQUAL(99, max[0]);
}
