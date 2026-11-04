#pragma once
#include <cstdint>

constexpr uint32_t rshift_round(uint32_t value, uint32_t rshift) {
	// value / 2^rshift rounded half up: the shifted value plus the bit just below it, which cannot overflow as adding
	// the half first would near UINT32_MAX
	return (value >> rshift) + ((value >> (rshift - 1)) & 1);
}

/** Unlike %, guarantees a non-negative result. */
inline constexpr int32_t mod(int32_t a, int32_t b) {
	return ((a % b) + b) % b;
}
