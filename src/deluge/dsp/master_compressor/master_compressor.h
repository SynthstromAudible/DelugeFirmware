/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "dsp/compressor/compressor.h"
#include "util/functions.h"

#define INLINE inline
#include <algorithm> // for min(), max()
#include <cassert>   // for assert()
#include <cmath>
#include <cstdint>

class MasterCompressor : public Compressor {
public:
	MasterCompressor();
	void setup(int32_t attack, int32_t release, int32_t threshold, int32_t ratio, int32_t makeup, int32_t mix);

	void render(StereoSample* buffer, uint16_t numSamples);
	q31_t calc_rms(StereoSample* buffer, uint16_t numSamples);
	uint8_t gr;
	q31_t threshold;
	q31_t shape;
	q31_t amount;
	q31_t out;
	q31_t meanVolume;
	q31_t over;
	q31_t finalVolume;
	q31_t currentVolume;
	q31_t amplitudeIncrement;
	q31_t mean;
};
