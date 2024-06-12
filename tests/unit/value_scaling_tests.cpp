#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/value_scaling.h"

#include <iostream>

TEST_GROUP(ValueScalingTest){};

TEST(ValueScalingTest, standardMenuItemValueScaling) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		int32_t finalValue = computeFinalValueForStandardMenuItem(i);
		int32_t currentValue = computeCurrentValueForStandardMenuItem(finalValue);
		CHECK_EQUAL(i, currentValue);
	}
	CHECK_EQUAL(INT32_MIN, computeFinalValueForStandardMenuItem(0));
	CHECK_EQUAL(-23, computeFinalValueForStandardMenuItem(25));
	CHECK_EQUAL(INT32_MAX, computeFinalValueForStandardMenuItem(50));
}

TEST(ValueScalingTest, HalfPrecisionValueScaling) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		int32_t finalValue = computeFinalValueForHalfPrecisionMenuItem(i);
		int32_t currentValue = computeCurrentValueForHalfPrecisionMenuItem(finalValue);
		CHECK_EQUAL(i, currentValue);
	}
	CHECK_EQUAL(0, computeFinalValueForHalfPrecisionMenuItem(0));
	CHECK_EQUAL(1073741812, computeFinalValueForHalfPrecisionMenuItem(25));
	CHECK_EQUAL(INT32_MAX, computeFinalValueForHalfPrecisionMenuItem(50));
}

TEST(ValueScalingTest, panValueScaling) {
	for (int i = kMinMenuRelativeValue; i <= kMaxMenuRelativeValue; i++) {
		int32_t finalValue = computeFinalValueForPan(i);
		int32_t currentValue = computeCurrentValueForPan(finalValue);
		CHECK_EQUAL(i, currentValue);
	}
	CHECK_EQUAL(INT32_MIN, computeFinalValueForPan(-25));
	CHECK_EQUAL(0, computeFinalValueForPan(0));
	CHECK_EQUAL(INT32_MAX, computeFinalValueForPan(25));
}

TEST(ValueScalingTest, arpMidiCvGateValueScaling) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		int32_t finalValue = computeFinalValueForArpMidiCvGate(i);
		int32_t currentValue = computeCurrentValueForArpMidiCvGate(finalValue);
		CHECK_EQUAL(i, currentValue);
	}
	CHECK_EQUAL(INT32_MIN, computeFinalValueForArpMidiCvGate(0));
	CHECK_EQUAL(-23, computeFinalValueForArpMidiCvGate(25));
	// As seen here, we diverge _slightly_ from the standard
	// menu item scaling when computing final values, despite roundtripping
	// and using the identical current value computation.
	//
	// See computeFinalValueForArpMidiCvGate()'s comment for possible
	// motivation.
	CHECK_EQUAL(INT32_MAX - 45, computeFinalValueForArpMidiCvGate(50));
}
