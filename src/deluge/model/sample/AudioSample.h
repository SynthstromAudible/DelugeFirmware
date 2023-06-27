/*
 * Copyright © 2014 Synthstrom Audible Limited
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

// Ok, creating a class for this is absolutely stupid, but I was a noob at the time! It doesn't add any performance overhead though.

#ifndef AUDIOSAMPLE_H
#define AUDIOSAMPLE_H

#include "functions.h"

class StereoSample {
public:
	StereoSample() {}
	inline void addMono(int32_t sampleValue) {
		l += sampleValue;
		r += sampleValue;
	}

	inline void addPannedMono(int32_t sampleValue, int32_t amplitudeL, int32_t amplitudeR) {
		l += (multiply_32x32_rshift32(sampleValue, amplitudeL) << 2);
		r += (multiply_32x32_rshift32(sampleValue, amplitudeR) << 2);
	}

	inline void addStereo(int32_t sampleValueL, int32_t sampleValueR) {
		l += sampleValueL;
		r += sampleValueR;
	}

	inline void addPannedStereo(int32_t sampleValueL, int32_t sampleValueR, int32_t amplitudeL, int32_t amplitudeR) {
		l += (multiply_32x32_rshift32(sampleValueL, amplitudeL) << 2);
		r += (multiply_32x32_rshift32(sampleValueR, amplitudeR) << 2);
	}

	int32_t l;
	int32_t r;
};

#endif // AUDIOSAMPLE_H
