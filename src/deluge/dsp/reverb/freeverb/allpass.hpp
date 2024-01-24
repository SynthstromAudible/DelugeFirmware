// Allpass filter declaration
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "util/functions.h"
#include <cstdint>

class allpass {
public:
	allpass();
	void setbuffer(int32_t* buf, int32_t size);
	inline int32_t process(int32_t inp);
	void mute();
	void setfeedback(float val);
	float getfeedback();
	// private:
	int32_t feedback;
	int32_t* buffer;
	int32_t bufsize;
	int32_t bufidx;
};

// Big to inline - but crucial for speed
inline int32_t allpass::process(int32_t input) {
	int32_t output;
	int32_t bufout;

	bufout = buffer[bufidx];

	output = -input + bufout;
	buffer[bufidx] = input + (bufout >> 1); // Shortcut - because feedback was always one half by default anyway
	// buffer[bufidx] = input + (multiply_32x32_rshift32_rounded(bufout, feedback) << 1);

	if (++bufidx >= bufsize)
		bufidx = 0;

	return output;
}

// ends
