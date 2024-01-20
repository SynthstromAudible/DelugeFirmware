#pragma once
#include <cstdint>

constexpr uint32_t rshift_round(uint32_t value, uint32_t rshift) {
	return (value + (1 << (rshift - 1))) >> rshift; // (Rohan) I never was quite 100% sure of this...
}

constexpr int32_t rshift_round_signed(int32_t value, uint32_t rshift) {
	return (value + (1 << (rshift - 1))) >> rshift; // (Rohan) I never was quite 100% sure of this...
}
