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

// Ok, creating a class for this is absolutely stupid, but I was a noob at the time! It doesn't add any performance
// overhead though.

#pragma once

#include "util/functions.h"

struct StereoSample {
	[[gnu::always_inline]] void addMono(q31_t sampleValue) {
		l += sampleValue;
		r += sampleValue;
	}

	// Amplitude is probably Q2.29?
	[[gnu::always_inline]] void addPannedMono(q31_t sampleValue, int32_t amplitudeL, int32_t amplitudeR) {
		l += (multiply_32x32_rshift32(sampleValue, amplitudeL) << 2);
		r += (multiply_32x32_rshift32(sampleValue, amplitudeR) << 2);
	}

	[[gnu::always_inline]] void addStereo(q31_t sampleValueL, q31_t sampleValueR) {
		l += sampleValueL;
		r += sampleValueR;
	}

	[[gnu::always_inline]] constexpr StereoSample operator+(const StereoSample& rhs) const {
		return StereoSample{
		    .l = l + rhs.l,
		    .r = r + rhs.r,
		};
	}

	[[gnu::always_inline]] constexpr StereoSample& operator+=(const StereoSample& rhs) {
		l = l + rhs.l;
		r = r + rhs.r;
		return *this;
	}

	// Amplitude is probably Q2.29?
	[[gnu::always_inline]] void addPannedStereo(q31_t sampleValueL, q31_t sampleValueR, int32_t amplitudeL,
	                                            int32_t amplitudeR) {
		l += (multiply_32x32_rshift32(sampleValueL, amplitudeL) << 2);
		r += (multiply_32x32_rshift32(sampleValueR, amplitudeR) << 2);
	}

	// Sean: don't initialize l and r to 0 as this introduces performance issues
	q31_t l;
	q31_t r;
};
