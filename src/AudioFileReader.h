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

#ifndef AUDIOFILEREADER_H_
#define AUDIOFILEREADER_H_

#include "r_typedefs.h"

class AudioFile;

// The reason we have this class and don't do it all in the AudioFile is that this class contains variables that are only needed during the reading process.

class AudioFileReader {
public:
	AudioFileReader();
	int readBytes(char* outputBuffer, int num);
	virtual int readBytesPassedErrorChecking(char* outputBuffer, int num) = 0;
	void jumpForwardToBytePos(uint32_t newPos);
	uint32_t getBytePos();
	int advanceClustersIfNecessary();
	virtual int readNewCluster() = 0;

	int currentClusterIndex;
	int byteIndexWithinCluster;
	uint32_t fileSize;
	AudioFile* audioFile;
};

#endif /* AUDIOFILEREADER_H_ */
