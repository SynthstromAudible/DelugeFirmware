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

#include "dsp/stereo_sample.h"
#include "util/fixedpoint.h"
#include <cstdint>

extern const int32_t ir[];

#define IR_SIZE 26
#define IR_BUFFER_SIZE (IR_SIZE - 1)

class ImpulseResponseProcessor {
public:
	ImpulseResponseProcessor();

	StereoSample buffer[IR_BUFFER_SIZE];

	inline void process(int32_t inputL, int32_t inputR, int32_t* outputL, int32_t* outputR) {

		*outputL = buffer[0].l + multiply_32x32_rshift32_rounded(inputL, ir[0]);
		*outputR = buffer[0].r + multiply_32x32_rshift32_rounded(inputR, ir[0]);

		for (int32_t i = 1; i != IR_BUFFER_SIZE; i++) {
			buffer[i - 1].l = buffer[i].l + multiply_32x32_rshift32_rounded(inputL, ir[i]);
			buffer[i - 1].r = buffer[i].r + multiply_32x32_rshift32_rounded(inputR, ir[i]);
		}

		buffer[IR_BUFFER_SIZE - 1].l = multiply_32x32_rshift32_rounded(inputL, ir[IR_BUFFER_SIZE]);
		buffer[IR_BUFFER_SIZE - 1].r = multiply_32x32_rshift32_rounded(inputR, ir[IR_BUFFER_SIZE]);
	}
};
