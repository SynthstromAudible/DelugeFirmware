#pragma once

#include "RZA1/system/r_typedefs.h"

namespace hid {

class Button {
public:
	int x, y;

	Button(uint8_t value);
	Button(int x, int y);
	uint8_t toChar();
	bool isButton();
	static bool isButton(uint8_t value);
};
} // namespace hid
