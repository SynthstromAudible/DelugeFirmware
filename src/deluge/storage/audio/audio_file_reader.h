/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

class AudioFile;

// The reason we have this class and don't do it all in the AudioFile is that this class contains variables that are
// only needed during the reading process.

class AudioFileReader {
public:
	AudioFileReader();
	Error readBytes(char* outputBuffer, int32_t num);
	virtual Error readBytesPassedErrorChecking(char* outputBuffer, int32_t num) = 0;
	void jumpForwardToBytePos(uint32_t newPos);
	uint32_t getBytePos();
	Error advanceClustersIfNecessary();
	virtual Error readNewCluster() = 0;

	int32_t currentClusterIndex;
	int32_t byteIndexWithinCluster;
	uint32_t fileSize;
	AudioFile* audioFile;
};
