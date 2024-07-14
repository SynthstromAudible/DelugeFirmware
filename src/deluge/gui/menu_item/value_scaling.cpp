#include "gui/menu_item/value_scaling.h"
#include "definitions_cxx.hpp"
#include "modulation/arpeggiator_rhythms.h"

#include <cstdint>

int32_t computeCurrentValueForStandardMenuItem(int32_t value) {
	return (((int64_t)value + 2147483648) * kMaxMenuValue + 2147483648) >> 32;
}

int32_t computeCurrentValueForHalfPrecisionMenuItem(int32_t value) {
	return ((int64_t)value * (kMaxMenuValue * 2) + 2147483648) >> 32;
}

int32_t computeCurrentValueForPan(int32_t value) {
	return ((int64_t)value * (kMaxMenuRelativeValue * 2) + 2147483648) >> 32;
}

int32_t computeCurrentValueForArpMidiCvRatchetsOrRhythm(uint32_t value) {
	return ((int64_t)value * kMaxMenuValue + 2147483648) >> 32;
}

int32_t computeFinalValueForStandardMenuItem(int32_t value) {
	if (value == kMaxMenuValue) {
		return 2147483647;
	}
	else if (value == kMinMenuValue) {
		return -2147483648;
	}
	else {
		// (2147483648 / kMidMenuValue) == 85899345
		return (uint32_t)value * 85899345 - 2147483648;
	}
}

int32_t computeFinalValueForHalfPrecisionMenuItem(int32_t value) {
	// comp params and osc pulse width aren't set up for negative inputs
	if (value == kMaxMenuValue) {
		return 2147483647;
	}
	else if (value == kMinMenuValue) {
		return 0;
	}
	else {
		return (uint32_t)value * (2147483648 / kMidMenuValue) >> 1;
	}
}

int32_t computeFinalValueForPan(int32_t value) {
	if (value == kMaxMenuRelativeValue) {
		return 2147483647;
	}
	else if (value == kMinMenuRelativeValue) {
		return -2147483648;
	}
	else {
		return ((int32_t)value * (2147483648 / (kMaxMenuRelativeValue * 2)) * 2);
	}
}

uint32_t computeFinalValueForArpMidiCvRatchetsOrRhythm(int32_t value) {
	return (uint32_t)value * 85899345;
}
