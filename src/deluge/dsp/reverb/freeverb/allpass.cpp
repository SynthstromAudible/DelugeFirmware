// Allpass filter implementation
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

#include "dsp/reverb/freeverb/allpass.hpp"

allpass::allpass() {
	bufidx = 0;
}

void allpass::setbuffer(int32_t* buf, int32_t size) {
	buffer = (int32_t*)buf;
	bufsize = size;
}

void allpass::mute() {
	for (int32_t i = 0; i < bufsize; i++) {
		buffer[i] = 0;
	}
}

void allpass::setfeedback(float val) {
	feedback = val * 2147483648u;
}

float allpass::getfeedback() {
	return (float)feedback / 2147483648u;
}

// ends
