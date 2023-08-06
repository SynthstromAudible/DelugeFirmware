#pragma once

#include <cstdint>

class Pad {
public:
	int32_t x, y;

	Pad(int32_t x_, int32_t y_);
	Pad(uint8_t value);
	uint8_t toChar();
	bool isPad();
	static bool isPad(uint8_t);
};
