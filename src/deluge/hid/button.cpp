#include "button.h"
#include "definitions_cxx.hpp"

namespace hid {

namespace button {

Cartesian toXY(Button b) {
	int y = (unsigned int)b / 9;
	int x = b - y * 9;
	y -= kDisplayHeight * 2;
	return {x, y};
}

} // namespace button

/*
Button::Button(uint8_t value) {
	y = (unsigned int)value / 9;
	x = value - y * 9;
	y -= kDisplayHeight * 2;
}

bool Button::isButton() {
	return y >= kDisplayHeight;
}

bool Button::isButton(uint8_t value) {
	return value >= kDisplayHeight * 2 * 9;
}

bool Button::operator==(Button const& other) {
	return x == other.x and y == other.y;
}
*/

} // namespace hid
