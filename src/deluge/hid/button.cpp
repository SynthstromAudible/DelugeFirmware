#include "button.h"
#include "definitions.h"

namespace hid {

namespace button {

struct xy toXY(Button b) {
	int y = (unsigned int)b / 9;
	int x = b - y * 9;
	y -= displayHeight * 2;
	return {x, y};
}

} // namespace button

/*
Button::Button(uint8_t value) {
	y = (unsigned int)value / 9;
	x = value - y * 9;
	y -= displayHeight * 2;
}

bool Button::isButton() {
	return y >= displayHeight;
}

bool Button::isButton(uint8_t value) {
	return value >= displayHeight * 2 * 9;
}

bool Button::operator==(Button const& other) {
	return x == other.x and y == other.y;
}
*/

} // namespace hid
