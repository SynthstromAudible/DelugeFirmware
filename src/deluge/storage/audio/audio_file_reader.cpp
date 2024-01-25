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

#include "storage/audio/audio_file_reader.h"
#include "storage/audio/audio_file_manager.h"

AudioFileReader::AudioFileReader() {
	// TODO Auto-generated constructor stub
}

// One limitation of this function is that it can never read the final byte of the file. Not a problem for us
int32_t AudioFileReader::readBytes(char* outputBuffer, int32_t num) {
	if ((uint32_t)(getBytePos() + num) > fileSize) {
		return ERROR_FILE_CORRUPTED;
	}

	return readBytesPassedErrorChecking(outputBuffer, num);
}

// Check it's not beyond the end of the file, before you call this
void AudioFileReader::jumpForwardToBytePos(uint32_t newPos) {

	uint32_t oldPos = getBytePos();
	uint32_t jumpForwardAmount = newPos - oldPos;
	byteIndexWithinCluster += jumpForwardAmount;
}

uint32_t AudioFileReader::getBytePos() {
	return byteIndexWithinCluster + currentClusterIndex * audioFileManager.clusterSize;
}

int32_t AudioFileReader::advanceClustersIfNecessary() {

	int32_t numClustersToAdvance = byteIndexWithinCluster >> audioFileManager.clusterSizeMagnitude;

	if (!numClustersToAdvance) {
		return NO_ERROR;
	}

	currentClusterIndex += numClustersToAdvance;
	byteIndexWithinCluster &= audioFileManager.clusterSize - 1;

	return readNewCluster();
}
