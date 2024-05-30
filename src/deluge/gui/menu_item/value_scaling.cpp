#include "gui/menu_item/value_scaling.h"
#include "definitions_cxx.hpp"
#include "modulation/arpeggiator.h"

#include <cstdint>

int32_t computeFinalValueForInteger(int32_t value) {
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

int32_t computeFinalValueForUnpatched(int32_t value) {
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

int32_t computeFinalValueForCompressor(int32_t value) {
	// comp params aren't set up for negative inputs - this is the same as osc pulse width
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

int32_t computeFinalValueForArpSeqLength(int32_t value) {
	return value * 85899345;
}

int32_t computeFinalValueForArpRhythm(int32_t value) {
	return value * 85899345;
}

int32_t computeFinalValueForArpGate(int32_t value) {
	return value * 85899345 - 2147483648;
}

int32_t computeFinalValueForArpRatchet(int32_t value) {
	return value * 85899345;
}

int32_t computeFinalValueForArpRatchetProbability(int32_t value) {
	return value * 85899345;
}

int32_t computeCurrentValueForInteger(int32_t value) {
	return (((int64_t)value + 2147483648) * kMaxMenuValue + 2147483648) >> 32;
}

int32_t computeCurrentValueForUnpatched(int32_t value) {
	return (((int64_t)value + 2147483648) * kMaxMenuValue + 2147483648) >> 32;
}

int32_t computeCurrentValueForPan(int32_t value) {
	return ((int64_t)value * (kMaxMenuRelativeValue * 2) + 2147483648) >> 32;
}

int32_t computeCurrentValueForCompressor(int32_t value) {
	return ((int64_t)value * (kMaxMenuValue * 2) + 2147483648) >> 32;
}

int32_t computeCurrentValueForArpGate(int32_t value) {
	return (((int64_t)value + 2147483648) * kMaxMenuValue + 2147483648) >> 32;
}

int32_t computeCurrentValueForArpRatchet(int32_t value) {
	return ((int64_t)value * kMaxMenuValue + 2147483648) >> 32;
}

int32_t computeCurrentValueForArpRatchetProbability(int32_t value) {
	return ((int64_t)value * kMaxMenuValue + 2147483648) >> 32;
}

int32_t computeCurrentValueForArpRhythm(int32_t value) {
	return ((int64_t)value * (NUM_PRESET_ARP_RHYTHMS - 1) + 2147483648) >> 32;
}

int32_t computeCurrentValueForArpSeqLength(int32_t value) {
	return ((int64_t)value * kMaxMenuValue + 2147483648) >> 32;
}
