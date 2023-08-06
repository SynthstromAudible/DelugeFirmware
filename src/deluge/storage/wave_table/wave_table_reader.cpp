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

#include "storage/wave_table/wave_table_reader.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/storage_manager.h"

WaveTableReader::WaveTableReader() {
	// TODO Auto-generated constructor stub
}

int32_t WaveTableReader::readBytesPassedErrorChecking(char* outputBuffer, int32_t num) {

	while (num--) {
		int32_t error = advanceClustersIfNecessary();
		if (error) {
			return error;
		}

		*outputBuffer = storageManager.fileClusterBuffer[byteIndexWithinCluster];
		outputBuffer++;
		byteIndexWithinCluster++;
	}

	return NO_ERROR;
}

int32_t WaveTableReader::readNewCluster() {

	UINT bytesRead;
	FRESULT result = f_read(&fileSystemStuff.currentFile, storageManager.fileClusterBuffer,
	                        audioFileManager.clusterSize, &bytesRead);
	if (result) {
		return ERROR_SD_CARD; // Failed to load cluster from card
	}
	else {
		return NO_ERROR;
	}
}
