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
#include "definitions_cxx.hpp"
#include "storage/audio/audio_file_manager.h"
#include "storage/storage_manager.h"

Error WaveTableReader::readBytesPassedErrorChecking(char* outputBuffer, int32_t num) {

	while (num--) {
		Error error = advanceClustersIfNecessary();
		if (error != Error::NONE) {
			return error;
		}

		*outputBuffer = smDeserializer.fileClusterBuffer[byteIndexWithinCluster];
		outputBuffer++;
		byteIndexWithinCluster++;
	}

	return Error::NONE;
}

Error WaveTableReader::readNewCluster() {

	UINT bytesRead;
	FRESULT result =
	    f_read(&smDeserializer.readFIL, smDeserializer.fileClusterBuffer, audioFileManager.clusterSize, &bytesRead);
	if (result) {
		return Error::SD_CARD; // Failed to load cluster from card
	}
	return Error::NONE;
}
