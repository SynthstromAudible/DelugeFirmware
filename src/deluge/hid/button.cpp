#include "button.h"
#include "definitions.h"

namespace hid {

Button::Button(uint8_t value) {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	x = (unsigned int)value % 10;
	y = ((unsigned int)value % 70) / 10;
	y -= displayHeight;
#else
	y = (unsigned int)value / 9;
	x = value - y * 9;
	y -= displayHeight * 2;
#endif
}

Button::Button(int x_, int y_) {
	x = x_;
	y = y_;
}

uint8_t Button::toChar() {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	return 10 * (y + displayHeight) + x;
#else
	return 9 * (y + displayHeight * 2) + x;
#endif
}

bool Button::isButton() {
	return y >= displayHeight;
}

bool Button::isButton(uint8_t value) {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	return value >= displayHeight * 10;
#else
	return value >= displayHeight * 2 * 9;
#endif
}

bool Button::operator==(Button const& other) {
	return x == other.x and y == other.y;
}

} // namespace hid
