#include "CppUTest/TestHarness.h"
#include "gui/menu_item/value_scaling.h"
#include "definitions_cxx.hpp"

#include <iostream>

TEST_GROUP(ValueScalingTest) {};

TEST(ValueScalingTest, standardMenuItemValueRoundTrip) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		int32_t finalValue = computeFinalValueForStandardMenuItem(i);
		int32_t currentValue = computeCurrentValueForStandardMenuItem(finalValue);
		CHECK_EQUAL(i, currentValue);
	}
	CHECK_EQUAL(INT32_MIN, computeFinalValueForStandardMenuItem(0));
	CHECK_EQUAL(-23,       computeFinalValueForStandardMenuItem(25));
	CHECK_EQUAL(INT32_MAX, computeFinalValueForStandardMenuItem(50));
}
