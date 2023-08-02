#include "pad.h"
#include "definitions_cxx.hpp"

Pad::Pad(int32_t x_, int32_t y_) : x(x_), y(y_) {
}

Pad::Pad(uint8_t value) {
	y = (uint32_t)value / 9;
	x = (value - y * 9) << 1;

	if (y >= kDisplayHeight) {
		y -= kDisplayHeight;
		x++;
	}
}

uint8_t Pad::toChar() {
	uint8_t value = 9 * y + (x / 2);
	if ((x % 2) == 1) {
		value += 9 * kDisplayHeight;
	}
	return value;
}

bool Pad::isPad() {
	return y < kDisplayHeight && x < kDisplayWidth + kSideBarWidth;
}

bool Pad::isPad(uint8_t value) {
	return value < (kDisplayHeight * 2 * 9);
}
