#include "gui/menu_item/value_scaling.h"
#include "definitions_cxx.hpp"

#include <cstdint>

int32_t computeCurrentValueForUnsignedMenuItem(uint32_t value) {
	return ((int64_t)value * kMaxMenuValue + 2147483648) >> 32;
}

uint32_t computeFinalValueForUnsignedMenuItem(int32_t value) {
	return (uint32_t)value * 85899345;
}

int32_t computeCurrentValueForTranspose(int32_t transpose, int32_t cents) {
	return transpose * 100 + cents;
}

void computeFinalValuesForTranspose(int32_t value, int32_t* transpose, int32_t* cents) {
	int32_t currentValue = value + 25600;
	int32_t semitones = (currentValue + 50) / 100;
	*cents = currentValue - semitones * 100;
	*transpose = semitones - 256;
}
