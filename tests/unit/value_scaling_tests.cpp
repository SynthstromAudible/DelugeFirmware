#include "CppUTest/TestHarness.h"
#include "gui/menu_item/value_scaling.h"
#include "definitions_cxx.hpp"

#include <iostream>

TEST_GROUP(ValueScalingTest) {};

TEST(ValueScalingTest, unpatchedParamValueRoundTrip) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		int32_t finalValue = computeFinalValueForUnpatchedParam(i);
		int32_t currentValue = computeCurrentValueForUnpatchedParam(finalValue);
		CHECK_EQUAL(i, currentValue);
	}
}
