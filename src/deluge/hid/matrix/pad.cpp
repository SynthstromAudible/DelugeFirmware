#include "pad.h"
#include "definitions.h"

Pad::Pad(int x_, int y_) : x(x_), y(y_) {
}

Pad::Pad(uint8_t value) {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	x = (unsigned int)value % 10;
	y = ((unsigned int)value % 70) / 10;
#else
	y = (unsigned int)value / 9;
	x = (value - y * 9) << 1;

	if (y >= displayHeight) {
		y -= displayHeight;
		x++;
	}
#endif
}

uint8_t Pad::toChar() {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	return y * 10 + x;
#else
	uint8_t value = 9 * y + (x / 2);
	if ((x % 2) == 1) {
		value += 9 * displayHeight;
	}
	return value;
#endif
}

bool Pad::isPad() {
	return y < displayHeight && x < displayWidth + sideBarWidth;
}

bool Pad::isPad(uint8_t value) {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	return (value % 70) < (displayHeight * 10);
#else
	return value < (displayHeight * 2 * 9);
#endif
}
