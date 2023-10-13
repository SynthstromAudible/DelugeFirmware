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

#include "chunkware_simplecomp.h"
#include "definitions_cxx.hpp"

#define INLINE inline
#include <algorithm> // for min(), max()
#include <cassert>   // for assert()
#include <cmath>
#include <cstdint>

class MasterCompressor {
public:
	MasterCompressor();
	void setup(int32_t attack, int32_t release, int32_t threshold, int32_t ratio, int32_t makeup, int32_t mix);

	void render(StereoSample* buffer, uint16_t numSamples, int32_t masterVolumeAdjustmentL,
	            int32_t masterVolumeAdjustmentR);
	float makeup;
	float gr;
	float wet;
	inline void setMakeup(float dB) {
		makeup = pow(10.0, (dB / 20.0));
		if (fabs(1.0 - makeup) < 0.0001)
			makeup = 1.0;
		if (makeup > 20.0)
			makeup = 20.0;
		if (makeup < 0.0001)
			makeup = 0.0;
	}
	inline float getMakeup() { return 20.0 * log10(makeup); }

	chunkware_simple::SimpleComp compressor;
};
