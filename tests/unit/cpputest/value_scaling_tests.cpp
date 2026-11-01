#include "CppUTest/TestHarness.h"

#include "definitions_cxx.hpp"
#include "gui/menu_item/value_scaling.h"
#include "modulation/arpeggiator_rhythms.h"

TEST_GROUP(ValueScalingTest){};

TEST(ValueScalingTest, consistentArpAndMenuMaxValues) {
	// See comment above definition of kMaxPresetArpRhythm
	CHECK_EQUAL(kMaxMenuValue, kMaxPresetArpRhythm);
	CHECK_EQUAL(50, kMaxPresetArpRhythm);
}

TEST(ValueScalingTest, unsignedMenuItemValueScaling) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		int32_t finalValue = computeFinalValueForUnsignedMenuItem(i);
		int32_t currentValue = computeCurrentValueForUnsignedMenuItem(finalValue);
		CHECK_EQUAL(i, currentValue);
	}
	CHECK_EQUAL(0, computeFinalValueForUnsignedMenuItem(0));
	CHECK_EQUAL(UINT32_MAX / 2 - 22, computeFinalValueForUnsignedMenuItem(25));
	CHECK_EQUAL(UINT32_MAX - 45, computeFinalValueForUnsignedMenuItem(50));
	// while 50 doesn't quite get to UINT32_MAX, make sure the current value math
	// behaves well on the whole range
	CHECK_EQUAL(50, computeCurrentValueForUnsignedMenuItem(UINT32_MAX));
}

TEST(ValueScalingTest, transpose) {
	for (int i = -9600; i <= 9600; i++) {
		int32_t transpose, cents;
		computeFinalValuesForTranspose(i, &transpose, &cents);
		int32_t current = computeCurrentValueForTranspose(transpose, cents);
		CHECK_EQUAL(i, current);
	}
	CHECK_EQUAL(0, computeCurrentValueForTranspose(0, 0));
	CHECK_EQUAL(110, computeCurrentValueForTranspose(1, 10));
	int32_t transpose, cents;
	computeFinalValuesForTranspose(110, &transpose, &cents);
	CHECK_EQUAL(1, transpose);
	CHECK_EQUAL(10, cents);
}
