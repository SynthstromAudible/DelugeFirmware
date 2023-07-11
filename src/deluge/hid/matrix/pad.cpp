#include "pad.h"
#include "definitions.h"

Pad::Pad(int x_, int y_) : x(x_), y(y_) {
}

Pad::Pad(uint8_t value) {
	y = (unsigned int)value / 9;
	x = (value - y * 9) << 1;

	if (y >= displayHeight) {
		y -= displayHeight;
		x++;
	}
}

uint8_t Pad::toChar() {
	uint8_t value = 9 * y + (x / 2);
	if ((x % 2) == 1) {
		value += 9 * displayHeight;
	}
	return value;
}

bool Pad::isPad() {
	return y < displayHeight && x < displayWidth + sideBarWidth;
}

bool Pad::isPad(uint8_t value) {
	return value < (displayHeight * 2 * 9);
}
