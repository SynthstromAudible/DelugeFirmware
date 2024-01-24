// Comb filter class declaration
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

class comb {
public:
	comb();
	void setbuffer(int32_t* buf, int32_t size);
	inline int32_t process(int32_t inp);
	void mute();
	void setdamp(float val);
	float getdamp();
	void setfeedback(int32_t val);
	int32_t getfeedback();

private:
	int32_t feedback;
	int32_t filterstore;
	int32_t damp1;
	int32_t damp2;
	int32_t* buffer;
	int32_t bufsize;
	int32_t bufidx;
};

// Big to inline - but crucial for speed

inline int32_t comb::process(int32_t input) {
	int32_t output;

	output = buffer[bufidx];

	filterstore = (multiply_32x32_rshift32_rounded(output, damp2) + multiply_32x32_rshift32_rounded(filterstore, damp1))
	              << 1;

	buffer[bufidx] = input + (multiply_32x32_rshift32_rounded(filterstore, feedback) << 1);

	if (++bufidx >= bufsize)
		bufidx = 0;

	return output;
}

// ends
