/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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
#include <cstdint>

class LiveInputBuffer {
public:
	LiveInputBuffer();
	~LiveInputBuffer() = default;
	void giveInput(int32_t numSamples, uint32_t currentTime, OscType inputType);
	bool getAveragesForCrossfade(int32_t* totals, int32_t startPos, int32_t lengthToAverageEach, int32_t numChannels);

	uint32_t upToTime;

	uint32_t numRawSamplesProcessed;

	int32_t lastSampleRead;
	int32_t lastAngle;
	int32_t angleLPFMem[kDifferenceLPFPoles];

	uint8_t percBuffer[kInputPercBufferSize];

	// Must be last!!! Cos we're gonna allocate and access it double-length for stereo
	int32_t rawBuffer[kInputRawBufferSize];
};
