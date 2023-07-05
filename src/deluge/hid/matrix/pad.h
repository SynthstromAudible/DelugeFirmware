#pragma once

#include "RZA1/system/r_typedefs.h"

class Pad {
public:
	int x, y;

	Pad(int x_, int y_);
	Pad(uint8_t value);
	uint8_t toChar();
	bool isPad();
	static bool isPad(uint8_t);
};
