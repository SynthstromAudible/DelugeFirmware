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

#include <AudioFileManager.h>
#include <SampleReader.h>
#include "Sample.h"
#include "Cluster.h"

SampleReader::SampleReader() {
	// TODO Auto-generated constructor stub
}

int SampleReader::readBytesPassedErrorChecking(char* outputBuffer, int num) {

	while (num--) {
		int error = advanceClustersIfNecessary();
		if (error) return error;

		*outputBuffer = currentCluster->data[byteIndexWithinCluster];
		outputBuffer++;
		byteIndexWithinCluster++;
	}

	return NO_ERROR;
}

int SampleReader::readNewCluster() {
	if (currentCluster) audioFileManager.removeReasonFromCluster(currentCluster, "E031");

	currentCluster = ((Sample*)audioFile)
	                     ->clusters.getElement(currentClusterIndex)
	                     ->getCluster((Sample*)audioFile, currentClusterIndex, CLUSTER_LOAD_IMMEDIATELY);
	if (!currentCluster) return ERROR_SD_CARD; // Failed to load cluster from card
	else return NO_ERROR;
}
