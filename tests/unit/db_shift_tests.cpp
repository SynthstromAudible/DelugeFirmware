#include "CppUTest/TestHarness.h"
#include "util/db_shift.h"

#include <cstdint>
#include <cstdlib>
#include <limits>

TEST_GROUP(DbShiftTests){};

TEST(DbShiftTests, positiveOffsetIncreasesValue) {
	int32_t oldValue = 0;
	CHECK(shiftVolumeByDB(oldValue, 3.0f) > oldValue);
}

TEST(DbShiftTests, negativeOffsetDecreasesValue) {
	// The bug: negative offsets converted a negative float to uint32_t (UB) and the attenuation was discarded.
	int32_t oldValue = 0;
	CHECK(shiftVolumeByDB(oldValue, -3.0f) < oldValue);
}

TEST(DbShiftTests, zeroOffsetIsIdentity) {
	int32_t oldValue = 0x50000000;
	CHECK_EQUAL(oldValue, shiftVolumeByDB(oldValue, 0.0f));
}

TEST(DbShiftTests, intraIntervalPositionIsPreserved) {
	// The bug: howFarUpIntervalFloat used integer division and was always 0.0, so any value partway up an interval
	// was snapped down to the interval floor. Two values in the same interval must stay ordered after a shift.
	int32_t low = 0x50000000;
	int32_t high = 0x5F000000;
	CHECK(low < high);
	CHECK(shiftVolumeByDB(low, 1.0f) < shiftVolumeByDB(high, 1.0f));
}

TEST(DbShiftTests, largeNegativeOffsetSaturatesToSilence) {
	CHECK_EQUAL(std::numeric_limits<int32_t>::min(), shiftVolumeByDB(0, -500.0f));
}

TEST(DbShiftTests, largePositiveOffsetSaturatesToMaximum) {
	CHECK_EQUAL(std::numeric_limits<int32_t>::max(), shiftVolumeByDB(0, 500.0f));
}

TEST(DbShiftTests, roundTripApproximatelyRestores) {
	// Start mid-range. The upper intervals are narrow - 13, 14 and 15 together span only 3.6 dB - so a large offset
	// near the top saturates legitimately and cannot round-trip.
	int32_t oldValue = 0;
	int32_t shifted = shiftVolumeByDB(oldValue, 1.0f);
	CHECK(shifted > oldValue);

	int32_t restored = shiftVolumeByDB(shifted, -1.0f);
	// Allow for fixed-point rounding across two conversions.
	CHECK(std::abs(static_cast<int64_t>(restored) - oldValue) < (1 << 20));
}

TEST(DbShiftTests, saturatesRatherThanWrappingNearTheTop) {
	// 0x50000000 sits in interval 13; intervals 13-15 span only 3.6 dB, so +6 dB has nowhere to go.
	CHECK_EQUAL(std::numeric_limits<int32_t>::max(), shiftVolumeByDB(0x50000000, 6.0f));
}
