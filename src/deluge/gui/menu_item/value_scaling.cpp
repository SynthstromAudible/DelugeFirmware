#include "gui/menu_item/value_scaling.h"
#include "definitions_cxx.hpp"

#include <cstdint>

int32_t computeCurrentValueForStandardMenuItem(int32_t value) {
	return (((int64_t)value + 2147483648) * kMaxMenuValue + 2147483648) >> 32;
}

int32_t computeFinalValueForStandardMenuItem(int32_t value) {
	if (value == kMaxMenuValue) {
		return 2147483647;
	}
	else if (value == kMinMenuValue) {
		return -2147483648;
	}
	else {
		return (uint32_t)value * (2147483648 / kMidMenuValue) - 2147483648;
	}
}
