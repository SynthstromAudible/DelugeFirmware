#include "CppUTest/TestHarness.h"
#include <cstdint>

namespace {

// Standalone reimplementation of TimeSignature for testing (mirrors model/time_signature.h)
struct TimeSignature {
	uint8_t numerator{4};
	uint8_t denominator{4};

	[[nodiscard]] constexpr int32_t barLengthInBaseTicks() const {
		return static_cast<int32_t>(numerator) * (96 / denominator);
	}

	[[nodiscard]] constexpr int32_t beatLengthInBaseTicks() const { return 96 / denominator; }

	[[nodiscard]] constexpr bool isDefault() const { return numerator == 4 && denominator == 4; }
};

// ==================== barLengthInBaseTicks ====================

TEST_GROUP(TimeSignatureBarLength){};

TEST(TimeSignatureBarLength, FourFour) {
	TimeSignature ts{4, 4};
	CHECK_EQUAL(96, ts.barLengthInBaseTicks());
}

TEST(TimeSignatureBarLength, ThreeFour) {
	TimeSignature ts{3, 4};
	CHECK_EQUAL(72, ts.barLengthInBaseTicks());
}

TEST(TimeSignatureBarLength, SevenEight) {
	TimeSignature ts{7, 8};
	CHECK_EQUAL(84, ts.barLengthInBaseTicks());
}

TEST(TimeSignatureBarLength, SixEight) {
	TimeSignature ts{6, 8};
	CHECK_EQUAL(72, ts.barLengthInBaseTicks());
}

TEST(TimeSignatureBarLength, FiveFour) {
	TimeSignature ts{5, 4};
	CHECK_EQUAL(120, ts.barLengthInBaseTicks());
}

TEST(TimeSignatureBarLength, TwoTwo) {
	TimeSignature ts{2, 2};
	CHECK_EQUAL(96, ts.barLengthInBaseTicks()); // Same as 4/4
}

TEST(TimeSignatureBarLength, ThreeSixteen) {
	TimeSignature ts{3, 16};
	CHECK_EQUAL(18, ts.barLengthInBaseTicks());
}

TEST(TimeSignatureBarLength, NineEight) {
	TimeSignature ts{9, 8};
	CHECK_EQUAL(108, ts.barLengthInBaseTicks());
}

TEST(TimeSignatureBarLength, TwelveEight) {
	TimeSignature ts{12, 8};
	CHECK_EQUAL(144, ts.barLengthInBaseTicks());
}

// ==================== beatLengthInBaseTicks ====================

TEST_GROUP(TimeSignatureBeatLength){};

TEST(TimeSignatureBeatLength, QuarterNote) {
	TimeSignature ts{4, 4};
	CHECK_EQUAL(24, ts.beatLengthInBaseTicks());
}

TEST(TimeSignatureBeatLength, EighthNote) {
	TimeSignature ts{7, 8};
	CHECK_EQUAL(12, ts.beatLengthInBaseTicks());
}

TEST(TimeSignatureBeatLength, HalfNote) {
	TimeSignature ts{3, 2};
	CHECK_EQUAL(48, ts.beatLengthInBaseTicks());
}

TEST(TimeSignatureBeatLength, SixteenthNote) {
	TimeSignature ts{5, 16};
	CHECK_EQUAL(6, ts.beatLengthInBaseTicks());
}

// ==================== isDefault ====================

TEST_GROUP(TimeSignatureDefault){};

TEST(TimeSignatureDefault, FourFourIsDefault) {
	TimeSignature ts{4, 4};
	CHECK_TRUE(ts.isDefault());
}

TEST(TimeSignatureDefault, ThreeFourIsNotDefault) {
	TimeSignature ts{3, 4};
	CHECK_FALSE(ts.isDefault());
}

TEST(TimeSignatureDefault, FourEightIsNotDefault) {
	TimeSignature ts{4, 8};
	CHECK_FALSE(ts.isDefault());
}

// ==================== Standard denominators ====================

TEST_GROUP(TimeSignatureStandardDenominators){};

TEST(TimeSignatureStandardDenominators, AllDivideEvenly) {
	uint8_t denoms[] = {2, 4, 8, 16};
	for (auto d : denoms) {
		TimeSignature ts{1, d};
		int32_t beat = ts.beatLengthInBaseTicks();
		CHECK(beat > 0);
		CHECK_EQUAL(0, 96 % d);
	}
}

TEST(TimeSignatureStandardDenominators, NumeratorOne) {
	TimeSignature ts{1, 4};
	CHECK_EQUAL(24, ts.barLengthInBaseTicks()); // 1 quarter note
}

} // namespace
