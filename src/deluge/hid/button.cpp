#include "button.h"
#include "definitions.h"

namespace hid {

namespace button {

struct xy toXY(Button b) {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	int x = (unsigned int)b % 10;
	int y = ((unsigned int)b % 70) / 10;
	y -= displayHeight;
#else
	int y = (unsigned int)b / 9;
	int x = b - y * 9;
	y -= displayHeight * 2;
#endif
	return {x, y};
}

}

	/*
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
*/

} // namespace hid
