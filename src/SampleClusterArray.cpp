/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include <SampleClusterArray.h>
#include "SampleCluster.h"
#include "definitions.h"
#include <new>

SampleClusterArray::SampleClusterArray() : ResizeableArray(sizeof(SampleCluster)) {
}

int SampleClusterArray::insertSampleClustersAtEnd(int numToInsert) {
	int oldNum = getNumElements();
	int error = insertAtIndex(oldNum, numToInsert);
	if (error) return error;

	for (int i = oldNum; i < oldNum + numToInsert; i++) {
		void* address = getElementAddress(i);
		SampleCluster* sampleCluster = new (address) SampleCluster();
	}

	return NO_ERROR;
}

SampleCluster* SampleClusterArray::getElement(int i) {
	return (SampleCluster*)getElementAddress(i);
}
